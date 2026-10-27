// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CCaptureContext.h"

#include "Core/N2CLiveness.h"
#include "Core/N2CSettings.h"
#include "Models/N2CJsonContract.h"
#include "Utils/N2CStableNames.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "Engine/UserDefinedEnum.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "StructUtils/UserDefinedStruct.h"
#include "Utils/N2CLogger.h"
#include "Utils/N2CNodeTypeRegistry.h"
#include "K2Node_AddComponent.h"
#include "K2Node_AsyncAction.h"
#include "K2Node_BaseAsyncTask.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Composite.h"
#include "K2Node_CreateDelegate.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_Knot.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_MathExpression.h"
#include "K2Node_StructOperation.h"
#include "K2Node_Timeline.h"
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

    // A graph left out at the depth limit (and not captured another way): a warning, so an agent or user can raise
    // Max Translation Depth or capture that graph on its own (card 0240)
    for (const TWeakObjectPtr<const UEdGraph>& Weak : DepthLimitedGraphs)
    {
        const UEdGraph* Graph = Weak.Get();
        if (!Graph || VisitedGraphs.Contains(Graph))
        {
            continue;
        }
        const UBlueprint* Owner = FBlueprintEditorUtils::FindBlueprintForGraph(Graph);
        FN2CDiagnostic& Diagnostic = Capture.Diagnostics.AddDefaulted_GetRef();
        Diagnostic.Severity = EN2CDiagnosticSeverity::Warning;
        Diagnostic.Code = N2CJsonContract::Diagnostic::DepthLimit;
        Diagnostic.Path = TEXT("graphs");
        Diagnostic.Message = FString::Printf(TEXT("Graph %s%s%s is used but not captured: it is beyond Max Translation Depth %d"),
            Owner ? *Owner->GetName() : TEXT(""), Owner ? TEXT(".") : TEXT(""), *Graph->GetName(), Options.Depth);
    }
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
            N2C_LOG(Debug, TEXT("Skipping ghost node %s (an unwired default event)"),
                *Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
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

    // A capture of one graph or a node selection (a translation) leaves variables out unless asked (card 0242)
    bIncludeVariables = Options.IncludeVariables.Get(false);

    // Get Blueprint metadata from first node (all nodes are from the same graph)
    CaptureBlueprintMetadata(CollectedNodes[0]);

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
            N2C_LOG_CONTEXT(Debug, Context, TEXT("Graph info"));
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
    CaptureComments(MainGraph);
    if (bIncludeVariables)
    {
        CaptureLocalVariables(MainGraph);
    }
    CurrentGraphIndex = INDEX_NONE;

    ProcessQueuedGraphs();
    if (bIncludeVariables)
    {
        CaptureMemberVariables();
    }

    FString Context = FString::Printf(TEXT("Translated %d nodes in %d graphs"),
        MainGraphNodeCount,
        N2CBlueprint.Graphs.Num());
    FN2CLogger::Get().Log(TEXT("Node translation complete"), EN2CLogSeverity::Info, Context);

    return N2CBlueprint.Graphs.Num() > 0;
}

void FN2CCaptureContext::CaptureBlueprintMetadata(UK2Node* FirstNode)
{
    if (FirstNode)
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
}

void FN2CCaptureContext::ProcessQueuedGraphs()
{
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
}

