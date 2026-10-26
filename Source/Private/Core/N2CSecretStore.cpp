// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CSecretStore.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Utils/N2CFileUtils.h"
#include "Utils/N2CLogger.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/WindowsHWrapper.h"
#include <wincred.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace
{
    const TCHAR* const LogContext = TEXT("SecretStore");
    const TCHAR* const KeyFieldSuffix = TEXT("_API_Key");
    const TCHAR* const VersionField = TEXT("Version");
    const TCHAR* const MigrationConflictsField = TEXT("MigrationConflicts");

    /** Sign-in token fields written before 2.0: the nested block and the older flat fields */
    const TCHAR* const RetiredSignInFields[] = {
        TEXT("OAuth"),
        TEXT("Claude_OAuth_AccessToken"), TEXT("Claude_OAuth_RefreshToken"), TEXT("Claude_OAuth_ExpiresAt"), TEXT("Claude_OAuth_Scope"),
        TEXT("Google_OAuth_AccessToken"), TEXT("Google_OAuth_RefreshToken"), TEXT("Google_OAuth_ExpiresAt"), TEXT("Google_OAuth_Scope"),
    };

    /** The provider name in a key field, for log lines ("OpenAI_API_Key" -> "OpenAI") */
    FString ProviderNameOf(const FString& Field)
    {
        return Field.EndsWith(KeyFieldSuffix) ? Field.LeftChop(FCString::Strlen(KeyFieldSuffix)) : Field;
    }

    void StampVersion(FJsonObject& Json)
    {
        double Existing = 0.0;
        if (!Json.TryGetNumberField(VersionField, Existing) || Existing < FN2CSecretFileBackend::FileVersion)
        {
            Json.SetNumberField(VersionField, FN2CSecretFileBackend::FileVersion);
        }
    }

    void LogRetiredSignInRemoval(const FString& Path)
    {
        FN2CLogger::Get().LogWarning(FString::Printf(
            TEXT("Claude Pro/Max and Google account sign-in were removed in NodeToCode 2.0, so the saved sign-in tokens were deleted from %s (API keys kept). Set an API key in Project Settings > Plugins > Node to Code > LLM Services. See %s"),
            *Path, N2CSignInRemoval::DocsUrl), LogContext);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// FN2CSecretFileBackend

FN2CSecretFileBackend::FN2CSecretFileBackend(const FString& InFilePath, const FString& InLegacyFilePath)
    : FilePath(FPaths::ConvertRelativePathToFull(InFilePath))
    , LegacyFilePath(InLegacyFilePath.IsEmpty() ? FString() : FPaths::ConvertRelativePathToFull(InLegacyFilePath))
{
}

FString FN2CSecretFileBackend::Describe() const
{
    return FilePath;
}

FString FN2CSecretFileBackend::GetDisplayName() const
{
    return FString::Printf(TEXT("the per-user secrets file %s"), *FilePath);
}

TArray<TPair<FString, FString>> FN2CSecretFileBackend::LoadMigrationConflictKeys() const
{
    TArray<TPair<FString, FString>> Result;
    TSharedPtr<FJsonObject> Json;
    const TSharedPtr<FJsonObject>* Conflicts = nullptr;
    if (ReadJsonFile(FilePath, Json) != EReadResult::Parsed || !Json->TryGetObjectField(MigrationConflictsField, Conflicts)
        || !Conflicts || !Conflicts->IsValid())
    {
        return Result;
    }
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Source : (*Conflicts)->Values)
    {
        const TSharedPtr<FJsonObject>* Fields = nullptr;
        if (!Source.Value.IsValid() || !Source.Value->TryGetObject(Fields) || !Fields || !Fields->IsValid())
        {
            continue;
        }
        TMap<FString, FString> Keys;
        ExtractKeys(**Fields, Keys);
        for (const TPair<FString, FString>& Key : Keys)
        {
            Result.Emplace(Key.Key, Key.Value);
        }
    }
    return Result;
}

FString FN2CSecretFileBackend::GetKeyField(const FString& ProviderId)
{
    return ProviderId + KeyFieldSuffix;
}

int32 FN2CSecretFileBackend::RemoveRetiredSignInEntries(FJsonObject& Json)
{
    int32 Removed = 0;
    for (const TCHAR* Field : RetiredSignInFields)
    {
        if (Json.HasField(Field))
        {
            Json.RemoveField(Field);
            ++Removed;
        }
    }
    return Removed;
}

void FN2CSecretFileBackend::ExtractKeys(const FJsonObject& Json, TMap<FString, FString>& OutKeys)
{
    OutKeys.Reset();
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Json.Values)
    {
        FString Value;
        if (Field.Key.EndsWith(KeyFieldSuffix) && Field.Key.Len() > FCString::Strlen(KeyFieldSuffix)
            && Field.Value.IsValid() && Field.Value->TryGetString(Value))
        {
            OutKeys.Add(ProviderNameOf(Field.Key), Value);
        }
    }
}

