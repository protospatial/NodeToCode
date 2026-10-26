// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpReadPathTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "Utils/N2CLogger.h"
#include "Utils/N2CPathUtils.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "HAL/PlatformFileManager.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Misc/Paths.h"

// Auto-register this tool
REGISTER_MCP_TOOL(FN2CMcpReadPathTool)

namespace N2CReadPath
{
    /** Reads an optional whole number (a JSON number or a numeric string) in [Min, Max]; absent gives Default */
    bool ReadOptionalInt(const TSharedPtr<FJsonObject>& Arguments, const TCHAR* Name, int32 Default, int32 Min, int32 Max, int32& OutValue, FString& OutError)
    {
        OutValue = Default;
        const TSharedPtr<FJsonValue> Value = Arguments->TryGetField(Name);
        if (!Value.IsValid() || Value->IsNull())
        {
            return true;
        }
        double Number = 0.0;
        FString Text;
        if (Value->TryGetString(Text))
        {
            Text.TrimStartAndEndInline();
            if (!Text.IsNumeric())
            {
                OutError = FString::Printf(TEXT("%s must be a whole number from %d to %d"), Name, Min, Max);
                return false;
            }
            Number = FCString::Atod(*Text);
        }
        else if (!Value->TryGetNumber(Number))
        {
            OutError = FString::Printf(TEXT("%s must be a whole number from %d to %d"), Name, Min, Max);
            return false;
        }
        if (Number != FMath::FloorToDouble(Number) || Number < Min || Number > Max)
        {
            OutError = FString::Printf(TEXT("%s must be a whole number from %d to %d"), Name, Min, Max);
            return false;
        }
        OutValue = static_cast<int32>(Number);
        return true;
    }

    /** A cursor is the index of the first entry of a page, as a decimal string; "" is the first page */
    bool ParseCursor(const FString& Cursor, int32& OutStart)
    {
        OutStart = 0;
        const FString Trimmed = Cursor.TrimStartAndEnd();
        if (Trimmed.IsEmpty())
        {
            return true;
        }
        for (const TCHAR C : Trimmed)
        {
            if (C < TEXT('0') || C > TEXT('9'))
            {
                return false;
            }
        }
        if (Trimmed.Len() > 9)
        {
            return false;
        }
        OutStart = FCString::Atoi(*Trimmed);
        return true;
    }
}

FMcpToolDefinition FN2CMcpReadPathTool::GetDefinition() const
{
    FMcpToolDefinition Definition(
        TEXT("read-path"),
        TEXT("Lists the files and folders directly inside a directory of the Unreal Engine project (not recursive), or of the "
             "translation output directory when that is outside the project. relativePath is relative to the project folder: "
             "\"\" or \".\" for the project folder, \"Config\", \"Content/Blueprints\"; an absolute path is accepted only inside those two folders. "
             "Results come in pages, folders first and then files, each sorted by name: total counts every entry, "
             "and when truncated is true pass nextCursor as cursor for the next page. filter keeps the names that match a wildcard such as \"*.json\". "
             "Credential folders (Saved/NodeToCode/User, .git) cannot be listed."),
        N2CMcpToolsets::FileSystem
    );

    TMap<FString, FString> Properties;
    Properties.Add(TEXT("relativePath"), TEXT("string"));
    Properties.Add(TEXT("pageSize"), TEXT("integer"));
    Properties.Add(TEXT("cursor"), TEXT("string"));
    Properties.Add(TEXT("filter"), TEXT("string"));

    TMap<FString, FString> Descriptions;
    Descriptions.Add(TEXT("relativePath"), TEXT("The directory, relative to the project folder (\"\" for the project folder); an absolute path must be inside the project or the translation output directory"));
    Descriptions.Add(TEXT("pageSize"), FString::Printf(TEXT("Optional. Entries per page, 1 to %d (default %d)"), MaxPageSize, DefaultPageSize));
    Descriptions.Add(TEXT("cursor"), TEXT("Optional. The nextCursor of the previous page; leave it out for the first page"));
    Descriptions.Add(TEXT("filter"), TEXT("Optional. A wildcard the names must match, * and ? (for example \"*.json\"), ignoring case; it applies to folders and files"));

    TArray<FString> Required;
    Required.Add(TEXT("relativePath"));

    Definition.InputSchema = BuildInputSchema(Properties, Required, Descriptions);
    const TSharedPtr<FJsonObject>* SchemaProperties = nullptr;
    const TSharedPtr<FJsonObject>* PageSize = nullptr;
    if (Definition.InputSchema.IsValid() && Definition.InputSchema->TryGetObjectField(TEXT("properties"), SchemaProperties)
        && (*SchemaProperties)->TryGetObjectField(TEXT("pageSize"), PageSize))
    {
        (*PageSize)->SetNumberField(TEXT("minimum"), 1);
        (*PageSize)->SetNumberField(TEXT("maximum"), MaxPageSize);
        (*PageSize)->SetNumberField(TEXT("default"), DefaultPageSize);
    }

    // Read-only: reads editor or project state and changes nothing
    SetAnnotations(Definition, FMcpToolAnnotations::ReadOnly());

    return Definition;
}

