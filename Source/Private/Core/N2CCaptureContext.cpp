// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CCaptureContext.h"

#include "Core/N2CLiveness.h"
#include "Core/N2CSettings.h"
#include "Models/N2CJsonContract.h"
#include "Utils/N2CStableNames.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/ScopeExit.h"
#include "Models/N2CJsonContract.h"
#include "Utils/N2CLogger.h"
#include "Utils/N2CNodeTypeRegistry.h"
#include "Utils/Validators/N2CBlueprintValidator.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectBase.h"
#include "UObject/Class.h"
#include "UObject/UnrealTypePrivate.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    TFunction<void()>& NodeCapturedHookForTests()
    {
        static TFunction<void()> Hook;
        return Hook;
    }
}

void FN2CCaptureContext::SetNodeCapturedHookForTests(TFunction<void()> Hook)
{
    NodeCapturedHookForTests() = MoveTemp(Hook);
}
#endif

FN2CCaptureContext::FN2CCaptureContext(const FN2CCaptureOptions& InOptions)
    : Options(InOptions)
{
    // The depth limit is read once, when the capture starts
    if (Options.Depth == INDEX_NONE)
    {
        const UN2CSettings* Settings = GetDefault<UN2CSettings>();
        Options.Depth = Settings ? Settings->TranslationDepth : 0;
    }
}

FN2CCapture FN2CCaptureContext::TakeCapture()
{
    FN2CCapture Capture;
    Capture.Blueprint = MoveTemp(N2CBlueprint);
    Capture.GraphGuids = MoveTemp(GraphGuids);
    Capture.NodeGuids = MoveTemp(NodeGuids);
    Capture.Pins = MoveTemp(CapturedPins);
    return Capture;
}

void FN2CCaptureContext::BeginGraph(const FString& Name, EN2CGraphType GraphType, const UEdGraph* Source)
{
    FN2CGraph& Graph = N2CBlueprint.Graphs.AddDefaulted_GetRef();
    Graph.Name = Name;
    Graph.GraphType = GraphType;
    CurrentGraphIndex = N2CBlueprint.Graphs.Num() - 1;
    GraphGuids.Add(Source ? Source->GraphGuid : FGuid());

    if (Source)
    {
        // Visited from now on, so a graph that calls itself (directly or through others) is not queued again
        VisitedGraphs.Add(Source);

        // A nested graph from another Blueprint says whose it is: two Blueprints can have functions of the same name
        const UBlueprint* Owner = FBlueprintEditorUtils::FindBlueprintForGraph(Source);
        if (Owner && CapturedBlueprint.IsValid() && Owner != CapturedBlueprint.Get())
        {
            Graph.OwnerBlueprint = Owner->GetName();
        }
    }
}

bool FN2CCaptureContext::AssignIDs(UK2Node* Node)
{
    // Knot nodes are pass-through connections: flows are traced through them and they get no ID
    if (!Node || Node->IsA<UK2Node_Knot>())
    {
        return false;
    }

    if (!NodeIDs.Contains(Node))
    {
        const FString NodeID = FString::Printf(TEXT("N%d"), NextNodeNumber++);
        NodeIDs.Add(Node, NodeID);
        NodeGuids.Add(NodeID, Node->NodeGuid);
    }
    const FString NodeID = NodeIDs.FindChecked(Node);

    for (const UEdGraphPin* Pin : Node->Pins)
    {
        // Hidden pins are not captured and get no ID
        if (!Pin || Pin->bHidden || PinIDs.Contains(Pin))
        {
            continue;
        }
        const FString PinID = FString::Printf(TEXT("P%d"), NextPinNumber++);
        PinIDs.Add(Pin, PinID);
        CapturedPins.Add(PinID, FN2CCapturedPin{ NodeID, Node->NodeGuid, Pin->PinId });
    }
    return true;
}

bool FN2CCaptureContext::DescribeNode(UK2Node* Node, FN2CNodeDefinition& OutNodeDef)
{
    if (!Node)
    {
        FN2CLogger::Get().LogWarning(TEXT("DescribeNode called with null node"));
        return false;
    }

    // Keep a caller-supplied ID; the generated one (N1) otherwise
    const FString CallerID = OutNodeDef.ID;

    // Flows go into a scratch graph that this context throws away with everything else it built
    BeginGraph(FString(), EN2CGraphType::EventGraph);
    const bool bProcessed = AssignIDs(Node) && ProcessNode(Node, OutNodeDef);
    CurrentGraphIndex = INDEX_NONE;

    if (!CallerID.IsEmpty())
    {
        OutNodeDef.ID = CallerID;
    }
    return bProcessed;
}

bool FN2CCaptureContext::IsGhostNode(const UEdGraphNode* Node)
{
    // A placeholder the editor placed (a new Blueprint's greyed-out BeginPlay, ActorBeginOverlap and Tick): disabled,
    // never by the user, and turned into a real node as soon as a pin is wired to it (UEdGraphPin::MakeLinkTo)
    return Node && Node->IsAutomaticallyPlacedGhostNode();
}

bool FN2CCaptureContext::IsDisabledNode(const UEdGraphNode* Node)
{
    // What the compiler leaves out (FEdGraphUtilities::CloneGraph for compile): Disabled, or DevelopmentOnly when the
    // Blueprint does not compile in development mode (UK2Node::IsInDevelopmentMode)
    return Node && !Node->IsNodeEnabled();
}

bool FN2CCaptureContext::CaptureNodes(TConstArrayView<UK2Node*> InCollectedNodes)
{
    // Ghost nodes are placeholders, not logic: they are not captured at all (translation-core-18, issue #13)
    TArray<UK2Node*> CollectedNodes;
    CollectedNodes.Reserve(InCollectedNodes.Num());
    for (UK2Node* Node : InCollectedNodes)
    {
        if (!Options.bIncludeGhostNodes && IsGhostNode(Node))
        {
            FN2CLogger::Get().Log(FString::Printf(TEXT("Skipping ghost node %s (an unwired default event)"),
                *Node->GetNodeTitle(ENodeTitleType::ListView).ToString()), EN2CLogSeverity::Debug);
            continue;
        }
        CollectedNodes.Add(Node);
    }

    if (CollectedNodes.Num() == 0)
    {
        FN2CLogger::Get().LogWarning(InCollectedNodes.Num() > 0
            ? TEXT("No nodes to translate: the graph holds only unwired default events")
            : TEXT("No nodes provided to translate"));
        return false;
    }

    FN2CLogger::Get().Log(TEXT("Starting node translation"), EN2CLogSeverity::Info);

    // Get Blueprint metadata from first node (all nodes are from the same graph)
    if (UK2Node* FirstNode = CollectedNodes[0])
    {
        if (UBlueprint* Blueprint = FirstNode->GetBlueprint())
        {
            CapturedBlueprint = Blueprint;

            // Set Blueprint name
            N2CBlueprint.Metadata.Name = Blueprint->GetName();

            // Set Blueprint class
            if (UClass* BPClass = FirstNode->GetBlueprintClassFromNode())
            {
                N2CBlueprint.Metadata.BlueprintClass = N2CStableNames::ClassName(BPClass);
            }

            // Determine Blueprint type
            switch (Blueprint->BlueprintType)
            {
                case BPTYPE_Const:
                    N2CBlueprint.Metadata.BlueprintType = EN2CBlueprintType::Const;
                    break;
                case BPTYPE_MacroLibrary:
                    N2CBlueprint.Metadata.BlueprintType = EN2CBlueprintType::MacroLibrary;
                    break;
                case BPTYPE_Interface:
                    N2CBlueprint.Metadata.BlueprintType = EN2CBlueprintType::Interface;
                    break;
                case BPTYPE_LevelScript:
                    N2CBlueprint.Metadata.BlueprintType = EN2CBlueprintType::LevelScript;
                    break;
                case BPTYPE_FunctionLibrary:
                    N2CBlueprint.Metadata.BlueprintType = EN2CBlueprintType::FunctionLibrary;
                    break;
                default:
                    N2CBlueprint.Metadata.BlueprintType = EN2CBlueprintType::Normal;
                    break;
            }

            // Log Blueprint info
            FString Context = FString::Printf(TEXT("Blueprint: %s, Type: %s, Class: %s"),
                *N2CBlueprint.Metadata.Name,
                *StaticEnum<EN2CBlueprintType>()->GetNameStringByValue(static_cast<int64>(N2CBlueprint.Metadata.BlueprintType)),
                *N2CBlueprint.Metadata.BlueprintClass);
            FN2CLogger::Get().Log(TEXT("Blueprint metadata collected"), EN2CLogSeverity::Info, Context);
        }
    }

    // The main graph is Graphs[0], named and typed from the first node's graph
    FString MainGraphName;
    EN2CGraphType MainGraphType = EN2CGraphType::EventGraph;
    UEdGraph* MainGraph = CollectedNodes[0] ? CollectedNodes[0]->GetGraph() : nullptr;
    if (CollectedNodes[0])
    {
        if (UEdGraph* Graph = MainGraph)
        {
            MainGraphName = Graph->GetName();
            MainGraphType = DetermineGraphType(Graph);

            FString Context = FString::Printf(TEXT("Created graph: %s of type %s"),
                *MainGraphName,
                *StaticEnum<EN2CGraphType>()->GetNameStringByValue(static_cast<int64>(MainGraphType)));
            FN2CLogger::Get().Log(TEXT("Graph info"), EN2CLogSeverity::Debug, Context);
        }
    }
    // The main graph counts as visited before any of its nodes, so a call to itself is not queued
    BeginGraph(MainGraphName, MainGraphType, MainGraph);

    // First pass: every node and pin gets its ID in collection order, so IDs never depend on which links are met first
    for (UK2Node* Node : CollectedNodes)
    {
        AssignIDs(Node);
    }

    // Second pass: describe each node and record its flows
    for (UK2Node* Node : CollectedNodes)
    {
        if (!Node)
        {
            FN2CLogger::Get().LogWarning(TEXT("Null node encountered during translation"));
            continue;
        }

        FN2CNodeDefinition NodeDef;
        if (ProcessNode(Node, NodeDef))
        {
            CurrentGraph().Nodes.Add(NodeDef);
        }

#if WITH_DEV_AUTOMATION_TESTS
        if (NodeCapturedHookForTests())
        {
            const TFunction<void()> Hook = MoveTemp(NodeCapturedHookForTests());
            NodeCapturedHookForTests() = nullptr;
            Hook();
        }
#endif
    }
    const int32 MainGraphNodeCount = CurrentGraph().Nodes.Num();
    N2CLiveness::MarkGraph(CurrentGraph());
    CurrentGraphIndex = INDEX_NONE;

    // Process any additional graphs that were discovered, shallowest first: every graph at depth N is processed
    // (and queues its own children at N + 1) before any graph at N + 1, so a graph is always reached by its shortest
    // call path and the depth limit counts calls from the captured graph. Within one depth the most recently queued
    // graph goes first, as before.
    while (AdditionalGraphsToProcess.Num() > 0)
    {
        int32 NextIndex = AdditionalGraphsToProcess.Num() - 1;
        for (int32 Index = NextIndex - 1; Index >= 0; --Index)
        {
            if (AdditionalGraphsToProcess[Index].Depth < AdditionalGraphsToProcess[NextIndex].Depth)
            {
                NextIndex = Index;
            }
        }
        const FGraphProcessInfo GraphInfo = AdditionalGraphsToProcess[NextIndex];
        AdditionalGraphsToProcess.RemoveAt(NextIndex);
        if (UEdGraph* QueuedGraph = GraphInfo.Graph.Get())
        {
            // The graph's own depth, so the graphs it queues are checked and queued at depth + 1
            CurrentDepth = GraphInfo.Depth;
            ProcessGraph(QueuedGraph, DetermineGraphType(QueuedGraph));
        }
    }
    CurrentDepth = 0;

    FString Context = FString::Printf(TEXT("Translated %d nodes in %d graphs"),
        MainGraphNodeCount,
        N2CBlueprint.Graphs.Num());
    FN2CLogger::Get().Log(TEXT("Node translation complete"), EN2CLogSeverity::Info, Context);

    return N2CBlueprint.Graphs.Num() > 0;
}

