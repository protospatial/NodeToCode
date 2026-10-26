// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpSearchBlueprintNodesTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "Core/N2CEditorIntegration.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpGraphTarget.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "MCP/Utils/N2CMcpNodeActions.h"
#include "Utils/N2CLogger.h"
#include "BlueprintActionMenuBuilder.h"
#include "BlueprintNodeSpawner.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Core/N2CCapture.h"
#include "Core/N2CSerializer.h"
#include "Utils/N2CNodeTypeRegistry.h"
#include "K2Node.h"
#include "EdGraphSchema_K2.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraph.h"

// Register the tool
REGISTER_MCP_TOOL(FN2CMcpSearchBlueprintNodesTool)

FN2CMcpSearchBlueprintNodesTool::FN2CMcpSearchBlueprintNodesTool()
{
}

FMcpToolDefinition FN2CMcpSearchBlueprintNodesTool::GetDefinition() const
{
    FMcpToolDefinition Definition(
        TEXT("search-blueprint-nodes"),
        TEXT("Searches for Blueprint nodes/actions relevant to a given query, in the action list of the target graph (blueprintPath, graphName, graphGuid, or the focused graph): the list the editor's node menu shows with Context Sensitive off. ")
        TEXT("For common math operations, you can simply search for symbols like '+', '-', '*', or '/'. ")
        TEXT("The promotable math nodes (e.g., Add, Subtract, Multiply, Divide) typically have wildcard pins that automatically promote to compatible types (e.g., float, integer, vector), so you usually don't need to search for type-specific versions like 'multiply float by float'. ")
        TEXT("Read-only: it never changes the graph. Each result's spawnMetadata.actionIdentifier is a stable ID (the node spawner's signature, the same in every search) to pass to add-bp-node-to-active-graph unchanged, with spawnMetadata.nodeName as nodeName."),
        N2CMcpToolsets::BlueprintDiscovery
    );
    
    // Build input schema
    TSharedPtr<FJsonObject> Schema = MakeShareable(new FJsonObject);
    Schema->SetStringField(TEXT("type"), TEXT("object"));
    
    TSharedPtr<FJsonObject> Properties = MakeShareable(new FJsonObject);
    
    // searchTerm property
    TSharedPtr<FJsonObject> SearchTermProp = MakeShareable(new FJsonObject);
    SearchTermProp->SetStringField(TEXT("type"), TEXT("string"));
    SearchTermProp->SetStringField(TEXT("description"), TEXT("The text query to search for"));
    Properties->SetObjectField(TEXT("searchTerm"), SearchTermProp);
    
    // contextSensitive property
    TSharedPtr<FJsonObject> ContextSensitiveProp = MakeShareable(new FJsonObject);
    ContextSensitiveProp->SetStringField(TEXT("type"), TEXT("boolean"));
    ContextSensitiveProp->SetStringField(TEXT("description"), TEXT("Kept for compatibility: true and false search the same list, the target graph's (as the editor's node menu with Context Sensitive off), and only set spawnMetadata.isContextSensitive. Both need a target graph."));
    ContextSensitiveProp->SetBoolField(TEXT("default"), true);
    Properties->SetObjectField(TEXT("contextSensitive"), ContextSensitiveProp);
    
    // maxResults property
    TSharedPtr<FJsonObject> MaxResultsProp = MakeShareable(new FJsonObject);
    MaxResultsProp->SetStringField(TEXT("type"), TEXT("integer"));
    MaxResultsProp->SetStringField(TEXT("description"), TEXT("Maximum number of results to return"));
    MaxResultsProp->SetNumberField(TEXT("default"), 20);
    MaxResultsProp->SetNumberField(TEXT("minimum"), 1);
    MaxResultsProp->SetNumberField(TEXT("maximum"), 100);
    Properties->SetObjectField(TEXT("maxResults"), MaxResultsProp);

    // category property
    TSharedPtr<FJsonObject> CategoryProp = MakeShareable(new FJsonObject);
    CategoryProp->SetStringField(TEXT("type"), TEXT("string"));
    CategoryProp->SetStringField(TEXT("description"),
        TEXT("Filter results by category. Common categories include: 'flowcontrol' (Branch, Sequence, etc.), ")
        TEXT("'operators' (math operators like +, -, *, /), 'struct' (Make/Break struct nodes), ")
        TEXT("'casting' (Cast nodes), 'math' (math functions). Case-insensitive partial match."));
    Properties->SetObjectField(TEXT("category"), CategoryProp);

    // excludeVMFunctions property
    TSharedPtr<FJsonObject> ExcludeVMProp = MakeShareable(new FJsonObject);
    ExcludeVMProp->SetStringField(TEXT("type"), TEXT("boolean"));
    ExcludeVMProp->SetStringField(TEXT("description"),
        TEXT("If true (default), excludes low-level VM math functions that are rarely used directly ")
        TEXT("(e.g., type-specific math like 'Multiply_FloatFloat'). Set to false to include all results."));
    ExcludeVMProp->SetBoolField(TEXT("default"), true);
    Properties->SetObjectField(TEXT("excludeVMFunctions"), ExcludeVMProp);

    // blueprintContext property
    TSharedPtr<FJsonObject> ContextProp = MakeShareable(new FJsonObject);
    ContextProp->SetStringField(TEXT("type"), TEXT("object"));
    ContextProp->SetStringField(TEXT("description"), TEXT("The older form of blueprintPath plus graphName, used only when none of blueprintPath, graphName and graphGuid is given. The graph to search for. Without it (and without blueprintPath, graphName, graphGuid) the focused graph is used. A Blueprint or graph that does not exist is an error (ASSET_NOT_FOUND, GRAPH_NOT_FOUND), never a search in another context"));
    
    TSharedPtr<FJsonObject> ContextProps = MakeShareable(new FJsonObject);
    
    TSharedPtr<FJsonObject> GraphPathProp = MakeShareable(new FJsonObject);
    GraphPathProp->SetStringField(TEXT("type"), TEXT("string"));
    GraphPathProp->SetStringField(TEXT("description"), TEXT("The graph to search for: '<Blueprint path>:<GraphName>' (e.g. '/Game/BP_Door.BP_Door:EventGraph'), or a graph name of owningBlueprintPath. Without it, the Blueprint's focused graph or else its event graph"));
    ContextProps->SetObjectField(TEXT("graphPath"), GraphPathProp);
    
    TSharedPtr<FJsonObject> BlueprintPathProp = MakeShareable(new FJsonObject);
    BlueprintPathProp->SetStringField(TEXT("type"), TEXT("string"));
    BlueprintPathProp->SetStringField(TEXT("description"), TEXT("The asset path of the UBlueprint asset itself"));
    ContextProps->SetObjectField(TEXT("owningBlueprintPath"), BlueprintPathProp);
    
    ContextProp->SetObjectField(TEXT("properties"), ContextProps);
    Properties->SetObjectField(TEXT("blueprintContext"), ContextProp);
    
    Schema->SetObjectField(TEXT("properties"), Properties);
    
    // Required fields
    TArray<TSharedPtr<FJsonValue>> RequiredArray;
    RequiredArray.Add(MakeShareable(new FJsonValueString(TEXT("searchTerm"))));
    Schema->SetArrayField(TEXT("required"), RequiredArray);
    
    Definition.InputSchema = Schema;
    
    // Annotations (as before card 0164; card 0205 sets the accurate hints)
    SetAnnotations(Definition, FMcpToolAnnotations::ReadOnly());
    
    // Results carry Blueprint text (tooltips of the project's own Blueprint functions and variables): marked as untrusted data (card 0124)
    Definition.bReturnsUntrustedContent = true;

    FN2CGraphTarget::AddSchemaProperties(Definition);

    return Definition;
}