FMcpToolCallResult FN2CMcpReadPathTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
    FString RelativePath;
    if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("relativePath"), RelativePath))
    {
        return FMcpToolCallResult::CreateErrorResult(TEXT("Missing required parameter: relativePath"));
    }

    int32 PageSize = DefaultPageSize;
    FString ArgumentError;
    if (!N2CReadPath::ReadOptionalInt(Arguments, TEXT("pageSize"), DefaultPageSize, 1, MaxPageSize, PageSize, ArgumentError))
    {
        return FMcpToolCallResult::CreateErrorResult(ArgumentError);
    }
    FString Cursor;
    Arguments->TryGetStringField(TEXT("cursor"), Cursor);
    int32 Start = 0;
    if (!N2CReadPath::ParseCursor(Cursor, Start))
    {
        return FMcpToolCallResult::CreateErrorResult(FString::Printf(
            TEXT("Invalid cursor \"%s\": pass the nextCursor of the previous page, or leave cursor out for the first page"), *Cursor));
    }
    FString Filter;
    Arguments->TryGetStringField(TEXT("filter"), Filter);
    Filter.TrimStartAndEndInline();

    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Executing read-path tool with relativePath: %s"), *RelativePath),
        EN2CLogSeverity::Debug
    );

    return ExecuteOnGameThread([RelativePath, PageSize, Start, Filter, Cursor]() -> FMcpToolCallResult
    {
        // Part 1: Path. The project folder and the translation output directory are the roots; the jail, the
        // absolute-path rule and the credential deny-list are in FN2CPathUtils::ResolveAgentReadPath.
        FString RequestedPath;
        FString PathError;
        if (!FN2CPathUtils::ResolveAgentReadPath(RelativePath, RequestedPath, PathError))
        {
            return FMcpToolCallResult::CreateErrorResult(PathError);
        }
        if (!FPaths::DirectoryExists(RequestedPath))
        {
            return FMcpToolCallResult::CreateErrorResult(
                FString::Printf(TEXT("Directory does not exist: %s"), *RelativePath));
        }

        // Part 2: The immediate children, filtered by name
        TArray<FString> Files;
        TArray<FString> Directories;
        IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
        PlatformFile.IterateDirectory(*RequestedPath, [&Files, &Directories, &Filter](const TCHAR* FilenameOrDirectory, bool bIsDirectory)
        {
            const FString ItemName = FPaths::GetCleanFilename(FilenameOrDirectory);
            if (ItemName.IsEmpty() || ItemName == TEXT(".") || ItemName == TEXT(".."))
            {
                return true;
            }
            if (!Filter.IsEmpty() && !ItemName.MatchesWildcard(Filter, ESearchCase::IgnoreCase))
            {
                return true;
            }
            (bIsDirectory ? Directories : Files).Add(ItemName);
            return true;
        });
        Directories.Sort();
        Files.Sort();

        // Part 3: One page of [folders..., files...]
        const int32 Total = Directories.Num() + Files.Num();
        if (Start > Total)
        {
            return FMcpToolCallResult::CreateErrorResult(FString::Printf(
                TEXT("Invalid cursor \"%s\": the directory has %d entries now; start again without a cursor"), *Cursor, Total));
        }
        const int32 End = static_cast<int32>(FMath::Min<int64>(static_cast<int64>(Start) + PageSize, Total));

        TArray<TSharedPtr<FJsonValue>> DirectoriesArray;
        TArray<TSharedPtr<FJsonValue>> FilesArray;
        for (int32 Index = Start; Index < End; ++Index)
        {
            if (Index < Directories.Num())
            {
                DirectoriesArray.Add(MakeShared<FJsonValueString>(Directories[Index]));
            }
            else
            {
                FilesArray.Add(MakeShared<FJsonValueString>(Files[Index - Directories.Num()]));
            }
        }
        const bool bTruncated = End < Total;

        // Part 4: Response. Paths are project-relative (absolute only for a translation root outside the project).
        TSharedPtr<FJsonObject> ResponseObject = MakeShared<FJsonObject>();
        ResponseObject->SetStringField(TEXT("path"), FN2CPathUtils::MakeAgentDisplayPath(RequestedPath));
        ResponseObject->SetArrayField(TEXT("directories"), DirectoriesArray);
        ResponseObject->SetArrayField(TEXT("files"), FilesArray);
        ResponseObject->SetNumberField(TEXT("directoryCount"), Directories.Num());
        ResponseObject->SetNumberField(TEXT("fileCount"), Files.Num());
        ResponseObject->SetNumberField(TEXT("total"), Total);
        ResponseObject->SetNumberField(TEXT("returned"), End - Start);
        ResponseObject->SetNumberField(TEXT("pageSize"), PageSize);
        ResponseObject->SetBoolField(TEXT("truncated"), bTruncated);
        if (bTruncated)
        {
            ResponseObject->SetStringField(TEXT("nextCursor"), FString::FromInt(End));
        }
        if (!Filter.IsEmpty())
        {
            ResponseObject->SetStringField(TEXT("filter"), Filter);
        }
        ResponseObject->SetBoolField(TEXT("success"), true);

        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Listed directory: %s (entries %d-%d of %d)"), *RelativePath, Start, End, Total),
            EN2CLogSeverity::Info
        );

        FString OutputString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
        FJsonSerializer::Serialize(ResponseObject.ToSharedRef(), Writer);
        return FMcpToolCallResult::CreateTextResult(OutputString);
    });
}
