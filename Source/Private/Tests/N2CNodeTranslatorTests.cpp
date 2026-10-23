// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CNodeTranslator.h"
#include "Core/N2CSettings.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "GameFramework/Actor.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Composite.h"
#include "K2Node_IfThenElse.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"

namespace N2CNodeTranslatorTests
{
	TArray<UK2Node*> CollectK2Nodes(const UEdGraph* Graph)
	{
		TArray<UK2Node*> Nodes;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UK2Node* K2Node = Cast<UK2Node>(Node))
			{
				Nodes.Add(K2Node);
			}
		}
		return Nodes;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FN2CProcessSingleNodeTest, "NodeToCode.Robustness.ProcessSingleNode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FN2CProcessSingleNodeTest::RunTest(const FString& Parameters)
{
	using namespace N2CNodeTranslatorTests;

	// Depth 1 lets a collapsed graph be queued, which is what used to leak into the next capture
	UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
	const int32 SavedDepth = Settings->TranslationDepth;
	Settings->TranslationDepth = 1;
	ON_SCOPE_EXIT { Settings->TranslationDepth = SavedDepth; };

	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), GetTransientPackage(),
		MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("BP_N2CProcessSingleNodeTest")),
		BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	if (!TestNotNull(TEXT("transient Blueprint"), Blueprint))
	{
		return false;
	}

	UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	if (!TestNotNull(TEXT("event graph"), EventGraph))
	{
		return false;
	}

	// Branch.Then -> PrintString in the event graph
	FGraphNodeCreator<UK2Node_IfThenElse> BranchCreator(*EventGraph);
	UK2Node_IfThenElse* Branch = BranchCreator.CreateNode();
	BranchCreator.Finalize();

	FGraphNodeCreator<UK2Node_CallFunction> PrintCreator(*EventGraph);
	UK2Node_CallFunction* Print = PrintCreator.CreateNode();
	Print->SetFromFunction(UKismetSystemLibrary::StaticClass()->FindFunctionByName(
		GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString)));
	PrintCreator.Finalize();

	UEdGraphPin* ThenPin = Branch->GetThenPin();
	UEdGraphPin* PrintExec = Print->GetExecPin();
	if (!TestNotNull(TEXT("Branch then pin"), ThenPin) || !TestNotNull(TEXT("PrintString exec pin"), PrintExec))
	{
		return false;
	}
	ThenPin->MakeLinkTo(PrintExec);
	TestTrue(TEXT("Branch is linked to PrintString"), ThenPin->LinkedTo.Contains(PrintExec));

	// A collapsed graph node: processing it used to queue its bound graph for whatever capture ran next
	FGraphNodeCreator<UK2Node_Composite> CompositeCreator(*EventGraph);
	UK2Node_Composite* Composite = CompositeCreator.CreateNode();
	CompositeCreator.Finalize();
	TestNotNull(TEXT("composite bound graph"), Composite->BoundGraph.Get());

	// The other graph: a function graph that GenerateN2CStruct captures
	UEdGraph* FunctionGraph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, TEXT("N2CTestFunction"),
		UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, FunctionGraph, /*bIsUserCreated*/ true, nullptr);
	const TArray<UK2Node*> FunctionNodes = CollectK2Nodes(FunctionGraph);
	if (!TestTrue(TEXT("function graph has nodes"), FunctionNodes.Num() > 0))
	{
		return false;
	}

	FN2CNodeTranslator& Translator = FN2CNodeTranslator::Get();

	TestTrue(TEXT("first capture succeeds"), Translator.GenerateN2CStruct(FunctionNodes));
	TestFalse(TEXT("no active graph after GenerateN2CStruct"), Translator.HasActiveGraph());
	const int32 GraphsAfterCapture = Translator.GetN2CBlueprint().Graphs.Num();

	// The Branch has a linked exec output, so ProcessNodeFlows writes into CurrentGraph
	FN2CNodeDefinition BranchDef;
	BranchDef.ID = TEXT("CallerBranch");
	TestTrue(TEXT("ProcessSingleNode processes the Branch"), Translator.ProcessSingleNode(Branch, BranchDef));
	TestEqual(TEXT("Branch node type"), BranchDef.NodeType, EN2CNodeType::Branch);
	TestTrue(TEXT("Branch pins captured"), BranchDef.InputPins.Num() > 0 && BranchDef.OutputPins.Num() > 0);
	TestEqual(TEXT("caller-supplied ID is preserved"), BranchDef.ID, FString(TEXT("CallerBranch")));
	TestFalse(TEXT("no active graph after ProcessSingleNode"), Translator.HasActiveGraph());

	FN2CNodeDefinition CompositeDef;
	TestTrue(TEXT("ProcessSingleNode processes the collapsed node"), Translator.ProcessSingleNode(Composite, CompositeDef));
	TestFalse(TEXT("generated ID is set when none was supplied"), CompositeDef.ID.IsEmpty());
	TestFalse(TEXT("no active graph after second ProcessSingleNode"), Translator.HasActiveGraph());
	TestEqual(TEXT("ProcessSingleNode leaves the last capture's graphs alone"),
		Translator.GetN2CBlueprint().Graphs.Num(), GraphsAfterCapture);

	// The next capture holds only the graph it was asked for
	TestTrue(TEXT("second capture succeeds"), Translator.GenerateN2CStruct(FunctionNodes));
	const FN2CBlueprint& Output = Translator.GetN2CBlueprint();
	TestEqual(TEXT("second capture has one graph"), Output.Graphs.Num(), 1);
	if (Output.Graphs.Num() > 0)
	{
		TestEqual(TEXT("second capture graph is the requested one"), Output.Graphs[0].Name, FunctionGraph->GetName());
	}
	for (const FN2CGraph& Graph : Output.Graphs)
	{
		TestNotEqual(TEXT("collapsed graph did not leak into the capture"), Graph.Name, Composite->BoundGraph->GetName());
	}
	TestFalse(TEXT("no active graph after second capture"), Translator.HasActiveGraph());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
