// Copyright ProtoSpatial. All Rights Reserved.

#include "Core/N2CNodeTranslator.h"

#include "Core/N2CSettings.h"
#include "Utils/N2CLogger.h"
#include "Utils/N2CNodeTypeHelper.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectBase.h"
#include "UObject/Class.h"

FN2CNodeTranslator& FN2CNodeTranslator::Get()
{
    static FN2CNodeTranslator Instance;
    return Instance;
}

bool FN2CNodeTranslator::GenerateN2CStruct(const TArray<UK2Node*>& CollectedNodes)
{
    // Clear any existing data
    N2CBlueprint = FN2CBlueprint();
    NodeIDMap.Empty();
    PinIDMap.Empty();

    if (CollectedNodes.Num() == 0)
    {
        FN2CLogger::Get().LogWarning(TEXT("No nodes provided to translate"));
        return false;
    }

    FN2CLogger::Get().Log(TEXT("Starting node translation"), EN2CLogSeverity::Info);

    // Get Blueprint metadata from first node (all nodes are from the same graph)                                                                                                                                                             
    if (UK2Node* FirstNode = CollectedNodes[0])                                                                                                                                                                                        
    {
        if (UBlueprint* Blueprint = FirstNode->GetBlueprint())
        {
            // Set Blueprint name
            N2CBlueprint.Metadata.Name = Blueprint->GetName();
            
            // Set Blueprint class
            if (UClass* BPClass = FirstNode->GetBlueprintClassFromNode())
            {
                N2CBlueprint.Metadata.BlueprintClass = GetCleanClassName(BPClass->GetName());
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
    
    // Create initial graph for the nodes
    FN2CGraph MainGraph;
    
    // Get graph info from first node
    if (CollectedNodes.Num() > 0 && CollectedNodes[0])
    {
        if (UEdGraph* Graph = CollectedNodes[0]->GetGraph())
        {
            MainGraph.Name = Graph->GetName();
            MainGraph.GraphType = DetermineGraphType(Graph);
            
            FString Context = FString::Printf(TEXT("Created graph: %s of type %s"),
                *MainGraph.Name,
                *StaticEnum<EN2CGraphType>()->GetNameStringByValue(static_cast<int64>(MainGraph.GraphType)));
            FN2CLogger::Get().Log(TEXT("Graph info"), EN2CLogSeverity::Debug, Context);
        }
    }
    
    CurrentGraph = &MainGraph;

    // Process each node
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
            CurrentGraph->Nodes.Add(NodeDef);
        }
    }

    // Add the main graph to the blueprint
    N2CBlueprint.Graphs.Add(MainGraph);

    // Process any additional graphs that were discovered
    while (AdditionalGraphsToProcess.Num() > 0)
    {
        FGraphProcessInfo GraphInfo = AdditionalGraphsToProcess.Pop();
        if (GraphInfo.Graph)
        {
            // Set the depth to parent depth before processing
            CurrentDepth = GraphInfo.ParentDepth;
            ProcessGraph(GraphInfo.Graph, DetermineGraphType(GraphInfo.Graph));
        }
    }

    FString Context = FString::Printf(TEXT("Translated %d nodes in %d graphs"), 
        MainGraph.Nodes.Num(), 
        N2CBlueprint.Graphs.Num());
    FN2CLogger::Get().Log(TEXT("Node translation complete"), EN2CLogSeverity::Info, Context);

    return N2CBlueprint.Graphs.Num() > 0;
}

FString FN2CNodeTranslator::GenerateNodeID()
{
    return FString::Printf(TEXT("N%d"), NodeIDMap.Num() + 1);
}

FString FN2CNodeTranslator::GeneratePinID(int32 PinCount)
{
    return FString::Printf(TEXT("P%d"), PinCount + 1);
}

bool FN2CNodeTranslator::InitializeNodeProcessing(UK2Node* Node, FN2CNodeDefinition& OutNodeDef)
{
    if (!Node)
    {
        FN2CLogger::Get().LogWarning(TEXT("Attempted to process null node"));
        return false;
    }

    // Skip knot nodes entirely - they're just pass-through connections
    if (Node->IsA<UK2Node_Knot>())
    {
        FN2CLogger::Get().Log(TEXT("Skipping knot node"), EN2CLogSeverity::Debug);
        return false;
    }

    // Check if we already have an ID for this node
    FString* ExistingID = NodeIDMap.Find(Node->NodeGuid);
    if (ExistingID)
    {
        OutNodeDef.ID = *ExistingID;
        FString Context = FString::Printf(TEXT("Reusing existing node ID %s for node %s"), 
            *OutNodeDef.ID,
            *Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
        FN2CLogger::Get().Log(Context, EN2CLogSeverity::Debug);
    }
    else
    {
        // Generate and map new node ID
        FString NodeID = GenerateNodeID();
        NodeIDMap.Add(Node->NodeGuid, NodeID);
        OutNodeDef.ID = NodeID;
        FString Context = FString::Printf(TEXT("Generated new node ID %s for node %s"), 
            *NodeID,
            *Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
        FN2CLogger::Get().Log(Context, EN2CLogSeverity::Debug);
    }

    return true;
}

bool FN2CNodeTranslator::ProcessNode(UK2Node* Node, FN2CNodeDefinition& OutNodeDef)
{
    if (!InitializeNodeProcessing(Node, OutNodeDef))
    {
        return false;
    }

    ProcessNodeTypeAndProperties(Node, OutNodeDef);

    // Track pin connections for flows
    TArray<UEdGraphPin*> ExecInputs;
    TArray<UEdGraphPin*> ExecOutputs;
    ProcessNodePins(Node, OutNodeDef, ExecInputs, ExecOutputs);
    ProcessNodeFlows(Node, ExecInputs, ExecOutputs);
    LogNodeDetails(OutNodeDef);

    return true;
}

void FN2CNodeTranslator::AddGraphToProcess(UEdGraph* Graph)
{
    if (!Graph)
    {
        return;
    }

    // Check if we've already processed this graph
    for (const FN2CGraph& ExistingGraph : N2CBlueprint.Graphs)
    {
        if (ExistingGraph.Name == Graph->GetName())
        {
            return;
        }
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
        const UN2CSettings* Settings = GetDefault<UN2CSettings>();
        int32 NextDepth = CurrentDepth + 1;
        if (Settings && NextDepth > Settings->TranslationDepth)
        {
            FString Context = FString::Printf(TEXT("Skipping graph '%s' - maximum translation depth reached (%d)"), 
                *Graph->GetName(), Settings->TranslationDepth);
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
            FString Context = FString::Printf(TEXT("Adding user-created graph to process: %s (Parent Depth: %d)"), 
                *Graph->GetName(), CurrentDepth);
            FN2CLogger::Get().Log(Context, EN2CLogSeverity::Debug);
            AdditionalGraphsToProcess.Add(FGraphProcessInfo(Graph, CurrentDepth));
        }
    }
    else
    {
        // Only log at Debug severity since this is expected behavior
        FString Context = FString::Printf(TEXT("Skipping engine graph: %s"), *Graph->GetName());
        FN2CLogger::Get().Log(Context, EN2CLogSeverity::Debug);
    }
}

bool FN2CNodeTranslator::ProcessGraph(UEdGraph* Graph, EN2CGraphType GraphType)
{
    if (!Graph)
    {
        FN2CLogger::Get().LogWarning(TEXT("Attempted to process null graph"));
        return false;
    }

    // Create new graph structure
    FN2CGraph NewGraph;
    NewGraph.Name = Graph->GetName();
    NewGraph.GraphType = GraphType;
    CurrentGraph = &NewGraph;

    // Collect and process nodes from this graph
    TArray<UK2Node*> GraphNodes;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (UK2Node* K2Node = Cast<UK2Node>(Node))
        {
            FN2CNodeDefinition NodeDef;
            if (ProcessNode(K2Node, NodeDef))
            {
                CurrentGraph->Nodes.Add(NodeDef);
            }
        }
    }

    // Add the processed graph to the blueprint if it has nodes
    if (CurrentGraph->Nodes.Num() > 0)
    {
        // Validate all flow references
        if (!ValidateFlowReferences(NewGraph))
        {
            FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Flow validation issues found in graph: %s"), *NewGraph.Name));
        }

        N2CBlueprint.Graphs.Add(NewGraph);
        
        FString Context = FString::Printf(TEXT("Processed graph: %s with %d nodes"), 
            *NewGraph.Name, 
            NewGraph.Nodes.Num());
        FN2CLogger::Get().Log(TEXT("Graph processing complete"), EN2CLogSeverity::Info, Context);
        
        return true;
    }

    return false;
}

EN2CGraphType FN2CNodeTranslator::DetermineGraphType(UEdGraph* Graph) const
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

void FN2CNodeTranslator::DetermineNodeType(UK2Node* Node, EN2CNodeType& OutType)
{
    FN2CNodeTypeHelper::DetermineNodeType(Node, OutType);
}

EN2CPinType FN2CNodeTranslator::DeterminePinType(const UEdGraphPin* Pin) const
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

bool FN2CNodeTranslator::ValidateFlowReferences(FN2CGraph& Graph)
{
    bool bIsValid = true;
    TSet<FString> NodeIds;
    TMap<FString, TSet<FString>> NodePinIds;

    // Build lookup maps for validation
    for (const FN2CNodeDefinition& Node : Graph.Nodes)
    {
        NodeIds.Add(Node.ID);
        
        // Track all pin IDs for this node
        TSet<FString>& PinIds = NodePinIds.Add(Node.ID);
        for (const FN2CPinDefinition& Pin : Node.InputPins)
        {
            PinIds.Add(Pin.ID);
        }
        for (const FN2CPinDefinition& Pin : Node.OutputPins)
        {
            PinIds.Add(Pin.ID);
        }
    }

    // Validate execution flows
    TArray<FString> InvalidExecutionFlows;
    for (const FString& ExecFlow : Graph.Flows.Execution)
    {
        TArray<FString> FlowNodes;
        ExecFlow.ParseIntoArray(FlowNodes, TEXT("->"));
        
        bool bFlowValid = true;
        for (const FString& NodeId : FlowNodes)
        {
            if (!NodeIds.Contains(NodeId))
            {
                FN2CLogger::Get().LogError(
                    FString::Printf(TEXT("Invalid node ID '%s' in execution flow: %s"), 
                    *NodeId, *ExecFlow));
                bFlowValid = false;
                bIsValid = false;
                break;
            }
        }
        
        if (!bFlowValid)
        {
            InvalidExecutionFlows.Add(ExecFlow);
        }
    }

    // Remove invalid execution flows
    for (const FString& InvalidFlow : InvalidExecutionFlows)
    {
        Graph.Flows.Execution.Remove(InvalidFlow);
        FN2CLogger::Get().LogWarning(
            FString::Printf(TEXT("Removed invalid execution flow: %s"), *InvalidFlow));
    }

    // Validate data flows
    TArray<FString> InvalidDataFlows;
    for (const auto& DataFlow : Graph.Flows.Data)
    {
        // Parse source pin reference (NodeID.PinID)
        TArray<FString> SourceParts;
        DataFlow.Key.ParseIntoArray(SourceParts, TEXT("."));
        
        // Parse target pin reference (NodeID.PinID)
        TArray<FString> TargetParts;
        DataFlow.Value.ParseIntoArray(TargetParts, TEXT("."));
        
        bool bFlowValid = true;
        
        // Validate source node and pin
        if (SourceParts.Num() != 2 || !NodeIds.Contains(SourceParts[0]) ||
            !NodePinIds.Contains(SourceParts[0]) || 
            !NodePinIds[SourceParts[0]].Contains(SourceParts[1]))
        {
            FN2CLogger::Get().LogError(
                FString::Printf(TEXT("Invalid source pin reference '%s' in data flow"), 
                *DataFlow.Key));
            bFlowValid = false;
            bIsValid = false;
        }
        
        // Validate target node and pin
        if (TargetParts.Num() != 2 || !NodeIds.Contains(TargetParts[0]) ||
            !NodePinIds.Contains(TargetParts[0]) || 
            !NodePinIds[TargetParts[0]].Contains(TargetParts[1]))
        {
            FN2CLogger::Get().LogError(
                FString::Printf(TEXT("Invalid target pin reference '%s' in data flow"), 
                *DataFlow.Value));
            bFlowValid = false;
            bIsValid = false;
        }
        
        if (!bFlowValid)
        {
            InvalidDataFlows.Add(DataFlow.Key);
        }
    }

    // Remove invalid data flows
    for (const FString& InvalidFlow : InvalidDataFlows)
    {
        Graph.Flows.Data.Remove(InvalidFlow);
        FN2CLogger::Get().LogWarning(
            FString::Printf(TEXT("Removed invalid data flow from: %s"), *InvalidFlow));
    }

    return bIsValid;
}

void FN2CNodeTranslator::ProcessNodeTypeAndProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef)
{
    // Determine node type
    DetermineNodeType(Node, OutNodeDef.NodeType);

    // Set node name
    OutNodeDef.Name = Node->GetNodeTitle(ENodeTitleType::MenuTitle).ToString();

    // Determine node specific data directly such as
    // MemberParent, MemberName, bLatent, 
    DetermineNodeSpecificProperties(Node, OutNodeDef);
    
    // Extract comments if present
    if (Node->NodeComment.Len() > 0) { OutNodeDef.Comment = Node->NodeComment; }
    
    // Set purity from node
    OutNodeDef.bPure = Node->IsNodePure();

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
}

void FN2CNodeTranslator::ProcessNodePins(UK2Node* Node, FN2CNodeDefinition& OutNodeDef, TArray<UEdGraphPin*>& OutExecInputs, TArray<UEdGraphPin*>& OutExecOutputs)
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
        
        // Generate and map pin ID (using local counter for this node)
        FString PinID = GeneratePinID(OutNodeDef.InputPins.Num() + OutNodeDef.OutputPins.Num());
        PinIDMap.Add(Pin->PinId, PinID);
        PinDef.ID = PinID;
        
        // Set pin name
        PinDef.Name = Pin->GetDisplayName().ToString();
        
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
            PinDef.DefaultValue = Pin->DefaultTextValue.ToString();
        }
        
        // Set subtype for container/object types
        if (UK2Node_CreateDelegate* CreateDelegateNode = Cast<UK2Node_CreateDelegate>(Node))
        {
            // For delegate output pin on CreateDelegate node, use function name as subtype
            if (Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == TEXT("delegate"))
            {
                PinDef.SubType = GetCleanClassName(CreateDelegateNode->GetFunctionName().ToString());
            }
            
            if (Pin->Direction == EGPD_Input && Pin->PinType.PinCategory == TEXT("object"))
            {
                PinDef.SubType = GetCleanClassName(CreateDelegateNode->GetScopeClass()->GetName());
            }
        }
        else if (Pin->PinType.PinSubCategoryObject.IsValid())
        {
            PinDef.SubType = GetCleanClassName(Pin->PinType.PinSubCategoryObject->GetName());
        }
        else if (!Pin->PinType.PinSubCategory.IsNone())
        {
            PinDef.SubType = GetCleanClassName(Pin->PinType.PinSubCategory.ToString());
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

void FN2CNodeTranslator::ProcessNodeFlows(UK2Node* Node, const TArray<UEdGraphPin*>& ExecInputs, const TArray<UEdGraphPin*>& ExecOutputs)
{
    // Record execution flows
    FString ExecDebug = FString::Printf(TEXT("Node %s has %d exec outputs"), 
        *Node->GetNodeTitle(ENodeTitleType::ListView).ToString(),
        ExecOutputs.Num());
    FN2CLogger::Get().Log(ExecDebug, EN2CLogSeverity::Debug);

    for (UEdGraphPin* ExecOutput : ExecOutputs)
    {
        if (!ExecOutput) continue;

        FString PinDebug = FString::Printf(TEXT("Exec output pin %s has %d connections"), 
            *ExecOutput->GetDisplayName().ToString(),
            ExecOutput->LinkedTo.Num());
        FN2CLogger::Get().Log(PinDebug, EN2CLogSeverity::Debug);

        for (UEdGraphPin* LinkedPin : ExecOutput->LinkedTo)
        {
            if (!LinkedPin) 
            {
                FN2CLogger::Get().Log(TEXT("Found null linked pin"), EN2CLogSeverity::Warning);
                continue;
            }

            // Trace through any knot nodes to find the actual target
            UEdGraphPin* ActualTargetPin = TraceConnectionThroughKnots(LinkedPin);
            if (!ActualTargetPin)
            {
                FN2CLogger::Get().Log(TEXT("Could not find valid target through knot chain"), EN2CLogSeverity::Warning);
                continue;
            }

            UEdGraphNode* TargetNode = ActualTargetPin->GetOwningNode();
            if (!TargetNode)
            {
                FN2CLogger::Get().Log(TEXT("Found null target node"), EN2CLogSeverity::Warning);
                continue;
            }

            // Get or generate IDs for the nodes
            FString SourceNodeID = NodeIDMap.FindRef(Node->NodeGuid);
            FString TargetNodeID = NodeIDMap.FindRef(TargetNode->NodeGuid);
            
            if (SourceNodeID.IsEmpty())
            {
                SourceNodeID = GenerateNodeID();
                NodeIDMap.Add(Node->NodeGuid, SourceNodeID);
                FN2CLogger::Get().Log(TEXT("Generated new source node ID"), EN2CLogSeverity::Debug);
            }
            
            if (TargetNodeID.IsEmpty())
            {
                TargetNodeID = GenerateNodeID();
                NodeIDMap.Add(TargetNode->NodeGuid, TargetNodeID);
                FN2CLogger::Get().Log(TEXT("Generated new target node ID"), EN2CLogSeverity::Debug);
            }

            // Add execution flow
            FString FlowStr = FString::Printf(TEXT("%s->%s"), *SourceNodeID, *TargetNodeID);
            CurrentGraph->Flows.Execution.AddUnique(FlowStr);
            
            // Log execution flow
            FString FlowContext = FString::Printf(TEXT("Added execution flow: %s (%s) -> %s (%s)"),
                *SourceNodeID, *Node->GetNodeTitle(ENodeTitleType::ListView).ToString(),
                *TargetNodeID, *TargetNode->GetNodeTitle(ENodeTitleType::ListView).ToString());
            FN2CLogger::Get().Log(FlowContext, EN2CLogSeverity::Debug);
        }
    }

    // Record data flows
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin && Pin->PinType.PinCategory != TEXT("exec"))
        {
            for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
            {
                // Trace through any knot nodes to find the actual source and target
                UEdGraphPin* ActualSourcePin = Pin;
                UEdGraphPin* ActualTargetPin = TraceConnectionThroughKnots(LinkedPin);
                
                if (ActualSourcePin && ActualTargetPin && 
                    ActualSourcePin->GetOwningNode() && ActualTargetPin->GetOwningNode())
                {
                    // Get node and pin IDs
                    FString SourceNodeID = NodeIDMap.FindRef(ActualSourcePin->GetOwningNode()->NodeGuid);
                    FString SourcePinID = PinIDMap.FindRef(ActualSourcePin->PinId);
                    FString TargetNodeID = NodeIDMap.FindRef(ActualTargetPin->GetOwningNode()->NodeGuid);
                    FString TargetPinID = PinIDMap.FindRef(ActualTargetPin->PinId);
                    
                    if (!SourceNodeID.IsEmpty() && !SourcePinID.IsEmpty() && 
                        !TargetNodeID.IsEmpty() && !TargetPinID.IsEmpty())
                    {
                        // Add data flow (always store as output->input direction)
                        FString SourceRef = FString::Printf(TEXT("%s.%s"), *SourceNodeID, *SourcePinID);
                        FString TargetRef = FString::Printf(TEXT("%s.%s"), *TargetNodeID, *TargetPinID);
                
                        // Always store flow from output pin to input pin
                        if (ActualSourcePin->Direction == EGPD_Output)
                        {
                            CurrentGraph->Flows.Data.Add(SourceRef, TargetRef);
                    
                            // Log data flow
                            FString FlowContext = FString::Printf(TEXT("Added data flow: %s.%s (%s.%s) -> %s.%s (%s.%s)"),
                                *SourceNodeID, *SourcePinID, 
                                *ActualSourcePin->GetOwningNode()->GetNodeTitle(ENodeTitleType::ListView).ToString(), 
                                *ActualSourcePin->GetDisplayName().ToString(),
                                *TargetNodeID, *TargetPinID,
                                *ActualTargetPin->GetOwningNode()->GetNodeTitle(ENodeTitleType::ListView).ToString(),
                                *ActualTargetPin->GetDisplayName().ToString());
                            FN2CLogger::Get().Log(FlowContext, EN2CLogSeverity::Debug);
                        }
                        else
                        {
                            CurrentGraph->Flows.Data.Add(TargetRef, SourceRef);
                    
                            // Log data flow
                            FString FlowContext = FString::Printf(TEXT("Added data flow: %s.%s (%s.%s) -> %s.%s (%s.%s)"),
                                *TargetNodeID, *TargetPinID,
                                *ActualTargetPin->GetOwningNode()->GetNodeTitle(ENodeTitleType::ListView).ToString(),
                                *ActualTargetPin->GetDisplayName().ToString(),
                                *SourceNodeID, *SourcePinID,
                                *ActualSourcePin->GetOwningNode()->GetNodeTitle(ENodeTitleType::ListView).ToString(),
                                *ActualSourcePin->GetDisplayName().ToString());
                            FN2CLogger::Get().Log(FlowContext, EN2CLogSeverity::Debug);
                        }
                    }
                }
            }
        }
    }
}

