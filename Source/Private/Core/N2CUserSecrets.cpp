// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CUserSecrets.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Utils/N2CLogger.h"

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

void UN2CUserSecrets::EnsureSecretsDirectoryExists()
{
    FString SecretsDir = FPaths::ConvertRelativePathToFull(
        FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("NodeToCode"), TEXT("User")));
    
    if (!IFileManager::Get().DirectoryExists(*SecretsDir))
    {
        IFileManager::Get().MakeDirectory(*SecretsDir, true);
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Created secrets directory: %s"), *SecretsDir),
            EN2CLogSeverity::Info);
    }
}

void UN2CUserSecrets::LoadSecrets()
{
    FString SecretsFilePath = GetSecretsFilePath();
    
    // Check if the file exists
    if (!FPaths::FileExists(SecretsFilePath))
    {
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Secrets file not found at: %s"), *SecretsFilePath),
            EN2CLogSeverity::Info);
        return;
    }
    
    // Load the file content
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *SecretsFilePath))
    {
        FN2CLogger::Get().LogError(
            FString::Printf(TEXT("Failed to load secrets from: %s"), *SecretsFilePath));
        return;
    }
    
    // Parse the JSON
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
    {
        FN2CLogger::Get().LogError(
            FString::Printf(TEXT("Failed to parse secrets JSON from: %s"), *SecretsFilePath));
        return;
    }
    
    // Extract the API keys
    OpenAI_API_Key = JsonObject->GetStringField(TEXT("OpenAI_API_Key"));
    Anthropic_API_Key = JsonObject->GetStringField(TEXT("Anthropic_API_Key"));
    Gemini_API_Key = JsonObject->GetStringField(TEXT("Gemini_API_Key"));
    DeepSeek_API_Key = JsonObject->GetStringField(TEXT("DeepSeek_API_Key"));
    
    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Successfully loaded secrets from: %s"), *SecretsFilePath),
        EN2CLogSeverity::Info);
}

void UN2CUserSecrets::SaveSecrets()
{
    // Ensure the directory exists
    EnsureSecretsDirectoryExists();
    
    // Create a JSON object
    TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
    JsonObject->SetStringField(TEXT("OpenAI_API_Key"), OpenAI_API_Key);
    JsonObject->SetStringField(TEXT("Anthropic_API_Key"), Anthropic_API_Key);
    JsonObject->SetStringField(TEXT("Gemini_API_Key"), Gemini_API_Key);
    JsonObject->SetStringField(TEXT("DeepSeek_API_Key"), DeepSeek_API_Key);
    
    // Serialize to string
    FString JsonString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
    if (!FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer))
    {
        FN2CLogger::Get().LogError(TEXT("Failed to serialize secrets to JSON"));
        return;
    }
    
    // Save to file
    FString SecretsFilePath = GetSecretsFilePath();
    if (!FFileHelper::SaveStringToFile(JsonString, *SecretsFilePath))
    {
        FN2CLogger::Get().LogError(
            FString::Printf(TEXT("Failed to save secrets to: %s"), *SecretsFilePath));
        return;
    }
    
    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Successfully saved secrets to: %s"), *SecretsFilePath),
        EN2CLogSeverity::Info);
}
