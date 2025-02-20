// Copyright ProtoSpatial. All Rights Reserved.

#include "Core/N2CSerializer.h"
#include "Utils/N2CLogger.h"

// Initialize static members
bool FN2CSerializer::bPrettyPrint = true;
int32 FN2CSerializer::IndentLevel = 1;

FString FN2CSerializer::ToJson(const FN2CBlueprint& Blueprint)
{
    // Validate Blueprint before serialization
    if (!Blueprint.IsValid())
    {
        FN2CLogger::Get().LogWarning(TEXT("Blueprint validation failed - attempting partial serialization"));
    }

    // Create JSON object
    TSharedPtr<FJsonObject> JsonObject = BlueprintToJsonObject(Blueprint);
    if (!JsonObject.IsValid())
    {
        FN2CLogger::Get().LogError(TEXT("Failed to create JSON object from Blueprint"), TEXT("Serialization"));
        return TEXT("");
    }

    FString OutputString;

    if (bPrettyPrint)
    {
        TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
            TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&OutputString, IndentLevel);

        if (!FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer))
        {
            FN2CLogger::Get().LogError(TEXT("Failed to serialize JSON object to string"));
            return TEXT("");
        }
    }
    else
    {
        TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
            TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutputString);

        if (!FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer))
        {
            FN2CLogger::Get().LogError(TEXT("Failed to serialize JSON object to string"));
            return TEXT("");
        }
    }

    return OutputString;
}

bool FN2CSerializer::FromJson(const FString& JsonString, FN2CBlueprint& OutBlueprint)
{
    // Parse JSON string
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
    {
        FN2CLogger::Get().LogError(TEXT("Failed to parse JSON string"));
        return false;
    }

    // Convert to Blueprint
    return ParseBlueprintFromJson(JsonObject, OutBlueprint);
}

void FN2CSerializer::SetPrettyPrint(bool bEnabled)
{
    bPrettyPrint = bEnabled;
}

void FN2CSerializer::SetIndentLevel(int32 Level)
{
    IndentLevel = FMath::Max(0, Level);
}

TSharedPtr<FJsonObject> FN2CSerializer::BlueprintToJsonObject(const FN2CBlueprint& Blueprint)
{
    TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();

    // Add version
    JsonObject->SetStringField(TEXT("version"), Blueprint.Version.Value);

    // Add metadata
    TSharedPtr<FJsonObject> MetadataObject = MakeShared<FJsonObject>();
    MetadataObject->SetStringField(TEXT("name"), Blueprint.Metadata.Name);
    MetadataObject->SetStringField(TEXT("blueprint_type"), 
        StaticEnum<EN2CBlueprintType>()->GetNameStringByValue(static_cast<int64>(Blueprint.Metadata.BlueprintType)));
    MetadataObject->SetStringField(TEXT("blueprint_class"), Blueprint.Metadata.BlueprintClass);
    JsonObject->SetObjectField(TEXT("metadata"), MetadataObject);

    // Add graphs array
    TArray<TSharedPtr<FJsonValue>> GraphsArray;
    for (const FN2CGraph& Graph : Blueprint.Graphs)
    {
        TSharedPtr<FJsonObject> GraphObject = GraphToJsonObject(Graph);
        if (GraphObject.IsValid())
        {
            GraphsArray.Add(MakeShared<FJsonValueObject>(GraphObject));
        }
    }
    JsonObject->SetArrayField(TEXT("graphs"), GraphsArray);

    return JsonObject;
}

TSharedPtr<FJsonObject> FN2CSerializer::GraphToJsonObject(const FN2CGraph& Graph)
{
    TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();

    // Add basic properties
    JsonObject->SetStringField(TEXT("name"), Graph.Name);
    JsonObject->SetStringField(TEXT("graph_type"), 
        StaticEnum<EN2CGraphType>()->GetNameStringByValue(static_cast<int64>(Graph.GraphType)));

    // Add nodes array
    TArray<TSharedPtr<FJsonValue>> NodesArray;
    for (const FN2CNodeDefinition& Node : Graph.Nodes)
    {
        TSharedPtr<FJsonObject> NodeObject = NodeToJsonObject(Node);
        if (NodeObject.IsValid())
        {
            NodesArray.Add(MakeShared<FJsonValueObject>(NodeObject));
        }
    }
    JsonObject->SetArrayField(TEXT("nodes"), NodesArray);

    // Add flows
    JsonObject->SetObjectField(TEXT("flows"), FlowsToJsonObject(Graph.Flows));

    return JsonObject;
}

