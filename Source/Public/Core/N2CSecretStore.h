// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Templates/UniquePtr.h"
#include "LLM/N2CLLMTypes.h"

class FJsonObject;

namespace N2CSignInRemoval
{
    /** Wiki section on the Claude Pro/Max and Google account sign-in that NodeToCode 2.0 removed */
    inline const TCHAR* const DocsUrl = TEXT("https://github.com/protospatial/NodeToCode/wiki/Provider-Configuration#subscription-sign-in-removed-in-20");
}

/**
 * Where the API keys are kept. FN2CSecretStore owns exactly one backend; the OS keychain backend (card 0161)
 * implements this same interface. Provider ids are "OpenAI", "Anthropic", "Gemini", "DeepSeek" (see
 * FN2CSecretStore::GetProviderId); a backend keeps entries for ids it does not know.
 */
class NODETOCODE_API IN2CSecretBackend
{
public:
    virtual ~IN2CSecretBackend() = default;

    /** Where the keys live, for log lines. Never contains a key. */
    virtual FString Describe() const = 0;

    /**
     * Reads every stored key into OutKeys, keyed by provider id.
     * @return false if the store exists but could not be read; OutKeys is then empty
     */
    virtual bool LoadAll(TMap<FString, FString>& OutKeys) = 0;

    /**
     * Stores Key for ProviderId (an empty Key clears it), leaving every other entry as it is now in the store,
     * not as it was when it was loaded. OutKeys receives the store's contents after the write.
     */
    virtual bool StoreKey(const FString& ProviderId, const FString& Key, TMap<FString, FString>& OutKeys) = 0;
};

/**
 * Per-user JSON file backend (the Phase 0 store, review decision dec-10): by default
 * FPlatformProcess::UserSettingsDir()/NodeToCode/secrets.json, outside every project.
 * {
 *     "Version": 1,
 *     "OpenAI_API_Key": "...",
 *     "Anthropic_API_Key": "...",
 *     "Gemini_API_Key": "...",
 *     "DeepSeek_API_Key": "..."
 * }
 * - Every write re-reads the file, changes one field and keeps every other one (unknown fields included), writes
 *   secrets.json.tmp, copies the previous file to secrets.json.bak and moves the temporary file over secrets.json.
 * - A file that does not parse is renamed to secrets.json.corrupt-<UTC timestamp>, an error is logged, and it is
 *   never written over.
 * - If LegacyFilePath (the project's Saved/NodeToCode/User/secrets.json, where 1.x and early 2.0 kept keys) exists
 *   when the keys are loaded, it is merged in and deleted: values already in the per-user file win, and a project key
 *   that differs is kept under "MigrationConflicts" in the per-user file. Logs name the provider, never the key.
 */
class NODETOCODE_API FN2CSecretFileBackend : public IN2CSecretBackend
{
public:
    /** Version written to the file */
    static constexpr int32 FileVersion = 1;

    explicit FN2CSecretFileBackend(const FString& InFilePath, const FString& InLegacyFilePath = FString());

    virtual FString Describe() const override;
    virtual bool LoadAll(TMap<FString, FString>& OutKeys) override;
    virtual bool StoreKey(const FString& ProviderId, const FString& Key, TMap<FString, FString>& OutKeys) override;

    const FString& GetFilePath() const { return FilePath; }

    /** The JSON field that holds ProviderId's key ("<ProviderId>_API_Key") */
    static FString GetKeyField(const FString& ProviderId);

    /**
     * Removes the sign-in token entries NodeToCode 2.0 no longer uses (the "OAuth" block and the flat Claude_OAuth_*
     * and Google_OAuth_* fields) from Json.
     * @return the number of top-level fields removed
     */
    static int32 RemoveRetiredSignInEntries(FJsonObject& Json);

private:
    enum class EReadResult : uint8 { Missing, Parsed, Unreadable, Corrupt };

    static EReadResult ReadJsonFile(const FString& Path, TSharedPtr<FJsonObject>& OutJson);

    /** Reads the file for a read-modify-write: a missing file gives an empty object, a corrupt one is set aside first */
    bool ReadForWrite(TSharedPtr<FJsonObject>& OutJson) const;

    /** Renames a file that does not parse to <file>.corrupt-<timestamp> and logs it */
    bool QuarantineCorruptFile() const;

    /** Writes Json through secrets.json.tmp, keeping the previous file as secrets.json.bak */
    bool WriteJsonFile(const TSharedRef<FJsonObject>& Json) const;