FN2CSecretFileBackend::EReadResult FN2CSecretFileBackend::ReadJsonFile(const FString& Path, TSharedPtr<FJsonObject>& OutJson)
{
    OutJson.Reset();
    if (!FPaths::FileExists(Path))
    {
        return EReadResult::Missing;
    }
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *Path))
    {
        return EReadResult::Unreadable;
    }
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, OutJson) || !OutJson.IsValid())
    {
        OutJson.Reset();
        return EReadResult::Corrupt;
    }
    return EReadResult::Parsed;
}

bool FN2CSecretFileBackend::QuarantineCorruptFile() const
{
    FString CorruptPath;
    if (!FN2CFileUtils::QuarantineCorruptFile(FilePath, CorruptPath))
    {
        FN2CLogger::Get().LogError(FString::Printf(
            TEXT("The secrets file %s does not parse and could not be set aside; it is left as it is and no key is saved until it is fixed or removed."),
            *FilePath), LogContext);
        return false;
    }
    FN2CLogger::Get().LogError(FString::Printf(
        TEXT("The secrets file %s does not parse, so it was kept as %s and API keys start empty. Re-enter them in Project Settings > Plugins > Node to Code, or repair that file and rename it back."),
        *FilePath, *CorruptPath), LogContext);
    return true;
}

bool FN2CSecretFileBackend::ReadForWrite(TSharedPtr<FJsonObject>& OutJson) const
{
    switch (ReadJsonFile(FilePath, OutJson))
    {
        case EReadResult::Parsed:
            return true;
        case EReadResult::Missing:
            OutJson = MakeShared<FJsonObject>();
            return true;
        case EReadResult::Corrupt:
            if (!QuarantineCorruptFile())
            {
                return false;
            }
            OutJson = MakeShared<FJsonObject>();
            return true;
        case EReadResult::Unreadable:
        default:
            FN2CLogger::Get().LogError(FString::Printf(
                TEXT("Could not read the secrets file %s, so it was not changed."), *FilePath), LogContext);
            return false;
    }
}

bool FN2CSecretFileBackend::WriteJsonFile(const TSharedRef<FJsonObject>& Json) const
{
    FString JsonString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
    if (!FJsonSerializer::Serialize(Json, Writer))
    {
        FN2CLogger::Get().LogError(TEXT("Failed to serialize secrets to JSON"), LogContext);
        return false;
    }

    FN2CAtomicWriteOptions Options;
    Options.bOwnerOnly = true;
    if (!FN2CFileUtils::SaveStringAtomically(JsonString, FilePath, Options))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to save secrets to %s"), *FilePath), LogContext);
        return false;
    }
    return true;
}

void FN2CSecretFileBackend::RecoverInterruptedWrite() const
{
    FN2CFileUtils::FinishInterruptedWrite(FilePath, [](const FString& Content)
    {
        TSharedPtr<FJsonObject> Parsed;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);
        return FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid();
    });
}