bool FN2CCaptureContext::CaptureGraphs(TConstArrayView<UEdGraph*> Graphs)
{
    // Metadata from the first node any of the graphs holds, as CaptureNodes takes it from its first node
    UK2Node* FirstNode = nullptr;
    for (const UEdGraph* Graph : Graphs)
    {
        if (!Graph)
        {
            continue;
        }
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            FirstNode = Cast<UK2Node>(Node);
            if (FirstNode)
            {
                break;
            }
        }
        if (FirstNode)
        {
            break;
        }
    }
    if (!FirstNode)
    {
        FN2CLogger::Get().LogWarning(TEXT("No nodes to capture in any of the graphs"));
        return false;
    }
    CaptureBlueprintMetadata(FirstNode);

    // A whole-Blueprint capture records its variables unless told not to (card 0242)
    bIncludeVariables = Options.IncludeVariables.Get(true);

    // Every graph asked for counts as visited before any is processed, so a call from one to another is never
    // queued as a nested graph: each is captured once, at depth 0, in the order given
    for (UEdGraph* Graph : Graphs)
    {
        if (Graph)
        {
            VisitedGraphs.Add(Graph);
        }
    }
    CurrentDepth = 0;
    for (UEdGraph* Graph : Graphs)
    {
        if (Graph)
        {
            // A graph with no nodes (after ghost nodes are left out) is dropped by ProcessGraph
            ProcessGraph(Graph, DetermineGraphType(Graph));
        }
    }
    ProcessQueuedGraphs();
    if (bIncludeVariables)
    {
        CaptureMemberVariables();
    }

    FN2CLogger::Get().Log(TEXT("Graph capture complete"), EN2CLogSeverity::Info,
        FString::Printf(TEXT("Captured %d graphs (%d asked for)"), N2CBlueprint.Graphs.Num(), Graphs.Num()));
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

    int32 GraphDepth = CurrentDepth + 1;
    if (const UK2Node_Composite* Composite = Cast<UK2Node_Composite>(Graph->GetOuter()))
    {
        // A math expression keeps its formula in the node itself; its bound graph is never captured
        if (Composite->IsA<UK2Node_MathExpression>())
        {
            return;
        }

        // A collapsed graph is a purely visual grouping of the graph that holds it: part of that graph, so it is always
        // captured, at that graph's depth (calls from inside it count from there), never cut off by the depth limit
        // (imp-translation-core-5)
        GraphDepth = CurrentDepth;
    }
    else
    {
        // A function or macro graph is followed only when its Blueprint is the project's own content: /Game or a
        // project plugin, a content or GameFeature plugin included (translation-core-21). Never engine content.
        const UBlueprint* OwningBlueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph);
        if (!IsUserContent(OwningBlueprint))
        {
            N2C_LOG(Debug, TEXT("Skipping graph %s: not the project's own content (%s)"),
                *Graph->GetName(), OwningBlueprint ? *OwningBlueprint->GetPathName() : TEXT("no Blueprint"));
            return;
        }
        if (GraphDepth > Options.Depth)
        {
            FN2CLogger::Get().Log(FString::Printf(TEXT("Skipping graph '%s' - maximum translation depth reached (%d)"),
                *Graph->GetName(), Options.Depth), EN2CLogSeverity::Warning);
            DepthLimitedGraphs.AddUnique(Graph);
            return;
        }
    }

    // Queued once (by the graph itself)
    for (const FGraphProcessInfo& Info : AdditionalGraphsToProcess)
    {
        if (Info.Graph == Graph)
        {
            return;
        }
    }
    N2C_LOG(Debug, TEXT("Adding user-created graph to process: %s (Depth: %d)"),
        *Graph->GetName(), GraphDepth);
    AdditionalGraphsToProcess.Add(FGraphProcessInfo(Graph, GraphDepth));
}

bool FN2CCaptureContext::IsUserContent(const UObject* Object)
{
    const UPackage* Package = Object ? Object->GetPackage() : nullptr;
    if (!Package)
    {
        return false;
    }

    // The package's mount point: /Game/Folder/BP_X -> Game, /ShooterCore/BP_X -> ShooterCore
    const FString Root = FPackageName::SplitPackageNameRoot(Package->GetFName(), nullptr);
    if (Root == TEXT("Game"))
    {
        return true;
    }

    // A plugin mounts its content at its name; only a plugin of the project (its Plugins folder, GameFeatures
    // included) is the user's own. Engine, Script and Temp are no plugin's mount point.
    const TSharedPtr<IPlugin> Plugin = Root.IsEmpty() ? nullptr : IPluginManager::Get().FindPlugin(Root);
    return Plugin.IsValid() && Plugin->CanContainContent() && Plugin->GetLoadedFrom() == EPluginLoadedFrom::Project;
}

void FN2CCaptureContext::CaptureComments(const UEdGraph* Graph)
{
    if (!Graph || CurrentGraphIndex == INDEX_NONE)
    {
        return;
    }

    // The captured nodes of the graph, in capture order, with where they are
    struct FPlacedNode
    {
        FString ID;
        FIntPoint Position;
        int32 Number;
    };
    TArray<FPlacedNode> Placed;
    for (const UEdGraphNode* Node : Graph->Nodes)
    {
        if (const FString* NodeID = Node ? NodeIDs.Find(Node) : nullptr)
        {
            Placed.Add({ *NodeID, FIntPoint(Node->NodePosX, Node->NodePosY), FCString::Atoi(**NodeID + 1) });
        }
    }
    Placed.Sort([](const FPlacedNode& A, const FPlacedNode& B) { return A.Number < B.Number; });

    // Comment boxes carry design intent the collector (K2 nodes only) never sees (imp-translation-core-4). A node is
    // inside a box when its top-left corner is: node sizes are only known to an open editor, which uses the whole node.
    for (const UEdGraphNode* Node : Graph->Nodes)
    {
        const UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(Node);
        if (!CommentNode)
        {
            continue;
        }
        const FIntRect Box(CommentNode->NodePosX, CommentNode->NodePosY,
            CommentNode->NodePosX + CommentNode->NodeWidth, CommentNode->NodePosY + CommentNode->NodeHeight);
        FN2CCommentBox Comment;
        Comment.Text = CommentNode->NodeComment;
        for (const FPlacedNode& Inside : Placed)
        {
            if (Inside.Position.X >= Box.Min.X && Inside.Position.X < Box.Max.X
                && Inside.Position.Y >= Box.Min.Y && Inside.Position.Y < Box.Max.Y)
            {
                Comment.NodeIDs.Add(Inside.ID);
            }
        }
        if (!Comment.Text.IsEmpty() || Comment.NodeIDs.Num() > 0)
        {
            CurrentGraph().Comments.Add(MoveTemp(Comment));
        }
    }
}

