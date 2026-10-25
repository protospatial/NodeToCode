// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CSerializer.h"
#include "Models/N2CJsonContract.h"
#include "Utils/N2CLogger.h"

namespace
{
    /** Pretty output starts one level in, as it always has (the N2C JSON goldens and saved files depend on it) */
    constexpr int32 PrettyInitialIndentLevel = 1;

    // Reader policy (card 0445, N2CJsonContract.h): unknown kinds and fields are kept, each with one warning

    /** The fields each object of the contract has; any other is kept in UnknownFields */
    const TCHAR* const TopLevelFields[] = { N2CJsonContract::VersionField, TEXT("metadata"), TEXT("graphs"), TEXT("structs"),
        TEXT("enums"), TEXT("guids") };
    const TCHAR* const MetadataFields[] = { TEXT("name"), TEXT("blueprint_type"), TEXT("blueprint_class") };
    const TCHAR* const GraphFields[] = { TEXT("name"), TEXT("graph_type"), TEXT("owner_blueprint"), TEXT("nodes"),
        N2CJsonContract::FlowsField };
    const TCHAR* const NodeFields[] = { TEXT("id"), TEXT("type"), N2CJsonContract::NodeClassField, N2CJsonContract::NodeTitleField,
        N2CJsonContract::NodeNameFieldV1, TEXT("member_parent"), TEXT("member_name"), TEXT("comment"), TEXT("pure"), TEXT("latent"),
        N2CJsonContract::UnusedField, N2CJsonContract::UnreachableField, N2CJsonContract::DisabledField, TEXT("input_pins"),
        TEXT("output_pins") };
    const TCHAR* const PinFields[] = { TEXT("id"), N2CJsonContract::PinNameField, N2CJsonContract::PinLabelField, TEXT("type"),
        TEXT("sub_type"), TEXT("default_value"), TEXT("connected"), TEXT("is_reference"), TEXT("is_const"), TEXT("is_array"),
        TEXT("is_map"), TEXT("is_set") };

    void AddReadWarning(TArray<FN2CJsonReadWarning>& Warnings, const TCHAR* Code, const FString& Path, const FString& Value,
        const FString& Message)
    {
        FN2CJsonReadWarning& Warning = Warnings.AddDefaulted_GetRef();
        Warning.Code = Code;
        Warning.Path = Path;
        Warning.Value = Value;
        Warning.Message = Message;
        FN2CLogger::Get().LogWarning(FString::Printf(TEXT("N2C JSON %s at %s: %s"), Code, *Path, *Message), TEXT("Deserialization"));
    }

    FString JoinPath(const FString& Path, const FString& Field)
    {
        return Path.IsEmpty() ? Field : Path + TEXT(".") + Field;
    }

    /** One JSON value as condensed text */
    FString JsonValueToText(const TSharedPtr<FJsonValue>& Value)
    {
        FString Text;
        const TArray<TSharedPtr<FJsonValue>> Wrapped = { Value };
        const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
            TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
        if (!FJsonSerializer::Serialize(Wrapped, Writer) || Text.Len() < 2)
        {
            return TEXT("null");
        }
        return Text.Mid(1, Text.Len() - 2); // without the wrapping [ ]
    }

    /** The JSON value of JsonValueToText's text, or null when it does not parse */
    TSharedPtr<FJsonValue> TextToJsonValue(const FString& Text)
    {
        TArray<TSharedPtr<FJsonValue>> Wrapped;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(TEXT("[") + Text + TEXT("]"));
        return FJsonSerializer::Deserialize(Reader, Wrapped) && Wrapped.Num() == 1 ? Wrapped[0] : nullptr;
    }