void FN2CSecretFileBackend::MigrateLegacyFile() const
{
    if (LegacyFilePath.IsEmpty() || LegacyFilePath.Equals(FilePath, ESearchCase::IgnoreCase))
    {
        return;
    }

    TSharedPtr<FJsonObject> Legacy;
    const EReadResult LegacyRead = ReadJsonFile(LegacyFilePath, Legacy);
    if (LegacyRead == EReadResult::Missing)
    {
        return;
    }
    if (LegacyRead != EReadResult::Parsed)
    {
        FN2CLogger::Get().LogError(FString::Printf(
            TEXT("The project secrets file %s could not be read, so its keys were not moved to %s. It was left in place: re-enter the keys in Project Settings > Plugins > Node to Code, then delete that file."),
            *LegacyFilePath, *FilePath), LogContext);
        return;
    }

    if (RemoveRetiredSignInEntries(*Legacy) > 0)
    {
        LogRetiredSignInRemoval(LegacyFilePath);
    }
    Legacy->RemoveField(VersionField);

    TSharedPtr<FJsonObject> User;
    if (!ReadForWrite(User))
    {
        FN2CLogger::Get().LogError(FString::Printf(
            TEXT("The project secrets file %s was not moved because %s could not be read; it is tried again on the next start."),
            *LegacyFilePath, *FilePath), LogContext);
        return;
    }

    // Values already in the per-user file win; an empty one is filled from the project file
    TArray<FString> Conflicts;
    TSharedPtr<FJsonObject> Replaced = MakeShared<FJsonObject>();
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Legacy->Values)
    {
        const TSharedPtr<FJsonValue> Existing = User->TryGetField(Field.Key);
        FString ExistingString, LegacyString;
        const bool bExistingIsString = Existing.IsValid() && Existing->TryGetString(ExistingString);
        const bool bLegacyIsString = Field.Value.IsValid() && Field.Value->TryGetString(LegacyString);
        if (!Existing.IsValid() || Existing->IsNull() || (bExistingIsString && ExistingString.IsEmpty()))
        {
            User->SetField(Field.Key, Field.Value);
        }
        else if (bExistingIsString && bLegacyIsString && !LegacyString.IsEmpty() && LegacyString != ExistingString)
        {
            Conflicts.Add(ProviderNameOf(Field.Key));
            Replaced->SetStringField(Field.Key, LegacyString);
        }
    }

    if (Conflicts.Num() > 0)
    {
        // Keep the project's differing keys outside the project, so moving the file loses none of them
        const TSharedPtr<FJsonObject>* ExistingConflicts = nullptr;
        TSharedPtr<FJsonObject> AllConflicts;
        if (User->TryGetObjectField(MigrationConflictsField, ExistingConflicts) && ExistingConflicts && ExistingConflicts->IsValid())
        {
            AllConflicts = *ExistingConflicts;
        }
        else
        {
            AllConflicts = MakeShared<FJsonObject>();
        }
        AllConflicts->SetObjectField(LegacyFilePath, Replaced);
        User->SetObjectField(MigrationConflictsField, AllConflicts);
    }

    StampVersion(*User);
    if (!WriteJsonFile(User.ToSharedRef()))
    {
        FN2CLogger::Get().LogError(FString::Printf(
            TEXT("The project secrets file %s was not moved because %s could not be written; it is tried again on the next start."),
            *LegacyFilePath, *FilePath), LogContext);
        return;
    }

    if (Conflicts.Num() > 0)
    {
        FN2CLogger::Get().LogWarning(FString::Printf(
            TEXT("While moving API keys out of the project, %s already had a different key in %s. The per-user key is kept; the project's key is saved under %s in that file."),
            *FString::Join(Conflicts, TEXT(", ")), *FilePath, MigrationConflictsField), LogContext);
    }

    IFileManager& FileManager = IFileManager::Get();
    FileManager.Delete(*FN2CFileUtils::TempPathFor(LegacyFilePath), false, true, true);
    if (!FileManager.Delete(*LegacyFilePath, false, /*EvenReadOnly*/ true, /*Quiet*/ true))
    {
        FN2CLogger::Get().LogWarning(FString::Printf(
            TEXT("API keys were copied to %s, but the project secrets file %s could not be deleted, so a plain-text copy is still in the project. Delete it by hand."),
            *FilePath, *LegacyFilePath), LogContext);
        return;
    }
    // Removes the folder only when nothing else is in it
    FileManager.DeleteDirectory(*FPaths::GetPath(LegacyFilePath), false, false);
    FN2CLogger::Get().Log(FString::Printf(TEXT("Moved API keys from %s to %s"), *LegacyFilePath, *FilePath),
        EN2CLogSeverity::Info, LogContext);
}

bool FN2CSecretFileBackend::LoadAll(TMap<FString, FString>& OutKeys)
{
    OutKeys.Reset();
    RecoverInterruptedWrite();
    MigrateLegacyFile();

    TSharedPtr<FJsonObject> Json;
    switch (ReadJsonFile(FilePath, Json))
    {
        case EReadResult::Missing:
            FN2CLogger::Get().Log(FString::Printf(TEXT("No secrets file at %s yet"), *FilePath), EN2CLogSeverity::Info, LogContext);
            return true;
        case EReadResult::Corrupt:
            QuarantineCorruptFile();
            return false;
        case EReadResult::Unreadable:
            FN2CLogger::Get().LogError(FString::Printf(TEXT("Could not read the secrets file %s"), *FilePath), LogContext);
            return false;
        case EReadResult::Parsed:
        default:
            break;
    }

    // Sign-in tokens from before 2.0 are deleted from the file; every other field is written back as it was
    if (RemoveRetiredSignInEntries(*Json) > 0)
    {
        if (WriteJsonFile(Json.ToSharedRef()))
        {
            LogRetiredSignInRemoval(FilePath);
        }
        else
        {
            FN2CLogger::Get().LogWarning(FString::Printf(
                TEXT("%s still holds sign-in tokens that NodeToCode 2.0 no longer uses, and they could not be removed; they are ignored. See %s"),
                *FilePath, N2CSignInRemoval::DocsUrl), LogContext);
        }
    }

    ExtractKeys(*Json, OutKeys);
    FN2CLogger::Get().Log(FString::Printf(TEXT("Loaded API keys from %s"), *FilePath), EN2CLogSeverity::Info, LogContext);
    return true;
}

