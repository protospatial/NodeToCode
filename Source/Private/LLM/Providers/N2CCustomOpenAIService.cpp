// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/Providers/N2CCustomOpenAIService.h"

#include "Core/N2CCustomProviderSettings.h"
#include "Core/N2CRequestSettings.h"
#include "LLM/N2CLLMPayloadBuilder.h"
#include "LLM/N2CSystemPromptManager.h"
#include "LLM/Providers/N2COpenAIResponseParser.h"
#include "Utils/N2CLogger.h"

bool UN2CCustomOpenAIService::Initialize(const FN2CLLMConfig& InConfig)
{
    const UN2CCustomProviderSettings* Settings = GetDefault<UN2CCustomProviderSettings>();
    const FString RequestProviderName = FN2CRequestRuntime::ConsumeSelectedCustomProviderName();
    const FN2CCustomProviderDefinition* Provider = Settings
        ? (RequestProviderName.IsEmpty()
            ? Settings->GetActiveProvider()
            : Settings->GetProvider(RequestProviderName))
        : nullptr;

    if (!Provider)
    {
        FN2CLogger::Get().LogError(TEXT("No active custom provider is configured"), TEXT("CustomProvider"));
        return false;
    }

    if (Provider->Endpoint.TrimStartAndEnd().IsEmpty())
    {
        FN2CLogger::Get().LogError(
            FString::Printf(TEXT("Custom provider '%s' requires a provider endpoint"), *Provider->Name),
            TEXT("CustomProvider"));
        return false;
    }

    if (Provider->ApiType != EN2CCustomProviderApiType::OpenAI)
    {
        FN2CLogger::Get().LogError(TEXT("Unsupported custom provider API type"), TEXT("CustomProvider"));
        return false;
    }

    FString BaseUrl = Provider->Endpoint.TrimStartAndEnd();
    while (BaseUrl.EndsWith(TEXT("/")))
    {
        BaseUrl.LeftChopInline(1);
    }

    MaxOutputTokens = FMath::Clamp(Provider->MaxOutputTokens, 1024, 131072);

    FN2CLLMConfig UpdatedConfig = InConfig;
    UpdatedConfig.ApiEndpoint = BaseUrl.EndsWith(TEXT("/chat/completions"), ESearchCase::IgnoreCase)
        ? BaseUrl
        : BaseUrl + TEXT("/chat/completions");
    UpdatedConfig.ApiKey = Settings->GetApiKey(Provider->Name);
    UpdatedConfig.Model = Provider->Model;
    UpdatedConfig.bUseSystemPrompts = Provider->bUseSystemPrompts;

    FN2CLogger::Get().Log(
        FString::Printf(
            TEXT("Custom provider '%s' output token budget: %d"),
            *Provider->Name,
            MaxOutputTokens),
        EN2CLogSeverity::Debug,
        TEXT("CustomProvider"));

    return Super::Initialize(UpdatedConfig);
}

void UN2CCustomOpenAIService::GetConfiguration(
    FString& OutEndpoint,
    FString& OutAuthToken,
    bool& OutSupportsSystemPrompts)
{
    OutEndpoint = Config.ApiEndpoint;
    OutAuthToken = Config.ApiKey;
    OutSupportsSystemPrompts = Config.bUseSystemPrompts;
}

void UN2CCustomOpenAIService::GetProviderHeaders(TMap<FString, FString>& OutHeaders) const
{
    OutHeaders.Add(TEXT("Content-Type"), TEXT("application/json"));
}

FString UN2CCustomOpenAIService::FormatRequestPayload(const FString& UserMessage, const FString& SystemMessage) const
{
    UN2CLLMPayloadBuilder* PayloadBuilder = NewObject<UN2CLLMPayloadBuilder>();
    PayloadBuilder->Initialize(Config.Model);
    PayloadBuilder->ConfigureForOpenAI();
    PayloadBuilder->SetTemperature(0.0f);
    PayloadBuilder->SetMaxTokens(MaxOutputTokens);
    PayloadBuilder->SetJsonResponseFormat(UN2CLLMPayloadBuilder::GetN2CResponseSchema());

    FString FinalContent = UserMessage;
    PromptManager->PrependSourceFilesToUserMessage(FinalContent);

    if (Config.bUseSystemPrompts && !SystemMessage.IsEmpty())
    {
        PayloadBuilder->AddSystemMessage(SystemMessage);
        PayloadBuilder->AddUserMessage(FinalContent);
    }
    else
    {
        const FString MergedContent = PromptManager->MergePrompts(SystemMessage, FinalContent);
        PayloadBuilder->AddUserMessage(MergedContent);
    }

    return PayloadBuilder->Build();
}

UN2CResponseParserBase* UN2CCustomOpenAIService::CreateResponseParser()
{
    return NewObject<UN2COpenAIResponseParser>(this);
}