FN2CVariable FN2CCaptureContext::DescribeVariable(const FBPVariableDescription& Variable, const FString& DefaultValue,
    const bool bMember) const
{
    // The type in the pin spellings, from the same helpers as pins (card 0238): a float is Real with sub_type "float",
    // a map is typed by its key and PinValueType is its value (PR #8 compared the category with "float" and swapped the
    // map's key and value)
    FN2CVariable Result;
    Result.Name = Variable.VarName.ToString();
    const FEdGraphPinType& Type = Variable.VarType;
    Result.Type = N2CStableNames::PinType(Type.PinCategory, Type.PinSubCategory, Type.PinSubCategoryObject.Get());
    Result.SubType = N2CStableNames::PinSubType(Type.PinSubCategory, Type.PinSubCategoryObject.Get());
    Result.bIsArray = Type.ContainerType == EPinContainerType::Array;
    Result.bIsSet = Type.ContainerType == EPinContainerType::Set;
    Result.bIsMap = Type.ContainerType == EPinContainerType::Map;
    if (Result.bIsMap)
    {
        const FEdGraphTerminalType& Value = Type.PinValueType;
        Result.ValueType = N2CStableNames::PinType(Value.TerminalCategory, Value.TerminalSubCategory,
            Value.TerminalSubCategoryObject.Get());
        Result.ValueSubType = N2CStableNames::PinSubType(Value.TerminalSubCategory, Value.TerminalSubCategoryObject.Get());
    }
    Result.DefaultValue = DefaultValue;
    if (!bMember)
    {
        return Result;
    }

    // Category, description and flags as the details panel and get-blueprint-member-variables read them (card 0283):
    // Instance Editable is CPF_Edit without CPF_DisableEditOnInstance, Private the BlueprintPrivate metadata
    const FString Category = N2CStableNames::SourceText(Variable.Category);
    if (!Category.IsEmpty() && !Category.Equals(N2CStableNames::SourceText(UEdGraphSchema_K2::VR_DefaultCategory)))
    {
        Result.Category = Category;
    }
    if (Variable.HasMetaData(FBlueprintMetadata::MD_Tooltip))
    {
        Result.Comment = Variable.GetMetaData(FBlueprintMetadata::MD_Tooltip);
    }
    const uint64 Flags = Variable.PropertyFlags;
    Result.bInstanceEditable = (Flags & CPF_Edit) != 0 && (Flags & CPF_DisableEditOnInstance) == 0;
    Result.bReadOnly = (Flags & CPF_BlueprintReadOnly) != 0;
    Result.bExposeOnSpawn = (Flags & CPF_ExposeOnSpawn) != 0;
    Result.bPrivate = Variable.HasMetaData(FBlueprintMetadata::MD_Private)
        && Variable.GetMetaData(FBlueprintMetadata::MD_Private).ToBool();
    Result.bReplicated = (Flags & CPF_Net) != 0;
    if ((Flags & CPF_RepNotify) != 0 && !Variable.RepNotifyFunc.IsNone())
    {
        Result.RepNotify = Variable.RepNotifyFunc.ToString();
    }
    return Result;
}

void FN2CCaptureContext::AddVariableRelatedTypes(const FEdGraphPinType& Type)
{
    if (!Options.bIncludeRelatedTypes)
    {
        return;
    }
    AddRelatedPinType(Type.PinCategory, Type.PinSubCategoryObject.Get());
    if (Type.IsMap())
    {
        AddRelatedPinType(Type.PinValueType.TerminalCategory, Type.PinValueType.TerminalSubCategoryObject.Get());
    }
}

void FN2CCaptureContext::CaptureMemberVariables()
{
    const UBlueprint* Blueprint = CapturedBlueprint.Get();
    if (!Blueprint)
    {
        return;
    }
    UClass* GeneratedClass = Blueprint->GeneratedClass;
    UObject* Defaults = GeneratedClass ? GeneratedClass->GetDefaultObject(/*bCreateIfNeeded*/ false) : nullptr;
    for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
    {
        // The default lives on the class defaults once the Blueprint is compiled (a full compile moves the
        // description's DefaultValue there and empties it); the description's value until the class has the variable
        FString DefaultValue = Variable.DefaultValue;
        const FProperty* Property = Defaults ? FindFProperty<FProperty>(GeneratedClass, Variable.VarName) : nullptr;
        FString FromDefaults;
        if (Property && FBlueprintEditorUtils::PropertyValueToString(Property, reinterpret_cast<const uint8*>(Defaults),
            FromDefaults, Defaults))
        {
            DefaultValue = FromDefaults;
        }
        N2CBlueprint.Variables.Add(DescribeVariable(Variable, DefaultValue, /*bMember*/ true));
        AddVariableRelatedTypes(Variable.VarType);
    }
}

