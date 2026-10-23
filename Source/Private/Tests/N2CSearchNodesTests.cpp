// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MCP/Tools/Implementations/Blueprint/Graph/N2CMcpSearchBlueprintNodesTool.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/TimelineTemplate.h"
#include "GameFramework/Actor.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/Guid.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"

namespace N2CSearchNodesTests
{
	/** Every link in the graph as "node.pin->node.pin", sorted, so any broken or added link changes it */
	TArray<FString> CollectLinks(const UEdGraph* Graph)
	{
		TArray<FString> Links;
		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node)
			{
				continue;
			}
			for (const UEdGraphPin* Pin : Node->Pins)
			{
				for (const UEdGraphPin* Linked : Pin->LinkedTo)
				{
					Links.Add(FString::Printf(TEXT("%s.%s->%s.%s"), *Node->GetName(), *Pin->PinName.ToString(),
						Linked ? *Linked->GetOwningNode()->GetName() : TEXT("null"), Linked ? *Linked->PinName.ToString() : TEXT("null")));
				}
			}
		}
		Links.Sort();
		return Links;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FN2CSearchIsReadOnlyTest, "NodeToCode.Robustness.SearchIsReadOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FN2CSearchIsReadOnlyTest::RunTest(const FString& Parameters)
{
	using namespace N2CSearchNodesTests;

	if (!TestNotNull(TEXT("GEditor"), GEditor) || !TestNotNull(TEXT("transaction buffer"), GEditor ? GEditor->Trans.Get() : nullptr))
	{
		return false;
	}

	// A real (non-transient) package, so MarkPackageDirty is not skipped the way it is for the transient package
	UPackage* Package = CreatePackage(*FString::Printf(TEXT("/Temp/N2CSearchReadOnlyTest_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), Package, TEXT("BP_N2CSearchReadOnly"),
		BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	if (!TestNotNull(TEXT("Blueprint"), Blueprint))
	{
		return false;
	}
	ON_SCOPE_EXIT { Package->SetDirtyFlag(false); };

	UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	if (!TestNotNull(TEXT("event graph"), EventGraph))
	{
		return false;
	}

	// A user-placed, wired Event BeginPlay: the spawner hands this exact node back for a non-template graph
	UK2Node_Event* BeginPlay = FBlueprintEditorUtils::FindOverrideForFunction(Blueprint, AActor::StaticClass(),
		FName(TEXT("ReceiveBeginPlay")));
	if (!BeginPlay)
	{
		int32 PosY = 0;
		BeginPlay = FKismetEditorUtilities::AddDefaultEventNode(Blueprint, EventGraph, FName(TEXT("ReceiveBeginPlay")),
			AActor::StaticClass(), PosY);
	}
	if (!TestNotNull(TEXT("Event BeginPlay"), BeginPlay))
	{
		return false;
	}
	BeginPlay->SetEnabledState(ENodeEnabledState::Enabled, /*bUserAction*/ true);

	FGraphNodeCreator<UK2Node_CallFunction> PrintCreator(*EventGraph);
	UK2Node_CallFunction* Print = PrintCreator.CreateNode();
	Print->SetFromFunction(UKismetSystemLibrary::StaticClass()->FindFunctionByName(
		GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString)));
	PrintCreator.Finalize();

	UEdGraphPin* ThenPin = BeginPlay->FindPin(UEdGraphSchema_K2::PN_Then);
	UEdGraphPin* PrintExec = Print->GetExecPin();
	if (!TestNotNull(TEXT("BeginPlay then pin"), ThenPin) || !TestNotNull(TEXT("PrintString exec pin"), PrintExec))
	{
		return false;
	}
	ThenPin->MakeLinkTo(PrintExec);

	// Baseline
	Package->SetDirtyFlag(false);
	const TArray<UEdGraphNode*> NodesBefore = EventGraph->Nodes;
	const TArray<FString> LinksBefore = CollectLinks(EventGraph);
	const int32 TimelinesBefore = Blueprint->Timelines.Num();
	const int32 ComponentTemplatesBefore = Blueprint->ComponentTemplates.Num();
	const int32 QueueBefore = GEditor->Trans->GetQueueLength();
	TestTrue(TEXT("baseline has the BeginPlay -> PrintString link"), LinksBefore.Num() > 0);

	FN2CMcpSearchBlueprintNodesTool Tool;
	const TCHAR* Terms[] = { TEXT("begin play"), TEXT("timeline"), TEXT("add static mesh component") };
	for (const TCHAR* Term : Terms)
	{
		const TArray<TSharedPtr<FJsonValue>> Results = Tool.SearchActions(Term, 20, /*bContextSensitive*/ true,
			Blueprint, EventGraph, FString(), /*bExcludeVMFunctions*/ true);
		TestTrue(FString::Printf(TEXT("'%s' finds results"), Term), Results.Num() > 0);

		// Results still carry pins read from the template node
		bool bAnyPins = false;
		for (const TSharedPtr<FJsonValue>& Value : Results)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Pins = nullptr;
			if (!Value->TryGetObject(Object))
			{
				continue;
			}
			for (const TCHAR* Field : { TEXT("input_pins"), TEXT("output_pins") })
			{
				if ((*Object)->TryGetArrayField(Field, Pins) && Pins->Num() > 0)
				{
					bAnyPins = true;
				}
			}
			if (bAnyPins)
			{
				break;
			}
		}
		TestTrue(FString::Printf(TEXT("'%s' results have pins"), Term), bAnyPins);

		TestEqual(FString::Printf(TEXT("'%s': node count unchanged"), Term), EventGraph->Nodes.Num(), NodesBefore.Num());
		TestTrue(FString::Printf(TEXT("'%s': same nodes in the graph"), Term), EventGraph->Nodes == NodesBefore);
		TestTrue(FString::Printf(TEXT("'%s': links unchanged"), Term), CollectLinks(EventGraph) == LinksBefore);
		TestTrue(FString::Printf(TEXT("'%s': BeginPlay still in the graph"), Term), EventGraph->Nodes.Contains(BeginPlay));
		TestTrue(FString::Printf(TEXT("'%s': BeginPlay still wired to PrintString"), Term), ThenPin->LinkedTo.Contains(PrintExec));
		TestEqual(FString::Printf(TEXT("'%s': Timelines unchanged"), Term), Blueprint->Timelines.Num(), TimelinesBefore);
		TestEqual(FString::Printf(TEXT("'%s': ComponentTemplates unchanged"), Term), Blueprint->ComponentTemplates.Num(), ComponentTemplatesBefore);
		TestFalse(FString::Printf(TEXT("'%s': package not dirty"), Term), Package->IsDirty());
		TestEqual(FString::Printf(TEXT("'%s': no transactions recorded"), Term), GEditor->Trans->GetQueueLength(), QueueBefore);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
