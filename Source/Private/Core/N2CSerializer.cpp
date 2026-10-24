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

TSharedPtr<FJsonObject> FN2CSerializer::GraphPinToJsonObject(const FN2CGraphPin& Pin)
{
    TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
    JsonObject->SetStringField(TEXT("node_guid"), Pin.NodeGuid);
    JsonObject->SetStringField(TEXT("pin_guid"), Pin.PinGuid);
    JsonObject->SetStringField(TEXT("node_id"), Pin.NodeId);
    JsonObject->SetStringField(TEXT("pin_id"), Pin.PinId);
    JsonObject->SetStringField(TEXT("node_name"), Pin.NodeName);
    JsonObject->SetStringField(TEXT("node_class"), Pin.NodeClass);
    JsonObject->SetStringField(TEXT("pin_name"), Pin.PinName);
    JsonObject->SetStringField(TEXT("display_name"), Pin.DisplayName);
    JsonObject->SetStringField(TEXT("friendly_name"), Pin.FriendlyName);
    JsonObject->SetStringField(TEXT("direction"), Pin.Direction);
    JsonObject->SetStringField(TEXT("category"), Pin.Category);
    JsonObject->SetStringField(TEXT("sub_category"), Pin.SubCategory);
    JsonObject->SetStringField(TEXT("sub_category_object"), Pin.SubCategoryObject);
    JsonObject->SetStringField(TEXT("sub_category_member_parent"), Pin.SubCategoryMemberParent);
    JsonObject->SetStringField(TEXT("sub_category_member_name"), Pin.SubCategoryMemberName);
    JsonObject->SetStringField(TEXT("container_type"), Pin.ContainerType);
    JsonObject->SetStringField(TEXT("value_terminal_category"), Pin.ValueTerminalCategory);
    JsonObject->SetStringField(TEXT("value_terminal_sub_category"), Pin.ValueTerminalSubCategory);
    JsonObject->SetStringField(TEXT("value_terminal_sub_category_object"), Pin.ValueTerminalSubCategoryObject);
    JsonObject->SetStringField(TEXT("pin_tooltip"), Pin.PinToolTip);
    JsonObject->SetStringField(TEXT("default_value"), Pin.DefaultValue);
    JsonObject->SetStringField(TEXT("autogenerated_default_value"), Pin.AutogeneratedDefaultValue);
    JsonObject->SetStringField(TEXT("default_object_path"), Pin.DefaultObjectPath);
    JsonObject->SetStringField(TEXT("default_text_value"), Pin.DefaultTextValue);
    JsonObject->SetStringField(TEXT("persistent_guid"), Pin.PersistentGuid);
    JsonObject->SetStringField(TEXT("parent_pin_guid"), Pin.ParentPinGuid);
    JsonObject->SetStringField(TEXT("reference_pass_through_node_guid"), Pin.ReferencePassThroughNodeGuid);
    JsonObject->SetStringField(TEXT("reference_pass_through_pin_guid"), Pin.ReferencePassThroughPinGuid);

    TArray<TSharedPtr<FJsonValue>> SubPinGuids;
    for (const FString& SubPinGuid : Pin.SubPinGuids)
    {
        SubPinGuids.Add(MakeShared<FJsonValueString>(SubPinGuid));
    }
    JsonObject->SetArrayField(TEXT("sub_pin_guids"), SubPinGuids);

    JsonObject->SetBoolField(TEXT("is_execution"), Pin.bIsExecution);
    JsonObject->SetBoolField(TEXT("deprecated"), Pin.bDeprecated);
    JsonObject->SetBoolField(TEXT("display_as_mutable_ref"), Pin.bDisplayAsMutableRef);
    JsonObject->SetBoolField(TEXT("allow_friendly_name"), Pin.bAllowFriendlyName);
    JsonObject->SetBoolField(TEXT("save_pin_if_orphaned"), Pin.bSavePinIfOrphaned);
    JsonObject->SetBoolField(TEXT("backwards_compat_empty_autogenerated_value"), Pin.bUseBackwardsCompatForEmptyAutogeneratedValue);
    JsonObject->SetBoolField(TEXT("connected"), Pin.bConnected);
    JsonObject->SetBoolField(TEXT("hidden"), Pin.bHidden);
    JsonObject->SetBoolField(TEXT("not_connectable"), Pin.bNotConnectable);
    JsonObject->SetBoolField(TEXT("orphaned"), Pin.bOrphaned);
    JsonObject->SetBoolField(TEXT("is_reference"), Pin.bIsReference);
    JsonObject->SetBoolField(TEXT("is_const"), Pin.bIsConst);
    JsonObject->SetBoolField(TEXT("is_weak_pointer"), Pin.bIsWeakPointer);
    JsonObject->SetBoolField(TEXT("is_uobject_wrapper"), Pin.bIsUObjectWrapper);
    JsonObject->SetBoolField(TEXT("serialize_as_single_precision_float"), Pin.bSerializeAsSinglePrecisionFloat);
    JsonObject->SetBoolField(TEXT("advanced_view"), Pin.bAdvancedView);
    JsonObject->SetBoolField(TEXT("default_value_is_read_only"), Pin.bDefaultValueIsReadOnly);
    JsonObject->SetBoolField(TEXT("default_value_is_ignored"), Pin.bDefaultValueIsIgnored);
    return JsonObject;
}

