// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/N2CBaseLLMService.h"
#include "LLM/N2CHttpHandler.h"
#include "LLM/N2CSystemPromptManager.h"
#include "LLM/N2CResponseParserBase.h"
#include "Utils/N2CLogger.h"
#include "Core/N2CSecretStore.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

FString UN2CBaseLLMService::GetMissingApiKeyError(EN2CLLMProvider Provider, const FString& ApiKey)
{
    if (!ApiKey.TrimStartAndEnd().IsEmpty())
    {
        return FString();
    }
    return IN2CCredentialSource::MakeMissingKeyMessage(Provider);
}

TSharedRef<const IN2CCredentialSource> UN2CBaseLLMService::GetCredentialSource() const
{
    return CredentialSource.IsValid() ? CredentialSource.ToSharedRef() : IN2CCredentialSource::Get();
}

bool UN2CBaseLLMService::Initialize(const FN2CLLMConfig& InConfig)
{
    Config = InConfig;
    
    // Use default endpoint if none provided
    if (Config.ApiEndpoint.IsEmpty())
    {
        Config.ApiEndpoint = GetDefaultEndpoint();
    }

    // Initialize components
    InitializeComponents();
    
    // The provider's non-secret headers; each request adds its own credential header (SendRequest)
    TMap<FString, FString> Headers;
    GetProviderHeaders(Headers);
    if (HttpHandler)
    {
        HttpHandler->ExtraHeaders = Headers;
    }

    bIsInitialized = true;
    return true;
}

void UN2CBaseLLMService::InitializeComponents()
{
    // Create HTTP handler
    HttpHandler = NewObject<UN2CHttpHandler>(this);
    if (!HttpHandler)
    {
        FN2CLogger::Get().LogError(TEXT("Failed to create HTTP handler"), TEXT("BaseLLMService"));
        return;
    }
    HttpHandler->Initialize(Config);

    // Create response parser
    ResponseParser = CreateResponseParser();
    if (!ResponseParser)
    {
        FN2CLogger::Get().LogError(TEXT("Failed to create response parser"), TEXT("BaseLLMService"));
        return;
    }
    ResponseParser->Initialize();

    // Create system prompt manager
    PromptManager = NewObject<UN2CSystemPromptManager>(this);
    if (!PromptManager)
    {
        FN2CLogger::Get().LogError(TEXT("Failed to create system prompt manager"), TEXT("BaseLLMService"));
        return;
    }
    PromptManager->Initialize(Config);
}

FN2CLLMRequestHandleRef UN2CBaseLLMService::SendRequest(
    const FString& JsonPayload,
    const FString& SystemMessage,
    const FOnLLMResponseReceived& OnComplete)
{
    if (!bIsInitialized)
    {
        FN2CLogger::Get().LogError(TEXT("Service not initialized"), TEXT("BaseLLMService"));
        FN2CLLMRequestHandleRef Handle = FN2CLLMRequestHandle::Create();
        Handle->Complete(OnComplete, TEXT("{\"error\": \"Service not initialized\"}"));
        return Handle;
    }
    
    // The key is read now, for this request only: a key changed since Initialize applies, and nothing keeps it after.
    // A source that answers later (a keychain, a token refresh) delivers through the same handle, so a Cancel() while
    // it resolves sends nothing.
    FN2CLLMRequestHandleRef Handle = FN2CLLMRequestHandle::Create();
    const EN2CLLMProvider Provider = GetProviderType();
    TWeakObjectPtr<UN2CBaseLLMService> WeakThis(this);
    GetCredentialSource()->Resolve(Provider,
        [WeakThis, Handle, JsonPayload, SystemMessage, OnComplete](FN2CCredentialResult Result)
        {
            if (Handle->IsFinished())
            {
                return;
            }
            UN2CBaseLLMService* StrongThis = WeakThis.Get();
            if (!StrongThis)
            {
                Handle->Complete(OnComplete, FN2CTranslationError::MakeErrorJson(TEXT("The LLM service was destroyed before the request was sent")));
                return;
            }
            if (Result.HasError())
            {
                // A cloud provider without a key would only get a 401 back; stop before any request is made
                const FString Message = Result.GetError().ToString();
                FN2CLogger::Get().LogError(Message, TEXT("BaseLLMService"));
                Handle->Complete(OnComplete, FN2CTranslationError::MakeErrorJson(Message, EN2CTranslationErrorKind::Auth));
                return;
            }
            StrongThis->SendWithCredential(Result.GetValue(), JsonPayload, SystemMessage, OnComplete, Handle);
        });
    return Handle;
}

void UN2CBaseLLMService::SendWithCredential(
    const FN2CCredential& Credential,
    const FString& JsonPayload,
    const FString& SystemMessage,
    const FOnLLMResponseReceived& OnComplete,
    const FN2CLLMRequestHandleRef& Handle)
{
    // Log provider, model and where the key came from (never the key)
    FString KeyOrigin;
    switch (Credential.Origin)
    {
    case EN2CApiKeySource::Store: KeyOrigin = TEXT("saved key"); break;
    case EN2CApiKeySource::Environment: KeyOrigin = FString::Printf(TEXT("key from %s"), *Credential.VariableName); break;
    default: KeyOrigin = TEXT("no key"); break;
    }
    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Sending request to %s using model: %s (%s)"),
            *UEnum::GetValueAsString(GetProviderType()), *Config.Model, *KeyOrigin),
        EN2CLogSeverity::Info,
        TEXT("BaseLLMService")
    );

    // Format request payload
    FString FormattedPayload = FormatRequestPayload(JsonPayload, SystemMessage);

    // Get endpoint
    FString Endpoint;
    bool bSupportsSystemPrompts;
    GetConfiguration(Endpoint, bSupportsSystemPrompts);

    if (!HttpHandler)
    {
        FN2CLogger::Get().LogError(TEXT("No HTTP handler"), TEXT("BaseLLMService"));
        Handle->Complete(OnComplete, TEXT("{\"error\": \"Service has no HTTP handler\"}"));
        return;
    }

    // Send request through HTTP handler, delivering through this request's handle
    HttpHandler->PostLLMRequest(
        Endpoint,
        Credential,
        FormattedPayload,
        OnComplete,
        Handle
    );
}
