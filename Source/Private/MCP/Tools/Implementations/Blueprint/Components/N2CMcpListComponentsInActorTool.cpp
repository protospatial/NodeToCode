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
        TEXT("Lists the components of an Actor Blueprint as a tree, as the Components panel shows them: native components of its C++ class, components its parent Blueprints add, and its own, with types and relative transforms"),
        N2CMcpToolsets::BlueprintComponent
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
    IncludeInheritedProp->SetStringField(TEXT("description"), TEXT("Include the native components of the C++ parent class and the components parent Blueprints add. When false, only this Blueprint's own components are listed"));
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
    
    // Read-only: reads editor or project state and changes nothing
    SetAnnotations(Definition, FMcpToolAnnotations::ReadOnly());
    
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

        // Read the hierarchy: native, inherited and own components, never creating anything (card 0167)
        FN2CComponentTree Tree;
        if (!FN2CMcpComponentUtils::BuildComponentTree(Blueprint, Tree, ErrorMsg))
        {
            return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
        }
        TSharedPtr<FJsonObject> Result = FN2CMcpComponentUtils::BuildComponentHierarchy(Tree, bIncludeInherited, ComponentTypeFilter);

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