FMcpToolCallResult FN2CMcpSearchBlueprintNodesTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
    return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult
    {
        // Parse arguments
        FString SearchTerm;
        bool bContextSensitive;
        int32 MaxResults;
        TSharedPtr<FJsonObject> BlueprintContext;
        FString CategoryFilter;
        bool bExcludeVMFunctions;
        FString ParseError;

        if (!ParseArguments(Arguments, SearchTerm, bContextSensitive, MaxResults, BlueprintContext, CategoryFilter, bExcludeVMFunctions, ParseError))
        {
            return FMcpToolCallResult::CreateErrorResult(ParseError);
        }

        FN2CLogger::Get().Log(FString::Printf(TEXT("Searching for Blueprint nodes: '%s' (ContextSensitive: %s, MaxResults: %d, Category: '%s', ExcludeVM: %s)"),
            *SearchTerm, bContextSensitive ? TEXT("true") : TEXT("false"), MaxResults,
            CategoryFilter.IsEmpty() ? TEXT("any") : *CategoryFilter, bExcludeVMFunctions ? TEXT("true") : TEXT("false")), EN2CLogSeverity::Info);

        // Context: the graph blueprintPath / graphName / graphGuid (or the older blueprintContext) names, else the
        // focused graph, through the one graph resolver (card 0152). A context that does not resolve is an error; it
        // never silently becomes another context or a global search (card 0277). contextSensitive false needs the
        // graph too: the engine's action filter rejects every action when its context has no Blueprint and no graph,
        // and the editor's menu keeps its Blueprint and graph with Context Sensitive off (card 0434)
        FN2CGraphTargetArgs TargetArgs = FN2CGraphTargetArgs::FromJson(Arguments);
        const bool bExplicitContext = BlueprintContext.IsValid()
            && (BlueprintContext->HasField(TEXT("owningBlueprintPath")) || BlueprintContext->HasField(TEXT("graphPath")));
        if (bExplicitContext && TargetArgs.IsEmpty())
        {
            FString ContextError;
            if (!ContextToTargetArgs(BlueprintContext, TargetArgs, ContextError))
            {
                return FMcpToolCallResult::CreateErrorResult(ContextError);
            }
        }
        const FN2CGraphTarget Target = FN2CGraphTarget::Resolve(TargetArgs, /*bForWrite*/ false);
        if (!Target.IsValid())
        {
            return FMcpToolCallResult::CreateErrorResult(TargetArgs.IsEmpty()
                ? TEXT("No graph to search for: no Blueprint editor is active or graph focused. Pass blueprintPath (with graphName or graphGuid for a graph other than the event graph); contextSensitive true and false both need it. Error: ") + Target.Error
                : Target.Error);
        }
        UBlueprint* ContextBlueprint = Target.Blueprint;
        UEdGraph* ContextGraph = Target.Graph;

        TArray<TSharedPtr<FJsonValue>> ResultNodes = SearchActions(SearchTerm, MaxResults, bContextSensitive,
            ContextBlueprint, ContextGraph, CategoryFilter, bExcludeVMFunctions);
        const int32 ResultCount = ResultNodes.Num();

        // Build result JSON
        TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
        ResultObject->SetArrayField(TEXT("nodes"), ResultNodes);

        FString ResultJsonString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultJsonString);
        FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);

        FN2CLogger::Get().Log(FString::Printf(TEXT("Blueprint node search completed. Found %d results"), ResultCount), EN2CLogSeverity::Info);

        // No BlueprintActionDatabase refresh: the search no longer adds or removes nodes, so there is nothing to resync.
        // The result says which graph it searched for ('target')
        return Target.Finish(FMcpToolCallResult::CreateTextResult(ResultJsonString));
    });
}

