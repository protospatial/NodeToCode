// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CNodeTranslator.h"

#include "Core/N2CSettings.h"
#include "Utils/N2CLogger.h"
#include "Utils/N2CNodeTypeRegistry.h"
#include "Utils/Validators/N2CBlueprintValidator.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectBase.h"
#include "UObject/Class.h"
#include "UObject/UnrealTypePrivate.h"

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
    ProcessedStructPaths.Empty();  // Clear processed structs set
    ProcessedEnumPaths.Empty();    // Clear processed enums set

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
    OutType = FN2CNodeTypeRegistry::Get().GetNodeType(Node);
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

bool FN2CNodeTranslator::ValidateFlowReferences(FN2CGraph& Graph)
{
    // Use the blueprint validator to validate the graph
    FN2CBlueprintValidator Validator;
    FString ErrorMessage;
    return Validator.ValidateFlowReferences(Graph, ErrorMessage);
}

void FN2CNodeTranslator::ProcessNodeTypeAndProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef)
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
                *OutNodeDef.Name, *NodeTypeName), EN2CLogSeverity::Debug);
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

    // Process any struct or enum types used in this node
    ProcessRelatedTypes(Node, OutNodeDef);

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

void FN2CNodeTranslator::FallbackProcessNodeProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef)
{
    // Set node name
    OutNodeDef.Name = Node->GetNodeTitle(ENodeTitleType::MenuTitle).ToString();

    // Determine node specific data directly such as
    // MemberParent, MemberName, bLatent, 
    DetermineNodeSpecificProperties(Node, OutNodeDef);
    
    // Extract comments if present
    if (Node->NodeComment.Len() > 0) { OutNodeDef.Comment = Node->NodeComment; }
    
    // Set purity from node
    OutNodeDef.bPure = Node->IsNodePure();
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

FN2CEnum FN2CNodeTranslator::ProcessBlueprintEnum(UEnum* Enum)
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
        FString ValueName = Enum->GetDisplayNameTextByIndex(i).ToString();
        
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

bool FN2CNodeTranslator::IsBlueprintStruct(UScriptStruct* Struct) const
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

bool FN2CNodeTranslator::IsBlueprintEnum(UEnum* Enum) const
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

EN2CStructMemberType FN2CNodeTranslator::ConvertPropertyToStructMemberType(FProperty* Property) const
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

void FN2CNodeTranslator::ProcessRelatedTypes(UK2Node* Node, FN2CNodeDefinition& OutNodeDef)
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

FString FN2CNodeTranslator::CleanPropertyName(const FString& RawName) const
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

FN2CStructMember FN2CNodeTranslator::ProcessStructMember(FProperty* Property)
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

FN2CStruct FN2CNodeTranslator::ProcessBlueprintStruct(UScriptStruct* Struct)
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

void FN2CNodeTranslator::DetermineNodeSpecificProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef)
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

    // Mark latent/async nodes
    if (Node->IsA<UK2Node_AsyncAction>() || 
        Node->IsA<UK2Node_BaseAsyncTask>() ||
        Node->IsA<UK2Node_Timeline>() ||
        (Node->IsA<UK2Node_CallFunction>() && Cast<UK2Node_CallFunction>(Node)->IsLatentFunction()))
    {
        OutNodeDef.bLatent = true;
    }
}
