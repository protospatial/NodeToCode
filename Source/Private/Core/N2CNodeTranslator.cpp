// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Models/N2CLogging.h"
#include "Core/N2CNodeTranslator.h"

#include "Core/N2CSettings.h"
#include "EdGraphNode_Comment.h"
#include "Utils/N2CLogger.h"
#include "Utils/N2CNodeTypeRegistry.h"
#include "Utils/Validators/N2CBlueprintValidator.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectBase.h"
#include "UObject/Class.h"
#include "UObject/UnrealTypePrivate.h"
#include "UObject/MetaData.h"
#include "StructUtils/UserDefinedStruct.h"
#include "RigVMEditorAsset.h"
#include "RigVMModel/RigVMGraph.h"
#include "RigVMModel/RigVMNode.h"
#include "RigVMModel/RigVMPin.h"
#include "RigVMModel/RigVMLink.h"
#include "RigVMModel/Nodes/RigVMCommentNode.h"
#include "RigVMModel/Nodes/RigVMFunctionEntryNode.h"
#include "RigVMModel/Nodes/RigVMFunctionReturnNode.h"
#include "RigVMModel/Nodes/RigVMUnitNode.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_Knot.h"
#include "ControlRigEditorAsset.h"
#include "Rigs/RigHierarchy.h"
#include "Rigs/RigHierarchyElements.h"
#include "Rigs/RigHierarchyMetadata.h"
#include "Blueprint/WidgetTree.h"
#include "Animation/WidgetAnimation.h"
#include "WidgetBlueprint.h"
#include "Components/PanelWidget.h"
#include "Components/PanelSlot.h"
#include "Misc/Base64.h"
#include "Misc/EngineVersion.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/CustomVersion.h"

