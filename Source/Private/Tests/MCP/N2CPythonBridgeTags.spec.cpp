// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CTestContentFolder.h"
#include "MCP/Python/N2CPythonBridge.h"
#include "Core/N2CGraphStateTypes.h"
#include "Core/N2CTagManager.h"
#include "Models/N2CTaggedBlueprintGraph.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "IAssetTools.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"

namespace N2CBridgeTagsSpec
{
	const TCHAR* Folder = TEXT("/Game/__N2CTestBridgeTags");

	/** An Actor Blueprint asset known to the Asset Registry (in memory, not saved) */
	UBlueprint* MakeBlueprintAsset(const FString& Name)
	{
		UPackage* Package = CreatePackage(*FString::Printf(TEXT("%s/%s"), Folder, *Name));
		UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), Package, FName(*Name),
			BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
		if (Blueprint)
		{
			FAssetRegistryModule::AssetCreated(Blueprint);
		}
		return Blueprint;
	}

	/** The bridge's JSON envelope: {success, data, error} */
	TSharedPtr<FJsonObject> Parse(const FString& Json)
	{
		TSharedPtr<FJsonObject> Object;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		return FJsonSerializer::Deserialize(Reader, Object) ? Object : nullptr;
	}

	bool Succeeded(const TSharedPtr<FJsonObject>& Envelope)
	{
		bool bSuccess = false;
		return Envelope.IsValid() && Envelope->TryGetBoolField(TEXT("success"), bSuccess) && bSuccess;
	}

	TSharedPtr<FJsonObject> Data(const TSharedPtr<FJsonObject>& Envelope)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		return Envelope.IsValid() && Envelope->TryGetObjectField(TEXT("data"), Object) ? *Object : nullptr;
	}
}