bool FN2CSecretFileBackend::StoreKey(const FString& ProviderId, const FString& Key, TMap<FString, FString>& OutKeys)
{
    TSharedPtr<FJsonObject> Json;
    if (!ReadForWrite(Json))
    {
        return false;
    }
    RemoveRetiredSignInEntries(*Json);
    Json->SetStringField(GetKeyField(ProviderId), Key);
    StampVersion(*Json);
    if (!WriteJsonFile(Json.ToSharedRef()))
    {
        return false;
    }
    ExtractKeys(*Json, OutKeys);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// FN2CCredentialManagerBackend

const TCHAR* const FN2CCredentialManagerBackend::DefaultTargetPrefix = TEXT("NodeToCode/");

#if PLATFORM_WINDOWS
namespace
{
    /** User name stored on each entry (Credential Manager shows it; it carries no secret) */
    const TCHAR* const CredentialUserName = TEXT("NodeToCode");

    /**
     * The key in a credential blob: UTF-8 as this backend writes it, or UTF-16LE as 'cmdkey /generic:... /pass:...' and
     * the Credential Manager control panel write it (every second byte zero; API keys are ASCII)
     */
    FString DecodeBlob(const uint8* Blob, uint32 Size)
    {
        if (!Blob || Size == 0)
        {
            return FString();
        }
        bool bUtf16 = Size >= 2 && Size % 2 == 0;
        for (uint32 Index = 1; bUtf16 && Index < Size; Index += 2)
        {
            bUtf16 = Blob[Index] == 0;
        }
        if (bUtf16)
        {
            FString Result;
            Result.Reserve(Size / 2);
            for (uint32 Index = 0; Index + 1 < Size; Index += 2)
            {
                Result.AppendChar(static_cast<TCHAR>(Blob[Index] | (Blob[Index + 1] << 8)));
            }
            return Result;
        }
        const FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Blob), static_cast<int32>(Size));
        return FString(Converted.Length(), Converted.Get());
    }
}
#endif

FN2CCredentialManagerBackend::FN2CCredentialManagerBackend(const FString& InTargetPrefix)
    : TargetPrefix(InTargetPrefix)
{
}

bool FN2CCredentialManagerBackend::IsSupported()
{
    return PLATFORM_WINDOWS != 0;
}

FString FN2CCredentialManagerBackend::Describe() const
{
    return FString::Printf(TEXT("Windows Credential Manager (%s*)"), *TargetPrefix);
}

FString FN2CCredentialManagerBackend::GetDisplayName() const
{
    return TEXT("Windows Credential Manager");
}

FString FN2CCredentialManagerBackend::GetTargetName(const FString& ProviderId) const
{
    return TargetPrefix + ProviderId;
}

bool FN2CCredentialManagerBackend::LoadAll(TMap<FString, FString>& OutKeys)
{
    OutKeys.Reset();
#if PLATFORM_WINDOWS
    const FString Filter = TargetPrefix + TEXT("*");
    DWORD Count = 0;
    PCREDENTIALW* Credentials = nullptr;
    if (!::CredEnumerateW(*Filter, 0, &Count, &Credentials))
    {
        const DWORD Error = ::GetLastError();
        if (Error == ERROR_NOT_FOUND)
        {
            return true;
        }
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Could not read the API keys from %s (Windows error %u)"),
            *Describe(), static_cast<uint32>(Error)), LogContext);
        return false;
    }
    for (DWORD Index = 0; Index < Count; ++Index)
    {
        const PCREDENTIALW Credential = Credentials[Index];
        if (!Credential || Credential->Type != CRED_TYPE_GENERIC || !Credential->TargetName)
        {
            continue;
        }
        const FString Target(Credential->TargetName);
        if (!Target.StartsWith(TargetPrefix, ESearchCase::IgnoreCase) || Target.Len() <= TargetPrefix.Len())
        {
            continue;
        }
        OutKeys.Add(Target.RightChop(TargetPrefix.Len()), DecodeBlob(Credential->CredentialBlob, Credential->CredentialBlobSize));
    }
    ::CredFree(Credentials);
    return true;
