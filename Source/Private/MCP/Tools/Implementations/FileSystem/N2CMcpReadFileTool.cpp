// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpReadFileTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "Utils/N2CLogger.h"
#include "Utils/N2CPathUtils.h"
#include "Utils/N2CContentTypeUtils.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"

// Auto-register this tool
REGISTER_MCP_TOOL(FN2CMcpReadFileTool)

FMcpToolDefinition FN2CMcpReadFileTool::GetDefinition() const
{
    FMcpToolDefinition Definition(
        TEXT("read-file"),
        TEXT("Reads the contents of a file within the Unreal Engine project. "
             "Use empty string \"\" for project root, NOT \".\" or \"/\". "
             "Enforces security boundaries to prevent directory traversal outside the project. "
             "Supports text files up to 500KB in size. Binary files like .uasset and .umap are not supported."),
        TEXT("File System")
    );
    
    // Define input schema
    TMap<FString, FString> Properties;
    Properties.Add(TEXT("relativePath"), TEXT("string"));
    
    TArray<FString> Required;
    Required.Add(TEXT("relativePath"));
    
    Definition.InputSchema = BuildInputSchema(Properties, Required);
    
    // Read-only: reads editor or project state and changes nothing
    SetAnnotations(Definition, FMcpToolAnnotations::ReadOnly());
    
    return Definition;
}

FMcpToolCallResult FN2CMcpReadFileTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
    // Extract the relative path parameter
    FString RelativePath;
    if (!Arguments->TryGetStringField(TEXT("relativePath"), RelativePath))
    {
        return FMcpToolCallResult::CreateErrorResult(TEXT("Missing required parameter: relativePath"));
    }
    
    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Executing read-file tool with relativePath: %s"), *RelativePath),
        EN2CLogSeverity::Debug
    );
    
    // Execute on Game Thread for file system operations
    return ExecuteOnGameThread([this, RelativePath]() -> FMcpToolCallResult
    {
        // Part 1: Path Validation
        
        // Get the project directory as the base path
        FString BasePath = FPaths::ProjectDir();
        if (BasePath.IsEmpty())
        {
            return FMcpToolCallResult::CreateErrorResult(TEXT("Failed to determine project directory"));
        }
        
        // Combine with the requested relative path
        FString RequestedPath = FPaths::Combine(BasePath, RelativePath);
        
        // Validate the path stays within bounds
        FString NormalizedPath;
        if (!FN2CPathUtils::ValidatePathWithinBounds(BasePath, RequestedPath, NormalizedPath))
        {
            FN2CLogger::Get().LogWarning(
                FString::Printf(TEXT("Path traversal attempt blocked. Requested: %s, Base: %s"), 
                    *RequestedPath, *BasePath)
            );
            return FMcpToolCallResult::CreateErrorResult(TEXT("Access denied: Path traversal detected"));
        }
        
        // Use the normalized path for all operations
        RequestedPath = NormalizedPath;

        // Credentials are never readable through MCP, whatever the client asks.
        if (FN2CPathUtils::IsDeniedForAgentAccess(RequestedPath))
        {
            FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Blocked MCP access to a credential path: %s"), *RelativePath));
            return FMcpToolCallResult::CreateErrorResult(TEXT("Access denied: this path holds credentials and is not readable through MCP"));
        }
        
        // Check if the file exists
        if (!FPaths::FileExists(RequestedPath))
        {
            return FMcpToolCallResult::CreateErrorResult(
                FString::Printf(TEXT("File does not exist: %s"), *RelativePath)
            );
        }
        
        // Check if it's a binary Unreal file (not supported)
        FString Extension = FPaths::GetExtension(RequestedPath).ToLower();
        if (Extension == TEXT("uasset") || Extension == TEXT("umap"))
        {
            return FMcpToolCallResult::CreateErrorResult(
                TEXT("Binary files like .uasset and .umap are not supported")
            );
        }
        
        // Part 2: File Stats and Size Check
        
        // Get file info
        FFileStatData FileStats = IFileManager::Get().GetStatData(*RequestedPath);
        
        // Check file size limit
        if (FileStats.FileSize > MaxFileSize)
        {
            return FMcpToolCallResult::CreateErrorResult(
                FString::Printf(TEXT("File too large: %lld bytes (max: %lld)"), 
                    FileStats.FileSize, MaxFileSize)
            );
        }
        
        // Part 3: Read File Content
        
        // Read file content
        FString FileContent;
        if (!FFileHelper::LoadFileToString(FileContent, *RequestedPath))
        {
            FN2CLogger::Get().LogError(
                FString::Printf(TEXT("Failed to read file: %s"), *RequestedPath)
            );
            return FMcpToolCallResult::CreateErrorResult(TEXT("Failed to read file"));
        }
        
        // Part 4: Build Response
        
        // Detect content type based on extension (already have Extension from earlier check)
        FString ContentType = FN2CContentTypeUtils::GetContentTypeFromExtension(Extension);
        
        // Build JSON response
        TSharedPtr<FJsonObject> ResponseObject = MakeShareable(new FJsonObject);
        
        // Add metadata
        ResponseObject->SetStringField(TEXT("path"), RelativePath);
        ResponseObject->SetStringField(TEXT("absolutePath"), RequestedPath);
        ResponseObject->SetStringField(TEXT("content"), FileContent);
        ResponseObject->SetNumberField(TEXT("size"), FileStats.FileSize);
        ResponseObject->SetStringField(TEXT("contentType"), ContentType);
        ResponseObject->SetStringField(TEXT("extension"), Extension);
        ResponseObject->SetStringField(TEXT("lastModified"), 
            FileStats.ModificationTime.ToString(TEXT("%Y-%m-%d %H:%M:%S")));
        
        // Add success status
        ResponseObject->SetBoolField(TEXT("success"), true);
        
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Successfully read file: %s (Size: %lld bytes, Type: %s)"), 
                *RelativePath, FileStats.FileSize, *ContentType),
            EN2CLogSeverity::Info
        );
        
        // Convert JSON to string for the result
        FString OutputString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
        FJsonSerializer::Serialize(ResponseObject.ToSharedRef(), Writer);
        
        return FMcpToolCallResult::CreateTextResult(OutputString);
    });
}