    /** Keeps every field of Object not in Known (case-sensitive), as text in read order, with one warning each */
    template <int32 N>
    void KeepUnknownFields(const TSharedPtr<FJsonObject>& Object, const TCHAR* const (&Known)[N], TMap<FString, FString>& OutFields,
        const FString& Path, TArray<FN2CJsonReadWarning>& Warnings)
    {
        OutFields.Reset();
        for (const auto& Field : Object->Values)
        {
            const FString Key(*Field.Key);
            bool bKnown = false;
            for (const TCHAR* Name : Known)
            {
                bKnown |= Key.Equals(Name, ESearchCase::CaseSensitive);
            }
            if (!bKnown && Field.Value.IsValid())
            {
                OutFields.Add(Key, JsonValueToText(Field.Value));
                AddReadWarning(Warnings, N2CJsonContract::ReadWarning::UnknownField, JoinPath(Path, Key), Key,
                    FString::Printf(TEXT("unknown field \"%s\" kept as it is"), *Key));
            }
        }
    }

    /** Writes the kept unknown fields after the known ones; never replaces a field the writer set */
    void WriteUnknownFields(const TSharedPtr<FJsonObject>& Object, const TMap<FString, FString>& Fields)
    {
        for (const TPair<FString, FString>& Field : Fields)
        {
            if (Object->HasField(Field.Key))
            {
                continue;
            }
            if (const TSharedPtr<FJsonValue> Value = TextToJsonValue(Field.Value))
            {
                Object->SetField(Field.Key, Value);
            }
        }
    }

    /** The enum value named Text, or TEnum::Unknown with Text kept in OutUnknownText and one warning */
    template <typename TEnum>
    TEnum ReadKind(const FString& Text, FString& OutUnknownText, const TCHAR* Code, const FString& Path, const TCHAR* What,
        TArray<FN2CJsonReadWarning>& Warnings)
    {
        OutUnknownText.Reset();
        const UEnum* Enum = StaticEnum<TEnum>();
        const int64 Value = Enum->GetValueByNameString(Text, EGetByNameFlags::None);
        if (Value != INDEX_NONE && Value != Enum->GetMaxEnumValue())
        {
            return static_cast<TEnum>(Value);
        }
        OutUnknownText = Text;
        AddReadWarning(Warnings, Code, Path, Text, FString::Printf(TEXT("unknown %s \"%s\" read as Unknown and kept"), What, *Text));
        return TEnum::Unknown;
    }

    /** The text ToJson writes for a kind: the kept text of an Unknown read from a document, else the enum name */
    template <typename TEnum>
    FString WriteKind(TEnum Value, const FString& UnknownText)
    {
        if (Value == TEnum::Unknown && !UnknownText.IsEmpty())
        {
            return UnknownText;
        }
        return StaticEnum<TEnum>()->GetNameStringByValue(static_cast<int64>(Value));
    }

    /** -1, 0 or 1 as version A is older than, the same as or newer than B, comparing dot-separated numbers */
    int32 CompareVersions(const FString& A, const FString& B)
    {
        TArray<FString> PartsA;
        TArray<FString> PartsB;
        A.ParseIntoArray(PartsA, TEXT("."), false);
        B.ParseIntoArray(PartsB, TEXT("."), false);
        for (int32 Index = 0; Index < FMath::Max(PartsA.Num(), PartsB.Num()); ++Index)
        {
            const int32 NumberA = PartsA.IsValidIndex(Index) ? FCString::Atoi(*PartsA[Index]) : 0;
            const int32 NumberB = PartsB.IsValidIndex(Index) ? FCString::Atoi(*PartsB[Index]) : 0;
            if (NumberA != NumberB)
            {
                return NumberA < NumberB ? -1 : 1;
            }
        }
        return 0;
    }
}

FString FN2CSerializer::ToJson(const FN2CBlueprint& Blueprint, EN2CJsonStyle Style)
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

    return WriteJson(JsonObject, Style);
}