#else
    FN2CLogger::Get().LogError(TEXT("Windows Credential Manager is not available on this platform"), LogContext);
    return false;
#endif
}

bool FN2CCredentialManagerBackend::StoreKey(const FString& ProviderId, const FString& Key, TMap<FString, FString>& OutKeys)
{
#if PLATFORM_WINDOWS
    if (ProviderId.IsEmpty())
    {
        return false;
    }
    const FString Target = GetTargetName(ProviderId);
    if (Key.IsEmpty())
    {
        if (!::CredDeleteW(*Target, CRED_TYPE_GENERIC, 0) && ::GetLastError() != ERROR_NOT_FOUND)
        {
            FN2CLogger::Get().LogError(FString::Printf(TEXT("Could not remove %s from Windows Credential Manager (Windows error %u)"),
                *Target, static_cast<uint32>(::GetLastError())), LogContext);
            return false;
        }
    }
    else
    {
        const FTCHARToUTF8 Utf8(*Key);
        if (Utf8.Length() > MaxKeyBytes)
        {
            FN2CLogger::Get().LogError(FString::Printf(TEXT("The %s API key is %d bytes long; Windows Credential Manager takes at most %d, so it was not saved."),
                *ProviderId, Utf8.Length(), MaxKeyBytes), LogContext);
            return false;
        }
        FString Comment = FString::Printf(TEXT("NodeToCode %s API key (Project Settings > Plugins > Node to Code)"), *ProviderId);
        FString UserName = CredentialUserName;
        FString TargetName = Target;

        CREDENTIALW Credential = {};
        Credential.Type = CRED_TYPE_GENERIC;
        Credential.TargetName = TargetName.GetCharArray().GetData();
        Credential.Comment = Comment.GetCharArray().GetData();
        Credential.UserName = UserName.GetCharArray().GetData();
        Credential.CredentialBlobSize = static_cast<DWORD>(Utf8.Length());
        Credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<ANSICHAR*>(Utf8.Get()));
        // This user on this machine only: the key does not roam with the profile
        Credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
        if (!::CredWriteW(&Credential, 0))
        {
            FN2CLogger::Get().LogError(FString::Printf(TEXT("Could not save %s to Windows Credential Manager (Windows error %u)"),
                *Target, static_cast<uint32>(::GetLastError())), LogContext);
            return false;
        }
    }

    if (!LoadAll(OutKeys))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("%s was written, but Windows Credential Manager could not be read back"), *Target), LogContext);
        return false;
    }
    return true;
#else
    OutKeys.Reset();
    FN2CLogger::Get().LogError(TEXT("Windows Credential Manager is not available on this platform"), LogContext);
    return false;
#endif
}

// ---------------------------------------------------------------------------------------------------------------------
// FN2CSecretStore

FN2CSecretStore& FN2CSecretStore::Get()
{
    static FN2CSecretStore Store([]() -> TUniquePtr<IN2CSecretBackend>
    {
        if (FN2CCredentialManagerBackend::IsSupported())
        {
            // Windows: keys live in Credential Manager; a per-user or project secrets.json moves there once
            TUniquePtr<FN2CCredentialManagerBackend> Vault = MakeUnique<FN2CCredentialManagerBackend>();
            MigrateFileToBackend(GetUserSecretsFilePath(), GetLegacyProjectSecretsFilePath(), *Vault);
            return Vault;
        }
        // Mac and Linux (not in the 2.0-beta allowlist, decision 0001): the per-user file
        return MakeUnique<FN2CSecretFileBackend>(GetUserSecretsFilePath(), GetLegacyProjectSecretsFilePath());
    }());
    return Store;
}

