// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpGetTranslationOutputDirectoryTool.h"
#include "MCP/Tools/N2CMcpOutputSchema.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "Core/N2CSettings.h"
#include "Utils/N2CPathUtils.h"
#include "Utils/N2CLogger.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/Paths.h"

// Auto-register this tool
REGISTER_MCP_TOOL(FN2CMcpGetTranslationOutputDirectoryTool)

FMcpToolDefinition FN2CMcpGetTranslationOutputDirectoryTool::GetDefinition() const
{
    FMcpToolDefinition Definition(
        TEXT("get-translation-output-directory"),
        TEXT("Returns the translation output directory configuration from NodeToCode settings. Shows whether a custom directory is set or if the default location is being used. ")
        TEXT("Every translation is saved there as a run folder with a manifest.json: from the editor's NodeToCode window, translate-batch, translate-focused-blueprint and the Python API."),
        N2CMcpToolsets::Translation
    );
    
    // This tool takes no input parameters
    Definition.InputSchema = BuildEmptyObjectSchema();
    
    {
        using namespace N2CMcpOutputSchema;
        Definition.OutputSchema = Object({
            { TEXT("isCustomDirectorySet"), Boolean() },
            { TEXT("customDirectory"), String(TEXT("The Project Settings path, when one is set")) },
            { TEXT("defaultDirectory"), String(TEXT("<Project>/Saved/NodeToCode/Translations (absolute), when no custom directory is set")) },
            { TEXT("absolutePath"), String(TEXT("The directory translations are saved to")) },
            { TEXT("directoryExists"), Boolean() },
            { TEXT("status"), String() },
            { TEXT("projectDirectory"), String() },
            { TEXT("projectSavedDirectory"), String() },
            { TEXT("usage"), Object({
                { TEXT("description"), String() },
                { TEXT("structure"), String() },
                { TEXT("autoCreateIfMissing"), Boolean() },
            }) },
        }, { TEXT("isCustomDirectorySet"), TEXT("absolutePath"), TEXT("directoryExists") });
    }

    // Read-only: reads editor or project state and changes nothing
    SetAnnotations(Definition, FMcpToolAnnotations::ReadOnly());
    
    return Definition;
}

FMcpToolCallResult FN2CMcpGetTranslationOutputDirectoryTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
    FN2CLogger::Get().Log(TEXT("Executing get-translation-output-directory tool"), EN2CLogSeverity::Debug);
    
    // Execute on Game Thread to access settings
    return ExecuteOnGameThread([this]() -> FMcpToolCallResult
    {
        // Get current settings
        const UN2CSettings* Settings = GetDefault<UN2CSettings>();
        if (!Settings)
        {
            FN2CLogger::Get().LogError(TEXT("Failed to get NodeToCode settings"));
            return FMcpToolCallResult::CreateErrorResult(TEXT("Failed to retrieve plugin settings"));
        }
        
        // Build the response JSON
        TSharedPtr<FJsonObject> ResponseObject = MakeShareable(new FJsonObject);
        
        // The directory the translation archive writes to (FN2CPathUtils::GetTranslationsBasePath, absolute)
        const FString AbsolutePath = FN2CPathUtils::GetTranslationsBasePath();
        const bool bIsCustomDirectorySet = !Settings->CustomTranslationOutputDirectory.Path.TrimStartAndEnd().IsEmpty();
        const bool bDirectoryExists = FPaths::DirectoryExists(AbsolutePath);
        ResponseObject->SetBoolField(TEXT("isCustomDirectorySet"), bIsCustomDirectorySet);
        ResponseObject->SetStringField(TEXT("absolutePath"), AbsolutePath);
        ResponseObject->SetBoolField(TEXT("directoryExists"), bDirectoryExists);
        if (bIsCustomDirectorySet)
        {
            ResponseObject->SetStringField(TEXT("customDirectory"), Settings->CustomTranslationOutputDirectory.Path);
            ResponseObject->SetStringField(TEXT("status"), bDirectoryExists
                ? TEXT("Custom directory is set and exists")
                : TEXT("Custom directory is set but does not exist (will be created when needed)"));
        }
        else
        {
            ResponseObject->SetStringField(TEXT("defaultDirectory"), FN2CPathUtils::GetDefaultTranslationsBasePath());
            ResponseObject->SetStringField(TEXT("status"), TEXT("Using default directory"));
        }
        
        // Add information about the project directory for context
        ResponseObject->SetStringField(TEXT("projectDirectory"), FPaths::GetProjectFilePath());
        ResponseObject->SetStringField(TEXT("projectSavedDirectory"), FPaths::ProjectSavedDir());
        
        // Add usage information
        TSharedPtr<FJsonObject> UsageObject = MakeShareable(new FJsonObject);
        UsageObject->SetStringField(TEXT("description"), TEXT("This directory is where NodeToCode saves all translation outputs including generated code files and N2C JSON files"));
        UsageObject->SetStringField(TEXT("structure"), TEXT("Each translation run creates {BlueprintName}_{yyyyMMdd-HHmmss-fff}_{runId} (a batch: BatchTranslation_...) holding manifest.json ")
            TEXT("(kind, provider, model, language, usage, and every entry with its role and files) and one folder per translated graph, struct or enum named after it ")
            TEXT("({Graph}/{Graph}.cpp, .h, _Notes.txt); a batch puts each item in its own folder first ({Item}/{Graph}/...)"));
        UsageObject->SetBoolField(TEXT("autoCreateIfMissing"), true);
        ResponseObject->SetObjectField(TEXT("usage"), UsageObject);
        
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Translation output directory: %s"), 
                bIsCustomDirectorySet ? TEXT("Custom directory configured") : TEXT("Using default directory")), 
            EN2CLogSeverity::Info
        );
        
        return FMcpToolCallResult::CreateJsonResult(ResponseObject.ToSharedRef(), /*bIndented*/ true);
    });
}