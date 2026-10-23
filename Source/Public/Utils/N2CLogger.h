// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

/**
 * @file N2CLogger.h
 * @brief Logging system for the Node to Code plugin
 */

#pragma once

#include "CoreMinimal.h"
#include "Models/N2CLogging.h"
#include "HAL/CriticalSection.h"
#include <atomic>

/**
 * @class FN2CLogger
 * @brief Central logging system for Node to Code operations
 */
class FN2CLogger
{
public:
    /** Get the singleton instance */
    static FN2CLogger& Get();

    /** Log a message with severity */
    void Log(const FString& Message, 
             EN2CLogSeverity Severity = EN2CLogSeverity::Info,
             const FString& Context = TEXT(""));

    /** Log an error with optional context */
    void LogError(const FString& Message, 
                 const FString& Context = TEXT(""));

    /** Log a warning with optional context */
    void LogWarning(const FString& Message,
                   const FString& Context = TEXT(""));

    /** Get all logged errors */
    TArray<FN2CError> GetErrors() const;

    /** Get errors of specific severity */
    TArray<FN2CError> GetErrorsBySeverity(EN2CLogSeverity Severity) const;

    /** Clear all logged errors */
    void ClearErrors();

    /** Set minimum severity level for logging */
    void SetMinSeverity(EN2CLogSeverity Severity);

    /** Enable/disable file logging */
    void EnableFileLogging(bool bEnable);

    /** Set the file logging path */
    void SetLogFilePath(const FString& Path);

    /** Configure the UE log category verbosity */
    static void ConfigureLogCategoryVerbosity(ELogVerbosity::Type Verbosity)
    {
        LogNodeToCode.SetVerbosity(Verbosity);
    }

    /** Map N2C severity to UE verbosity */
    static ELogVerbosity::Type SeverityToVerbosity(EN2CLogSeverity Severity)
    {
        switch (Severity)
        {
            case EN2CLogSeverity::Debug:
                return ELogVerbosity::VeryVerbose;
            case EN2CLogSeverity::Info:
                return ELogVerbosity::Log;
            case EN2CLogSeverity::Warning:
                return ELogVerbosity::Warning;
            case EN2CLogSeverity::Error:
                return ELogVerbosity::Error;
            case EN2CLogSeverity::Fatal:
                return ELogVerbosity::Fatal;
            default:
                return ELogVerbosity::Log;
        }
    }


private:
    /** Private constructor for singleton */
    FN2CLogger();

    /** Write to log file if enabled */
    void WriteToFile(const FString& Message);

    /** Format error for output */
    FString FormatError(const FN2CError& Error) const;

    /** Most recent log records, a ring buffer of at most MaxRetainedRecords entries (oldest overwritten first) */
    TArray<FN2CError> LoggedErrors;

    /** Index in LoggedErrors where the next record goes once the buffer is full */
    int32 NextRecordIndex = 0;

    /** Cap on retained records, so a long session or a chatty worker thread cannot grow memory without bound */
    static constexpr int32 MaxRetainedRecords = 1000;

    /** Guards LoggedErrors, NextRecordIndex, LogFilePath and the file append. Log() is called from the game
     *  thread, HTTP server threads and task-graph workers. */
    mutable FCriticalSection Lock;

    /** Minimum severity level for logging */
    std::atomic<EN2CLogSeverity> MinSeverity;

    /** Whether file logging is enabled */
    std::atomic<bool> bFileLoggingEnabled;

    /** Path for log file */
    FString LogFilePath;
};