FN2CSecretMigrationResult FN2CSecretStore::MigrateFileToBackend(const FString& InUserFile, const FString& InLegacyFile, IN2CSecretBackend& Target)
{
    FN2CSecretMigrationResult Result;
    const FString UserFile = FPaths::ConvertRelativePathToFull(InUserFile);
    const FString LegacyFile = InLegacyFile.IsEmpty() ? FString() : FPaths::ConvertRelativePathToFull(InLegacyFile);
    const bool bUserFile = FPaths::FileExists(UserFile) || FPaths::FileExists(FN2CFileUtils::TempPathFor(UserFile));
    const bool bLegacyFile = !LegacyFile.IsEmpty() && FPaths::FileExists(LegacyFile);
    if (!bUserFile && !bLegacyFile)
    {
        return Result;
    }
    Result.bFoundFile = true;

    auto Fail = [&Result, &UserFile, &Target](const FString& Why)
    {
        Result.bFailed = true;
        FN2CLogger::Get().LogError(FString::Printf(
            TEXT("API keys were not moved from %s to %s: %s. The file was left in place and is tried again on the next start; every key in it is still used."),
            *UserFile, *Target.Describe(), *Why), LogContext);
        return Result;
    };

    // Merges a project file into the per-user file first (FN2CSecretFileBackend::MigrateLegacyFile)
    FN2CSecretFileBackend File(UserFile, LegacyFile);
    TMap<FString, FString> FileKeys;
    if (!File.LoadAll(FileKeys))
    {
        return Fail(TEXT("the file could not be read"));
    }
    if (!FPaths::FileExists(UserFile))
    {
        // A project file that could not be merged (already logged) and no per-user file: nothing to move yet
        Result.bFoundFile = false;
        return Result;
    }

    TMap<FString, FString> Existing;
    if (!Target.LoadAll(Existing))
    {
        return Fail(TEXT("the target could not be read"));
    }

    TMap<FString, FString> Written;
    auto Store = [&Target, &Existing, &Written](const FString& Id, const FString& Key)
    {
        TMap<FString, FString> After;
        if (!Target.StoreKey(Id, Key, After))
        {
            return false;
        }
        Existing = MoveTemp(After);
        Written.Add(Id, Key);
        return true;
    };
    auto IsKept = [&Existing](const FString& ProviderId, const FString& Key)
    {
        const FString ConflictPrefix = ProviderId + TEXT(".Conflict.");
        for (const TPair<FString, FString>& Entry : Existing)
        {
            if ((Entry.Key == ProviderId || Entry.Key.StartsWith(ConflictPrefix)) && Entry.Value == Key)
            {
                return true;
            }
        }
        return false;
    };
    auto KeepConflict = [&Existing, &Store, &Result, &IsKept](const FString& ProviderId, const FString& Key)
    {
        if (IsKept(ProviderId, Key))
        {
            return true;
        }
        FString Id;
        for (int32 Number = 1; Id.IsEmpty() || Existing.Contains(Id); ++Number)
        {
            Id = FString::Printf(TEXT("%s.Conflict.%d"), *ProviderId, Number);
        }
        if (!Store(Id, Key))
        {
            return false;
        }
        Result.ConflictIds.Add(Id);
        return true;
    };

    TArray<FString> Ids;
    FileKeys.GetKeys(Ids);
    Ids.Sort();
    for (const FString& Id : Ids)
    {
        const FString& Key = FileKeys[Id];
        if (Key.TrimStartAndEnd().IsEmpty())
        {
            continue;
        }
        const FString* Current = Existing.Find(Id);
        if (!Current || Current->TrimStartAndEnd().IsEmpty())
        {
            if (!Store(Id, Key))
            {
                return Fail(FString::Printf(TEXT("the %s key could not be written"), *Id));
            }
            Result.Moved.Add(Id);
        }
        else if (*Current != Key && !KeepConflict(Id, Key))
        {
            return Fail(FString::Printf(TEXT("the %s key differs from the one already stored and could not be kept beside it"), *Id));
        }
    }
    for (const TPair<FString, FString>& Conflict : File.LoadMigrationConflictKeys())
    {
        if (!Conflict.Value.TrimStartAndEnd().IsEmpty() && !KeepConflict(Conflict.Key, Conflict.Value))
        {
            return Fail(FString::Printf(TEXT("a %s key kept under MigrationConflicts could not be written"), *Conflict.Key));
        }
    }

    // Read every written key back before anything is deleted
    TMap<FString, FString> ReadBack;
    if (!Target.LoadAll(ReadBack))
    {
        return Fail(TEXT("the target could not be read back"));
    }
    for (const TPair<FString, FString>& Entry : Written)
    {
        const FString* Found = ReadBack.Find(Entry.Key);
        if (!Found || *Found != Entry.Value)
        {
            return Fail(FString::Printf(TEXT("the %s key did not read back unchanged"), *Entry.Key));
        }
    }

    if (Result.ConflictIds.Num() > 0)
    {
        FN2CLogger::Get().LogWarning(FString::Printf(
            TEXT("While moving API keys to %s, keys that differed from the ones already stored were kept as %s. The stored keys are used; remove the extra entries when they are not needed."),
            *Target.Describe(), *FString::Join(Result.ConflictIds, TEXT(", "))), LogContext);
    }

    IFileManager& FileManager = IFileManager::Get();
    FileManager.Delete(*FN2CFileUtils::TempPathFor(UserFile), false, true, true);
    FileManager.Delete(*FN2CFileUtils::BackupPathFor(UserFile), false, true, true);
    if (!FileManager.Delete(*UserFile, false, /*EvenReadOnly*/ true, /*Quiet*/ true) || FPaths::FileExists(UserFile))
    {
        FN2CLogger::Get().LogWarning(FString::Printf(
            TEXT("API keys were moved to %s, but %s could not be deleted, so a plain-text copy is still there. Delete it by hand."),
            *Target.Describe(), *UserFile), LogContext);
        return Result;
    }
    Result.bCompleted = true;

    TArray<FString> CorruptCopies;
    FileManager.FindFiles(CorruptCopies, *(UserFile + TEXT(".corrupt-*")), true, false);
    if (CorruptCopies.Num() > 0)
    {
        FN2CLogger::Get().LogWarning(FString::Printf(
            TEXT("%s still holds %s, a damaged secrets file that may contain API keys in plain text. Re-enter any key it held in Project Settings, then delete it."),
            *FPaths::GetPath(UserFile), *FString::Join(CorruptCopies, TEXT(", "))), LogContext);
    }

    FN2CLogger::Get().Log(FString::Printf(TEXT("Moved API keys (%s) from %s to %s and deleted the file"),
        Result.Moved.Num() > 0 ? *FString::Join(Result.Moved, TEXT(", ")) : TEXT("none new"), *UserFile, *Target.Describe()),
        EN2CLogSeverity::Info, LogContext);
    return Result;
}

