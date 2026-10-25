// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpDeleteComponentInActorTool.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpComponentUtils.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "Utils/N2CLogger.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

REGISTER_MCP_TOOL(FN2CMcpDeleteComponentInActorTool)

FMcpToolDefinition FN2CMcpDeleteComponentInActorTool::GetDefinition() const
{
    FMcpToolDefinition Definition(
        TEXT("delete-component-in-actor"),
        TEXT("Deletes a component from a Blueprint actor. Its child components are deleted too, or kept and moved under the Blueprint's root component"),
        TEXT("Blueprint Components")
    );

    // Build input schema
    TSharedPtr<FJsonObject> Schema = MakeShareable(new FJsonObject);
    Schema->SetStringField(TEXT("type"), TEXT("object"));
    
    TSharedPtr<FJsonObject> Properties = MakeShareable(new FJsonObject);
    
    // componentName property
    TSharedPtr<FJsonObject> ComponentNameProp = MakeShareable(new FJsonObject);
    ComponentNameProp->SetStringField(TEXT("type"), TEXT("string"));
    ComponentNameProp->SetStringField(TEXT("description"), TEXT("Name of the component to delete"));
    Properties->SetObjectField(TEXT("componentName"), ComponentNameProp);

    // deleteChildren property
    TSharedPtr<FJsonObject> DeleteChildrenProp = MakeShareable(new FJsonObject);
    DeleteChildrenProp->SetStringField(TEXT("type"), TEXT("boolean"));
    DeleteChildrenProp->SetBoolField(TEXT("default"), false);
    DeleteChildrenProp->SetStringField(TEXT("description"), TEXT("If true, deletes all child components. If false, the children are kept and end up under the Blueprint's root component, not under the deleted component's parent"));
    Properties->SetObjectField(TEXT("deleteChildren"), DeleteChildrenProp);

    // blueprintPath property
    TSharedPtr<FJsonObject> BlueprintPathProp = MakeShareable(new FJsonObject);
    BlueprintPathProp->SetStringField(TEXT("type"), TEXT("string"));
    BlueprintPathProp->SetStringField(TEXT("description"), TEXT("Optional asset path of the Blueprint. If not provided, uses the currently focused Blueprint"));
    Properties->SetObjectField(TEXT("blueprintPath"), BlueprintPathProp);

    Schema->SetObjectField(TEXT("properties"), Properties);
    
    // Required fields
    TArray<TSharedPtr<FJsonValue>> RequiredArray;
    RequiredArray.Add(MakeShareable(new FJsonValueString(TEXT("componentName"))));
    Schema->SetArrayField(TEXT("required"), RequiredArray);
    
    Definition.InputSchema = Schema;
    
    return Definition;
}

