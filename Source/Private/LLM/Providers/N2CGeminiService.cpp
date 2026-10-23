// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/Providers/N2CGeminiService.h"

#include "Auth/N2CGoogleOAuthTokenManager.h"
#include "Core/N2CSettings.h"
#include "LLM/N2CLLMModelRegistry.h"
#include "LLM/N2CSystemPromptManager.h"
#include "Utils/N2CLogger.h"

UN2CResponseParserBase* UN2CGeminiService::CreateResponseParser()
{
    UN2CGeminiResponseParser* Parser = NewObject<UN2CGeminiResponseParser>(this);
    return Parser;
}

void UN2CGeminiService::GetConfiguration(
    FString& OutEndpoint,
    FString& OutAuthToken,
    bool& OutSupportsSystemPrompts)
{
    if (IsUsingOAuth())
    {
        // OAuth uses Google Code Assist API endpoint (same as gemini-cli)
        // The direct generativelanguage.googleapis.com API doesn't accept OAuth tokens
        OutEndpoint = TEXT("https://cloudcode-pa.googleapis.com/v1internal:generateContent");
        OutAuthToken = TEXT("");  // Will add Bearer token in GetProviderHeaders
        FN2CLogger::Get().Log(TEXT("Using Google OAuth with Code Assist API for Gemini"), EN2CLogSeverity::Debug);
    }
    else
    {
        // Standard Generative Language API. The key goes in the x-goog-api-key header (GetProviderHeaders),
        // never in the URL: UE's HTTP module logs full URLs on timeouts and failures.
        OutEndpoint = FString::Printf(TEXT("%s%s:generateContent"), *Config.ApiEndpoint, *Config.Model);
        OutAuthToken = TEXT("");  // Gemini takes the key in x-goog-api-key, not a Bearer header
    }

    // Default to supporting system prompts since all Gemini models currently support system prompts
    OutSupportsSystemPrompts = true;
}

void UN2CGeminiService::GetProviderHeaders(TMap<FString, FString>& OutHeaders) const
{
    OutHeaders.Add(TEXT("Content-Type"), TEXT("application/json"));

    if (IsUsingOAuth())
    {
        UN2CGoogleOAuthTokenManager* TokenManager = UN2CGoogleOAuthTokenManager::Get();
        if (TokenManager)
        {
            FString AccessToken = TokenManager->GetAccessToken();
            if (!AccessToken.IsEmpty())
            {
                OutHeaders.Add(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *AccessToken));
            }
        }
    }
    else if (!Config.ApiKey.IsEmpty())
    {
        OutHeaders.Add(TEXT("x-goog-api-key"), Config.ApiKey);
    }
}

bool UN2CGeminiService::IsUsingOAuth() const
{
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    return Settings && Settings->IsUsingGeminiOAuth();
}

FString UN2CGeminiService::FormatRequestPayload(const FString& UserMessage, const FString& SystemMessage) const
{
    // Create and configure payload builder
    UN2CLLMPayloadBuilder* PayloadBuilder = NewObject<UN2CLLMPayloadBuilder>();
    PayloadBuilder->Initialize(Config.Model);
    PayloadBuilder->ConfigureForGemini();

    // Max output tokens from the registry default (16000 for unregistered IDs). Temperature is applied by
    // ConfigureForGemini only where the registry allows it.
    const FN2CModelMetadata* ModelMeta = FN2CLLMModelRegistry::Get().GetModelMetadataById(Config.Model);
    PayloadBuilder->SetMaxTokens(ModelMeta ? ModelMeta->DefaultMaxTokens : 16000);

    // Try prepending source files to the user message
    FString FinalUserMessage = UserMessage;
    PromptManager->PrependSourceFilesToUserMessage(FinalUserMessage);

    // Add system message and user message
    PayloadBuilder->AddSystemMessage(SystemMessage);
    PayloadBuilder->AddUserMessage(FinalUserMessage);

    // Add JSON schema for response format if model supports it
    if (Config.Model != TEXT("gemini-2.0-flash-thinking-exp-01-21"))
    {
        PayloadBuilder->SetJsonResponseFormat(UN2CLLMPayloadBuilder::GetN2CResponseSchema());
    }

    // Build the base payload
    FString BasePayload = PayloadBuilder->Build();

    // If using OAuth, wrap the payload for the Code Assist API format
    if (IsUsingOAuth())
    {
        // Code Assist API expects: { "model": "...", "project": "...", "request": { <gemini payload> } }
        TSharedPtr<FJsonObject> WrapperObject = MakeShareable(new FJsonObject());

        // Normalize model name for Code Assist API
        // The API uses different model names than the direct Generative Language API
        FString ModelName = Config.Model;

        // Map preview models with date suffixes to their Code Assist equivalents
        // Gemini 3 models (no normalization needed - already in correct format)
        if (ModelName.Contains(TEXT("gemini-3-pro-preview")))
        {
            ModelName = TEXT("gemini-3-pro-preview");
        }
        else if (ModelName.Contains(TEXT("gemini-3-flash-preview")))
        {
            ModelName = TEXT("gemini-3-flash-preview");
        }
        // Gemini 2.5 models
        else if (ModelName.Contains(TEXT("gemini-2.5-flash-preview")))
        {
            ModelName = TEXT("gemini-2.5-flash-preview");
        }
        else if (ModelName.Contains(TEXT("gemini-2.5-pro-preview")))
        {
            ModelName = TEXT("gemini-2.5-pro-preview");
        }
        // Gemini 2.0 models
        else if (ModelName.Contains(TEXT("gemini-2.0-flash-lite")))
        {
            ModelName = TEXT("gemini-2.0-flash-lite");
        }
        else if (ModelName.Contains(TEXT("gemini-2.0-pro-exp")))
        {
            ModelName = TEXT("gemini-2.0-pro-exp");
        }
        else if (ModelName.Contains(TEXT("gemini-2.0-flash-thinking")))
        {
            ModelName = TEXT("gemini-2.0-flash-thinking-exp");
        }

        FN2CLogger::Get().Log(FString::Printf(TEXT("Code Assist API model: %s (original: %s)"),
            *ModelName, *Config.Model), EN2CLogSeverity::Debug);

        WrapperObject->SetStringField(TEXT("model"), ModelName);

        // Include project ID if available
        UN2CGoogleOAuthTokenManager* TokenManager = UN2CGoogleOAuthTokenManager::Get();
        if (TokenManager)
        {
            FString ProjectId = TokenManager->GetProjectId();
            if (!ProjectId.IsEmpty())
            {
                WrapperObject->SetStringField(TEXT("project"), ProjectId);
            }
        }

        // Parse the base payload and set it as the "request" field
        TSharedPtr<FJsonObject> BasePayloadObject;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BasePayload);
        if (FJsonSerializer::Deserialize(Reader, BasePayloadObject) && BasePayloadObject.IsValid())
        {
            // Remove the "model" field from the inner request - Code Assist API expects it at the top level only
            if (BasePayloadObject->HasField(TEXT("model")))
            {
                BasePayloadObject->RemoveField(TEXT("model"));
            }
            WrapperObject->SetObjectField(TEXT("request"), BasePayloadObject);
        }

        // Serialize the wrapper
        FString WrappedPayload;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&WrappedPayload);
        FJsonSerializer::Serialize(WrapperObject.ToSharedRef(), Writer);

        return WrappedPayload;
    }

    return BasePayload;
}