FN2CSecretStore::FN2CSecretStore(TUniquePtr<IN2CSecretBackend> InBackend, FEnvironmentReader InEnvironmentReader)
    : Backend(MoveTemp(InBackend))
    , EnvironmentReader(MoveTemp(InEnvironmentReader))
{
    check(Backend.IsValid());
    if (!EnvironmentReader)
    {
        EnvironmentReader = [](const FString& VariableName) { return FPlatformMisc::GetEnvironmentVariable(*VariableName); };
    }
    Backend->LoadAll(Keys);
}

FString FN2CSecretStore::GetUserSecretsFilePath()
{
    return FPaths::ConvertRelativePathToFull(
        FPaths::Combine(FPlatformProcess::UserSettingsDir(), TEXT("NodeToCode"), TEXT("secrets.json")));
}

FString FN2CSecretStore::GetLegacyProjectSecretsFilePath()
{
    return FPaths::ConvertRelativePathToFull(
        FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("NodeToCode"), TEXT("User"), TEXT("secrets.json")));
}

FString FN2CSecretStore::GetProviderId(EN2CLLMProvider Provider)
{
    switch (Provider)
    {
        case EN2CLLMProvider::OpenAI: return TEXT("OpenAI");
        case EN2CLLMProvider::Anthropic: return TEXT("Anthropic");
        case EN2CLLMProvider::Gemini: return TEXT("Gemini");
        case EN2CLLMProvider::DeepSeek: return TEXT("DeepSeek");
        default: return FString(); // Ollama and LM Studio run locally without a key
    }
}

TArray<FString> FN2CSecretStore::GetEnvironmentVariableNames(const FString& ProviderId)
{
    if (ProviderId == TEXT("OpenAI")) { return { TEXT("OPENAI_API_KEY") }; }
    if (ProviderId == TEXT("Anthropic")) { return { TEXT("ANTHROPIC_API_KEY") }; }
    // GOOGLE_API_KEY is the name Google's own SDKs also read
    if (ProviderId == TEXT("Gemini")) { return { TEXT("GEMINI_API_KEY"), TEXT("GOOGLE_API_KEY") }; }
    // DEEPSEEK_API is the name some local .env files use
    if (ProviderId == TEXT("DeepSeek")) { return { TEXT("DEEPSEEK_API_KEY"), TEXT("DEEPSEEK_API") }; }
    return {};
}

TArray<FString> FN2CSecretStore::GetEnvironmentVariableNames(EN2CLLMProvider Provider)
{
    return GetEnvironmentVariableNames(GetProviderId(Provider));
}

