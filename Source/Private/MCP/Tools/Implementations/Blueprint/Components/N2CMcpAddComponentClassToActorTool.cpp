// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpAddComponentClassToActorTool.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpComponentUtils.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "Utils/N2CLogger.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"
#include "SubobjectData.h"
#include "SubobjectDataSubsystem.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UObjectGlobals.h"

REGISTER_MCP_TOOL(FN2CMcpAddComponentClassToActorTool)

FMcpToolDefinition FN2CMcpAddComponentClassToActorTool::GetDefinition() const
{
    FMcpToolDefinition Definition(
        TEXT("add-component-class-to-actor"),
        TEXT("Adds a component of a specified class to an Actor Blueprint, as the Components panel does (one undoable step). A scene component is attached to parentComponent, or to the actor's root component"),
        N2CMcpToolsets::BlueprintComponent
    );

    // Build input schema
    TSharedPtr<FJsonObject> Schema = MakeShareable(new FJsonObject);
    Schema->SetStringField(TEXT("type"), TEXT("object"));
    
    TSharedPtr<FJsonObject> Properties = MakeShareable(new FJsonObject);
    
    // componentClass property
    TSharedPtr<FJsonObject> ComponentClassProp = MakeShareable(new FJsonObject);
    ComponentClassProp->SetStringField(TEXT("type"), TEXT("string"));
    ComponentClassProp->SetStringField(TEXT("description"), TEXT("Class of the component to add: a class path ('/Script/Engine.StaticMeshComponent'), a loaded class name ('StaticMeshComponent'), or a component Blueprint's path"));
    Properties->SetObjectField(TEXT("componentClass"), ComponentClassProp);

    // componentName property
    TSharedPtr<FJsonObject> ComponentNameProp = MakeShareable(new FJsonObject);
    ComponentNameProp->SetStringField(TEXT("type"), TEXT("string"));
    ComponentNameProp->SetStringField(TEXT("description"), TEXT("Optional variable name for the component; must not be in use in the Blueprint. Generated from the class name if not provided"));
    Properties->SetObjectField(TEXT("componentName"), ComponentNameProp);

    // parentComponent property
    TSharedPtr<FJsonObject> ParentComponentProp = MakeShareable(new FJsonObject);
    ParentComponentProp->SetStringField(TEXT("type"), TEXT("string"));
    ParentComponentProp->SetStringField(TEXT("description"), TEXT("Name of the scene component to attach to, scene components only: one this Blueprint adds, one a parent Blueprint adds, or a native one ('Mesh' on a Character), as list-components-in-actor names them. Defaults to the actor's root component"));
    Properties->SetObjectField(TEXT("parentComponent"), ParentComponentProp);

    // attachSocketName property
    TSharedPtr<FJsonObject> AttachSocketProp = MakeShareable(new FJsonObject);
    AttachSocketProp->SetStringField(TEXT("type"), TEXT("string"));
    AttachSocketProp->SetStringField(TEXT("description"), TEXT("Socket of the parent component to attach to; must exist on it"));
    Properties->SetObjectField(TEXT("attachSocketName"), AttachSocketProp);

    // relativeTransform property
    TSharedPtr<FJsonObject> TransformProp = MakeShareable(new FJsonObject);
    TransformProp->SetStringField(TEXT("type"), TEXT("object"));
    TransformProp->SetStringField(TEXT("description"), TEXT("Optional relative transform for scene components"));
    
    TSharedPtr<FJsonObject> TransformProperties = MakeShareable(new FJsonObject);
    
    // Location
    TSharedPtr<FJsonObject> LocationProp = MakeShareable(new FJsonObject);
    LocationProp->SetStringField(TEXT("type"), TEXT("object"));
    TSharedPtr<FJsonObject> LocationProps = MakeShareable(new FJsonObject);
    LocationProps->SetObjectField(TEXT("x"), BuildNumberProperty(TEXT("X coordinate")));
    LocationProps->SetObjectField(TEXT("y"), BuildNumberProperty(TEXT("Y coordinate")));
    LocationProps->SetObjectField(TEXT("z"), BuildNumberProperty(TEXT("Z coordinate")));
    LocationProp->SetObjectField(TEXT("properties"), LocationProps);
    TransformProperties->SetObjectField(TEXT("location"), LocationProp);
    
    // Rotation
    TSharedPtr<FJsonObject> RotationProp = MakeShareable(new FJsonObject);
    RotationProp->SetStringField(TEXT("type"), TEXT("object"));
    TSharedPtr<FJsonObject> RotationProps = MakeShareable(new FJsonObject);
    RotationProps->SetObjectField(TEXT("pitch"), BuildNumberProperty(TEXT("Pitch (degrees)")));
    RotationProps->SetObjectField(TEXT("yaw"), BuildNumberProperty(TEXT("Yaw (degrees)")));
    RotationProps->SetObjectField(TEXT("roll"), BuildNumberProperty(TEXT("Roll (degrees)")));
    RotationProp->SetObjectField(TEXT("properties"), RotationProps);
    TransformProperties->SetObjectField(TEXT("rotation"), RotationProp);
    
    // Scale
    TSharedPtr<FJsonObject> ScaleProp = MakeShareable(new FJsonObject);
    ScaleProp->SetStringField(TEXT("type"), TEXT("object"));
    TSharedPtr<FJsonObject> ScaleProps = MakeShareable(new FJsonObject);
    ScaleProps->SetObjectField(TEXT("x"), BuildNumberProperty(TEXT("X scale"), 1.0));
    ScaleProps->SetObjectField(TEXT("y"), BuildNumberProperty(TEXT("Y scale"), 1.0));
    ScaleProps->SetObjectField(TEXT("z"), BuildNumberProperty(TEXT("Z scale"), 1.0));
    ScaleProp->SetObjectField(TEXT("properties"), ScaleProps);
    TransformProperties->SetObjectField(TEXT("scale"), ScaleProp);
    
    TransformProp->SetObjectField(TEXT("properties"), TransformProperties);
    Properties->SetObjectField(TEXT("relativeTransform"), TransformProp);

    // blueprintPath property
    TSharedPtr<FJsonObject> BlueprintPathProp = MakeShareable(new FJsonObject);
    BlueprintPathProp->SetStringField(TEXT("type"), TEXT("string"));
    BlueprintPathProp->SetStringField(TEXT("description"), TEXT("Optional asset path of the Blueprint. If not provided, uses the currently focused Blueprint"));
    Properties->SetObjectField(TEXT("blueprintPath"), BlueprintPathProp);

    Schema->SetObjectField(TEXT("properties"), Properties);
    
    // Required fields
    TArray<TSharedPtr<FJsonValue>> RequiredArray;
    RequiredArray.Add(MakeShareable(new FJsonValueString(TEXT("componentClass"))));
    Schema->SetArrayField(TEXT("required"), RequiredArray);
    
    Definition.InputSchema = Schema;
    
    return Definition;
}

FMcpToolCallResult FN2CMcpAddComponentClassToActorTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
    return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult
    {
        // Parse arguments
        FString ComponentClassPath;
        if (!Arguments->TryGetStringField(TEXT("componentClass"), ComponentClassPath))
        {
            return FMcpToolCallResult::CreateErrorResult(TEXT("Missing required parameter: componentClass"));
        }

        FString ComponentName;
        Arguments->TryGetStringField(TEXT("componentName"), ComponentName);
        ComponentName.TrimStartAndEndInline();

        FString ParentComponentName;
        Arguments->TryGetStringField(TEXT("parentComponent"), ParentComponentName);
        ParentComponentName.TrimStartAndEndInline();

        FString AttachSocketName;
        Arguments->TryGetStringField(TEXT("attachSocketName"), AttachSocketName);
        AttachSocketName.TrimStartAndEndInline();

        const TSharedPtr<FJsonObject>* TransformJson = nullptr;
        Arguments->TryGetObjectField(TEXT("relativeTransform"), TransformJson);

        FString BlueprintPath;
        Arguments->TryGetStringField(TEXT("blueprintPath"), BlueprintPath);

        // ---- Validate everything before changing anything (card 0167) ----

        FString ErrorMsg;
        UBlueprint* Blueprint = FN2CMcpBlueprintUtils::ResolveBlueprint(BlueprintPath, ErrorMsg);
        if (!Blueprint)
        {
            return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
        }

        FN2CComponentTree Tree;
        if (!FN2CMcpComponentUtils::BuildComponentTree(Blueprint, Tree, ErrorMsg))
        {
            return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
        }

        // A Blueprint that supports a construction script but has none: never create one here, and never reach
        // AddNewSubobject, which check()s for it on UE 5.6 and 5.7 (5.8 returns an error)
        if (!Blueprint->SimpleConstructionScript)
        {
            return FMcpToolCallResult::CreateErrorResult(FString::Printf(
                TEXT("NO_CONSTRUCTION_SCRIPT|Blueprint '%s' has no construction script to add components to"), *Blueprint->GetName()));
        }

        UClass* ComponentClass = FN2CMcpComponentUtils::ResolveComponentClass(ComponentClassPath, ErrorMsg);
        if (!ComponentClass)
        {
            return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
        }
        const bool bIsSceneComponent = ComponentClass->IsChildOf(USceneComponent::StaticClass());

        if (!ComponentName.IsEmpty())
        {
            if (!ParentComponentName.IsEmpty() && FName(*ParentComponentName) == FName(*ComponentName))
            {
                return FMcpToolCallResult::CreateErrorResult(FString::Printf(
                    TEXT("PARENT_IS_SELF|parentComponent '%s' is the name of the component being added"), *ParentComponentName));
            }
            if (Tree.Find(ComponentName) != INDEX_NONE)
            {
                return FMcpToolCallResult::CreateErrorResult(FString::Printf(
                    TEXT("NAME_EXISTS|Component with name '%s' already exists"), *ComponentName));
            }
            if (!FN2CMcpComponentUtils::ValidateNewComponentName(Blueprint, ComponentName, ErrorMsg))
            {
                return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
            }
        }

        if (!bIsSceneComponent && (!ParentComponentName.IsEmpty() || !AttachSocketName.IsEmpty()))
        {
            return FMcpToolCallResult::CreateErrorResult(FString::Printf(
                TEXT("NOT_SCENE_COMPONENT|'%s' is not a SceneComponent, so it is not attached to anything: leave out ")
                TEXT("parentComponent and attachSocketName"), *ComponentClass->GetName()));
        }

        // The parent: the one named (this Blueprint's, a parent Blueprint's or a native component), else the root
        int32 ParentIndex = INDEX_NONE;
        if (bIsSceneComponent)
        {
            if (!ParentComponentName.IsEmpty())
            {
                ParentIndex = Tree.Find(ParentComponentName);
                if (ParentIndex == INDEX_NONE)
                {
                    return FMcpToolCallResult::CreateErrorResult(FString::Printf(
                        TEXT("PARENT_NOT_FOUND|Parent component '%s' not found. Scene components: %s"),
                        *ParentComponentName, *Tree.SceneComponentNames()));
                }
            }
            else
            {
                ParentIndex = Tree.Root;
            }

            if (Tree.Entries.IsValidIndex(ParentIndex))
            {
                const FN2CComponentEntry& Parent = Tree.Entries[ParentIndex];
                const USceneComponent* ParentTemplate = Cast<USceneComponent>(Parent.Template);
                if (!Parent.bScene || !ParentTemplate)
                {
                    return FMcpToolCallResult::CreateErrorResult(FString::Printf(
                        TEXT("PARENT_NOT_SCENE|Parent component '%s' is not a SceneComponent"), *Parent.Name.ToString()));
                }
                // Both directions, as USubobjectDataSubsystem::FindParentForNewSubobject checks (it would otherwise
                // attach the component to the scene root without saying so)
                const USceneComponent* NewDefaults = GetDefault<USceneComponent>(ComponentClass);
                const FName SocketFName = AttachSocketName.IsEmpty() ? NAME_None : FName(*AttachSocketName);
                if (!ParentTemplate->CanAttachAsChild(NewDefaults, SocketFName) || !NewDefaults->CanAttachAsChild(ParentTemplate, NAME_None))
                {
                    return FMcpToolCallResult::CreateErrorResult(FString::Printf(
                        TEXT("CANNOT_ATTACH|A '%s' cannot be attached to '%s'"), *ComponentClass->GetName(), *Parent.Name.ToString()));
                }
                // Some components answer only one of the two (a spring arm lists SpringEndpoint but does not
                // override DoesSocketExist)
                const TArray<FName> SocketNames = AttachSocketName.IsEmpty() ? TArray<FName>() : ParentTemplate->GetAllSocketNames();
                if (!AttachSocketName.IsEmpty() && !ParentTemplate->DoesSocketExist(SocketFName) && !SocketNames.Contains(SocketFName))
                {
                    TArray<FString> Sockets;
                    for (const FName& Socket : SocketNames)
                    {
                        Sockets.Add(Socket.ToString());
                    }
                    return FMcpToolCallResult::CreateErrorResult(FString::Printf(
                        TEXT("SOCKET_NOT_FOUND|'%s' has no socket '%s'. Its sockets: %s"), *Parent.Name.ToString(),
                        *AttachSocketName, Sockets.Num() > 0 ? *FString::Join(Sockets, TEXT(", ")) : TEXT("(none)")));
                }
            }
            else if (!AttachSocketName.IsEmpty())
            {
                return FMcpToolCallResult::CreateErrorResult(TEXT("PARENT_NOT_FOUND|attachSocketName needs a parent component, and the actor has no root component"));
            }
        }

        // ---- Add it through the engine's subobject subsystem, as the Components panel does, in one transaction ----

        FName NewVariableName;
        FGuid NewNodeGuid;
        {
            FScopedTransaction Transaction(FText::Format(
                NSLOCTEXT("NodeToCode", "AddComponentTransaction", "Add Component '{0}'"),
                FText::FromString(ComponentName.IsEmpty() ? ComponentClass->GetName() : ComponentName)));

            TArray<FSubobjectDataHandle> Handles;
            FSubobjectDataHandle ActorHandle;
            if (!FN2CMcpComponentUtils::GatherSubobjects(Blueprint, Handles, ActorHandle, ErrorMsg))
            {
                Transaction.Cancel();
                return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
            }

            FSubobjectDataHandle ParentHandle = ActorHandle;
            if (bIsSceneComponent && Tree.Entries.IsValidIndex(ParentIndex))
            {
                ParentHandle = FN2CMcpComponentUtils::FindSubobjectHandle(Handles, Tree.Entries[ParentIndex].Name);
                if (!ParentHandle.IsValid())
                {
                    Transaction.Cancel();
                    return FMcpToolCallResult::CreateErrorResult(FString::Printf(
                        TEXT("PARENT_NOT_FOUND|Parent component '%s' has no subobject entry (compile the Blueprint and retry)"),
                        *Tree.Entries[ParentIndex].Name.ToString()));
                }
            }

            Blueprint->Modify();
            if (Blueprint->SimpleConstructionScript)
            {
                Blueprint->SimpleConstructionScript->Modify();
            }

            FAddNewSubobjectParams Params;
            Params.ParentHandle = ParentHandle;
            Params.NewClass = ComponentClass;
            Params.BlueprintContext = Blueprint;
            // MarkModified below; the relative transform is the one given (or identity), not conformed to the parent
            Params.bSkipMarkBlueprintModified = true;
            Params.bConformTransformToParent = false;

            FText FailReason;
            USubobjectDataSubsystem* Subsystem = USubobjectDataSubsystem::Get();
            const FSubobjectDataHandle NewHandle = Subsystem->AddNewSubobject(Params, FailReason);
            const FSubobjectData* NewData = NewHandle.GetData();
            USCS_Node* NewNode = NewData ? FN2CMcpComponentUtils::FindOwnSCSNodeForTemplate(Blueprint, NewData->GetComponentTemplate()) : nullptr;
            if (!NewNode)
            {
                const FString Reason = FailReason.IsEmpty() ? FString(TEXT("the engine did not add it")) : FailReason.ToString();
                Transaction.Cancel();
                return FMcpToolCallResult::CreateErrorResult(FString::Printf(
                    TEXT("ADD_FAILED|Could not add a '%s': %s"), *ComponentClass->GetName(), *Reason));
            }

            if (!ComponentName.IsEmpty() && NewNode->GetVariableName() != FName(*ComponentName))
            {
                Subsystem->RenameSubobject(NewHandle, FText::FromString(ComponentName));
            }
            if (!AttachSocketName.IsEmpty())
            {
                NewNode->Modify();
                NewNode->AttachToName = FName(*AttachSocketName);
            }
            if (bIsSceneComponent && TransformJson && TransformJson->IsValid())
            {
                FN2CMcpComponentUtils::ApplyTransformToComponent(Cast<USceneComponent>(NewNode->ComponentTemplate), *TransformJson, ErrorMsg);
            }

            NewVariableName = NewNode->GetVariableName();
            NewNodeGuid = NewNode->VariableGuid;
            FN2CMcpBlueprintUtils::MarkModified(Blueprint, /*bStructural*/ true);
        }

        // Compile once, after the transaction has closed, so the result reports what the compiler said (card 0151)
        FN2CCompileDiagnostics Diagnostics;
        FN2CMcpBlueprintUtils::Compile(Blueprint, Diagnostics);

        // Where the component ended up, read back after the compile
        FN2CComponentTree After;
        FN2CMcpComponentUtils::BuildComponentTree(Blueprint, After, ErrorMsg);
        const int32 NewIndex = After.Find(NewVariableName.ToString());
        const FN2CComponentEntry* NewEntry = After.Entries.IsValidIndex(NewIndex) ? &After.Entries[NewIndex] : nullptr;

        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("componentName"), NewVariableName.ToString());
        Result->SetStringField(TEXT("componentClass"), ComponentClass->GetName());
        Result->SetStringField(TEXT("componentClassPath"), ComponentClass->GetPathName());
        Result->SetStringField(TEXT("nodeGuid"), NewNodeGuid.ToString());
        Result->SetBoolField(TEXT("isSceneComponent"), bIsSceneComponent);
        if (NewEntry && After.Entries.IsValidIndex(NewEntry->Parent))
        {
            Result->SetStringField(TEXT("attachedTo"), After.Entries[NewEntry->Parent].Name.ToString());
            if (!NewEntry->SocketName.IsNone())
            {
                Result->SetStringField(TEXT("attachSocketName"), NewEntry->SocketName.ToString());
            }
        }
        Result->SetBoolField(TEXT("isRootComponent"), NewIndex != INDEX_NONE && NewIndex == After.Root);

        TSharedPtr<FJsonObject> CompilationStatus = MakeShared<FJsonObject>();
        CompilationStatus->SetBoolField(TEXT("success"), Diagnostics.bSuccess);
        CompilationStatus->SetNumberField(TEXT("errorCount"), Diagnostics.ErrorCount);
        CompilationStatus->SetNumberField(TEXT("warningCount"), Diagnostics.WarningCount);
        CompilationStatus->SetNumberField(TEXT("compilationTime"), Diagnostics.CompileSeconds);
        Result->SetObjectField(TEXT("compilationStatus"), CompilationStatus);

        TArray<TSharedPtr<FJsonValue>> NextSteps;
        NextSteps.Add(MakeShared<FJsonValueString>(TEXT("Use 'list-components-in-actor' to see the updated component hierarchy")));
        NextSteps.Add(MakeShared<FJsonValueString>(FString::Printf(
            TEXT("The component is a Blueprint variable: search-blueprint-nodes for 'Get %s' to read it in a graph"), *NewVariableName.ToString())));
        NextSteps.Add(MakeShared<FJsonValueString>(TEXT("Edit > Undo (Ctrl+Z) in the editor removes it again")));
        Result->SetArrayField(TEXT("nextSteps"), NextSteps);

        FString OutputString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
        FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);

        FN2CLogger::Get().Log(FString::Printf(TEXT("Added component '%s' of class '%s' to Blueprint '%s'"),
            *NewVariableName.ToString(), *ComponentClass->GetName(), *Blueprint->GetName()), EN2CLogSeverity::Info);

        return FMcpToolCallResult::CreateTextResult(OutputString);
    });
}

TSharedPtr<FJsonObject> FN2CMcpAddComponentClassToActorTool::BuildNumberProperty(const FString& Description, double DefaultValue) const
{
    TSharedPtr<FJsonObject> Prop = MakeShareable(new FJsonObject);
    Prop->SetStringField(TEXT("type"), TEXT("number"));
    Prop->SetStringField(TEXT("description"), Description);
    if (DefaultValue != 0.0)
    {
        Prop->SetNumberField(TEXT("default"), DefaultValue);
    }
    return Prop;
}