FString FN2CNodeTranslator::GetCleanClassName(const FString& InName)
{
    FString CleanName = InName;                                                                                                                                                                      
                                                                                                                                                                                                           
     // Remove SKEL_ prefix if present                                                                                                                                                                     
     if (CleanName.StartsWith(TEXT("SKEL_")))                                                                                                                                                              
     {                                                                                                                                                                                                     
         CleanName = CleanName.RightChop(5); // Length of "SKEL_"                                                                                                                                          
     }                                                                                                                                                                                                     
                                                                                                                                                                                                           
     // Remove _C suffix if present                                                                                                                                                                        
     if (CleanName.EndsWith(TEXT("_C")))                                                                                                                                                                   
     {                                                                                                                                                                                                     
         CleanName = CleanName.LeftChop(2); // Length of "_C"                                                                                                                                              
     }                                                                                                                                                                                                     
                                                                                                                                                                                                           
     return CleanName;
}

void FN2CNodeTranslator::LogNodeDetails(const FN2CNodeDefinition& NodeDef)
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
        *NodeDef.Name,
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

UEdGraphPin* FN2CNodeTranslator::TraceConnectionThroughKnots(UEdGraphPin* StartPin) const
{
    if (!StartPin)
    {
        return nullptr;
    }

    UEdGraphPin* CurrentPin = StartPin;
    TSet<UEdGraphNode*> VisitedNodes;  // Prevent infinite loops

    while (CurrentPin)
    {
        UEdGraphNode* OwningNode = CurrentPin->GetOwningNode();
        if (!OwningNode)
        {
            return nullptr;
        }

        // If we've hit this node before, we have a loop
        if (VisitedNodes.Contains(OwningNode))
        {
            FN2CLogger::Get().LogWarning(TEXT("Detected loop in knot node chain"));
            return nullptr;
        }
        VisitedNodes.Add(OwningNode);

        // If this isn't a knot node, we've found our target
        if (!OwningNode->IsA<UK2Node_Knot>())
        {
            return CurrentPin;
        }

        // For knot nodes, follow to the next connection
        // Knots should only have one connection on the opposite side
        TArray<UEdGraphPin*>& LinkedPins = (CurrentPin->Direction == EGPD_Input) ? 
            OwningNode->Pins[1]->LinkedTo : OwningNode->Pins[0]->LinkedTo;

        if (LinkedPins.Num() == 0)
        {
            return nullptr;  // Dead end
        }

        CurrentPin = LinkedPins[0];
    }

    return nullptr;
}