TSharedPtr<FJsonObject> FN2CSerializer::NodeToJsonObject(const FN2CNodeDefinition& Node)
{
    TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();

    // Required fields
    JsonObject->SetStringField(TEXT("id"), Node.ID);
    JsonObject->SetStringField(TEXT("type"), 
        StaticEnum<EN2CNodeType>()->GetNameStringByValue(static_cast<int64>(Node.NodeType)));
    JsonObject->SetStringField(TEXT("name"), Node.Name);
    
    // Optional fields - only add if non-empty
    if (!Node.GetCleanMemberParent().IsEmpty())
    {
        JsonObject->SetStringField(TEXT("member_parent"), Node.GetCleanMemberParent());
    }
    if (!Node.MemberName.IsEmpty())
    {
        JsonObject->SetStringField(TEXT("member_name"), Node.MemberName);
    }
    if (!Node.Comment.IsEmpty())
    {
        JsonObject->SetStringField(TEXT("comment"), Node.Comment);
    }
    
    // Only add flags if true
    if (Node.bPure)
    {
        JsonObject->SetBoolField(TEXT("pure"), true);
    }
    if (Node.bLatent)
    {
        JsonObject->SetBoolField(TEXT("latent"), true);
    }

    // Add input pins array
    TArray<TSharedPtr<FJsonValue>> InputPinsArray;
    for (const FN2CPinDefinition& Pin : Node.InputPins)
    {
        TSharedPtr<FJsonObject> PinObject = PinToJsonObject(Pin);
        if (PinObject.IsValid())
        {
            InputPinsArray.Add(MakeShared<FJsonValueObject>(PinObject));
        }
    }
    JsonObject->SetArrayField(TEXT("input_pins"), InputPinsArray);

    // Add output pins array
    TArray<TSharedPtr<FJsonValue>> OutputPinsArray;
    for (const FN2CPinDefinition& Pin : Node.OutputPins)
    {
        TSharedPtr<FJsonObject> PinObject = PinToJsonObject(Pin);
        if (PinObject.IsValid())
        {
            OutputPinsArray.Add(MakeShared<FJsonValueObject>(PinObject));
        }
    }
    JsonObject->SetArrayField(TEXT("output_pins"), OutputPinsArray);

    return JsonObject;
}

TSharedPtr<FJsonObject> FN2CSerializer::PinToJsonObject(const FN2CPinDefinition& Pin)
{
    TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();

    // Required fields
    JsonObject->SetStringField(TEXT("id"), Pin.ID);
    JsonObject->SetStringField(TEXT("name"), Pin.Name);
    
    // Only add type if not Exec
    if (Pin.Type != EN2CPinType::Exec)
    {
        JsonObject->SetStringField(TEXT("type"), 
            StaticEnum<EN2CPinType>()->GetNameStringByValue(static_cast<int64>(Pin.Type)));
    }
    
    // Optional fields - only add if non-empty
    if (!Pin.SubType.IsEmpty())
    {
        JsonObject->SetStringField(TEXT("sub_type"), Pin.SubType);
    }
    if (!Pin.DefaultValue.IsEmpty())
    {
        JsonObject->SetStringField(TEXT("default_value"), Pin.DefaultValue);
    }
    
    // Only add connection status if true
    if (Pin.bConnected)
    {
        JsonObject->SetBoolField(TEXT("connected"), true);
    }
    
    // Only add flags if true
    if (Pin.bIsReference)
    {
        JsonObject->SetBoolField(TEXT("is_reference"), true);
    }
    if (Pin.bIsConst)
    {
        JsonObject->SetBoolField(TEXT("is_const"), true);
    }
    if (Pin.bIsArray)
    {
        JsonObject->SetBoolField(TEXT("is_array"), true);
    }
    if (Pin.bIsMap)
    {
        JsonObject->SetBoolField(TEXT("is_map"), true);
    }
    if (Pin.bIsSet)
    {
        JsonObject->SetBoolField(TEXT("is_set"), true);
    }

    return JsonObject;
}

