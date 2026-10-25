// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CCapture.h"
#include "Core/N2CSettings.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Composite.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_IfThenElse.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/ScopeExit.h"
#include "Tests/N2CTestBlueprintFactory.h"
#include "Tests/N2CTestHelpers.h"

/**
 * N2C::DescribeNode (the MCP add-node and search tools) describes one node in a fresh capture context: it never queues
 * nested graphs, never reads or changes another capture, and a capture made afterwards holds only what it was asked
 * for. Before card 0133 this was FN2CNodeTranslator::ProcessSingleNode on the shared singleton, which wrote flows
 * through a dangling graph pointer and queued a collapsed node's graph for the next capture (translation-core-6).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FN2CDescribeNodeTest, "NodeToCode.Robustness.DescribeNode", N2C_TEST_FLAGS)

bool FN2CDescribeNodeTest::RunTest(const FString& Parameters)
{
	using N2CTest::FTestBlueprint;

	// Depth 2 lets a user function and the one it calls be captured, which is what a lone node must never queue
	UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
	const int32 SavedDepth = Settings->TranslationDepth;
	Settings->TranslationDepth = 2;
	ON_SCOPE_EXIT { Settings->TranslationDepth = SavedDepth; };

	// Under /Game, so capture treats the Blueprint's functions and collapsed graphs as the user's
	FTestBlueprint BP(TEXT("BP_N2CDescribeNode"), TEXT("/Game"));
	if (!TestNotNull(TEXT("test Blueprint"), BP.GetBlueprint()))
	{
		return false;
	}

	// Inner <- Outer: a call to Outer reaches Inner at depth 2
	UEdGraph* Inner = BP.AddFunction(TEXT("Inner"));
	UEdGraph* Outer = BP.AddFunction(TEXT("Outer"));
	UK2Node_CallFunction* OuterCallsInner = BP.AddSelfCall(Outer, TEXT("Inner"));
	BP.Link(BP.FunctionEntry(Outer), UEdGraphSchema_K2::PN_Then, OuterCallsInner, UEdGraphSchema_K2::PN_Execute);

	// The event graph: Branch.Then -> PrintString (a linked exec output), a call to Outer and a collapsed graph
	UEdGraph* EventGraph = BP.EventGraph();
	UK2Node_IfThenElse* Branch = BP.AddBranch(EventGraph);
	UK2Node_CallFunction* Print = BP.AddCall(EventGraph, UKismetSystemLibrary::StaticClass(),
		GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString));
	BP.Link(Branch, UEdGraphSchema_K2::PN_Then, Print, UEdGraphSchema_K2::PN_Execute);
	UK2Node_CallFunction* CallOuter = BP.AddSelfCall(EventGraph, TEXT("Outer"));
	UK2Node_Composite* Composite = BP.AddCollapsedGraph(EventGraph, TEXT("DescribeCollapsed"));

	// An unrelated function that uses no other graph
	UEdGraph* Unrelated = BP.AddFunction(TEXT("Unrelated"));
	UK2Node_CallFunction* Collect = BP.AddCall(Unrelated, UKismetSystemLibrary::StaticClass(),
		GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, CollectGarbage));
	BP.Link(BP.FunctionEntry(Unrelated), UEdGraphSchema_K2::PN_Then, Collect, UEdGraphSchema_K2::PN_Execute);

	for (const FString& Error : BP.Errors())
	{
		AddError(FString::Printf(TEXT("building the graph: %s"), *Error));
	}
	if (!Branch || !Print || !CallOuter || !Composite || !Collect || BP.Errors().Num() > 0)
	{
		return false;
	}

	// The Branch has a linked exec output, so its flows are written (to the context's scratch graph)
	const TValueOrError<FN2CNodeDefinition, FString> BranchDef = N2C::DescribeNode(*Branch, TEXT("CallerBranch"));
	if (TestTrue(TEXT("DescribeNode describes the Branch"), BranchDef.HasValue()))
	{
		TestEqual(TEXT("Branch node type"), BranchDef.GetValue().NodeType, EN2CNodeType::Branch);
		TestTrue(TEXT("Branch pins captured"), BranchDef.GetValue().InputPins.Num() > 0 && BranchDef.GetValue().OutputPins.Num() > 0);
		TestEqual(TEXT("caller-supplied ID is kept"), BranchDef.GetValue().ID, FString(TEXT("CallerBranch")));
	}

	// The same node again, without an ID: a fresh context each time, so the same first ID and pin IDs
	const TValueOrError<FN2CNodeDefinition, FString> BranchAgain = N2C::DescribeNode(*Branch);
	if (TestTrue(TEXT("DescribeNode describes the Branch again"), BranchAgain.HasValue()))
	{
		TestEqual(TEXT("a generated ID starts from N1 in every context"), BranchAgain.GetValue().ID, FString(TEXT("N1")));
		if (TestTrue(TEXT("Branch has input pins"), BranchAgain.GetValue().InputPins.Num() > 0))
		{
			TestEqual(TEXT("pin IDs start from P1 in every context"), BranchAgain.GetValue().InputPins[0].ID, FString(TEXT("P1")));
		}
	}

	// A call to a user function whose own call reaches depth 2, and a collapsed node: neither queues its graph
	const TValueOrError<FN2CNodeDefinition, FString> CallDef = N2C::DescribeNode(*CallOuter);
	if (TestTrue(TEXT("DescribeNode describes the call to Outer"), CallDef.HasValue()))
	{
		TestEqual(TEXT("call node type"), CallDef.GetValue().NodeType, EN2CNodeType::CallFunction);
	}
	TestTrue(TEXT("DescribeNode describes the collapsed node"), N2C::DescribeNode(*Composite).HasValue());

	// The next capture of an unrelated graph holds only that graph
	const TValueOrError<FN2CCapture, FString> UnrelatedCapture = N2C::CaptureGraph(*Unrelated);
	if (TestTrue(TEXT("the unrelated graph is captured"), UnrelatedCapture.HasValue()))
	{
		const FN2CBlueprint& Output = UnrelatedCapture.GetValue().Blueprint;
		if (TestEqual(TEXT("the capture has one graph"), Output.Graphs.Num(), 1))
		{
			TestEqual(TEXT("the graph is the one asked for"), Output.Graphs[0].Name, Unrelated->GetName());
		}
		TestEqual(TEXT("no structs"), Output.Structs.Num(), 0);
		TestEqual(TEXT("no enums"), Output.Enums.Num(), 0);
	}

	// A capture, unlike DescribeNode, does follow the call to depth 2 (so the test above could have seen a leak)
	const TValueOrError<FN2CCapture, FString> EventCapture = N2C::CaptureGraph(*EventGraph);
	if (TestTrue(TEXT("the event graph is captured"), EventCapture.HasValue()))
	{
		TArray<FString> Names;
		for (const FN2CGraph& Graph : EventCapture.GetValue().Blueprint.Graphs)
		{
			Names.Add(Graph.Name);
		}
		TestTrue(TEXT("Outer (depth 1) is captured"), Names.Contains(Outer->GetName()));
		TestTrue(TEXT("Inner (depth 2) is captured"), Names.Contains(Inner->GetName()));
		TestTrue(TEXT("the collapsed graph (depth 1) is captured"), Names.Contains(TEXT("DescribeCollapsed")));
	}

	// Depth from the options wins over the setting
	FN2CCaptureOptions DepthOne;
	DepthOne.Depth = 1;
	const TValueOrError<FN2CCapture, FString> ShallowCapture = N2C::CaptureGraph(*EventGraph, DepthOne);
	if (TestTrue(TEXT("the event graph is captured at depth 1"), ShallowCapture.HasValue()))
	{
		bool bHasInner = false;
		for (const FN2CGraph& Graph : ShallowCapture.GetValue().Blueprint.Graphs)
		{
			bHasInner |= Graph.Name == Inner->GetName();
		}
		TestFalse(TEXT("Inner (depth 2) is not captured at Depth 1"), bHasInner);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