void FN2CNodeTranslator::DetermineNodeSpecificProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef)
{
    if (!Node)
    {
        return;
    }

    // Handle function-based nodes
    if (UK2Node_CallFunction* FuncNode = Cast<UK2Node_CallFunction>(Node))
    {
        if (UFunction* Function = FuncNode->GetTargetFunction())
        {
            OutNodeDef.MemberParent = GetCleanClassName(Function->GetOwnerClass()->GetName());
            OutNodeDef.MemberName = GetCleanClassName(Function->GetName());
            OutNodeDef.bLatent = FuncNode->IsLatentFunction();
        }
    }

    // Handle event nodes
    else if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
    {
        OutNodeDef.MemberName = GetCleanClassName(EventNode->EventReference.GetMemberName().ToString());
        if (UClass* EventClass = EventNode->EventReference.GetMemberParentClass())
        {
            OutNodeDef.MemberParent = GetCleanClassName(EventClass->GetPathName());
        }
    }

    // Handle make struct nodes. Placing inside of variable check due to being a considered
    // a variable before being considered a MakeStruct node.
    else if (UK2Node_MakeStruct* MakeStructNode = Cast<UK2Node_MakeStruct>(Node))
    {
        if (UScriptStruct* Struct = MakeStructNode->StructType)
        {
            OutNodeDef.Name = FString::Printf(TEXT("Make %s"), *Struct->GetName());
            OutNodeDef.MemberName = GetCleanClassName(Struct->GetName());

            // Get the path name and extract just the base name before the period
            const FString FullPath = Struct->GetStructPathName().ToString();
            int32 DotIndex;                                                                                                                                                                                                                
            if (FullPath.FindChar('.', DotIndex))                                                                                                                                                                                          
            {                                                                                                                                                                                                                              
                OutNodeDef.MemberParent = GetCleanClassName(FullPath.Left(DotIndex));                                                                                                                                                                         
            }                                                                                                                                                                                                                              
            else                                                                                                                                                                                                                           
            {                                                                                                                                                                                                                              
                OutNodeDef.MemberParent = FullPath;                                                                                                                                                                                        
            }
            
         return;
            
        }
    }
    
    // Handle variable nodes
    else if (UK2Node_Variable* VarNode = Cast<UK2Node_Variable>(Node))
    {
        // Get variable reference info
        const FMemberReference& VarRef = VarNode->VariableReference;

        OutNodeDef.Name = VarNode->GetVarNameString();
        
        // Get member name safely
        FName MemberName = VarRef.GetMemberName();
        if (!MemberName.IsNone())
        {
            OutNodeDef.MemberName = GetCleanClassName(MemberName.ToString());
        }
        else if (UEdGraph* Graph = Node->GetGraph())
        {
            // Fallback to a generated name using the graph
            OutNodeDef.MemberName = FString::Printf(TEXT("Var_%s_%d"), 
                *Graph->GetName(),
                Node->GetUniqueID());
        }
        else
        {
            OutNodeDef.MemberName = FString::Printf(TEXT("UnknownVariable"));
        }
        
        // For variable nodes, we want the pin type info as the MemberParent
        // since it represents the variable's type
        for (UEdGraphPin* Pin : VarNode->Pins)
        {
            if (Pin && Pin->Direction == EGPD_Output && Pin->PinType.PinCategory != FName("exec"))
            {
                // Start with required PinCategory
                TArray<FString> TypeParts;
                TypeParts.Add(GetCleanClassName(Pin->PinType.PinCategory.ToString()));
                
                // Add optional parts if they exist and aren't "None"
                if (!Pin->PinType.PinSubCategory.IsNone() && !Pin->PinType.PinSubCategory.ToString().IsEmpty())
                {
                    TypeParts.Add(GetCleanClassName(Pin->PinType.PinSubCategory.ToString()));
                }
                
                if (Pin->PinType.PinSubCategoryObject.IsValid())
                {
                    TypeParts.Add(GetCleanClassName(Pin->PinType.PinSubCategoryObject->GetName()));
                }
                
                if (!Pin->PinType.PinSubCategoryMemberReference.MemberName.IsNone())
                {
                    TypeParts.Add(GetCleanClassName(Pin->PinType.PinSubCategoryMemberReference.MemberName.ToString()));
                }
                
                // Join with forward slashes
                OutNodeDef.MemberParent = FString::Join(TypeParts, TEXT("/"));
                break;
            }
        }
    }

    // Handle struct operation nodes
    else if (UK2Node_StructOperation* StructNode = Cast<UK2Node_StructOperation>(Node))
    {
        if (UScriptStruct* Struct = StructNode->StructType)
        {
            OutNodeDef.MemberParent = GetCleanClassName(Struct->GetName());
        }
    }

    // Handle actor bound events
    else if (UK2Node_ActorBoundEvent* ActorEventNode = Cast<UK2Node_ActorBoundEvent>(Node))
    {
        OutNodeDef.MemberName = ActorEventNode->DelegatePropertyName.ToString();
        if (UClass* DelegateClass = ActorEventNode->DelegateOwnerClass)
        {
            OutNodeDef.MemberParent = GetCleanClassName(DelegateClass->GetName());
        }
    }

    // Handle component bound events
    else if (UK2Node_ComponentBoundEvent* CompEventNode = Cast<UK2Node_ComponentBoundEvent>(Node))
    {
        OutNodeDef.MemberName = CompEventNode->DelegatePropertyName.ToString();
        if (UClass* DelegateClass = CompEventNode->DelegateOwnerClass)
        {
            OutNodeDef.MemberParent = GetCleanClassName(DelegateClass->GetName());
        }
    }

    // Handle custom events
    else if (UK2Node_CustomEvent* CustomEventNode = Cast<UK2Node_CustomEvent>(Node))
    {
        OutNodeDef.MemberName = CustomEventNode->CustomFunctionName.ToString();
        if (UBlueprint* BP = CustomEventNode->GetBlueprint())
        {
            OutNodeDef.MemberParent = GetCleanClassName(BP->GetName());
        }
    }

    // Handle function entry/result nodes
    else if (UK2Node_FunctionEntry* FuncEntryNode = Cast<UK2Node_FunctionEntry>(Node))
    {
        OutNodeDef.MemberName = FuncEntryNode->CustomGeneratedFunctionName.ToString();
        if (UBlueprint* BP = FuncEntryNode->GetBlueprint())
        {
            OutNodeDef.MemberParent = GetCleanClassName(BP->GetName());
        }
    }
    else if (UK2Node_FunctionResult* FuncResultNode = Cast<UK2Node_FunctionResult>(Node))
    {
        if (UBlueprint* BP = FuncResultNode->GetBlueprint())
        {
            OutNodeDef.MemberParent = GetCleanClassName(BP->GetName());
        }
    }

    // Handle delegate nodes
    else if (UK2Node_BaseMCDelegate* DelegateNode = Cast<UK2Node_BaseMCDelegate>(Node))
    {
        OutNodeDef.MemberName = DelegateNode->DelegateReference.GetMemberName().ToString();
        if (UClass* DelegateClass = DelegateNode->DelegateReference.GetMemberParentClass())
        {
            OutNodeDef.MemberParent = GetCleanClassName(DelegateClass->GetName());
        }
    }

    // Handle component add nodes
    else if (UK2Node_AddComponent* AddCompNode = Cast<UK2Node_AddComponent>(Node))
    {
        if (UClass* ComponentClass = AddCompNode->TemplateType)
        {
            OutNodeDef.MemberParent = GetCleanClassName(ComponentClass->GetName());
        }
    }

    // Handle timeline nodes
    else if (UK2Node_Timeline* TimelineNode = Cast<UK2Node_Timeline>(Node))
    {
        OutNodeDef.MemberName = TimelineNode->TimelineName.ToString();
        if (UBlueprint* BP = TimelineNode->GetBlueprint())
        {
            OutNodeDef.MemberParent = GetCleanClassName(BP->GetName());
        }
    }

    // Handle macro instance nodes
    else if (UK2Node_MacroInstance* MacroNode = Cast<UK2Node_MacroInstance>(Node))
    {
        if (UEdGraph* MacroGraph = MacroNode->GetMacroGraph())
        {
            OutNodeDef.MemberName = GetCleanClassName(MacroGraph->GetName());
            if (UBlueprint* BP = Cast<UBlueprint>(MacroGraph->GetOuter()))
            {
                OutNodeDef.MemberParent = GetCleanClassName(BP->GetName());
            }
        }
    }
    
    // Handle create delegate nodes
    else if (UK2Node_CreateDelegate* CreateDelegateNode = Cast<UK2Node_CreateDelegate>(Node))
    {
        OutNodeDef.MemberName = GetCleanClassName(CreateDelegateNode->GetFunctionName().ToString());
        
        // Try to get the scope class first since it's more reliable
        if (UClass* ScopeClass = CreateDelegateNode->GetScopeClass())
        {
            OutNodeDef.MemberParent = GetCleanClassName(ScopeClass->GetName());
            
            // Try to find and add the function graph
            if (UBlueprint* BP = Cast<UBlueprint>(ScopeClass->ClassGeneratedBy))
            {
                for (UEdGraph* FuncGraph : BP->FunctionGraphs)
                {
                    if (FuncGraph && FuncGraph->GetFName() == CreateDelegateNode->GetFunctionName())
                    {
                        AddGraphToProcess(FuncGraph);
                        break;
                    }
                }
            }
        }
        // Fallback to delegate signature if scope class isn't available
        else if (UFunction* DelegateSignature = CreateDelegateNode->GetDelegateSignature())
        {
            if (UClass* OwnerClass = DelegateSignature->GetOwnerClass())
            {
                OutNodeDef.MemberParent = GetCleanClassName(OwnerClass->GetName());
                
                if (UBlueprintGeneratedClass* BlueprintClass = Cast<UBlueprintGeneratedClass>(OwnerClass))
                {
                    if (UBlueprint* FunctionBlueprint = Cast<UBlueprint>(BlueprintClass->ClassGeneratedBy))
                    {
                        // Find and add the function graph
                        for (UEdGraph* FuncGraph : FunctionBlueprint->FunctionGraphs)
                        {
                            if (FuncGraph && FuncGraph->GetFName() == DelegateSignature->GetFName())
                            {
                                AddGraphToProcess(FuncGraph);
                                break;
                            }
                        }
                    }
                }
            }
        }
        
        if (OutNodeDef.MemberParent.IsEmpty())
        {
            FN2CLogger::Get().LogWarning(TEXT("Could not determine owner class for CreateDelegate node"));
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