TSharedPtr<FJsonObject> FN2CSerializer::FlowsToJsonObject(const FN2CFlows& Flows)
{
    TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();

    // Add execution flows array
    TArray<TSharedPtr<FJsonValue>> ExecutionArray;
    for (const FString& Flow : Flows.Execution)
    {
        ExecutionArray.Add(MakeShared<FJsonValueString>(Flow));
    }
    JsonObject->SetArrayField(TEXT("execution"), ExecutionArray);

    // Add data flows object
    TSharedPtr<FJsonObject> DataFlowsObject = MakeShared<FJsonObject>();
    for (const auto& DataFlow : Flows.Data)
    {
        DataFlowsObject->SetStringField(DataFlow.Key, DataFlow.Value);
    }
    JsonObject->SetObjectField(TEXT("data"), DataFlowsObject);

    return JsonObject;
}

bool FN2CSerializer::ParseBlueprintFromJson(const TSharedPtr<FJsonObject>& JsonObject, FN2CBlueprint& OutBlueprint)
{
    if (!JsonObject.IsValid())
    {
        return false;
    }

    // Parse version
    FString Version;
    if (!JsonObject->TryGetStringField(TEXT("version"), Version))
    {
        FN2CLogger::Get().LogError(TEXT("Missing version field in JSON"), TEXT("Deserialization"));
        return false;
    }
    
    if (Version != TEXT("1.0.0"))
    {
        FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Unexpected version '%s' - expected '1.0.0'"), *Version));
    }
    OutBlueprint.Version.Value = Version;

    // Parse metadata
    const TSharedPtr<FJsonObject>* MetadataObject;
    if (!JsonObject->TryGetObjectField(TEXT("metadata"), MetadataObject))
    {
        FN2CLogger::Get().LogError(TEXT("Missing metadata object in JSON"), TEXT("Deserialization"));
        return false;
    }

    // Track partial deserialization state
    bool bHasPartialData = false;

    FString Name, TypeString, Class;
    if (!(*MetadataObject)->TryGetStringField(TEXT("name"), Name) ||
        !(*MetadataObject)->TryGetStringField(TEXT("blueprint_type"), TypeString) ||
        !(*MetadataObject)->TryGetStringField(TEXT("blueprint_class"), Class))
    {
        FN2CLogger::Get().LogError(TEXT("Missing required metadata fields in JSON"));
        return false;
    }

    OutBlueprint.Metadata.Name = Name;
    OutBlueprint.Metadata.BlueprintClass = Class;

    // Convert type string to enum
    int64 TypeValue = StaticEnum<EN2CBlueprintType>()->GetValueByNameString(TypeString, EGetByNameFlags::None);
    if (TypeValue == INDEX_NONE)
    {
        FN2CLogger::Get().LogError(TEXT("Invalid blueprint_type in JSON"));
        return false;
    }
    OutBlueprint.Metadata.BlueprintType = static_cast<EN2CBlueprintType>(TypeValue);

    // Parse graphs array
    const TArray<TSharedPtr<FJsonValue>>* GraphsArray;
    if (!JsonObject->TryGetArrayField(TEXT("graphs"), GraphsArray))
    {
        FN2CLogger::Get().LogError(TEXT("Missing graphs array in JSON"), TEXT("Deserialization"));
        return false;
    }

    int32 ValidGraphCount = 0;
    int32 TotalGraphCount = GraphsArray->Num();

    OutBlueprint.Graphs.Empty();
    for (const TSharedPtr<FJsonValue>& GraphValue : *GraphsArray)
    {
        const TSharedPtr<FJsonObject>& GraphObject = GraphValue->AsObject();
        if (!GraphObject.IsValid())
        {
            continue;
        }

        FN2CGraph Graph;
        if (ParseGraphFromJson(GraphObject, Graph))
        {
            OutBlueprint.Graphs.Add(Graph);
            ValidGraphCount++;
        }
        else
        {
            FN2CLogger::Get().LogWarning(TEXT("Skipping invalid graph during deserialization"));
            bHasPartialData = true;
        }
    }

    // Log deserialization results
    if (ValidGraphCount < TotalGraphCount)
    {
        FString Context = FString::Printf(TEXT("Processed %d/%d graphs successfully"), 
            ValidGraphCount, TotalGraphCount);
        FN2CLogger::Get().LogWarning(TEXT("Partial deserialization completed"), Context);
        return ValidGraphCount > 0;  // Return true if we got at least one valid graph
    }

    return true;
}