bool FN2CCaptureContext::ProcessNode(UK2Node* Node, FN2CNodeDefinition& OutNodeDef)
{
    // Knots are skipped (they have no ID); every other node got its ID in the first pass over its graph
    const FString* NodeID = Node ? NodeIDs.Find(Node) : nullptr;
    if (!NodeID)
    {
        return false;
    }
    OutNodeDef.ID = *NodeID;

    ProcessNodeTypeAndProperties(Node, OutNodeDef);
    OutNodeDef.bDisabled = IsDisabledNode(Node);

    // Track pin connections for flows
    TArray<UEdGraphPin*> ExecInputs;
    TArray<UEdGraphPin*> ExecOutputs;
    ProcessNodePins(Node, OutNodeDef, ExecInputs, ExecOutputs);
    ProcessNodeFlows(Node, ExecInputs, ExecOutputs);
    LogNodeDetails(OutNodeDef);

    return true;
}

void FN2CCaptureContext::AddGraphToProcess(UEdGraph* Graph)
{
    if (!Options.bDiscoverNestedGraphs)
    {
        return;
    }
    if (!Graph)
    {
        return;
    }

    // A graph captured or being captured (the one calling it included) is never queued again. By the graph itself,
    // not its name: two Blueprints can each have a function of the same name.
    if (VisitedGraphs.Contains(Graph))
    {
        return;
    }

    // Check if this is a user-created graph
    bool bIsUserCreated = false;

    // Check if this is a composite/collapsed graph
    if (Cast<UK2Node_Composite>(Graph->GetOuter()))
    {
        bIsUserCreated = !Graph->GetOuter()->IsA<UK2Node_MathExpression>();  

        if (bIsUserCreated)
        {
            FString Context = FString::Printf(TEXT("Found composite graph: %s"), *Graph->GetName());
            FN2CLogger::Get().Log(Context, EN2CLogSeverity::Debug);
        }
    }
    // Otherwise check if it's in a user content directory
    else if (UBlueprint* OwningBP = Cast<UBlueprint>(Graph->GetOuter()))
    {
        // Check if the Blueprint is in a user content directory
        FString BlueprintPath = OwningBP->GetPathName();
        bIsUserCreated = BlueprintPath.Contains(TEXT("/Game/")) || 
                        BlueprintPath.Contains(TEXT("/Content/"));

        // For macros, also check if it's a user-created macro
        if (UK2Node_MacroInstance* MacroNode = Cast<UK2Node_MacroInstance>(Graph->GetOuter()))
        {
            if (UEdGraph* MacroGraph = MacroNode->GetMacroGraph())
            {
                if (UBlueprint* MacroBP = Cast<UBlueprint>(MacroGraph->GetOuter()))
                {
                    FString MacroPath = MacroBP->GetPathName();
                    bIsUserCreated = MacroPath.Contains(TEXT("/Game/")) || 
                                   MacroPath.Contains(TEXT("/Content/"));
                }
            }
        }
    }

    // Only process user-created graphs
    if (bIsUserCreated)
    {
        // Now check recursion depth limit since we know it's a user graph
        int32 NextDepth = CurrentDepth + 1;
        if (NextDepth > Options.Depth)
        {
            FString Context = FString::Printf(TEXT("Skipping graph '%s' - maximum translation depth reached (%d)"), 
                *Graph->GetName(), Options.Depth);
            FN2CLogger::Get().Log(Context, EN2CLogSeverity::Warning);
            return;
        }

        // Check if we already have this graph queued
        bool bAlreadyQueued = false;
        for (const FGraphProcessInfo& Info : AdditionalGraphsToProcess)
        {
            if (Info.Graph == Graph)
            {
                bAlreadyQueued = true;
                break;
            }
        }

        if (!bAlreadyQueued)
        {
            FString Context = FString::Printf(TEXT("Adding user-created graph to process: %s (Depth: %d)"), 
                *Graph->GetName(), NextDepth);
            FN2CLogger::Get().Log(Context, EN2CLogSeverity::Debug);
            AdditionalGraphsToProcess.Add(FGraphProcessInfo(Graph, NextDepth));
        }
    }
    else
    {
        // Only log at Debug severity since this is expected behavior
        FString Context = FString::Printf(TEXT("Skipping engine graph: %s"), *Graph->GetName());
        FN2CLogger::Get().Log(Context, EN2CLogSeverity::Debug);
    }
}

bool FN2CCaptureContext::ProcessGraph(UEdGraph* Graph, EN2CGraphType GraphType)
{
    if (!Graph)
    {
        FN2CLogger::Get().LogWarning(TEXT("Attempted to process null graph"));
        return false;
    }

    // Write the graph in place at the end of Graphs; it is removed again if it gets no nodes
    BeginGraph(Graph->GetName(), GraphType, Graph);
    ON_SCOPE_EXIT { CurrentGraphIndex = INDEX_NONE; };

    // First pass: IDs for every node and pin in graph order; second pass: the nodes and their flows
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (Options.bIncludeGhostNodes || !IsGhostNode(Node))
        {
            AssignIDs(Cast<UK2Node>(Node));
        }
    }
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        UK2Node* K2Node = Cast<UK2Node>(Node);
        if (K2Node && (Options.bIncludeGhostNodes || !IsGhostNode(K2Node)))
        {
            FN2CNodeDefinition NodeDef;
            if (ProcessNode(K2Node, NodeDef))
            {
                CurrentGraph().Nodes.Add(NodeDef);
            }
        }
    }

    // Keep the graph if it has nodes
    FN2CGraph& NewGraph = CurrentGraph();
    if (NewGraph.Nodes.Num() > 0)
    {
        N2CLiveness::MarkGraph(NewGraph);

        // Validate all flow references
        if (!ValidateFlowReferences(NewGraph))
        {
            FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Flow validation issues found in graph: %s"), *NewGraph.Name));
        }

        FString Context = FString::Printf(TEXT("Processed graph: %s with %d nodes"), 
            *NewGraph.Name, 
            NewGraph.Nodes.Num());
        FN2CLogger::Get().Log(TEXT("Graph processing complete"), EN2CLogSeverity::Info, Context);
        
        return true;
    }

    N2CBlueprint.Graphs.RemoveAt(CurrentGraphIndex);
    GraphGuids.RemoveAt(CurrentGraphIndex);
    return false;
}

EN2CGraphType FN2CCaptureContext::DetermineGraphType(UEdGraph* Graph) const
{
    if (!Graph)
    {
        return EN2CGraphType::EventGraph;
    }

    // First check if this is a function graph by looking for function entry node
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node->IsA<UK2Node_FunctionEntry>())
        {
            return EN2CGraphType::Function;
        }
    }

    // Check for composite/collapsed graphs
    if (Cast<UK2Node_Composite>(Graph->GetOuter()))
    {
        return EN2CGraphType::Composite;
    }

    // Check graph name for common patterns
    FString GraphName = Graph->GetName().ToLower();
    
    if (GraphName.Contains(TEXT("construction")))
    {
        return EN2CGraphType::Construction;
    }
    
    if (GraphName.Contains(TEXT("macro")))
    {
        return EN2CGraphType::Macro;
    }
    
    if (GraphName.Contains(TEXT("animation")))
    {
        return EN2CGraphType::Animation;
    }

    // Default to event graph
    return EN2CGraphType::EventGraph;
}

