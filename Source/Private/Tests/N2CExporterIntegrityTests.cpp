// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CNodeTranslator.h"
#include "Core/N2CSerializer.h"
#include "Toolsets/N2CBlueprintToolset.h"
#include "RigVMEditorAsset.h"
#include "RigVMModel/RigVMGraph.h"
#include "RigVMModel/RigVMNode.h"
#include "RigVMModel/RigVMPin.h"
#include "RigVMModel/RigVMLink.h"
#include "Tests/N2CTestGraphNode.h"

#include "K2Node_Composite.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Knot.h"
#include "Math/Vector.h"
#include "Misc/AutomationTest.h"
#include "RigVMModel/RigVMController.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
UBlueprint* MakeTestBlueprint(const FName Name)
{
    const FName UniqueName = MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), Name);
    UBlueprint* Blueprint = NewObject<UBlueprint>(GetTransientPackage(), UniqueName, RF_Transient);
    Blueprint->BlueprintType = BPTYPE_Normal;
    return Blueprint;
}

UEdGraph* MakeBlueprintGraph(UBlueprint* Blueprint, const FName Name, const bool bEventGraph)
{
    if (!IsValid(Blueprint))
    {
        return nullptr;
    }

    UEdGraph* Graph = NewObject<UEdGraph>(Blueprint, Name, RF_Transient);
    Graph->Schema = UEdGraphSchema_K2::StaticClass();
    Graph->GraphGuid = FGuid::NewGuid();
    if (bEventGraph)
    {
        Blueprint->UbergraphPages.Add(Graph);
    }
    else
    {
        Blueprint->FunctionGraphs.Add(Graph);
    }
    return Graph;
}

template <typename TNode>
TNode* AddNode(UEdGraph* Graph, const FName Name)
{
    if (!IsValid(Graph))
    {
        return nullptr;
    }

    TNode* Node = NewObject<TNode>(Graph, Name, RF_Transient);
    Graph->AddNode(Node, false, false);
    Node->CreateNewGuid();
    Node->AllocateDefaultPins();
    return Node;
}

UN2CTestGraphNode* AddTestNode(UEdGraph* Graph, const FName Name)
{
    UN2CTestGraphNode* Node = AddNode<UN2CTestGraphNode>(Graph, Name);
    if (Node)
    {
        Node->TestTitle = Name;
    }
    return Node;
}

UEdGraphPin* AddTestPin(
    UEdGraphNode* Node,
    const EEdGraphPinDirection Direction,
    const FName Category,
    const FName Name,
    const bool bHidden = false)
{
    if (!IsValid(Node))
    {
        return nullptr;
    }

    FEdGraphPinType PinType;
    PinType.PinCategory = Category;
    UEdGraphPin* Pin = Node->CreatePin(Direction, PinType, Name);
    if (Pin)
    {
        Pin->bHidden = bHidden;
    }
    return Pin;
}

UEdGraphPin* FindPin(UEdGraphNode* Node, const FName PinName, const EEdGraphPinDirection Direction)
{
    if (!IsValid(Node))
    {
        return nullptr;
    }
    return Node->FindPin(PinName, Direction);
}

FString EndpointKey(const FGuid& NodeGuid, const FGuid& PinGuid)
{
    return NodeGuid.ToString() + TEXT(":") + PinGuid.ToString();
}

FString UndirectedEdgeKey(const FString& Left, const FString& Right)
{
    return Left.Compare(Right, ESearchCase::CaseSensitive) <= 0
        ? Left + TEXT("<->") + Right
        : Right + TEXT("<->") + Left;
}

TSet<FString> NativeLinkedToEdges(const UEdGraph* Graph)
{
    TSet<FString> Edges;
    if (!IsValid(Graph))
    {
        return Edges;
    }

    for (const UEdGraphNode* Node : Graph->Nodes)
    {
        if (!IsValid(Node))
        {
            continue;
        }
        for (const UEdGraphPin* Pin : Node->Pins)
        {
            if (!Pin)
            {
                continue;
            }
            for (const UEdGraphPin* Linked : Pin->LinkedTo)
            {
                if (!Linked || !IsValid(Linked->GetOwningNode()))
                {
                    continue;
                }

                const UEdGraphPin* Source = Pin;
                const UEdGraphPin* Target = Linked;
                if (Linked->Direction == EGPD_Output && Pin->Direction != EGPD_Output)
                {
                    Swap(Source, Target);
                }
                if (Source->Direction != EGPD_Output || Target->Direction != EGPD_Input)
                {
                    continue;
                }

                Edges.Add(UndirectedEdgeKey(
                    EndpointKey(Source->GetOwningNode()->NodeGuid, Source->PinId),
                    EndpointKey(Target->GetOwningNode()->NodeGuid, Target->PinId)));
            }
        }
    }
    return Edges;
}

const FN2CGraph* TranslateGraph(
    FAutomationTestBase& Test,
    UBlueprint* Blueprint,
    const FName GraphName,
    const FN2CBlueprint*& OutBlueprint)
{
    FN2CNodeTranslator& Translator = FN2CNodeTranslator::Get();
    if (!Test.TestTrue(TEXT("Blueprint translates"), Translator.GenerateFromBlueprint(Blueprint, false)))
    {
        return nullptr;
    }

    OutBlueprint = &Translator.GetN2CBlueprint();
    for (const FN2CGraph& Graph : OutBlueprint->Graphs)
    {
        if (Graph.Name == GraphName.ToString())
        {
            return &Graph;
        }
    }
    Test.AddError(FString::Printf(TEXT("Translated graph '%s' was not found."), *GraphName.ToString()));
    return nullptr;
}