TArray<TSharedPtr<FJsonValue>> FN2CMcpSearchBlueprintNodesTool::SearchActions(
    const FString& SearchTerm,
    int32 MaxResults,
    bool bContextSensitive,
    UBlueprint* ContextBlueprint,
    UEdGraph* ContextGraph,
    const FString& CategoryFilter,
    bool bExcludeVMFunctions)
{
    check(IsInGameThread());

    if (!(ContextBlueprint && ContextGraph))
    {
        // Without a graph the engine's filter rejects every action (BlueprintActionFilter's IsSchemaIncompatible, with
        // no Blueprint and no graph in its context), so there are no results; Execute refuses that case first
        ContextBlueprint = nullptr;
        ContextGraph = nullptr;
    }

    // Build the action list from the database (the same list add-bp-node-to-active-graph resolves IDs in). It is the
    // list the editor's node menu shows with Context Sensitive off: SBlueprintActionMenu keeps the Blueprint and the
    // graph in the context, and FBlueprintActionMenuUtils::MakeContextMenu then adds no filter flags, target classes or
    // pins. bContextSensitive true and false build this same list and differ only in isContextSensitive (card 0434)
    FBlueprintActionMenuBuilder MenuBuilder;
    N2CNodeActions::BuildActionList(MenuBuilder, ContextBlueprint, ContextGraph);

    const TArray<FString> LowerFilterTerms = N2CNodeActions::TokenizeSearchTerm(SearchTerm);

    TArray<TSharedPtr<FJsonValue>> ResultNodes;
    for (int32 i = 0; i < MenuBuilder.GetNumActions() && ResultNodes.Num() < MaxResults; ++i)
    {
        const TSharedPtr<FEdGraphSchemaAction>& Action = MenuBuilder.GetSchemaAction(i);
        if (!Action.IsValid() || !N2CNodeActions::MatchesSearchTerm(*Action, LowerFilterTerms))
        {
            continue;
        }

        // Category filter on the action's own menu category
        if (!N2CNodeActions::MatchesCategory(*Action, CategoryFilter))
        {
            continue;
        }

        if (bExcludeVMFunctions && N2CNodeActions::IsExcludedVMFunction(*Action))
        {
            continue;
        }

        // Result IDs are numbered per search, so the same search gives the same IDs
        const FString NodeId = FString::Printf(TEXT("SearchNode_%d"), ResultNodes.Num() + 1);
        TSharedPtr<FJsonObject> NodeJson = ConvertActionToJson(Action, NodeId, bContextSensitive, ContextGraph);
        if (NodeJson.IsValid())
        {
            ResultNodes.Add(MakeShareable(new FJsonValueObject(NodeJson)));
        }
    }

    return ResultNodes;
}