FMcpToolCallResult FN2CMcpDeleteComponentInActorTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
    return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult
    {
        // Parse arguments
        FString ComponentName;
        if (!Arguments->TryGetStringField(TEXT("componentName"), ComponentName))
        {
            return FMcpToolCallResult::CreateErrorResult(TEXT("Missing required parameter: componentName"));
        }

        bool bDeleteChildren = false;
        Arguments->TryGetBoolField(TEXT("deleteChildren"), bDeleteChildren);

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

        // Get the Simple Construction Script
        USimpleConstructionScript* SCS = FN2CMcpComponentUtils::GetBlueprintSCS(Blueprint, ErrorMsg);
        if (!SCS)
        {
            return FMcpToolCallResult::CreateErrorResult(
                TEXT("NO_COMPONENTS|Blueprint has no components to delete"));
        }

        // Find the component to delete
        USCS_Node* NodeToDelete = FN2CMcpComponentUtils::FindSCSNodeByName(SCS, ComponentName);
        if (!NodeToDelete)
        {
            // Check if it's an inherited component
            TArray<USCS_Node*> InheritedNodes;
            FN2CMcpComponentUtils::GetInheritedSCSNodes(Blueprint, InheritedNodes);
            
            for (USCS_Node* InheritedNode : InheritedNodes)
            {
                if (InheritedNode && InheritedNode->GetVariableName() == *ComponentName)
                {
                    return FMcpToolCallResult::CreateErrorResult(
                        TEXT("INHERITED_COMPONENT|Cannot delete components inherited from parent Blueprint"));
                }
            }

            return FMcpToolCallResult::CreateErrorResult(
                FString::Printf(TEXT("COMPONENT_NOT_FOUND|Component '%s' not found in Blueprint"), *ComponentName));
        }

        // Build result with component info before deletion
        TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
        Result->SetStringField(TEXT("deletedComponentName"), ComponentName);
        Result->SetStringField(TEXT("deletedComponentClass"), 
            NodeToDelete->ComponentClass ? NodeToDelete->ComponentClass->GetName() : TEXT("Unknown"));
        Result->SetStringField(TEXT("deletedComponentGuid"), NodeToDelete->VariableGuid.ToString());

        // Get child information before deletion
        TArray<USCS_Node*> DirectChildren = NodeToDelete->GetChildNodes();
        TArray<TSharedPtr<FJsonValue>> AffectedChildren;
        
        if (DirectChildren.Num() > 0)
        {
            if (bDeleteChildren)
            {
                // Get all children that will be deleted
                TArray<USCS_Node*> AllChildren;
                FN2CMcpComponentUtils::GetAllChildNodes(NodeToDelete, AllChildren);
                
                for (USCS_Node* Child : AllChildren)
                {
                    TSharedPtr<FJsonObject> ChildInfo = MakeShareable(new FJsonObject);
                    ChildInfo->SetStringField(TEXT("name"), Child->GetVariableName().ToString());
                    ChildInfo->SetStringField(TEXT("action"), TEXT("deleted"));
                    AffectedChildren.Add(MakeShareable(new FJsonValueObject(ChildInfo)));
                }
            }
            else
            {
                // Children will be reparented
                FString NewParentName;
                if (!NodeToDelete->ParentComponentOrVariableName.IsNone())
                {
                    NewParentName = NodeToDelete->ParentComponentOrVariableName.ToString();
                }
                
                for (USCS_Node* Child : DirectChildren)
                {
                    TSharedPtr<FJsonObject> ChildInfo = MakeShareable(new FJsonObject);
                    ChildInfo->SetStringField(TEXT("name"), Child->GetVariableName().ToString());
                    ChildInfo->SetStringField(TEXT("action"), TEXT("reparented"));
                    if (!NewParentName.IsEmpty())
                    {
                        ChildInfo->SetStringField(TEXT("newParent"), NewParentName);
                    }
                    else
                    {
                        ChildInfo->SetStringField(TEXT("newParent"), TEXT("root"));
                    }
                    AffectedChildren.Add(MakeShareable(new FJsonValueObject(ChildInfo)));
                }
            }
        }
        
        Result->SetArrayField(TEXT("affectedChildren"), AffectedChildren);
        Result->SetBoolField(TEXT("childrenDeleted"), bDeleteChildren);

        // Perform the deletion
        if (!FN2CMcpComponentUtils::DeleteSCSNode(SCS, NodeToDelete, bDeleteChildren, ErrorMsg))
        {
            return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
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
        
        if (AffectedChildren.Num() > 0 && !bDeleteChildren)
        {
            NextSteps.Add(MakeShareable(new FJsonValueString(
                TEXT("Reparented children may need their transforms adjusted"))));
        }
        
        Result->SetArrayField(TEXT("nextSteps"), NextSteps);

        // Convert to string
        FString OutputString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
        FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);

        FN2CLogger::Get().Log(FString::Printf(
            TEXT("Deleted component '%s' from Blueprint '%s'%s"), 
            *ComponentName,
            *Blueprint->GetName(),
            bDeleteChildren ? TEXT(" (including children)") : TEXT("")), 
            EN2CLogSeverity::Info);

        return FMcpToolCallResult::CreateTextResult(OutputString);
    });
}