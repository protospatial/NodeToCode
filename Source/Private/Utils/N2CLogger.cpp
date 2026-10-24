// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Utils/N2CLogger.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/OutputDeviceConsole.h"

FN2CLogger& FN2CLogger::Get()
{
    static FN2CLogger Instance;
    return Instance;
}

FN2CLogger::FN2CLogger()
    : MinSeverity(EN2CLogSeverity::Info)
    , bFileLoggingEnabled(false)
    , LogFilePath(FPaths::ProjectSavedDir() / TEXT("NodeToCode.log"))
{
}

void FN2CLogger::Log(const FString& Message, EN2CLogSeverity Severity, const FString& Context)
{
    if (Severity < MinSeverity)
    {
        return;
    }

    // Create error record
    FN2CError Error;
    Error.Message = Message;
    Error.Severity = Severity;
    Error.Context = Context;
    Error.Timestamp = FDateTime::Now();

    // Format for output
    FString FormattedMessage = FormatError(Error);

    {
        FScopeLock ScopeLock(&Lock);

        // Retain the record in the bounded ring buffer
        if (LoggedErrors.Num() < MaxRetainedRecords)
        {
            LoggedErrors.Add(MoveTemp(Error));
        }
        else
        {
            LoggedErrors[NextRecordIndex] = MoveTemp(Error);
            NextRecordIndex = (NextRecordIndex + 1) % MaxRetainedRecords;
        }

        // Write to log file if enabled (serialized with the buffer so lines never interleave)
        if (bFileLoggingEnabled)
        {
            WriteToFile(FormattedMessage);
        }
    }
    // UE_LOG below runs outside the lock

    // Output to console window
    const TCHAR* SeverityColor = TEXT("");
    switch (Severity)
    {
        case EN2CLogSeverity::Debug:
            SeverityColor = TEXT("");
            break;
        case EN2CLogSeverity::Info:
            SeverityColor = TEXT("");
            break;
        case EN2CLogSeverity::Warning:
            SeverityColor = TEXT("yellow");
            break;
        case EN2CLogSeverity::Error:
            SeverityColor = TEXT("red");
            break;
        case EN2CLogSeverity::Fatal:
            SeverityColor = TEXT("red");
            break;
    }

    // Output to NodeToCode log
    switch (Severity)
    {
        case EN2CLogSeverity::Debug:
            UE_LOG(LogNodeToCode, VeryVerbose, TEXT("%s"), *FormattedMessage);
            break;
        case EN2CLogSeverity::Info:
            UE_LOG(LogNodeToCode, Log, TEXT("%s"), *FormattedMessage);
            break;
        case EN2CLogSeverity::Warning:
            UE_LOG(LogNodeToCode, Warning, TEXT("%s"), *FormattedMessage);
            break;
        case EN2CLogSeverity::Error:
            UE_LOG(LogNodeToCode, Error, TEXT("%s"), *FormattedMessage);
            break;
        case EN2CLogSeverity::Fatal:
            UE_LOG(LogNodeToCode, Fatal, TEXT("%s"), *FormattedMessage);
            break;
    }
}

void FN2CLogger::LogError(const FString& Message, const FString& Context)
{
    Log(Message, EN2CLogSeverity::Error, Context);
}

void FN2CLogger::LogWarning(const FString& Message, const FString& Context)
{
    Log(Message, EN2CLogSeverity::Warning, Context);
}

TArray<FN2CError> FN2CLogger::GetErrors() const
{
    FScopeLock ScopeLock(&Lock);
    if (LoggedErrors.Num() < MaxRetainedRecords || NextRecordIndex == 0)
    {
        return LoggedErrors;
    }
    // Oldest first
    TArray<FN2CError> Ordered;
    Ordered.Reserve(LoggedErrors.Num());
    for (int32 i = 0; i < LoggedErrors.Num(); ++i)
    {
        Ordered.Add(LoggedErrors[(NextRecordIndex + i) % LoggedErrors.Num()]);
    }
    return Ordered;
}

TArray<FN2CError> FN2CLogger::GetErrorsBySeverity(EN2CLogSeverity Severity) const
{
    TArray<FN2CError> FilteredErrors;
    for (const FN2CError& Error : GetErrors())
    {
        if (Error.Severity == Severity)
        {
            FilteredErrors.Add(Error);
        }
    }
    return FilteredErrors;
}

void FN2CLogger::ClearErrors()
{
    FScopeLock ScopeLock(&Lock);
    LoggedErrors.Empty();
    NextRecordIndex = 0;
}

void FN2CLogger::SetMinSeverity(EN2CLogSeverity Severity)
{
    MinSeverity = Severity;
    
    // Update UE log category verbosity to match
    ConfigureLogCategoryVerbosity(SeverityToVerbosity(Severity));
}

void FN2CLogger::EnableFileLogging(bool bEnable)
{
    bFileLoggingEnabled = bEnable;
}

void FN2CLogger::SetLogFilePath(const FString& Path)
{
    FScopeLock ScopeLock(&Lock);
    LogFilePath = Path;
}

void FN2CLogger::WriteToFile(const FString& Message)
{
    if (!bFileLoggingEnabled)
    {
        return;
    }

    // Ensure directory exists
    FString Directory = FPaths::GetPath(LogFilePath);
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    if (!PlatformFile.DirectoryExists(*Directory))
    {
        PlatformFile.CreateDirectoryTree(*Directory);
    }

    // Append to file
    FFileHelper::SaveStringToFile(
        Message + LINE_TERMINATOR,
        *LogFilePath,
        FFileHelper::EEncodingOptions::AutoDetect,
        &IFileManager::Get(),
        FILEWRITE_Append
    );
}

FString FN2CLogger::FormatError(const FN2CError& Error) const
{
    FString SeverityStr;
    switch (Error.Severity)
    {
        case EN2CLogSeverity::Debug:   SeverityStr = TEXT("DEBUG"); break;
        case EN2CLogSeverity::Info:    SeverityStr = TEXT("INFO"); break;
        case EN2CLogSeverity::Warning: SeverityStr = TEXT("WARNING"); break;
        case EN2CLogSeverity::Error:   SeverityStr = TEXT("ERROR"); break;
        case EN2CLogSeverity::Fatal:   SeverityStr = TEXT("FATAL"); break;
    }

    return FString::Printf(TEXT("[%s][%s] %s%s%s"),
        *Error.Timestamp.ToString(),
        *SeverityStr,
        *Error.Message,
        Error.Context.IsEmpty() ? TEXT("") : TEXT(" - "),
        *Error.Context
    );
}