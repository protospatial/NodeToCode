// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpSearchBlueprintNodesTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "Core/N2CEditorIntegration.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "Utils/N2CLogger.h"
#include "BlueprintActionFilter.h"
#include "BlueprintActionMenuBuilder.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Core/N2CNodeTranslator.h"
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
        TEXT("Searches for Blueprint nodes/actions relevant to a given query. Can perform a context-sensitive search based on the current Blueprint or a global search. ")
        TEXT("For common math operations, you can simply search for symbols like '+', '-', '*', or '/'. ")
        TEXT("The promotable math nodes (e.g., Add, Subtract, Multiply, Divide) typically have wildcard pins that automatically promote to compatible types (e.g., float, integer, vector), so you usually don't need to search for type-specific versions like 'multiply float by float'."),
        TEXT("Blueprint Discovery")
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
    ContextSensitiveProp->SetStringField(TEXT("description"), TEXT("If true, performs a context-sensitive search using blueprintContext. If false, performs a global search ignoring blueprintContext."));
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
    ContextProp->SetStringField(TEXT("description"), TEXT("Information to make the search context-sensitive when contextSensitive is true"));
    
    TSharedPtr<FJsonObject> ContextProps = MakeShareable(new FJsonObject);
    
    TSharedPtr<FJsonObject> GraphPathProp = MakeShareable(new FJsonObject);
    GraphPathProp->SetStringField(TEXT("type"), TEXT("string"));
    GraphPathProp->SetStringField(TEXT("description"), TEXT("The asset path of the UEdGraph currently being viewed"));
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
    
    // Annotations
    // Annotations
    Definition.Annotations = MakeShareable(new FJsonObject);
    Definition.Annotations->SetBoolField(TEXT("readOnlyHint"), true);
    
    return Definition;
}

FMcpToolCallResult FN2CMcpSearchBlueprintNodesTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
    // The lambda captures 'this' and 'Arguments'.
    // We need to ensure all variables used inside the lambda that are defined outside are captured.
    // In this case, Arguments is captured. SearchTerm, bContextSensitive, MaxResults, BlueprintContext are parsed inside.
    
    return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult // Modified to capture Arguments directly
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
    
        // Get context if needed
        UBlueprint* ContextBlueprint = nullptr;
        UEdGraph* ContextGraph = nullptr;
    
        if (bContextSensitive && BlueprintContext.IsValid())
        {
            FString ContextError;
            if (!GetContextFromPaths(BlueprintContext, ContextBlueprint, ContextGraph, ContextError))
            {
                // Try to fall back to active editor
                FString FocusError;
                if (!FN2CMcpBlueprintUtils::GetFocusedEditorGraph(ContextBlueprint, ContextGraph, FocusError))
                {
                    return FMcpToolCallResult::CreateErrorResult(
                        FString::Printf(TEXT("Context-sensitive search requested but no valid context available: %s. Focus Error: %s"), *ContextError, *FocusError));
                }
            }
        }
        else if (bContextSensitive)
        {
            // Try to get context from active editor
            FString FocusError;
            if (!FN2CMcpBlueprintUtils::GetFocusedEditorGraph(ContextBlueprint, ContextGraph, FocusError))
            {
                return FMcpToolCallResult::CreateErrorResult(TEXT("Context-sensitive search requested but no Blueprint editor is active or graph focused. Error: ") + FocusError);
            }
        }
    
        // Set up the action filter
        FBlueprintActionFilter Filter;
        if (bContextSensitive && ContextBlueprint && ContextGraph)
        {
            Filter.Context.Blueprints.Add(ContextBlueprint);
            Filter.Context.Graphs.Add(ContextGraph);
        }
        // For global search, leave Context empty
    
        // Build the action list
        FBlueprintActionMenuBuilder MenuBuilder;
        MenuBuilder.AddMenuSection(Filter, FText::GetEmpty(), 0);
    
        // Rebuild to populate from database
        MenuBuilder.RebuildActionList();
    
        // Tokenize search terms
        TArray<FString> FilterTerms;
        SearchTerm.ParseIntoArray(FilterTerms, TEXT(" "), true);
    
        // Also create case-insensitive versions
        TArray<FString> LowerFilterTerms;
        for (const FString& Term : FilterTerms)
        {
            LowerFilterTerms.Add(Term.ToLower());
        }
    
        // Search through actions
        TArray<TSharedPtr<FJsonValue>> ResultNodes;
        int32 ResultCount = 0;

        for (int32 i = 0; i < MenuBuilder.GetNumActions() && ResultCount < MaxResults; ++i)
        {
            const TSharedPtr<FEdGraphSchemaAction>& Action = MenuBuilder.GetSchemaAction(i);
            if (!Action.IsValid())
            {
                continue;
            }
            const FString& ActionSearchText = Action->GetFullSearchText();
            FString LowerSearchText = ActionSearchText.ToLower();

            // Check if all search terms match (case-insensitive)
            bool bMatchesAllTerms = true;
            for (const FString& Term : LowerFilterTerms)
            {
                if (!LowerSearchText.Contains(Term))
                {
                    bMatchesAllTerms = false;
                    break;
                }
            }

            if (!bMatchesAllTerms)
            {
                continue;
            }

            // Apply category filter if specified
            if (!CategoryFilter.IsEmpty() && !MatchesCategoryFilter(ActionSearchText, CategoryFilter))
            {
                continue;
            }

            // Apply VMFunction exclusion filter
            if (bExcludeVMFunctions && IsExcludedVMFunction(ActionSearchText))
            {
                continue;
            }

            TSharedPtr<FJsonObject> NodeJson = ConvertActionToJson(Action, bContextSensitive, ContextBlueprint, ContextGraph);
            if (NodeJson.IsValid())
            {
                ResultNodes.Add(MakeShareable(new FJsonValueObject(NodeJson)));
                ResultCount++;
            }
        }
    
        // Build result JSON
        TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
        ResultObject->SetArrayField(TEXT("nodes"), ResultNodes);
    
        FString ResultJsonString; // Renamed to avoid conflict with outer scope 'Result'
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultJsonString);
        FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
    
        FN2CLogger::Get().Log(FString::Printf(TEXT("Blueprint node search completed. Found %d results"), ResultCount), EN2CLogSeverity::Info);

        // Schedule deferred refresh of BlueprintActionDatabase
        FN2CMcpBlueprintUtils::DeferredRefreshBlueprintActionDatabase();
        
        return FMcpToolCallResult::CreateTextResult(ResultJsonString); // Return the result from the lambda
    });
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

