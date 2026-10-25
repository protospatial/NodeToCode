// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpListComponentsInActorTool.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpComponentUtils.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "Utils/N2CLogger.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "GameFramework/Actor.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

REGISTER_MCP_TOOL(FN2CMcpListComponentsInActorTool)

FMcpToolDefinition FN2CMcpListComponentsInActorTool::GetDefinition() const
{
    FMcpToolDefinition Definition(
        TEXT("list-components-in-actor"),
        TEXT("Lists all components in a Blueprint actor with their hierarchy, types, and properties"),
        TEXT("Blueprint Components")
    );

    // Build input schema
    TSharedPtr<FJsonObject> Schema = MakeShareable(new FJsonObject);
    Schema->SetStringField(TEXT("type"), TEXT("object"));
    
    TSharedPtr<FJsonObject> Properties = MakeShareable(new FJsonObject);
    
    // blueprintPath property
    TSharedPtr<FJsonObject> BlueprintPathProp = MakeShareable(new FJsonObject);
    BlueprintPathProp->SetStringField(TEXT("type"), TEXT("string"));
    BlueprintPathProp->SetStringField(TEXT("description"), TEXT("Optional asset path of the Blueprint. If not provided, uses the currently focused Blueprint"));
    Properties->SetObjectField(TEXT("blueprintPath"), BlueprintPathProp);

    // includeInherited property
    TSharedPtr<FJsonObject> IncludeInheritedProp = MakeShareable(new FJsonObject);
    IncludeInheritedProp->SetStringField(TEXT("type"), TEXT("boolean"));
    IncludeInheritedProp->SetBoolField(TEXT("default"), true);
    IncludeInheritedProp->SetStringField(TEXT("description"), TEXT("Include components added by parent Blueprints. Native components of a C++ parent are never listed"));
    Properties->SetObjectField(TEXT("includeInherited"), IncludeInheritedProp);

    // componentTypeFilter property
    TSharedPtr<FJsonObject> TypeFilterProp = MakeShareable(new FJsonObject);
    TypeFilterProp->SetStringField(TEXT("type"), TEXT("string"));
    TArray<TSharedPtr<FJsonValue>> TypeFilterEnum;
    TypeFilterEnum.Add(MakeShareable(new FJsonValueString(TEXT("all"))));
    TypeFilterEnum.Add(MakeShareable(new FJsonValueString(TEXT("scene"))));
    TypeFilterEnum.Add(MakeShareable(new FJsonValueString(TEXT("actor"))));
    TypeFilterEnum.Add(MakeShareable(new FJsonValueString(TEXT("primitive"))));
    TypeFilterProp->SetArrayField(TEXT("enum"), TypeFilterEnum);
    TypeFilterProp->SetStringField(TEXT("default"), TEXT("all"));
    TypeFilterProp->SetStringField(TEXT("description"), TEXT("Filter by component type"));
    Properties->SetObjectField(TEXT("componentTypeFilter"), TypeFilterProp);

    Schema->SetObjectField(TEXT("properties"), Properties);
    
    // No required fields
    TArray<TSharedPtr<FJsonValue>> RequiredArray;
    Schema->SetArrayField(TEXT("required"), RequiredArray);
    
    Definition.InputSchema = Schema;
    
    // Add read-only annotation
    AddReadOnlyAnnotation(Definition);
    
    return Definition;
}

FMcpToolCallResult FN2CMcpListComponentsInActorTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
    return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult
    {
        // Parse arguments
        FString BlueprintPath;
        Arguments->TryGetStringField(TEXT("blueprintPath"), BlueprintPath);

        bool bIncludeInherited = true;
        Arguments->TryGetBoolField(TEXT("includeInherited"), bIncludeInherited);

        FString ComponentTypeFilter = TEXT("all");
        Arguments->TryGetStringField(TEXT("componentTypeFilter"), ComponentTypeFilter);

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
            // It's okay if there's no SCS - it might just not have any components
            TSharedPtr<FJsonObject> EmptyResult = MakeShareable(new FJsonObject);
            TArray<TSharedPtr<FJsonValue>> EmptyArray;
            EmptyResult->SetArrayField(TEXT("components"), EmptyArray);
            EmptyResult->SetNumberField(TEXT("totalCount"), 0);
            EmptyResult->SetStringField(TEXT("rootComponent"), TEXT(""));
            
            FString OutputString;
            TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
            FJsonSerializer::Serialize(EmptyResult.ToSharedRef(), Writer);
            
            return FMcpToolCallResult::CreateTextResult(OutputString);
        }

        // Get root nodes
        const TArray<USCS_Node*>& RootNodes = SCS->GetRootNodes();
        
        // Build component hierarchy
        TSharedPtr<FJsonObject> Result = FN2CMcpComponentUtils::BuildComponentHierarchy(
            Blueprint, RootNodes, bIncludeInherited, ComponentTypeFilter);

        // Add Blueprint info
        Result->SetStringField(TEXT("blueprintName"), Blueprint->GetName());
        Result->SetStringField(TEXT("blueprintPath"), Blueprint->GetPathName());

        // Convert to string
        FString OutputString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
        FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);

        int32 TotalCount = 0;
        Result->TryGetNumberField(TEXT("totalCount"), TotalCount);
        
        FN2CLogger::Get().Log(FString::Printf(
            TEXT("Listed %d components in Blueprint '%s'"), 
            TotalCount,
            *Blueprint->GetName()), EN2CLogSeverity::Info);

        return FMcpToolCallResult::CreateTextResult(OutputString);
    });
}