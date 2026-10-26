// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Utils/N2CLogger.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Serialization/Archive.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/OutputDeviceConsole.h"
#include "HAL/IConsoleManager.h"

namespace N2CLoggerPrivate
{
    /** Off by default and not read from or saved to config: set it in the console for one session */
    static TAutoConsoleVariable<bool> CVarLogLLMPayloads(
        TEXT("n2c.LogLLMPayloads"),
        false,
        TEXT("1 writes NodeToCode's LLM request payloads, system and user messages and model responses to the log at ")
        TEXT("Debug severity (Min Severity Debug). They hold your Blueprint logic and generated code: turn it off before ")
        TEXT("sharing a log. 0 (default) logs only their sizes."),
        FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* Variable)
        {
            if (Variable && Variable->GetBool())
            {
                UE_LOG(LogNodeToCode, Warning, TEXT("n2c.LogLLMPayloads is on: LLM request payloads, prompts and model responses (your Blueprint logic and generated code) are written to the log at Debug severity. Set it to 0 before sharing a log."));
            }
        }),
        ECVF_Default);
}

bool FN2CLogger::ShouldLogLLMPayloads()
{
    return N2CLoggerPrivate::CVarLogLLMPayloads.GetValueOnAnyThread();
}

void FN2CLogger::LogLLMContent(const TCHAR* Label, const TCHAR* What, const FString& Content, const FString& Context)
{
    if (!IsEnabled(EN2CLogSeverity::Debug))
    {
        return;
    }
    if (ShouldLogLLMPayloads())
    {
        Log(FString::Printf(TEXT("%s:\n\n%s"), Label, *Content), EN2CLogSeverity::Debug, Context);
        return;
    }
    Log(FString::Printf(TEXT("%s: %d characters (not logged; n2c.LogLLMPayloads 1 logs it)"), What, Content.Len()),
        EN2CLogSeverity::Debug, Context);
}

FString FN2CLogger::RedactSecret(const FString& Text, const FString& Secret)
{
    // A very short value would also match ordinary text; real keys are far longer
    if (Secret.Len() < 8 || !Text.Contains(Secret, ESearchCase::CaseSensitive))
    {
        return Text;
    }
    return Text.Replace(*Secret, TEXT("[redacted]"), ESearchCase::CaseSensitive);
}

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

FN2CLogger::~FN2CLogger()
{
    FScopeLock ScopeLock(&Lock);
    CloseLogFile();
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
    FScopeLock ScopeLock(&Lock);
    bFileLoggingEnabled = bEnable;
    if (!bEnable)
    {
        CloseLogFile();
    }
}

void FN2CLogger::SetLogFilePath(const FString& Path)
{
    FScopeLock ScopeLock(&Lock);
    if (Path != LogFilePath)
    {
        CloseLogFile();
        LogFilePath = Path;
    }
}

void FN2CLogger::CloseLogFile()
{
    if (LogFile)
    {
        LogFile->Close();
        LogFile.Reset();
    }
}

void FN2CLogger::WriteToFile(const FString& Message)
{
    if (!bFileLoggingEnabled)
    {
        return;
    }

    if (!LogFile)
    {
        // Opened once and kept open: before card 0193 every line reopened the file with SaveStringToFile and
        // EEncodingOptions::AutoDetect, which wrote a line holding any non-ASCII character as UTF-16 and the rest as
        // ANSI, so one file mixed encodings. Every line is UTF-8 now.
        IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
        const FString Directory = FPaths::GetPath(LogFilePath);
        if (!Directory.IsEmpty() && !PlatformFile.DirectoryExists(*Directory))
        {
            PlatformFile.CreateDirectoryTree(*Directory);
        }
        LogFile.Reset(IFileManager::Get().CreateFileWriter(*LogFilePath, FILEWRITE_Append | FILEWRITE_AllowRead));
        if (!LogFile)
        {
            // Cannot open (a read-only folder, the path is a directory): stop trying instead of failing on every line.
            // UE_LOG, not Log, since the caller holds Lock.
            bFileLoggingEnabled = false;
            UE_LOG(LogNodeToCode, Warning, TEXT("NodeToCode file logging is off: cannot open %s for writing"), *LogFilePath);
            return;
        }
    }

    const FTCHARToUTF8 Utf8(*(Message + LINE_TERMINATOR));
    LogFile->Serialize(const_cast<ANSICHAR*>(Utf8.Get()), Utf8.Length());
    // Each line reaches the file at once, so a crash keeps everything logged before it
    LogFile->Flush();
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