// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/N2CBaseLLMService.h"

#include "Core/N2CRequestSettings.h"
#include "Core/N2CSettings.h"
#include "LLM/N2CHttpHandler.h"
#include "LLM/N2CSystemPromptManager.h"
#include "LLM/N2CResponseParserBase.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Utils/N2CLogger.h"

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
    
    // Set provider-specific headers
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

void UN2CBaseLLMService::ResendFormattedRequest(
    const FString& FormattedPayload,
    const FOnLLMResponseReceived& OnComplete)
{
    if (!bIsInitialized || !HttpHandler)
    {
        FN2CLogger::Get().LogError(TEXT("Cannot replay request because the service is not initialized"), TEXT("BaseLLMService"));
        OnComplete.ExecuteIfBound(TEXT("{\"error\": \"Service not initialized\"}"));
        return;
    }

    LastFormattedRequestPayload = FormattedPayload;

    FString Endpoint, AuthToken;
    bool bSupportsSystemPrompts = false;
    GetConfiguration(Endpoint, AuthToken, bSupportsSystemPrompts);

    HttpHandler->PostLLMRequest(
        Endpoint,
        AuthToken,
        FormattedPayload,
        OnComplete);
}

FString UN2CBaseLLMService::BuildFormattedRequestPayload(
    const FString& JsonPayload,
    const FString& SystemMessage)
{
    const UN2CSettings* RequestSettings = GetDefault<UN2CSettings>();

    // Compose request-wide custom instructions only after the concrete provider service has been
    // initialized. This ensures model-specific matching uses the model that will actually receive
    // the request, including transient provider choices and named custom providers.
    FString EffectiveSystemMessage = SystemMessage;
    if (RequestSettings)
    {
        const FString CustomInstructions = RequestSettings->GetEffectiveCustomInstructions(
            Config.Provider,
            Config.Model);

        if (!CustomInstructions.IsEmpty())
        {
            if (!EffectiveSystemMessage.IsEmpty())
            {
                EffectiveSystemMessage += TEXT("\n\n");
            }

            EffectiveSystemMessage += TEXT("<customInstructions>\n");
            EffectiveSystemMessage += CustomInstructions;
            EffectiveSystemMessage += TEXT("\n</customInstructions>");

            FN2CLogger::Get().Log(
                FString::Printf(TEXT("Applied custom instructions for model: %s"), *Config.Model),
                EN2CLogSeverity::Debug,
                TEXT("BaseLLMService"));
        }
    }

    // Per-operation ad hoc instructions are always additive. They are separate from persistent
    // global/model instructions so model-level replacement semantics cannot discard request-specific
    // guidance entered immediately before dispatch.
    const FString AdHocInstructions = FN2CRequestRuntime::GetAdHocInstructions().TrimStartAndEnd();
    if (!AdHocInstructions.IsEmpty())
    {
        if (!EffectiveSystemMessage.IsEmpty())
        {
            EffectiveSystemMessage += TEXT("\n\n");
        }

        EffectiveSystemMessage += TEXT("<adHocInstructions>\n");
        EffectiveSystemMessage += AdHocInstructions;
        EffectiveSystemMessage += TEXT("\n</adHocInstructions>");

        FN2CLogger::Get().Log(
            TEXT("Applied ad hoc instructions for the current translation operation"),
            EN2CLogSeverity::Debug,
            TEXT("BaseLLMService"));
    }

    // Ad-hoc attachments selected in the provider picker apply only to this translation operation.
    // Persistent Reference Source Files continue to be added by the provider's prompt manager. Skip
    // duplicates here if the same file is already configured globally.
    FString EffectiveUserMessage = JsonPayload;
    FString AdHocContext;

    auto NormalizeContextPath = [](const FString& InPath)
    {
        FString Normalized = FPaths::ConvertRelativePathToFull(InPath);
        FPaths::NormalizeFilename(Normalized);
        return Normalized;
    };

    TSet<FString> GlobalReferencePaths;
    if (RequestSettings)
    {
        for (const FFilePath& GlobalPath : RequestSettings->ReferenceSourceFilePaths)
        {
            if (!GlobalPath.FilePath.IsEmpty())
            {
                GlobalReferencePaths.Add(NormalizeContextPath(GlobalPath.FilePath).ToLower());
            }
        }
    }

    int32 LoadedAdHocFileCount = 0;
    for (const FString& AttachedPath : FN2CRequestRuntime::GetAdditionalContextFilePaths())
    {
        const FString NormalizedPath = NormalizeContextPath(AttachedPath);
        if (GlobalReferencePaths.Contains(NormalizedPath.ToLower()))
        {
            continue;
        }

        FString Content;
        if (!FFileHelper::LoadFileToString(Content, *NormalizedPath))
        {
            FN2CLogger::Get().LogWarning(
                FString::Printf(TEXT("Failed to load ad-hoc context file: %s"), *NormalizedPath),
                TEXT("BaseLLMService"));
            continue;
        }

        if (!AdHocContext.IsEmpty())
        {
            AdHocContext += TEXT("\n\n");
        }

        AdHocContext += FString::Printf(
            TEXT("File: %s\n```\n%s\n```"),
            *FPaths::GetCleanFilename(NormalizedPath),
            *Content);
        ++LoadedAdHocFileCount;
    }

    if (!AdHocContext.IsEmpty())
    {
        EffectiveUserMessage = FString::Printf(
            TEXT("<adHocContextFiles>\n%s\n</adHocContextFiles>\n\n%s"),
            *AdHocContext,
            *EffectiveUserMessage);

        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Attached %d ad-hoc context file(s) to request"), LoadedAdHocFileCount),
            EN2CLogSeverity::Debug,
            TEXT("BaseLLMService"));
    }

    // Providers without a separate system-message channel already merge this system message into
    // the user content in their provider-specific payload builder.
    LastFormattedRequestPayload = FormatRequestPayload(EffectiveUserMessage, EffectiveSystemMessage);
    return LastFormattedRequestPayload;
}

void UN2CBaseLLMService::SendRequest(
    const FString& JsonPayload,
    const FString& SystemMessage,
    const FOnLLMResponseReceived& OnComplete)
{
    if (!bIsInitialized)
    {
        FN2CLogger::Get().LogError(TEXT("Service not initialized"), TEXT("BaseLLMService"));
        const bool bExecuted = OnComplete.ExecuteIfBound(TEXT("{\"error\": \"Service not initialized\"}"));
        return;
    }
    
    // Log provider and model info
    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Sending request to %s using model: %s"), 
            *UEnum::GetValueAsString(GetProviderType()), *Config.Model),
        EN2CLogSeverity::Info,
        TEXT("BaseLLMService")
    );

    const FString FormattedPayload = BuildFormattedRequestPayload(JsonPayload, SystemMessage);

    // Get endpoint and auth token
    FString Endpoint, AuthToken;
    bool bSupportsSystemPrompts;
    GetConfiguration(Endpoint, AuthToken, bSupportsSystemPrompts);

    // Send request through HTTP handler
    HttpHandler->PostLLMRequest(
        Endpoint,
        AuthToken,
        FormattedPayload,
        OnComplete
    );
}
