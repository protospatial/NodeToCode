// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

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

    // Add structs array
    TArray<TSharedPtr<FJsonValue>> StructsArray;
    for (const FN2CStruct& Struct : Blueprint.Structs)
    {
        TSharedPtr<FJsonObject> StructObject = StructToJsonObject(Struct);
        if (StructObject.IsValid())
        {
            StructsArray.Add(MakeShared<FJsonValueObject>(StructObject));
        }
    }
    JsonObject->SetArrayField(TEXT("structs"), StructsArray);

    // Add enums array
    TArray<TSharedPtr<FJsonValue>> EnumsArray;
    for (const FN2CEnum& Enum : Blueprint.Enums)
    {
        TSharedPtr<FJsonObject> EnumObject = EnumToJsonObject(Enum);
        if (EnumObject.IsValid())
        {
            EnumsArray.Add(MakeShared<FJsonValueObject>(EnumObject));
        }
    }
    JsonObject->SetArrayField(TEXT("enums"), EnumsArray);

    // Add variables array
    TArray<TSharedPtr<FJsonValue>> VarsArray;
    for (const FN2CVariable& Var : Blueprint.Variables)
    {
        TSharedPtr<FJsonObject> VarObject = VariableToJsonObject(Var);
        if (VarObject.IsValid())
        {
            VarsArray.Add(MakeShared<FJsonValueObject>(VarObject));
        }
    }
    JsonObject->SetArrayField(TEXT("variables"), VarsArray);

    // Add components array
    TArray<TSharedPtr<FJsonValue>> ComponentsArray;
    for (const FN2CComponentOverride& Component : Blueprint.Components)
    {
        TSharedPtr<FJsonObject> ComponentObject = MakeShared<FJsonObject>();

        ComponentObject->SetStringField(TEXT("component_name"), Component.ComponentName);
        ComponentObject->SetStringField(TEXT("component_class_name"), Component.ComponentClassName);

        if (!Component.AttachParentName.IsEmpty())
        {
            ComponentObject->SetStringField(TEXT("attach_parent_name"), Component.AttachParentName);
        }

        // Serialize overridden properties as an array of variable objects
        TArray<TSharedPtr<FJsonValue>> OverriddenPropsArray;
        for (const FN2CVariable& Var : Component.OverriddenProperties)
        {
            TSharedPtr<FJsonObject> VarObject = VariableToJsonObject(Var);
            if (VarObject.IsValid())
            {
                OverriddenPropsArray.Add(MakeShared<FJsonValueObject>(VarObject));
            }
        }
        ComponentObject->SetArrayField(TEXT("overridden_properties"), OverriddenPropsArray);

        ComponentsArray.Add(MakeShared<FJsonValueObject>(ComponentObject));
    }
    JsonObject->SetArrayField(TEXT("components"), ComponentsArray);

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

TSharedPtr<FJsonObject> FN2CSerializer::StructToJsonObject(const FN2CStruct& Struct)
{
    TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
    
    // Add basic struct info
    JsonObject->SetStringField(TEXT("name"), Struct.Name);
    
    if (!Struct.Comment.IsEmpty())
    {
        JsonObject->SetStringField(TEXT("comment"), Struct.Comment);
    }
    
    // Add members array
    TArray<TSharedPtr<FJsonValue>> MembersArray;
    for (const FN2CStructMember& Member : Struct.Members)
    {
        TSharedPtr<FJsonObject> MemberObject = MakeShared<FJsonObject>();
        
        // Add member properties
        MemberObject->SetStringField(TEXT("name"), Member.Name);
        MemberObject->SetStringField(TEXT("type"), 
            StaticEnum<EN2CStructMemberType>()->GetNameStringByValue(static_cast<int64>(Member.Type)));
        
        if (!Member.TypeName.IsEmpty())
        {
            MemberObject->SetStringField(TEXT("type_name"), Member.TypeName);
        }
        
        if (Member.bIsArray)
        {
            MemberObject->SetBoolField(TEXT("is_array"), true);
        }
        
        if (Member.bIsSet)
        {
            MemberObject->SetBoolField(TEXT("is_set"), true);
        }
        
        if (Member.bIsMap)
        {
            MemberObject->SetBoolField(TEXT("is_map"), true);
            MemberObject->SetStringField(TEXT("key_type"), 
                StaticEnum<EN2CStructMemberType>()->GetNameStringByValue(static_cast<int64>(Member.KeyType)));
                
            if (!Member.KeyTypeName.IsEmpty())
            {
                MemberObject->SetStringField(TEXT("key_type_name"), Member.KeyTypeName);
            }
        }
        
        if (!Member.DefaultValue.IsEmpty())
        {
            MemberObject->SetStringField(TEXT("default_value"), Member.DefaultValue);
        }
        
        if (!Member.Comment.IsEmpty())
        {
            MemberObject->SetStringField(TEXT("comment"), Member.Comment);
        }
        
        MembersArray.Add(MakeShared<FJsonValueObject>(MemberObject));
    }
    
    JsonObject->SetArrayField(TEXT("members"), MembersArray);
    
    return JsonObject;
}