bool FN2CMcpSearchBlueprintNodesTool::MatchesCategoryFilter(const FString& ActionSearchText, const FString& CategoryFilter)
{
    // Categories in the search text are enclosed in pipes, e.g., |utilities|flowcontrol
    // We do a case-insensitive partial match on the category portion
    FString LowerSearchText = ActionSearchText.ToLower();
    FString LowerCategoryFilter = CategoryFilter.ToLower();

    // Look for the category pattern between pipes
    int32 PipeIndex = LowerSearchText.Find(TEXT("|"));
    if (PipeIndex == INDEX_NONE)
    {
        return false;
    }

    // Extract everything after the first pipe (the category section)
    FString CategorySection = LowerSearchText.Mid(PipeIndex);

    // Check if the filter matches anywhere in the category section
    return CategorySection.Contains(LowerCategoryFilter);
}

bool FN2CMcpSearchBlueprintNodesTool::IsExcludedVMFunction(const FString& ActionSearchText)
{
    FString LowerSearchText = ActionSearchText.ToLower();

    // VMFunction patterns to exclude - these are low-level type-specific math operations
    // that Blueprint developers rarely use directly (they use the promotable operators instead)
    static const TArray<FString> ExcludedPatterns = {
        // Type-specific math operations (use promotable operators like * instead)
        TEXT("_floatfloat"),      // e.g., Multiply_FloatFloat
        TEXT("_intint"),          // e.g., Add_IntInt
        TEXT("_int64int64"),      // e.g., Multiply_Int64Int64
        TEXT("_vectorfloat"),     // e.g., Multiply_VectorFloat
        TEXT("_vectorvector"),    // e.g., Add_VectorVector
        TEXT("_vectorint"),       // e.g., Multiply_VectorInt
        TEXT("_rotatorrotator"),  // e.g., Add_RotatorRotator
        TEXT("_transformtransform"), // Combined transforms
        TEXT("_linearcolorfloat"), // Color math
        TEXT("_linearcolorlinearcolor"),
        TEXT("_bytebyte"),        // Byte math
        TEXT("_matrix"),          // Matrix operations
        TEXT("_quatquat"),        // Quaternion operations
        TEXT("_timespantimespan"), // TimeSpan operations
        TEXT("_datetimetimespan"), // DateTime operations
    };

    for (const FString& Pattern : ExcludedPatterns)
    {
        if (LowerSearchText.Contains(Pattern))
        {
            return true;
        }
    }

    return false;
}