/**
 * The Python bridge names a graph by {Blueprint, GraphGuid} for its tags (card 0498; card 0192 keyed the graph state
 * that way). A duplicated Blueprint keeps the original's graph GUIDs, so RemoveTag by GUID alone is ambiguous once both
 * are tagged: it must fail with a message that asks for the Blueprint path and remove nothing. With the copy's path it
 * removes the copy's tag and the original keeps its own; ListTags filtered by a Blueprint path lists only that
 * Blueprint's graphs. Runs on every engine (no editor is opened).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FN2CPythonBridgeTagsTest, "NodeToCode.MCP.PythonBridgeTags", N2C_TEST_FLAGS)

bool FN2CPythonBridgeTagsTest::RunTest(const FString& Parameters)
{
	using namespace N2CBridgeTagsSpec;

	// The GUID-only removal below is ambiguous on purpose; the graph state manager warns about it
	AddExpectedMessage(TEXT("is in more than one Blueprint"), EAutomationExpectedMessageFlags::Contains, 0);

	const FString Tag = FString::Printf(TEXT("N2CBridgeTag0498_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString Category = TEXT("N2CBridgeTags");
	UN2CTagManager& Tags = UN2CTagManager::Get();

	N2CTest::DeleteContentFolder(Folder);
	FN2CGraphKey OriginalKey;
	FN2CGraphKey CopyKey;
	ON_SCOPE_EXIT
	{
		// Never leave test tags in the host's graph state
		if (OriginalKey.IsValid())
		{
			Tags.RemoveTag(OriginalKey, Tag, Category);
		}
		if (CopyKey.IsValid())
		{
			Tags.RemoveTag(CopyKey, Tag, Category);
		}
		Tags.SaveTags();
		N2CTest::DeleteContentFolder(Folder);
	};

	// 1. A Blueprint and its Content Browser duplicate: the copy keeps the original's EventGraph GUID
	UBlueprint* Original = MakeBlueprintAsset(TEXT("BP_N2CBridgeTags"));
	UEdGraph* OriginalGraph = Original ? FBlueprintEditorUtils::FindEventGraph(Original) : nullptr;
	if (!TestNotNull(TEXT("original Blueprint and its EventGraph"), OriginalGraph))
	{
		return false;
	}
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	UBlueprint* Copy = Cast<UBlueprint>(AssetTools.DuplicateAsset(TEXT("BP_N2CBridgeTagsCopy"), Folder, Original));
	UEdGraph* CopyGraph = Copy ? FBlueprintEditorUtils::FindEventGraph(Copy) : nullptr;
	if (!TestNotNull(TEXT("the duplicate and its EventGraph"), CopyGraph)
		|| !TestEqual(TEXT("the copy keeps the graph GUID"), CopyGraph->GraphGuid, OriginalGraph->GraphGuid))
	{
		return false;
	}
	const FString Guid = OriginalGraph->GraphGuid.ToString();
	const FString OriginalPath = FSoftObjectPath(Original).ToString();
	const FString CopyPath = FSoftObjectPath(Copy).ToString();
	OriginalKey = FN2CGraphKey::FromGraph(OriginalGraph);
	CopyKey = FN2CGraphKey::FromGraph(CopyGraph);

	// 2. Tag both with the same tag
	TestTrue(TEXT("tag the original"), Tags.AddTag(FN2CTaggedBlueprintGraph(Tag, Category, TEXT(""), OriginalGraph->GraphGuid,
		OriginalGraph->GetName(), FSoftObjectPath(Original))));
	TestTrue(TEXT("tag the copy"), Tags.AddTag(FN2CTaggedBlueprintGraph(Tag, Category, TEXT(""), CopyGraph->GraphGuid,
		CopyGraph->GetName(), FSoftObjectPath(Copy))));

	// 3. ListTags by Blueprint path lists only that Blueprint's graph
	{
		const TSharedPtr<FJsonObject> Listed = Data(Parse(UN2CPythonBridge::ListTags(TEXT(""), Tag, CopyPath)));
		const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
		if (TestTrue(TEXT("ListTags by the copy's path lists one entry"), Listed.IsValid()
			&& Listed->TryGetArrayField(TEXT("tags"), Entries) && Entries->Num() == 1))
		{
			TestEqual(TEXT("the entry is the copy's"), (*Entries)[0]->AsObject()->GetStringField(TEXT("blueprint_path")), CopyPath);
		}
		const TSharedPtr<FJsonObject> All = Data(Parse(UN2CPythonBridge::ListTags(TEXT(""), Tag)));
		TestTrue(TEXT("ListTags without a path lists both"), All.IsValid() && All->GetIntegerField(TEXT("count")) == 2);
	}

	// 4. By GUID alone the removal is ambiguous: an error that asks for the path, and both keep their tag
	{
		const FString Json = UN2CPythonBridge::RemoveTag(Guid, Tag);
		const TSharedPtr<FJsonObject> Envelope = Parse(Json);
		TestFalse(TEXT("RemoveTag by GUID alone fails: ") + Json, Succeeded(Envelope));
		TestTrue(TEXT("the error asks for blueprint_path: ") + Json, Json.Contains(TEXT("blueprint_path")));
		TestTrue(TEXT("the original keeps its tag"), Tags.GraphHasTag(OriginalKey, Tag, Category));
		TestTrue(TEXT("the copy keeps its tag"), Tags.GraphHasTag(CopyKey, Tag, Category));
	}

	// 5. With the copy's path: the copy's tag goes, the original's stays
	{
		const FString Json = UN2CPythonBridge::RemoveTag(Guid, Tag, CopyPath);
		const TSharedPtr<FJsonObject> Envelope = Parse(Json);
		if (TestTrue(TEXT("RemoveTag with the copy's path succeeds: ") + Json, Succeeded(Envelope)))
		{
			const TSharedPtr<FJsonObject> Removed = Data(Envelope);
			TestEqual(TEXT("removed_count"), Removed->GetIntegerField(TEXT("removed_count")), 1);
			TestEqual(TEXT("blueprint_path names the copy"), Removed->GetStringField(TEXT("blueprint_path")), CopyPath);
			TestEqual(TEXT("the copy has no tags left"), Removed->GetIntegerField(TEXT("remaining_tags")), 0);
		}
		TestFalse(TEXT("the copy lost its tag"), Tags.GraphHasTag(CopyKey, Tag, Category));
		TestTrue(TEXT("the original keeps its tag"), Tags.GraphHasTag(OriginalKey, Tag, Category));
	}

	// 6. A package path (no asset name) names the Blueprint too
	{
		const FString PackagePath = FString::Printf(TEXT("%s/BP_N2CBridgeTags"), Folder);
		const FString Json = UN2CPythonBridge::RemoveTag(Guid, Tag, PackagePath);
		TestTrue(TEXT("RemoveTag with the original's package path succeeds: ") + Json, Succeeded(Parse(Json)));
		TestFalse(TEXT("the original lost its tag"), Tags.GraphHasTag(OriginalKey, Tag, Category));
		TestEqual(TEXT("the original's path is reported"), Data(Parse(Json)).IsValid()
			? Data(Parse(Json))->GetStringField(TEXT("blueprint_path")) : FString(), OriginalPath);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