void FN2CCaptureContext::CaptureLocalVariables(const UEdGraph* Graph)
{
    if (!Graph || CurrentGraphIndex == INDEX_NONE)
    {
        return;
    }
    for (const UEdGraphNode* Node : Graph->Nodes)
    {
        if (const UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node))
        {
            for (const FBPVariableDescription& Variable : Entry->LocalVariables)
            {
                CurrentGraph().LocalVariables.Add(DescribeVariable(Variable, Variable.DefaultValue, /*bMember*/ false));
                AddVariableRelatedTypes(Variable.VarType);
            }
            return;
        }
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
        CaptureComments(Graph);
        if (bIncludeVariables)
        {
            CaptureLocalVariables(Graph);
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

EN2CGraphType FN2CCaptureContext::DetermineGraphType(const UEdGraph* Graph)
{
    if (!Graph)
    {
        return EN2CGraphType::EventGraph;
    }

    // A collapsed graph first: the K2 schema would report the type of the graph that holds it
    if (Cast<UK2Node_Composite>(Graph->GetOuter()))
    {
        return EN2CGraphType::Composite;
    }

    // The schema knows the graph's kind (translation-core-20): the K2 schema from the Blueprint's graph lists, the
    // animation schemas for anim graphs and state machines. Never the graph's name or the nodes it happens to hold.
    const UEdGraphSchema* Schema = Graph->GetSchema();
    switch (Schema ? Schema->GetGraphType(Graph) : GT_Ubergraph)
    {
        case GT_Function:
            return Graph->GetFName() == UEdGraphSchema_K2::FN_UserConstructionScript
                ? EN2CGraphType::Construction : EN2CGraphType::Function;
        case GT_Macro:
            return EN2CGraphType::Macro;
        case GT_Animation:
        case GT_StateMachine:
            return EN2CGraphType::Animation;
        case GT_Ubergraph:
        default:
            return EN2CGraphType::EventGraph;
    }
}

void FN2CCaptureContext::DetermineNodeType(UK2Node* Node, EN2CNodeType& OutType)
{
    OutType = FN2CNodeTypeRegistry::Get().GetNodeType(Node);
}

EN2CPinType FN2CCaptureContext::DeterminePinType(const UEdGraphPin* Pin) const
{
    if (!Pin) return EN2CPinType::Wildcard;

    // The pin's own type (a map pin's key); subcategories before categories, a byte with a UEnum is Enum (card 0238)
    return N2CStableNames::PinType(Pin->PinType.PinCategory, Pin->PinType.PinSubCategory, Pin->PinType.PinSubCategoryObject.Get());
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

void FN2CCaptureContext::ProcessNodeTypeAndProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef)
{
    // Determine node type
    DetermineNodeType(Node, OutNodeDef.NodeType);

    // Every type has a processor: its own, else the base processor (card 0239), which also sets "latent" for every kind
    // of node, so the old fallback path that never ran is gone
    const TSharedPtr<IN2CNodeProcessor> Processor = FN2CNodeProcessorFactory::Get().GetProcessor(OutNodeDef.NodeType);
    if (!Processor.IsValid() || !Processor->Process(Node, OutNodeDef))
    {
        FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Node processor failed for node '%s' of type %s"),
            *Node->GetNodeTitle(ENodeTitleType::ListView).ToString(),
            *StaticEnum<EN2CNodeType>()->GetNameStringByValue(static_cast<int64>(OutNodeDef.NodeType))));
    }

    // A collapsed graph's node names the graph captured for it, as a macro instance names its macro (imp-translation-core-4)
    if (const UK2Node_Composite* CompositeNode = Cast<UK2Node_Composite>(Node))
    {
        if (CompositeNode->BoundGraph && !CompositeNode->IsA<UK2Node_MathExpression>())
        {
            OutNodeDef.MemberName = CompositeNode->BoundGraph->GetName();
        }
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
                        N2C_LOG(Debug,
                            TEXT("Added delegate function graph to process: %s"), *FuncGraph->GetName());
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
                            N2C_LOG(Debug,
                                TEXT("Added delegate signature graph to process: %s"), *FuncGraph->GetName());
                            break;
                        }
                    }
                }
            }
        }
    }
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
        N2C_LOG(Debug, TEXT("ProcessNodePins: Pin GUID %s has ID %s (Pin Name: %s, Node: %s)"),
            *Pin->PinId.ToString(), *PinID, *Pin->GetDisplayName().ToString(), 
            *Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
        
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
        else
        {
            PinDef.SubType = N2CStableNames::PinSubType(Pin->PinType.PinSubCategory, Pin->PinType.PinSubCategoryObject.Get());
        }

        // A map pin's own type is its key; its value type is PinValueType (card 0238, translation-core-13)
        if (PinDef.bIsMap)
        {
            const FEdGraphTerminalType& Value = Pin->PinType.PinValueType;
            PinDef.ValueType = N2CStableNames::PinType(Value.TerminalCategory, Value.TerminalSubCategory,
                Value.TerminalSubCategoryObject.Get());
            PinDef.ValueSubType = N2CStableNames::PinSubType(Value.TerminalSubCategory, Value.TerminalSubCategoryObject.Get());
        }
        
        // Add to appropriate pin array
        if (Pin->Direction == EGPD_Input)
        {
            OutNodeDef.InputPins.Add(PinDef);
            
            if (Pin->PinType.PinCategory == TEXT("exec"))
            {
                OutExecInputs.Add(Pin);
                N2C_LOG(Debug, TEXT("Found exec input pin: %s on node %s"),
                    *Pin->GetDisplayName().ToString(),
                    *Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
            }
        }
        else if (Pin->Direction == EGPD_Output)
        {
            OutNodeDef.OutputPins.Add(PinDef);
            
            // Track execution outputs
            if (Pin->PinType.PinCategory == TEXT("exec"))
            {
                OutExecOutputs.Add(Pin);
                N2C_LOG(Debug, TEXT("Found exec output pin: %s on node %s"),
                    *Pin->GetDisplayName().ToString(),
                    *Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
            }
        }
    }
}

void FN2CCaptureContext::ProcessNodeFlows(UK2Node* Node, const TArray<UEdGraphPin*>& ExecInputs, const TArray<UEdGraphPin*>& ExecOutputs)
{
    // Record execution flows: one entry per link, from the exec output pin to the exec input pin it runs
    // ("N3.P5->N4.P1"), through knots and every branch of their fan-out, so Branch True/False, Switch cases, Sequence
    // outputs and loop pins stay apart, as do Gate Enter/Open/Close and Timeline Play/Reverse (translation-core-2)
    N2C_LOG(Debug, TEXT("Node %s has %d exec outputs"),
        *Node->GetNodeTitle(ENodeTitleType::ListView).ToString(),
        ExecOutputs.Num());

    // A disabled node runs nothing: the links into it go on to where exec passes through it, if anywhere
    const FString SourceNodeID = NodeIDs.FindRef(Node);
    for (UEdGraphPin* ExecOutput : IsDisabledNode(Node) ? TArray<UEdGraphPin*>() : ExecOutputs)
    {
        const FString SourcePinID = ExecOutput ? PinIDs.FindRef(ExecOutput) : FString();
        if (SourceNodeID.IsEmpty() || SourcePinID.IsEmpty())
        {
            continue;
        }

        N2C_LOG(Debug, TEXT("Exec output pin %s has %d connections"),
            *ExecOutput->GetDisplayName().ToString(),
            ExecOutput->LinkedTo.Num());

        for (UEdGraphPin* TargetPin : TraceExecTargets(ExecOutput))
        {
            const UEdGraphNode* TargetNode = TargetPin->GetOwningNode();
            const FString TargetNodeID = NodeIDs.FindRef(TargetNode);
            const FString TargetPinID = PinIDs.FindRef(TargetPin);
            if (TargetNodeID.IsEmpty() || TargetPinID.IsEmpty())
            {
                // The target is outside the capture (or hidden): there is no ID to point at
                N2C_LOG(Debug, TEXT("Exec link from %s.%s to %s, outside the capture, is not recorded"),
                    *SourceNodeID, *SourcePinID, TargetNode ? *TargetNode->GetNodeTitle(ENodeTitleType::ListView).ToString() : TEXT("(null)"));
                continue;
            }

            CurrentGraph().Flows.Execution.AddUnique(N2CJsonContract::MakeExecEdge(SourceNodeID, SourcePinID, TargetNodeID, TargetPinID));

            N2C_LOG(Debug, TEXT("Added execution flow: %s.%s (%s.%s) -> %s.%s (%s.%s)"),
                *SourceNodeID, *SourcePinID, *Node->GetNodeTitle(ENodeTitleType::ListView).ToString(),
                *ExecOutput->GetDisplayName().ToString(), *TargetNodeID, *TargetPinID,
                *TargetNode->GetNodeTitle(ENodeTitleType::ListView).ToString(), *TargetPin->GetDisplayName().ToString());
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

            N2C_LOG(Debug, TEXT("Added data flow: %s (%s.%s) -> %s (%s.%s)"),
                *SourceRef, *Node->GetNodeTitle(ENodeTitleType::ListView).ToString(), *Pin->GetDisplayName().ToString(),
                *TargetRef, *TargetNode->GetNodeTitle(ENodeTitleType::ListView).ToString(),
                *TargetPin->GetDisplayName().ToString());
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
        N2C_LOG(Debug,
            TEXT("Enum %s already processed - skipping"), *EnumPath);
        return Result;
    }
    
    // Mark as processed
    ProcessedEnumPaths.Add(EnumPath);
    N2C_LOG(Debug, TEXT("Added enum to processed paths"));
    
    // Set basic enum info
    Result.Name = EnumName;
    
    N2C_LOG(Debug,
        TEXT("Enum details: Name=%s"),
            *Result.Name);
    
    // Get enum comment if available
    FString EnumComment = Enum->GetMetaData(TEXT("ToolTip"));
    
    if (!EnumComment.IsEmpty())
    {
        Result.Comment = EnumComment;
        N2C_LOG(Debug,
            TEXT("Enum comment: %s"), *Result.Comment);
    }
    
    // Every value but the "_MAX" entry the engine adds and values marked Hidden (card 0236, translation-core-11): a
    // display name that merely contains "max" ("Max", "MaxHealth") is a real value. Each with its number.
    const int32 NumEnums = Enum->NumEnums();
    const int32 Listed = Enum->ContainsExistingMax() ? NumEnums - 1 : NumEnums;
    for (int32 i = 0; i < Listed; ++i)
    {
        if (Enum->HasMetaData(TEXT("Hidden"), i))
        {
            N2C_LOG(Debug, TEXT("  -> Skipping hidden value #%d"), i);
            continue;
        }
        FN2CEnumValue Value;
        Value.Name = N2CStableNames::SourceText(Enum->GetDisplayNameTextByIndex(i));
        Value.Value = Enum->GetValueByIndex(i);
        Value.bHasValue = true;
        if (Enum->HasMetaData(TEXT("ToolTip"), i))
        {
            Value.Comment = Enum->GetMetaData(TEXT("ToolTip"), i);
        }
        Result.Values.Add(MoveTemp(Value));
    }

    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Processed enum %s with %d values"), 
            *Result.Name, 
            Result.Values.Num()),
        EN2CLogSeverity::Info);
    
    return Result;
}

