// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/Providers/N2CCustomOpenAIService.h"

#include "Core/N2CCustomProviderSettings.h"
#include "LLM/N2CLLMPayloadBuilder.h"
#include "LLM/N2CSystemPromptManager.h"
#include "LLM/Providers/N2COpenAIResponseParser.h"
#include "Utils/N2CLogger.h"

bool UN2CCustomOpenAIService::Initialize(const FN2CLLMConfig& InConfig)
{
    const UN2CCustomProviderSettings* Settings = GetDefault<UN2CCustomProviderSettings>();
    const FN2CCustomProviderDefinition* Provider = Settings ? Settings->GetActiveProvider() : nullptr;
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

    FN2CLLMConfig UpdatedConfig = InConfig;
    UpdatedConfig.ApiEndpoint = Provider->Endpoint.TrimStartAndEnd();
    UpdatedConfig.ApiKey = Settings->GetApiKey(Provider->Name);
    UpdatedConfig.Model = Provider->Model;
    UpdatedConfig.bUseSystemPrompts = Provider->bUseSystemPrompts;

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
    PayloadBuilder->SetMaxTokens(8192);
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
