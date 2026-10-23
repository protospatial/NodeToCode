// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CUserSecrets.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Utils/N2CLogger.h"

namespace
{
    /** The API key fields, in file order */
    const TCHAR* const ApiKeyFields[] = {
        TEXT("OpenAI_API_Key"), TEXT("Anthropic_API_Key"), TEXT("Gemini_API_Key"), TEXT("DeepSeek_API_Key")
    };

    /** Sign-in token fields written before 2.0: the nested block and the older flat fields */
    const TCHAR* const RetiredSignInFields[] = {
        TEXT("OAuth"),
        TEXT("Claude_OAuth_AccessToken"), TEXT("Claude_OAuth_RefreshToken"), TEXT("Claude_OAuth_ExpiresAt"), TEXT("Claude_OAuth_Scope"),
        TEXT("Google_OAuth_AccessToken"), TEXT("Google_OAuth_RefreshToken"), TEXT("Google_OAuth_ExpiresAt"), TEXT("Google_OAuth_Scope"),
    };

    FString GetKeyField(const FJsonObject& Json, const TCHAR* Field)
    {
        FString Value;
        Json.TryGetStringField(Field, Value);
        return Value;
    }
}

UN2CUserSecrets::UN2CUserSecrets()
{
    // Load secrets when the object is created
    LoadSecrets();
}

FString UN2CUserSecrets::GetSecretsFilePath()
{
    return FPaths::ConvertRelativePathToFull(
        FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("NodeToCode"), TEXT("User"), TEXT("secrets.json")));
}

void UN2CUserSecrets::EnsureDirectoryExists(const FString& FilePath)
{
    const FString Dir = FPaths::GetPath(FilePath);
    if (!Dir.IsEmpty() && !IFileManager::Get().DirectoryExists(*Dir))
    {
        IFileManager::Get().MakeDirectory(*Dir, true);
        FN2CLogger::Get().Log(FString::Printf(TEXT("Created secrets directory: %s"), *Dir), EN2CLogSeverity::Info);
    }
}

bool UN2CUserSecrets::ReadJsonFile(const FString& FilePath, TSharedPtr<FJsonObject>& OutJson)
{
    OutJson.Reset();
    FString JsonString;
    if (!FPaths::FileExists(FilePath) || !FFileHelper::LoadFileToString(JsonString, *FilePath))
    {
        return false;
    }
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    return FJsonSerializer::Deserialize(Reader, OutJson) && OutJson.IsValid();
}

bool UN2CUserSecrets::WriteJsonFile(const FString& FilePath, const TSharedRef<FJsonObject>& Json)
{
    FString JsonString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
    if (!FJsonSerializer::Serialize(Json, Writer))
    {
        FN2CLogger::Get().LogError(TEXT("Failed to serialize secrets to JSON"));
        return false;
    }

    EnsureDirectoryExists(FilePath);
    const FString TempPath = FilePath + TEXT(".tmp");
    if (!FFileHelper::SaveStringToFile(JsonString, *TempPath)
        || !IFileManager::Get().Move(*FilePath, *TempPath, /*bReplace*/ true))
    {
        IFileManager::Get().Delete(*TempPath);
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to save secrets to: %s"), *FilePath));
        return false;
    }
    return true;
}

int32 UN2CUserSecrets::RemoveRetiredSignInEntries(FJsonObject& Json)
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

void UN2CUserSecrets::LoadSecrets()
{
    LoadSecretsFromFile(GetSecretsFilePath());
}

bool UN2CUserSecrets::LoadSecretsFromFile(const FString& FilePath)
{
    if (!FPaths::FileExists(FilePath))
    {
        FN2CLogger::Get().Log(FString::Printf(TEXT("Secrets file not found at: %s"), *FilePath), EN2CLogSeverity::Info);
        return false;
    }

    TSharedPtr<FJsonObject> Json;
    if (!ReadJsonFile(FilePath, Json))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to load or parse secrets JSON from: %s"), *FilePath));
        return false;
    }

    OpenAI_API_Key = GetKeyField(*Json, ApiKeyFields[0]);
    Anthropic_API_Key = GetKeyField(*Json, ApiKeyFields[1]);
    Gemini_API_Key = GetKeyField(*Json, ApiKeyFields[2]);
    DeepSeek_API_Key = GetKeyField(*Json, ApiKeyFields[3]);

    // Sign-in tokens from before 2.0 are deleted from the file; the parsed object keeps every other field
    // (the API keys included), so writing it back loses nothing else
    if (RemoveRetiredSignInEntries(*Json) > 0)
    {
        if (WriteJsonFile(FilePath, Json.ToSharedRef()))
        {
            FN2CLogger::Get().LogWarning(FString::Printf(
                TEXT("Claude Pro/Max and Google account sign-in were removed in NodeToCode 2.0, so the saved sign-in tokens were deleted from %s (API keys kept). Set an API key in Project Settings > Plugins > Node to Code > LLM Services. See %s"),
                *FilePath, N2CSignInRemoval::DocsUrl), TEXT("UserSecrets"));
        }
        else
        {
            static bool bWarned = false;
            if (!bWarned)
            {
                bWarned = true;
                FN2CLogger::Get().LogWarning(FString::Printf(
                    TEXT("%s still holds sign-in tokens that NodeToCode 2.0 no longer uses, and they could not be removed; they are ignored. See %s"),
                    *FilePath, N2CSignInRemoval::DocsUrl), TEXT("UserSecrets"));
            }
        }
    }

    FN2CLogger::Get().Log(FString::Printf(TEXT("Successfully loaded secrets from: %s"), *FilePath), EN2CLogSeverity::Info);
    return true;
}

void UN2CUserSecrets::SaveSecrets()
{
    SaveSecretsToFile(GetSecretsFilePath());
}

bool UN2CUserSecrets::SaveSecretsToFile(const FString& FilePath) const
{
    // Read-modify-write: keep the fields this version does not write
    TSharedPtr<FJsonObject> Json;
    if (!ReadJsonFile(FilePath, Json))
    {
        Json = MakeShared<FJsonObject>();
    }
    RemoveRetiredSignInEntries(*Json);

    Json->SetStringField(ApiKeyFields[0], OpenAI_API_Key);
    Json->SetStringField(ApiKeyFields[1], Anthropic_API_Key);
    Json->SetStringField(ApiKeyFields[2], Gemini_API_Key);
    Json->SetStringField(ApiKeyFields[3], DeepSeek_API_Key);

    if (!WriteJsonFile(FilePath, Json.ToSharedRef()))
    {
        return false;
    }
    FN2CLogger::Get().Log(FString::Printf(TEXT("Successfully saved secrets to: %s"), *FilePath), EN2CLogSeverity::Info);
    return true;
}