namespace
{
TMap<FString, FString> ExportRigVMNativeProperties(const UObject* Object)
{
    N2C_LOG_FUNCTION_ENTRY();
    TMap<FString, FString> Properties;
    if (!Object)
    {
        return Properties;
    }

    constexpr EPropertyFlags ExcludedFlags = CPF_Transient | CPF_DuplicateTransient |
        CPF_NonPIEDuplicateTransient | CPF_Deprecated | CPF_EditorOnly;
    for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
    {
        FProperty* Property = *It;
        const FName PropertyName = Property->GetFName();
        if (Property->HasAnyPropertyFlags(ExcludedFlags) ||
            PropertyName == TEXT("Graph") || PropertyName == TEXT("Nodes") ||
            PropertyName == TEXT("Pins") || PropertyName == TEXT("Links") ||
            PropertyName == TEXT("ParentPin") || PropertyName == TEXT("SubPins"))
        {
            continue;
        }

        FString Value;
        Property->ExportText_Direct(
            Value,
            Property->ContainerPtrToValuePtr<void>(Object),
            nullptr,
            const_cast<UObject*>(Object),
            PPF_None);
        Properties.Add(Property->GetName(), MoveTemp(Value));
    }
    return Properties;
}

FString RigVMPinDirectionToString(const ERigVMPinDirection Direction)
{
    N2C_LOG_FUNCTION_ENTRY();
    switch (Direction)
    {
        case ERigVMPinDirection::Input: return TEXT("Input");
        case ERigVMPinDirection::Output: return TEXT("Output");
        case ERigVMPinDirection::IO: return TEXT("IO");
        default: return TEXT("Invalid");
    }
}

EN2CPinType DetermineRigVMPinType(const URigVMPin* Pin)
{
    N2C_LOG_FUNCTION_ENTRY();
    if (!Pin)
    {
        return EN2CPinType::Custom;
    }
    if (Pin->IsExecuteContext())
    {
        return EN2CPinType::Exec;
    }
    if (Pin->IsArray())
    {
        return EN2CPinType::Array;
    }
    if (Pin->IsEnum())
    {
        return EN2CPinType::Enum;
    }
    if (Pin->IsStruct())
    {
        return EN2CPinType::Struct;
    }
    if (Pin->IsUObject())
    {
        return EN2CPinType::Object;
    }

    const FString Type = Pin->GetCPPType();
    if (Type == TEXT("bool")) return EN2CPinType::Boolean;
    if (Type == TEXT("int32") || Type == TEXT("int")) return EN2CPinType::Integer;
    if (Type == TEXT("int64")) return EN2CPinType::Integer64;
    if (Type == TEXT("float")) return EN2CPinType::Float;
    if (Type == TEXT("double")) return EN2CPinType::Double;
    if (Type == TEXT("FString")) return EN2CPinType::String;
    if (Type == TEXT("FName")) return EN2CPinType::Name;
    if (Type == TEXT("FText")) return EN2CPinType::Text;
    if (Type == TEXT("FVector")) return EN2CPinType::Vector;
    if (Type == TEXT("FVector2D")) return EN2CPinType::Vector2D;
    if (Type == TEXT("FQuat")) return EN2CPinType::Quat;
    if (Type == TEXT("FRotator")) return EN2CPinType::Rotator;
    if (Type == TEXT("FTransform")) return EN2CPinType::Transform;
    return EN2CPinType::Custom;
}

EN2CStructMemberType RigVMVariableType(const FString& CPPType)
{
    N2C_LOG_FUNCTION_ENTRY();
    if (CPPType == TEXT("bool")) return EN2CStructMemberType::Bool;
    if (CPPType == TEXT("int32") || CPPType == TEXT("int")) return EN2CStructMemberType::Int;
    if (CPPType == TEXT("float") || CPPType == TEXT("double")) return EN2CStructMemberType::Float;
    if (CPPType == TEXT("FName")) return EN2CStructMemberType::Name;
    if (CPPType == TEXT("FString")) return EN2CStructMemberType::String;
    if (CPPType == TEXT("FText")) return EN2CStructMemberType::Text;
    if (CPPType.StartsWith(TEXT("TArray<"))) return EN2CStructMemberType::Array;
    if (CPPType.StartsWith(TEXT("TSet<"))) return EN2CStructMemberType::Set;
    if (CPPType.StartsWith(TEXT("TMap<"))) return EN2CStructMemberType::Map;
    return EN2CStructMemberType::Custom;
}

FN2CVariable RigVMVariableToNodeToCode(const FRigVMGraphVariableDescription& Description)
{
    N2C_LOG_FUNCTION_ENTRY();
    FN2CVariable Variable;
    Variable.Name = Description.Name.ToString();
    Variable.Type = RigVMVariableType(Description.CPPType);
    Variable.TypeName = Description.CPPType;
    Variable.DefaultValue = Description.DefaultValue;
    Variable.Comment = Description.Tooltip.ToString();
    Variable.bIsArray = Description.CPPType.StartsWith(TEXT("TArray<"));
    Variable.bIsSet = Description.CPPType.StartsWith(TEXT("TSet<"));
    Variable.bIsMap = Description.CPPType.StartsWith(TEXT("TMap<"));
    Variable.NativeProperties.Add(TEXT("guid"), Description.Guid.ToString());
    Variable.NativeProperties.Add(TEXT("cpp_type_object_path"), Description.CPPTypeObject
        ? Description.CPPTypeObject->GetPathName() : Description.CPPTypeObjectPath.ToString());
    Variable.NativeProperties.Add(TEXT("category"), Description.Category.ToString());
    Variable.NativeProperties.Add(TEXT("tooltip"), Description.Tooltip.ToString());
    Variable.NativeProperties.Add(TEXT("public"), Description.bPublic ? TEXT("true") : TEXT("false"));
    Variable.NativeProperties.Add(TEXT("private"), Description.bPrivate ? TEXT("true") : TEXT("false"));
    Variable.NativeProperties.Add(TEXT("exposed_on_spawn"), Description.bExposedOnSpawn ? TEXT("true") : TEXT("false"));
    Variable.NativeProperties.Add(TEXT("expose_to_cinematics"), Description.bExposeToCinematics ? TEXT("true") : TEXT("false"));
    return Variable;
}

TMap<FString, FString> ExportNativeProperties(const UObject* Object)
{
    N2C_LOG_FUNCTION_ENTRY();
    TMap<FString, FString> Properties;
    if (!Object)
    {
        return Properties;
    }

    constexpr EPropertyFlags ExcludedFlags = CPF_Transient | CPF_DuplicateTransient |
        CPF_NonPIEDuplicateTransient | CPF_Deprecated;
    for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
    {
        FProperty* Property = *It;
        if (!Property || Property->HasAnyPropertyFlags(ExcludedFlags))
        {
            continue;
        }

        FString Value;
        Property->ExportText_Direct(
            Value,
            Property->ContainerPtrToValuePtr<void>(Object),
            nullptr,
            const_cast<UObject*>(Object),
            PPF_None);
        Properties.Add(Property->GetName(), MoveTemp(Value));
    }
    return Properties;
}

TMap<FString, FString> ExportNativeProperties(const UStruct* Struct, const void* ValueContainer)
{
    N2C_LOG_FUNCTION_ENTRY();
    TMap<FString, FString> Properties;
    if (!Struct || !ValueContainer)
    {
        return Properties;
    }

    constexpr EPropertyFlags ExcludedFlags = CPF_Transient | CPF_DuplicateTransient |
        CPF_NonPIEDuplicateTransient | CPF_Deprecated;
    for (TFieldIterator<FProperty> It(Struct); It; ++It)
    {
        FProperty* Property = *It;
        if (!Property || Property->HasAnyPropertyFlags(ExcludedFlags))
        {
            continue;
        }

        FString Value;
        Property->ExportText_Direct(
            Value,
            Property->ContainerPtrToValuePtr<void>(ValueContainer),
            nullptr,
            nullptr,
            PPF_None);
        Properties.Add(Property->GetName(), MoveTemp(Value));
    }
    return Properties;
}

FString RigElementKeyToExportPath(const FRigElementKey& Key)
{
    N2C_LOG_FUNCTION_ENTRY();
    if (!Key.IsValid())
    {
        return FString();
    }
    const FString TypeName = StaticEnum<ERigElementType>()->GetNameStringByValue(static_cast<int64>(Key.Type));
    return TypeName + TEXT(":") + Key.Name.ToString();
}

FString ExportStructValue(const UScriptStruct* Struct, const void* Value)
{
    N2C_LOG_FUNCTION_ENTRY();
    if (!Struct || !Value)
    {
        return FString();
    }
    FString Text;
    Struct->ExportText(Text, Value, nullptr, nullptr, PPF_None, nullptr);
    return Text;
}

void AddObjectMetadata(TMap<FString, FString>& OutProperties, const UObject* Object)
{
    N2C_LOG_FUNCTION_ENTRY();
    if (const TMap<FName, FString>* Metadata = FMetaData::GetMapForObject(Object))
    {
        for (const TPair<FName, FString>& Entry : *Metadata)
        {
            OutProperties.Add(TEXT("metadata.") + Entry.Key.ToString(), Entry.Value);
        }
    }
}

const TCHAR* EnumUnderlyingTypeName(const UEnum::EUnderlyingType Type)
{
    N2C_LOG_FUNCTION_ENTRY();
    switch (Type)
    {
        case UEnum::EUnderlyingType::int8: return TEXT("int8");
        case UEnum::EUnderlyingType::int16: return TEXT("int16");
        case UEnum::EUnderlyingType::int32: return TEXT("int32");
        case UEnum::EUnderlyingType::int64: return TEXT("int64");
        case UEnum::EUnderlyingType::uint8: return TEXT("uint8");
        case UEnum::EUnderlyingType::uint16: return TEXT("uint16");
        case UEnum::EUnderlyingType::uint32: return TEXT("uint32");
        case UEnum::EUnderlyingType::uint64: return TEXT("uint64");
        default: return TEXT("unknown");
    }
}

void AddControlRigHierarchyExport(FN2CBlueprint& Blueprint, IControlRigEditorAssetInterface* RigAsset)
{
    N2C_LOG_FUNCTION_ENTRY();
    if (!RigAsset)
    {
        return;
    }

    URigHierarchy* Hierarchy = RigAsset->GetHierarchy();
    if (!Hierarchy)
    {
        return;
    }

    FN2CNativeObject Settings;
    Settings.Kind = TEXT("control_rig_hierarchy_settings");
    Settings.Name = TEXT("HierarchySettings");
    Settings.ObjectPath = Blueprint.Metadata.Name + TEXT("#HierarchySettings");
    Settings.NativeClass = FRigHierarchySettings::StaticStruct()->GetPathName();
    Settings.NativeProperties = ExportNativeProperties(
        FRigHierarchySettings::StaticStruct(), &RigAsset->GetHierarchySettings());
    Blueprint.NativeObjects.Add(MoveTemp(Settings));

    const TArray<FRigElementKey> Keys = Hierarchy->GetAllKeys(false);
    for (int32 Order = 0; Order < Keys.Num(); ++Order)
    {
        const FRigElementKey& Key = Keys[Order];
        const int32 ElementIndex = Hierarchy->GetIndex(Key);
        const FRigBaseElement* Element = Hierarchy->Get(ElementIndex);
        if (!Element)
        {
            continue;
        }

        const FString KeyPath = RigElementKeyToExportPath(Key);
        const FRigElementKey ParentKey = Hierarchy->GetFirstParent(Key);

        FN2CNativeObject ElementObject;
        ElementObject.Kind = TEXT("control_rig_hierarchy_element");
        ElementObject.Name = Key.Name.ToString();
        ElementObject.ObjectPath = Blueprint.Metadata.Name + TEXT("#Hierarchy/") + KeyPath;
        ElementObject.NativeClass = Element->GetScriptStruct()->GetPathName();
        ElementObject.ParentPath = RigElementKeyToExportPath(ParentKey);
        ElementObject.Order = Order;
        ElementObject.NativeProperties = ExportNativeProperties(Element->GetScriptStruct(), Element);
        ElementObject.NativeProperties.Add(TEXT("key_type"),
            StaticEnum<ERigElementType>()->GetNameStringByValue(static_cast<int64>(Key.Type)));
        ElementObject.NativeProperties.Add(TEXT("key_name"), Key.Name.ToString());
        ElementObject.NativeProperties.Add(TEXT("local_index"), FString::FromInt(Hierarchy->GetLocalIndex(Key)));
        ElementObject.NativeProperties.Add(TEXT("is_procedural"), Hierarchy->IsProcedural(Key) ? TEXT("true") : TEXT("false"));

        if (Element->GetNumTransforms() > 0)
        {
            const FTransform InitialLocal = Hierarchy->GetInitialLocalTransform(ElementIndex);
            const FTransform InitialGlobal = Hierarchy->GetInitialGlobalTransform(ElementIndex);
            ElementObject.NativeProperties.Add(TEXT("initial_local_transform"),
                ExportStructValue(TBaseStructure<FTransform>::Get(), &InitialLocal));
            ElementObject.NativeProperties.Add(TEXT("initial_global_transform"),
                ExportStructValue(TBaseStructure<FTransform>::Get(), &InitialGlobal));
        }

        if (Key.Type == ERigElementType::Control)
        {
            const FRigControlValue InitialValue = Hierarchy->GetInitialControlValue(ElementIndex);
            if (const FRigControlElement* Control = Hierarchy->Find<FRigControlElement>(Key))
            {
                ElementObject.NativeProperties.Append(
                    ExportNativeProperties(FRigControlSettings::StaticStruct(), &Control->Settings));
            }
            ElementObject.NativeProperties.Append(
                ExportNativeProperties(FRigControlValue::StaticStruct(), &InitialValue));
        }

        for (const FName& MetadataName : Hierarchy->GetMetadataNames(Key))
        {
            const ERigMetadataType MetadataType = Hierarchy->GetMetadataType(Key, MetadataName);
            const FRigBaseMetadata* Metadata = Element->GetMetadata(MetadataName, MetadataType);
            if (!Metadata)
            {
                continue;
            }

            FN2CNativeObject MetadataObject;
            MetadataObject.Kind = TEXT("control_rig_hierarchy_metadata");
            MetadataObject.Name = MetadataName.ToString();
            MetadataObject.ObjectPath = ElementObject.ObjectPath + TEXT("#Metadata/") + MetadataObject.Name;
            MetadataObject.NativeClass = Metadata->GetMetadataStruct()->GetPathName();
            MetadataObject.ParentPath = ElementObject.ObjectPath;
            MetadataObject.NativeProperties = ExportNativeProperties(
                Metadata->GetMetadataStruct(), Metadata);
            Blueprint.NativeObjects.Add(MoveTemp(MetadataObject));
        }

        Blueprint.NativeObjects.Add(MoveTemp(ElementObject));

        for (const FRigComponentKey& ComponentKey : Element->GetComponentKeys())
        {
            const FRigBaseComponent* Component = Hierarchy->FindComponent(ComponentKey);
            if (!Component)
            {
                continue;
            }

            FN2CNativeObject ComponentObject;
            ComponentObject.Kind = TEXT("control_rig_hierarchy_component");
            ComponentObject.Name = ComponentKey.Name.ToString();
            ComponentObject.ObjectPath = Blueprint.Metadata.Name + TEXT("#Hierarchy/") + KeyPath +
                TEXT("/Components/") + ComponentObject.Name;
            ComponentObject.NativeClass = Component->GetScriptStruct()->GetPathName();
            ComponentObject.ParentPath = Blueprint.Metadata.Name + TEXT("#Hierarchy/") + KeyPath;
            ComponentObject.NativeProperties = ExportNativeProperties(Component->GetScriptStruct(), Component);
            Blueprint.NativeObjects.Add(MoveTemp(ComponentObject));
        }
    }

    TArray<uint8> SerializedHierarchy;
    FMemoryWriter Writer(SerializedHierarchy, true);
    Hierarchy->Save(Writer);
    if (!Writer.IsError())
    {
        FN2CNativeObject Snapshot;
        Snapshot.Kind = TEXT("control_rig_hierarchy_snapshot");
        Snapshot.Name = TEXT("SerializedHierarchy");
        Snapshot.ObjectPath = Blueprint.Metadata.Name + TEXT("#HierarchySnapshot");
        Snapshot.NativeClass = Hierarchy->GetClass()->GetPathName();
        Snapshot.NativeProperties.Add(TEXT("serialization_api"), TEXT("URigHierarchy::Save/Load"));
        Snapshot.NativeProperties.Add(TEXT("engine_version"), FEngineVersion::Current().ToString());
        Snapshot.NativeProperties.Add(TEXT("byte_count"), FString::FromInt(SerializedHierarchy.Num()));
        for (const FCustomVersion& Version : Writer.GetCustomVersions().GetAllVersions())
        {
            Snapshot.NativeProperties.Add(
                TEXT("custom_version.") + Version.Key.ToString(), FString::FromInt(Version.Version));
        }
        Snapshot.NativeProperties.Add(TEXT("data_base64"), FBase64::Encode(SerializedHierarchy));
        Blueprint.NativeObjects.Add(MoveTemp(Snapshot));
    }
}

void AddWidgetTreeExport(FN2CBlueprint& Blueprint, UWidgetBlueprint* WidgetBlueprint)
{
    N2C_LOG_FUNCTION_ENTRY();
    if (!WidgetBlueprint)
    {
        return;
    }

    FN2CNativeObject BlueprintData;
    BlueprintData.Kind = TEXT("widget_blueprint_properties");
    BlueprintData.Name = WidgetBlueprint->GetName();
    BlueprintData.ObjectPath = WidgetBlueprint->GetPathName();
    BlueprintData.NativeClass = WidgetBlueprint->GetClass()->GetPathName();
    BlueprintData.NativeProperties = ExportNativeProperties(WidgetBlueprint);
    Blueprint.NativeObjects.Add(MoveTemp(BlueprintData));

    for (int32 AnimationIndex = 0; AnimationIndex < WidgetBlueprint->Animations.Num(); ++AnimationIndex)
    {
        UWidgetAnimation* Animation = WidgetBlueprint->Animations[AnimationIndex];
        if (!Animation)
        {
            continue;
        }

        FN2CNativeObject AnimationObject;
        AnimationObject.Kind = TEXT("widget_animation");
        AnimationObject.Name = Animation->GetName();
        AnimationObject.ObjectPath = Animation->GetPathName();
        AnimationObject.NativeClass = Animation->GetClass()->GetPathName();
        AnimationObject.ParentPath = WidgetBlueprint->GetPathName();
        AnimationObject.Order = AnimationIndex;
        AnimationObject.NativeProperties = ExportNativeProperties(Animation);
        Blueprint.NativeObjects.Add(MoveTemp(AnimationObject));

        TArray<UObject*> AnimationSubobjects;
        GetObjectsWithOuter(Animation, AnimationSubobjects, EGetObjectsFlags::IncludeNestedObjects);
        for (UObject* Subobject : AnimationSubobjects)
        {
            if (!Subobject)
            {
                continue;
            }
            const FString ClassPath = Subobject->GetClass()->GetPathName();
            if (!ClassPath.Contains(TEXT("MovieScene")))
            {
                continue;
            }

            FN2CNativeObject AnimationData;
            AnimationData.Kind = TEXT("widget_animation_data");
            AnimationData.Name = Subobject->GetName();
            AnimationData.ObjectPath = Subobject->GetPathName();
            AnimationData.NativeClass = ClassPath;
            AnimationData.ParentPath = Subobject->GetOuter() ? Subobject->GetOuter()->GetPathName() : FString();
            AnimationData.NativeProperties = ExportNativeProperties(Subobject);
            Blueprint.NativeObjects.Add(MoveTemp(AnimationData));
        }
    }

    const TArray<UWidget*> Widgets = WidgetBlueprint->GetAllSourceWidgets();
    for (int32 Order = 0; Order < Widgets.Num(); ++Order)
    {
        UWidget* Widget = Widgets[Order];
        if (!Widget)
        {
            continue;
        }

        UPanelWidget* ParentWidget = Widget->GetParent();
        FN2CNativeObject WidgetObject;
        WidgetObject.Kind = TEXT("widget");
        WidgetObject.Name = Widget->GetName();
        WidgetObject.ObjectPath = Widget->GetPathName();
        WidgetObject.NativeClass = Widget->GetClass()->GetPathName();
        WidgetObject.ParentPath = ParentWidget ? ParentWidget->GetPathName() : FString();
        WidgetObject.Order = ParentWidget ? ParentWidget->GetChildIndex(Widget) : Order;
        WidgetObject.NativeProperties = ExportNativeProperties(Widget);
        Blueprint.NativeObjects.Add(MoveTemp(WidgetObject));

        if (UPanelSlot* Slot = Widget->Slot)
        {
            FN2CNativeObject SlotObject;
            SlotObject.Kind = TEXT("widget_slot");
            SlotObject.Name = Slot->GetName();
            SlotObject.ObjectPath = Slot->GetPathName();
            SlotObject.NativeClass = Slot->GetClass()->GetPathName();
            SlotObject.ParentPath = Widget->GetPathName();
            SlotObject.Order = ParentWidget ? ParentWidget->GetChildIndex(Widget) : Order;
            SlotObject.NativeProperties = ExportNativeProperties(Slot);
            Blueprint.NativeObjects.Add(MoveTemp(SlotObject));
        }
    }
}

void AddBlueprintAssetProperties(FN2CBlueprint& Blueprint, UBlueprint* SourceBlueprint)
{
    N2C_LOG_FUNCTION_ENTRY();
    if (!SourceBlueprint)
    {
        return;
    }
    FN2CNativeObject AssetProperties;
    AssetProperties.Kind = TEXT("blueprint_asset_properties");
    AssetProperties.Name = SourceBlueprint->GetName();
    AssetProperties.ObjectPath = SourceBlueprint->GetPathName();
    AssetProperties.NativeClass = SourceBlueprint->GetClass()->GetPathName();
    AssetProperties.NativeProperties = ExportNativeProperties(SourceBlueprint);
    if (const TMap<FName, FString>* Metadata = FMetaData::GetMapForObject(SourceBlueprint))
    {
        for (const TPair<FName, FString>& Entry : *Metadata)
        {
            AssetProperties.NativeProperties.Add(TEXT("metadata.") + Entry.Key.ToString(), Entry.Value);
        }
    }
    Blueprint.NativeObjects.Add(MoveTemp(AssetProperties));
}

void AddGeneratedClassDefaults(FN2CBlueprint& Blueprint, UBlueprint* SourceBlueprint)
{
    N2C_LOG_FUNCTION_ENTRY();
    if (!SourceBlueprint || !SourceBlueprint->GeneratedClass)
    {
        return;
    }

    UObject* DefaultObject = SourceBlueprint->GeneratedClass->GetDefaultObject();
    UObject* ParentDefaultObject = SourceBlueprint->ParentClass
        ? SourceBlueprint->ParentClass->GetDefaultObject() : nullptr;
    if (!DefaultObject)
    {
        return;
    }

    FN2CNativeObject Defaults;
    Defaults.Kind = TEXT("class_default_overrides");
    Defaults.Name = DefaultObject->GetName();
    Defaults.ObjectPath = DefaultObject->GetPathName();
    Defaults.NativeClass = DefaultObject->GetClass()->GetPathName();
    for (TFieldIterator<FProperty> It(SourceBlueprint->GeneratedClass); It; ++It)
    {
        FProperty* Property = *It;
        if (!Property || Property->HasAnyPropertyFlags(
                CPF_Transient | CPF_DuplicateTransient | CPF_NonPIEDuplicateTransient | CPF_Deprecated))
        {
            continue;
        }

        FProperty* ParentProperty = ParentDefaultObject
            ? ParentDefaultObject->GetClass()->FindPropertyByName(Property->GetFName()) : nullptr;
        if (ParentProperty && ParentProperty->GetClass() == Property->GetClass() &&
            ParentProperty->Identical_InContainer(DefaultObject, ParentDefaultObject))
        {
            continue;
        }

        FString Value;
        const void* ParentValue = ParentProperty && ParentProperty->GetClass() == Property->GetClass()
            ? ParentProperty->ContainerPtrToValuePtr<void>(ParentDefaultObject) : nullptr;
        Property->ExportText_Direct(
            Value,
            Property->ContainerPtrToValuePtr<void>(DefaultObject),
            ParentValue,
            DefaultObject,
            PPF_None);
        Defaults.NativeProperties.Add(Property->GetName(), MoveTemp(Value));
        Defaults.NativeProperties.Add(
            TEXT("property_type.") + Property->GetName(), Property->GetClass()->GetName());
    }
    Blueprint.NativeObjects.Add(MoveTemp(Defaults));
}
}