TSharedPtr<FJsonObject> FN2CSerializer::EnumToJsonObject(const FN2CEnum& Enum)
{
    TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
    
    // Add basic enum info
    JsonObject->SetStringField(TEXT("name"), Enum.Name);
    
    if (!Enum.Comment.IsEmpty())
    {
        JsonObject->SetStringField(TEXT("comment"), Enum.Comment);
    }
    
    // Add values array
    TArray<TSharedPtr<FJsonValue>> ValuesArray;
    for (const FN2CEnumValue& Value : Enum.Values)
    {
        TSharedPtr<FJsonObject> ValueObject = MakeShared<FJsonObject>();
        
        // Add value properties
        ValueObject->SetStringField(TEXT("name"), Value.Name);
        
        if (!Value.Comment.IsEmpty())
        {
            ValueObject->SetStringField(TEXT("comment"), Value.Comment);
        }
        
        ValuesArray.Add(MakeShared<FJsonValueObject>(ValueObject));
    }
    
    JsonObject->SetArrayField(TEXT("values"), ValuesArray);
    
    return JsonObject;
}

TSharedPtr<FJsonObject> FN2CSerializer::VariableToJsonObject(const FN2CVariable& Var)
{
    TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();

    JsonObject->SetStringField(TEXT("name"), Var.Name);
    JsonObject->SetStringField(TEXT("type"),
        StaticEnum<EN2CStructMemberType>()->GetNameStringByValue(static_cast<int64>(Var.Type)));

    if (!Var.TypeName.IsEmpty())
    {
        JsonObject->SetStringField(TEXT("type_name"), Var.TypeName);
    }

    if (Var.bIsArray)
    {
        JsonObject->SetBoolField(TEXT("is_array"), true);
    }
    if (Var.bIsSet)
    {
        JsonObject->SetBoolField(TEXT("is_set"), true);
    }
    if (Var.bIsMap)
    {
        JsonObject->SetBoolField(TEXT("is_map"), true);
        JsonObject->SetStringField(TEXT("key_type"),
            StaticEnum<EN2CStructMemberType>()->GetNameStringByValue(static_cast<int64>(Var.KeyType)));
        if (!Var.KeyTypeName.IsEmpty())
        {
            JsonObject->SetStringField(TEXT("key_type_name"), Var.KeyTypeName);
        }
    }

    if (!Var.DefaultValue.IsEmpty())
    {
        JsonObject->SetStringField(TEXT("default_value"), Var.DefaultValue);
    }
    if (!Var.Comment.IsEmpty())
    {
        JsonObject->SetStringField(TEXT("comment"), Var.Comment);
    }

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

    // Optionally parse variables array
    const TArray<TSharedPtr<FJsonValue>>* VarsArray;
    if (JsonObject->TryGetArrayField(TEXT("variables"), VarsArray))
    {
        OutBlueprint.Variables.Empty();
        for (const TSharedPtr<FJsonValue>& VarValue : *VarsArray)
        {
            const TSharedPtr<FJsonObject>& VarObject = VarValue->AsObject();
            if (!VarObject.IsValid())
            {
                continue;
            }

            FN2CVariable Var;
            if (ParseVariableFromJson(VarObject, Var))
            {
                OutBlueprint.Variables.Add(Var);
            }
        }
    }

    return true;
}

