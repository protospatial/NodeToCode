// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

/** How FN2CFileUtils::SaveStringAtomically writes a file */
struct FN2CAtomicWriteOptions
{
    /** Copy the previous file to <file>.bak before replacing it */
    bool bKeepBackup = true;

    /** Make the new file and its backup readable by the owner only (POSIX platforms; Windows relies on the folder ACL) */
    bool bOwnerOnly = false;
};

/**
 * File helpers for the plugin's own state files (graph state, secrets, script registry).
 *
 * The write pattern: <file>.tmp is written in full (UTF-8 without BOM), the previous <file> is copied to <file>.bak,
 * then <file>.tmp is moved over <file>. A crash or power loss leaves either the old file or the new one, never a
 * truncated one; a crash between the temporary write and the move leaves a complete <file>.tmp that
 * FinishInterruptedWrite moves into place on the next load.
 */
class NODETOCODE_API FN2CFileUtils
{
public:
    /** <file>.tmp, the temporary file SaveStringAtomically writes first */
    static FString TempPathFor(const FString& Path);

    /** <file>.bak, the copy of the previous file SaveStringAtomically keeps */
    static FString BackupPathFor(const FString& Path);

    /**
     * Writes Content to Path through a temporary file and a move, so Path is never left truncated.
     * Creates the folder if needed.
     * @return true when Path now holds Content
     */
    static bool SaveStringAtomically(const FString& Content, const FString& Path,
        const FN2CAtomicWriteOptions& Options = FN2CAtomicWriteOptions());

    /**
     * Renames a file that cannot be used (it does not parse) to <file>.corrupt-<UTC yyyymmdd-hhmmss>, adding -1, -2 ...
     * if that name is taken, so the next save cannot overwrite it.
     * @param OutCorruptPath the new name, when the rename worked
     * @return true when the file was renamed
     */
    static bool QuarantineCorruptFile(const FString& Path, FString& OutCorruptPath);

    /**
     * Finishes a SaveStringAtomically that stopped after <file>.tmp was complete but before it replaced <file>:
     * when Path is missing and <file>.tmp exists and IsComplete accepts its content, <file>.tmp is moved to Path.
     * @return true when the temporary file was moved into place
     */
    static bool FinishInterruptedWrite(const FString& Path, TFunctionRef<bool(const FString& /*Content*/)> IsComplete);

    /** Owner-only read/write on POSIX platforms; does nothing on Windows (per-user folders carry the ACL) */
    static void RestrictToOwner(const FString& Path);
};