FN2CNodeTranslator& FN2CNodeTranslator::Get()
{
    N2C_LOG_FUNCTION_ENTRY();
    static FN2CNodeTranslator Instance;
    return Instance;
}

bool FN2CNodeTranslator::GenerateN2CStruct(const TArray<UK2Node*>& CollectedNodes)
{
    N2C_LOG_FUNCTION_ENTRY();
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

bool FN2CNodeTranslator::GenerateFromBlueprint(UBlueprint* InBlueprint, bool bIncludeVariables)
{
    N2C_LOG_FUNCTION_ENTRY();
    N2CBlueprint = FN2CBlueprint();
    NodeIDMap.Empty();
    PinIDMap.Empty();
    ProcessedStructPaths.Empty();
    ProcessedEnumPaths.Empty();
    AdditionalGraphsToProcess.Empty();

    if (!InBlueprint)
    {
        FN2CLogger::Get().LogWarning(TEXT("No Blueprint provided to GenerateFromBlueprint"));
        return false;
    }

    // Metadata
    N2CBlueprint.Metadata.Name = InBlueprint->GetName();
    N2CBlueprint.ParentClassPath = InBlueprint->ParentClass
        ? InBlueprint->ParentClass->GetPathName() : FString();
    if (InBlueprint->GeneratedClass)
    {
        N2CBlueprint.GeneratedClassPath = InBlueprint->GeneratedClass->GetPathName();
        N2CBlueprint.Metadata.BlueprintClass = GetCleanClassName(InBlueprint->GeneratedClass->GetName());
    }
    else if (InBlueprint->SkeletonGeneratedClass)
    {
        N2CBlueprint.GeneratedClassPath = InBlueprint->SkeletonGeneratedClass->GetPathName();
        N2CBlueprint.Metadata.BlueprintClass = GetCleanClassName(InBlueprint->SkeletonGeneratedClass->GetName());
    }
    for (const FBPInterfaceDescription& InterfaceDescription : InBlueprint->ImplementedInterfaces)
    {
        if (InterfaceDescription.Interface)
        {
            N2CBlueprint.ImplementedInterfacePaths.AddUnique(
                InterfaceDescription.Interface->GetPathName());
        }
    }
    switch (InBlueprint->BlueprintType)
    {
        case BPTYPE_Const:            N2CBlueprint.Metadata.BlueprintType = EN2CBlueprintType::Const; break;
        case BPTYPE_MacroLibrary:     N2CBlueprint.Metadata.BlueprintType = EN2CBlueprintType::MacroLibrary; break;
        case BPTYPE_Interface:        N2CBlueprint.Metadata.BlueprintType = EN2CBlueprintType::Interface; break;
        case BPTYPE_LevelScript:      N2CBlueprint.Metadata.BlueprintType = EN2CBlueprintType::LevelScript; break;
        case BPTYPE_FunctionLibrary:  N2CBlueprint.Metadata.BlueprintType = EN2CBlueprintType::FunctionLibrary; break;
        default:                      N2CBlueprint.Metadata.BlueprintType = EN2CBlueprintType::Normal; break;
    }

    IRigVMEditorAssetInterface* RigVMAsset = Cast<IRigVMEditorAssetInterface>(InBlueprint);
    TArray<URigVMGraph*> RigVMGraphs;
    TSet<UEdGraph*> RigVMEditorGraphs;
    if (RigVMAsset)
    {
        RigVMGraphs = RigVMAsset->GetAllModels();
        for (URigVMGraph* RigVMGraph : RigVMGraphs)
        {
            if (RigVMGraph)
            {
                if (UEdGraph* EditorGraph = RigVMAsset->GetEdGraph(RigVMGraph))
                {
                    RigVMEditorGraphs.Add(EditorGraph);
                }
            }
        }
    }

    // Keep the established primary graph ordering, then include every graph category
    // exposed by UBlueprint (interface, delegate, extension, and nested graphs).
    TArray<UEdGraph*> Graphs;
    TSet<UEdGraph*> SeenGraphs;
    auto AddGraph = [&Graphs, &SeenGraphs](UEdGraph* Graph)
    {
        if (Graph && !SeenGraphs.Contains(Graph))
        {
            SeenGraphs.Add(Graph);
            Graphs.Add(Graph);
        }
    };

    for (UEdGraph* Graph : InBlueprint->UbergraphPages)
    {
        AddGraph(Graph);
    }
    for (UEdGraph* Graph : InBlueprint->FunctionGraphs)
    {
        AddGraph(Graph);
    }
    for (UEdGraph* Graph : InBlueprint->MacroGraphs)
    {
        AddGraph(Graph);
    }

    TArray<UEdGraph*> AllBlueprintGraphs;
    InBlueprint->GetAllGraphs(AllBlueprintGraphs);
    for (UEdGraph* Graph : AllBlueprintGraphs)
    {
        AddGraph(Graph);
    }

    for (UEdGraph* Graph : Graphs)
    {
        if (!Graph || RigVMEditorGraphs.Contains(Graph) ||
            (RigVMAsset && RigVMAsset->GetModel(Graph)))
        {
            continue;
        }
        CurrentDepth = 0;
        ProcessGraph(Graph, DetermineGraphType(Graph));
    }

    // UBlueprint graph arrays do not contain every RigVM model graph. Read the
    // model API directly, without creating or saving editor wrapper graphs.
    for (URigVMGraph* RigVMGraph : RigVMGraphs)
    {
        ProcessRigVMGraph(RigVMGraph);
    }

    // Collect component overrides defined on this Blueprint (SimpleConstructionScript)
    CollectComponentOverrides(InBlueprint);
    AddBlueprintAssetProperties(N2CBlueprint, InBlueprint);
    AddGeneratedClassDefaults(N2CBlueprint, InBlueprint);
    AddControlRigHierarchyExport(N2CBlueprint, Cast<IControlRigEditorAssetInterface>(InBlueprint));
    AddWidgetTreeExport(N2CBlueprint, Cast<UWidgetBlueprint>(InBlueprint));

    // Variables
    if (bIncludeVariables)
    {
        for (const FBPVariableDescription& Desc : InBlueprint->NewVariables)
        {
            FN2CVariable Var;
            ConvertVariableDescription(Desc, Var);
            N2CBlueprint.Variables.Add(Var);
        }
        if (RigVMAsset)
        {
            for (const FRigVMGraphVariableDescription& Description : RigVMAsset->GetAssetVariables())
            {
                FN2CVariable Var = RigVMVariableToNodeToCode(Description);
                const bool bAlreadyPresent = N2CBlueprint.Variables.ContainsByPredicate(
                    [&Var](const FN2CVariable& Existing)
                    {
                        return Existing.Name == Var.Name && Existing.TypeName == Var.TypeName;
                    });
                if (!bAlreadyPresent)
                {
                    N2CBlueprint.Variables.Add(MoveTemp(Var));
                }
            }
        }
    }

    // Synthesize a dedicated ClassItSelf graph to carry only class-level structure
    // (UCLASS, class declaration, UPROPERTY members, ctor/dtor) separate from EventGraph logic.
    // 
    // NOTE: ClassItSelf is a synthetic graph type that does not correspond to any actual Blueprint graph.
    // It serves as a marker for the LLM to generate class-level C++ code (class declaration, constructor,
    // destructor, member variables, and component declarations). The actual class-level data (variables
    // and components) are stored at the Blueprint level (N2CBlueprint.Variables and N2CBlueprint.Components),
    // not within this graph. The graph itself is intentionally empty (no nodes) because it represents
    // class structure, not executable flow.
    {
        FN2CGraph ClassItSelfGraph;
        ClassItSelfGraph.Name = TEXT("ClassItSelf");
        ClassItSelfGraph.GraphType = EN2CGraphType::ClassItSelf;
        // Intentionally leave this graph empty - it's a marker for class-level code generation
        // The LLM will use this graph type along with Blueprint-level Variables[] and Components[]
        // arrays to generate the class skeleton in C++
        N2CBlueprint.Graphs.Add(ClassItSelfGraph);
    }
    FString Ctx = FString::Printf(
        TEXT("Generated from Blueprint: %s (Graphs=%d, Vars=%d, Components=%d)"),
        *N2CBlueprint.Metadata.Name,
        N2CBlueprint.Graphs.Num(),
        N2CBlueprint.Variables.Num(),
        N2CBlueprint.Components.Num());
    FN2CLogger::Get().Log(TEXT("Blueprint translation complete"), EN2CLogSeverity::Info, Ctx);
    return N2CBlueprint.Graphs.Num() > 0;
}

void FN2CNodeTranslator::CollectComponentOverrides(UBlueprint* InBlueprint)
{
    N2C_LOG_FUNCTION_ENTRY();
    if (!InBlueprint)
    {
        return;
    }

    USimpleConstructionScript* SCS = InBlueprint->SimpleConstructionScript;
    if (!SCS)
    {
        return;
    }

    const TArray<USCS_Node*>& AllNodes = SCS->GetAllNodes();
    if (AllNodes.Num() == 0)
    {
        return;
    }

    for (USCS_Node* Node : AllNodes)
    {
        if (!Node)
        {
            continue;
        }

        UActorComponent* Template = Node->ComponentTemplate;
        FN2CNativeObject SCSNodeObject;
        SCSNodeObject.Kind = TEXT("simple_construction_node");
        SCSNodeObject.Name = Node->GetVariableName().ToString();
        SCSNodeObject.ObjectPath = Node->GetPathName();
        SCSNodeObject.NativeClass = Node->GetClass()->GetPathName();
        SCSNodeObject.NativeProperties = ExportNativeProperties(Node);
        SCSNodeObject.NativeProperties.Add(TEXT("variable_name"), Node->GetVariableName().ToString());
        SCSNodeObject.NativeProperties.Add(TEXT("variable_guid"), Node->VariableGuid.ToString());
        SCSNodeObject.NativeProperties.Add(TEXT("parent_component_or_variable_name"),
            Node->ParentComponentOrVariableName.ToString());
        SCSNodeObject.NativeProperties.Add(TEXT("parent_is_native"),
            Node->bIsParentComponentNative ? TEXT("true") : TEXT("false"));
        if (Node->bIsParentComponentNative)
        {
            SCSNodeObject.ParentPath = TEXT("native_component:") + Node->ParentComponentOrVariableName.ToString();
            SCSNodeObject.Order = INDEX_NONE;
        }
        else if (Node->ParentComponentOrVariableName.IsNone())
        {
            SCSNodeObject.Order = SCS->GetRootNodes().IndexOfByKey(Node);
        }
        else
        {
            for (USCS_Node* ParentNode : AllNodes)
            {
                if (ParentNode && ParentNode->GetVariableName() == Node->ParentComponentOrVariableName)
                {
                    SCSNodeObject.ParentPath = ParentNode->GetPathName();
                    SCSNodeObject.Order = ParentNode->GetChildNodes().IndexOfByKey(Node);
                    break;
                }
            }
        }
        N2CBlueprint.NativeObjects.Add(MoveTemp(SCSNodeObject));
        if (!Template)
        {
            continue;
        }

        FN2CNativeObject ComponentTemplateObject;
        ComponentTemplateObject.Kind = TEXT("component_template");
        ComponentTemplateObject.Name = Node->GetVariableName().ToString();
        ComponentTemplateObject.ObjectPath = Template->GetPathName();
        ComponentTemplateObject.NativeClass = Template->GetClass()->GetPathName();
        ComponentTemplateObject.ParentPath = Node->GetPathName();
        ComponentTemplateObject.NativeProperties = ExportNativeProperties(Template);
        ComponentTemplateObject.NativeProperties.Add(TEXT("creation_method"),
            StaticEnum<EComponentCreationMethod>()->GetNameStringByValue(static_cast<int64>(Template->CreationMethod)));
        N2CBlueprint.NativeObjects.Add(MoveTemp(ComponentTemplateObject));

        UClass* ComponentClass = Template->GetClass();
        if (!ComponentClass)
        {
            continue;
        }

        UActorComponent* ClassDefaultObject = Cast<UActorComponent>(ComponentClass->GetDefaultObject());
        if (!ClassDefaultObject)
        {
            continue;
        }
        FN2CComponentOverride ComponentOverride;
        ComponentOverride.ComponentName = Node->GetVariableName().ToString();
        ComponentOverride.ComponentClassName = GetCleanClassName(ComponentClass->GetName());

        ComponentOverride.AttachParentName = Node->ParentComponentOrVariableName.ToString();
        // Diff properties between the Blueprint component template and the class CDO
        for (TFieldIterator<FProperty> PropIt(ComponentClass, EFieldIteratorFlags::IncludeSuper); PropIt; ++PropIt)
        {
            FProperty* Property = *PropIt;
            if (!Property)
            {
                continue;
            }

            // Skip transient or non-config properties that are unlikely to be Blueprint-edited defaults
            if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_TextExportTransient))
            {
                continue;
            }

            // If values are identical, skip
            if (Property->Identical_InContainer(Template, ClassDefaultObject))
            {
                continue;
            }

            FN2CVariable Var;
            Var.Name = Property->GetName();

            // Map property type to EN2CStructMemberType and TypeName when possible
            Var.Type = ConvertPropertyToStructMemberType(Property);

            if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
            {
                if (UScriptStruct* Struct = StructProp->Struct)
                {
                    Var.TypeName = Struct->GetName();
                }
            }
            else if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Property))
            {
                if (UClass* ObjClass = ObjProp->PropertyClass)
                {
                    Var.TypeName = ObjClass->GetName();
                }
            }
            else if (FClassProperty* ClassProp = CastField<FClassProperty>(Property))
            {
                if (UClass* MetaClass = ClassProp->MetaClass)
                {
                    Var.TypeName = MetaClass->GetName();
                }
            }
            else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
            {
                if (UEnum* Enum = EnumProp->GetEnum())
                {
                    Var.TypeName = Enum->GetName();
                }
            }

            // Export the overridden value as text
            FString ExportedValue;
            void* TemplateValuePtr = Property->ContainerPtrToValuePtr<void>(Template);
            void* DefaultValuePtr = Property->ContainerPtrToValuePtr<void>(ClassDefaultObject);
            Property->ExportText_Direct(ExportedValue, TemplateValuePtr, DefaultValuePtr, nullptr, PPF_None);
            Var.DefaultValue = ExportedValue;

            ComponentOverride.OverriddenProperties.Add(Var);
        }

        const bool bHasOverrides = ComponentOverride.OverriddenProperties.Num() > 0;

        // Treat components whose template was not created as Native as
        // Blueprint-added (non-native) components. These should always be
        // declared and constructed in the ClassItSelf graph, even if they
        // have no property overrides. For native/inherited components we only
        // include them when there are overridden properties to initialize.
        const bool bIsBlueprintAddedComponent = (Template->CreationMethod != EComponentCreationMethod::Native);

        if (bIsBlueprintAddedComponent || bHasOverrides)
        {
            N2CBlueprint.Components.Add(ComponentOverride);
        }
    }
}