bool FN2CMcpSearchBlueprintNodesTool::GetContextFromPaths(
    const TSharedPtr<FJsonObject>& BlueprintContext,
    UBlueprint*& OutBlueprint,
    UEdGraph*& OutGraph,
    FString& OutError)
{
    if (!BlueprintContext.IsValid())
    {
        OutError = TEXT("Invalid blueprintContext object");
        return false;
    }
    
    FString BlueprintPath;
    if (BlueprintContext->TryGetStringField(TEXT("owningBlueprintPath"), BlueprintPath))
    {
        // Load the blueprint
        FString ResolveErrorNested;
        OutBlueprint = FN2CMcpBlueprintUtils::ResolveBlueprint(BlueprintPath, ResolveErrorNested);
        
        if (!OutBlueprint)
        {
            OutError = FString::Printf(TEXT("Failed to load Blueprint from path: %s. Error: %s"), *BlueprintPath, *ResolveErrorNested);
            return false;
        }
    }
    
    FString GraphPath;
    if (BlueprintContext->TryGetStringField(TEXT("graphPath"), GraphPath))
    {
        // Parse graph path (format: /Path/To/Blueprint.Blueprint:GraphName)
        FString PackagePath;
        FString GraphName;
        if (GraphPath.Split(TEXT(":"), &PackagePath, &GraphName))
        {
            // If we don't have the blueprint yet, try to load it from the graph path
            if (!OutBlueprint)
            {
                FString ResolveErrorNested;
                OutBlueprint = FN2CMcpBlueprintUtils::ResolveBlueprint(PackagePath, ResolveErrorNested);
            }
            
            if (OutBlueprint)
            {
                // Find the graph by name
                for (UEdGraph* Graph : OutBlueprint->UbergraphPages)
                {
                    if (Graph && Graph->GetFName().ToString() == GraphName)
                    {
                        OutGraph = Graph;
                        break;
                    }
                }
                
                // Also check function graphs
                if (!OutGraph)
                {
                    for (UEdGraph* Graph : OutBlueprint->FunctionGraphs)
                    {
                        if (Graph && Graph->GetFName().ToString() == GraphName)
                        {
                            OutGraph = Graph;
                            break;
                        }
                    }
                }
            }
        }
    }
    
    return OutBlueprint != nullptr;
}

TSharedPtr<FJsonObject> FN2CMcpSearchBlueprintNodesTool::ConvertActionToJson(
    const TSharedPtr<FEdGraphSchemaAction>& Action,
    bool bIsContextSensitive,
    UBlueprint* ContextBlueprint,
    UEdGraph* ContextGraph)
{
    // Generate a unique node ID for this search result
    static int32 SearchNodeCounter = 0;
    FString NodeId = FString::Printf(TEXT("SearchNode_%d"), ++SearchNodeCounter);
    
    TSharedPtr<FJsonObject> NodeJson;
    
    // Try to spawn the actual node if we have a context
    if (ContextGraph && Action.IsValid())
    {
        const TSharedPtr<FEdGraphSchemaAction>& SchemaAction = Action;
        if (SchemaAction.IsValid())
        {
            // Perform the action to spawn the node in the context graph
            const FVector2f SpawnLocation(0.0f, 0.0f);
            UEdGraphNode* SpawnedNode = SchemaAction->PerformAction(ContextGraph, nullptr, SpawnLocation);
            
            // Try to cast to K2Node
            UK2Node* K2Node = Cast<UK2Node>(SpawnedNode);
            if (K2Node)
            {
                // Convert the actual spawned node to N2CJSON
                NodeJson = ConvertNodeToN2CJson(K2Node, NodeId);
                
                // Remove the node from the graph immediately
                ContextGraph->RemoveNode(SpawnedNode);
            }
            else if (SpawnedNode)
            {
                // Clean up non-K2 nodes
                ContextGraph->RemoveNode(SpawnedNode);
            }
        }
    }
    
    // Fallback if we couldn't spawn the node
    if (!NodeJson.IsValid())
    {
        // Create a template node definition from the action metadata
        FN2CNodeDefinition NodeDef = CreateNodeDefinitionFromAction(Action, NodeId);
        
        // Convert to JSON using the standard N2C serializer
        NodeJson = FN2CSerializer::NodeToJsonObject(NodeDef);
    }
    
    // Add minimal spawning metadata for future node creation
    if (NodeJson.IsValid() && Action.IsValid())
    {
        TSharedPtr<FJsonObject> SpawnMetadata = MakeShareable(new FJsonObject);
        
        // Use the raw search text as the unique action identifier
        // This is the most precise way to identify a specific action
        FString ActionIdentifier = Action->GetFullSearchText();
        
        // Replace newlines with a delimiter that's easier for LLMs to handle
        // This makes the identifier a single line that's easier to copy/paste
        // Use > as delimiter since it's unique and won't be confused with existing content
        ActionIdentifier = ActionIdentifier.Replace(TEXT("\n"), TEXT(">"));
        
        SpawnMetadata->SetStringField(TEXT("actionIdentifier"), ActionIdentifier);
        
        // Store whether this action requires specific context
        SpawnMetadata->SetBoolField(TEXT("isContextSensitive"), bIsContextSensitive);
        
        NodeJson->SetObjectField(TEXT("spawnMetadata"), SpawnMetadata);
    }
    
    return NodeJson;
}

