// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "N2CUserSecrets.generated.h"

class FJsonObject;

namespace N2CSignInRemoval
{
    /** Wiki section on the Claude Pro/Max and Google account sign-in that NodeToCode 2.0 removed */
    inline const TCHAR* const DocsUrl = TEXT("https://github.com/protospatial/NodeToCode/wiki/Provider-Configuration#subscription-sign-in-removed-in-20");
}

/**
 * @class UN2CUserSecrets
 * @brief Stores the provider API keys
 *
 * Uses a JSON file (Saved/NodeToCode/User/secrets.json) instead of Unreal's config system to ensure consistent
 * behavior across engine versions:
 * {
 *     "OpenAI_API_Key": "...",
 *     "Anthropic_API_Key": "...",
 *     "Gemini_API_Key": "...",
 *     "DeepSeek_API_Key": "..."
 * }
 * Saving is a read-modify-write: fields this class does not know are kept.
 */

UCLASS()
class NODETOCODE_API UN2CUserSecrets : public UObject
{
    GENERATED_BODY()
public:
    UN2CUserSecrets();

    /** Load API keys from storage */
    void LoadSecrets();

    /** Save API keys to storage */
    void SaveSecrets();

    /** Get the path to the secrets file */
    static FString GetSecretsFilePath();

    /**
     * Loads the API keys from FilePath. Sign-in tokens saved by earlier versions (the "OAuth" block
     * and the flat Claude_OAuth_* and Google_OAuth_* fields) are removed from the file by a read-modify-write
     * that keeps every other field, and the removal is logged once.
     * @return true if the file exists and was parsed
     */
    bool LoadSecretsFromFile(const FString& FilePath);

    /** Writes the API keys to FilePath, keeping any other field already in the file */
    bool SaveSecretsToFile(const FString& FilePath) const;

    /**
     * Removes the sign-in token entries NodeToCode 2.0 no longer uses from Json.
     * @return the number of top-level fields removed
     */
    static int32 RemoveRetiredSignInEntries(FJsonObject& Json);

    /** OpenAI API Key */
    UPROPERTY(EditAnywhere, Category = "Node to Code | API Keys")
    FString OpenAI_API_Key;

    /** Anthropic API Key */
    UPROPERTY(EditAnywhere, Category = "Node to Code | API Keys")
    FString Anthropic_API_Key;

    /** Gemini API Key */
    UPROPERTY(EditAnywhere, Category = "Node to Code | API Keys")
    FString Gemini_API_Key;

    /** DeepSeek API Key */
    UPROPERTY(EditAnywhere, Category = "Node to Code | API Keys")
    FString DeepSeek_API_Key;

private:
    /** Ensure the directory holding FilePath exists */
    static void EnsureDirectoryExists(const FString& FilePath);

    /** Parses FilePath into OutJson; false if it is missing or not a JSON object */
    static bool ReadJsonFile(const FString& FilePath, TSharedPtr<FJsonObject>& OutJson);

    /** Serializes Json to FilePath through a temporary file, so a failed write leaves the old file in place */
    static bool WriteJsonFile(const FString& FilePath, const TSharedRef<FJsonObject>& Json);
};