void FN2CNodeTranslator::ConvertVariableDescription(const FBPVariableDescription& Desc, FN2CVariable& OutVar)
{
    N2C_LOG_FUNCTION_ENTRY();
    OutVar = FN2CVariable();
    OutVar.Name = Desc.VarName.ToString();
    OutVar.DisplayName = Desc.FriendlyName;
    
    // Map type from FEdGraphPinType
    const FEdGraphPinType& T = Desc.VarType;
    const FString& Cat = T.PinCategory.ToString();
    OutVar.TypePath = T.PinSubCategoryObject.IsValid() ? T.PinSubCategoryObject->GetPathName() : FString();
    OutVar.NativeProperties.Add(TEXT("guid"), Desc.VarGuid.ToString());
    OutVar.NativeProperties.Add(TEXT("friendly_name"), Desc.FriendlyName);
    OutVar.NativeProperties.Add(TEXT("category"), Desc.Category.ToString());
    OutVar.NativeProperties.Add(TEXT("property_flags"), LexToString(Desc.PropertyFlags));
    OutVar.NativeProperties.Add(TEXT("rep_notify_function"), Desc.RepNotifyFunc.ToString());
    OutVar.NativeProperties.Add(TEXT("replication_condition"), FString::FromInt(Desc.ReplicationCondition.GetValue()));
    OutVar.NativeProperties.Add(TEXT("pin_category"), T.PinCategory.ToString());
    OutVar.NativeProperties.Add(TEXT("pin_sub_category"), T.PinSubCategory.ToString());
    OutVar.NativeProperties.Add(TEXT("pin_container_type"), LexToString(static_cast<uint8>(T.ContainerType)));
    OutVar.NativeProperties.Add(TEXT("pin_terminal_category"), T.PinValueType.TerminalCategory.ToString());
    OutVar.NativeProperties.Add(TEXT("pin_terminal_sub_category"), T.PinValueType.TerminalSubCategory.ToString());
    OutVar.NativeProperties.Add(TEXT("pin_terminal_object_path"), T.PinValueType.TerminalSubCategoryObject.IsValid()
        ? T.PinValueType.TerminalSubCategoryObject->GetPathName() : FString());
    OutVar.NativeProperties.Add(TEXT("pin_is_reference"), T.bIsReference ? TEXT("true") : TEXT("false"));
    OutVar.NativeProperties.Add(TEXT("pin_is_const"), T.bIsConst ? TEXT("true") : TEXT("false"));
    OutVar.NativeProperties.Add(TEXT("pin_is_weak_pointer"), T.bIsWeakPointer ? TEXT("true") : TEXT("false"));
    OutVar.NativeProperties.Add(TEXT("pin_is_uobject_wrapper"), T.bIsUObjectWrapper ? TEXT("true") : TEXT("false"));
    for (const FBPVariableMetaDataEntry& Metadata : Desc.MetaDataArray)
    {
        OutVar.NativeProperties.Add(TEXT("metadata.") + Metadata.DataKey.ToString(), Metadata.DataValue);
    }
    
    // Map basic types
    if (Cat == TEXT("bool")) 
    {
        OutVar.Type = EN2CStructMemberType::Bool;
    }
    else if (Cat == TEXT("byte")) 
    {
        OutVar.Type = EN2CStructMemberType::Byte;
    }
    else if (Cat == TEXT("int")) 
    {
        OutVar.Type = EN2CStructMemberType::Int;
    }
    else if (Cat == TEXT("float")) 
    {
        OutVar.Type = EN2CStructMemberType::Float;
    }
    else if (Cat == TEXT("string")) 
    {
        OutVar.Type = EN2CStructMemberType::String;
    }
    else if (Cat == TEXT("name")) 
    {
        OutVar.Type = EN2CStructMemberType::Name;
    }
    else if (Cat == TEXT("text")) 
    {
        OutVar.Type = EN2CStructMemberType::Text;
    }
    else if (Cat == TEXT("struct")) 
    {
        OutVar.Type = EN2CStructMemberType::Struct;
        OutVar.TypeName = T.PinSubCategoryObject.IsValid() ? T.PinSubCategoryObject->GetName() : FString();
    }
    else if (Cat == TEXT("class")) 
    {
        OutVar.Type = EN2CStructMemberType::Class;
        OutVar.TypeName = T.PinSubCategoryObject.IsValid() ? T.PinSubCategoryObject->GetName() : FString();
    }
    else if (Cat == TEXT("object")) 
    {
        OutVar.Type = EN2CStructMemberType::Object;
        OutVar.TypeName = T.PinSubCategoryObject.IsValid() ? T.PinSubCategoryObject->GetName() : FString();
    }
    else if (Cat == TEXT("enum")) 
    {
        OutVar.Type = EN2CStructMemberType::Enum;
        OutVar.TypeName = T.PinSubCategoryObject.IsValid() ? T.PinSubCategoryObject->GetName() : FString();
    }
    else 
    {
        OutVar.Type = EN2CStructMemberType::Custom;
    }

    // Handle container types
    OutVar.bIsArray = T.ContainerType == EPinContainerType::Array;
    OutVar.bIsSet = T.ContainerType == EPinContainerType::Set;
    OutVar.bIsMap = T.ContainerType == EPinContainerType::Map;

    // Extract Map key type from PinValueType
    if (OutVar.bIsMap)
    {
        // Try to extract key type from PinValueType (for Map containers)
        if (T.PinValueType.TerminalCategory != NAME_None)
        {
            const FString KeyCategory = T.PinValueType.TerminalCategory.ToString();
            
            // Map key category to EN2CStructMemberType
            if (KeyCategory == TEXT("bool"))
            {
                OutVar.KeyType = EN2CStructMemberType::Bool;
            }
            else if (KeyCategory == TEXT("byte"))
            {
                OutVar.KeyType = EN2CStructMemberType::Byte;
            }
            else if (KeyCategory == TEXT("int") || KeyCategory == TEXT("int64"))
            {
                OutVar.KeyType = EN2CStructMemberType::Int;
            }
            else if (KeyCategory == TEXT("float") || KeyCategory == TEXT("double"))
            {
                OutVar.KeyType = EN2CStructMemberType::Float;
            }
            else if (KeyCategory == TEXT("string"))
            {
                OutVar.KeyType = EN2CStructMemberType::String;
            }
            else if (KeyCategory == TEXT("name"))
            {
                OutVar.KeyType = EN2CStructMemberType::Name;
            }
            else if (KeyCategory == TEXT("text"))
            {
                OutVar.KeyType = EN2CStructMemberType::Text;
            }
            else if (KeyCategory == TEXT("struct"))
            {
                OutVar.KeyType = EN2CStructMemberType::Struct;
                if (T.PinValueType.TerminalSubCategoryObject.IsValid())
                {
                    OutVar.KeyTypeName = T.PinValueType.TerminalSubCategoryObject->GetName();
                }
            }
            else if (KeyCategory == TEXT("enum"))
            {
                OutVar.KeyType = EN2CStructMemberType::Enum;
                if (T.PinValueType.TerminalSubCategoryObject.IsValid())
                {
                    OutVar.KeyTypeName = T.PinValueType.TerminalSubCategoryObject->GetName();
                }
            }
            else
            {
                OutVar.KeyType = EN2CStructMemberType::Custom;
            }
        }
        else
        {
            // Fallback: if PinValueType is not available, use Custom
            OutVar.KeyType = EN2CStructMemberType::Custom;
            FN2CLogger::Get().LogWarning(FString::Printf(
                TEXT("Map variable '%s' has no key type information in PinValueType, using Custom"), 
                *OutVar.Name));
        }
    }

    // Set default value if available
    OutVar.DefaultValue = Desc.DefaultValue;
}

