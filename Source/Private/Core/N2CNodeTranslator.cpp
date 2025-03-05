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
    
    // Set basic enum info
    Result.Name = Enum->GetName();
    Result.Path = EnumPath;
    Result.bIsBlueprintEnum = IsBlueprintEnum(Enum);
    
    // Get enum comment if available
    FString EnumComment = Enum->GetMetaData(TEXT("ToolTip"));
    if (!EnumComment.IsEmpty())
    {
        Result.Comment = EnumComment;
    }
    
    // Process enum values
    int32 NumEnums = Enum->NumEnums();
    for (int32 i = 0; i < NumEnums; ++i)
    {
        FN2CEnumValue Value;
        Value.Name = Enum->GetNameStringByIndex(i);
        Value.Value = Enum->GetValueByIndex(i);
        
        // Get value comment if available
        FString ValueComment = Enum->GetMetaData(TEXT("ToolTip"));
        
        if (!ValueComment.IsEmpty())
        {
            Value.Comment = ValueComment;
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

EN2CStructMemberType FN2CNodeTranslator::ConvertPropertyToStructMemberType(UProperty* Property) const
{
    if (!Property)
    {
        return EN2CStructMemberType::Int; // Default
    }
    
    if (Cast<UBoolProperty>(Property))
        return EN2CStructMemberType::Bool;
    if (Cast<UByteProperty>(Property))
        return EN2CStructMemberType::Byte;
    if (Cast<UIntProperty>(Property) || Cast<UInt64Property>(Property))
        return EN2CStructMemberType::Int;
    if (Cast<UFloatProperty>(Property) || Cast<UDoubleProperty>(Property))
        return EN2CStructMemberType::Float;
    if (Cast<UStrProperty>(Property))
        return EN2CStructMemberType::String;
    if (Cast<UNameProperty>(Property))
        return EN2CStructMemberType::Name;
    if (Cast<UTextProperty>(Property))
        return EN2CStructMemberType::Text;
    
    if (UStructProperty* StructProp = Cast<UStructProperty>(Property))
    {
        if (StructProp->Struct->GetFName() == NAME_Vector)
            return EN2CStructMemberType::Vector;
        if (StructProp->Struct->GetFName() == NAME_Vector2D)
            return EN2CStructMemberType::Vector2D;
        if (StructProp->Struct->GetFName() == NAME_Rotator)
            return EN2CStructMemberType::Rotator;
        if (StructProp->Struct->GetFName() == NAME_Transform)
            return EN2CStructMemberType::Transform;

        return EN2CStructMemberType::Struct;
    }

    if (Cast<UEnumProperty>(Property))
        return EN2CStructMemberType::Enum;
    if (Cast<UClassProperty>(Property))
        return EN2CStructMemberType::Class;
    if (Cast<UObjectProperty>(Property))
        return EN2CStructMemberType::Object;

    return EN2CStructMemberType::Custom; // For any other types
}

void FN2CNodeTranslator::ProcessRelatedTypes(UK2Node* Node, FN2CNodeDefinition& OutNodeDef)
{
    if (!Node)
    {
        return;
    }
    
    // Check for struct nodes
    if (UK2Node_StructOperation* StructNode = Cast<UK2Node_StructOperation>(Node))
    {
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
    if (UK2Node_MakeStruct* MakeStructNode = Cast<UK2Node_MakeStruct>(Node))
    {
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
    if (UK2Node_BreakStruct* BreakStructNode = Cast<UK2Node_BreakStruct>(Node))
    {
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
            UScriptStruct* Struct = Cast<UScriptStruct>(Pin->PinType.PinSubCategoryObject.Get());
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
            UEnum* Enum = Cast<UEnum>(Pin->PinType.PinSubCategoryObject.Get());
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

FN2CStructMember FN2CNodeTranslator::ProcessStructMember(UProperty* Property)
{
     FN2CStructMember Member;

     if (!Property)
     {
         FN2CLogger::Get().LogWarning(TEXT("Null property provided to ProcessStructMember"));
         return Member;
     }

     // Set member name
     Member.Name = Property->GetName();

     // Get member comment if available
     FString MemberComment;
     if (Property->HasMetaData(TEXT("ToolTip")))
     {
         Member.Comment = Property->GetMetaData(TEXT("ToolTip"));
     }

     // Determine member type
     Member.Type = ConvertPropertyToStructMemberType(Property);

     // Handle container types
     UArrayProperty* ArrayProp = Cast<UArrayProperty>(Property);
     if (ArrayProp)
     {
         Member.bIsArray = true;
         UProperty* InnerProp = ArrayProp->Inner;
         if (InnerProp)
         {
             Member.Type = ConvertPropertyToStructMemberType(InnerProp);

             // Handle inner struct or enum types
             UStructProperty* InnerStructProp = Cast<UStructProperty>(InnerProp);
             if (InnerStructProp)
             {
                 Member.TypeName = InnerStructProp->Struct->GetName();

                 // Process nested struct if it's Blueprint-defined
                 if (IsBlueprintStruct(InnerStructProp->Struct))
                 {
                     FN2CStruct NestedStruct = ProcessBlueprintStruct(InnerStructProp->Struct);
                     if (NestedStruct.IsValid())
                     {
                         N2CBlueprint.Structs.Add(NestedStruct);
                     }
                 }
             }
             else
             {
                 UEnumProperty* InnerEnumProp = Cast<UEnumProperty>(InnerProp);
                 if (InnerEnumProp)
                 {
                     Member.TypeName = InnerEnumProp->Enum->GetName();

                     // Process enum if it's Blueprint-defined
                     if (IsBlueprintEnum(InnerEnumProp->Enum))
                     {
                         FN2CEnum NestedEnum = ProcessBlueprintEnum(InnerEnumProp->Enum);
                         if (NestedEnum.IsValid())
                         {
                             N2CBlueprint.Enums.Add(NestedEnum);
                         }
                     }
                 }
             }
         }
     }
     else if (USetProperty* SetProp = Cast<USetProperty>(Property))
     {
         Member.bIsSet = true;
         // Similar logic for sets as for arrays
     }
     else if (UMapProperty* MapProp = Cast<UMapProperty>(Property))
     {
         Member.bIsMap = true;
         // Handle key and value types for maps
     }
     else
     {
         // Handle non-container types
         UStructProperty* StructProp = Cast<UStructProperty>(Property);
         if (StructProp)
         {
             Member.TypeName = StructProp->Struct->GetName();

             // Process nested struct if it's Blueprint-defined
             if (IsBlueprintStruct(StructProp->Struct))
             {
                 FN2CStruct NestedStruct = ProcessBlueprintStruct(StructProp->Struct);
                 if (NestedStruct.IsValid())
                 {
                     N2CBlueprint.Structs.Add(NestedStruct);
                 }
             }
         }
         else
         {
             UEnumProperty* EnumProp = Cast<UEnumProperty>(Property);
             if (EnumProp)
             {
                 Member.TypeName = EnumProp->Enum->GetName();

                 // Process enum if it's Blueprint-defined
                 if (IsBlueprintEnum(EnumProp->Enum))
                 {
                     FN2CEnum NestedEnum = ProcessBlueprintEnum(EnumProp->Enum);
                     if (NestedEnum.IsValid())
                     {
                         N2CBlueprint.Enums.Add(NestedEnum);
                     }
                 }
             }
         }
     }
                                                                                                                                                                                                                                                                                                                      
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
    
    // Set basic struct info
    Result.Name = Struct->GetName();
    Result.Path = StructPath;
    Result.bIsBlueprintStruct = IsBlueprintStruct(Struct);
    
    // Get struct comment if available
    FString StructComment = Struct->GetMetaData(TEXT("ToolTip"));
    
    if (!StructComment.IsEmpty())
    {
        Result.Comment = StructComment;
    }
    
    // Process struct members
    for (TFieldIterator<UProperty> PropIt(Struct); PropIt; ++PropIt)
    {
        UProperty* Property = *PropIt;
        if (Property)
        {
            FN2CStructMember Member = ProcessStructMember(Property);
            Result.Members.Add(Member);
        }
    }
    
    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Processed struct %s with %d members"), 
            *Result.Name, 
            Result.Members.Num()),
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