void FN2CCaptureContext::DetermineNodeType(UK2Node* Node, EN2CNodeType& OutType)
{
    OutType = FN2CNodeTypeRegistry::Get().GetNodeType(Node);
}

EN2CPinType FN2CCaptureContext::DeterminePinType(const UEdGraphPin* Pin) const
{
    if (!Pin) return EN2CPinType::Wildcard;

    const FName& PinCategory = Pin->PinType.PinCategory;
    
    // Handle execution pins
    if (PinCategory == UEdGraphSchema_K2::PC_Exec)
        return EN2CPinType::Exec;
        
    // Handle basic types
    if (PinCategory == UEdGraphSchema_K2::PC_Boolean)
        return EN2CPinType::Boolean;
    if (PinCategory == UEdGraphSchema_K2::PC_Byte)
        return EN2CPinType::Byte;
    if (PinCategory == UEdGraphSchema_K2::PC_Int)
        return EN2CPinType::Integer;
    if (PinCategory == UEdGraphSchema_K2::PC_Int64)
        return EN2CPinType::Integer64;
    if (PinCategory == UEdGraphSchema_K2::PC_Float)
        return EN2CPinType::Float;
    if (PinCategory == UEdGraphSchema_K2::PC_Double)
        return EN2CPinType::Double;
    if (PinCategory == UEdGraphSchema_K2::PC_Real)
        return EN2CPinType::Real;
    if (PinCategory == UEdGraphSchema_K2::PC_String)
        return EN2CPinType::String;
    if (PinCategory == UEdGraphSchema_K2::PC_Name)
        return EN2CPinType::Name;
    if (PinCategory == UEdGraphSchema_K2::PC_Text)
        return EN2CPinType::Text;
        
    // Handle object types
    if (PinCategory == UEdGraphSchema_K2::PC_Object)
        return EN2CPinType::Object;
    if (PinCategory == UEdGraphSchema_K2::PC_Class)
        return EN2CPinType::Class;
    if (PinCategory == UEdGraphSchema_K2::PC_Interface)
        return EN2CPinType::Interface;
    if (PinCategory == UEdGraphSchema_K2::PC_Struct)
        return EN2CPinType::Struct;
    if (PinCategory == UEdGraphSchema_K2::PC_Enum)
        return EN2CPinType::Enum;
        
    // Handle delegate types
    if (PinCategory == UEdGraphSchema_K2::PC_Delegate)
        return EN2CPinType::Delegate;
    if (PinCategory == UEdGraphSchema_K2::PC_MCDelegate)
        return EN2CPinType::MulticastDelegate;
        
    // Handle field path and special types
    if (PinCategory == UEdGraphSchema_K2::PC_FieldPath)
        return EN2CPinType::FieldPath;
    if (PinCategory == UEdGraphSchema_K2::PC_Wildcard)
        return EN2CPinType::Wildcard;
        
    // Handle soft references
    if (PinCategory == UEdGraphSchema_K2::PC_SoftObject)
        return EN2CPinType::SoftObject;
    if (PinCategory == UEdGraphSchema_K2::PC_SoftClass)
        return EN2CPinType::SoftClass;

    // Handle special subcategories
    if (Pin->PinType.PinSubCategory == UEdGraphSchema_K2::PSC_Bitmask)
        return EN2CPinType::Bitmask;
    if (Pin->PinType.PinSubCategory == UEdGraphSchema_K2::PSC_Self)
        return EN2CPinType::Self;
    if (Pin->PinType.PinSubCategory == UEdGraphSchema_K2::PSC_Index)
        return EN2CPinType::Index;

    // Default to wildcard for unknown types
    return EN2CPinType::Wildcard;
}

TArray<UEdGraphPin*> FN2CCaptureContext::TraceTargetsThroughKnots(UEdGraphPin* OutputPin) const
{
    // Depth first in link order: every non-knot pin the output reaches, each once, following every branch of a knot
    TArray<UEdGraphPin*> Targets;
    if (!OutputPin)
    {
        return Targets;
    }
    TSet<const UEdGraphNode*> VisitedKnots;
    TArray<UEdGraphPin*> Stack;
    for (int32 Index = OutputPin->LinkedTo.Num() - 1; Index >= 0; --Index)
    {
        Stack.Push(OutputPin->LinkedTo[Index]);
    }
    while (Stack.Num() > 0)
    {
        UEdGraphPin* Linked = Stack.Pop(EAllowShrinking::No);
        UEdGraphNode* LinkedNode = Linked ? Linked->GetOwningNode() : nullptr;
        if (!LinkedNode)
        {
            continue;
        }
        if (!LinkedNode->IsA<UK2Node_Knot>())
        {
            Targets.AddUnique(Linked);
            continue;
        }
        bool bAlreadyVisited = false;
        VisitedKnots.Add(LinkedNode, &bAlreadyVisited);
        if (bAlreadyVisited)
        {
            continue;
        }
        // A knot passes its input on to every link of its output
        UK2Node_Knot* Knot = CastChecked<UK2Node_Knot>(LinkedNode);
        if (UEdGraphPin* KnotOutput = Knot->GetOutputPin())
        {
            for (int32 Index = KnotOutput->LinkedTo.Num() - 1; Index >= 0; --Index)
            {
                Stack.Push(KnotOutput->LinkedTo[Index]);
            }
        }
    }
    return Targets;
}

TArray<UEdGraphPin*> FN2CCaptureContext::TraceExecTargets(UEdGraphPin* ExecOutput) const
{
    // The exec inputs an output runs, as the compiler sees them: a disabled node is left out, and exec passes through
    // it to whatever its exec output runs when it has exactly one exec input and one exec output
    // (UK2Node::GetPassThroughPin); any other disabled node ends the chain there (translation-core-18)
    TArray<UEdGraphPin*> Targets;
    TSet<const UEdGraphNode*> VisitedDisabled;
    TArray<UEdGraphPin*> Outputs;
    Outputs.Push(ExecOutput);
    while (Outputs.Num() > 0)
    {
        for (UEdGraphPin* Target : TraceTargetsThroughKnots(Outputs.Pop(EAllowShrinking::No)))
        {
            UEdGraphNode* TargetNode = Target->GetOwningNode();
            if (!IsDisabledNode(TargetNode))
            {
                Targets.AddUnique(Target);
                continue;
            }
            bool bAlreadyVisited = false;
            VisitedDisabled.Add(TargetNode, &bAlreadyVisited);
            UEdGraphPin* PassThrough = bAlreadyVisited ? nullptr : TargetNode->GetPassThroughPin(Target);
            if (PassThrough && PassThrough->Direction == EGPD_Output)
            {
                Outputs.Push(PassThrough);
            }
        }
    }
    return Targets;
}

bool FN2CCaptureContext::ValidateFlowReferences(FN2CGraph& Graph)
{
    // Use the blueprint validator to validate the graph
    FN2CBlueprintValidator Validator;
    FString ErrorMessage;
    return Validator.ValidateFlowReferences(Graph, ErrorMessage);
}