bool FN2CMcpSearchBlueprintNodesTool::ParseArguments(
    const TSharedPtr<FJsonObject>& ArgumentsJsonObj,
    FString& OutSearchTerm,
    bool& OutContextSensitive,
    int32& OutMaxResults,
    TSharedPtr<FJsonObject>& OutBlueprintContext,
    FString& OutCategoryFilter,
    bool& bOutExcludeVMFunctions,
    FString& OutError)
{
    FN2CMcpArgumentParser ArgParser(ArgumentsJsonObj);

    if (!ArgParser.TryGetRequiredString(TEXT("searchTerm"), OutSearchTerm, OutError))
    {
        return false;
    }

    OutContextSensitive = ArgParser.GetOptionalBool(TEXT("contextSensitive"), true);
    OutMaxResults = ArgParser.GetOptionalInt(TEXT("maxResults"), 20);
    OutMaxResults = FMath::Clamp(OutMaxResults, 1, 100);
    OutBlueprintContext = ArgParser.GetOptionalObject(TEXT("blueprintContext"));
    OutCategoryFilter = ArgParser.GetOptionalString(TEXT("category"), TEXT(""));
    bOutExcludeVMFunctions = ArgParser.GetOptionalBool(TEXT("excludeVMFunctions"), true);

    return true;
}

