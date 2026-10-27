// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CTestContentFolder.h"
#include "Tests/N2CToolSpecHarness.h"
#include "BlueprintLibraries/N2CTagBlueprintLibrary.h"
#include "Core/N2CGraphStateTypes.h"
#include "Core/N2CGraphStateManager.h"
#include "Models/N2CTaggedBlueprintGraph.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/Package.h"

namespace N2CTagGraphsAndFiltersTest
{
	const TCHAR* Folder = TEXT("/Game/__N2CTestTagGraphsAndFilters");

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

	/** list-blueprint-tags with the given filters: the listed {tag, category} pairs, or false when the call failed */
	bool ListTags(FAutomationTestBase& Test, const FString& What, const TMap<FString, FString>& Filters,
		TArray<TPair<FString, FString>>& OutTags, TSharedPtr<FJsonObject>& OutApplied)
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		for (const TPair<FString, FString>& Filter : Filters)
		{
			Args->SetStringField(Filter.Key, Filter.Value);
		}
		const FMcpToolCallResult Result = N2CTest::RunToolUnmarked(TEXT("list-blueprint-tags"), Args);
		const TSharedPtr<FJsonObject> Json = N2CTest::ResultJson(Result);
		if (!Test.TestFalse(What + TEXT(": not an error"), Result.bIsError) || !Test.TestNotNull(What + TEXT(": JSON result"), Json.Get()))
		{
			return false;
		}
		OutTags.Reset();
		const TArray<TSharedPtr<FJsonValue>>* Tags = nullptr;
		if (Json->TryGetArrayField(TEXT("tags"), Tags))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Tags)
			{
				const TSharedPtr<FJsonObject> Tag = Value.IsValid() ? Value->AsObject() : nullptr;
				if (Tag.IsValid())
				{
					OutTags.Emplace(Tag->GetStringField(TEXT("tag")), Tag->GetStringField(TEXT("category")));
				}
			}
		}
		const TSharedPtr<FJsonObject>* Applied = nullptr;
		OutApplied = Json->TryGetObjectField(TEXT("appliedFilters"), Applied) ? *Applied : nullptr;
		return true;
	}
}