void FN2CCaptureContext::ProcessNodeTypeAndProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef)
{
    // Determine node type
    DetermineNodeType(Node, OutNodeDef.NodeType);

    // Get the appropriate processor for this node type
    TSharedPtr<IN2CNodeProcessor> Processor = FN2CNodeProcessorFactory::Get().GetProcessor(OutNodeDef.NodeType);
    if (Processor.IsValid())
    {
        // Process the node using the processor
        if (!Processor->Process(Node, OutNodeDef))
        {
            FString NodeTypeName = StaticEnum<EN2CNodeType>()->GetNameStringByValue(static_cast<int64>(OutNodeDef.NodeType));
            FString NodeTitle = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
            
            FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Node processor failed for node '%s' of type %s"),
                *NodeTitle, *NodeTypeName));
            
            // Fall back to the old method if processor fails
            FallbackProcessNodeProperties(Node, OutNodeDef);
        }
        else
        {
            // Log successful processing
            FString NodeTypeName = StaticEnum<EN2CNodeType>()->GetNameStringByValue(static_cast<int64>(OutNodeDef.NodeType));
            FN2CLogger::Get().Log(FString::Printf(TEXT("Successfully processed node '%s' using %s processor"),
                *OutNodeDef.Title, *NodeTypeName), EN2CLogSeverity::Debug);
        }
    }
    else
    {
        // Fall back to the old method if no processor is available
        FString NodeTypeName = StaticEnum<EN2CNodeType>()->GetNameStringByValue(static_cast<int64>(OutNodeDef.NodeType));
        FString NodeTitle = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
        
        FN2CLogger::Get().LogWarning(FString::Printf(TEXT("No processor available for node '%s' of type %s"),
            *NodeTitle, *NodeTypeName));
        
        FallbackProcessNodeProperties(Node, OutNodeDef);
    }

    // Stable identity (card 0138): the node class; the title stays only as a hint that says more than member_name
    OutNodeDef.NodeClass = Node->GetClass()->GetName();
    if (N2CJsonContract::IsRedundantHint(OutNodeDef.Title, OutNodeDef.MemberName))
    {
        OutNodeDef.Title.Reset();
    }

    // Process any struct or enum types used in this node
    if (Options.bIncludeRelatedTypes)
    {
        ProcessRelatedTypes(Node, OutNodeDef);
    }

    // Check for nested graphs that might need processing
    if (UK2Node_Composite* CompositeNode = Cast<UK2Node_Composite>(Node))
    {
        if (UEdGraph* BoundGraph = CompositeNode->BoundGraph)
        {
            AddGraphToProcess(BoundGraph);
        }
    }
    else if (UK2Node_MacroInstance* MacroNode = Cast<UK2Node_MacroInstance>(Node))
    {
        if (UEdGraph* MacroGraph = MacroNode->GetMacroGraph())
        {
            AddGraphToProcess(MacroGraph);
        }
    }
    else if (UK2Node_CallFunction* FuncNode = Cast<UK2Node_CallFunction>(Node))
    {
        if (UFunction* Function = FuncNode->GetTargetFunction())
        {
            if (UBlueprintGeneratedClass* BlueprintClass = Cast<UBlueprintGeneratedClass>(Function->GetOwnerClass()))
            {
                if (UBlueprint* FunctionBlueprint = Cast<UBlueprint>(BlueprintClass->ClassGeneratedBy))
                {
                    // Find and add the function graph
                    for (UEdGraph* FuncGraph : FunctionBlueprint->FunctionGraphs)
                    {
                        if (FuncGraph && FuncGraph->GetFName() == Function->GetFName())
                        {
                            AddGraphToProcess(FuncGraph);
                            break;
                        }
                    }
                }
            }
        }
    }
    else if (UK2Node_CreateDelegate* CreateDelegateNode = Cast<UK2Node_CreateDelegate>(Node))
    {
        // Try to find and add the function graph
        if (UClass* ScopeClass = CreateDelegateNode->GetScopeClass())
        {
            if (UBlueprint* BP = Cast<UBlueprint>(ScopeClass->ClassGeneratedBy))
            {
                for (UEdGraph* FuncGraph : BP->FunctionGraphs)
                {
                    if (FuncGraph && FuncGraph->GetFName() == CreateDelegateNode->GetFunctionName())
                    {
                        AddGraphToProcess(FuncGraph);
                        FN2CLogger::Get().Log(
                            FString::Printf(TEXT("Added delegate function graph to process: %s"), *FuncGraph->GetName()),
                            EN2CLogSeverity::Debug);
                        break;
                    }
                }
            }
        }
        else if (UFunction* DelegateSignature = CreateDelegateNode->GetDelegateSignature())
        {
            if (UBlueprintGeneratedClass* BlueprintClass = Cast<UBlueprintGeneratedClass>(DelegateSignature->GetOwnerClass()))
            {
                if (UBlueprint* FunctionBlueprint = Cast<UBlueprint>(BlueprintClass->ClassGeneratedBy))
                {
                    // Find and add the function graph
                    for (UEdGraph* FuncGraph : FunctionBlueprint->FunctionGraphs)
                    {
                        if (FuncGraph && FuncGraph->GetFName() == DelegateSignature->GetFName())
                        {
                            AddGraphToProcess(FuncGraph);
                            FN2CLogger::Get().Log(
                                FString::Printf(TEXT("Added delegate signature graph to process: %s"), *FuncGraph->GetName()),
                                EN2CLogSeverity::Debug);
                            break;
                        }
                    }
                }
            }
        }
    }
}

void FN2CCaptureContext::FallbackProcessNodeProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef)
{
    // The title hint, in source text so it is the same in every editor language
    OutNodeDef.Title = N2CStableNames::NodeTitle(Node);

    // Determine node specific data directly such as
    // MemberParent, MemberName, bLatent, 
    DetermineNodeSpecificProperties(Node, OutNodeDef);
    
    // Extract comments if present
    if (Node->NodeComment.Len() > 0) { OutNodeDef.Comment = Node->NodeComment; }
    
    // Set purity from node
    OutNodeDef.bPure = Node->IsNodePure();
}