bool FN2CSerializer::ParseVariableFromJson(const TSharedPtr<FJsonObject>& JsonObject, FN2CVariable& OutVar)
{
    if (!JsonObject.IsValid())
    {
        return false;
    }

    FString Name, TypeString;
    if (!JsonObject->TryGetStringField(TEXT("name"), Name) ||
        !JsonObject->TryGetStringField(TEXT("type"), TypeString))
    {
        FN2CLogger::Get().LogError(TEXT("Missing required variable fields in JSON"));
        return false;
    }

    OutVar.Name = Name;

    int64 TypeValue = StaticEnum<EN2CStructMemberType>()->GetValueByNameString(TypeString, EGetByNameFlags::None);
    if (TypeValue == INDEX_NONE)
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Invalid variable type: %s"), *TypeString));
        return false;
    }
    OutVar.Type = static_cast<EN2CStructMemberType>(TypeValue);

    // Optional fields
    JsonObject->TryGetStringField(TEXT("type_name"), OutVar.TypeName);
    JsonObject->TryGetBoolField(TEXT("is_array"), OutVar.bIsArray);
    JsonObject->TryGetBoolField(TEXT("is_set"), OutVar.bIsSet);
    JsonObject->TryGetBoolField(TEXT("is_map"), OutVar.bIsMap);

    if (OutVar.bIsMap)
    {
        FString KeyTypeString;
        if (JsonObject->TryGetStringField(TEXT("key_type"), KeyTypeString))
        {
            int64 KeyTypeValue = StaticEnum<EN2CStructMemberType>()->GetValueByNameString(KeyTypeString, EGetByNameFlags::None);
            if (KeyTypeValue != INDEX_NONE)
            {
                OutVar.KeyType = static_cast<EN2CStructMemberType>(KeyTypeValue);
            }
        }
        JsonObject->TryGetStringField(TEXT("key_type_name"), OutVar.KeyTypeName);
    }

    JsonObject->TryGetStringField(TEXT("default_value"), OutVar.DefaultValue);
    JsonObject->TryGetStringField(TEXT("comment"), OutVar.Comment);

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

