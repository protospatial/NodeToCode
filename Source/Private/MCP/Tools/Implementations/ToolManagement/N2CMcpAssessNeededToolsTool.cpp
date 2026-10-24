// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpAssessNeededToolsTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Tools/N2CMcpToolManager.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "Utils/N2CLogger.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// Auto-register this tool
REGISTER_MCP_TOOL(FN2CMcpAssessNeededToolsTool)

TMap<FString, FString> FN2CMcpAssessNeededToolsTool::GetCategoryDescriptions()
{
    return {
        { "Tool Management", "Tools for managing the available toolset." },
        { "Blueprint Discovery", "Tools for searching and listing Blueprints, functions, variables, and nodes." },
        { "Blueprint Graph Editing", "Tools for adding, connecting, and deleting nodes in a Blueprint graph." },
        { "Blueprint Function Management", "Tools for creating, deleting, and opening Blueprint functions." },
        { "Blueprint Variable Management", "Tools for creating member and local variables in Blueprints." },
        { "Blueprint Organization", "Tools for applying and managing tags on Blueprint graphs." },
        { "Content Browser", "Tools for interacting with the Unreal Engine Content Browser." },
        { "File System", "Tools for reading files and directories from the project's file system." },
        { "Translation", "Tools for translating Blueprints to code and managing LLM providers." }
    };
}

FMcpToolDefinition FN2CMcpAssessNeededToolsTool::GetDefinition() const
{
    FString Description = TEXT("Assesses and enables the required tool categories for a task. Provide a list of categories needed. The tool list is updated, but the client is not notified: call tools/list again to see the new tools. Available categories are:\n");
    for (const auto& Pair : GetCategoryDescriptions())
    {
        Description += FString::Printf(TEXT("- **%s**: %s\n"), *Pair.Key, *Pair.Value);
    }
    
    FMcpToolDefinition Definition(
        TEXT("assess-needed-tools"),
        Description
    );

    Definition.Category = TEXT("Tool Management");

    // Build input schema manually since we need an array type
    TSharedPtr<FJsonObject> Schema = MakeShareable(new FJsonObject);
    Schema->SetStringField(TEXT("type"), TEXT("object"));
    
    TSharedPtr<FJsonObject> Properties = MakeShareable(new FJsonObject);
    TSharedPtr<FJsonObject> CategoriesProp = MakeShareable(new FJsonObject);
    CategoriesProp->SetStringField(TEXT("type"), TEXT("array"));
    CategoriesProp->SetStringField(TEXT("description"), TEXT("A list of tool category names to enable."));
    
    TSharedPtr<FJsonObject> ItemsSchema = MakeShareable(new FJsonObject);
    ItemsSchema->SetStringField(TEXT("type"), TEXT("string"));
    CategoriesProp->SetObjectField(TEXT("items"), ItemsSchema);
    
    Properties->SetObjectField(TEXT("categories"), CategoriesProp);
    Schema->SetObjectField(TEXT("properties"), Properties);
    
    TArray<TSharedPtr<FJsonValue>> RequiredArray;
    RequiredArray.Add(MakeShareable(new FJsonValueString(TEXT("categories"))));
    Schema->SetArrayField(TEXT("required"), RequiredArray);

    Definition.InputSchema = Schema;
    
    return Definition;
}

FMcpToolCallResult FN2CMcpAssessNeededToolsTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
    return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult
    {
        FN2CMcpArgumentParser ArgParser(Arguments);
        FString ErrorMsg;

        const TArray<TSharedPtr<FJsonValue>>* CategoriesArray;
        if (!ArgParser.TryGetRequiredArray(TEXT("categories"), CategoriesArray, ErrorMsg))
        {
            return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
        }

        TArray<FString> Categories;
        for (const auto& Value : *CategoriesArray)
        {
            FString Category;
            if (Value->TryGetString(Category))
            {
                Categories.Add(Category);
            }
        }

        FN2CMcpToolManager::Get().UpdateActiveTools(Categories);

        FString EnabledCategories = FString::Join(Categories, TEXT(", "));
        FString Message = FString::Printf(TEXT("Successfully enabled tools for categories: %s. The tool list has been updated; call tools/list again to see the new tools."), *EnabledCategories);
        
        if (Categories.Num() == 0)
        {
            Message = TEXT("Tool set has been reset to default. Only assess-needed-tools is available.");
        }

        FN2CLogger::Get().Log(Message, EN2CLogSeverity::Info);
        return FMcpToolCallResult::CreateTextResult(Message);
    });
}