/**
 * Card 0294. list-blueprint-tags applies graphGuid, tag and category together: with two tags on one graph, {graphGuid,
 * tag} lists one entry (before, graphGuid ignored the other filters and listed both while appliedFilters still named
 * the tag). UN2CTagBlueprintLibrary::TagBlueprintGraph finds every graph of the Blueprint (GetAllGraphs), so a macro
 * graph can be tagged by name and the tag info names the matched graph's GUID. Runs on every engine (no editor opened).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FN2CTagGraphsAndFiltersTest, "NodeToCode.Tags.GraphsAndFilters", N2C_TEST_FLAGS)

bool FN2CTagGraphsAndFiltersTest::RunTest(const FString& Parameters)
{
	using namespace N2CTagGraphsAndFiltersTest;

	const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString TagA = FString::Printf(TEXT("N2CTag0294A_%s"), *Suffix);
	const FString TagB = FString::Printf(TEXT("N2CTag0294B_%s"), *Suffix);
	const FString CategoryA = TEXT("N2CTag0294Alpha");
	const FString CategoryB = TEXT("N2CTag0294Beta");
	UN2CGraphStateManager& Tags = UN2CGraphStateManager::Get();

	N2CTest::DeleteContentFolder(Folder);
	FN2CGraphKey EventKey;
	FN2CGraphKey MacroKey;
	ON_SCOPE_EXIT
	{
		// Never leave test tags in the host's graph state
		if (EventKey.IsValid())
		{
			Tags.RemoveTag(EventKey, TagA, CategoryA);
			Tags.RemoveTag(EventKey, TagB, CategoryB);
		}
		if (MacroKey.IsValid())
		{
			Tags.RemoveTag(MacroKey, TagA, CategoryA);
		}
		Tags.SaveState();
		N2CTest::DeleteContentFolder(Folder);
	};

	UBlueprint* Blueprint = MakeBlueprintAsset(TEXT("BP_N2CTagGraphsAndFilters"));
	UEdGraph* EventGraph = Blueprint ? FBlueprintEditorUtils::FindEventGraph(Blueprint) : nullptr;
	if (!TestNotNull(TEXT("Blueprint and its EventGraph"), EventGraph))
	{
		return false;
	}
	UEdGraph* MacroGraph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, FName(TEXT("N2CTagMacro")),
		UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	if (!TestNotNull(TEXT("macro graph"), MacroGraph))
	{
		return false;
	}
	FBlueprintEditorUtils::AddMacroGraph(Blueprint, MacroGraph, /*bIsUserCreated*/ true, nullptr);
	TestTrue(TEXT("the macro graph has a GUID"), MacroGraph->GraphGuid.IsValid());
	EventKey = FN2CGraphKey::FromGraph(EventGraph);
	MacroKey = FN2CGraphKey::FromGraph(MacroGraph);

	// A macro graph can be tagged by name, and the tag info names the matched graph
	bool bSuccess = false;
	FString Error;
	const FN2CTagInfo MacroInfo = UN2CTagBlueprintLibrary::TagBlueprintGraph(Blueprint, MacroGraph->GetName(), TagA,
		CategoryA, TEXT(""), bSuccess, Error);
	TestTrue(FString::Printf(TEXT("tag the macro graph by name (%s)"), *Error), bSuccess);
	TestEqual(TEXT("the tag info names the macro graph's GUID"), MacroInfo.GraphGuid,
		MacroGraph->GraphGuid.ToString(EGuidFormats::DigitsWithHyphens));
	TestEqual(TEXT("the tag info names the macro graph"), MacroInfo.GraphName, MacroGraph->GetName());
	TestTrue(TEXT("the macro graph has the tag"), Tags.GraphHasTag(MacroKey, TagA, CategoryA));

	// An unknown graph name still fails
	bSuccess = true;
	UN2CTagBlueprintLibrary::TagBlueprintGraph(Blueprint, TEXT("N2CNoSuchGraph"), TagA, CategoryA, TEXT(""), bSuccess, Error);
	TestFalse(TEXT("an unknown graph name is refused"), bSuccess);

	// Two tags on the event graph
	UN2CTagBlueprintLibrary::TagBlueprintGraph(Blueprint, EventGraph->GetName(), TagA, CategoryA, TEXT(""), bSuccess, Error);
	TestTrue(TEXT("tag A on the event graph"), bSuccess);
	UN2CTagBlueprintLibrary::TagBlueprintGraph(Blueprint, EventGraph->GetName(), TagB, CategoryB, TEXT(""), bSuccess, Error);
	TestTrue(TEXT("tag B on the event graph"), bSuccess);

	const FString EventGuid = EventGraph->GraphGuid.ToString(EGuidFormats::DigitsWithHyphens);
	TArray<TPair<FString, FString>> Listed;
	TSharedPtr<FJsonObject> Applied;

	if (ListTags(*this, TEXT("graphGuid alone"), {{TEXT("graphGuid"), EventGuid}}, Listed, Applied))
	{
		TestEqual(TEXT("graphGuid alone lists both tags of the graph"), Listed.Num(), 2);
	}

	if (ListTags(*this, TEXT("graphGuid and tag"), {{TEXT("graphGuid"), EventGuid}, {TEXT("tag"), TagA.ToLower()}}, Listed, Applied))
	{
		TestEqual(TEXT("{graphGuid, tag} lists one entry"), Listed.Num(), 1);
		TestTrue(TEXT("{graphGuid, tag} lists tag A"), Listed.Num() == 1 && Listed[0].Key == TagA);
		TestTrue(TEXT("appliedFilters names graphGuid and tag"), Applied.IsValid()
			&& Applied->HasField(TEXT("graphGuid")) && Applied->HasField(TEXT("tag")));
	}

	if (ListTags(*this, TEXT("graphGuid and category"), {{TEXT("graphGuid"), EventGuid}, {TEXT("category"), CategoryB}}, Listed, Applied))
	{
		TestTrue(TEXT("{graphGuid, category} lists only tag B"), Listed.Num() == 1 && Listed[0].Key == TagB);
	}

	if (ListTags(*this, TEXT("graphGuid, tag and another category"),
		{{TEXT("graphGuid"), EventGuid}, {TEXT("tag"), TagA}, {TEXT("category"), CategoryB}}, Listed, Applied))
	{
		TestEqual(TEXT("{graphGuid, tag A, category B} lists nothing"), Listed.Num(), 0);
	}

	if (ListTags(*this, TEXT("tag across graphs"), {{TEXT("tag"), TagA}}, Listed, Applied))
	{
		TestEqual(TEXT("tag A alone lists the event and macro graphs"), Listed.Num(), 2);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