void FN2CCaptureContext::ProcessNodePins(UK2Node* Node, FN2CNodeDefinition& OutNodeDef, TArray<UEdGraphPin*>& OutExecInputs, TArray<UEdGraphPin*>& OutExecOutputs)
{
    // Process input pins
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (!Pin) continue;

        // Skip hidden pins entirely
        if (Pin->bHidden)
        {
            continue;
        }

        FN2CPinDefinition PinDef;
        
        // The pin's ID from the first pass over its graph
        const FString PinID = PinIDs.FindRef(Pin);
        PinDef.ID = PinID;

        // Debug logging for pin mapping
        FN2CLogger::Get().Log(FString::Printf(TEXT("ProcessNodePins: Pin GUID %s has ID %s (Pin Name: %s, Node: %s)"), 
            *Pin->PinId.ToString(), *PinID, *Pin->GetDisplayName().ToString(), 
            *Node->GetNodeTitle(ENodeTitleType::ListView).ToString()), EN2CLogSeverity::Debug);
        
        // The pin's identifier (PinName: "execute", "then", "ReturnValue"), never its display name, which changes with
        // the editor language and the friendly-names preference and is blank for exec pins (card 0138); the display
        // label only as a hint, when it says more than the name
        PinDef.Name = Pin->PinName.ToString();
        PinDef.Label = N2CStableNames::PinLabel(Pin);
        
        // Determine pin type
        PinDef.Type = DeterminePinType(Pin);
        
        // Set pin metadata
        PinDef.bConnected = Pin->LinkedTo.Num() > 0;
        PinDef.bIsReference = Pin->PinType.bIsReference;
        PinDef.bIsConst = Pin->PinType.bIsConst;
        PinDef.bIsArray = Pin->PinType.ContainerType == EPinContainerType::Array;
        PinDef.bIsMap = Pin->PinType.ContainerType == EPinContainerType::Map;
        PinDef.bIsSet = Pin->PinType.ContainerType == EPinContainerType::Set;

        // Get default value if any
        if (!Pin->DefaultValue.IsEmpty())
        {
            PinDef.DefaultValue = Pin->DefaultValue;
        }
        else if (Pin->DefaultObject)
        {
            PinDef.DefaultValue = Pin->DefaultObject->GetPathName();
        }
        else if (!Pin->DefaultTextValue.IsEmpty())
        {
            PinDef.DefaultValue = N2CStableNames::SourceText(Pin->DefaultTextValue);
        }
        
        // Set subtype for container/object types
        if (UK2Node_CreateDelegate* CreateDelegateNode = Cast<UK2Node_CreateDelegate>(Node))
        {
            // For delegate output pin on CreateDelegate node, use function name as subtype
            if (Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == TEXT("delegate"))
            {
                PinDef.SubType = CreateDelegateNode->GetFunctionName().ToString();
            }
            
            if (Pin->Direction == EGPD_Input && Pin->PinType.PinCategory == TEXT("object"))
            {
                if (UClass* ScopeClass = CreateDelegateNode->GetScopeClass())
                {
                    PinDef.SubType = N2CStableNames::ClassName(ScopeClass);
                }
            }
        }
        else if (Pin->PinType.PinSubCategoryObject.IsValid())
        {
            PinDef.SubType = N2CStableNames::TypeObjectName(Pin->PinType.PinSubCategoryObject.Get());
        }
        else if (!Pin->PinType.PinSubCategory.IsNone())
        {
            PinDef.SubType = Pin->PinType.PinSubCategory.ToString();
        }
        
        // Add to appropriate pin array
        if (Pin->Direction == EGPD_Input)
        {
            OutNodeDef.InputPins.Add(PinDef);
            
            if (Pin->PinType.PinCategory == TEXT("exec"))
            {
                OutExecInputs.Add(Pin);
                FString PinContext = FString::Printf(TEXT("Found exec input pin: %s on node %s"),
                    *Pin->GetDisplayName().ToString(),
                    *Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
                FN2CLogger::Get().Log(PinContext, EN2CLogSeverity::Debug);
            }
        }
        else if (Pin->Direction == EGPD_Output)
        {
            OutNodeDef.OutputPins.Add(PinDef);
            
            // Track execution outputs
            if (Pin->PinType.PinCategory == TEXT("exec"))
            {
                OutExecOutputs.Add(Pin);
                FString PinContext = FString::Printf(TEXT("Found exec output pin: %s on node %s"),
                    *Pin->GetDisplayName().ToString(),
                    *Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
                FN2CLogger::Get().Log(PinContext, EN2CLogSeverity::Debug);
            }
        }
    }
}

void FN2CCaptureContext::ProcessNodeFlows(UK2Node* Node, const TArray<UEdGraphPin*>& ExecInputs, const TArray<UEdGraphPin*>& ExecOutputs)
{
    // Record execution flows: one entry per link, from the exec output pin to the exec input pin it runs
    // ("N3.P5->N4.P1"), through knots and every branch of their fan-out, so Branch True/False, Switch cases, Sequence
    // outputs and loop pins stay apart, as do Gate Enter/Open/Close and Timeline Play/Reverse (translation-core-2)
    FString ExecDebug = FString::Printf(TEXT("Node %s has %d exec outputs"), 
        *Node->GetNodeTitle(ENodeTitleType::ListView).ToString(),
        ExecOutputs.Num());
    FN2CLogger::Get().Log(ExecDebug, EN2CLogSeverity::Debug);

    // A disabled node runs nothing: the links into it go on to where exec passes through it, if anywhere
    const FString SourceNodeID = NodeIDs.FindRef(Node);
    for (UEdGraphPin* ExecOutput : IsDisabledNode(Node) ? TArray<UEdGraphPin*>() : ExecOutputs)
    {
        const FString SourcePinID = ExecOutput ? PinIDs.FindRef(ExecOutput) : FString();
        if (SourceNodeID.IsEmpty() || SourcePinID.IsEmpty())
        {
            continue;
        }

        FString PinDebug = FString::Printf(TEXT("Exec output pin %s has %d connections"), 
            *ExecOutput->GetDisplayName().ToString(),
            ExecOutput->LinkedTo.Num());
        FN2CLogger::Get().Log(PinDebug, EN2CLogSeverity::Debug);

        for (UEdGraphPin* TargetPin : TraceExecTargets(ExecOutput))
        {
            const UEdGraphNode* TargetNode = TargetPin->GetOwningNode();
            const FString TargetNodeID = NodeIDs.FindRef(TargetNode);
            const FString TargetPinID = PinIDs.FindRef(TargetPin);
            if (TargetNodeID.IsEmpty() || TargetPinID.IsEmpty())
            {
                // The target is outside the capture (or hidden): there is no ID to point at
                FN2CLogger::Get().Log(FString::Printf(TEXT("Exec link from %s.%s to %s, outside the capture, is not recorded"),
                    *SourceNodeID, *SourcePinID, TargetNode ? *TargetNode->GetNodeTitle(ENodeTitleType::ListView).ToString() : TEXT("(null)")),
                    EN2CLogSeverity::Debug);
                continue;
            }

            CurrentGraph().Flows.Execution.AddUnique(N2CJsonContract::MakeExecEdge(SourceNodeID, SourcePinID, TargetNodeID, TargetPinID));

            FN2CLogger::Get().Log(FString::Printf(TEXT("Added execution flow: %s.%s (%s.%s) -> %s.%s (%s.%s)"),
                *SourceNodeID, *SourcePinID, *Node->GetNodeTitle(ENodeTitleType::ListView).ToString(),
                *ExecOutput->GetDisplayName().ToString(), *TargetNodeID, *TargetPinID,
                *TargetNode->GetNodeTitle(ENodeTitleType::ListView).ToString(), *TargetPin->GetDisplayName().ToString()),
                EN2CLogSeverity::Debug);
        }
    }

    // Record data flows: each edge once, from its output pin, to every input it reaches (through knots and their
    // fan-out), so an output feeding several inputs keeps all of them (translation-core-1, PR #7)
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (!Pin || Pin->Direction != EGPD_Output || Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
        {
            continue;
        }
        const FString SourcePinID = PinIDs.FindRef(Pin);
        if (SourceNodeID.IsEmpty() || SourcePinID.IsEmpty())
        {
            continue;
        }
        const FString SourceRef = FString::Printf(TEXT("%s.%s"), *SourceNodeID, *SourcePinID);

        for (UEdGraphPin* TargetPin : TraceTargetsThroughKnots(Pin))
        {
            const UEdGraphNode* TargetNode = TargetPin->GetOwningNode();
            const FString TargetNodeID = NodeIDs.FindRef(TargetNode);
            const FString TargetPinID = PinIDs.FindRef(TargetPin);
            if (TargetNodeID.IsEmpty() || TargetPinID.IsEmpty())
            {
                // The input is outside the capture (or hidden): there is no ID to point at
                continue;
            }
            const FString TargetRef = FString::Printf(TEXT("%s.%s"), *TargetNodeID, *TargetPinID);
            CurrentGraph().Flows.Data.FindOrAdd(SourceRef).Targets.AddUnique(TargetRef);

            FN2CLogger::Get().Log(FString::Printf(TEXT("Added data flow: %s (%s.%s) -> %s (%s.%s)"),
                *SourceRef, *Node->GetNodeTitle(ENodeTitleType::ListView).ToString(), *Pin->GetDisplayName().ToString(),
                *TargetRef, *TargetNode->GetNodeTitle(ENodeTitleType::ListView).ToString(),
                *TargetPin->GetDisplayName().ToString()), EN2CLogSeverity::Debug);
        }
    }
}

FN2CEnum FN2CCaptureContext::ProcessBlueprintEnum(UEnum* Enum)
{
    FN2CEnum Result;
    
    if (!Enum)
    {
        FN2CLogger::Get().LogError(TEXT("Null enum provided to ProcessBlueprintEnum"));
        return Result;
    }
    
    // Get enum path
    FString EnumPath = Enum->GetPathName();
    FString EnumName = Enum->GetName();
    
    FN2CLogger::Get().Log(
        FString::Printf(TEXT("ProcessBlueprintEnum: Processing enum '%s' (Path: %s)"), 
            *EnumName, *EnumPath),
        EN2CLogSeverity::Info);
    
    // Check if we've already processed this enum
    if (ProcessedEnumPaths.Contains(EnumPath))
    {
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Enum %s already processed - skipping"), *EnumPath),
            EN2CLogSeverity::Debug);
        return Result;
    }
    
    // Mark as processed
    ProcessedEnumPaths.Add(EnumPath);
    FN2CLogger::Get().Log(TEXT("Added enum to processed paths"), EN2CLogSeverity::Debug);
    
    // Set basic enum info
    Result.Name = EnumName;
    
    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Enum details: Name=%s"), 
            *Result.Name),
        EN2CLogSeverity::Debug);
    
    // Get enum comment if available
    FString EnumComment = Enum->GetMetaData(TEXT("ToolTip"));
    
    if (!EnumComment.IsEmpty())
    {
        Result.Comment = EnumComment;
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Enum comment: %s"), *Result.Comment),
            EN2CLogSeverity::Debug);
    }
    
    // Process enum values
    int32 NumEnums = Enum->NumEnums();
    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Enum has %d values according to NumEnums()"), NumEnums),
        EN2CLogSeverity::Debug);
    
    // Log all enum names and values for debugging
    for (int32 i = 0; i < NumEnums; ++i)
    {
        FString ValueName = N2CStableNames::SourceText(Enum->GetDisplayNameTextByIndex(i));
        
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Enum value #%d: Name='%s'"), 
                i, *ValueName),
            EN2CLogSeverity::Debug);
            
        // Check if this is a hidden enum value (like _MAX or similar)
        bool bIsHidden = ValueName.Contains(TEXT("_MAX")) || 
                         ValueName.Contains(TEXT("MAX_")) ||
                         ValueName.Contains(TEXT("E MAX")) ||
                         ValueName.Contains(TEXT("MAX")) ||
                         ValueName.StartsWith(TEXT("_")) ||
                         ValueName.EndsWith(TEXT("_None"));
                         
        if (bIsHidden)
        {
            FN2CLogger::Get().Log(
                FString::Printf(TEXT("  -> Skipping hidden value '%s'"), *ValueName),
                EN2CLogSeverity::Debug);
            continue; // Skip adding this value
        }
        
        FN2CEnumValue Value;
        Value.Name = ValueName;
        
        // Get value comment if available
        FString ValueComment;
        if (Enum->HasMetaData(TEXT("ToolTip"), i))
        {
            ValueComment = Enum->GetMetaData(TEXT("ToolTip"), i);
            Value.Comment = ValueComment;
            FN2CLogger::Get().Log(
                FString::Printf(TEXT("  -> Comment: %s"), *ValueComment),
                EN2CLogSeverity::Debug);
        }
        
        Result.Values.Add(Value);
    }
    
    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Processed enum %s with %d values"), 
            *Result.Name, 
            Result.Values.Num()),
        EN2CLogSeverity::Info);
    
    return Result;
}

bool FN2CCaptureContext::IsBlueprintStruct(UScriptStruct* Struct) const
{
    if (!Struct)
    {
        return false;
    }
    
    // Check if the struct is in a user content directory
    FString StructPath = Struct->GetPathName();
    return StructPath.Contains(TEXT("/Game/")) || 
           StructPath.Contains(TEXT("/Content/"));
}

bool FN2CCaptureContext::IsBlueprintEnum(UEnum* Enum) const
{
    if (!Enum)
    {
        return false;
    }
    
    // Check if the enum is in a user content directory
    FString EnumPath = Enum->GetPathName();
    return EnumPath.Contains(TEXT("/Game/")) || 
           EnumPath.Contains(TEXT("/Content/"));
}