bool FN2CCaptureContext::IsBlueprintStruct(const UScriptStruct* Struct)
{
    // By its class, not its path: a struct asset in a content or GameFeature plugin is as much the user's as one in
    // /Game, and a C++ struct is never one (translation-core-21)
    return Struct && Struct->IsA<UUserDefinedStruct>();
}

bool FN2CCaptureContext::IsBlueprintEnum(const UEnum* Enum)
{
    return Enum && Enum->IsA<UUserDefinedEnum>();
}

EN2CStructMemberType FN2CCaptureContext::ConvertPropertyToStructMemberType(FProperty* Property) const
{
    if (!Property)
    {
        FN2CLogger::Get().LogWarning(TEXT("ConvertPropertyToStructMemberType: Null property provided"));
        return EN2CStructMemberType::Int; // Default
    }
    
    FString PropertyName = Property->GetName();
    N2C_LOG(Debug, TEXT("ConvertPropertyToStructMemberType: Property '%s'"),
        *PropertyName);
    
    if (CastField<FBoolProperty>(Property))
    {
        N2C_LOG(Debug, TEXT("  -> Identified as Bool type"));
        return EN2CStructMemberType::Bool;
    }
    if (const FByteProperty* ByteProp = CastField<FByteProperty>(Property))
    {
        // A user enum is a namespaced enum, held in an FByteProperty with its Enum set (card 0236, translation-core-12)
        return ByteProp->Enum ? EN2CStructMemberType::Enum : EN2CStructMemberType::Byte;
    }
    if (CastField<FIntProperty>(Property) || CastField<FInt64Property>(Property))
    {
        N2C_LOG(Debug, TEXT("  -> Identified as Int type"));
        return EN2CStructMemberType::Int;
    }
    if (CastField<FFloatProperty>(Property) || CastField<FDoubleProperty>(Property))
    {
        N2C_LOG(Debug, TEXT("  -> Identified as Float type"));
        return EN2CStructMemberType::Float;
    }
    if (CastField<FStrProperty>(Property))
    {
        N2C_LOG(Debug, TEXT("  -> Identified as String type"));
        return EN2CStructMemberType::String;
    }
    if (CastField<FNameProperty>(Property))
    {
        N2C_LOG(Debug, TEXT("  -> Identified as Name type"));
        return EN2CStructMemberType::Name;
    }
    if (CastField<FTextProperty>(Property))
    {
        N2C_LOG(Debug, TEXT("  -> Identified as Text type"));
        return EN2CStructMemberType::Text;
    }
    
    if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
    {
        FString StructName = StructProp->Struct ? StructProp->Struct->GetName() : TEXT("Unknown");
        FString StructPath = StructProp->Struct ? StructProp->Struct->GetPathName() : TEXT("Unknown");
        
        N2C_LOG(Debug, TEXT("  -> Identified as Struct type: %s (Path: %s)"),
            *StructName, *StructPath);
            
        if (StructProp->Struct->GetFName() == NAME_Vector)
        {
            N2C_LOG(Debug, TEXT("  -> Specialized as Vector type"));
            return EN2CStructMemberType::Vector;
        }
        if (StructProp->Struct->GetFName() == NAME_Vector2D)
        {
            N2C_LOG(Debug, TEXT("  -> Specialized as Vector2D type"));
            return EN2CStructMemberType::Vector2D;
        }
        if (StructProp->Struct->GetFName() == NAME_Rotator)
        {
            N2C_LOG(Debug, TEXT("  -> Specialized as Rotator type"));
            return EN2CStructMemberType::Rotator;
        }
        if (StructProp->Struct->GetFName() == NAME_Transform)
        {
            N2C_LOG(Debug, TEXT("  -> Specialized as Transform type"));
            return EN2CStructMemberType::Transform;
        }

        return EN2CStructMemberType::Struct;
    }

    if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
    {
        FString EnumName = EnumProp->GetEnum() ? EnumProp->GetEnum()->GetName() : TEXT("Unknown");
        FString EnumPath = EnumProp->GetEnum() ? EnumProp->GetEnum()->GetPathName() : TEXT("Unknown");
        
        N2C_LOG(Debug, TEXT("  -> Identified as Enum type: %s (Path: %s)"),
            *EnumName, *EnumPath);
        return EN2CStructMemberType::Enum;
    }
    if (FClassProperty* ClassProp = CastField<FClassProperty>(Property))
    {
        FString ClassName = ClassProp->MetaClass ? ClassProp->MetaClass->GetName() : TEXT("Unknown");
        
        N2C_LOG(Debug, TEXT("  -> Identified as Class type: %s"), *ClassName);
        return EN2CStructMemberType::Class;
    }
    if (CastField<FSoftClassProperty>(Property))
    {
        return EN2CStructMemberType::Class;
    }
    if (CastField<FInterfaceProperty>(Property))
    {
        return EN2CStructMemberType::Object;
    }
    if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Property))
    {
        FString ObjClassName = ObjProp->PropertyClass ? ObjProp->PropertyClass->GetName() : TEXT("Unknown");
        
        N2C_LOG(Debug, TEXT("  -> Identified as Object type: %s"), *ObjClassName);
        return EN2CStructMemberType::Object;
    }

    N2C_LOG(Debug, TEXT("  -> Unrecognized property type '%s', using Custom type"),
        *Property->GetClass()->GetName());
    return EN2CStructMemberType::Custom; // For any other types
}