    /** Finishes a write that stopped after the temporary file was complete but before it replaced the file */
    void RecoverInterruptedWrite() const;

    /** Merges LegacyFilePath into the file, then deletes it */
    void MigrateLegacyFile() const;

    static void ExtractKeys(const FJsonObject& Json, TMap<FString, FString>& OutKeys);

    FString FilePath;
    FString LegacyFilePath;
};

/** Where the API key a provider's requests use comes from */
enum class EN2CApiKeySource : uint8
{
    /** No key: neither the store nor the environment has one */
    None,
    /** The secret store (the key typed in Project Settings) */
    Store,
    /** An environment variable of the editor process (see FN2CSecretStore::GetEnvironmentVariableNames) */
    Environment,
};

/**
 * The one owner of the provider API keys in this process. Every read and write of a key goes through Get(); nothing
 * else keeps a copy that it later writes back, so one key edit cannot undo another.
 *
 * When the store has no key for a provider, GetApiKey falls back to the provider's environment variable
 * (ANTHROPIC_API_KEY, OPENAI_API_KEY, GEMINI_API_KEY, DEEPSEEK_API_KEY or DEEPSEEK_API), so CI, build machines and
 * agents can supply keys without typing them into a field. A stored key always wins. Environment keys are never
 * written to the store, and logs name the variable, never its value.
 */
class NODETOCODE_API FN2CSecretStore
{
public:
    /** Reads one environment variable; empty when it is not set */
    using FEnvironmentReader = TFunction<FString(const FString& VariableName)>;

    /** The process-wide store, backed by the per-user file. The first call loads it and migrates a project file. */
    static FN2CSecretStore& Get();

    /**
     * A store over a given backend (tests use one with a temporary folder).
     * @param InEnvironmentReader reads environment variables; unset means the process environment
     */
    explicit FN2CSecretStore(TUniquePtr<IN2CSecretBackend> InBackend, FEnvironmentReader InEnvironmentReader = FEnvironmentReader());

    /** The id a provider's key is stored under, or empty for providers without a key (Ollama, LM Studio) */
    static FString GetProviderId(EN2CLLMProvider Provider);

    /** The environment variables read for ProviderId when the store has no key, in the order they are tried */
    static TArray<FString> GetEnvironmentVariableNames(const FString& ProviderId);
    static TArray<FString> GetEnvironmentVariableNames(EN2CLLMProvider Provider);

    /** The key requests use: the stored key, else the first set environment variable, else empty */
    FString GetApiKey(const FString& ProviderId) const;
    FString GetApiKey(EN2CLLMProvider Provider) const;
    bool HasApiKey(EN2CLLMProvider Provider) const;

    /** The stored key only, without the environment fallback (what the settings field shows and edits) */
    FString GetStoredApiKey(const FString& ProviderId) const;
    FString GetStoredApiKey(EN2CLLMProvider Provider) const;

    /**
     * Where GetApiKey's key for ProviderId comes from.
     * @param OutVariableName receives the variable's name when the source is Environment
     */
    EN2CApiKeySource GetApiKeySource(const FString& ProviderId, FString* OutVariableName = nullptr) const;
    EN2CApiKeySource GetApiKeySource(EN2CLLMProvider Provider, FString* OutVariableName = nullptr) const;

    /**
     * Sets one provider's key and saves it at once. If the save fails the key is still used for this session and an
     * error is logged.
     * @return true if the key was saved
     */
    bool SetApiKey(const FString& ProviderId, const FString& Key);
    bool SetApiKey(EN2CLLMProvider Provider, const FString& Key);

    /** Re-reads the backend (for example after another editor changed the file) */
    void Reload();

    /** Where the per-user file lives: FPlatformProcess::UserSettingsDir()/NodeToCode/secrets.json */
    static FString GetUserSecretsFilePath();

    /** Where 1.x and early 2.0 kept keys: <Project>/Saved/NodeToCode/User/secrets.json */
    static FString GetLegacyProjectSecretsFilePath();

private:
    /** The first set environment variable for ProviderId: its value, and its name in OutVariableName */
    FString FindEnvironmentKey(const FString& ProviderId, FString& OutVariableName) const;

    mutable FCriticalSection Mutex;
    TUniquePtr<IN2CSecretBackend> Backend;
    TMap<FString, FString> Keys;
    FEnvironmentReader EnvironmentReader;

    /** "<ProviderId>=<variable>" pairs already logged, so the environment fallback is logged once per variable */
    mutable TSet<FString> LoggedEnvironmentKeys;
};
