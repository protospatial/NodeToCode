// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CTestBlueprintFactory.h"
#include "Utils/N2CGraphUtils.h"
#include "Core/Services/N2CTokenEstimationService.h"
#include "BlueprintLibraries/N2CTagBlueprintLibrary.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_Composite.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"

/**
 * The editor's one tagged-graph resolver (FN2CGraphUtils, card 0251) on a Blueprint built in code: the graphs the
 * overlay can tag (an interface implementation, collapsed graphs at the top and nested in a function) are found by
 * GraphGuid, token estimation reads them, a missing graph's message names it and its Blueprint, and a graph capture
 * cannot read (a non-Blueprint schema, as an AnimBP state machine has) is refused with a reason.
 */
BEGIN_DEFINE_SPEC(FN2CGraphResolverSpec, "NodeToCode.GraphResolver", N2C_TEST_FLAGS)
	TUniquePtr<N2CTest::FTestBlueprint> Test;
	UEdGraph* InterfaceGraph = nullptr;
	UEdGraph* Collapsed = nullptr;
	UEdGraph* NestedCollapsed = nullptr;
	UEdGraph* NotK2 = nullptr;

	FString Path() const { return Test->GetBlueprint()->GetPathName(); }

	FN2CTagInfo TagFor(const UEdGraph* Graph) const
	{
		FN2CTagInfo Info;
		Info.GraphGuid = Graph->GraphGuid.ToString();
		Info.GraphName = Graph->GetName();
		Info.BlueprintPath = Path();
		Info.Tag = TEXT("N2CTest");
		return Info;
	}
END_DEFINE_SPEC(FN2CGraphResolverSpec)