FString FN2CSerializer::ToJson(const FN2CCapture& Capture, EN2CJsonStyle Style, EN2CJsonGuids Guids)
{
    if (Guids == EN2CJsonGuids::None)
    {
        return ToJson(Capture.Blueprint, Style);
    }

    if (!Capture.Blueprint.IsValid())
    {
        FN2CLogger::Get().LogWarning(TEXT("Blueprint validation failed - attempting partial serialization"));
    }
    TSharedPtr<FJsonObject> JsonObject = BlueprintToJsonObject(Capture.Blueprint);
    if (!JsonObject.IsValid())
    {
        FN2CLogger::Get().LogError(TEXT("Failed to create JSON object from Blueprint"), TEXT("Serialization"));
        return TEXT("");
    }
    JsonObject->SetObjectField(TEXT("guids"), GuidTableToJsonObject(Capture));
    return WriteJson(JsonObject, Style);
}

TSharedPtr<FJsonObject> FN2CSerializer::GuidTableToJsonObject(const FN2CCapture& Capture)
{
    TSharedPtr<FJsonObject> Table = MakeShared<FJsonObject>();

    TArray<TSharedPtr<FJsonValue>> Graphs;
    for (const FGuid& GraphGuid : Capture.GraphGuids)
    {
        Graphs.Add(MakeShared<FJsonValueString>(GraphGuid.ToString(EGuidFormats::DigitsWithHyphens)));
    }
    Table->SetArrayField(TEXT("graphs"), Graphs);

    TSharedPtr<FJsonObject> Nodes = MakeShared<FJsonObject>();
    for (const TPair<FString, FGuid>& Node : Capture.NodeGuids)
    {
        Nodes->SetStringField(Node.Key, Node.Value.ToString(EGuidFormats::DigitsWithHyphens));
    }
    Table->SetObjectField(TEXT("nodes"), Nodes);

    TSharedPtr<FJsonObject> Pins = MakeShared<FJsonObject>();
    for (const TPair<FString, FN2CCapturedPin>& Pin : Capture.Pins)
    {
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("node"), Pin.Value.NodeID);
        Entry->SetStringField(TEXT("guid"), Pin.Value.PinGuid.ToString(EGuidFormats::DigitsWithHyphens));
        Pins->SetObjectField(Pin.Key, Entry);
    }
    Table->SetObjectField(TEXT("pins"), Pins);
    return Table;
}

FString FN2CSerializer::WriteJson(const TSharedPtr<FJsonObject>& JsonObject, EN2CJsonStyle Style)
{
    FString OutputString;

    if (Style == EN2CJsonStyle::Pretty)
    {
        TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
            TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&OutputString, PrettyInitialIndentLevel);

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
    TArray<FN2CJsonReadWarning> Warnings;
    return FromJson(JsonString, OutBlueprint, Warnings);
}

bool FN2CSerializer::FromJson(const FString& JsonString, FN2CBlueprint& OutBlueprint, TArray<FN2CJsonReadWarning>& OutWarnings)
{
    OutWarnings.Reset();

    // Parse JSON string
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
    {
        FN2CLogger::Get().LogError(TEXT("Failed to parse JSON string"));
        return false;
    }

    // Convert to Blueprint
    return ParseBlueprintFromJson(JsonObject, OutBlueprint, FString(), OutWarnings);
}

TSharedPtr<FJsonObject> FN2CSerializer::BlueprintToJsonObject(const FN2CBlueprint& Blueprint)
{
    TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();

    // Add version
    JsonObject->SetStringField(N2CJsonContract::VersionField, Blueprint.Version.Value);

    // Add metadata
    TSharedPtr<FJsonObject> MetadataObject = MakeShared<FJsonObject>();
    MetadataObject->SetStringField(TEXT("name"), Blueprint.Metadata.Name);
    MetadataObject->SetStringField(TEXT("blueprint_type"),
        WriteKind(Blueprint.Metadata.BlueprintType, Blueprint.Metadata.UnknownBlueprintType));
    MetadataObject->SetStringField(TEXT("blueprint_class"), Blueprint.Metadata.BlueprintClass);
    WriteUnknownFields(MetadataObject, Blueprint.Metadata.UnknownFields);
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

    // Top-level fields of a later 2.x document, kept by FromJson (card 0445)
    WriteUnknownFields(JsonObject, Blueprint.UnknownFields);

    return JsonObject;
}