TSharedPtr<FJsonObject> FN2CSerializer::GraphToJsonObject(const FN2CGraph& Graph)
{
    TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
    JsonObject->SetStringField(TEXT("name"), Graph.Name);
    JsonObject->SetStringField(TEXT("graph_type"),
        StaticEnum<EN2CGraphType>()->GetNameStringByValue(static_cast<int64>(Graph.GraphType)));
    JsonObject->SetStringField(TEXT("graph_guid"), Graph.GraphGuid);
    JsonObject->SetStringField(TEXT("graph_path"), Graph.GraphPath);
    JsonObject->SetNumberField(TEXT("native_node_count"), Graph.NativeNodeCount);
    JsonObject->SetNumberField(TEXT("skipped_node_count"), Graph.SkippedNodeCount);
    JsonObject->SetBoolField(TEXT("no_executable_nodes"), Graph.bNoExecutableNodes);

    TArray<TSharedPtr<FJsonValue>> SkippedNodeClasses;
    for (const FString& NodeClass : Graph.SkippedNodeClasses)
    {
        SkippedNodeClasses.Add(MakeShared<FJsonValueString>(NodeClass));
    }
    JsonObject->SetArrayField(TEXT("skipped_node_classes"), SkippedNodeClasses);

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

    TArray<TSharedPtr<FJsonValue>> PinsArray;
    for (const FN2CGraphPin& Pin : Graph.Pins)
    {
        PinsArray.Add(MakeShared<FJsonValueObject>(GraphPinToJsonObject(Pin)));
    }
    JsonObject->SetArrayField(TEXT("pins"), PinsArray);

    // Legacy compact flows are retained only for backward compatibility.
    JsonObject->SetObjectField(TEXT("flows"), FlowsToJsonObject(Graph.Flows));

    TArray<TSharedPtr<FJsonValue>> DirectLinks;
    for (const FN2CDirectLink& Link : Graph.Connections)
    {
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("source_node_guid"), Link.SourceNodeGuid);
        Entry->SetStringField(TEXT("source_pin_guid"), Link.SourcePinGuid);
        Entry->SetStringField(TEXT("target_node_guid"), Link.TargetNodeGuid);
        Entry->SetStringField(TEXT("target_pin_guid"), Link.TargetPinGuid);
        Entry->SetStringField(TEXT("source_node_id"), Link.SourceNodeId);
        Entry->SetStringField(TEXT("source_pin_id"), Link.SourcePinId);
        Entry->SetStringField(TEXT("target_node_id"), Link.TargetNodeId);
        Entry->SetStringField(TEXT("target_pin_id"), Link.TargetPinId);
        Entry->SetStringField(TEXT("source_node_name"), Link.SourceNodeName);
        Entry->SetStringField(TEXT("source_node_class"), Link.SourceNodeClass);
        Entry->SetStringField(TEXT("source_pin_name"), Link.SourcePinName);
        Entry->SetStringField(TEXT("source_direction"), Link.SourceDirection);
        Entry->SetStringField(TEXT("target_node_name"), Link.TargetNodeName);
        Entry->SetStringField(TEXT("target_node_class"), Link.TargetNodeClass);
        Entry->SetStringField(TEXT("target_pin_name"), Link.TargetPinName);
        Entry->SetStringField(TEXT("target_direction"), Link.TargetDirection);
        Entry->SetBoolField(TEXT("is_execution"), Link.bIsExecution);
        Entry->SetBoolField(TEXT("source_hidden"), Link.bSourceHidden);
        Entry->SetBoolField(TEXT("target_hidden"), Link.bTargetHidden);
        Entry->SetBoolField(TEXT("source_is_knot"), Link.bSourceIsKnot);
        Entry->SetBoolField(TEXT("target_is_knot"), Link.bTargetIsKnot);
        DirectLinks.Add(MakeShared<FJsonValueObject>(Entry));
    }
    JsonObject->SetArrayField(TEXT("connections"), DirectLinks);

    TArray<TSharedPtr<FJsonValue>> Errors;
    for (const FString& Error : Graph.ConnectionErrors)
    {
        Errors.Add(MakeShared<FJsonValueString>(Error));
    }
    JsonObject->SetArrayField(TEXT("connection_errors"), Errors);
    JsonObject->SetNumberField(TEXT("unresolved_connection_count"), Graph.UnresolvedConnectionCount);
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
    
    // Exec is required too: ParsePinFromJson expects the type field.
    JsonObject->SetStringField(TEXT("type"),
        StaticEnum<EN2CPinType>()->GetNameStringByValue(static_cast<int64>(Pin.Type)));
    
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
            FN2CLogger::Get().LogError(TEXT("Invalid graph object in Blueprint JSON"));
            bHasPartialData = true;
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
        FN2CLogger::Get().LogWarning(TEXT("Partial deserialization detected"), Context);
        bHasPartialData = true;  // Preserve diagnostics and continue parsing variables.
    }

    // Optionally parse variables array
    const TArray<TSharedPtr<FJsonValue>>* VarsArray = nullptr;
    if (JsonObject->HasField(TEXT("variables")))
    {
        if (!JsonObject->TryGetArrayField(TEXT("variables"), VarsArray))
        {
            FN2CLogger::Get().LogError(TEXT("Invalid variables array in JSON"));
            return false;
        }
        OutBlueprint.Variables.Empty();
        for (const TSharedPtr<FJsonValue>& VarValue : *VarsArray)
        {
            const TSharedPtr<FJsonObject> VarObject = VarValue.IsValid() ? VarValue->AsObject() : nullptr;
            if (!VarObject.IsValid())
            {
                bHasPartialData = true;
                continue;
            }

            FN2CVariable Var;
            if (ParseVariableFromJson(VarObject, Var))
            {
                OutBlueprint.Variables.Add(Var);
            }
            else
            {
                bHasPartialData = true;
            }
        }
    }

    return !bHasPartialData && ValidGraphCount == TotalGraphCount;
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