void TestExportMatchesNativeGraph(
    FAutomationTestBase& Test,
    UBlueprint* Blueprint,
    UEdGraph* NativeGraph,
    const FName GraphName,
    const FN2CGraph*& OutGraph,
    const FN2CBlueprint*& OutBlueprint)
{
    OutGraph = TranslateGraph(Test, Blueprint, GraphName, OutBlueprint);
    if (!OutGraph)
    {
        return;
    }

    const TSet<FString> NativeEdges = NativeLinkedToEdges(NativeGraph);
    TSet<FString> ExportedEdges;
    for (const FN2CDirectLink& Link : OutGraph->Connections)
    {
        ExportedEdges.Add(UndirectedEdgeKey(
            Link.SourceNodeGuid + TEXT(":") + Link.SourcePinGuid,
            Link.TargetNodeGuid + TEXT(":") + Link.TargetPinGuid));
    }

    Test.TestEqual(TEXT("Direct connection count equals unique UE LinkedTo pairs"),
        ExportedEdges.Num(), NativeEdges.Num());
    for (const FString& Edge : NativeEdges)
    {
        Test.TestTrue(FString::Printf(TEXT("UE LinkedTo edge is present: %s"), *Edge), ExportedEdges.Contains(Edge));
    }
    for (const FString& Edge : ExportedEdges)
    {
        Test.TestTrue(FString::Printf(TEXT("Exported edge exists in UE LinkedTo: %s"), *Edge), NativeEdges.Contains(Edge));
    }
    Test.TestEqual(TEXT("Valid fixture has no unresolved links"), OutGraph->UnresolvedConnectionCount, 0);
    Test.TestTrue(TEXT("Valid fixture has no connection errors"), OutGraph->ConnectionErrors.IsEmpty());

    TMap<FString, const FN2CGraphPin*> ExportedPins;
    for (const FN2CGraphPin& Pin : OutGraph->Pins)
    {
        ExportedPins.Add(Pin.NodeGuid + TEXT(":") + Pin.PinGuid, &Pin);
    }

    int32 NativePinCount = 0;
    for (const UEdGraphNode* Node : NativeGraph->Nodes)
    {
        if (!IsValid(Node))
        {
            continue;
        }
        for (const UEdGraphPin* Pin : Node->Pins)
        {
            if (!Pin)
            {
                continue;
            }
            ++NativePinCount;
            const FString Key = EndpointKey(Node->NodeGuid, Pin->PinId);
            const FN2CGraphPin* ExportedPin = ExportedPins.FindRef(Key);
            Test.TestNotNull(FString::Printf(TEXT("Pin metadata exists for %s.%s"), *Node->GetName(), *Pin->PinName.ToString()), ExportedPin);
            if (!ExportedPin)
            {
                continue;
            }
            Test.TestEqual(TEXT("Pin name matches native pin"), ExportedPin->PinName, Pin->PinName.ToString());
            Test.TestEqual(TEXT("Pin direction matches native pin"),
                ExportedPin->Direction,
                Pin->Direction == EGPD_Input ? FString(TEXT("Input")) : FString(TEXT("Output")));
            Test.TestEqual(TEXT("Pin category matches native pin"), ExportedPin->Category, Pin->PinType.PinCategory.ToString());
            Test.TestEqual(TEXT("Hidden flag matches native pin"), ExportedPin->bHidden, Pin->bHidden);
            Test.TestEqual(TEXT("Connected flag matches native pin"), ExportedPin->bConnected, Pin->LinkedTo.Num() > 0);
            if (Pin->ParentPin)
            {
                Test.TestEqual(TEXT("Split pin parent identity is preserved"),
                    ExportedPin->ParentPinGuid, Pin->ParentPin->PinId.ToString());
            }
        }
    }
    Test.TestEqual(TEXT("Full pin metadata includes every native graph pin"), OutGraph->Pins.Num(), NativePinCount);

    for (const FN2CDirectLink& Link : OutGraph->Connections)
    {
        const FN2CGraphPin* Source = ExportedPins.FindRef(Link.SourceNodeGuid + TEXT(":") + Link.SourcePinGuid);
        const FN2CGraphPin* Target = ExportedPins.FindRef(Link.TargetNodeGuid + TEXT(":") + Link.TargetPinGuid);
        Test.TestNotNull(TEXT("Direct link source exists in pin metadata"), Source);
        Test.TestNotNull(TEXT("Direct link target exists in pin metadata"), Target);
        if (!Source || !Target)
        {
            continue;
        }
        Test.TestEqual(TEXT("Direct link source pin name matches metadata"), Link.SourcePinName, Source->PinName);
        Test.TestEqual(TEXT("Direct link target pin name matches metadata"), Link.TargetPinName, Target->PinName);
        Test.TestEqual(TEXT("Execution identity matches endpoint types"),
            Link.bIsExecution, Source->bIsExecution || Target->bIsExecution);
        Test.TestEqual(TEXT("Source hidden identity matches metadata"), Link.bSourceHidden, Source->bHidden);
        Test.TestEqual(TEXT("Target hidden identity matches metadata"), Link.bTargetHidden, Target->bHidden);
    }
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FNodeToCodeDirectLinkIntegrityTest,
    "NodeToCode.Exporter.DirectLinkIntegrity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNodeToCodeDirectLinkIntegrityTest::RunTest(const FString& Parameters)
{
    UBlueprint* Blueprint = MakeTestBlueprint(TEXT("N2CDirectLinkFixture"));
    UEdGraph* Graph = MakeBlueprintGraph(Blueprint, TEXT("EventGraph"), true);
    if (!TestNotNull(TEXT("Fixture Blueprint exists"), Blueprint) ||
        !TestNotNull(TEXT("Event graph exists"), Graph))
    {
        return false;
    }

    // Add the consumers first so the source node is a forward reference in graph order.
    UK2Node_IfThenElse* BranchA = AddNode<UK2Node_IfThenElse>(Graph, TEXT("BranchA"));
    UK2Node_IfThenElse* BranchB = AddNode<UK2Node_IfThenElse>(Graph, TEXT("BranchB"));
    UK2Node_IfThenElse* BranchC = AddNode<UK2Node_IfThenElse>(Graph, TEXT("BranchC"));
    UK2Node_ExecutionSequence* Sequence = AddNode<UK2Node_ExecutionSequence>(Graph, TEXT("Sequence"));
    UK2Node_Knot* Knot = AddNode<UK2Node_Knot>(Graph, TEXT("Reroute"));
    UK2Node_Knot* CycleKnotA = AddNode<UK2Node_Knot>(Graph, TEXT("CycleRerouteA"));
    UK2Node_Knot* CycleKnotB = AddNode<UK2Node_Knot>(Graph, TEXT("CycleRerouteB"));
    UK2Node_Composite* Composite = AddNode<UK2Node_Composite>(Graph, TEXT("CollapsedGraph"));
    UN2CTestGraphNode* ThenSink = AddTestNode(Graph, TEXT("ThenSink"));
    UN2CTestGraphNode* ElseSink = AddTestNode(Graph, TEXT("ElseSink"));
    UN2CTestGraphNode* SequenceSink0 = AddTestNode(Graph, TEXT("SequenceSink0"));
    UN2CTestGraphNode* SequenceSink1 = AddTestNode(Graph, TEXT("SequenceSink1"));
    UN2CTestGraphNode* KnotSink0 = AddTestNode(Graph, TEXT("KnotSink0"));
    UN2CTestGraphNode* KnotSink1 = AddTestNode(Graph, TEXT("KnotSink1"));
    UN2CTestGraphNode* SplitSource = AddTestNode(Graph, TEXT("SplitSource"));
    UN2CTestGraphNode* SplitSink = AddTestNode(Graph, TEXT("SplitSink"));
    UN2CTestGraphNode* HiddenSource = AddTestNode(Graph, TEXT("HiddenSource"));
    UN2CTestGraphNode* HiddenSink = AddTestNode(Graph, TEXT("HiddenSink"));
    UN2CTestGraphNode* FanOutSource = AddTestNode(Graph, TEXT("FanOutSource"));

    if (!TestNotNull(TEXT("Three Branch nodes exist"), BranchA) ||
        !TestNotNull(TEXT("Sequence node exists"), Sequence) ||
        !TestNotNull(TEXT("Knot node exists"), Knot) ||
        !TestNotNull(TEXT("Collapsed graph node exists"), Composite) ||
        !TestNotNull(TEXT("Fan-out source exists"), FanOutSource))
    {
        return false;
    }

    UEdGraphPin* ConditionA = BranchA->GetConditionPin();
    UEdGraphPin* ConditionB = BranchB->GetConditionPin();
    UEdGraphPin* ConditionC = BranchC->GetConditionPin();
    if (!TestNotNull(TEXT("Branch A condition pin exists"), ConditionA) ||
        !TestNotNull(TEXT("Branch B condition pin exists"), ConditionB) ||
        !TestNotNull(TEXT("Branch C condition pin exists"), ConditionC))
    {
        return false;
    }

    // Blueprint nodes may reuse a pin GUID on separate nodes. This mirrors the live BP_Item fan-out.
    ConditionB->PinId = ConditionA->PinId;
    ConditionC->PinId = ConditionA->PinId;
    UEdGraphPin* FanOutPin = AddTestPin(FanOutSource, EGPD_Output, UEdGraphSchema_K2::PC_Boolean, TEXT("Condition"));
    FanOutPin->MakeLinkTo(ConditionA);
    FanOutPin->MakeLinkTo(ConditionB);
    FanOutPin->MakeLinkTo(ConditionC);

    UEdGraphPin* BranchThen = FindPin(BranchA, UEdGraphSchema_K2::PN_Then, EGPD_Output);
    UEdGraphPin* BranchElse = BranchA->GetElsePin();
    UEdGraphPin* SequenceExec = FindPin(Sequence, UEdGraphSchema_K2::PN_Execute, EGPD_Input);
    UEdGraphPin* SequenceThen0 = Sequence->GetThenPinGivenIndex(0);
    UEdGraphPin* SequenceThen1 = Sequence->GetThenPinGivenIndex(1);
    UEdGraphPin* ThenSinkExec = AddTestPin(ThenSink, EGPD_Input, UEdGraphSchema_K2::PC_Exec, TEXT("execute"));
    UEdGraphPin* ElseSinkExec = AddTestPin(ElseSink, EGPD_Input, UEdGraphSchema_K2::PC_Exec, TEXT("execute"));
    UEdGraphPin* SequenceSink0Exec = AddTestPin(SequenceSink0, EGPD_Input, UEdGraphSchema_K2::PC_Exec, TEXT("execute"));
    UEdGraphPin* SequenceSink1Exec = AddTestPin(SequenceSink1, EGPD_Input, UEdGraphSchema_K2::PC_Exec, TEXT("execute"));
    if (!TestNotNull(TEXT("Branch then output exists"), BranchThen) ||
        !TestNotNull(TEXT("Branch else output exists"), BranchElse) ||
        !TestNotNull(TEXT("Sequence execute input exists"), SequenceExec) ||
        !TestNotNull(TEXT("Sequence output zero exists"), SequenceThen0) ||
        !TestNotNull(TEXT("Sequence output one exists"), SequenceThen1))
    {
        return false;
    }

    BranchThen->MakeLinkTo(SequenceExec);
    BranchElse->MakeLinkTo(ElseSinkExec);
    SequenceThen0->MakeLinkTo(SequenceSink0Exec);
    SequenceThen1->MakeLinkTo(SequenceSink1Exec);

    Knot->GetInputPin()->PinType.PinCategory = UEdGraphSchema_K2::PC_Exec;
    Knot->GetOutputPin()->PinType.PinCategory = UEdGraphSchema_K2::PC_Exec;
    CycleKnotA->GetInputPin()->PinType.PinCategory = UEdGraphSchema_K2::PC_Exec;
    CycleKnotA->GetOutputPin()->PinType.PinCategory = UEdGraphSchema_K2::PC_Exec;
    CycleKnotB->GetInputPin()->PinType.PinCategory = UEdGraphSchema_K2::PC_Exec;
    CycleKnotB->GetOutputPin()->PinType.PinCategory = UEdGraphSchema_K2::PC_Exec;
    CycleKnotA->GetOutputPin()->MakeLinkTo(CycleKnotB->GetInputPin());
    CycleKnotB->GetOutputPin()->MakeLinkTo(CycleKnotA->GetInputPin());
    UEdGraphPin* KnotSink0Exec = AddTestPin(KnotSink0, EGPD_Input, UEdGraphSchema_K2::PC_Exec, TEXT("execute"));
    UEdGraphPin* KnotSink1Exec = AddTestPin(KnotSink1, EGPD_Input, UEdGraphSchema_K2::PC_Exec, TEXT("execute"));
    BranchB->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output)->MakeLinkTo(Knot->GetInputPin());
    Knot->GetOutputPin()->MakeLinkTo(KnotSink0Exec);
    Knot->GetOutputPin()->MakeLinkTo(KnotSink1Exec);

    UEdGraph* CollapsedBody = MakeBlueprintGraph(Blueprint, TEXT("CollapsedBody"), false);
    Composite->BoundGraph = CollapsedBody;
    Composite->AllocateDefaultPins();
    UEdGraphPin* CompositeExec = AddTestPin(Composite, EGPD_Input, UEdGraphSchema_K2::PC_Exec, TEXT("execute"));
    UEdGraphPin* CompositeThen = AddTestPin(Composite, EGPD_Output, UEdGraphSchema_K2::PC_Exec, TEXT("then"));
    UN2CTestGraphNode* CollapsedEntry = AddTestNode(CollapsedBody, TEXT("CollapsedEntry"));
    UN2CTestGraphNode* CollapsedExit = AddTestNode(CollapsedBody, TEXT("CollapsedExit"));
    UEdGraphPin* CollapsedEntryOut = AddTestPin(CollapsedEntry, EGPD_Output, UEdGraphSchema_K2::PC_Exec, TEXT("then"));
    UEdGraphPin* CollapsedExitIn = AddTestPin(CollapsedExit, EGPD_Input, UEdGraphSchema_K2::PC_Exec, TEXT("execute"));
    CollapsedEntryOut->MakeLinkTo(CollapsedExitIn);
    SequenceThen1->MakeLinkTo(CompositeExec);
    CompositeThen->MakeLinkTo(ThenSinkExec);

    FEdGraphPinType VectorType;
    VectorType.PinCategory = UEdGraphSchema_K2::PC_Struct;
    VectorType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
    UEdGraphPin* VectorParent = SplitSource->CreatePin(EGPD_Output, VectorType, TEXT("Vector"));
    FEdGraphPinType FloatType;
    FloatType.PinCategory = UEdGraphSchema_K2::PC_Real;
    FloatType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
    UEdGraphPin* SplitX = SplitSource->CreatePin(EGPD_Output, FloatType, TEXT("X"));
    SplitX->ParentPin = VectorParent;
    VectorParent->SubPins.Add(SplitX);
    UEdGraphPin* SplitSinkX = AddTestPin(SplitSink, EGPD_Input, UEdGraphSchema_K2::PC_Real, TEXT("X"));
    SplitX->MakeLinkTo(SplitSinkX);

    UEdGraphPin* HiddenOut = AddTestPin(HiddenSource, EGPD_Output, UEdGraphSchema_K2::PC_Boolean, TEXT("HiddenValue"), true);
    UEdGraphPin* HiddenIn = AddTestPin(HiddenSink, EGPD_Input, UEdGraphSchema_K2::PC_Boolean, TEXT("HiddenValue"), true);
    HiddenOut->MakeLinkTo(HiddenIn);

    const FN2CBlueprint* ExportedBlueprint = nullptr;
    const FN2CGraph* ExportedGraph = nullptr;
    TestExportMatchesNativeGraph(*this, Blueprint, Graph, TEXT("EventGraph"), ExportedGraph, ExportedBlueprint);
    if (!ExportedGraph || !ExportedBlueprint)
    {
        return false;
    }

    int32 FanOutEdgeCount = 0;
    for (const FN2CDirectLink& Link : ExportedGraph->Connections)
    {
        if (Link.SourceNodeGuid == FanOutSource->NodeGuid.ToString() &&
            Link.SourcePinGuid == FanOutPin->PinId.ToString())
        {
            ++FanOutEdgeCount;
        }
    }
    TestEqual(TEXT("Output fan-out preserves all three consumers despite reused pin GUIDs"), FanOutEdgeCount, 3);
    TestTrue(TEXT("True branch pin is preserved"),
        ExportedGraph->Connections.ContainsByPredicate([&](const FN2CDirectLink& Link)
        {
            return Link.SourceNodeGuid == BranchA->NodeGuid.ToString() && Link.SourcePinName == TEXT("then") && Link.bIsExecution;
        }));
    TestTrue(TEXT("False branch pin is preserved"),
        ExportedGraph->Connections.ContainsByPredicate([&](const FN2CDirectLink& Link)
        {
            return Link.SourceNodeGuid == BranchA->NodeGuid.ToString() && Link.SourcePinName == TEXT("else") && Link.bIsExecution;
        }));
    TestTrue(TEXT("Hidden pin metadata is preserved"),
        ExportedGraph->Pins.ContainsByPredicate([&](const FN2CGraphPin& Pin)
        {
            return Pin.NodeGuid == HiddenSource->NodeGuid.ToString() && Pin.PinGuid == HiddenOut->PinId.ToString() && Pin.bHidden;
        }));
    TestTrue(TEXT("Split pin parent metadata is preserved"),
        ExportedGraph->Pins.ContainsByPredicate([&](const FN2CGraphPin& Pin)
        {
            return Pin.NodeGuid == SplitSource->NodeGuid.ToString() && Pin.PinGuid == SplitX->PinId.ToString() &&
                Pin.ParentPinGuid == VectorParent->PinId.ToString();
        }));
    TestTrue(TEXT("Parent pin lists its split children"),
        ExportedGraph->Pins.ContainsByPredicate([&](const FN2CGraphPin& Pin)
        {
            return Pin.NodeGuid == SplitSource->NodeGuid.ToString() && Pin.PinGuid == VectorParent->PinId.ToString() &&
                Pin.SubPinGuids.Contains(SplitX->PinId.ToString());
        }));
    TestTrue(TEXT("Knot nodes remain explicit physical link endpoints"),
        ExportedGraph->Connections.ContainsByPredicate([&](const FN2CDirectLink& Link)
        {
            return Link.bSourceIsKnot || Link.bTargetIsKnot;
        }));
    int32 KnotOutputFanOut = 0;
    for (const FN2CDirectLink& Link : ExportedGraph->Connections)
    {
        if (Link.SourceNodeGuid == Knot->NodeGuid.ToString() && Link.bSourceIsKnot)
        {
            ++KnotOutputFanOut;
        }
    }
    TestEqual(TEXT("Reroute output fan-out preserves both consumers"), KnotOutputFanOut, 2);
    int32 KnotCycleLinkCount = 0;
    for (const FN2CDirectLink& Link : ExportedGraph->Connections)
    {
        if ((Link.SourceNodeGuid == CycleKnotA->NodeGuid.ToString() && Link.TargetNodeGuid == CycleKnotB->NodeGuid.ToString()) ||
            (Link.SourceNodeGuid == CycleKnotB->NodeGuid.ToString() && Link.TargetNodeGuid == CycleKnotA->NodeGuid.ToString()))
        {
            ++KnotCycleLinkCount;
        }
    }
    TestEqual(TEXT("Knot cycle preserves both physical edges"), KnotCycleLinkCount, 2);
    TestTrue(TEXT("Sequence output zero is named distinctly"),
        ExportedGraph->Connections.ContainsByPredicate([&](const FN2CDirectLink& Link)
        {
            return Link.SourceNodeGuid == Sequence->NodeGuid.ToString() && Link.SourcePinName == SequenceThen0->PinName.ToString();
        }));
    TestTrue(TEXT("Sequence output one is named distinctly"),
        ExportedGraph->Connections.ContainsByPredicate([&](const FN2CDirectLink& Link)
        {
            return Link.SourceNodeGuid == Sequence->NodeGuid.ToString() && Link.SourcePinName == SequenceThen1->PinName.ToString();
        }));

    FN2CBlueprint SerializableBlueprint = *ExportedBlueprint;
    SerializableBlueprint.Metadata.BlueprintClass = TEXT("N2CTestBlueprint_C");
    const FString Json = FN2CSerializer::ToJson(SerializableBlueprint);
    TestFalse(TEXT("Fixture serializes to JSON"), Json.IsEmpty());
    FN2CBlueprint RoundTripped;
    TestTrue(TEXT("Full graph JSON round-trip succeeds"), FN2CSerializer::FromJson(Json, RoundTripped));
    const FN2CGraph* RoundTrippedGraph = nullptr;
    for (const FN2CGraph& GraphResult : RoundTripped.Graphs)
    {
        if (GraphResult.Name == TEXT("EventGraph"))
        {
            RoundTrippedGraph = &GraphResult;
            break;
        }
    }
    TestNotNull(TEXT("Round-trip contains EventGraph"), RoundTrippedGraph);
    if (RoundTrippedGraph)
    {
        TestEqual(TEXT("Round-trip preserves direct links"), RoundTrippedGraph->Connections.Num(), ExportedGraph->Connections.Num());
        TestEqual(TEXT("Round-trip preserves all pin metadata"), RoundTrippedGraph->Pins.Num(), ExportedGraph->Pins.Num());
    }

    const FN2CBlueprint* CollapsedBlueprintData = nullptr;
    const FN2CGraph* CollapsedGraphModel = nullptr;
    TestExportMatchesNativeGraph(*this, Blueprint, CollapsedBody, TEXT("CollapsedBody"),
        CollapsedGraphModel, CollapsedBlueprintData);
    TestNotNull(TEXT("Collapsed function graph model exists"), CollapsedGraphModel);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FNodeToCodeBlueprintGraphCoverageTest,
    "NodeToCode.Exporter.BlueprintGraphCoverage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNodeToCodeBlueprintGraphCoverageTest::RunTest(const FString& Parameters)
{
    UBlueprint* Blueprint = MakeTestBlueprint(TEXT("N2CGraphCoverageFixture"));
    UEdGraph* EventGraph = MakeBlueprintGraph(Blueprint, TEXT("EventGraph"), true);
    AddTestNode(EventGraph, TEXT("EventNode"));
    MakeBlueprintGraph(Blueprint, TEXT("EmptyFunction"), false);

    UBlueprint* FirstInterfaceOwner = MakeTestBlueprint(TEXT("N2CInterfaceOwnerA"));
    UEdGraph* FirstInterfaceGraph = MakeBlueprintGraph(
        FirstInterfaceOwner, TEXT("SharedInterfaceFunction"), false);
    AddTestNode(FirstInterfaceGraph, TEXT("FirstInterfaceNode"));
    FBPInterfaceDescription& FirstInterface = Blueprint->ImplementedInterfaces.AddDefaulted_GetRef();
    FirstInterface.Interface = UInterface::StaticClass();
    FirstInterface.Graphs.Add(FirstInterfaceGraph);

    UBlueprint* SecondInterfaceOwner = MakeTestBlueprint(TEXT("N2CInterfaceOwnerB"));
    UEdGraph* SecondInterfaceGraph = MakeBlueprintGraph(
        SecondInterfaceOwner, TEXT("SharedInterfaceFunction"), false);
    AddTestNode(SecondInterfaceGraph, TEXT("SecondInterfaceNode"));
    FBPInterfaceDescription& SecondInterface = Blueprint->ImplementedInterfaces.AddDefaulted_GetRef();
    SecondInterface.Interface = UInterface::StaticClass();
    SecondInterface.Graphs.Add(SecondInterfaceGraph);

    FN2CNodeTranslator& Translator = FN2CNodeTranslator::Get();
    if (!TestTrue(TEXT("Blueprint with implemented-interface graphs translates"),
        Translator.GenerateFromBlueprint(Blueprint, false)))
    {
        return false;
    }

    FN2CBlueprint Serializable = Translator.GetN2CBlueprint();
    Serializable.Metadata.BlueprintClass = TEXT("N2CGraphCoverageFixture_C");
    TestTrue(TEXT("Graph coverage fixture validates"), Serializable.IsValid());

    TSet<FString> GraphPaths;
    TSet<FString> GraphGuids;
    int32 SharedFunctionCount = 0;
    const FN2CGraph* EmptyGraphData = nullptr;
    for (const FN2CGraph& Graph : Serializable.Graphs)
    {
        if (Graph.GraphType == EN2CGraphType::ClassItSelf)
        {
            continue;
        }
        GraphPaths.Add(Graph.GraphPath);
        GraphGuids.Add(Graph.GraphGuid);
        if (Graph.Name == TEXT("SharedInterfaceFunction"))
        {
            ++SharedFunctionCount;
        }
        if (Graph.Name == TEXT("EmptyFunction"))
        {
            EmptyGraphData = &Graph;
        }
        TestEqual(
            FString::Printf(TEXT("Native node accounting matches graph '%s'"), *Graph.Name),
            Graph.NativeNodeCount,
            Graph.Nodes.Num() + Graph.SkippedNodeCount);
    }

    TestTrue(TEXT("UE implemented-interface graphs are included"),
        GraphPaths.Contains(FirstInterfaceGraph->GetPathName()) &&
        GraphPaths.Contains(SecondInterfaceGraph->GetPathName()));
    TestEqual(TEXT("Same-named interface graphs are both retained"), SharedFunctionCount, 2);
    TestEqual(TEXT("Native graph paths remain distinct"), GraphPaths.Num(), 4);
    TestEqual(TEXT("Native graph GUIDs remain distinct"), GraphGuids.Num(), 4);
    TestNotNull(TEXT("Verified empty graph is retained"), EmptyGraphData);
    if (EmptyGraphData)
    {
        TestEqual(TEXT("Empty graph has zero native nodes"), EmptyGraphData->NativeNodeCount, 0);
        TestTrue(TEXT("Empty graph is explicitly marked non-executable"), EmptyGraphData->bNoExecutableNodes);
        TestTrue(TEXT("Empty graph passes structural validation"), EmptyGraphData->IsValid());
    }

    const FString Json = FN2CSerializer::ToJson(Serializable);
    FN2CBlueprint RoundTripped;
    TestTrue(TEXT("Graph identity/count metadata round-trips"), FN2CSerializer::FromJson(Json, RoundTripped));
    if (RoundTripped.Graphs.Num() != Serializable.Graphs.Num())
    {
        AddError(TEXT("Round-tripped graph count differs from source graph count."));
        return false;
    }
    for (const FN2CGraph& Graph : Serializable.Graphs)
    {
        const FN2CGraph* Match = RoundTripped.Graphs.FindByPredicate([&Graph](const FN2CGraph& Candidate)
        {
            return Candidate.Name == Graph.Name && Candidate.GraphPath == Graph.GraphPath;
        });
        TestNotNull(FString::Printf(TEXT("Graph identity round-trips: %s"), *Graph.Name), Match);
        if (Match)
        {
            TestEqual(TEXT("Native count round-trips"), Match->NativeNodeCount, Graph.NativeNodeCount);
            TestEqual(TEXT("Skipped count round-trips"), Match->SkippedNodeCount, Graph.SkippedNodeCount);
            TestEqual(TEXT("Empty graph proof round-trips"), Match->bNoExecutableNodes, Graph.bNoExecutableNodes);
        }
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FNodeToCodeEmptyGraphIntegrityTest,
    "NodeToCode.Exporter.EmptyAndDanglingGraphs",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNodeToCodeEmptyGraphIntegrityTest::RunTest(const FString& Parameters)
{
    UBlueprint* EmptyBlueprint = MakeTestBlueprint(TEXT("N2CNoLinkFixture"));
    UEdGraph* EmptyGraph = MakeBlueprintGraph(EmptyBlueprint, TEXT("EventGraph"), true);
    UN2CTestGraphNode* Isolated = AddTestNode(EmptyGraph, TEXT("Isolated"));
    AddTestPin(Isolated, EGPD_Output, UEdGraphSchema_K2::PC_Exec, TEXT("then"));
    const FN2CBlueprint* EmptyData = nullptr;
    const FN2CGraph* EmptyModel = TranslateGraph(*this, EmptyBlueprint, TEXT("EventGraph"), EmptyData);
    if (!EmptyModel)
    {
        return false;
    }
    TestEqual(TEXT("No-link graph exports zero direct links"), EmptyModel->Connections.Num(), 0);
    TestEqual(TEXT("No-link graph has no unresolved links"), EmptyModel->UnresolvedConnectionCount, 0);

    UBlueprint* BrokenBlueprint = MakeTestBlueprint(TEXT("N2CDanglingLinkFixture"));
    UEdGraph* BrokenGraph = MakeBlueprintGraph(BrokenBlueprint, TEXT("EventGraph"), true);


    UN2CTestGraphNode* Source = AddTestNode(BrokenGraph, TEXT("Source"));
    UN2CTestGraphNode* Target = AddTestNode(BrokenGraph, TEXT("Target"));
    UEdGraphPin* SourceOut = AddTestPin(Source, EGPD_Output, UEdGraphSchema_K2::PC_Boolean, TEXT("value"));
    UEdGraphPin* TargetIn = AddTestPin(Target, EGPD_Input, UEdGraphSchema_K2::PC_Boolean, TEXT("value"));
    SourceOut->MakeLinkTo(TargetIn);
    // Corrupt the endpoint after linking to emulate malformed native pin identity.
    TargetIn->PinId = FGuid();

    const FN2CBlueprint* BrokenData = nullptr;
    const FN2CGraph* BrokenModel = TranslateGraph(*this, BrokenBlueprint, TEXT("EventGraph"), BrokenData);
    if (!BrokenModel)
    {
        return false;
    }
    TestTrue(TEXT("Malformed pin identity is counted"), BrokenModel->UnresolvedConnectionCount > 0);
    TestTrue(TEXT("Malformed link includes a concrete diagnostic"), BrokenModel->ConnectionErrors.Num() > 0);

    FN2CBlueprint BrokenSerializable = *BrokenData;
    BrokenSerializable.Metadata.BlueprintClass = TEXT("N2CTestBlueprint_C");
    const FString BrokenJson = FN2CSerializer::ToJson(BrokenSerializable);
    FN2CBlueprint Rejected;
    AddExpectedError(TEXT("Failed to parse pin metadata in graph 'EventGraph'."), EAutomationExpectedErrorFlags::Contains, 1);
    TestFalse(TEXT("Deserializer rejects an export with unresolved graph links"), FN2CSerializer::FromJson(BrokenJson, Rejected));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FNodeToCodeLegacyExecPinRoundTripTest,
    "NodeToCode.Exporter.LegacyExecPinRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNodeToCodeLegacyExecPinRoundTripTest::RunTest(const FString& Parameters)
{
    UBlueprint* Blueprint = MakeTestBlueprint(TEXT("N2CLegacyExecFixture"));
    UEdGraph* Graph = MakeBlueprintGraph(Blueprint, TEXT("EventGraph"), true);
    UN2CTestGraphNode* Source = AddTestNode(Graph, TEXT("Source"));
    UN2CTestGraphNode* Target = AddTestNode(Graph, TEXT("Target"));
    UEdGraphPin* SourceOut = AddTestPin(Source, EGPD_Output, UEdGraphSchema_K2::PC_Exec, TEXT("then"));
    UEdGraphPin* TargetIn = AddTestPin(Target, EGPD_Input, UEdGraphSchema_K2::PC_Exec, TEXT("execute"));
    SourceOut->MakeLinkTo(TargetIn);

    const FN2CBlueprint* Data = nullptr;
    const FN2CGraph* Model = TranslateGraph(*this, Blueprint, TEXT("EventGraph"), Data);
    if (!Model || !Data)
    {
        return false;
    }

    FN2CBlueprint SerializableBlueprint = *Data;
    SerializableBlueprint.Metadata.BlueprintClass = TEXT("N2CTestBlueprint_C");
    FString Json = FN2CSerializer::ToJson(SerializableBlueprint);
    TSharedPtr<FJsonObject> Root;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
    if (!TestTrue(TEXT("Serialized JSON parses for legacy fixture"), FJsonSerializer::Deserialize(Reader, Root)) ||
        !TestNotNull(TEXT("Serialized JSON root exists"), Root.Get()))
    {
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* GraphValues = nullptr;
    if (!Root->TryGetArrayField(TEXT("graphs"), GraphValues))
    {
        AddError(TEXT("Serialized JSON has no graph array."));
        return false;
    }

    for (const TSharedPtr<FJsonValue>& GraphValue : *GraphValues)
    {
        const TSharedPtr<FJsonObject> GraphObject = GraphValue.IsValid() ? GraphValue->AsObject() : nullptr;
        if (!GraphObject.IsValid())
        {
            AddError(TEXT("Serialized graph entry is invalid."));
            return false;
        }
        GraphObject->RemoveField(TEXT("pins"));
        GraphObject->RemoveField(TEXT("connections"));
        GraphObject->RemoveField(TEXT("connection_errors"));
        GraphObject->RemoveField(TEXT("unresolved_connection_count"));
        GraphObject->RemoveField(TEXT("graph_guid"));
        GraphObject->RemoveField(TEXT("graph_path"));
        GraphObject->RemoveField(TEXT("native_node_count"));
        GraphObject->RemoveField(TEXT("skipped_node_count"));
        GraphObject->RemoveField(TEXT("skipped_node_classes"));
        GraphObject->RemoveField(TEXT("no_executable_nodes"));

        const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
        if (!GraphObject->TryGetArrayField(TEXT("nodes"), Nodes))
        {
            continue;
        }
        for (const TSharedPtr<FJsonValue>& NodeValue : *Nodes)
        {
            const TSharedPtr<FJsonObject> NodeObject = NodeValue.IsValid() ? NodeValue->AsObject() : nullptr;
            if (!NodeObject.IsValid())
            {
                continue;
            }
            for (const TCHAR* PinField : { TEXT("input_pins"), TEXT("output_pins") })
            {
                const TArray<TSharedPtr<FJsonValue>>* Pins = nullptr;
                if (!NodeObject->TryGetArrayField(PinField, Pins))
                {
                    continue;
                }
                for (const TSharedPtr<FJsonValue>& PinValue : *Pins)
                {
                    const TSharedPtr<FJsonObject> PinObject = PinValue.IsValid() ? PinValue->AsObject() : nullptr;
                    if (!PinObject.IsValid())
                    {
                        continue;
                    }
                    FString Type;
                    if (PinObject->TryGetStringField(TEXT("type"), Type) && Type == TEXT("Exec"))
                    {
                        PinObject->RemoveField(TEXT("type"));
                    }
                }
            }
        }
    }

    FString LegacyJson;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&LegacyJson);
    TestTrue(TEXT("Legacy JSON serializes"), FJsonSerializer::Serialize(Root.ToSharedRef(), Writer));
    FN2CBlueprint LegacyData;
    TestTrue(TEXT("Legacy JSON without Exec pin types remains readable"), FN2CSerializer::FromJson(LegacyJson, LegacyData));

    bool bFoundExecPin = false;
    for (const FN2CGraph& LegacyGraph : LegacyData.Graphs)
    {
        for (const FN2CNodeDefinition& Node : LegacyGraph.Nodes)
        {
            for (const FN2CPinDefinition& Pin : Node.InputPins)
            {
                if (StaticEnum<EN2CPinType>()->GetNameStringByValue(static_cast<int64>(Pin.Type)) == TEXT("Exec"))
                {
                    bFoundExecPin = true;
                }
            }
            for (const FN2CPinDefinition& Pin : Node.OutputPins)
            {
                if (StaticEnum<EN2CPinType>()->GetNameStringByValue(static_cast<int64>(Pin.Type)) == TEXT("Exec"))
                {
                    bFoundExecPin = true;
                }
            }
        }
    }
    TestTrue(TEXT("Legacy Exec pins default to the Exec type"), bFoundExecPin);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FNodeToCodeRigVMGraphExportTest,
    "NodeToCode.Exporter.RigVMGraphExport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNodeToCodeRigVMGraphExportTest::RunTest(const FString& Parameters)
{
    const FString AssetPath = TEXT("/Game/Characters/Mannequins/Rigs/CR_Mannequin_Body.CR_Mannequin_Body");
    UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
    if (!TestNotNull(TEXT("FactoryGame Control Rig fixture loads read-only"), Blueprint))
    {
        return false;
    }

    IRigVMEditorAssetInterface* RigVMAsset = Cast<IRigVMEditorAssetInterface>(Blueprint);
    if (!TestNotNull(TEXT("Control Rig implements the RigVM editor asset interface"), RigVMAsset))
    {
        return false;
    }
    const TArray<URigVMGraph*> SourceGraphs = RigVMAsset->GetAllModels();
    if (!TestTrue(TEXT("Control Rig exposes RigVM models"), !SourceGraphs.IsEmpty()))
    {
        return false;
    }
    const auto RigVMPinPath = [](const URigVMPin* Pin)
    {
        FString PinPath = Pin->GetPinPath();
        if (PinPath.IsEmpty())
        {
            const URigVMNode* Node = Pin->GetNode();
            const FString NodePath = Node->GetNodePath().IsEmpty() ? Node->GetPathName() : Node->GetNodePath();
            PinPath = NodePath + TEXT(".") + Pin->GetName();
        }
        return PinPath;
    };

    FN2CNodeTranslator& Translator = FN2CNodeTranslator::Get();
    if (!TestTrue(TEXT("Control Rig Blueprint translates"), Translator.GenerateFromBlueprint(Blueprint, true)))
    {
        return false;
    }
    FN2CBlueprint Serializable = Translator.GetN2CBlueprint();

    for (const URigVMGraph* SourceGraph : SourceGraphs)
    {
        if (!SourceGraph)
        {
            AddError(TEXT("RigVM asset returned a null model graph."));
            continue;
        }
        const FString GraphPath = SourceGraph->GetPathName();
        const FN2CGraph* ExportedGraph = Serializable.Graphs.FindByPredicate([&GraphPath](const FN2CGraph& Graph)
        {
            return Graph.GraphPath == GraphPath;
        });
        if (!TestNotNull(FString::Printf(TEXT("RigVM model graph exports: %s"), *GraphPath), ExportedGraph))
        {
            continue;
        }

        TestTrue(TEXT("Model graph is marked RigVM"), ExportedGraph->GraphType == EN2CGraphType::RigVM);
        TestEqual(TEXT("Every model node is exported"), ExportedGraph->NativeNodeCount, SourceGraph->GetNodes().Num());
        TestEqual(TEXT("Every model link is exported"), ExportedGraph->Connections.Num(), SourceGraph->GetLinks().Num());
        TestEqual(TEXT("Model graph has no unresolved links"), ExportedGraph->UnresolvedConnectionCount, 0);
        TestTrue(TEXT("RigVM graph native class is recorded"), !ExportedGraph->NativeClass.IsEmpty());

        int32 NativePinCount = 0;
        TMap<FString, FString> NodeGuids;
        TMap<FString, FString> PinGuids;
        for (const URigVMNode* Node : SourceGraph->GetNodes())
        {
            if (!Node) continue;
            const FString NodePath = Node->GetNodePath().IsEmpty() ? Node->GetPathName() : Node->GetNodePath();
            NodeGuids.Add(NodePath, FGuid::NewDeterministicGuid(GraphPath + TEXT("|") + NodePath).ToString());
            for (const URigVMPin* Pin : Node->GetAllPinsRecursively())
            {
                if (!Pin) continue;
                ++NativePinCount;
                FString PinPath = RigVMPinPath(Pin);
                PinGuids.Add(PinPath, FGuid::NewDeterministicGuid(GraphPath + TEXT("|") + PinPath).ToString());
            }
        }
        TestEqual(TEXT("Every model pin is exported"), ExportedGraph->Pins.Num(), NativePinCount);

        TSet<FString> ExpectedEdges;
        for (const URigVMLink* Link : SourceGraph->GetLinks())
        {
            if (!Link || !Link->GetSourcePin() || !Link->GetTargetPin())
            {
                AddError(TEXT("RigVM fixture contains a link with an invalid endpoint."));
                continue;
            }
            const URigVMNode* SourceNode = Link->GetSourcePin()->GetNode();
            const URigVMNode* TargetNode = Link->GetTargetPin()->GetNode();
            const FString SourceNodePath = SourceNode->GetNodePath().IsEmpty() ? SourceNode->GetPathName() : SourceNode->GetNodePath();
            const FString TargetNodePath = TargetNode->GetNodePath().IsEmpty() ? TargetNode->GetPathName() : TargetNode->GetNodePath();
            ExpectedEdges.Add(NodeGuids.FindRef(SourceNodePath) + TEXT(":") +
                PinGuids.FindRef(RigVMPinPath(Link->GetSourcePin())) + TEXT("->") +
                NodeGuids.FindRef(TargetNodePath) + TEXT(":") +
                PinGuids.FindRef(RigVMPinPath(Link->GetTargetPin())));
        }
        TSet<FString> ExportedEdges;
        for (const FN2CDirectLink& Link : ExportedGraph->Connections)
        {
            ExportedEdges.Add(Link.SourceNodeGuid + TEXT(":") + Link.SourcePinGuid + TEXT("->") +
                Link.TargetNodeGuid + TEXT(":") + Link.TargetPinGuid);
        }
        TestEqual(TEXT("RigVM physical link endpoint set matches"), ExportedEdges.Num(), ExpectedEdges.Num());
        for (const FString& Edge : ExpectedEdges)
        {
            TestTrue(FString::Printf(TEXT("RigVM link is preserved: %s"), *Edge), ExportedEdges.Contains(Edge));
        }
    }

    for (const FRigVMGraphVariableDescription& Description : RigVMAsset->GetAssetVariables())
    {
        TestTrue(FString::Printf(TEXT("RigVM asset variable is exported: %s"), *Description.Name.ToString()),
            Serializable.Variables.ContainsByPredicate([&Description](const FN2CVariable& Variable)
            {
                return Variable.Name == Description.Name.ToString() && Variable.TypeName == Description.CPPType;
            }));
    }

    TestTrue(TEXT("Control Rig graph data validates"), Serializable.IsValid());

    const FN2CBlueprintExportResult ToolExport = UN2CBlueprintToolset::ExportBlueprintJson(AssetPath);
    TestTrue(TEXT("Live JSON tool exports the Control Rig asset"), ToolExport.bSuccess);
    TestEqual(TEXT("Tool reports the requested Control Rig path"), ToolExport.AssetObjectPath, AssetPath);

    const URigVMGraph* GraphForText = nullptr;
    for (const URigVMGraph* Model : SourceGraphs)
    {
        if (Model && !Model->GetNodes().IsEmpty())
        {
            GraphForText = Model;
            break;
        }
    }
    if (!TestNotNull(TEXT("A non-empty RigVM graph is available for the raw-text comparison"), GraphForText))
    {
        return false;
    }

    const FString GraphName = GraphForText->GetGraphName().IsEmpty()
        ? GraphForText->GetName() : GraphForText->GetGraphName();
    const FN2CBlueprintGraphTextResult GraphText = UN2CBlueprintToolset::ExportBlueprintGraphText(
        AssetPath, GraphName, GraphForText->GetPathName());
    TestTrue(TEXT("RigVM graph text oracle exports its model"), GraphText.bSuccess);
    TestEqual(TEXT("Graph text reports native model node count"),
        GraphText.NativeNodeCount, GraphForText->GetNodes().Num());
    TestEqual(TEXT("Graph text preserves model path"), GraphText.GraphPath, GraphForText->GetPathName());

    const FN2CGraph* GraphForTextData = Serializable.Graphs.FindByPredicate([GraphForText](const FN2CGraph& Graph)
    {
        return Graph.GraphPath == GraphForText->GetPathName();
    });
    if (!TestNotNull(TEXT("Raw-text model has a NodeToCode graph"), GraphForTextData))
    {
        return false;
    }

    TMap<FString, FString> NativePathByEndpoint;
    for (const FN2CGraphPin& Pin : GraphForTextData->Pins)
    {
        NativePathByEndpoint.Add(Pin.NodeGuid + TEXT(":") + Pin.PinGuid, Pin.NativePath);
    }
    TSet<FString> ExportedModelEdges;
    for (const FN2CDirectLink& Link : GraphForTextData->Connections)
    {
        const FString SourcePath = NativePathByEndpoint.FindRef(Link.SourceNodeGuid + TEXT(":") + Link.SourcePinGuid);
        const FString TargetPath = NativePathByEndpoint.FindRef(Link.TargetNodeGuid + TEXT(":") + Link.TargetPinGuid);
        ExportedModelEdges.Add(SourcePath + TEXT(" -> ") + TargetPath);
    }

    TSet<FString> RawModelEdges;
    TArray<FString> TextLines;
    GraphText.GraphText.ParseIntoArrayLines(TextLines, false);
    int32 RawNodeCount = 0;
    int32 RawPinCount = 0;
    for (const FString& Line : TextLines)
    {
        if (Line.StartsWith(TEXT("Node ")))
        {
            ++RawNodeCount;
        }
        else if (Line.StartsWith(TEXT("  Pin ")))
        {
            ++RawPinCount;
        }
        else if (Line.StartsWith(TEXT("Link ")))
        {
            const FString LinkText = Line.RightChop(5);
            const int32 SeparatorIndex = LinkText.Find(TEXT(" -> "));
            if (SeparatorIndex != INDEX_NONE)
            {
                RawModelEdges.Add(LinkText.Left(SeparatorIndex) + TEXT(" -> ") +
                    LinkText.Mid(SeparatorIndex + 4));
            }
        }
    }
    TestEqual(TEXT("RigVM raw node count matches model JSON"), RawNodeCount, GraphForTextData->NativeNodeCount);
    TestEqual(TEXT("RigVM raw pin count matches model JSON"), RawPinCount, GraphForTextData->Pins.Num());
    TestEqual(TEXT("RigVM raw link count matches model JSON"), RawModelEdges.Num(), ExportedModelEdges.Num());
    for (const FString& Edge : RawModelEdges)
    {
        TestTrue(FString::Printf(TEXT("RigVM raw link endpoint is preserved: %s"), *Edge),
            ExportedModelEdges.Contains(Edge));
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS