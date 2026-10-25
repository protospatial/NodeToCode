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
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UObjectGlobals.h"

REGISTER_MCP_TOOL(FN2CMcpAddComponentClassToActorTool)

FMcpToolDefinition FN2CMcpAddComponentClassToActorTool::GetDefinition() const
{
    FMcpToolDefinition Definition(
        TEXT("add-component-class-to-actor"),
        TEXT("Adds a component of a specified class to a Blueprint actor. For scene components, supports parent attachment and relative transform"),
        TEXT("Blueprint Components")
    );

    // Build input schema
    TSharedPtr<FJsonObject> Schema = MakeShareable(new FJsonObject);
    Schema->SetStringField(TEXT("type"), TEXT("object"));
    
    TSharedPtr<FJsonObject> Properties = MakeShareable(new FJsonObject);
    
    // componentClass property
    TSharedPtr<FJsonObject> ComponentClassProp = MakeShareable(new FJsonObject);
    ComponentClassProp->SetStringField(TEXT("type"), TEXT("string"));
    ComponentClassProp->SetStringField(TEXT("description"), TEXT("Class path of the component to add (e.g., '/Script/Engine.StaticMeshComponent')"));
    Properties->SetObjectField(TEXT("componentClass"), ComponentClassProp);

    // componentName property
    TSharedPtr<FJsonObject> ComponentNameProp = MakeShareable(new FJsonObject);
    ComponentNameProp->SetStringField(TEXT("type"), TEXT("string"));
    ComponentNameProp->SetStringField(TEXT("description"), TEXT("Optional custom name for the component. Auto-generated if not provided"));
    Properties->SetObjectField(TEXT("componentName"), ComponentNameProp);

    // parentComponent property
    TSharedPtr<FJsonObject> ParentComponentProp = MakeShareable(new FJsonObject);
    ParentComponentProp->SetStringField(TEXT("type"), TEXT("string"));
    ParentComponentProp->SetStringField(TEXT("description"), TEXT("Name of a component this Blueprint added (not a native or inherited one) to attach to, scene components only. After the compile the new component sits under the Blueprint's root component, so only the root is kept as a parent"));
    Properties->SetObjectField(TEXT("parentComponent"), ParentComponentProp);

    // attachSocketName property
    TSharedPtr<FJsonObject> AttachSocketProp = MakeShareable(new FJsonObject);
    AttachSocketProp->SetStringField(TEXT("type"), TEXT("string"));
    AttachSocketProp->SetStringField(TEXT("description"), TEXT("Socket name to attach to on parent component"));
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

        FString ParentComponentName;
        Arguments->TryGetStringField(TEXT("parentComponent"), ParentComponentName);

        FString AttachSocketName;
        Arguments->TryGetStringField(TEXT("attachSocketName"), AttachSocketName);

        const TSharedPtr<FJsonObject>* TransformJson = nullptr;
        Arguments->TryGetObjectField(TEXT("relativeTransform"), TransformJson);

        FString BlueprintPath;
        Arguments->TryGetStringField(TEXT("blueprintPath"), BlueprintPath);

        // Resolve the Blueprint
        FString ErrorMsg;
        UBlueprint* Blueprint = FN2CMcpBlueprintUtils::ResolveBlueprint(BlueprintPath, ErrorMsg);
        if (!Blueprint)
        {
            return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
        }

        // Verify it's an actor Blueprint
        if (!Blueprint->ParentClass || !Blueprint->ParentClass->IsChildOf(AActor::StaticClass()))
        {
            return FMcpToolCallResult::CreateErrorResult(
                TEXT("NOT_ACTOR_BLUEPRINT|Blueprint must derive from Actor to have components"));
        }

        // Resolve the component class
        UClass* ComponentClass = ResolveComponentClass(ComponentClassPath, ErrorMsg);
        if (!ComponentClass)
        {
            return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
        }

        // Get the Simple Construction Script
        USimpleConstructionScript* SCS = FN2CMcpComponentUtils::GetBlueprintSCS(Blueprint, ErrorMsg);
        if (!SCS)
        {
            return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
        }

        // Create the SCS node
        USCS_Node* NewNode = FN2CMcpComponentUtils::CreateSCSNode(SCS, ComponentClass, ComponentName, ErrorMsg);
        if (!NewNode)
        {
            return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
        }

        // Get the actual component name that was generated
        FString ActualComponentName = NewNode->GetVariableName().ToString();

        // Handle attachment for scene components
        bool bIsSceneComponent = ComponentClass->IsChildOf(USceneComponent::StaticClass());
        if (bIsSceneComponent)
        {
            // Find parent node if specified
            if (!ParentComponentName.IsEmpty())
            {
                USCS_Node* ParentNode = FN2CMcpComponentUtils::FindSCSNodeByName(SCS, ParentComponentName);
                if (!ParentNode)
                {
                    return FMcpToolCallResult::CreateErrorResult(
                        FString::Printf(TEXT("PARENT_NOT_FOUND|Parent component '%s' not found"), *ParentComponentName));
                }

                // Set up attachment
                if (!FN2CMcpComponentUtils::SetupComponentAttachment(NewNode, ParentNode, AttachSocketName, ErrorMsg))
                {
                    return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
                }
            }
            else if (SCS->GetRootNodes().Num() > 0)
            {
                // If no parent specified but there's a root component, attach to it
                USCS_Node* RootNode = SCS->GetRootNodes()[0];
                if (RootNode && RootNode != NewNode)
                {
                    FN2CMcpComponentUtils::SetupComponentAttachment(NewNode, RootNode, TEXT(""), ErrorMsg);
                }
            }

            // Apply transform if provided
            if (TransformJson && TransformJson->IsValid())
            {
                USceneComponent* SceneTemplate = Cast<USceneComponent>(NewNode->ComponentTemplate);
                if (SceneTemplate)
                {
                    FN2CMcpComponentUtils::ApplyTransformToComponent(SceneTemplate, *TransformJson, ErrorMsg);
                }
            }
        }

        // Structural change: mark it, then compile once now (no transaction is open here) so the result reports what
        // the compiler said (card 0151)
        FN2CMcpBlueprintUtils::MarkModified(Blueprint, /*bStructural*/ true);
        FN2CCompileDiagnostics Diagnostics;
        FN2CMcpBlueprintUtils::Compile(Blueprint, Diagnostics);
        const int32 ErrorCount = Diagnostics.ErrorCount;
        const int32 WarningCount = Diagnostics.WarningCount;
        const float CompilationTime = Diagnostics.CompileSeconds;
        const bool bCompileSuccess = Diagnostics.bSuccess;

        // Build result JSON
        TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
        Result->SetStringField(TEXT("componentName"), ActualComponentName);
        Result->SetStringField(TEXT("componentClass"), ComponentClass->GetName());
        Result->SetStringField(TEXT("componentClassPath"), ComponentClass->GetPathName());
        Result->SetStringField(TEXT("nodeGuid"), NewNode->VariableGuid.ToString());
        Result->SetBoolField(TEXT("isSceneComponent"), bIsSceneComponent);
        
        if (!ParentComponentName.IsEmpty())
        {
            Result->SetStringField(TEXT("attachedTo"), ParentComponentName);
            if (!AttachSocketName.IsEmpty())
            {
                Result->SetStringField(TEXT("attachSocketName"), AttachSocketName);
            }
        }

        // Add compilation status
        TSharedPtr<FJsonObject> CompilationStatus = MakeShareable(new FJsonObject);
        CompilationStatus->SetBoolField(TEXT("success"), bCompileSuccess);
        CompilationStatus->SetNumberField(TEXT("errorCount"), ErrorCount);
        CompilationStatus->SetNumberField(TEXT("warningCount"), WarningCount);
        CompilationStatus->SetNumberField(TEXT("compilationTime"), CompilationTime);
        Result->SetObjectField(TEXT("compilationStatus"), CompilationStatus);

        // Add next steps suggestions
        TArray<TSharedPtr<FJsonValue>> NextSteps;
        NextSteps.Add(MakeShareable(new FJsonValueString(
            TEXT("Use 'list-components-in-actor' to see the updated component hierarchy"))));
        
        if (bIsSceneComponent)
        {
            NextSteps.Add(MakeShareable(new FJsonValueString(
                FString::Printf(TEXT("Use 'create-variable' with typeIdentifier '%s' to create a component reference variable"), 
                *ComponentClass->GetPathName()))));
        }
        
        NextSteps.Add(MakeShareable(new FJsonValueString(
            TEXT("Use 'add-bp-node-to-active-graph' to add nodes that interact with this component"))));
        
        Result->SetArrayField(TEXT("nextSteps"), NextSteps);

        // Convert to string
        FString OutputString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
        FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);

        FN2CLogger::Get().Log(FString::Printf(
            TEXT("Added component '%s' of class '%s' to Blueprint '%s'"), 
            *ActualComponentName,
            *ComponentClass->GetName(),
            *Blueprint->GetName()), EN2CLogSeverity::Info);

        return FMcpToolCallResult::CreateTextResult(OutputString);
    });
}

UClass* FN2CMcpAddComponentClassToActorTool::ResolveComponentClass(const FString& ClassPath, FString& OutErrorMsg) const
{
    if (ClassPath.IsEmpty())
    {
        OutErrorMsg = TEXT("EMPTY_CLASS_PATH|Component class path is empty");
        return nullptr;
    }

    // Try to find the class
    UClass* ComponentClass = nullptr;

    // First try as a direct path
    ComponentClass = FindObject<UClass>(nullptr, *ClassPath);
    
    // If not found, try loading it
    if (!ComponentClass)
    {
        ComponentClass = LoadObject<UClass>(nullptr, *ClassPath);
    }

    // If still not found, try with different formats
    if (!ComponentClass)
    {
        // Try without /Script/ prefix
        FString ClassNameOnly = ClassPath;
        if (ClassPath.StartsWith(TEXT("/Script/")))
        {
            ClassNameOnly = ClassPath.RightChop(8);
            int32 DotIndex;
            if (ClassNameOnly.FindChar('.', DotIndex))
            {
                FString ModuleName = ClassNameOnly.Left(DotIndex);
                FString ClassName = ClassNameOnly.RightChop(DotIndex + 1);
                
                // Try to find by class name
                ComponentClass = FindObject<UClass>(nullptr, *ClassName);
            }
        }
    }

    if (!ComponentClass)
    {
        OutErrorMsg = FString::Printf(TEXT("CLASS_NOT_FOUND|Component class '%s' not found"), *ClassPath);
        return nullptr;
    }

    // Validate the class
    if (!FN2CMcpComponentUtils::ValidateComponentClass(ComponentClass, OutErrorMsg))
    {
        return nullptr;
    }

    return ComponentClass;
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