void FN2CNodeTranslator::CollectLocalVariables(UK2Node_FunctionEntry* EntryNode, TArray<FN2CVariable>& OutVariables)
{
    N2C_LOG_FUNCTION_ENTRY();
    if (!EntryNode)
    {
        return;
    }

    OutVariables.Empty();
    
    for (const FBPVariableDescription& Desc : EntryNode->LocalVariables)
    {
        FN2CVariable Var;
        ConvertVariableDescription(Desc, Var);
        OutVariables.Add(Var);
    }
}

FString FN2CNodeTranslator::GenerateNodeID()
{
    N2C_LOG_FUNCTION_ENTRY();
    return FString::Printf(TEXT("N%d"), NodeIDMap.Num() + 1);
}

FString FN2CNodeTranslator::GeneratePinID(int32 PinCount)
{
    N2C_LOG_FUNCTION_ENTRY();
    return FString::Printf(TEXT("P%d"), PinCount + 1);
}

bool FN2CNodeTranslator::InitializeNodeProcessing(UK2Node* Node, FN2CNodeDefinition& OutNodeDef)
{
    N2C_LOG_FUNCTION_ENTRY();
    if (!Node)
    {
        FN2CLogger::Get().LogWarning(TEXT("Attempted to process null node"));
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

    OutNodeDef.NativeClass = Node->GetClass()->GetPathName();
    OutNodeDef.NativePath = Node->GetPathName();
    OutNodeDef.NativeProperties = ExportNativeProperties(Node);
    return true;
}

bool FN2CNodeTranslator::ProcessNode(UK2Node* Node, FN2CNodeDefinition& OutNodeDef)
{
    N2C_LOG_FUNCTION_ENTRY();
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

bool FN2CNodeTranslator::ProcessNativeGraphNode(UEdGraphNode* Node, FN2CNodeDefinition& OutNodeDef)
{
    N2C_LOG_FUNCTION_ENTRY();
    if (!Node)
    {
        return false;
    }

    FString* ExistingID = NodeIDMap.Find(Node->NodeGuid);
    OutNodeDef.ID = ExistingID ? *ExistingID : GenerateNodeID();
    NodeIDMap.Add(Node->NodeGuid, OutNodeDef.ID);
    OutNodeDef.NodeType = Node->IsA<UEdGraphNode_Comment>()
        ? EN2CNodeType::Comment : EN2CNodeType::Custom;
    OutNodeDef.Name = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
    OutNodeDef.MemberParent = Node->GetClass()->GetPathName();
    OutNodeDef.MemberName = Node->GetClass()->GetName();
    OutNodeDef.NativeClass = Node->GetClass()->GetPathName();
    OutNodeDef.NativePath = Node->GetPathName();
    OutNodeDef.NativeProperties = ExportNativeProperties(Node);
    OutNodeDef.Comment = Node->NodeComment;
    OutNodeDef.bPure = !Node->Pins.ContainsByPredicate([](const UEdGraphPin* Pin)
    {
        return Pin && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
    });

    TArray<UEdGraphPin*> ExecInputs;
    TArray<UEdGraphPin*> ExecOutputs;
    ProcessNodePins(Node, OutNodeDef, ExecInputs, ExecOutputs);
    ProcessNodeFlows(Node, ExecInputs, ExecOutputs);
    return true;
}

void FN2CNodeTranslator::AddGraphToProcess(UEdGraph* Graph)
{
    N2C_LOG_FUNCTION_ENTRY();
    if (!Graph)
    {
        return;
    }

    // Graph names are not unique across implemented interfaces; use native identity.
    const FString GraphPath = Graph->GetPathName();
    const FString GraphGuid = Graph->GraphGuid.IsValid() ? Graph->GraphGuid.ToString() : FString();
    for (const FN2CGraph& ExistingGraph : N2CBlueprint.Graphs)
    {
        if (ExistingGraph.GraphPath == GraphPath ||
            (!GraphGuid.IsEmpty() && ExistingGraph.GraphGuid == GraphGuid))
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
    N2C_LOG_FUNCTION_ENTRY();
    if (!Graph)
    {
        FN2CLogger::Get().LogWarning(TEXT("Attempted to process null graph"));
        return false;
    }

    // Create new graph structure with native identity and counts for coverage audits.
    FN2CGraph NewGraph;
    NewGraph.Name = Graph->GetName();
    NewGraph.GraphType = GraphType;
    NewGraph.GraphGuid = Graph->GraphGuid.IsValid() ? Graph->GraphGuid.ToString() : FString();
    NewGraph.GraphPath = Graph->GetPathName();
    NewGraph.NativeClass = Graph->GetClass()->GetPathName();
    NewGraph.NativeProperties = ExportNativeProperties(Graph);
    NewGraph.NativeNodeCount = Graph->Nodes.Num();

    // Collect function-local variables from the entry node (if any)
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (UK2Node_FunctionEntry* EntryNode = Cast<UK2Node_FunctionEntry>(Node))
        {
            CollectLocalVariables(EntryNode, NewGraph.LocalVariables);
            break;
        }
    }

    CurrentGraph = &NewGraph;

    // Preserve every native node in the normalized list; comments and reroutes
    // carry source annotations and physical topology even though they do not execute.
    bool bHasUntranslatedBehavior = false;
    bool bHasExecutableNodes = false;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (!Node)
        {
            ++NewGraph.SkippedNodeCount;
            NewGraph.SkippedNodeClasses.AddUnique(TEXT("<null>"));
            bHasUntranslatedBehavior = true;
            continue;
        }

        if (UK2Node* K2Node = Cast<UK2Node>(Node))
        {
            FN2CNodeDefinition NodeDef;
            if (ProcessNode(K2Node, NodeDef))
            {
                CurrentGraph->Nodes.Add(NodeDef);
                bHasExecutableNodes |= !K2Node->IsA<UK2Node_Knot>();
            }
            else
            {
                ++NewGraph.SkippedNodeCount;
                NewGraph.SkippedNodeClasses.AddUnique(Node->GetClass()->GetPathName());
                bHasUntranslatedBehavior = true;
            }
        }
        else
        {
            FN2CNodeDefinition NodeDef;
            if (ProcessNativeGraphNode(Node, NodeDef))
            {
                CurrentGraph->Nodes.Add(NodeDef);
                bHasExecutableNodes |= !Node->IsA<UEdGraphNode_Comment>();
            }
            else
            {
                ++NewGraph.SkippedNodeCount;
                NewGraph.SkippedNodeClasses.AddUnique(Node->GetClass()->GetPathName());
                if (!Node->IsA<UEdGraphNode_Comment>())
                {
                    bHasUntranslatedBehavior = true;
                }
            }
        }
    }
    NewGraph.bNoExecutableNodes = !bHasExecutableNodes && !bHasUntranslatedBehavior;

    // Preserve the complete original topology independently of lossy legacy flows.
    CollectDirectConnections(Graph, NewGraph);

    // Keep every native graph, including verified empty/comment-only graphs.
    if (CurrentGraph->Nodes.Num() > 0 && !ValidateFlowReferences(NewGraph))
    {
        FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Flow validation issues found in graph: %s"), *NewGraph.Name));
    }

    N2CBlueprint.Graphs.Add(NewGraph);
    FString Context = FString::Printf(
        TEXT("Processed graph: %s (native nodes=%d, translated=%d, skipped=%d)"),
        *NewGraph.Name,
        NewGraph.NativeNodeCount,
        NewGraph.Nodes.Num(),
        NewGraph.SkippedNodeCount);
    FN2CLogger::Get().Log(TEXT("Graph processing complete"), EN2CLogSeverity::Info, Context);
    return true;
}

bool FN2CNodeTranslator::ProcessRigVMGraph(URigVMGraph* SourceGraph)
{
    N2C_LOG_FUNCTION_ENTRY();
    if (!SourceGraph)
    {
        return false;
    }

    const FString GraphPath = SourceGraph->GetPathName();
    if (N2CBlueprint.Graphs.ContainsByPredicate([&GraphPath](const FN2CGraph& Existing)
        {
            return Existing.GraphPath == GraphPath;
        }))
    {
        return true;
    }

    FN2CGraph Graph;
    Graph.Name = SourceGraph->GetGraphName();
    if (Graph.Name.IsEmpty())
    {
        Graph.Name = SourceGraph->GetName();
    }
    Graph.GraphType = EN2CGraphType::RigVM;
    Graph.GraphGuid = FGuid::NewDeterministicGuid(GraphPath).ToString();
    Graph.GraphPath = GraphPath;
    Graph.NativeClass = SourceGraph->GetClass()->GetPathName();
    Graph.NativeProperties = ExportRigVMNativeProperties(SourceGraph);

    const TArray<URigVMNode*>& SourceNodes = SourceGraph->GetNodes();
    Graph.NativeNodeCount = SourceNodes.Num();
    Graph.bNoExecutableNodes = SourceNodes.IsEmpty();
    for (const FRigVMGraphVariableDescription& Description : SourceGraph->GetLocalVariables())
    {
        Graph.LocalVariables.Add(RigVMVariableToNodeToCode(Description));
    }

    TMap<const URigVMNode*, FString> NodeIds;
    TMap<const URigVMNode*, FString> NodeGuids;
    TMap<const URigVMPin*, FString> PinIds;
    TMap<const URigVMPin*, FString> PinGuids;
    TMap<const URigVMNode*, TArray<URigVMPin*>> PinsByNode;
    TSet<FString> SeenNodeGuids;
    TSet<FString> SeenPinGuids;
    int32 NodeIndex = 0;

    for (URigVMNode* Node : SourceNodes)
    {
        if (!Node)
        {
            ++Graph.SkippedNodeCount;
            Graph.SkippedNodeClasses.AddUnique(TEXT("<null>"));
            Graph.ConnectionErrors.Add(TEXT("RigVM graph contains a null model node."));
            ++Graph.UnresolvedConnectionCount;
            continue;
        }

        const FString NodePath = Node->GetNodePath().IsEmpty() ? Node->GetPathName() : Node->GetNodePath();
        const FString NodeGuid = FGuid::NewDeterministicGuid(GraphPath + TEXT("|") + NodePath).ToString();
        if (SeenNodeGuids.Contains(NodeGuid))
        {
            Graph.ConnectionErrors.Add(FString::Printf(
                TEXT("RigVM node path '%s' has a duplicate deterministic identity."), *NodePath));
            ++Graph.UnresolvedConnectionCount;
            continue;
        }
        SeenNodeGuids.Add(NodeGuid);
        NodeGuids.Add(Node, NodeGuid);
        NodeIds.Add(Node, FString::Printf(TEXT("N%d"), ++NodeIndex));

        TArray<URigVMPin*> NodePins = Node->GetAllPinsRecursively();
        PinsByNode.Add(Node, NodePins);
        int32 PinIndex = 0;
        for (URigVMPin* Pin : NodePins)
        {
            if (!Pin)
            {
                continue;
            }
            FString PinPath = Pin->GetPinPath();
            if (PinPath.IsEmpty())
            {
                PinPath = NodePath + TEXT(".") + Pin->GetName();
            }
            const FString PinKey = GraphPath + TEXT("|") + PinPath;
            const FString PinGuid = FGuid::NewDeterministicGuid(PinKey).ToString();
            if (SeenPinGuids.Contains(PinGuid))
            {
                Graph.ConnectionErrors.Add(FString::Printf(
                    TEXT("RigVM pin path '%s' has a duplicate deterministic identity."), *PinPath));
                ++Graph.UnresolvedConnectionCount;
                continue;
            }
            SeenPinGuids.Add(PinGuid);
            PinGuids.Add(Pin, PinGuid);
            PinIds.Add(Pin, FString::Printf(TEXT("P%d"), ++PinIndex));
        }
    }

    for (URigVMNode* Node : SourceNodes)
    {
        if (!Node || !NodeIds.Contains(Node))
        {
            continue;
        }

        FN2CNodeDefinition NodeDefinition;
        NodeDefinition.ID = NodeIds.FindRef(Node);
        NodeDefinition.Name = Node->GetNodeTitle();
        if (NodeDefinition.Name.IsEmpty())
        {
            NodeDefinition.Name = Node->GetName();
        }
        NodeDefinition.NativeClass = Node->GetClass()->GetPathName();
        NodeDefinition.NativePath = Node->GetNodePath();
        NodeDefinition.NativeProperties = ExportRigVMNativeProperties(Node);
        NodeDefinition.bPure = Node->IsPure();
        NodeDefinition.NodeType = EN2CNodeType::Custom;

        if (Node->IsA<URigVMFunctionEntryNode>())
        {
            NodeDefinition.NodeType = EN2CNodeType::FunctionEntry;
        }
        else if (Node->IsA<URigVMFunctionReturnNode>())
        {
            NodeDefinition.NodeType = EN2CNodeType::FunctionResult;
        }
        else if (Node->IsA<URigVMCommentNode>())
        {
            NodeDefinition.NodeType = EN2CNodeType::Comment;
            NodeDefinition.Comment = NodeDefinition.Name;
        }
        else if (URigVMUnitNode* UnitNode = Cast<URigVMUnitNode>(Node))
        {
            NodeDefinition.NodeType = EN2CNodeType::CallFunction;
            if (UScriptStruct* ScriptStruct = UnitNode->GetScriptStruct())
            {
                NodeDefinition.MemberParent = ScriptStruct->GetPathName();
            }
            NodeDefinition.MemberName = UnitNode->GetMethodName().ToString();
        }
        else
        {
            NodeDefinition.MemberParent = NodeDefinition.NativeClass;
            NodeDefinition.MemberName = NodeDefinition.NativePath;
        }

        for (URigVMPin* Pin : PinsByNode.FindRef(Node))
        {
            if (!Pin || !PinIds.Contains(Pin))
            {
                continue;
            }

            const FString CPPType = Pin->GetCPPType();
            const FString PinName = Pin->GetDisplayName().IsNone()
                ? Pin->GetName() : Pin->GetDisplayName().ToString();
            const ERigVMPinDirection Direction = Pin->GetDirection();
            FN2CPinDefinition PinDefinition;
            PinDefinition.ID = PinIds.FindRef(Pin);
            PinDefinition.Name = PinName;
            PinDefinition.Type = DetermineRigVMPinType(Pin);
            PinDefinition.SubType = CPPType;
            PinDefinition.DefaultValue = Pin->GetDefaultValue();
            PinDefinition.bConnected = Pin->IsLinked(true);
            PinDefinition.bIsArray = Pin->IsArray();
            PinDefinition.bIsSet = CPPType.StartsWith(TEXT("TSet<"));
            PinDefinition.bIsMap = CPPType.StartsWith(TEXT("TMap<"));
            PinDefinition.bIsReference = CPPType.Contains(TEXT("&"));
            PinDefinition.bIsConst = CPPType.StartsWith(TEXT("const "));

            if (Direction == ERigVMPinDirection::Output)
            {
                NodeDefinition.OutputPins.Add(MoveTemp(PinDefinition));
            }
            else
            {
                // RigVM IO pins retain their exact direction in Graph.Pins.
                NodeDefinition.InputPins.Add(MoveTemp(PinDefinition));
            }
        }

        Graph.Nodes.Add(MoveTemp(NodeDefinition));
    }

    TMap<const URigVMPin*, int32> GraphPinIndexByPin;
    for (URigVMNode* Node : SourceNodes)
    {
        if (!Node || !NodeIds.Contains(Node))
        {
            continue;
        }
        for (URigVMPin* Pin : PinsByNode.FindRef(Node))
        {
            if (!Pin || !PinGuids.Contains(Pin))
            {
                continue;
            }

            FN2CGraphPin PinData;
            PinData.NodeGuid = NodeGuids.FindRef(Node);
            PinData.PinGuid = PinGuids.FindRef(Pin);
            PinData.NodeId = NodeIds.FindRef(Node);
            PinData.PinId = PinIds.FindRef(Pin);
            PinData.NodeName = Node->GetNodeTitle();
            PinData.NodeClass = Node->GetClass()->GetPathName();
            PinData.NativePath = Pin->GetPinPath();
            PinData.NativeProperties = ExportRigVMNativeProperties(Pin);
            PinData.PinName = Pin->GetName();
            PinData.DisplayName = Pin->GetDisplayName().ToString();
            PinData.FriendlyName = PinData.DisplayName;
            PinData.Direction = RigVMPinDirectionToString(Pin->GetDirection());
            PinData.Category = Pin->GetCPPType();
            PinData.SubCategoryObject = IsValid(Pin->GetCPPTypeObject())
                ? Pin->GetCPPTypeObject()->GetPathName() : FString();
            PinData.ContainerType = Pin->IsArray() ? TEXT("Array") :
                PinData.Category.StartsWith(TEXT("TSet<")) ? TEXT("Set") :
                PinData.Category.StartsWith(TEXT("TMap<")) ? TEXT("Map") : TEXT("None");
            PinData.PinToolTip = Pin->GetToolTipText().ToString();
            PinData.DefaultValue = Pin->GetDefaultValue();
            if (UObject* DefaultObject = Pin->GetDefaultValueObject())
            {
                PinData.DefaultObjectPath = DefaultObject->GetPathName();
            }
            PinData.bIsExecution = Pin->IsExecuteContext();
            PinData.bConnected = Pin->IsLinked(true);
            PinData.bIsReference = PinData.Category.Contains(TEXT("&"));
            PinData.bIsConst = PinData.Category.StartsWith(TEXT("const "));
            PinData.bIsUObjectWrapper = IsValid(Pin->GetCPPTypeObject()) &&
                Pin->GetCPPTypeObject()->IsA<UClass>();
            if (URigVMPin* ParentPin = Pin->GetParentPin())
            {
                PinData.ParentPinGuid = PinGuids.FindRef(ParentPin);
            }
            for (URigVMPin* SubPin : Pin->GetSubPins())
            {
                if (SubPin && PinGuids.Contains(SubPin))
                {
                    PinData.SubPinGuids.Add(PinGuids.FindRef(SubPin));
                }
            }
            GraphPinIndexByPin.Add(Pin, Graph.Pins.Add(MoveTemp(PinData)));
        }
    }

    TSet<FString> SeenLinks;
    for (URigVMLink* RigVMLink : SourceGraph->GetLinks())
    {
        URigVMPin* SourcePin = RigVMLink ? RigVMLink->GetSourcePin() : nullptr;
        URigVMPin* TargetPin = RigVMLink ? RigVMLink->GetTargetPin() : nullptr;
        URigVMNode* SourceNode = SourcePin ? SourcePin->GetNode() : nullptr;
        URigVMNode* TargetNode = TargetPin ? TargetPin->GetNode() : nullptr;
        if (!SourcePin || !TargetPin || !SourceNode || !TargetNode ||
            !PinGuids.Contains(SourcePin) || !PinGuids.Contains(TargetPin) ||
            !NodeGuids.Contains(SourceNode) || !NodeGuids.Contains(TargetNode))
        {
            Graph.ConnectionErrors.Add(FString::Printf(
                TEXT("RigVM graph '%s' contains a link with an unresolved endpoint."), *Graph.Name));
            ++Graph.UnresolvedConnectionCount;
            continue;
        }

        const FString EdgeKey = NodeGuids.FindRef(SourceNode) + TEXT(":") + PinGuids.FindRef(SourcePin) +
            TEXT("->") + NodeGuids.FindRef(TargetNode) + TEXT(":") + PinGuids.FindRef(TargetPin);
        if (SeenLinks.Contains(EdgeKey))
        {
            Graph.ConnectionErrors.Add(FString::Printf(
                TEXT("RigVM graph '%s' contains a duplicate direct link '%s'."), *Graph.Name, *EdgeKey));
            ++Graph.UnresolvedConnectionCount;
            continue;
        }
        SeenLinks.Add(EdgeKey);

        const int32* SourcePinIndex = GraphPinIndexByPin.Find(SourcePin);
        const int32* TargetPinIndex = GraphPinIndexByPin.Find(TargetPin);
        FN2CDirectLink Link;
        Link.SourceNodeGuid = NodeGuids.FindRef(SourceNode);
        Link.SourcePinGuid = PinGuids.FindRef(SourcePin);
        Link.TargetNodeGuid = NodeGuids.FindRef(TargetNode);
        Link.TargetPinGuid = PinGuids.FindRef(TargetPin);
        Link.SourceNodeId = NodeIds.FindRef(SourceNode);
        Link.SourcePinId = PinIds.FindRef(SourcePin);
        Link.TargetNodeId = NodeIds.FindRef(TargetNode);
        Link.TargetPinId = PinIds.FindRef(TargetPin);
        Link.SourceNodeName = SourceNode->GetNodeTitle();
        Link.SourceNodeClass = SourceNode->GetClass()->GetPathName();
        Link.SourcePinName = SourcePinIndex ? Graph.Pins[*SourcePinIndex].PinName : SourcePin->GetName();
        Link.SourceDirection = TEXT("Output");
        Link.TargetNodeName = TargetNode->GetNodeTitle();
        Link.TargetNodeClass = TargetNode->GetClass()->GetPathName();
        Link.TargetPinName = TargetPinIndex ? Graph.Pins[*TargetPinIndex].PinName : TargetPin->GetName();
        Link.TargetDirection = TEXT("Input");
        Link.bIsExecution = SourcePin->IsExecuteContext() || TargetPin->IsExecuteContext();
        Link.bSourceHidden = SourcePinIndex ? Graph.Pins[*SourcePinIndex].bHidden : false;
        Link.bTargetHidden = TargetPinIndex ? Graph.Pins[*TargetPinIndex].bHidden : false;
        Graph.Connections.Add(MoveTemp(Link));
    }

    for (const FString& Error : Graph.ConnectionErrors)
    {
        FN2CLogger::Get().LogWarning(Error);
    }
    N2CBlueprint.Graphs.Add(MoveTemp(Graph));
    return true;
}
EN2CGraphType FN2CNodeTranslator::DetermineGraphType(UEdGraph* Graph) const
{
    N2C_LOG_FUNCTION_ENTRY();
    if (!Graph)
    {
        return EN2CGraphType::EventGraph;
    }

    // First check if this is a function graph by looking for function entry node
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node && Node->IsA<UK2Node_FunctionEntry>())
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
    N2C_LOG_FUNCTION_ENTRY();
    OutType = FN2CNodeTypeRegistry::Get().GetNodeType(Node);
}

