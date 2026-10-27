// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpGetTranslationOutputDirectoryTool.h"
#include "MCP/Tools/N2CMcpOutputSchema.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "Core/N2CSettings.h"
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
        TEXT("Translations run from the editor's NodeToCode window are saved there; translate-focused-blueprint returns its translation in the tool result and writes no files."),
        N2CMcpToolsets::Translation
    );
    
    // This tool takes no input parameters
    Definition.InputSchema = BuildEmptyObjectSchema();
    
    {
        using namespace N2CMcpOutputSchema;
        Definition.OutputSchema = Object({
            { TEXT("isCustomDirectorySet"), Boolean() },
            { TEXT("customDirectory"), String(TEXT("The Project Settings path, when one is set")) },
            { TEXT("defaultDirectory"), String(TEXT("Saved/NodeToCode/Translations, when no custom directory is set")) },
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
        
        // Check if custom directory is set
        bool bIsCustomDirectorySet = !Settings->CustomTranslationOutputDirectory.Path.IsEmpty();
        ResponseObject->SetBoolField(TEXT("isCustomDirectorySet"), bIsCustomDirectorySet);
        
        if (bIsCustomDirectorySet)
        {
            // Custom directory is set
            FString CustomPath = Settings->CustomTranslationOutputDirectory.Path;
            ResponseObject->SetStringField(TEXT("customDirectory"), CustomPath);
            
            // Check if the directory exists
            bool bDirectoryExists = FPaths::DirectoryExists(CustomPath);
            ResponseObject->SetBoolField(TEXT("directoryExists"), bDirectoryExists);
            
            // Convert to absolute path if relative
            FString AbsolutePath = FPaths::ConvertRelativePathToFull(CustomPath);
            ResponseObject->SetStringField(TEXT("absolutePath"), AbsolutePath);
            
            // Add directory status message
            if (bDirectoryExists)
            {
                ResponseObject->SetStringField(TEXT("status"), TEXT("Custom directory is set and exists"));
            }
            else
            {
                ResponseObject->SetStringField(TEXT("status"), TEXT("Custom directory is set but does not exist (will be created when needed)"));
            }
        }
        else
        {
            // Using default directory
            FString DefaultPath = FPaths::ProjectSavedDir() / TEXT("NodeToCode") / TEXT("Translations");
            ResponseObject->SetStringField(TEXT("defaultDirectory"), DefaultPath);
            
            // Convert to absolute path
            FString AbsolutePath = FPaths::ConvertRelativePathToFull(DefaultPath);
            ResponseObject->SetStringField(TEXT("absolutePath"), AbsolutePath);
            
            // Check if the default directory exists
            bool bDirectoryExists = FPaths::DirectoryExists(DefaultPath);
            ResponseObject->SetBoolField(TEXT("directoryExists"), bDirectoryExists);
            
            ResponseObject->SetStringField(TEXT("status"), TEXT("Using default directory"));
        }
        
        // Add information about the project directory for context
        ResponseObject->SetStringField(TEXT("projectDirectory"), FPaths::GetProjectFilePath());
        ResponseObject->SetStringField(TEXT("projectSavedDirectory"), FPaths::ProjectSavedDir());
        
        // Add usage information
        TSharedPtr<FJsonObject> UsageObject = MakeShareable(new FJsonObject);
        UsageObject->SetStringField(TEXT("description"), TEXT("This directory is where NodeToCode saves all translation outputs including generated code files and N2C JSON files"));
        UsageObject->SetStringField(TEXT("structure"), TEXT("Each translation creates a timestamped subdirectory: {BlueprintName}_{YYYY-MM-DD-HH.MM.SS}"));
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