bool FN2CSerializer::ParseGraphFromJson(const TSharedPtr<FJsonObject>& JsonObject, FN2CGraph& OutGraph)
{
    if (!JsonObject.IsValid())
    {
        return false;
    }

    // Parse basic properties
    FString Name, TypeString;
    if (!JsonObject->TryGetStringField(TEXT("name"), Name) ||
        !JsonObject->TryGetStringField(TEXT("graph_type"), TypeString))
    {
        FN2CLogger::Get().LogError(TEXT("Missing required graph fields in JSON"));
        return false;
    }

    OutGraph.Name = Name;

    // Convert type string to enum
    int64 TypeValue = StaticEnum<EN2CGraphType>()->GetValueByNameString(TypeString, EGetByNameFlags::None);
    if (TypeValue == INDEX_NONE)
    {
        FN2CLogger::Get().LogError(TEXT("Invalid graph_type in JSON"));
        return false;
    }
    OutGraph.GraphType = static_cast<EN2CGraphType>(TypeValue);

    // Parse nodes array
    const TArray<TSharedPtr<FJsonValue>>* NodesArray;
    if (!JsonObject->TryGetArrayField(TEXT("nodes"), NodesArray))
    {
        FN2CLogger::Get().LogError(TEXT("Missing nodes array in JSON"));
        return false;
    }

    OutGraph.Nodes.Empty();
    for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArray)
    {
        const TSharedPtr<FJsonObject>& NodeObject = NodeValue->AsObject();
        if (!NodeObject.IsValid())
        {
            continue;
        }

        FN2CNodeDefinition Node;
        if (ParseNodeFromJson(NodeObject, Node))
        {
            OutGraph.Nodes.Add(Node);
        }
    }

    // Parse flows
    const TSharedPtr<FJsonObject>* FlowsObject;
    if (!JsonObject->TryGetObjectField(TEXT("flows"), FlowsObject))
    {
        FN2CLogger::Get().LogError(TEXT("Missing flows object in JSON"));
        return false;
    }

    return ParseFlowsFromJson(*FlowsObject, OutGraph.Flows);
}

bool FN2CSerializer::ParseNodeFromJson(const TSharedPtr<FJsonObject>& JsonObject, FN2CNodeDefinition& OutNode)
{
    if (!JsonObject.IsValid())
    {
        return false;
    }

    // Parse basic properties
    FString ID, TypeString, Name, MemberParent, MemberName, Comment;
    if (!JsonObject->TryGetStringField(TEXT("id"), ID) ||
        !JsonObject->TryGetStringField(TEXT("type"), TypeString) ||
        !JsonObject->TryGetStringField(TEXT("name"), Name))
    {
        FN2CLogger::Get().LogError(TEXT("Missing required node fields in JSON"));
        return false;
    }

    OutNode.ID = ID;
    OutNode.Name = Name;

    // Optional fields
    JsonObject->TryGetStringField(TEXT("member_parent"), MemberParent);
    JsonObject->TryGetStringField(TEXT("member_name"), MemberName);
    JsonObject->TryGetStringField(TEXT("comment"), Comment);

    OutNode.MemberParent = MemberParent;
    OutNode.MemberName = MemberName;
    OutNode.Comment = Comment;

    // Convert type string to enum
    int64 TypeValue = StaticEnum<EN2CNodeType>()->GetValueByNameString(TypeString, EGetByNameFlags::None);
    if (TypeValue == INDEX_NONE)
    {
        FN2CLogger::Get().LogError(TEXT("Invalid node_type in JSON"));
        return false;
    }
    OutNode.NodeType = static_cast<EN2CNodeType>(TypeValue);

    // Parse flags
    bool bPure = false;
    bool bLatent = false;
    JsonObject->TryGetBoolField(TEXT("pure"), bPure);
    JsonObject->TryGetBoolField(TEXT("latent"), bLatent);
    OutNode.bPure = bPure;
    OutNode.bLatent = bLatent;

    // Parse input pins array
    const TArray<TSharedPtr<FJsonValue>>* InputPinsArray;
    if (!JsonObject->TryGetArrayField(TEXT("input_pins"), InputPinsArray))
    {
        FN2CLogger::Get().LogError(TEXT("Missing input_pins array in JSON"));
        return false;
    }

    OutNode.InputPins.Empty();
    for (const TSharedPtr<FJsonValue>& PinValue : *InputPinsArray)
    {
        const TSharedPtr<FJsonObject>& PinObject = PinValue->AsObject();
        if (!PinObject.IsValid())
        {
            continue;
        }

        FN2CPinDefinition Pin;
        if (ParsePinFromJson(PinObject, Pin))
        {
            OutNode.InputPins.Add(Pin);
        }
    }

    // Parse output pins array
    const TArray<TSharedPtr<FJsonValue>>* OutputPinsArray;
    if (!JsonObject->TryGetArrayField(TEXT("output_pins"), OutputPinsArray))
    {
        FN2CLogger::Get().LogError(TEXT("Missing output_pins array in JSON"));
        return false;
    }

    OutNode.OutputPins.Empty();
    for (const TSharedPtr<FJsonValue>& PinValue : *OutputPinsArray)
    {
        const TSharedPtr<FJsonObject>& PinObject = PinValue->AsObject();
        if (!PinObject.IsValid())
        {
            continue;
        }

        FN2CPinDefinition Pin;
        if (ParsePinFromJson(PinObject, Pin))
        {
            OutNode.OutputPins.Add(Pin);
        }
    }

    return true;
}