void FN2CCaptureContext::ProcessRelatedTypes(UK2Node* Node, FN2CNodeDefinition& OutNodeDef)
{
    if (!Node)
    {
        return;
    }

    // A struct node (Make, Break, Set Members, member get and set: every K2Node_StructOperation) names its struct
    if (const UK2Node_StructOperation* StructNode = Cast<UK2Node_StructOperation>(Node))
    {
        AddBlueprintStruct(StructNode->StructType);
    }

    // The user structs and enums the pins hold: each pin's own type (a map's key) and a map pin's value type
    // (PinValueType, card 0238, translation-core-13), so a struct or enum used only as a map value is listed too
    for (const UEdGraphPin* Pin : Node->Pins)
    {
        if (!Pin)
        {
            continue;
        }
        AddRelatedPinType(Pin->PinType.PinCategory, Pin->PinType.PinSubCategoryObject.Get());
        if (Pin->PinType.IsMap())
        {
            const FEdGraphTerminalType& Value = Pin->PinType.PinValueType;
            AddRelatedPinType(Value.TerminalCategory, Value.TerminalSubCategoryObject.Get());
        }
    }
}

void FN2CCaptureContext::LogNodeDetails(const FN2CNodeDefinition& NodeDef)
{
    // Several KB per node: built only when Debug is on (card 0193)
    if (!FN2CLogger::IsEnabled(EN2CLogSeverity::Debug))
    {
        return;
    }

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
         N2C_LOG(Debug, TEXT("Cleaned property name from '%s' to '%s'"),
             *RawName, *CleanName);
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

    Member.Name = CleanPropertyName(Property->GetName());
    if (Property->HasMetaData(TEXT("ToolTip")))
    {
        Member.Comment = Property->GetMetaData(TEXT("ToolTip"));
    }

    // A container's element (an array's or set's inner, a map's value) is the member's type, a map's key its key type;
    // every one gets its type name and its user struct or enum (card 0236: sets, keys and enum inners were skipped)
    if (const FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
    {
        Member.bIsArray = true;
        ResolveMemberType(ArrayProp->Inner, Member.Type, Member.TypeName);
    }
    else if (const FSetProperty* SetProp = CastField<FSetProperty>(Property))
    {
        Member.bIsSet = true;
        ResolveMemberType(SetProp->ElementProp, Member.Type, Member.TypeName);
    }
    else if (const FMapProperty* MapProp = CastField<FMapProperty>(Property))
    {
        Member.bIsMap = true;
        ResolveMemberType(MapProp->KeyProp, Member.KeyType, Member.KeyTypeName);
        ResolveMemberType(MapProp->ValueProp, Member.Type, Member.TypeName);
    }
    else
    {
        ResolveMemberType(Property, Member.Type, Member.TypeName);
    }

    N2C_LOG(Debug, TEXT("ProcessStructMember: %s is %s %s"), *Member.Name,
        *StaticEnum<EN2CStructMemberType>()->GetNameStringByValue(static_cast<int64>(Member.Type)), *Member.TypeName);
    return Member;
}

void FN2CCaptureContext::ResolveMemberType(const FProperty* Property, EN2CStructMemberType& OutType, FString& OutTypeName)
{
    OutType = ConvertPropertyToStructMemberType(const_cast<FProperty*>(Property));
    OutTypeName.Reset();
    if (!Property)
    {
        return;
    }

    // The type name of every type that needs one (card 0236, translation-core-8): a struct, an enum (an FEnumProperty or
    // a user enum's FByteProperty, translation-core-12), and the class of an object, class, soft, weak, lazy or
    // interface member; a user struct or enum is listed too
    if (const FStructProperty* StructProp = CastField<FStructProperty>(Property))
    {
        if (StructProp->Struct && OutType == EN2CStructMemberType::Struct)
        {
            OutTypeName = N2CStableNames::TypeObjectName(StructProp->Struct);
            AddBlueprintStruct(StructProp->Struct);
        }
    }
    else if (const UEnum* Enum = EnumOfProperty(Property))
    {
        OutTypeName = N2CStableNames::TypeObjectName(Enum);
        AddBlueprintEnum(const_cast<UEnum*>(Enum));
    }
    else if (const FClassProperty* ClassProp = CastField<FClassProperty>(Property))
    {
        OutTypeName = ClassProp->MetaClass ? N2CStableNames::TypeObjectName(ClassProp->MetaClass) : FString();
    }
    else if (const FSoftClassProperty* SoftClassProp = CastField<FSoftClassProperty>(Property))
    {
        OutTypeName = SoftClassProp->MetaClass ? N2CStableNames::TypeObjectName(SoftClassProp->MetaClass) : FString();
    }
    else if (const FObjectPropertyBase* ObjectProp = CastField<FObjectPropertyBase>(Property))
    {
        OutTypeName = ObjectProp->PropertyClass ? N2CStableNames::TypeObjectName(ObjectProp->PropertyClass) : FString();
    }
    else if (const FInterfaceProperty* InterfaceProp = CastField<FInterfaceProperty>(Property))
    {
        OutTypeName = InterfaceProp->InterfaceClass ? N2CStableNames::TypeObjectName(InterfaceProp->InterfaceClass) : FString();
    }
}

const UEnum* FN2CCaptureContext::EnumOfProperty(const FProperty* Property)
{
    if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
    {
        return EnumProp->GetEnum();
    }
    if (const FByteProperty* ByteProp = CastField<FByteProperty>(Property))
    {
        return ByteProp->Enum;
    }
    return nullptr;
}

void FN2CCaptureContext::AddBlueprintStruct(UScriptStruct* Struct)
{
    // Checked before processing (card 0236, translation-core-24): a struct met again is already listed, and processing
    // it again returned an empty struct that the validator logged as ERROR "Missing name"
    if (!Struct || !IsBlueprintStruct(Struct) || ProcessedStructPaths.Contains(Struct->GetPathName()))
    {
        return;
    }
    FN2CStruct StructDef = ProcessBlueprintStruct(Struct);
    if (StructDef.IsValid())
    {
        N2CBlueprint.Structs.Add(MoveTemp(StructDef));
    }
}

void FN2CCaptureContext::AddBlueprintEnum(UEnum* Enum)
{
    if (!Enum || !IsBlueprintEnum(Enum) || ProcessedEnumPaths.Contains(Enum->GetPathName()))
    {
        return;
    }
    FN2CEnum EnumDef = ProcessBlueprintEnum(Enum);
    if (EnumDef.IsValid())
    {
        N2CBlueprint.Enums.Add(MoveTemp(EnumDef));
    }
}

void FN2CCaptureContext::AddRelatedPinType(const FName& Category, UObject* TypeObject)
{
    if (Category == UEdGraphSchema_K2::PC_Struct)
    {
        AddBlueprintStruct(Cast<UScriptStruct>(TypeObject));
    }
    else if (Category == UEdGraphSchema_K2::PC_Byte || Category == UEdGraphSchema_K2::PC_Enum)
    {
        AddBlueprintEnum(Cast<UEnum>(TypeObject));
    }
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
        N2C_LOG(Debug,
            TEXT("Struct %s already processed - skipping"), *StructPath);
        return Result;
    }
    
    // Mark as processed
    ProcessedStructPaths.Add(StructPath);
    N2C_LOG(Debug, TEXT("Added struct to processed paths"));
    
    // Set basic struct info
    Result.Name = StructName;
    
    N2C_LOG(Debug,
        TEXT("Struct details: Name=%s"),
            *Result.Name);
    
    // Get struct comment if available
    FString StructComment = Struct->GetMetaData(TEXT("ToolTip"));
    
    if (!StructComment.IsEmpty())
    {
        Result.Comment = StructComment;
        N2C_LOG(Debug,
            TEXT("Struct comment: %s"), *Result.Comment);
    }
    
    // Log property iteration start
    N2C_LOG(Debug, TEXT("Beginning property iteration for struct members..."));
    
    // Process struct members
    int32 PropertyCount = 0;
    for (TFieldIterator<FProperty> PropIt(Struct); PropIt; ++PropIt)
    {
        PropertyCount++;
        if (FProperty* Property = *PropIt)
        {
            N2C_LOG(Debug,
                TEXT("Found property #%d: '%s' of class '%s'"),
                    PropertyCount,
                    *Property->GetName(), 
                    *Property->GetClass()->GetName());
                
            FN2CStructMember Member = ProcessStructMember(Property);
            Result.Members.Add(Member);
            
            N2C_LOG(Debug,
                TEXT("Added member '%s' of type '%s' to struct"),
                    *Member.Name,
                    *StaticEnum<EN2CStructMemberType>()->GetNameStringByValue(static_cast<int64>(Member.Type)));
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
            N2C_LOG(Debug, TEXT("Attempting alternative property access via Children field..."));
            
            for (FField* Field = BaseStruct->ChildProperties; Field; Field = Field->Next)
            {
                if (FProperty* Property = CastField<FProperty>(Field))
                {
                    N2C_LOG(Debug,
                        TEXT("Found property via Children: '%s' of class '%s'"),
                            *Property->GetName(), 
                            *Property->GetClass()->GetName());
                        
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