bool FN2CMcpSearchBlueprintNodesTool::ContextToTargetArgs(
    const TSharedPtr<FJsonObject>& BlueprintContext,
    FN2CGraphTargetArgs& OutArgs,
    FString& OutError)
{
    OutArgs = FN2CGraphTargetArgs();
    if (!BlueprintContext.IsValid())
    {
        OutError = TEXT("Invalid blueprintContext object");
        return false;
    }

    FString BlueprintPath;
    BlueprintContext->TryGetStringField(TEXT("owningBlueprintPath"), BlueprintPath);
    BlueprintPath.TrimStartAndEndInline();

    FString GraphPath;
    BlueprintContext->TryGetStringField(TEXT("graphPath"), GraphPath);
    GraphPath.TrimStartAndEndInline();

    // graphPath is '<Blueprint path>:<GraphName>' or a bare graph name of owningBlueprintPath
    FString GraphPackagePath;
    FString GraphName = GraphPath;
    GraphPath.Split(TEXT(":"), &GraphPackagePath, &GraphName);

    OutArgs.BlueprintPath = !BlueprintPath.IsEmpty() ? BlueprintPath : GraphPackagePath;
    OutArgs.GraphName = GraphName.TrimStartAndEnd();
    if (OutArgs.BlueprintPath.IsEmpty())
    {
        OutError = FString::Printf(TEXT("GRAPH_NOT_FOUND: graphPath '%s' names no Blueprint; use '<Blueprint path>:<GraphName>' or pass owningBlueprintPath"), *GraphPath);
        return false;
    }
    return true;
}

bool FN2CMcpSearchBlueprintNodesTool::GetContextFromPaths(
    const TSharedPtr<FJsonObject>& BlueprintContext,
    UBlueprint*& OutBlueprint,
    UEdGraph*& OutGraph,
    FString& OutError)
{
    OutBlueprint = nullptr;
    OutGraph = nullptr;
    FN2CGraphTargetArgs Args;
    if (!ContextToTargetArgs(BlueprintContext, Args, OutError))
    {
        return false;
    }
    // The one graph resolver (card 0152): ASSET_NOT_FOUND, GRAPH_NOT_FOUND, AMBIGUOUS_GRAPH_NAME; without a graph name,
    // the Blueprint's focused graph or else its event graph
    const FN2CGraphTarget Target = FN2CGraphTarget::Resolve(Args, /*bForWrite*/ false);
    if (!Target.IsValid())
    {
        OutError = Target.Error;
        return false;
    }
    OutBlueprint = Target.Blueprint;
    OutGraph = Target.Graph;
    return true;
}

TSharedPtr<FJsonObject> FN2CMcpSearchBlueprintNodesTool::ConvertActionToJson(
    const TSharedPtr<FEdGraphSchemaAction>& Action,
    const FString& NodeId,
    bool bIsContextSensitive,
    UEdGraph* ContextGraph)
{
    if (!Action.IsValid())
    {
        return nullptr;
    }

    TSharedPtr<FJsonObject> NodeJson;

    // Read pins from the spawner's template node. Never PerformAction here: for an event that already exists
    // the event spawner returns the user's own node, and every spawn opens an "Add Node" transaction, dirties
    // the Blueprint and creates timeline/component templates. Template nodes live in a transient graph owned
    // by FBlueprintNodeTemplateCache, where spawners skip all of that.
    if (UK2Node* TemplateNode = Cast<UK2Node>(GetTemplateNodeForAction(Action, ContextGraph)))
    {
        NodeJson = ConvertNodeToN2CJson(TemplateNode, NodeId);
    }

    // Fallback if there is no template node
    if (!NodeJson.IsValid())
    {
        NodeJson = FN2CSerializer::NodeToJsonObject(CreateNodeDefinitionFromAction(Action, NodeId));
    }

    if (NodeJson.IsValid())
    {
        TSharedPtr<FJsonObject> SpawnMetadata = MakeShareable(new FJsonObject);

        // The spawner's signature: stable across searches, contexts, captures and compiles, and free of control
        // characters (card 0277). add-bp-node-to-active-graph resolves it in the action database
        SpawnMetadata->SetStringField(TEXT("actionIdentifier"), N2CNodeActions::GetActionId(Action));
        SpawnMetadata->SetStringField(TEXT("nodeName"), Action->GetMenuDescription().ToString());
        SpawnMetadata->SetStringField(TEXT("category"), Action->GetCategory().ToString());

        // The contextSensitive the search was called with
        SpawnMetadata->SetBoolField(TEXT("isContextSensitive"), bIsContextSensitive);

        NodeJson->SetObjectField(TEXT("spawnMetadata"), SpawnMetadata);
    }

    return NodeJson;
}