EN2CPinType FN2CNodeTranslator::DeterminePinType(const UEdGraphPin* Pin) const
{
    N2C_LOG_FUNCTION_ENTRY();
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

void FN2CNodeTranslator::CollectDirectConnections(UEdGraph* Graph, FN2CGraph& OutGraph)
{
    N2C_LOG_FUNCTION_ENTRY();
    OutGraph.Pins.Empty();
    OutGraph.Connections.Empty();
    OutGraph.ConnectionErrors.Empty();
    OutGraph.UnresolvedConnectionCount = 0;

    auto RecordError = [&OutGraph](const FString& Message, bool bUnresolvedLink)
    {
        OutGraph.ConnectionErrors.AddUnique(Message);
        if (bUnresolvedLink)
        {
            ++OutGraph.UnresolvedConnectionCount;
        }
    };

    if (!Graph)
    {
        RecordError(TEXT("Cannot collect links from a null graph."), true);
        return;
    }

    TSet<FGuid> SeenNodeGuids;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (!Node)
        {
            RecordError(FString::Printf(TEXT("Graph '%s' contains a null node."), *Graph->GetName()), true);
            continue;
        }

        if (!Node->NodeGuid.IsValid())
        {
            RecordError(FString::Printf(TEXT("Node '%s' in graph '%s' has no valid GUID."),
                *Node->GetName(), *Graph->GetName()), true);
        }
        else if (SeenNodeGuids.Contains(Node->NodeGuid))
        {
            RecordError(FString::Printf(TEXT("Node GUID '%s' is duplicated in graph '%s'."),
                *Node->NodeGuid.ToString(), *Graph->GetName()), true);
        }
        SeenNodeGuids.Add(Node->NodeGuid);

        TSet<FGuid> SeenPinGuids;
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (!Pin)
            {
                RecordError(FString::Printf(TEXT("Node '%s' in graph '%s' contains a null pin."),
                    *Node->GetName(), *Graph->GetName()), true);
                continue;
            }

            if (!Pin->PinId.IsValid())
            {
                RecordError(FString::Printf(TEXT("Pin '%s' on node '%s' in graph '%s' has no valid GUID."),
                    *Pin->PinName.ToString(), *Node->GetName(), *Graph->GetName()), true);
            }
            else if (SeenPinGuids.Contains(Pin->PinId))
            {
                RecordError(FString::Printf(TEXT("Pin GUID '%s' is duplicated on node '%s' in graph '%s'."),
                    *Pin->PinId.ToString(), *Node->GetName(), *Graph->GetName()), true);
            }
            SeenPinGuids.Add(Pin->PinId);

            FN2CGraphPin PinData;
            PinData.NodeGuid = Node->NodeGuid.ToString();
            PinData.PinGuid = Pin->PinId.ToString();
            PinData.NodeId = NodeIDMap.FindRef(Node->NodeGuid);
            PinData.PinId = PinIDMap.FindRef(Pin);
            PinData.NodeName = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
            PinData.NodeClass = Node->GetClass()->GetPathName();
            PinData.PinName = Pin->PinName.ToString();
            PinData.DisplayName = Pin->GetDisplayName().ToString();
            PinData.FriendlyName = Pin->PinFriendlyName.ToString();
            PinData.Direction = Pin->Direction == EGPD_Input ? TEXT("Input") :
                Pin->Direction == EGPD_Output ? TEXT("Output") : TEXT("Invalid");
            PinData.Category = Pin->PinType.PinCategory.ToString();
            PinData.SubCategory = Pin->PinType.PinSubCategory.ToString();
            PinData.SubCategoryObject = Pin->PinType.PinSubCategoryObject.IsValid()
                ? Pin->PinType.PinSubCategoryObject->GetPathName() : FString();
            UObject* MemberParent = Pin->PinType.PinSubCategoryMemberReference.MemberParent.Get();
            PinData.SubCategoryMemberParent = IsValid(MemberParent) ? MemberParent->GetPathName() : FString();
            PinData.SubCategoryMemberName = Pin->PinType.PinSubCategoryMemberReference.MemberName.ToString();
            PinData.ContainerType = StaticEnum<EPinContainerType>()->GetNameStringByValue(
                static_cast<int64>(Pin->PinType.ContainerType));
            PinData.ValueTerminalCategory = Pin->PinType.PinValueType.TerminalCategory.ToString();
            PinData.ValueTerminalSubCategory = Pin->PinType.PinValueType.TerminalSubCategory.ToString();
            PinData.ValueTerminalSubCategoryObject = Pin->PinType.PinValueType.TerminalSubCategoryObject.IsValid()
                ? Pin->PinType.PinValueType.TerminalSubCategoryObject->GetPathName() : FString();
            PinData.PinToolTip = Pin->PinToolTip;
            PinData.DefaultValue = Pin->DefaultValue;
            PinData.AutogeneratedDefaultValue = Pin->AutogeneratedDefaultValue;
            PinData.DefaultObjectPath = IsValid(Pin->DefaultObject) ? Pin->DefaultObject->GetPathName() : FString();
            PinData.DefaultTextValue = Pin->DefaultTextValue.ToString();
            PinData.PersistentGuid = Pin->PersistentGuid.IsValid() ? Pin->PersistentGuid.ToString() : FString();
            PinData.ParentPinGuid = Pin->ParentPin ? Pin->ParentPin->PinId.ToString() : FString();
            if (Pin->ReferencePassThroughConnection && Pin->ReferencePassThroughConnection->GetOwningNode())
            {
                PinData.ReferencePassThroughNodeGuid = Pin->ReferencePassThroughConnection->GetOwningNode()->NodeGuid.ToString();
                PinData.ReferencePassThroughPinGuid = Pin->ReferencePassThroughConnection->PinId.ToString();
            }
            for (const UEdGraphPin* SubPin : Pin->SubPins)
            {
                if (SubPin)
                {
                    PinData.SubPinGuids.Add(SubPin->PinId.ToString());
                }
            }
            PinData.bIsExecution = Pin->PinType.PinCategory == TEXT("exec");
            PinData.bDeprecated = Pin->bDeprecated;
            PinData.bDisplayAsMutableRef = Pin->bDisplayAsMutableRef;
            PinData.bAllowFriendlyName = Pin->bAllowFriendlyName;
            PinData.bSavePinIfOrphaned = Pin->ShouldSavePinIfOrphaned();
            PinData.bUseBackwardsCompatForEmptyAutogeneratedValue = Pin->bUseBackwardsCompatForEmptyAutogeneratedValue;
            PinData.bConnected = Pin->LinkedTo.Num() > 0;
            PinData.bHidden = Pin->bHidden;
            PinData.bNotConnectable = Pin->bNotConnectable;
            PinData.bOrphaned = Pin->bOrphanedPin;
            PinData.bIsReference = Pin->PinType.bIsReference;
            PinData.bIsConst = Pin->PinType.bIsConst;
            PinData.bIsWeakPointer = Pin->PinType.bIsWeakPointer;
            PinData.bIsUObjectWrapper = Pin->PinType.bIsUObjectWrapper;
            PinData.bSerializeAsSinglePrecisionFloat = Pin->PinType.bSerializeAsSinglePrecisionFloat;
            PinData.bAdvancedView = Pin->bAdvancedView;
            PinData.bDefaultValueIsReadOnly = Pin->bDefaultValueIsReadOnly;
            PinData.bDefaultValueIsIgnored = Pin->bDefaultValueIsIgnored;
            OutGraph.Pins.Add(MoveTemp(PinData));
        }
    }

    TSet<FString> SeenLinks;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (!Node)
        {
            continue;
        }

        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (!Pin)
            {
                continue;
            }

            for (UEdGraphPin* Linked : Pin->LinkedTo)
            {
                if (!Linked)
                {
                    RecordError(FString::Printf(TEXT("Pin '%s' on node '%s' in graph '%s' has a null link."),
                        *Pin->PinName.ToString(), *Node->GetName(), *Graph->GetName()), true);
                    continue;
                }

                UEdGraphPin* Source = Pin;
                UEdGraphPin* Target = Linked;
                UEdGraphNode* SourceNode = Source->GetOwningNode();
                UEdGraphNode* TargetNode = Target->GetOwningNode();

                auto EndpointKey = [](const UEdGraphNode* EndpointNode, const UEdGraphPin* EndpointPin)
                {
                    const FString NodeKey = EndpointNode
                        ? (EndpointNode->NodeGuid.IsValid()
                            ? EndpointNode->NodeGuid.ToString()
                            : EndpointNode->GetPathName())
                        : TEXT("<null-node>");
                    const FString PinKey = EndpointPin
                        ? (EndpointPin->PinId.IsValid()
                            ? EndpointPin->PinId.ToString()
                            : EndpointPin->GetName())
                        : TEXT("<null-pin>");
                    return NodeKey + TEXT(":") + PinKey;
                };

                if (Linked->Direction == EGPD_Output && Pin->Direction != EGPD_Output)
                {
                    Swap(Source, Target);
                    Swap(SourceNode, TargetNode);
                }

                const bool bValidDirections =
                    Source->Direction == EGPD_Output && Target->Direction == EGPD_Input;
                FString SourceKey = EndpointKey(SourceNode, Source);
                FString TargetKey = EndpointKey(TargetNode, Target);
                if (!bValidDirections && SourceKey > TargetKey)
                {
                    Swap(Source, Target);
                    Swap(SourceNode, TargetNode);
                    Swap(SourceKey, TargetKey);
                }

                // Pin GUIDs are scoped to a node. Include both node GUIDs so
                // duplicated pin GUIDs on distinct nodes do not erase fan-out edges.
                const FString LinkKey = SourceKey + TEXT("->") + TargetKey;
                if (SeenLinks.Contains(LinkKey))
                {
                    continue;
                }
                SeenLinks.Add(LinkKey);

                const bool bValidEndpoints =
                    SourceNode && TargetNode &&
                    SourceNode->GetGraph() == Graph && TargetNode->GetGraph() == Graph &&
                    SourceNode->NodeGuid.IsValid() && TargetNode->NodeGuid.IsValid() &&
                    Source->PinId.IsValid() && Target->PinId.IsValid();
                if (!bValidEndpoints || !bValidDirections)
                {
                    RecordError(FString::Printf(
                        TEXT("Malformed link in graph '%s': %s.%s -> %s.%s (directions %s -> %s)."),
                        *Graph->GetName(),
                        SourceNode ? *SourceNode->GetName() : TEXT("<null-node>"),
                        Source ? *Source->PinName.ToString() : TEXT("<null-pin>"),
                        TargetNode ? *TargetNode->GetName() : TEXT("<null-node>"),
                        Target ? *Target->PinName.ToString() : TEXT("<null-pin>"),
                        Source ? (Source->Direction == EGPD_Output ? TEXT("Output") : TEXT("Input")) : TEXT("Invalid"),
                        Target ? (Target->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output")) : TEXT("Invalid")),
                        true);
                    continue;
                }

                FN2CDirectLink Link;
                Link.SourceNodeGuid = SourceNode->NodeGuid.ToString();
                Link.SourcePinGuid = Source->PinId.ToString();
                Link.TargetNodeGuid = TargetNode->NodeGuid.ToString();
                Link.TargetPinGuid = Target->PinId.ToString();
                Link.SourceNodeId = NodeIDMap.FindRef(SourceNode->NodeGuid);
                Link.TargetNodeId = NodeIDMap.FindRef(TargetNode->NodeGuid);
                Link.SourcePinId = PinIDMap.FindRef(Source);
                Link.TargetPinId = PinIDMap.FindRef(Target);
                Link.SourceNodeName = SourceNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
                Link.SourceNodeClass = SourceNode->GetClass()->GetPathName();
                Link.SourcePinName = Source->PinName.ToString();
                Link.SourceDirection = TEXT("Output");
                Link.TargetNodeName = TargetNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
                Link.TargetNodeClass = TargetNode->GetClass()->GetPathName();
                Link.TargetPinName = Target->PinName.ToString();
                Link.TargetDirection = TEXT("Input");
                Link.bIsExecution =
                    Source->PinType.PinCategory == TEXT("exec") ||
                    Target->PinType.PinCategory == TEXT("exec");
                Link.bSourceHidden = Source->bHidden;
                Link.bTargetHidden = Target->bHidden;
                Link.bSourceIsKnot = SourceNode->IsA<UK2Node_Knot>();
                Link.bTargetIsKnot = TargetNode->IsA<UK2Node_Knot>();
                OutGraph.Connections.Add(MoveTemp(Link));
            }
        }
    }

    if (OutGraph.UnresolvedConnectionCount > 0)
    {
        FN2CLogger::Get().LogWarning(FString::Printf(
            TEXT("Direct edge export for graph '%s' found %d unresolved issues."),
            *OutGraph.Name, OutGraph.UnresolvedConnectionCount));
    }
}