EN2CStructMemberType FN2CCaptureContext::ConvertPropertyToStructMemberType(FProperty* Property) const
{
    if (!Property)
    {
        FN2CLogger::Get().LogWarning(TEXT("ConvertPropertyToStructMemberType: Null property provided"));
        return EN2CStructMemberType::Int; // Default
    }
    
    FString PropertyName = Property->GetName();
    FN2CLogger::Get().Log(FString::Printf(TEXT("ConvertPropertyToStructMemberType: Property '%s'"), 
        *PropertyName), EN2CLogSeverity::Debug);
    
    if (CastField<FBoolProperty>(Property))
    {
        FN2CLogger::Get().Log(TEXT("  -> Identified as Bool type"), EN2CLogSeverity::Debug);
        return EN2CStructMemberType::Bool;
    }
    if (CastField<FByteProperty>(Property))
    {
        FN2CLogger::Get().Log(TEXT("  -> Identified as Byte type"), EN2CLogSeverity::Debug);
        return EN2CStructMemberType::Byte;
    }
    if (CastField<FIntProperty>(Property) || CastField<FInt64Property>(Property))
    {
        FN2CLogger::Get().Log(TEXT("  -> Identified as Int type"), EN2CLogSeverity::Debug);
        return EN2CStructMemberType::Int;
    }
    if (CastField<FFloatProperty>(Property) || CastField<FDoubleProperty>(Property))
    {
        FN2CLogger::Get().Log(TEXT("  -> Identified as Float type"), EN2CLogSeverity::Debug);
        return EN2CStructMemberType::Float;
    }
    if (CastField<FStrProperty>(Property))
    {
        FN2CLogger::Get().Log(TEXT("  -> Identified as String type"), EN2CLogSeverity::Debug);
        return EN2CStructMemberType::String;
    }
    if (CastField<FNameProperty>(Property))
    {
        FN2CLogger::Get().Log(TEXT("  -> Identified as Name type"), EN2CLogSeverity::Debug);
        return EN2CStructMemberType::Name;
    }
    if (CastField<FTextProperty>(Property))
    {
        FN2CLogger::Get().Log(TEXT("  -> Identified as Text type"), EN2CLogSeverity::Debug);
        return EN2CStructMemberType::Text;
    }
    
    if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
    {
        FString StructName = StructProp->Struct ? StructProp->Struct->GetName() : TEXT("Unknown");
        FString StructPath = StructProp->Struct ? StructProp->Struct->GetPathName() : TEXT("Unknown");
        
        FN2CLogger::Get().Log(FString::Printf(TEXT("  -> Identified as Struct type: %s (Path: %s)"), 
            *StructName, *StructPath), EN2CLogSeverity::Debug);
            
        if (StructProp->Struct->GetFName() == NAME_Vector)
        {
            FN2CLogger::Get().Log(TEXT("  -> Specialized as Vector type"), EN2CLogSeverity::Debug);
            return EN2CStructMemberType::Vector;
        }
        if (StructProp->Struct->GetFName() == NAME_Vector2D)
        {
            FN2CLogger::Get().Log(TEXT("  -> Specialized as Vector2D type"), EN2CLogSeverity::Debug);
            return EN2CStructMemberType::Vector2D;
        }
        if (StructProp->Struct->GetFName() == NAME_Rotator)
        {
            FN2CLogger::Get().Log(TEXT("  -> Specialized as Rotator type"), EN2CLogSeverity::Debug);
            return EN2CStructMemberType::Rotator;
        }
        if (StructProp->Struct->GetFName() == NAME_Transform)
        {
            FN2CLogger::Get().Log(TEXT("  -> Specialized as Transform type"), EN2CLogSeverity::Debug);
            return EN2CStructMemberType::Transform;
        }

        return EN2CStructMemberType::Struct;
    }

    if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
    {
        FString EnumName = EnumProp->GetEnum() ? EnumProp->GetEnum()->GetName() : TEXT("Unknown");
        FString EnumPath = EnumProp->GetEnum() ? EnumProp->GetEnum()->GetPathName() : TEXT("Unknown");
        
        FN2CLogger::Get().Log(FString::Printf(TEXT("  -> Identified as Enum type: %s (Path: %s)"), 
            *EnumName, *EnumPath), EN2CLogSeverity::Debug);
        return EN2CStructMemberType::Enum;
    }
    if (FClassProperty* ClassProp = CastField<FClassProperty>(Property))
    {
        FString ClassName = ClassProp->MetaClass ? ClassProp->MetaClass->GetName() : TEXT("Unknown");
        
        FN2CLogger::Get().Log(FString::Printf(TEXT("  -> Identified as Class type: %s"), *ClassName), 
            EN2CLogSeverity::Debug);
        return EN2CStructMemberType::Class;
    }
    if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Property))
    {
        FString ObjClassName = ObjProp->PropertyClass ? ObjProp->PropertyClass->GetName() : TEXT("Unknown");
        
        FN2CLogger::Get().Log(FString::Printf(TEXT("  -> Identified as Object type: %s"), *ObjClassName), 
            EN2CLogSeverity::Debug);
        return EN2CStructMemberType::Object;
    }

    FN2CLogger::Get().Log(FString::Printf(TEXT("  -> Unrecognized property type '%s', using Custom type"), 
        *Property->GetClass()->GetName()), EN2CLogSeverity::Debug);
    return EN2CStructMemberType::Custom; // For any other types
}

void FN2CCaptureContext::ProcessRelatedTypes(UK2Node* Node, FN2CNodeDefinition& OutNodeDef)
{
    if (!Node)
    {
        return;
    }
    
    // Check for struct nodes - use string comparison for class names
    const FString NodeClassName = Node->GetClass()->GetName();
    
    // Check for struct operation nodes
    if (NodeClassName.Contains(TEXT("K2Node_StructOperation")))
    {
        UK2Node_StructOperation* StructNode = (UK2Node_StructOperation*)Node;
        if (UScriptStruct* Struct = StructNode->StructType)
        {
            if (IsBlueprintStruct(Struct))
            {
                FN2CStruct StructDef = ProcessBlueprintStruct(Struct);
                if (StructDef.IsValid())
                {
                    N2CBlueprint.Structs.Add(StructDef);
                    
                    FN2CLogger::Get().Log(
                        FString::Printf(TEXT("Added Blueprint struct %s from struct operation node"), 
                        *StructDef.Name),
                        EN2CLogSeverity::Info);
                }
            }
        }
    }
    
    // Check make struct nodes
    if (NodeClassName.Contains(TEXT("K2Node_MakeStruct")))
    {
        UK2Node_MakeStruct* MakeStructNode = (UK2Node_MakeStruct*)Node;
        if (UScriptStruct* Struct = MakeStructNode->StructType)
        {
            if (IsBlueprintStruct(Struct))
            {
                FN2CStruct StructDef = ProcessBlueprintStruct(Struct);
                if (StructDef.IsValid())
                {
                    N2CBlueprint.Structs.Add(StructDef);
                    
                    FN2CLogger::Get().Log(
                        FString::Printf(TEXT("Added Blueprint struct %s from make struct node"), 
                        *StructDef.Name),
                        EN2CLogSeverity::Info);
                }
            }
        }
    }
    
    // Check break struct nodes
    if (NodeClassName.Contains(TEXT("K2Node_BreakStruct")))
    {
        UK2Node_BreakStruct* BreakStructNode = (UK2Node_BreakStruct*)Node;
        if (UScriptStruct* Struct = BreakStructNode->StructType)
        {
            if (IsBlueprintStruct(Struct))
            {
                FN2CStruct StructDef = ProcessBlueprintStruct(Struct);
                if (StructDef.IsValid())
                {
                    N2CBlueprint.Structs.Add(StructDef);
                    
                    FN2CLogger::Get().Log(
                        FString::Printf(TEXT("Added Blueprint struct %s from break struct node"), 
                        *StructDef.Name),
                        EN2CLogSeverity::Info);
                }
            }
        }
    }
    
    // Check struct references in pins
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (!Pin)
            continue;
            
        // Check for struct types
        if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
        {
            UScriptStruct* Struct = (UScriptStruct*)Pin->PinType.PinSubCategoryObject.Get();
            if (Struct && IsBlueprintStruct(Struct))
            {
                FN2CStruct StructDef = ProcessBlueprintStruct(Struct);
                if (StructDef.IsValid())
                {
                    N2CBlueprint.Structs.Add(StructDef);
                    
                    FN2CLogger::Get().Log(
                        FString::Printf(TEXT("Added Blueprint struct %s from pin type"), 
                        *StructDef.Name),
                        EN2CLogSeverity::Info);
                }
            }
        }
        
        // Check for enum types
        else if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Byte ||
                 Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Enum)
        {
            UEnum* Enum = (UEnum*)Pin->PinType.PinSubCategoryObject.Get();
            if (Enum && IsBlueprintEnum(Enum))
            {
                FN2CEnum EnumDef = ProcessBlueprintEnum(Enum);
                if (EnumDef.IsValid())
                {
                    N2CBlueprint.Enums.Add(EnumDef);
                    
                    FN2CLogger::Get().Log(
                        FString::Printf(TEXT("Added Blueprint enum %s from pin type"), 
                        *EnumDef.Name),
                        EN2CLogSeverity::Info);
                }
            }
        }
    }
}

