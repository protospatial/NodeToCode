// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpReadPathTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "Utils/N2CLogger.h"
#include "Utils/N2CPathUtils.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Misc/Paths.h"

// Auto-register this tool
REGISTER_MCP_TOOL(FN2CMcpReadPathTool)

FMcpToolDefinition FN2CMcpReadPathTool::GetDefinition() const
{
    FMcpToolDefinition Definition(
        TEXT("read-path"),
        TEXT("Lists all files and folders in a directory within the Unreal Engine project. "
             "Use empty string \"\" for project root, NOT \".\" or \"/\". "
             "Examples: \"\" for root, \"Config\" for Config folder, \"Content/Blueprints\" for nested paths. "
             "Enforces security boundaries to prevent directory traversal outside the project."),
        N2CMcpToolsets::FileSystem
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

FMcpToolCallResult FN2CMcpReadPathTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
    // Extract the relative path parameter
    FString RelativePath;
    if (!Arguments->TryGetStringField(TEXT("relativePath"), RelativePath))
    {
        return FMcpToolCallResult::CreateErrorResult(TEXT("Missing required parameter: relativePath"));
    }
    
    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Executing read-path tool with relativePath: %s"), *RelativePath),
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
        // Note: FPaths::Combine correctly handles empty string as the relative path,
        // returning just the base path. Using "." would create an invalid path.
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
        
        // Check if the directory exists
        if (!FPaths::DirectoryExists(RequestedPath))
        {
            return FMcpToolCallResult::CreateErrorResult(
                FString::Printf(TEXT("Directory does not exist: %s"), *RelativePath)
            );
        }
        
        // Part 2: Directory Iteration
        
        TArray<FString> Files;
        TArray<FString> Directories;
        
        // Use IPlatformFile for directory iteration
        IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
        
        // Define a visitor class for directory iteration
        class FDirectoryVisitor : public IPlatformFile::FDirectoryVisitor
        {
        public:
            TArray<FString>& FilesRef;
            TArray<FString>& DirectoriesRef;
            
            FDirectoryVisitor(TArray<FString>& InFiles, TArray<FString>& InDirectories)
                : FilesRef(InFiles)
                , DirectoriesRef(InDirectories)
            {
            }
            
            virtual bool Visit(const TCHAR* FilenameOrDirectory, bool bIsDirectory) override
            {
                FString ItemName = FPaths::GetCleanFilename(FilenameOrDirectory);
                
                // Skip . and .. entries
                if (ItemName == TEXT(".") || ItemName == TEXT("..") || ItemName.IsEmpty())
                {
                    return true; // Continue iteration
                }
                
                if (bIsDirectory)
                {
                    DirectoriesRef.Add(ItemName);
                }
                else
                {
                    FilesRef.Add(ItemName);
                }
                
                return true; // Continue iteration
            }
        };
        
        // Create visitor and iterate directory
        FDirectoryVisitor Visitor(Files, Directories);
        PlatformFile.IterateDirectory(*RequestedPath, Visitor);
        
        // Log for debugging
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Directory scan of '%s' found %d files and %d directories"), 
                *RequestedPath, Files.Num(), Directories.Num()),
            EN2CLogSeverity::Info
        );
        
        // Sort arrays for consistent output
        Files.Sort();
        Directories.Sort();
        
        // Part 3: Build JSON Response
        
        TSharedPtr<FJsonObject> ResponseObject = MakeShareable(new FJsonObject);
        
        // Add metadata
        ResponseObject->SetStringField(TEXT("path"), RelativePath);
        ResponseObject->SetStringField(TEXT("absolutePath"), RequestedPath);
        ResponseObject->SetNumberField(TEXT("fileCount"), Files.Num());
        ResponseObject->SetNumberField(TEXT("directoryCount"), Directories.Num());
        
        // Add files array
        TArray<TSharedPtr<FJsonValue>> FilesArray;
        for (const FString& FileName : Files)
        {
            FilesArray.Add(MakeShareable(new FJsonValueString(FileName)));
        }
        ResponseObject->SetArrayField(TEXT("files"), FilesArray);
        
        // Add directories array
        TArray<TSharedPtr<FJsonValue>> DirectoriesArray;
        for (const FString& DirectoryName : Directories)
        {
            DirectoriesArray.Add(MakeShareable(new FJsonValueString(DirectoryName)));
        }
        ResponseObject->SetArrayField(TEXT("directories"), DirectoriesArray);
        
        // Add success status
        ResponseObject->SetBoolField(TEXT("success"), true);
        
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Listed directory: %s (Files: %d, Directories: %d)"), 
                *RelativePath, Files.Num(), Directories.Num()),
            EN2CLogSeverity::Info
        );
        
        // Convert JSON to string for the result
        FString OutputString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
        FJsonSerializer::Serialize(ResponseObject.ToSharedRef(), Writer);
        
        return FMcpToolCallResult::CreateTextResult(OutputString);
    });
}