FString FN2CSecretStore::FindEnvironmentKey(const FString& ProviderId, FString& OutVariableName) const
{
    OutVariableName.Reset();
    for (const FString& Name : GetEnvironmentVariableNames(ProviderId))
    {
        FString Value = EnvironmentReader(Name).TrimStartAndEnd();
        if (!Value.IsEmpty())
        {
            OutVariableName = Name;
            return Value;
        }
    }
    return FString();
}

FString FN2CSecretStore::GetStoredApiKey(const FString& ProviderId) const
{
    FScopeLock Lock(&Mutex);
    const FString* Key = Keys.Find(ProviderId);
    return Key ? *Key : FString();
}

FString FN2CSecretStore::GetStoredApiKey(EN2CLLMProvider Provider) const
{
    const FString Id = GetProviderId(Provider);
    return Id.IsEmpty() ? FString() : GetStoredApiKey(Id);
}

FString FN2CSecretStore::GetApiKey(const FString& ProviderId) const
{
    const FString Stored = GetStoredApiKey(ProviderId);
    if (!Stored.TrimStartAndEnd().IsEmpty())
    {
        return Stored;
    }

    FString VariableName;
    const FString FromEnvironment = FindEnvironmentKey(ProviderId, VariableName);
    if (FromEnvironment.IsEmpty())
    {
        return Stored;
    }

    bool bAlreadyLogged = false;
    {
        FScopeLock Lock(&Mutex);
        LoggedEnvironmentKeys.Add(ProviderId + TEXT("=") + VariableName, &bAlreadyLogged);
    }
    if (!bAlreadyLogged)
    {
        // The variable's name only: its value is a secret
        FN2CLogger::Get().Log(FString::Printf(
            TEXT("No %s API key is saved, so the %s environment variable is used. A key saved in Project Settings takes priority."),
            *ProviderId, *VariableName), EN2CLogSeverity::Info, LogContext);
    }
    return FromEnvironment;
}

FString FN2CSecretStore::GetApiKey(EN2CLLMProvider Provider) const
{
    const FString Id = GetProviderId(Provider);
    return Id.IsEmpty() ? FString() : GetApiKey(Id);
}

EN2CApiKeySource FN2CSecretStore::GetApiKeySource(const FString& ProviderId, FString* OutVariableName) const
{
    if (OutVariableName)
    {
        OutVariableName->Reset();
    }
    if (!GetStoredApiKey(ProviderId).TrimStartAndEnd().IsEmpty())
    {
        return EN2CApiKeySource::Store;
    }
    FString VariableName;
    if (FindEnvironmentKey(ProviderId, VariableName).IsEmpty())
    {
        return EN2CApiKeySource::None;
    }
    if (OutVariableName)
    {
        *OutVariableName = VariableName;
    }
    return EN2CApiKeySource::Environment;
}

EN2CApiKeySource FN2CSecretStore::GetApiKeySource(EN2CLLMProvider Provider, FString* OutVariableName) const
{
    return GetApiKeySource(GetProviderId(Provider), OutVariableName);
}

bool FN2CSecretStore::HasApiKey(EN2CLLMProvider Provider) const
{
    return !GetApiKey(Provider).IsEmpty();
}

bool FN2CSecretStore::SetApiKey(const FString& ProviderId, const FString& Key)
{
    if (ProviderId.IsEmpty())
    {
        return false;
    }
    FScopeLock Lock(&Mutex);
    TMap<FString, FString> Stored;
    if (Backend->StoreKey(ProviderId, Key, Stored))
    {
        // The backend re-read the store, so this also picks up keys another editor saved meanwhile
        Keys = MoveTemp(Stored);
        FN2CLogger::Get().Log(FString::Printf(TEXT("Saved the %s API key to %s"), *ProviderId, *Backend->Describe()),
            EN2CLogSeverity::Info, LogContext);
        return true;
    }
    Keys.Add(ProviderId, Key);
    FN2CLogger::Get().LogError(FString::Printf(
        TEXT("The %s API key could not be saved to %s; it is used until the editor closes."), *ProviderId, *Backend->Describe()),
        LogContext);
    return false;
}

bool FN2CSecretStore::SetApiKey(EN2CLLMProvider Provider, const FString& Key)
{
    return SetApiKey(GetProviderId(Provider), Key);
}

FString FN2CSecretStore::GetStoreDisplayName() const
{
    FScopeLock Lock(&Mutex);
    return Backend->GetDisplayName();
}

void FN2CSecretStore::Reload()
{
    FScopeLock Lock(&Mutex);
    TMap<FString, FString> Loaded;
    Backend->LoadAll(Loaded);
    Keys = MoveTemp(Loaded);
}