UEdGraphNode* FN2CMcpSearchBlueprintNodesTool::GetTemplateNodeForAction(const TSharedPtr<FEdGraphSchemaAction>& Action, UEdGraph* ContextGraph)
{
    if (!Action.IsValid())
    {
        return nullptr;
    }

    const UBlueprintNodeSpawner* Spawner = N2CNodeActions::GetSpawner(*Action);
    if (!Spawner)
    {
        return nullptr;
    }

    // ContextGraph only selects the kind of transient outer the template is built in (the action filter already
    // checked compatibility against it); the template is never added to ContextGraph
    UEdGraphNode* TemplateNode = Spawner->GetTemplateNode(ContextGraph);
    if (TemplateNode && ContextGraph && TemplateNode->GetGraph() == ContextGraph)
    {
        // Should be impossible; refuse rather than hand back a node from the user's graph
        return nullptr;
    }

    // Spawners do not allocate pins on template nodes; allocate them the way BlueprintActionFilter's pin matching does
    if (TemplateNode && TemplateNode->Pins.Num() == 0)
    {
        TemplateNode->AllocateDefaultPins();
    }
    return TemplateNode;
}

TSharedPtr<FJsonObject> FN2CMcpSearchBlueprintNodesTool::ConvertNodeToN2CJson(UK2Node* Node, const FString& NodeId)
{
    if (!Node)
    {
        return nullptr;
    }

    // Describe the node with the capture's node logic (type, processor, pins, properties) in a fresh context, with
    // the specified ID
    const TValueOrError<FN2CNodeDefinition, FString> NodeDef = N2C::DescribeNode(*Node, NodeId);
    if (NodeDef.HasError())
    {
        FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Failed to process node %s for search results"),
            *Node->GetNodeTitle(ENodeTitleType::ListView).ToString()));
        return nullptr;
    }

    return FN2CSerializer::NodeToJsonObject(NodeDef.GetValue());
}

FN2CNodeDefinition FN2CMcpSearchBlueprintNodesTool::CreateNodeDefinitionFromAction(const TSharedPtr<FEdGraphSchemaAction>& Action, const FString& NodeId)
{
    FN2CNodeDefinition NodeDef;
    NodeDef.ID = NodeId;
    NodeDef.NodeType = EN2CNodeType::CallFunction; // Default to function call
    NodeDef.bPure = false;
    NodeDef.bLatent = false;
    if (!Action.IsValid())
    {
        return NodeDef;
    }

    // The menu name as the title (a hint); no pins without a node instance
    NodeDef.Title = Action->GetMenuDescription().ToString();

    // Guess the node type from the top menu category
    TArray<FString> Categories;
    Action->GetCategory().ToString().ParseIntoArray(Categories, TEXT("|"), true);
    const FString FirstCategory = Categories.Num() > 0 ? Categories[0].ToLower() : FString();
    if (FirstCategory.Contains(TEXT("variable")))
    {
        NodeDef.NodeType = EN2CNodeType::VariableGet;
    }
    else if (FirstCategory.Contains(TEXT("event")))
    {
        NodeDef.NodeType = EN2CNodeType::Event;
    }
    else if (FirstCategory.Contains(TEXT("flow")))
    {
        NodeDef.NodeType = EN2CNodeType::Branch;
    }
    else if (FirstCategory.Contains(TEXT("struct")))
    {
        NodeDef.NodeType = EN2CNodeType::MakeStruct;
    }

    return NodeDef;
}