void FN2CGraphResolverSpec::Define()
{
	BeforeEach([this]()
	{
		Test = MakeUnique<N2CTest::FTestBlueprint>(TEXT("BP_N2CGraphResolver"));
		UBlueprint* Blueprint = Test->GetBlueprint();

		// An interface implementation: GetClass returns a value, so implementing the interface adds a function graph
		// to ImplementedInterfaces[].Graphs (an interface event would go to the event graph instead)
		if (Blueprint && FBlueprintEditorUtils::ImplementNewInterface(Blueprint,
			FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("SoundEffectPresetWidgetInterface"))))
		{
			for (const FBPInterfaceDescription& Interface : Blueprint->ImplementedInterfaces)
			{
				for (UEdGraph* Graph : Interface.Graphs)
				{
					if (Graph && Graph->GetFName() == FName(TEXT("GetClass")))
					{
						InterfaceGraph = Graph;
					}
				}
			}
		}

		// A collapsed graph in the event graph with a node in it, and one nested in a function
		if (UK2Node_Composite* Node = Test->AddCollapsedGraph(Test->EventGraph(), FName(TEXT("N2CResolverCollapsed"))))
		{
			Collapsed = Node->BoundGraph;
			Test->AddCall(Collapsed, UKismetSystemLibrary::StaticClass(), GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString));
		}
		if (UEdGraph* Function = Test->AddFunction(FName(TEXT("N2CResolverFunction"))))
		{
			if (UK2Node_Composite* Node = Test->AddCollapsedGraph(Function, FName(TEXT("N2CResolverNested"))))
			{
				NestedCollapsed = Node->BoundGraph;
			}
		}

		// A sub-graph whose schema is not the Blueprint (K2) schema, as an AnimBP state machine's is
		if (Blueprint)
		{
			NotK2 = FBlueprintEditorUtils::CreateNewGraph(Blueprint, FName(TEXT("N2CResolverNotK2")), UEdGraph::StaticClass(),
				UEdGraphSchema::StaticClass());
			if (NotK2)
			{
				Test->EventGraph()->SubGraphs.Add(NotK2);
			}
		}

		TestTrue(TEXT("test graphs built: ") + FString::Join(Test->Errors(), TEXT("; ")),
			Test->Errors().Num() == 0 && InterfaceGraph && Collapsed && NestedCollapsed && NotK2);
	});

	AfterEach([this]()
	{
		InterfaceGraph = nullptr;
		Collapsed = nullptr;
		NestedCollapsed = nullptr;
		NotK2 = nullptr;
		Test.Reset();
	});

	Describe(TEXT("FindGraphByGuid"), [this]()
	{
		It(TEXT("finds an interface implementation graph"), [this]()
		{
			TestTrue(TEXT("interface graph found"),
				FN2CGraphUtils::FindGraphByGuid(Test->GetBlueprint(), InterfaceGraph->GraphGuid) == InterfaceGraph);
		});

		It(TEXT("finds a collapsed graph and a collapsed graph nested in a function"), [this]()
		{
			TestTrue(TEXT("collapsed graph found"),
				FN2CGraphUtils::FindGraphByGuid(Test->GetBlueprint(), Collapsed->GraphGuid) == Collapsed);
			TestTrue(TEXT("nested collapsed graph found"),
				FN2CGraphUtils::FindGraphByGuid(Test->GetBlueprint(), NestedCollapsed->GraphGuid) == NestedCollapsed);
		});

		It(TEXT("finds the event graph, and nothing for an unknown GUID or no Blueprint"), [this]()
		{
			TestTrue(TEXT("event graph found"),
				FN2CGraphUtils::FindGraphByGuid(Test->GetBlueprint(), Test->EventGraph()->GraphGuid) == Test->EventGraph());
			TestNull(TEXT("unknown GUID"), FN2CGraphUtils::FindGraphByGuid(Test->GetBlueprint(), FGuid::NewGuid()));
			TestNull(TEXT("no Blueprint"), FN2CGraphUtils::FindGraphByGuid(nullptr, Collapsed->GraphGuid));
		});
	});

	Describe(TEXT("ResolveTaggedGraph"), [this]()
	{
		It(TEXT("resolves a tag on an interface graph and on a collapsed graph by Blueprint path and GUID"), [this]()
		{
			for (UEdGraph* Expected : TArray<UEdGraph*>{ InterfaceGraph, Collapsed })
			{
				UBlueprint* Blueprint = nullptr;
				FString Message;
				const FN2CTagInfo Tag = TagFor(Expected);
				UEdGraph* Found = FN2CGraphUtils::ResolveTaggedGraph(Tag.BlueprintPath, Tag.GraphGuid, Tag.GraphName, Blueprint, Message);
				TestTrue(FString::Printf(TEXT("%s resolves: %s"), *Tag.GraphName, *Message), Found == Expected);
				TestTrue(TEXT("its Blueprint"), Blueprint == Test->GetBlueprint());
				TestTrue(TEXT("no message"), Message.IsEmpty());
			}
		});

		It(TEXT("names the graph and the Blueprint when the GUID matches no graph"), [this]()
		{
			UBlueprint* Blueprint = nullptr;
			FString Message;
			EN2CGraphResolveError Error = EN2CGraphResolveError::None;
			UEdGraph* Found = FN2CGraphUtils::ResolveTaggedGraph(Path(), FGuid::NewGuid().ToString(), TEXT("N2CDeletedGraph"),
				Blueprint, Message, &Error);
			TestNull(TEXT("nothing found"), Found);
			TestTrue(TEXT("GraphNotFound"), Error == EN2CGraphResolveError::GraphNotFound);
			TestTrue(TEXT("names the graph: ") + Message, Message.Contains(TEXT("N2CDeletedGraph")));
			TestTrue(TEXT("names the Blueprint: ") + Message, Message.Contains(Path()));
		});

		It(TEXT("names the graph and the path when the Blueprint does not load, and refuses a bad GUID"), [this]()
		{
			UBlueprint* Blueprint = nullptr;
			FString Message;
			EN2CGraphResolveError Error = EN2CGraphResolveError::None;
			const FString Missing = TEXT("/Temp/N2CTest_NoSuchPackage/BP_N2CNoSuch.BP_N2CNoSuch");
			TestNull(TEXT("no graph"), FN2CGraphUtils::ResolveTaggedGraph(Missing, Collapsed->GraphGuid.ToString(),
				TEXT("N2CResolverCollapsed"), Blueprint, Message, &Error));
			TestTrue(TEXT("BlueprintNotFound"), Error == EN2CGraphResolveError::BlueprintNotFound);
			TestTrue(TEXT("names the graph and path: ") + Message,
				Message.Contains(TEXT("N2CResolverCollapsed")) && Message.Contains(Missing));

			TestNull(TEXT("bad GUID"), FN2CGraphUtils::ResolveTaggedGraph(Path(), TEXT("not-a-guid"), TEXT("X"), Blueprint, Message, &Error));
			TestTrue(TEXT("InvalidGuid"), Error == EN2CGraphResolveError::InvalidGuid);
		});
	});

	Describe(TEXT("CanTranslateGraph"), [this]()
	{
		It(TEXT("accepts the event graph, an interface graph and collapsed graphs"), [this]()
		{
			for (UEdGraph* Graph : TArray<UEdGraph*>{ Test->EventGraph(), InterfaceGraph, Collapsed, NestedCollapsed })
			{
				FString Reason;
				TestTrue(FString::Printf(TEXT("%s translatable: %s"), *Graph->GetName(), *Reason),
					FN2CGraphUtils::CanTranslateGraph(Graph, &Reason));
			}
		});

		It(TEXT("refuses a graph with a non-Blueprint schema and says why"), [this]()
		{
			FString Reason;
			TestFalse(TEXT("not translatable"), FN2CGraphUtils::CanTranslateGraph(NotK2, &Reason));
			TestTrue(TEXT("reason names the graph: ") + Reason, Reason.Contains(TEXT("N2CResolverNotK2")));
			// It is still found by GUID: double-click in the main window can open it
			TestTrue(TEXT("still found"), FN2CGraphUtils::FindGraphByGuid(Test->GetBlueprint(), NotK2->GraphGuid) == NotK2);
		});

		It(TEXT("refuses a graph its Blueprint does not list"), [this]()
		{
			UEdGraph* Orphan = FBlueprintEditorUtils::CreateNewGraph(Test->GetBlueprint(), FName(TEXT("N2CResolverOrphan")),
				UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
			FString Reason;
			TestFalse(TEXT("not translatable"), FN2CGraphUtils::CanTranslateGraph(Orphan, &Reason));
			TestTrue(TEXT("reason given: ") + Reason, Reason.Contains(TEXT("N2CResolverOrphan")));
			TestFalse(TEXT("null graph"), FN2CGraphUtils::CanTranslateGraph(nullptr));
		});
	});

	Describe(TEXT("token estimation"), [this]()
	{
		It(TEXT("estimates a tagged collapsed graph and a tagged interface graph"), [this]()
		{
			FN2CTokenEstimationService& Service = FN2CTokenEstimationService::Get();
			for (UEdGraph* Graph : TArray<UEdGraph*>{ Collapsed, InterfaceGraph })
			{
				const FN2CTagInfo Tag = TagFor(Graph);
				Service.InvalidateCache(Tag.GraphGuid);
				TestTrue(FString::Printf(TEXT("%s has an estimate"), *Graph->GetName()), Service.GetTokenEstimate(Tag) > 0);
				Service.InvalidateCache(Tag.GraphGuid);
			}
		});

		It(TEXT("gives no estimate for a graph capture cannot read"), [this]()
		{
			FN2CTokenEstimationService& Service = FN2CTokenEstimationService::Get();
			const FN2CTagInfo Tag = TagFor(NotK2);
			Service.InvalidateCache(Tag.GraphGuid);
			AddExpectedMessage(TEXT("N2CResolverNotK2"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0, false);
			TestEqual(TEXT("no estimate"), Service.GetTokenEstimate(Tag), 0);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
