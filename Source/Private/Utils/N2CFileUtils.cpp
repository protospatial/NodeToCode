// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Utils/N2CFileUtils.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Utils/N2CLogger.h"

#if PLATFORM_UNIX || PLATFORM_APPLE
#include <sys/stat.h>
#endif

namespace
{
    const TCHAR* const LogContext = TEXT("FileUtils");
}

FString FN2CFileUtils::TempPathFor(const FString& Path)
{
    return Path + TEXT(".tmp");
}

FString FN2CFileUtils::BackupPathFor(const FString& Path)
{
    return Path + TEXT(".bak");
}

void FN2CFileUtils::RestrictToOwner(const FString& Path)
{
#if PLATFORM_UNIX || PLATFORM_APPLE
    chmod(TCHAR_TO_UTF8(*Path), S_IRUSR | S_IWUSR);
#endif
}

bool FN2CFileUtils::SaveStringAtomically(const FString& Content, const FString& Path, const FN2CAtomicWriteOptions& Options)
{
    IFileManager& FileManager = IFileManager::Get();
    const FString Dir = FPaths::GetPath(Path);
    if (!Dir.IsEmpty() && !FileManager.DirectoryExists(*Dir))
    {
        FileManager.MakeDirectory(*Dir, true);
    }

    const FString TempPath = TempPathFor(Path);
    if (!FFileHelper::SaveStringToFile(Content, *TempPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        FileManager.Delete(*TempPath, false, true, true);
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to write %s"), *TempPath), LogContext);
        return false;
    }
    if (Options.bOwnerOnly)
    {
        RestrictToOwner(TempPath);
    }

    // The previous file is copied, not moved, so Path is only absent for the instant the move replaces it,
    // and by then <file>.tmp is complete (FinishInterruptedWrite completes the move on the next load)
    if (Options.bKeepBackup && FileManager.FileExists(*Path))
    {
        const FString BackupPath = BackupPathFor(Path);
        if (FileManager.Copy(*BackupPath, *Path, /*Replace*/ true, /*EvenIfReadOnly*/ true) == COPY_OK)
        {
            if (Options.bOwnerOnly)
            {
                RestrictToOwner(BackupPath);
            }
        }
        else
        {
            FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Could not refresh the backup %s"), *BackupPath), LogContext);
        }
    }

    if (!FileManager.Move(*Path, *TempPath, /*Replace*/ true, /*EvenIfReadOnly*/ true, /*Attributes*/ false,
        /*bDoNotRetryOrError*/ true))
    {
        FileManager.Delete(*TempPath, false, true, true);
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to replace %s with %s"), *Path, *TempPath), LogContext);
        return false;
    }
    return true;
}

bool FN2CFileUtils::QuarantineCorruptFile(const FString& Path, FString& OutCorruptPath)
{
    OutCorruptPath.Reset();
    IFileManager& FileManager = IFileManager::Get();
    const FString Base = FString::Printf(TEXT("%s.corrupt-%s"), *Path, *FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S")));
    FString Candidate = Base;
    for (int32 Suffix = 1; FileManager.FileExists(*Candidate) && Suffix < 1000; ++Suffix)
    {
        Candidate = FString::Printf(TEXT("%s-%d"), *Base, Suffix);
    }
    if (FileManager.FileExists(*Candidate)
        || !FileManager.Move(*Candidate, *Path, /*Replace*/ false, /*EvenIfReadOnly*/ true, /*Attributes*/ false,
            /*bDoNotRetryOrError*/ true))
    {
        return false;
    }
    OutCorruptPath = Candidate;
    return true;
}

bool FN2CFileUtils::FinishInterruptedWrite(const FString& Path, TFunctionRef<bool(const FString&)> IsComplete)
{
    IFileManager& FileManager = IFileManager::Get();
    const FString TempPath = TempPathFor(Path);
    if (FileManager.FileExists(*Path) || !FileManager.FileExists(*TempPath))
    {
        return false;
    }
    FString Content;
    if (!FFileHelper::LoadFileToString(Content, *TempPath) || !IsComplete(Content))
    {
        return false;
    }
    if (!FileManager.Move(*Path, *TempPath, /*Replace*/ false, /*EvenIfReadOnly*/ true, /*Attributes*/ false,
        /*bDoNotRetryOrError*/ true))
    {
        return false;
    }
    FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Finished an interrupted save of %s from %s"), *Path, *TempPath),
        LogContext);
    return true;
}