bool FN2CSerializer::ParsePinFromJson(const TSharedPtr<FJsonObject>& JsonObject, FN2CPinDefinition& OutPin)
{
    if (!JsonObject.IsValid())
    {
        return false;
    }

    // Parse basic properties
    FString ID, Name, TypeString, SubType, DefaultValue;
    if (!JsonObject->TryGetStringField(TEXT("id"), ID) ||
        !JsonObject->TryGetStringField(TEXT("name"), Name) ||
        !JsonObject->TryGetStringField(TEXT("type"), TypeString))
    {
        FN2CLogger::Get().LogError(TEXT("Missing required pin fields in JSON"));
        return false;
    }

    OutPin.ID = ID;
    OutPin.Name = Name;

    // Convert type string to enum
    int64 TypeValue = StaticEnum<EN2CPinType>()->GetValueByNameString(TypeString, EGetByNameFlags::None);
    if (TypeValue == INDEX_NONE)
    {
        FN2CLogger::Get().LogError(TEXT("Invalid pin_type in JSON"));
        return false;
    }
    OutPin.Type = static_cast<EN2CPinType>(TypeValue);

    // Optional fields
    JsonObject->TryGetStringField(TEXT("sub_type"), OutPin.SubType);
    JsonObject->TryGetStringField(TEXT("default_value"), OutPin.DefaultValue);

    // Parse flags
    bool bConnected = false;
    bool bIsReference = false;
    bool bIsConst = false;
    bool bIsArray = false;
    bool bIsMap = false;
    bool bIsSet = false;

    JsonObject->TryGetBoolField(TEXT("connected"), bConnected);
    JsonObject->TryGetBoolField(TEXT("is_reference"), bIsReference);
    JsonObject->TryGetBoolField(TEXT("is_const"), bIsConst);
    JsonObject->TryGetBoolField(TEXT("is_array"), bIsArray);
    JsonObject->TryGetBoolField(TEXT("is_map"), bIsMap);
    JsonObject->TryGetBoolField(TEXT("is_set"), bIsSet);

    OutPin.bConnected = bConnected;
    OutPin.bIsReference = bIsReference;
    OutPin.bIsConst = bIsConst;
    OutPin.bIsArray = bIsArray;
    OutPin.bIsMap = bIsMap;
    OutPin.bIsSet = bIsSet;

    return true;
}

bool FN2CSerializer::ParseFlowsFromJson(const TSharedPtr<FJsonObject>& JsonObject, FN2CFlows& OutFlows)
{
    if (!JsonObject.IsValid())
    {
        return false;
    }

    // Parse execution flows array
    const TArray<TSharedPtr<FJsonValue>>* ExecutionArray;
    if (!JsonObject->TryGetArrayField(TEXT("execution"), ExecutionArray))
    {
        FN2CLogger::Get().LogError(TEXT("Missing execution array in JSON"));
        return false;
    }

    OutFlows.Execution.Empty();
    for (const TSharedPtr<FJsonValue>& FlowValue : *ExecutionArray)
    {
        if (FlowValue->Type == EJson::String)
        {
            OutFlows.Execution.Add(FlowValue->AsString());
        }
    }

    // Parse data flows object
    const TSharedPtr<FJsonObject>* DataFlowsObject;
    if (!JsonObject->TryGetObjectField(TEXT("data"), DataFlowsObject))
    {
        FN2CLogger::Get().LogError(TEXT("Missing data flows object in JSON"));
        return false;
    }

    OutFlows.Data.Empty();
    for (const auto& DataFlow : (*DataFlowsObject)->Values)
    {
        if (DataFlow.Value->Type == EJson::String)
        {
            OutFlows.Data.Add(DataFlow.Key, DataFlow.Value->AsString());
        }
    }

    return true;
}