void FN2CCaptureContext::LogNodeDetails(const FN2CNodeDefinition& NodeDef)
{
    // Log detailed node info
    FString NodeInfo = FString::Printf(TEXT("Node Details:\n")
        TEXT("  ID: %s\n")
        TEXT("  Name: %s\n")
        TEXT("  Type: %s\n")
        TEXT("  Member Parent: %s\n")
        TEXT("  Member Name: %s\n")
        TEXT("  Comment: %s\n")
        TEXT("  Pure: %s\n")
        TEXT("  Latent: %s\n")
        TEXT("  Input Pins: %d\n")
        TEXT("  Output Pins: %d"),
        *NodeDef.ID,
        *NodeDef.Title,
        *StaticEnum<EN2CNodeType>()->GetNameStringByValue(static_cast<int64>(NodeDef.NodeType)),
        *NodeDef.MemberParent,
        *NodeDef.MemberName,
        *NodeDef.Comment,
        NodeDef.bPure ? TEXT("true") : TEXT("false"),
        NodeDef.bLatent ? TEXT("true") : TEXT("false"),
        NodeDef.InputPins.Num(),
        NodeDef.OutputPins.Num());

    // Add detailed pin info
    NodeInfo += TEXT("\n  Input Pins:");
    for (const FN2CPinDefinition& Pin : NodeDef.InputPins)
    {
        NodeInfo += FString::Printf(TEXT("\n    - Pin %s (%s):\n")
            TEXT("      Type: %s\n")
            TEXT("      SubType: %s\n")
            TEXT("      Default: %s\n")
            TEXT("      Connected: %s\n")
            TEXT("      IsRef: %s\n")
            TEXT("      IsConst: %s\n")
            TEXT("      IsArray: %s\n")
            TEXT("      IsMap: %s\n")
            TEXT("      IsSet: %s"),
            *Pin.ID,
            *Pin.Name,
            *StaticEnum<EN2CPinType>()->GetNameStringByValue(static_cast<int64>(Pin.Type)),
            *Pin.SubType,
            *Pin.DefaultValue,
            Pin.bConnected ? TEXT("true") : TEXT("false"),
            Pin.bIsReference ? TEXT("true") : TEXT("false"),
            Pin.bIsConst ? TEXT("true") : TEXT("false"),
            Pin.bIsArray ? TEXT("true") : TEXT("false"),
            Pin.bIsMap ? TEXT("true") : TEXT("false"),
            Pin.bIsSet ? TEXT("true") : TEXT("false"));
    }

    NodeInfo += TEXT("\n  Output Pins:");
    for (const FN2CPinDefinition& Pin : NodeDef.OutputPins)
    {
        NodeInfo += FString::Printf(TEXT("\n    - Pin %s (%s):\n")
            TEXT("      Type: %s\n")
            TEXT("      SubType: %s\n")
            TEXT("      Default: %s\n")
            TEXT("      Connected: %s\n")
            TEXT("      IsRef: %s\n")
            TEXT("      IsConst: %s\n")
            TEXT("      IsArray: %s\n")
            TEXT("      IsMap: %s\n")
            TEXT("      IsSet: %s"),
            *Pin.ID,
            *Pin.Name,
            *StaticEnum<EN2CPinType>()->GetNameStringByValue(static_cast<int64>(Pin.Type)),
            *Pin.SubType,
            *Pin.DefaultValue,
            Pin.bConnected ? TEXT("true") : TEXT("false"),
            Pin.bIsReference ? TEXT("true") : TEXT("false"),
            Pin.bIsConst ? TEXT("true") : TEXT("false"),
            Pin.bIsArray ? TEXT("true") : TEXT("false"),
            Pin.bIsMap ? TEXT("true") : TEXT("false"),
            Pin.bIsSet ? TEXT("true") : TEXT("false"));
    }

    FN2CLogger::Get().Log(NodeInfo, EN2CLogSeverity::Debug);
}

FString FN2CCaptureContext::CleanPropertyName(const FString& RawName) const
{
    FString CleanName = RawName;

    // Look for pattern: _##_GUID where ## is 1-3 digits
    // This matches patterns like _8_FA2E526ECD4A7D7CEB62D89E0B51D8C8
    static const FRegexPattern PropertySuffixPattern(TEXT("_\\d{1,3}_[0-9A-F]{32}$"));
    FRegexMatcher Matcher(PropertySuffixPattern, CleanName);

    if (Matcher.FindNext())
    {
     // Get the position where the pattern starts
     int32 MatchStart = Matcher.GetMatchBeginning();

        // Remove the pattern
        if (MatchStart > 0)
        {
         CleanName = CleanName.Left(MatchStart);
         FN2CLogger::Get().Log(FString::Printf(TEXT("Cleaned property name from '%s' to '%s'"),
             *RawName, *CleanName), EN2CLogSeverity::Debug);
        }
    }

 return CleanName;
}

FN2CStructMember FN2CCaptureContext::ProcessStructMember(FProperty* Property)
{
    FN2CStructMember Member;

    if (!Property)
    {
        FN2CLogger::Get().LogWarning(TEXT("Null property provided to ProcessStructMember"));
        return Member;
    }

    // Set member name with cleaned version
    Member.Name = CleanPropertyName(Property->GetName());
    
    FString DebugInfo = FString::Printf(TEXT("ProcessStructMember: Processing property '%s'"), 
        *Member.Name);
    FN2CLogger::Get().Log(DebugInfo, EN2CLogSeverity::Debug);

    // Get member comment if available
    if (Property->HasMetaData(TEXT("ToolTip")))
    {
        Member.Comment = Property->GetMetaData(TEXT("ToolTip"));
        FN2CLogger::Get().Log(FString::Printf(TEXT("  -> Found comment: %s"), *Member.Comment), EN2CLogSeverity::Debug);
    }

    // Determine member type
    Member.Type = ConvertPropertyToStructMemberType(Property);
    FN2CLogger::Get().Log(FString::Printf(TEXT("  -> Determined type: %s"), 
        *StaticEnum<EN2CStructMemberType>()->GetNameStringByValue(static_cast<int64>(Member.Type))), 
        EN2CLogSeverity::Debug);

    // Handle container types
    if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
    {
        FN2CLogger::Get().Log(TEXT("  -> Processing as Array property"), EN2CLogSeverity::Debug);
        Member.bIsArray = true;
        FProperty* InnerProp = ArrayProp->Inner;
        if (InnerProp)
        {
            FN2CLogger::Get().Log(FString::Printf(TEXT("  -> Array inner type: %s"), 
                *InnerProp->GetClass()->GetName()), EN2CLogSeverity::Debug);
                
            Member.Type = ConvertPropertyToStructMemberType(InnerProp);

            // Handle inner struct or enum types
            if (FStructProperty* InnerStructProp = CastField<FStructProperty>(InnerProp))
            {
                Member.TypeName = InnerStructProp->Struct->GetName();
                FN2CLogger::Get().Log(FString::Printf(TEXT("  -> Array of struct: %s"), 
                    *Member.TypeName), EN2CLogSeverity::Debug);

                // Process nested struct if it's Blueprint-defined
                if (IsBlueprintStruct(InnerStructProp->Struct))
                {
                    FN2CLogger::Get().Log(TEXT("  -> Processing blueprint-defined nested struct"), EN2CLogSeverity::Debug);
                    FN2CStruct NestedStruct = ProcessBlueprintStruct(InnerStructProp->Struct);
                    if (NestedStruct.IsValid())
                    {
                        N2CBlueprint.Structs.Add(NestedStruct);
                        FN2CLogger::Get().Log(TEXT("  -> Added nested struct to blueprint"), EN2CLogSeverity::Debug);
                    }
                    else
                    {
                        FN2CLogger::Get().LogWarning(TEXT("  -> Nested struct validation failed"));
                    }
                }
            }
            else if (FEnumProperty* InnerEnumProp = CastField<FEnumProperty>(InnerProp))
            {
                Member.TypeName = InnerEnumProp->GetEnum()->GetName();
                FN2CLogger::Get().Log(FString::Printf(TEXT("  -> Array of enum: %s"), 
                    *Member.TypeName), EN2CLogSeverity::Debug);

                // Process enum if it's Blueprint-defined
                if (IsBlueprintEnum(InnerEnumProp->GetEnum()))
                {
                    FN2CLogger::Get().Log(TEXT("  -> Processing blueprint-defined nested enum"), EN2CLogSeverity::Debug);
                    FN2CEnum NestedEnum = ProcessBlueprintEnum(InnerEnumProp->GetEnum());
                    if (NestedEnum.IsValid())
                    {
                        N2CBlueprint.Enums.Add(NestedEnum);
                        FN2CLogger::Get().Log(TEXT("  -> Added nested enum to blueprint"), EN2CLogSeverity::Debug);
                    }
                    else
                    {
                        FN2CLogger::Get().LogWarning(TEXT("  -> Nested enum validation failed"));
                    }
                }
            }
        }
        else
        {
            FN2CLogger::Get().LogWarning(TEXT("  -> Array property has null inner property"));
        }
    }
    else if (FSetProperty* SetProp = CastField<FSetProperty>(Property))
    {
        FN2CLogger::Get().Log(TEXT("  -> Processing as Set property"), EN2CLogSeverity::Debug);
        Member.bIsSet = true;
        // Similar logic for sets as for arrays
    }
    else if (FMapProperty* MapProp = CastField<FMapProperty>(Property))
    {
        FN2CLogger::Get().Log(TEXT("  -> Processing as Map property"), EN2CLogSeverity::Debug);
        Member.bIsMap = true;
        
        // Process key type
        FProperty* KeyProp = MapProp->KeyProp;
        if (KeyProp)
        {
            Member.KeyType = ConvertPropertyToStructMemberType(KeyProp);
            FN2CLogger::Get().Log(FString::Printf(TEXT("  -> Map key type: %s"), 
                *StaticEnum<EN2CStructMemberType>()->GetNameStringByValue(static_cast<int64>(Member.KeyType))), 
                EN2CLogSeverity::Debug);
                
            // Handle key type name for complex types
            if (FStructProperty* KeyStructProp = CastField<FStructProperty>(KeyProp))
            {
                Member.KeyTypeName = KeyStructProp->Struct->GetName();
                
                // Process nested struct if it's Blueprint-defined
                if (IsBlueprintStruct(KeyStructProp->Struct))
                {
                    FN2CLogger::Get().Log(TEXT("  -> Processing blueprint-defined key struct"), EN2CLogSeverity::Debug);
                    FN2CStruct NestedStruct = ProcessBlueprintStruct(KeyStructProp->Struct);
                    if (NestedStruct.IsValid())
                    {
                        N2CBlueprint.Structs.Add(NestedStruct);
                    }
                }
            }
            else if (FEnumProperty* KeyEnumProp = CastField<FEnumProperty>(KeyProp))
            {
                Member.KeyTypeName = KeyEnumProp->GetEnum()->GetName();
                
                // Process enum if it's Blueprint-defined
                if (IsBlueprintEnum(KeyEnumProp->GetEnum()))
                {
                    FN2CLogger::Get().Log(TEXT("  -> Processing blueprint-defined key enum"), EN2CLogSeverity::Debug);
                    FN2CEnum NestedEnum = ProcessBlueprintEnum(KeyEnumProp->GetEnum());
                    if (NestedEnum.IsValid())
                    {
                        N2CBlueprint.Enums.Add(NestedEnum);
                    }
                }
            }
        }
        
        // Process value type (similar to array inner type)
        FProperty* ValueProp = MapProp->ValueProp;
        if (ValueProp)
        {
            Member.Type = ConvertPropertyToStructMemberType(ValueProp);
            FN2CLogger::Get().Log(FString::Printf(TEXT("  -> Map value type: %s"), 
                *StaticEnum<EN2CStructMemberType>()->GetNameStringByValue(static_cast<int64>(Member.Type))), 
                EN2CLogSeverity::Debug);
                
            // Handle value type name for complex types
            if (FStructProperty* ValueStructProp = CastField<FStructProperty>(ValueProp))
            {
                Member.TypeName = ValueStructProp->Struct->GetName();
                
                // Process nested struct if it's Blueprint-defined
                if (IsBlueprintStruct(ValueStructProp->Struct))
                {
                    FN2CLogger::Get().Log(TEXT("  -> Processing blueprint-defined value struct"), EN2CLogSeverity::Debug);
                    FN2CStruct NestedStruct = ProcessBlueprintStruct(ValueStructProp->Struct);
                    if (NestedStruct.IsValid())
                    {
                        N2CBlueprint.Structs.Add(NestedStruct);
                    }
                }
            }
            else if (FEnumProperty* ValueEnumProp = CastField<FEnumProperty>(ValueProp))
            {
                Member.TypeName = ValueEnumProp->GetEnum()->GetName();
                
                // Process enum if it's Blueprint-defined
                if (IsBlueprintEnum(ValueEnumProp->GetEnum()))
                {
                    FN2CLogger::Get().Log(TEXT("  -> Processing blueprint-defined value enum"), EN2CLogSeverity::Debug);
                    FN2CEnum NestedEnum = ProcessBlueprintEnum(ValueEnumProp->GetEnum());
                    if (NestedEnum.IsValid())
                    {
                        N2CBlueprint.Enums.Add(NestedEnum);
                    }
                }
            }
        }
    }
    else
    {
        // Handle non-container types
        if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
        {
            Member.TypeName = StructProp->Struct->GetName();
            FN2CLogger::Get().Log(FString::Printf(TEXT("  -> Struct type: %s (Path: %s)"), 
                *Member.TypeName, *StructProp->Struct->GetPathName()), EN2CLogSeverity::Debug);

            // Process nested struct if it's Blueprint-defined
            if (IsBlueprintStruct(StructProp->Struct))
            {
                FN2CLogger::Get().Log(TEXT("  -> Processing blueprint-defined struct"), EN2CLogSeverity::Debug);
                FN2CStruct NestedStruct = ProcessBlueprintStruct(StructProp->Struct);
                if (NestedStruct.IsValid())
                {
                    N2CBlueprint.Structs.Add(NestedStruct);
                    FN2CLogger::Get().Log(TEXT("  -> Added struct to blueprint"), EN2CLogSeverity::Debug);
                }
                else
                {
                    FN2CLogger::Get().LogWarning(TEXT("  -> Struct validation failed"));
                }
            }
        }
        else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
        {
            Member.TypeName = EnumProp->GetEnum()->GetName();
            FN2CLogger::Get().Log(FString::Printf(TEXT("  -> Enum type: %s (Path: %s)"), 
                *Member.TypeName, *EnumProp->GetEnum()->GetPathName()), EN2CLogSeverity::Debug);

            // Process enum if it's Blueprint-defined
            if (IsBlueprintEnum(EnumProp->GetEnum()))
            {
                FN2CLogger::Get().Log(TEXT("  -> Processing blueprint-defined enum"));
                FN2CEnum NestedEnum = ProcessBlueprintEnum(EnumProp->GetEnum());
                if (NestedEnum.IsValid())
                {
                    N2CBlueprint.Enums.Add(NestedEnum);
                    FN2CLogger::Get().Log(TEXT("  -> Added enum to blueprint"), EN2CLogSeverity::Debug);
                }
                else
                {
                    FN2CLogger::Get().LogWarning(TEXT("  -> Enum validation failed"));
                }
            }
        }
    }
    
    FN2CLogger::Get().Log(FString::Printf(TEXT("ProcessStructMember: Completed processing of '%s'"), *Member.Name), 
        EN2CLogSeverity::Debug);
                                                                                                                                                                                                                                                                                                                      
    return Member;
}