bool FN2CSerializer::ParseGraphPinFromJson(const TSharedPtr<FJsonObject>& JsonObject, FN2CGraphPin& OutPin)
{
    if (!JsonObject.IsValid())
    {
        return false;
    }

    if (!JsonObject->TryGetStringField(TEXT("node_guid"), OutPin.NodeGuid) ||
        !JsonObject->TryGetStringField(TEXT("pin_guid"), OutPin.PinGuid) ||
        !JsonObject->TryGetStringField(TEXT("node_id"), OutPin.NodeId) ||
        !JsonObject->TryGetStringField(TEXT("pin_id"), OutPin.PinId) ||
        !JsonObject->TryGetStringField(TEXT("node_name"), OutPin.NodeName) ||
        !JsonObject->TryGetStringField(TEXT("node_class"), OutPin.NodeClass) ||
        !JsonObject->TryGetStringField(TEXT("pin_name"), OutPin.PinName) ||
        !JsonObject->TryGetStringField(TEXT("display_name"), OutPin.DisplayName) ||
        !JsonObject->TryGetStringField(TEXT("friendly_name"), OutPin.FriendlyName) ||
        !JsonObject->TryGetStringField(TEXT("direction"), OutPin.Direction) ||
        !JsonObject->TryGetStringField(TEXT("category"), OutPin.Category) ||
        !JsonObject->TryGetStringField(TEXT("sub_category"), OutPin.SubCategory) ||
        !JsonObject->TryGetStringField(TEXT("sub_category_object"), OutPin.SubCategoryObject) ||
        !JsonObject->TryGetStringField(TEXT("sub_category_member_parent"), OutPin.SubCategoryMemberParent) ||
        !JsonObject->TryGetStringField(TEXT("sub_category_member_name"), OutPin.SubCategoryMemberName) ||
        !JsonObject->TryGetStringField(TEXT("container_type"), OutPin.ContainerType) ||
        !JsonObject->TryGetStringField(TEXT("value_terminal_category"), OutPin.ValueTerminalCategory) ||
        !JsonObject->TryGetStringField(TEXT("value_terminal_sub_category"), OutPin.ValueTerminalSubCategory) ||
        !JsonObject->TryGetStringField(TEXT("value_terminal_sub_category_object"), OutPin.ValueTerminalSubCategoryObject) ||
        !JsonObject->TryGetStringField(TEXT("pin_tooltip"), OutPin.PinToolTip) ||
        !JsonObject->TryGetStringField(TEXT("default_value"), OutPin.DefaultValue) ||
        !JsonObject->TryGetStringField(TEXT("autogenerated_default_value"), OutPin.AutogeneratedDefaultValue) ||
        !JsonObject->TryGetStringField(TEXT("default_object_path"), OutPin.DefaultObjectPath) ||
        !JsonObject->TryGetStringField(TEXT("default_text_value"), OutPin.DefaultTextValue) ||
        !JsonObject->TryGetStringField(TEXT("persistent_guid"), OutPin.PersistentGuid) ||
        !JsonObject->TryGetStringField(TEXT("parent_pin_guid"), OutPin.ParentPinGuid) ||
        !JsonObject->TryGetStringField(TEXT("reference_pass_through_node_guid"), OutPin.ReferencePassThroughNodeGuid) ||
        !JsonObject->TryGetStringField(TEXT("reference_pass_through_pin_guid"), OutPin.ReferencePassThroughPinGuid) ||
        !JsonObject->TryGetBoolField(TEXT("is_execution"), OutPin.bIsExecution) ||
        !JsonObject->TryGetBoolField(TEXT("deprecated"), OutPin.bDeprecated) ||
        !JsonObject->TryGetBoolField(TEXT("display_as_mutable_ref"), OutPin.bDisplayAsMutableRef) ||
        !JsonObject->TryGetBoolField(TEXT("allow_friendly_name"), OutPin.bAllowFriendlyName) ||
        !JsonObject->TryGetBoolField(TEXT("save_pin_if_orphaned"), OutPin.bSavePinIfOrphaned) ||
        !JsonObject->TryGetBoolField(TEXT("backwards_compat_empty_autogenerated_value"), OutPin.bUseBackwardsCompatForEmptyAutogeneratedValue) ||
        !JsonObject->TryGetBoolField(TEXT("connected"), OutPin.bConnected) ||
        !JsonObject->TryGetBoolField(TEXT("hidden"), OutPin.bHidden) ||
        !JsonObject->TryGetBoolField(TEXT("not_connectable"), OutPin.bNotConnectable) ||
        !JsonObject->TryGetBoolField(TEXT("orphaned"), OutPin.bOrphaned) ||
        !JsonObject->TryGetBoolField(TEXT("is_reference"), OutPin.bIsReference) ||
        !JsonObject->TryGetBoolField(TEXT("is_const"), OutPin.bIsConst) ||
        !JsonObject->TryGetBoolField(TEXT("is_weak_pointer"), OutPin.bIsWeakPointer) ||
        !JsonObject->TryGetBoolField(TEXT("is_uobject_wrapper"), OutPin.bIsUObjectWrapper) ||
        !JsonObject->TryGetBoolField(TEXT("serialize_as_single_precision_float"), OutPin.bSerializeAsSinglePrecisionFloat) ||
        !JsonObject->TryGetBoolField(TEXT("advanced_view"), OutPin.bAdvancedView) ||
        !JsonObject->TryGetBoolField(TEXT("default_value_is_read_only"), OutPin.bDefaultValueIsReadOnly) ||
        !JsonObject->TryGetBoolField(TEXT("default_value_is_ignored"), OutPin.bDefaultValueIsIgnored))
    {
        return false;
    }

    FGuid ParsedNodeGuid;
    FGuid ParsedPinGuid;
    if (!FGuid::Parse(OutPin.NodeGuid, ParsedNodeGuid) || !ParsedNodeGuid.IsValid() ||
        !FGuid::Parse(OutPin.PinGuid, ParsedPinGuid) || !ParsedPinGuid.IsValid())
    {
        return false;
    }

    const UEnum* ContainerType = StaticEnum<EPinContainerType>();
    if (!ContainerType || ContainerType->GetValueByNameString(OutPin.ContainerType, EGetByNameFlags::None) == INDEX_NONE)
    {
        return false;
    }

    if (OutPin.Direction != TEXT("Input") && OutPin.Direction != TEXT("Output"))
    {
        return false;
    }
    if (OutPin.bIsExecution != (OutPin.Category == TEXT("exec")))
    {
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* SubPinGuids = nullptr;
    if (!JsonObject->TryGetArrayField(TEXT("sub_pin_guids"), SubPinGuids))
    {
        return false;
    }
    for (const TSharedPtr<FJsonValue>& Value : *SubPinGuids)
    {
        FString SubPinGuid;
        FGuid ParsedSubPinGuid;
        if (!Value.IsValid() || !Value->TryGetString(SubPinGuid) ||
            !FGuid::Parse(SubPinGuid, ParsedSubPinGuid) || !ParsedSubPinGuid.IsValid())
        {
            return false;
        }
        OutPin.SubPinGuids.Add(MoveTemp(SubPinGuid));
    }

    const bool bHasParentPin = !OutPin.ParentPinGuid.IsEmpty();
    if (bHasParentPin)
    {
        FGuid ParentPinGuid;
        if (!FGuid::Parse(OutPin.ParentPinGuid, ParentPinGuid) || !ParentPinGuid.IsValid())
        {
            return false;
        }
    }

    const bool bHasReferencePassThroughNode = !OutPin.ReferencePassThroughNodeGuid.IsEmpty();
    const bool bHasReferencePassThroughPin = !OutPin.ReferencePassThroughPinGuid.IsEmpty();
    if (bHasReferencePassThroughNode != bHasReferencePassThroughPin)
    {
        return false;
    }
    if (bHasReferencePassThroughNode)
    {
        FGuid ReferenceNodeGuid;
        FGuid ReferencePinGuid;
        if (!FGuid::Parse(OutPin.ReferencePassThroughNodeGuid, ReferenceNodeGuid) || !ReferenceNodeGuid.IsValid() ||
            !FGuid::Parse(OutPin.ReferencePassThroughPinGuid, ReferencePinGuid) || !ReferencePinGuid.IsValid())
        {
            return false;
        }
    }
    return true;
}

bool FN2CSerializer::ParseGraphFromJson(const TSharedPtr<FJsonObject>& JsonObject, FN2CGraph& OutGraph)
{
    if (!JsonObject.IsValid())
    {
        return false;
    }

    FString Name;
    FString TypeString;
    if (!JsonObject->TryGetStringField(TEXT("name"), Name) ||
        !JsonObject->TryGetStringField(TEXT("graph_type"), TypeString))
    {
        FN2CLogger::Get().LogError(TEXT("Missing required graph fields in JSON"));
        return false;
    }

    const int64 TypeValue = StaticEnum<EN2CGraphType>()->GetValueByNameString(TypeString, EGetByNameFlags::None);
    if (TypeValue == INDEX_NONE)
    {
        FN2CLogger::Get().LogError(TEXT("Invalid graph_type in JSON"));
        return false;
    }
    OutGraph.Name = Name;
    OutGraph.GraphType = static_cast<EN2CGraphType>(TypeValue);

    const TCHAR* CoverageFields[] = {
        TEXT("graph_guid"), TEXT("graph_path"), TEXT("native_node_count"),
        TEXT("skipped_node_count"), TEXT("skipped_node_classes"), TEXT("no_executable_nodes")
    };
    bool bHasCoverageMetadata = false;
    for (const TCHAR* Field : CoverageFields)
    {
        bHasCoverageMetadata |= JsonObject->HasField(Field);
    }
    if (bHasCoverageMetadata)
    {
        FString GraphGuid;
        FString GraphPath;
        double NativeNodeCount = 0.0;
        double SkippedNodeCount = 0.0;
        bool bNoExecutableNodes = false;
        const TArray<TSharedPtr<FJsonValue>>* SkippedClasses = nullptr;
        if (!JsonObject->TryGetStringField(TEXT("graph_guid"), GraphGuid) ||
            !JsonObject->TryGetStringField(TEXT("graph_path"), GraphPath) ||
            !JsonObject->TryGetNumberField(TEXT("native_node_count"), NativeNodeCount) ||
            !JsonObject->TryGetNumberField(TEXT("skipped_node_count"), SkippedNodeCount) ||
            !JsonObject->TryGetArrayField(TEXT("skipped_node_classes"), SkippedClasses) ||
            !JsonObject->TryGetBoolField(TEXT("no_executable_nodes"), bNoExecutableNodes) ||
            NativeNodeCount < 0.0 || NativeNodeCount > static_cast<double>(MAX_int32) ||
            SkippedNodeCount < 0.0 || SkippedNodeCount > NativeNodeCount ||
            FMath::FloorToDouble(NativeNodeCount) != NativeNodeCount ||
            FMath::FloorToDouble(SkippedNodeCount) != SkippedNodeCount)
        {
            FN2CLogger::Get().LogError(FString::Printf(
                TEXT("Invalid graph coverage metadata in graph '%s'."), *Name));
            return false;
        }

        OutGraph.GraphGuid = MoveTemp(GraphGuid);
        OutGraph.GraphPath = MoveTemp(GraphPath);
        OutGraph.NativeNodeCount = static_cast<int32>(NativeNodeCount);
        OutGraph.SkippedNodeCount = static_cast<int32>(SkippedNodeCount);
        OutGraph.bNoExecutableNodes = bNoExecutableNodes;
        for (const TSharedPtr<FJsonValue>& ClassValue : *SkippedClasses)
        {
            FString NodeClass;
            if (!ClassValue.IsValid() || !ClassValue->TryGetString(NodeClass) || NodeClass.IsEmpty())
            {
                FN2CLogger::Get().LogError(FString::Printf(
                    TEXT("Invalid skipped node class in graph '%s'."), *Name));
                return false;
            }
            OutGraph.SkippedNodeClasses.AddUnique(MoveTemp(NodeClass));
        }
        if (OutGraph.SkippedNodeCount > 0 && OutGraph.SkippedNodeClasses.IsEmpty())
        {
            FN2CLogger::Get().LogError(FString::Printf(
                TEXT("Graph '%s' reports skipped nodes without their native classes."), *Name));
            return false;
        }
        if (OutGraph.GraphType != EN2CGraphType::ClassItSelf && OutGraph.GraphPath.IsEmpty())
        {
            FN2CLogger::Get().LogError(FString::Printf(
                TEXT("Graph '%s' is missing its native object path."), *Name));
            return false;
        }
    }

    const TArray<TSharedPtr<FJsonValue>>* NodesArray = nullptr;
    if (!JsonObject->TryGetArrayField(TEXT("nodes"), NodesArray))
    {
        FN2CLogger::Get().LogError(TEXT("Missing nodes array in JSON"));
        return false;
    }

    OutGraph.Nodes.Empty();
    for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArray)
    {
        const TSharedPtr<FJsonObject> NodeObject = NodeValue.IsValid() ? NodeValue->AsObject() : nullptr;
        if (!NodeObject.IsValid())
        {
            FN2CLogger::Get().LogError(FString::Printf(
                TEXT("Invalid node object in graph '%s'."), *Name));
            return false;
        }

        FN2CNodeDefinition Node;
        if (!ParseNodeFromJson(NodeObject, Node))
        {
            FN2CLogger::Get().LogError(FString::Printf(
                TEXT("Failed to parse node in graph '%s'."), *Name));
            return false;
        }
        OutGraph.Nodes.Add(MoveTemp(Node));
    }

    if (bHasCoverageMetadata)
    {
        if (OutGraph.NativeNodeCount != OutGraph.Nodes.Num() + OutGraph.SkippedNodeCount ||
            (OutGraph.bNoExecutableNodes && OutGraph.Nodes.Num() > 0))
        {
            FN2CLogger::Get().LogError(FString::Printf(
                TEXT("Graph '%s' native/translated/skipped node counts disagree."), *Name));
            return false;
        }
    }

    OutGraph.Pins.Empty();
    TMap<FString, int32> PinIndexes;
    const TArray<TSharedPtr<FJsonValue>>* PinsArray = nullptr;
    const bool bHasPinMetadata = JsonObject->HasField(TEXT("pins"));
    if (bHasPinMetadata)
    {
        if (!JsonObject->TryGetArrayField(TEXT("pins"), PinsArray))
        {
            FN2CLogger::Get().LogError(FString::Printf(
                TEXT("Invalid pins array in graph '%s'."), *Name));
            return false;
        }

        for (const TSharedPtr<FJsonValue>& PinValue : *PinsArray)
        {
            const TSharedPtr<FJsonObject> PinObject = PinValue.IsValid() ? PinValue->AsObject() : nullptr;
            FN2CGraphPin Pin;
            if (!ParseGraphPinFromJson(PinObject, Pin))
            {
                FN2CLogger::Get().LogError(FString::Printf(
                    TEXT("Failed to parse pin metadata in graph '%s'."), *Name));
                return false;
            }

            const FString PinKey = Pin.NodeGuid + TEXT(":") + Pin.PinGuid;
            if (PinIndexes.Contains(PinKey))
            {
                FN2CLogger::Get().LogError(FString::Printf(
                    TEXT("Duplicate node/pin GUID pair '%s' in graph '%s'."), *PinKey, *Name));
                return false;
            }
            PinIndexes.Add(PinKey, OutGraph.Pins.Num());
            OutGraph.Pins.Add(MoveTemp(Pin));
        }

        for (const FN2CGraphPin& Pin : OutGraph.Pins)
        {
            if (!Pin.ParentPinGuid.IsEmpty())
            {
                const FString ParentKey = Pin.NodeGuid + TEXT(":") + Pin.ParentPinGuid;
                const int32* ParentIndex = PinIndexes.Find(ParentKey);
                if (!ParentIndex ||
                    OutGraph.Pins[*ParentIndex].NodeGuid != Pin.NodeGuid ||
                    !OutGraph.Pins[*ParentIndex].SubPinGuids.Contains(Pin.PinGuid))
                {
                    FN2CLogger::Get().LogError(FString::Printf(
                        TEXT("Split pin '%s' has an invalid parent relationship in graph '%s'."),
                        *Pin.PinGuid, *Name));
                    return false;
                }
            }

            for (const FString& SubPinGuid : Pin.SubPinGuids)
            {
                const FString SubPinKey = Pin.NodeGuid + TEXT(":") + SubPinGuid;
                const int32* SubPinIndex = PinIndexes.Find(SubPinKey);
                if (!SubPinIndex ||
                    OutGraph.Pins[*SubPinIndex].NodeGuid != Pin.NodeGuid ||
                    OutGraph.Pins[*SubPinIndex].ParentPinGuid != Pin.PinGuid)
                {
                    FN2CLogger::Get().LogError(FString::Printf(
                        TEXT("Pin '%s' has an invalid subpin relationship in graph '%s'."),
                        *Pin.PinGuid, *Name));
                    return false;
                }
            }

            if (!Pin.ReferencePassThroughPinGuid.IsEmpty())
            {
                const FString ReferenceKey =
                    Pin.ReferencePassThroughNodeGuid + TEXT(":") + Pin.ReferencePassThroughPinGuid;
                if (!PinIndexes.Contains(ReferenceKey))
                {
                    FN2CLogger::Get().LogError(FString::Printf(
                        TEXT("Pin '%s' has an unresolved reference pass-through in graph '%s'."),
                        *Pin.PinGuid, *Name));
                    return false;
                }
            }
        }
    }

    if (bHasPinMetadata &&
        (!JsonObject->HasField(TEXT("connections")) ||
         !JsonObject->HasField(TEXT("connection_errors")) ||
         !JsonObject->HasField(TEXT("unresolved_connection_count"))))
    {
        FN2CLogger::Get().LogError(FString::Printf(
            TEXT("Graph '%s' has pin metadata but is missing link diagnostics."), *Name));
        return false;
    }

    const TSharedPtr<FJsonObject>* FlowsObject = nullptr;
    if (!JsonObject->TryGetObjectField(TEXT("flows"), FlowsObject) ||
        !ParseFlowsFromJson(*FlowsObject, OutGraph.Flows))
    {
        FN2CLogger::Get().LogError(FString::Printf(
            TEXT("Missing or invalid flows object in graph '%s'."), *Name));
        return false;
    }

    OutGraph.Connections.Empty();
    TSet<FString> SeenLinks;
    const TArray<TSharedPtr<FJsonValue>>* DirectLinks = nullptr;
    if (JsonObject->HasField(TEXT("connections")))
    {
        if (!JsonObject->TryGetArrayField(TEXT("connections"), DirectLinks))
        {
            FN2CLogger::Get().LogError(FString::Printf(
                TEXT("Invalid connections array in graph '%s'."), *Name));
            return false;
        }

        for (const TSharedPtr<FJsonValue>& Value : *DirectLinks)
        {
            const TSharedPtr<FJsonObject> Entry = Value.IsValid() ? Value->AsObject() : nullptr;
            if (!Entry.IsValid())
            {
                return false;
            }

            FN2CDirectLink Link;
            if (!Entry->TryGetStringField(TEXT("source_node_guid"), Link.SourceNodeGuid) ||
                !Entry->TryGetStringField(TEXT("source_pin_guid"), Link.SourcePinGuid) ||
                !Entry->TryGetStringField(TEXT("target_node_guid"), Link.TargetNodeGuid) ||
                !Entry->TryGetStringField(TEXT("target_pin_guid"), Link.TargetPinGuid))
            {
                FN2CLogger::Get().LogError(FString::Printf(
                    TEXT("Connection in graph '%s' is missing endpoint GUIDs."), *Name));
                return false;
            }

            FGuid SourceNodeGuid;
            FGuid SourcePinGuid;
            FGuid TargetNodeGuid;
            FGuid TargetPinGuid;
            if (!FGuid::Parse(Link.SourceNodeGuid, SourceNodeGuid) || !SourceNodeGuid.IsValid() ||
                !FGuid::Parse(Link.SourcePinGuid, SourcePinGuid) || !SourcePinGuid.IsValid() ||
                !FGuid::Parse(Link.TargetNodeGuid, TargetNodeGuid) || !TargetNodeGuid.IsValid() ||
                !FGuid::Parse(Link.TargetPinGuid, TargetPinGuid) || !TargetPinGuid.IsValid())
            {
                FN2CLogger::Get().LogError(FString::Printf(
                    TEXT("Connection in graph '%s' contains an invalid endpoint GUID."), *Name));
                return false;
            }

            Entry->TryGetStringField(TEXT("source_node_id"), Link.SourceNodeId);
            Entry->TryGetStringField(TEXT("source_pin_id"), Link.SourcePinId);
            Entry->TryGetStringField(TEXT("target_node_id"), Link.TargetNodeId);
            Entry->TryGetStringField(TEXT("target_pin_id"), Link.TargetPinId);
            const bool bHasSourceNodeName = Entry->TryGetStringField(TEXT("source_node_name"), Link.SourceNodeName);
            const bool bHasSourceNodeClass = Entry->TryGetStringField(TEXT("source_node_class"), Link.SourceNodeClass);
            const bool bHasSourcePinName = Entry->TryGetStringField(TEXT("source_pin_name"), Link.SourcePinName);
            const bool bHasSourceDirection = Entry->TryGetStringField(TEXT("source_direction"), Link.SourceDirection);
            const bool bHasTargetNodeName = Entry->TryGetStringField(TEXT("target_node_name"), Link.TargetNodeName);
            const bool bHasTargetNodeClass = Entry->TryGetStringField(TEXT("target_node_class"), Link.TargetNodeClass);
            const bool bHasTargetPinName = Entry->TryGetStringField(TEXT("target_pin_name"), Link.TargetPinName);
            const bool bHasTargetDirection = Entry->TryGetStringField(TEXT("target_direction"), Link.TargetDirection);
            const bool bHasExecutionFlag = Entry->TryGetBoolField(TEXT("is_execution"), Link.bIsExecution);
            const bool bHasSourceHiddenFlag = Entry->TryGetBoolField(TEXT("source_hidden"), Link.bSourceHidden);
            const bool bHasTargetHiddenFlag = Entry->TryGetBoolField(TEXT("target_hidden"), Link.bTargetHidden);
            const bool bHasSourceKnotFlag = Entry->TryGetBoolField(TEXT("source_is_knot"), Link.bSourceIsKnot);
            const bool bHasTargetKnotFlag = Entry->TryGetBoolField(TEXT("target_is_knot"), Link.bTargetIsKnot);
            if (bHasPinMetadata &&
                (!bHasSourceNodeName || !bHasSourceNodeClass || !bHasSourcePinName || !bHasSourceDirection ||
                 !bHasTargetNodeName || !bHasTargetNodeClass || !bHasTargetPinName || !bHasTargetDirection ||
                 !bHasExecutionFlag || !bHasSourceHiddenFlag || !bHasTargetHiddenFlag ||
                 !bHasSourceKnotFlag || !bHasTargetKnotFlag))
            {
                FN2CLogger::Get().LogError(FString::Printf(
                    TEXT("Connection in graph '%s' is missing endpoint metadata."), *Name));
                return false;
            }

            const FString SourceKey = Link.SourceNodeGuid + TEXT(":") + Link.SourcePinGuid;
            const FString TargetKey = Link.TargetNodeGuid + TEXT(":") + Link.TargetPinGuid;
            const FString EdgeKey = SourceKey + TEXT("->") + TargetKey;
            if (SeenLinks.Contains(EdgeKey))
            {
                FN2CLogger::Get().LogError(FString::Printf(
                    TEXT("Duplicate direct connection in graph '%s': %s."), *Name, *EdgeKey));
                return false;
            }
            SeenLinks.Add(EdgeKey);

            if (bHasPinMetadata)
            {
                const int32* SourceIndex = PinIndexes.Find(SourceKey);
                const int32* TargetIndex = PinIndexes.Find(TargetKey);
                if (!SourceIndex || !TargetIndex)
                {
                    FN2CLogger::Get().LogError(FString::Printf(
                        TEXT("Connection in graph '%s' references a pin absent from the pin table: %s."),
                        *Name, *EdgeKey));
                    return false;
                }

                const FN2CGraphPin& SourcePin = OutGraph.Pins[*SourceIndex];
                const FN2CGraphPin& TargetPin = OutGraph.Pins[*TargetIndex];
                if ((!Link.SourceDirection.IsEmpty() && Link.SourceDirection != SourcePin.Direction) ||
                    (!Link.TargetDirection.IsEmpty() && Link.TargetDirection != TargetPin.Direction) ||
                    (!Link.SourcePinName.IsEmpty() && Link.SourcePinName != SourcePin.PinName) ||
                    (!Link.TargetPinName.IsEmpty() && Link.TargetPinName != TargetPin.PinName) ||
                    (!Link.SourceNodeId.IsEmpty() && Link.SourceNodeId != SourcePin.NodeId) ||
                    (!Link.TargetNodeId.IsEmpty() && Link.TargetNodeId != TargetPin.NodeId) ||
                    (!Link.SourceNodeClass.IsEmpty() && Link.SourceNodeClass != SourcePin.NodeClass) ||
                    (!Link.TargetNodeClass.IsEmpty() && Link.TargetNodeClass != TargetPin.NodeClass) ||
                    (!Link.SourceNodeName.IsEmpty() && Link.SourceNodeName != SourcePin.NodeName) ||
                    (!Link.TargetNodeName.IsEmpty() && Link.TargetNodeName != TargetPin.NodeName) ||
                    Link.bSourceIsKnot != Link.SourceNodeClass.Contains(TEXT("K2Node_Knot")) ||
                    Link.bTargetIsKnot != Link.TargetNodeClass.Contains(TEXT("K2Node_Knot")) ||
                    Link.bSourceHidden != SourcePin.bHidden ||
                    Link.bTargetHidden != TargetPin.bHidden ||
                    Link.bIsExecution != (SourcePin.bIsExecution || TargetPin.bIsExecution))
                {
                    FN2CLogger::Get().LogError(FString::Printf(
                        TEXT("Connection metadata disagrees with graph pin metadata in graph '%s': %s."),
                        *Name, *EdgeKey));
                    return false;
                }
                Link.SourceDirection = SourcePin.Direction;
                Link.TargetDirection = TargetPin.Direction;
                if (Link.SourceDirection != TEXT("Output") || Link.TargetDirection != TEXT("Input"))
                {
                    FN2CLogger::Get().LogError(FString::Printf(
                        TEXT("Connection in graph '%s' has invalid direction: %s."), *Name, *EdgeKey));
                    return false;
                }
            }
            else if ((!Link.SourceDirection.IsEmpty() && Link.SourceDirection != TEXT("Output")) ||
                (!Link.TargetDirection.IsEmpty() && Link.TargetDirection != TEXT("Input")))
            {
                FN2CLogger::Get().LogError(FString::Printf(
                    TEXT("Connection in graph '%s' has invalid direction: %s."), *Name, *EdgeKey));
                return false;
            }

            OutGraph.Connections.Add(MoveTemp(Link));
        }
    }

    OutGraph.ConnectionErrors.Empty();
    const TArray<TSharedPtr<FJsonValue>>* ErrorArray = nullptr;
    if (JsonObject->HasField(TEXT("connection_errors")))
    {
        if (!JsonObject->TryGetArrayField(TEXT("connection_errors"), ErrorArray))
        {
            return false;
        }
        for (const TSharedPtr<FJsonValue>& ErrorValue : *ErrorArray)
        {
            FString Error;
            if (!ErrorValue.IsValid() || !ErrorValue->TryGetString(Error))
            {
                return false;
            }
            OutGraph.ConnectionErrors.Add(MoveTemp(Error));
        }
    }

    OutGraph.UnresolvedConnectionCount = 0;
    if (JsonObject->HasField(TEXT("unresolved_connection_count")))
    {
        double Count = 0.0;
        if (!JsonObject->TryGetNumberField(TEXT("unresolved_connection_count"), Count) ||
            Count < 0.0 || Count > static_cast<double>(MAX_int32))
        {
            return false;
        }
        OutGraph.UnresolvedConnectionCount = static_cast<int32>(Count);
    }
    if (OutGraph.UnresolvedConnectionCount > 0 || OutGraph.ConnectionErrors.Num() > 0)
    {
        FN2CLogger::Get().LogError(FString::Printf(
            TEXT("Graph '%s' contains %d unresolved issues and %d connection errors."),
            *Name, OutGraph.UnresolvedConnectionCount, OutGraph.ConnectionErrors.Num()));
        return false;
    }
    return true;
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
        const TSharedPtr<FJsonObject> PinObject = PinValue.IsValid() ? PinValue->AsObject() : nullptr;
        FN2CPinDefinition Pin;
        if (!ParsePinFromJson(PinObject, Pin))
        {
            FN2CLogger::Get().LogError(FString::Printf(
                TEXT("Invalid input pin in node '%s'."), *OutNode.Name));
            return false;
        }
        OutNode.InputPins.Add(MoveTemp(Pin));
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
        const TSharedPtr<FJsonObject> PinObject = PinValue.IsValid() ? PinValue->AsObject() : nullptr;
        FN2CPinDefinition Pin;
        if (!ParsePinFromJson(PinObject, Pin))
        {
            FN2CLogger::Get().LogError(FString::Printf(
                TEXT("Invalid output pin in node '%s'."), *OutNode.Name));
            return false;
        }
        OutNode.OutputPins.Add(MoveTemp(Pin));
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
        !JsonObject->TryGetStringField(TEXT("name"), Name))
    {
        FN2CLogger::Get().LogError(TEXT("Missing required pin fields in JSON"));
        return false;
    }
    // Earlier NodeToCode exports omitted Exec pin types.
    if (!JsonObject->TryGetStringField(TEXT("type"), TypeString))
    {
        TypeString = TEXT("Exec");
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

    const TArray<TSharedPtr<FJsonValue>>* ExecutionArray = nullptr;
    if (!JsonObject->TryGetArrayField(TEXT("execution"), ExecutionArray))
    {
        FN2CLogger::Get().LogError(TEXT("Missing execution array in JSON"));
        return false;
    }

    OutFlows.Execution.Empty();
    for (const TSharedPtr<FJsonValue>& FlowValue : *ExecutionArray)
    {
        FString Flow;
        if (!FlowValue.IsValid() || !FlowValue->TryGetString(Flow))
        {
            FN2CLogger::Get().LogError(TEXT("Invalid execution flow entry in JSON"));
            return false;
        }
        OutFlows.Execution.Add(MoveTemp(Flow));
    }

    const TSharedPtr<FJsonObject>* DataFlowsObject = nullptr;
    if (!JsonObject->TryGetObjectField(TEXT("data"), DataFlowsObject) || !DataFlowsObject->IsValid())
    {
        FN2CLogger::Get().LogError(TEXT("Missing data flows object in JSON"));
        return false;
    }

    OutFlows.Data.Empty();
    for (const TPair<FString, TSharedPtr<FJsonValue>>& DataFlow : (*DataFlowsObject)->Values)
    {
        FString Target;
        if (!DataFlow.Value.IsValid() || !DataFlow.Value->TryGetString(Target))
        {
            FN2CLogger::Get().LogError(FString::Printf(
                TEXT("Invalid data flow target for '%s' in JSON"), *DataFlow.Key));
            return false;
        }
        OutFlows.Data.Add(DataFlow.Key, MoveTemp(Target));
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