TSharedPtr<FJsonObject> FN2CSerializer::GraphToJsonObject(const FN2CGraph& Graph)
{
    TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();

    // Add basic properties
    JsonObject->SetStringField(TEXT("name"), Graph.Name);
    JsonObject->SetStringField(TEXT("graph_type"), WriteKind(Graph.GraphType, Graph.UnknownGraphType));

    // Optional: only a nested graph from another Blueprint has an owner
    if (!Graph.OwnerBlueprint.IsEmpty())
    {
        JsonObject->SetStringField(TEXT("owner_blueprint"), Graph.OwnerBlueprint);
    }

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
    JsonObject->SetObjectField(N2CJsonContract::FlowsField, FlowsToJsonObject(Graph.Flows));
    WriteUnknownFields(JsonObject, Graph.UnknownFields);

    return JsonObject;
}

TSharedPtr<FJsonObject> FN2CSerializer::NodeToJsonObject(const FN2CNodeDefinition& Node)
{
    TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();

    // Required fields
    JsonObject->SetStringField(TEXT("id"), Node.ID);
    JsonObject->SetStringField(TEXT("type"), WriteKind(Node.NodeType, Node.UnknownType));
    // Stable identity and the display hint (card 0138): class, then title only when it says more than member_name
    if (!Node.NodeClass.IsEmpty())
    {
        JsonObject->SetStringField(N2CJsonContract::NodeClassField, Node.NodeClass);
    }
    if (!Node.Title.IsEmpty())
    {
        JsonObject->SetStringField(N2CJsonContract::NodeTitleField, Node.Title);
    }
    
    // Optional fields - only add if non-empty
    if (!Node.MemberParent.IsEmpty())
    {
        JsonObject->SetStringField(TEXT("member_parent"), Node.MemberParent);
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
    // Liveness (card 0137): nodes Blueprint never runs
    if (Node.bUnused)
    {
        JsonObject->SetBoolField(N2CJsonContract::UnusedField, true);
    }
    if (Node.bUnreachable)
    {
        JsonObject->SetBoolField(N2CJsonContract::UnreachableField, true);
    }
    if (Node.bDisabled)
    {
        JsonObject->SetBoolField(N2CJsonContract::DisabledField, true);
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
    WriteUnknownFields(JsonObject, Node.UnknownFields);

    return JsonObject;
}

TSharedPtr<FJsonObject> FN2CSerializer::PinToJsonObject(const FN2CPinDefinition& Pin)
{
    TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();

    // Required fields
    JsonObject->SetStringField(TEXT("id"), Pin.ID);
    JsonObject->SetStringField(N2CJsonContract::PinNameField, Pin.Name);
    if (!Pin.Label.IsEmpty())
    {
        JsonObject->SetStringField(N2CJsonContract::PinLabelField, Pin.Label);
    }
    
    // Only add type if not Exec
    if (Pin.Type != EN2CPinType::Exec)
    {
        JsonObject->SetStringField(TEXT("type"), WriteKind(Pin.Type, Pin.UnknownType));
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
    WriteUnknownFields(JsonObject, Pin.UnknownFields);

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
    JsonObject->SetArrayField(N2CJsonContract::ExecutionField, ExecutionArray);

    // Data flows: output pin ref -> the input it feeds, or an array of them when it feeds several (so a graph without
    // fan-out gets the same object as N2C JSON v1)
    TSharedPtr<FJsonObject> DataFlowsObject = MakeShared<FJsonObject>();
    for (const TPair<FString, FN2CDataFlowTargets>& DataFlow : Flows.Data)
    {
        const TArray<FString>& Targets = DataFlow.Value.Targets;
        if (Targets.Num() == 1)
        {
            DataFlowsObject->SetStringField(DataFlow.Key, Targets[0]);
        }
        else if (Targets.Num() > 1)
        {
            TArray<TSharedPtr<FJsonValue>> TargetValues;
            for (const FString& Target : Targets)
            {
                TargetValues.Add(MakeShared<FJsonValueString>(Target));
            }
            DataFlowsObject->SetArrayField(DataFlow.Key, TargetValues);
        }
    }
    JsonObject->SetObjectField(N2CJsonContract::DataField, DataFlowsObject);

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

bool FN2CSerializer::ParseBlueprintFromJson(const TSharedPtr<FJsonObject>& JsonObject, FN2CBlueprint& OutBlueprint,
    const FString& Path, TArray<FN2CJsonReadWarning>& Warnings)
{
    if (!JsonObject.IsValid())
    {
        return false;
    }

    // Parse version: 1.x (1.2.x and earlier 2.0 builds) is read into the v2 model, which then says
    // N2CJsonContract::Version; a 2.x document keeps its own version (its unknown fields are kept, card 0445); anything
    // else is read as far as its fields are known and keeps its version
    FString Version;
    if (!JsonObject->TryGetStringField(N2CJsonContract::VersionField, Version))
    {
        FN2CLogger::Get().LogError(TEXT("Missing version field in JSON"), TEXT("Deserialization"));
        return false;
    }

    if (N2CJsonContract::IsReadable(Version))
    {
        if (N2CJsonContract::ParseMajor(Version) < N2CJsonContract::Major)
        {
            FN2CLogger::Get().Log(FString::Printf(TEXT("Reading N2C JSON %s into the %s model"), *Version,
                N2CJsonContract::Version), EN2CLogSeverity::Info, TEXT("Deserialization"));
            OutBlueprint.Version.Value = N2CJsonContract::Version;
        }
        else
        {
            if (CompareVersions(Version, N2CJsonContract::Version) > 0)
            {
                AddReadWarning(Warnings, N2CJsonContract::ReadWarning::NewerVersion, JoinPath(Path, N2CJsonContract::VersionField),
                    Version, FString::Printf(TEXT("N2C JSON %s is newer than this build's %s; what this build does not know is kept"),
                        *Version, N2CJsonContract::Version));
            }
            OutBlueprint.Version.Value = Version;
        }
    }
    else
    {
        FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Unexpected N2C JSON version '%s' (this reader reads %d.x to %d.x); "
            "reading the fields it knows"), *Version, N2CJsonContract::OldestReadableMajor, N2CJsonContract::Major));
        OutBlueprint.Version.Value = Version;
    }

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

    // An unknown blueprint_type is kept as Unknown with its text (card 0445)
    const FString MetadataPath = JoinPath(Path, TEXT("metadata"));
    OutBlueprint.Metadata.BlueprintType = ReadKind<EN2CBlueprintType>(TypeString, OutBlueprint.Metadata.UnknownBlueprintType,
        N2CJsonContract::ReadWarning::UnknownBlueprintType, JoinPath(MetadataPath, TEXT("blueprint_type")), TEXT("blueprint_type"), Warnings);
    KeepUnknownFields(*MetadataObject, MetadataFields, OutBlueprint.Metadata.UnknownFields, MetadataPath, Warnings);

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
    for (int32 GraphIndex = 0; GraphIndex < GraphsArray->Num(); ++GraphIndex)
    {
        const FString GraphPath = FString::Printf(TEXT("%s[%d]"), *JoinPath(Path, TEXT("graphs")), GraphIndex);
        const TSharedPtr<FJsonValue>& GraphValue = (*GraphsArray)[GraphIndex];
        const TSharedPtr<FJsonObject>* GraphObject = nullptr;
        FN2CGraph Graph;
        if (GraphValue.IsValid() && GraphValue->TryGetObject(GraphObject) && ParseGraphFromJson(*GraphObject, Graph, GraphPath, Warnings))
        {
            OutBlueprint.Graphs.Add(MoveTemp(Graph));
            ValidGraphCount++;
        }
        else
        {
            AddReadWarning(Warnings, N2CJsonContract::ReadWarning::SkippedGraph, GraphPath, FString(),
                TEXT("graph skipped: it is not an object or misses a required field"));
            bHasPartialData = true;
        }
    }

    // Structs and enums are optional
    OutBlueprint.Structs.Empty();
    const TArray<TSharedPtr<FJsonValue>>* StructsArray = nullptr;
    if (JsonObject->TryGetArrayField(TEXT("structs"), StructsArray))
    {
        for (const TSharedPtr<FJsonValue>& StructValue : *StructsArray)
        {
            const TSharedPtr<FJsonObject>* StructObject = nullptr;
            FN2CStruct Struct;
            if (StructValue.IsValid() && StructValue->TryGetObject(StructObject) && ParseStructFromJson(*StructObject, Struct))
            {
                OutBlueprint.Structs.Add(MoveTemp(Struct));
            }
            else
            {
                bHasPartialData = true;
            }
        }
    }

    OutBlueprint.Enums.Empty();
    const TArray<TSharedPtr<FJsonValue>>* EnumsArray = nullptr;
    if (JsonObject->TryGetArrayField(TEXT("enums"), EnumsArray))
    {
        for (const TSharedPtr<FJsonValue>& EnumValue : *EnumsArray)
        {
            const TSharedPtr<FJsonObject>* EnumObject = nullptr;
            FN2CEnum Enum;
            if (EnumValue.IsValid() && EnumValue->TryGetObject(EnumObject) && ParseEnumFromJson(*EnumObject, Enum))
            {
                OutBlueprint.Enums.Add(MoveTemp(Enum));
            }
            else
            {
                bHasPartialData = true;
            }
        }
    }

    // Top-level fields of a later 2.x document (card 0445)
    KeepUnknownFields(JsonObject, TopLevelFields, OutBlueprint.UnknownFields, Path, Warnings);

    if (bHasPartialData)
    {
        FN2CLogger::Get().LogWarning(TEXT("Some graphs, structs or enums in the N2C JSON could not be read"));
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

bool FN2CSerializer::ParseGraphFromJson(const TSharedPtr<FJsonObject>& JsonObject, FN2CGraph& OutGraph, const FString& Path,
    TArray<FN2CJsonReadWarning>& Warnings)
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

    // An unknown graph_type is kept as Unknown with its text, and the graph with it (card 0445)
    OutGraph.GraphType = ReadKind<EN2CGraphType>(TypeString, OutGraph.UnknownGraphType, N2CJsonContract::ReadWarning::UnknownGraphType,
        JoinPath(Path, TEXT("graph_type")), TEXT("graph_type"), Warnings);

    // Optional
    OutGraph.OwnerBlueprint.Reset();
    JsonObject->TryGetStringField(TEXT("owner_blueprint"), OutGraph.OwnerBlueprint);

    // Parse nodes array
    const TArray<TSharedPtr<FJsonValue>>* NodesArray;
    if (!JsonObject->TryGetArrayField(TEXT("nodes"), NodesArray))
    {
        FN2CLogger::Get().LogError(TEXT("Missing nodes array in JSON"));
        return false;
    }

    OutGraph.Nodes.Empty();
    for (int32 NodeIndex = 0; NodeIndex < NodesArray->Num(); ++NodeIndex)
    {
        const FString NodePath = FString::Printf(TEXT("%s[%d]"), *JoinPath(Path, TEXT("nodes")), NodeIndex);
        const TSharedPtr<FJsonValue>& NodeValue = (*NodesArray)[NodeIndex];
        const TSharedPtr<FJsonObject>* NodeObject = nullptr;
        FN2CNodeDefinition Node;
        if (NodeValue.IsValid() && NodeValue->TryGetObject(NodeObject) && ParseNodeFromJson(*NodeObject, Node, NodePath, Warnings))
        {
            OutGraph.Nodes.Add(MoveTemp(Node));
        }
        else
        {
            AddReadWarning(Warnings, N2CJsonContract::ReadWarning::SkippedNode, NodePath, FString(),
                TEXT("node skipped: it is not an object or misses a required field; flows naming it now dangle"));
        }
    }

    // Parse flows
    const TSharedPtr<FJsonObject>* FlowsObject;
    if (!JsonObject->TryGetObjectField(N2CJsonContract::FlowsField, FlowsObject))
    {
        FN2CLogger::Get().LogError(TEXT("Missing flows object in JSON"));
        return false;
    }

    if (!ParseFlowsFromJson(*FlowsObject, OutGraph.Flows))
    {
        return false;
    }

    KeepUnknownFields(JsonObject, GraphFields, OutGraph.UnknownFields, Path, Warnings);
    return true;
}

bool FN2CSerializer::ParseNodeFromJson(const TSharedPtr<FJsonObject>& JsonObject, FN2CNodeDefinition& OutNode, const FString& Path,
    TArray<FN2CJsonReadWarning>& Warnings)
{
    if (!JsonObject.IsValid())
    {
        return false;
    }

    // Parse basic properties
    FString ID, TypeString, MemberParent, MemberName, Comment;
    if (!JsonObject->TryGetStringField(TEXT("id"), ID) ||
        !JsonObject->TryGetStringField(TEXT("type"), TypeString))
    {
        FN2CLogger::Get().LogError(TEXT("Missing required node fields in JSON"));
        return false;
    }

    OutNode.ID = ID;

    // The title hint: "title" since card 0138, "name" in 1.x documents (display text either way); "class" is 2.x only
    OutNode.NodeClass.Reset();
    OutNode.Title.Reset();
    JsonObject->TryGetStringField(N2CJsonContract::NodeClassField, OutNode.NodeClass);
    if (!JsonObject->TryGetStringField(N2CJsonContract::NodeTitleField, OutNode.Title))
    {
        JsonObject->TryGetStringField(N2CJsonContract::NodeNameFieldV1, OutNode.Title);
    }

    // Optional fields
    JsonObject->TryGetStringField(TEXT("member_parent"), MemberParent);
    JsonObject->TryGetStringField(TEXT("member_name"), MemberName);
    JsonObject->TryGetStringField(TEXT("comment"), Comment);

    OutNode.MemberParent = MemberParent;
    OutNode.MemberName = MemberName;
    OutNode.Comment = Comment;

    // An unknown node type is kept as Unknown with its text, and the node with its pins (card 0445)
    OutNode.NodeType = ReadKind<EN2CNodeType>(TypeString, OutNode.UnknownType, N2CJsonContract::ReadWarning::UnknownNodeType,
        JoinPath(Path, TEXT("type")), TEXT("node type"), Warnings);

    // Parse flags
    bool bPure = false;
    bool bLatent = false;
    JsonObject->TryGetBoolField(TEXT("pure"), bPure);
    JsonObject->TryGetBoolField(TEXT("latent"), bLatent);
    OutNode.bPure = bPure;
    OutNode.bLatent = bLatent;

    bool bUnused = false;
    bool bUnreachable = false;
    bool bDisabled = false;
    JsonObject->TryGetBoolField(N2CJsonContract::UnusedField, bUnused);
    JsonObject->TryGetBoolField(N2CJsonContract::UnreachableField, bUnreachable);
    JsonObject->TryGetBoolField(N2CJsonContract::DisabledField, bDisabled);
    OutNode.bUnused = bUnused;
    OutNode.bUnreachable = bUnreachable;
    OutNode.bDisabled = bDisabled;

    // Parse the input and output pins arrays
    for (const TCHAR* PinsField : { TEXT("input_pins"), TEXT("output_pins") })
    {
        const bool bInput = FCString::Strcmp(PinsField, TEXT("input_pins")) == 0;
        const TArray<TSharedPtr<FJsonValue>>* PinsArray;
        if (!JsonObject->TryGetArrayField(PinsField, PinsArray))
        {
            FN2CLogger::Get().LogError(FString::Printf(TEXT("Missing %s array in JSON"), PinsField));
            return false;
        }

        TArray<FN2CPinDefinition>& Pins = bInput ? OutNode.InputPins : OutNode.OutputPins;
        Pins.Empty();
        for (int32 PinIndex = 0; PinIndex < PinsArray->Num(); ++PinIndex)
        {
            const FString PinPath = FString::Printf(TEXT("%s[%d]"), *JoinPath(Path, PinsField), PinIndex);
            const TSharedPtr<FJsonValue>& PinValue = (*PinsArray)[PinIndex];
            const TSharedPtr<FJsonObject>* PinObject = nullptr;
            FN2CPinDefinition Pin;
            if (PinValue.IsValid() && PinValue->TryGetObject(PinObject) && ParsePinFromJson(*PinObject, Pin, PinPath, Warnings))
            {
                Pins.Add(MoveTemp(Pin));
            }
            else
            {
                AddReadWarning(Warnings, N2CJsonContract::ReadWarning::SkippedPin, PinPath, FString(),
                    TEXT("pin skipped: it is not an object or misses a required field"));
            }
        }
    }

    KeepUnknownFields(JsonObject, NodeFields, OutNode.UnknownFields, Path, Warnings);
    return true;
}

bool FN2CSerializer::ParsePinFromJson(const TSharedPtr<FJsonObject>& JsonObject, FN2CPinDefinition& OutPin, const FString& Path,
    TArray<FN2CJsonReadWarning>& Warnings)
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

    OutPin.ID = ID;
    OutPin.Name = Name;
    OutPin.Label.Reset();
    JsonObject->TryGetStringField(N2CJsonContract::PinLabelField, OutPin.Label);

    // ToJson leaves "type" out for exec pins, so a pin without one is Exec; an unknown type is kept as Unknown with its
    // text, and the pin with it (card 0445)
    OutPin.UnknownType.Reset();
    if (!JsonObject->TryGetStringField(TEXT("type"), TypeString))
    {
        OutPin.Type = EN2CPinType::Exec;
    }
    else
    {
        OutPin.Type = ReadKind<EN2CPinType>(TypeString, OutPin.UnknownType, N2CJsonContract::ReadWarning::UnknownPinType,
            JoinPath(Path, TEXT("type")), TEXT("pin type"), Warnings);
    }

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

    KeepUnknownFields(JsonObject, PinFields, OutPin.UnknownFields, Path, Warnings);
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
    if (!JsonObject->TryGetArrayField(N2CJsonContract::ExecutionField, ExecutionArray))
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

    // Parse data flows object: output pin ref -> one input pin ref (v1 and v2) or an array of them (v2 fan-out)
    const TSharedPtr<FJsonObject>* DataFlowsObject;
    if (!JsonObject->TryGetObjectField(N2CJsonContract::DataField, DataFlowsObject))
    {
        FN2CLogger::Get().LogError(TEXT("Missing data flows object in JSON"));
        return false;
    }

    OutFlows.Data.Empty();
    for (const auto& DataFlow : (*DataFlowsObject)->Values)
    {
        if (!DataFlow.Value.IsValid())
        {
            continue;
        }
        if (DataFlow.Value->Type == EJson::String)
        {
            OutFlows.Data.FindOrAdd(FString(*DataFlow.Key)).Targets.AddUnique(DataFlow.Value->AsString());
        }
        else if (DataFlow.Value->Type == EJson::Array)
        {
            FN2CDataFlowTargets& Targets = OutFlows.Data.FindOrAdd(FString(*DataFlow.Key));
            for (const TSharedPtr<FJsonValue>& Target : DataFlow.Value->AsArray())
            {
                if (Target.IsValid() && Target->Type == EJson::String)
                {
                    Targets.Targets.AddUnique(Target->AsString());
                }
            }
        }
        else
        {
            FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Skipping data flow %s: its value is not a pin ref or an array of them"),
                *DataFlow.Key));
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
        
        // Parse value properties: "name" (ToJson writes no numeric "value", and none is needed)
        FString ValueName;
        if (!ValueObject->TryGetStringField(TEXT("name"), ValueName))
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