FString FN2CMcpSearchBlueprintNodesTool::ExtractInternalName(const TSharedPtr<FEdGraphSchemaAction>& Action)
{
    // For now, we'll return an empty string as accessing internal action structure is complex
    return FString();
}

TArray<FString> FN2CMcpSearchBlueprintNodesTool::ExtractCategoryPath(const TSharedPtr<FEdGraphSchemaAction>& Action)
{
    TArray<FString> CategoryPath;
    
    // Extract categories from the search text if possible
    FString SearchText = Action.IsValid() ? Action->GetFullSearchText() : FString();
    
    // Simple heuristic: split by common separators that might appear in search text
    // This is a simplified approach since we can't directly access the category structure
    if (!SearchText.IsEmpty())
    {
        // Look for category-like patterns in the search text
        // For example "Development > String > Print String"
        SearchText.ParseIntoArray(CategoryPath, TEXT(">"), true);
        
        // Clean up the entries
        for (FString& Category : CategoryPath)
        {
            Category = Category.TrimStartAndEnd();
        }
    }
    
    return CategoryPath;
}

TSharedPtr<FJsonObject> FN2CMcpSearchBlueprintNodesTool::ConvertNodeToN2CJson(UK2Node* Node, const FString& NodeId)
{
    if (!Node)
    {
        return nullptr;
    }
    
    // Create a node definition with the specified ID
    FN2CNodeDefinition NodeDef;
    NodeDef.ID = NodeId;
    
    // Use the NodeTranslator to properly process the node
    // This reuses all the existing node processing logic including:
    // - Node type determination
    // - Node processor selection
    // - Pin processing
    // - Property extraction
    if (!FN2CNodeTranslator::Get().ProcessSingleNode(Node, NodeDef))
    {
        FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Failed to process node %s for search results"), 
            *Node->GetNodeTitle(ENodeTitleType::ListView).ToString()));
        return nullptr;
    }
    
    // Convert to JSON using the serializer
    return FN2CSerializer::NodeToJsonObject(NodeDef);
}

FN2CNodeDefinition FN2CMcpSearchBlueprintNodesTool::CreateNodeDefinitionFromAction(const TSharedPtr<FEdGraphSchemaAction>& Action, const FString& NodeId)
{
    FN2CNodeDefinition NodeDef;
    NodeDef.ID = NodeId;
    
    // Get search text as the node name
    FString SearchText = Action.IsValid() ? Action->GetFullSearchText() : FString();
    NodeDef.Name = SearchText;
    
    // Try to determine node type from the action
    // This is a simplified approach since we don't have the actual node yet
    NodeDef.NodeType = EN2CNodeType::CallFunction; // Default to function call
    
    // Extract category information for potential node type hints
    TArray<FString> Categories = ExtractCategoryPath(Action);
    if (Categories.Num() > 0)
    {
        const FString& FirstCategory = Categories[0].ToLower();
        
        // Try to guess node type from category
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
    }
    
    // Set default flags
    NodeDef.bPure = false;
    NodeDef.bLatent = false;
    
    // Note: We can't populate pins without the actual node instance
    // This would require spawning the node which is more complex
    
    return NodeDef;
}
