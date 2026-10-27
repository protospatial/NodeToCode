// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CTestContentFolder.h"
#include "BlueprintLibraries/N2CTagBlueprintLibrary.h"
#include "Core/N2CGraphStateTypes.h"
#include "Core/N2CTagManager.h"
#include "Models/N2CTaggedBlueprintGraph.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "IAssetTools.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/Package.h"

namespace N2CTagLibraryRemoveTagTest
{
	const TCHAR* Folder = TEXT("/Game/__N2CTestTagLibraryRemove");

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
}

/**
 * UN2CTagBlueprintLibrary::RemoveTag names a graph by {Blueprint, GraphGuid}, as the Python bridge's RemoveTag does
 * (#198, card 0344). A duplicated Blueprint keeps the original's graph GUIDs: once both are tagged, a GUID alone
 * removes nothing, and the copy's Blueprint path removes only the copy's tag. Runs on every engine (no editor opened).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FN2CTagLibraryRemoveTagTest, "NodeToCode.Tags.LibraryRemoveTag", N2C_TEST_FLAGS)

bool FN2CTagLibraryRemoveTagTest::RunTest(const FString& Parameters)
{
	using namespace N2CTagLibraryRemoveTagTest;

	// The GUID-only removal below is ambiguous on purpose; the graph state manager warns about it
	AddExpectedMessage(TEXT("is in more than one Blueprint"), EAutomationExpectedMessageFlags::Contains, 0);

	const FString Tag = FString::Printf(TEXT("N2CLibraryTag0344_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString Category = TEXT("N2CLibraryTags");
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

	UBlueprint* Original = MakeBlueprintAsset(TEXT("BP_N2CLibraryTags"));
	UEdGraph* OriginalGraph = Original ? FBlueprintEditorUtils::FindEventGraph(Original) : nullptr;
	if (!TestNotNull(TEXT("original Blueprint and its EventGraph"), OriginalGraph))
	{
		return false;
	}
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	UBlueprint* Copy = Cast<UBlueprint>(AssetTools.DuplicateAsset(TEXT("BP_N2CLibraryTagsCopy"), Folder, Original));
	UEdGraph* CopyGraph = Copy ? FBlueprintEditorUtils::FindEventGraph(Copy) : nullptr;
	if (!TestNotNull(TEXT("the duplicate and its EventGraph"), CopyGraph)
		|| !TestEqual(TEXT("the copy keeps the graph GUID"), CopyGraph->GraphGuid, OriginalGraph->GraphGuid))
	{
		return false;
	}
	const FString Guid = OriginalGraph->GraphGuid.ToString();
	OriginalKey = FN2CGraphKey::FromGraph(OriginalGraph);
	CopyKey = FN2CGraphKey::FromGraph(CopyGraph);

	TestTrue(TEXT("tag the original"), Tags.AddTag(FN2CTaggedBlueprintGraph(Tag, Category, TEXT(""), OriginalGraph->GraphGuid,
		OriginalGraph->GetName(), FSoftObjectPath(Original))));
	TestTrue(TEXT("tag the copy"), Tags.AddTag(FN2CTaggedBlueprintGraph(Tag, Category, TEXT(""), CopyGraph->GraphGuid,
		CopyGraph->GetName(), FSoftObjectPath(Copy))));

	// By GUID alone the graph is ambiguous: nothing is removed
	TestFalse(TEXT("RemoveTag by GUID alone removes nothing"), UN2CTagBlueprintLibrary::RemoveTag(Guid, Tag, Category));
	TestTrue(TEXT("the original keeps its tag"), Tags.GraphHasTag(OriginalKey, Tag, Category));
	TestTrue(TEXT("the copy keeps its tag"), Tags.GraphHasTag(CopyKey, Tag, Category));

	// The copy's path (as a tag info's Blueprint Path gives it): only the copy's tag goes
	TestTrue(TEXT("RemoveTag with the copy's path"), UN2CTagBlueprintLibrary::RemoveTag(Guid, Tag, Category, FSoftObjectPath(Copy).ToString()));
	TestFalse(TEXT("the copy lost its tag"), Tags.GraphHasTag(CopyKey, Tag, Category));
	TestTrue(TEXT("the original keeps its tag"), Tags.GraphHasTag(OriginalKey, Tag, Category));

	// A package path names the Blueprint too; with one Blueprint left tagged, a bare GUID would also do
	TestTrue(TEXT("RemoveTag with the original's package path"),
		UN2CTagBlueprintLibrary::RemoveTag(Guid, Tag, Category, FString::Printf(TEXT("%s/BP_N2CLibraryTags"), Folder)));
	TestFalse(TEXT("the original lost its tag"), Tags.GraphHasTag(OriginalKey, Tag, Category));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