UEdGraphPin* FN2CNodeTranslator::TraceConnectionThroughKnots(UEdGraphPin* StartPin) const
{
    N2C_LOG_FUNCTION_ENTRY();
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
    N2C_LOG_FUNCTION_ENTRY();
    // Use the blueprint validator to validate the graph
    FN2CBlueprintValidator Validator;
    FString ErrorMessage;
    return Validator.ValidateFlowReferences(Graph, ErrorMessage);
}

void FN2CNodeTranslator::ProcessNodeTypeAndProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef)
{
    N2C_LOG_FUNCTION_ENTRY();
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
    N2C_LOG_FUNCTION_ENTRY();
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

void FN2CNodeTranslator::ProcessNodePins(UEdGraphNode* Node, FN2CNodeDefinition& OutNodeDef, TArray<UEdGraphPin*>& OutExecInputs, TArray<UEdGraphPin*>& OutExecOutputs)
{
    N2C_LOG_FUNCTION_ENTRY();
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
        PinIDMap.Add(Pin, PinID);
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

void FN2CNodeTranslator::ProcessNodeFlows(UEdGraphNode* Node, const TArray<UEdGraphPin*>& ExecInputs, const TArray<UEdGraphPin*>& ExecOutputs)
{
    N2C_LOG_FUNCTION_ENTRY();
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
                    FString SourcePinID = PinIDMap.FindRef(ActualSourcePin);
                    FString TargetNodeID = NodeIDMap.FindRef(ActualTargetPin->GetOwningNode()->NodeGuid);
                    FString TargetPinID = PinIDMap.FindRef(ActualTargetPin);
                    
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
    N2C_LOG_FUNCTION_ENTRY();
    FN2CEnum Result;
    
    if (!Enum)
    {
        FN2CLogger::Get().LogError(TEXT("Null enum provided to ProcessBlueprintEnum"));
        return Result;
    }
    
    // Get enum path
    FString EnumPath = Enum->GetPathName();
    FString EnumName = Enum->GetName();

    // Fallback to FName if GetName returns something unexpected
    if (EnumName.IsEmpty())
    {
        EnumName = Enum->GetFName().ToString();
    }

    // Final fallback: use the path's last segment
    if (EnumName.IsEmpty())
    {
        FString Isolated;
        if (EnumPath.Split(TEXT("/"), nullptr, &Isolated, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
        {
            EnumName = Isolated;
        }
    }

    // If still empty, return invalid result early
    if (EnumName.IsEmpty())
    {
        FN2CLogger::Get().LogError(TEXT("ProcessBlueprintEnum: Could not determine enum name (GetName, GetFName, and path fallback all failed)"));
        return Result;
    }

    FN2CLogger::Get().Log(
        FString::Printf(TEXT("ProcessBlueprintEnum: Processing enum '%s' (Path: %s)"),
            *EnumName, *EnumPath),
        EN2CLogSeverity::Info);

    // Process each enum fresh every time - don't skip duplicates
    // (ProcessedEnumPaths only prevents duplicate side-effects like struct nesting)

    // Set basic enum info
    Result.Name = EnumName;
    Result.TypePath = EnumPath;
    Result.UnderlyingType = EnumUnderlyingTypeName(Enum->GetUnderlyingType());
    AddObjectMetadata(Result.NativeProperties, Enum);
    Result.NativeProperties.Add(TEXT("cpp_form"), LexToString(static_cast<uint8>(Enum->GetCppForm())));
    Result.NativeProperties.Add(TEXT("is_flags_enum"),
        Enum->HasAnyEnumFlags(EEnumFlags::Flags) ? TEXT("true") : TEXT("false"));
    Result.NativeProperties.Add(TEXT("newer_version_exists"),
        Enum->HasAnyEnumFlags(EEnumFlags::NewerVersionExists) ? TEXT("true") : TEXT("false"));
    
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
    
    // Preserve every authored enumerator, including hidden and autogenerated sentinels.
    for (int32 i = 0; i < NumEnums; ++i)
    {
        FString ValueName = Enum->GetAuthoredNameStringByIndex(i);
        if (ValueName.IsEmpty())
        {
            ValueName = Enum->GetNameStringByIndex(i);
        }
        const FString DisplayName = Enum->GetDisplayNameTextByIndex(i).ToString();
        
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Enum value #%d: Name='%s'"), 
                i, *ValueName),
            EN2CLogSeverity::Debug);
        
        FN2CEnumValue Value;
        Value.Name = ValueName;
        Value.DisplayName = DisplayName;
        Value.Value = Enum->GetValueByIndex(i);
        Value.NativeProperties.Add(TEXT("index"), FString::FromInt(i));
        Value.NativeProperties.Add(TEXT("hidden"), Enum->HasMetaData(TEXT("Hidden"), i) ? TEXT("true") : TEXT("false"));
        for (const TCHAR* MetadataKey : { TEXT("ToolTip"), TEXT("DisplayName"), TEXT("ScriptName"),
                 TEXT("Hidden"), TEXT("Deprecated"), TEXT("DeprecationMessage"), TEXT("Spacer") })
        {
            if (Enum->HasMetaData(MetadataKey, i))
            {
                Value.NativeProperties.Add(
                    FString(TEXT("metadata.")) + MetadataKey, Enum->GetMetaData(MetadataKey, i));
            }
        }
        
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
    N2C_LOG_FUNCTION_ENTRY();
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
    N2C_LOG_FUNCTION_ENTRY();
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
    N2C_LOG_FUNCTION_ENTRY();
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
    N2C_LOG_FUNCTION_ENTRY();
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
    N2C_LOG_FUNCTION_ENTRY();
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
    N2C_LOG_FUNCTION_ENTRY();
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
    N2C_LOG_FUNCTION_ENTRY();
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
    N2C_LOG_FUNCTION_ENTRY();
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

    Member.NativeProperties.Add(TEXT("cpp_type"), Property->GetCPPType());
    Member.NativeProperties.Add(TEXT("property_class"), Property->GetClass()->GetName());
    Member.NativeProperties.Add(TEXT("owner_struct_path"), Property->GetOwnerStruct()->GetPathName());
    Member.NativeProperties.Add(TEXT("property_flags"), LexToString(static_cast<uint64>(Property->GetPropertyFlags())));
    Member.NativeProperties.Add(TEXT("array_dim"), FString::FromInt(Property->ArrayDim));

    if (const TMap<FName, FString>* Metadata = Property->GetMetaDataMap())
    {
        for (const TPair<FName, FString>& Entry : *Metadata)
        {
            Member.NativeProperties.Add(TEXT("metadata.") + Entry.Key.ToString(), Entry.Value);
        }
    }

    FProperty* ValueTypeProperty = Property;
    if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
    {
        ValueTypeProperty = ArrayProp->Inner;
    }
    else if (FSetProperty* SetProp = CastField<FSetProperty>(Property))
    {
        ValueTypeProperty = SetProp->ElementProp;
    }
    else if (FMapProperty* MapProp = CastField<FMapProperty>(Property))
    {
        ValueTypeProperty = MapProp->ValueProp;
        Member.NativeProperties.Add(TEXT("key_cpp_type"), MapProp->KeyProp->GetCPPType());
        if (FStructProperty* KeyStruct = CastField<FStructProperty>(MapProp->KeyProp))
        {
            Member.NativeProperties.Add(TEXT("key_type_path"), KeyStruct->Struct->GetPathName());
        }
        else if (FEnumProperty* KeyEnum = CastField<FEnumProperty>(MapProp->KeyProp))
        {
            Member.NativeProperties.Add(TEXT("key_type_path"), KeyEnum->GetEnum()->GetPathName());
        }
    }

    if (FStructProperty* StructProperty = CastField<FStructProperty>(ValueTypeProperty))
    {
        Member.TypeName = StructProperty->Struct->GetName();
        Member.TypePath = StructProperty->Struct->GetPathName();
    }
    else if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(ValueTypeProperty))
    {
        Member.TypeName = EnumProperty->GetEnum()->GetName();
        Member.TypePath = EnumProperty->GetEnum()->GetPathName();
    }
    else if (FByteProperty* ByteProperty = CastField<FByteProperty>(ValueTypeProperty); ByteProperty && ByteProperty->Enum)
    {
        Member.TypeName = ByteProperty->Enum->GetName();
        Member.TypePath = ByteProperty->Enum->GetPathName();
    }
    else if (FClassProperty* ClassProperty = CastField<FClassProperty>(ValueTypeProperty))
    {
        Member.TypeName = ClassProperty->MetaClass->GetName();
        Member.TypePath = ClassProperty->MetaClass->GetPathName();
    }
    else if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(ValueTypeProperty))
    {
        Member.TypeName = ObjectProperty->PropertyClass->GetName();
        Member.TypePath = ObjectProperty->PropertyClass->GetPathName();
    }

    if (const UUserDefinedStruct* OwnerStruct = Cast<UUserDefinedStruct>(Property->GetOwnerStruct()))
    {
        if (const uint8* DefaultInstance = OwnerStruct->GetDefaultInstance())
        {
            Property->ExportText_Direct(
                Member.DefaultValue,
                Property->ContainerPtrToValuePtr<void>(DefaultInstance),
                nullptr,
                nullptr,
                PPF_None);
        }
    }

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
    N2C_LOG_FUNCTION_ENTRY();
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
    Result.TypePath = StructPath;
    Result.NativeProperties.Add(TEXT("cpp_struct_name"), Struct->GetStructCPPName());
    Result.NativeProperties.Add(TEXT("struct_flags"), LexToString(static_cast<uint32>(Struct->StructFlags)));
    if (const UUserDefinedStruct* UserStruct = Cast<UUserDefinedStruct>(Struct))
    {
        Result.NativeProperties.Add(TEXT("guid"), UserStruct->Guid.ToString());
        Result.NativeProperties.Add(TEXT("status"), FString::FromInt(static_cast<int32>(UserStruct->Status.GetValue())));
    }
    AddObjectMetadata(Result.NativeProperties, Struct);
    
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
    N2C_LOG_FUNCTION_ENTRY();
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