FN2CStruct FN2CCaptureContext::ProcessBlueprintStruct(UScriptStruct* Struct)
{
    FN2CStruct Result;
    
    if (!Struct)
    {
        FN2CLogger::Get().LogError(TEXT("Null struct provided to ProcessBlueprintStruct"));
        return Result;
    }
    
    // Get struct path
    FString StructPath = Struct->GetPathName();
    FString StructName = Struct->GetName();
    
    FN2CLogger::Get().Log(
        FString::Printf(TEXT("ProcessBlueprintStruct: Processing struct '%s' (Path: %s)"), 
            *StructName, *StructPath),
        EN2CLogSeverity::Info);
    
    // Check if we've already processed this struct
    if (ProcessedStructPaths.Contains(StructPath))
    {
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Struct %s already processed - skipping"), *StructPath),
            EN2CLogSeverity::Debug);
        return Result;
    }
    
    // Mark as processed
    ProcessedStructPaths.Add(StructPath);
    FN2CLogger::Get().Log(TEXT("Added struct to processed paths"), EN2CLogSeverity::Debug);
    
    // Set basic struct info
    Result.Name = StructName;
    
    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Struct details: Name=%s"), 
            *Result.Name),
        EN2CLogSeverity::Debug);
    
    // Get struct comment if available
    FString StructComment = Struct->GetMetaData(TEXT("ToolTip"));
    
    if (!StructComment.IsEmpty())
    {
        Result.Comment = StructComment;
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Struct comment: %s"), *Result.Comment),
            EN2CLogSeverity::Debug);
    }
    
    // Log property iteration start
    FN2CLogger::Get().Log(TEXT("Beginning property iteration for struct members..."), EN2CLogSeverity::Debug);
    
    // Process struct members
    int32 PropertyCount = 0;
    for (TFieldIterator<FProperty> PropIt(Struct); PropIt; ++PropIt)
    {
        PropertyCount++;
        if (FProperty* Property = *PropIt)
        {
            FN2CLogger::Get().Log(
                FString::Printf(TEXT("Found property #%d: '%s' of class '%s'"), 
                    PropertyCount,
                    *Property->GetName(), 
                    *Property->GetClass()->GetName()),
                EN2CLogSeverity::Debug);
                
            FN2CStructMember Member = ProcessStructMember(Property);
            Result.Members.Add(Member);
            
            FN2CLogger::Get().Log(
                FString::Printf(TEXT("Added member '%s' of type '%s' to struct"), 
                    *Member.Name,
                    *StaticEnum<EN2CStructMemberType>()->GetNameStringByValue(static_cast<int64>(Member.Type))),
                EN2CLogSeverity::Debug);
        }
        else
        {
            FN2CLogger::Get().LogWarning(
                FString::Printf(TEXT("Null property found at index %d"), PropertyCount));
        }
    }
    
    if (PropertyCount == 0)
    {
        FN2CLogger::Get().LogWarning(
            FString::Printf(TEXT("No properties found in struct '%s' - TFieldIterator returned no results"), *StructName));
            
        // Try alternative property access if available
        UStruct* BaseStruct = Cast<UStruct>(Struct);
        if (BaseStruct && BaseStruct->Children)
        {
            FN2CLogger::Get().Log(TEXT("Attempting alternative property access via Children field..."), EN2CLogSeverity::Debug);
            
            for (FField* Field = BaseStruct->ChildProperties; Field; Field = Field->Next)
            {
                if (FProperty* Property = CastField<FProperty>(Field))
                {
                    FN2CLogger::Get().Log(
                        FString::Printf(TEXT("Found property via Children: '%s' of class '%s'"), 
                            *Property->GetName(), 
                            *Property->GetClass()->GetName()),
                        EN2CLogSeverity::Debug);
                        
                    FN2CStructMember Member = ProcessStructMember(Property);
                    Result.Members.Add(Member);
                }
            }
        }
    }
    
    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Processed struct %s with %d members (found %d properties during iteration)"), 
            *Result.Name, 
            Result.Members.Num(),
            PropertyCount),
        EN2CLogSeverity::Info);
    
    return Result;
}

void FN2CCaptureContext::DetermineNodeSpecificProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef)
{
    if (!Node)
    {
        return;
    }

    // Handle component add nodes
    if (UK2Node_AddComponent* AddCompNode = Cast<UK2Node_AddComponent>(Node))
    {
        if (UClass* ComponentClass = AddCompNode->TemplateType)
        {
            OutNodeDef.MemberParent = N2CStableNames::ClassName(ComponentClass);
        }
    }

    // Handle timeline nodes
    else if (UK2Node_Timeline* TimelineNode = Cast<UK2Node_Timeline>(Node))
    {
        OutNodeDef.MemberName = TimelineNode->TimelineName.ToString();
        if (UBlueprint* BP = TimelineNode->GetBlueprint())
        {
            OutNodeDef.MemberParent = BP->GetName();
        }
    }

    // Mark latent/async nodes
    if (Node->IsA<UK2Node_AsyncAction>() || 
        Node->IsA<UK2Node_BaseAsyncTask>() ||
        Node->IsA<UK2Node_Timeline>() ||
        (Node->IsA<UK2Node_CallFunction>() && Cast<UK2Node_CallFunction>(Node)->IsLatentFunction()))
    {
        OutNodeDef.bLatent = true;
    }
}