bool FN2CSerializer::ParseStructFromJson(const TSharedPtr<FJsonObject>& JsonObject, FN2CStruct& OutStruct)
{
    if (!JsonObject.IsValid())
    {
        return false;
    }
    
    // Parse basic struct info
    FString Name;
    
    if (!JsonObject->TryGetStringField(TEXT("name"), Name))
    {
        FN2CLogger::Get().LogError(TEXT("Missing required struct fields in JSON"));
        return false;
    }
    
    OutStruct.Name = Name;
    
    // Parse optional comment
    FString Comment;
    if (JsonObject->TryGetStringField(TEXT("comment"), Comment))
    {
        OutStruct.Comment = Comment;
    }
    
    // Parse members array
    const TArray<TSharedPtr<FJsonValue>>* MembersArray;
    if (!JsonObject->TryGetArrayField(TEXT("members"), MembersArray))
    {
        FN2CLogger::Get().LogError(TEXT("Missing members array in JSON"));
        return false;
    }
    
    OutStruct.Members.Empty();
    for (const TSharedPtr<FJsonValue>& MemberValue : *MembersArray)
    {
        const TSharedPtr<FJsonObject>& MemberObject = MemberValue->AsObject();
        if (!MemberObject.IsValid())
        {
            continue;
        }
        
        FN2CStructMember Member;
        
        // Parse member properties
        FString MemberName, TypeString, TypeName;
        
        if (!MemberObject->TryGetStringField(TEXT("name"), MemberName) ||
            !MemberObject->TryGetStringField(TEXT("type"), TypeString))
        {
            FN2CLogger::Get().LogError(TEXT("Missing required member fields in JSON"));
            continue;
        }
        
        Member.Name = MemberName;
        
        // Convert type string to enum
        int64 TypeValue = StaticEnum<EN2CStructMemberType>()->GetValueByNameString(TypeString, EGetByNameFlags::None);
        if (TypeValue == INDEX_NONE)
        {
            FN2CLogger::Get().LogError(FString::Printf(TEXT("Invalid member type: %s"), *TypeString));
            continue;
        }
        Member.Type = static_cast<EN2CStructMemberType>(TypeValue);
        
        // Parse optional fields
        MemberObject->TryGetStringField(TEXT("type_name"), Member.TypeName);
        MemberObject->TryGetBoolField(TEXT("is_array"), Member.bIsArray);
        MemberObject->TryGetBoolField(TEXT("is_set"), Member.bIsSet);
        MemberObject->TryGetBoolField(TEXT("is_map"), Member.bIsMap);
        
        if (Member.bIsMap)
        {
            FString KeyTypeString;
            if (MemberObject->TryGetStringField(TEXT("key_type"), KeyTypeString))
            {
                int64 KeyTypeValue = StaticEnum<EN2CStructMemberType>()->GetValueByNameString(KeyTypeString, EGetByNameFlags::None);
                if (KeyTypeValue != INDEX_NONE)
                {
                    Member.KeyType = static_cast<EN2CStructMemberType>(KeyTypeValue);
                }
            }
            
            MemberObject->TryGetStringField(TEXT("key_type_name"), Member.KeyTypeName);
        }
        
        MemberObject->TryGetStringField(TEXT("default_value"), Member.DefaultValue);
        MemberObject->TryGetStringField(TEXT("comment"), Member.Comment);
        
        OutStruct.Members.Add(Member);
    }
    
    return true;
}

bool FN2CSerializer::ParseEnumFromJson(const TSharedPtr<FJsonObject>& JsonObject, FN2CEnum& OutEnum)
{
    if (!JsonObject.IsValid())
    {
        return false;
    }
    
    // Parse basic enum info
    FString Name;
    
    if (!JsonObject->TryGetStringField(TEXT("name"), Name))
    {
        FN2CLogger::Get().LogError(TEXT("Missing required enum fields in JSON"));
        return false;
    }
    
    OutEnum.Name = Name;
    
    // Parse optional comment
    FString Comment;
    if (JsonObject->TryGetStringField(TEXT("comment"), Comment))
    {
        OutEnum.Comment = Comment;
    }
    
    // Parse values array
    const TArray<TSharedPtr<FJsonValue>>* ValuesArray;
    if (!JsonObject->TryGetArrayField(TEXT("values"), ValuesArray))
    {
        FN2CLogger::Get().LogError(TEXT("Missing values array in JSON"));
        return false;
    }
    
    OutEnum.Values.Empty();
    for (const TSharedPtr<FJsonValue>& ValueValue : *ValuesArray)
    {
        const TSharedPtr<FJsonObject>& ValueObject = ValueValue->AsObject();
        if (!ValueObject.IsValid())
        {
            continue;
        }
        
        FN2CEnumValue Value;
        
        // Parse value properties
        FString ValueName;
        double ValueNumber;
        
        if (!ValueObject->TryGetStringField(TEXT("name"), ValueName) ||
            !ValueObject->TryGetNumberField(TEXT("value"), ValueNumber))
        {
            FN2CLogger::Get().LogError(TEXT("Missing required enum value fields in JSON"));
            continue;
        }
        
        Value.Name = ValueName;
        
        // Parse optional comment
        FString ValueComment;
        if (ValueObject->TryGetStringField(TEXT("comment"), ValueComment))
        {
            Value.Comment = ValueComment;
        }
        
        OutEnum.Values.Add(Value);
    }
    
    return true;
}
