// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CConnectionTester.h"

#include "Core/N2CCustomProviderSettings.h"
#include "Core/N2CSettings.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Widgets/Notifications/SNotificationList.h"

namespace
{
void AddBearerHeaderIfPresent(TMap<FString, FString>& Headers, const FString& ApiKey)
{
    if (!ApiKey.IsEmpty())
    {
        Headers.Add(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKey));
    }
}
}

void FN2CConnectionTester::TestProvider(EN2CLLMProvider Provider, const UN2CSettings& Settings)
{
    FString ProviderName;
    FString Url;
    TMap<FString, FString> Headers;

    switch (Provider)
    {
        case EN2CLLMProvider::OpenAI:
            ProviderName = TEXT("OpenAI");
            Url = TEXT("https://api.openai.com/v1/models");
            AddBearerHeaderIfPresent(Headers, Settings.OpenAI_API_Key_UI);
            break;

        case EN2CLLMProvider::Anthropic:
            ProviderName = TEXT("Anthropic");
            Url = TEXT("https://api.anthropic.com/v1/models");
            if (!Settings.Anthropic_API_Key_UI.IsEmpty())
            {
                Headers.Add(TEXT("x-api-key"), Settings.Anthropic_API_Key_UI);
            }
            Headers.Add(TEXT("anthropic-version"), TEXT("2023-06-01"));
            break;

        case EN2CLLMProvider::Gemini:
            ProviderName = TEXT("Gemini");
            Url = FString::Printf(
                TEXT("https://generativelanguage.googleapis.com/v1beta/models?key=%s"),
                *Settings.Gemini_API_Key_UI);
            break;

        case EN2CLLMProvider::DeepSeek:
            ProviderName = TEXT("DeepSeek");
            Url = TEXT("https://api.deepseek.com/models");
            AddBearerHeaderIfPresent(Headers, Settings.DeepSeek_API_Key_UI);
            break;

        case EN2CLLMProvider::Ollama:
        {
            ProviderName = TEXT("Ollama");
            const FString BaseUrl = NormalizeBaseUrl(Settings.OllamaConfig.OllamaEndpoint, TEXT("/api/chat"));
            Url = BaseUrl + TEXT("/api/tags");
            AddBearerHeaderIfPresent(Headers, Settings.OllamaConfig.ApiKey);
            break;
        }

        case EN2CLLMProvider::LMStudio:
        {
            ProviderName = TEXT("LM Studio");
            const FString BaseUrl = NormalizeBaseUrl(Settings.LMStudioEndpoint, TEXT("/v1/chat/completions"));
            Url = BaseUrl.EndsWith(TEXT("/v1"))
                ? BaseUrl + TEXT("/models")
                : BaseUrl + TEXT("/v1/models");
            break;
        }

        case EN2CLLMProvider::MiniMax:
        {
            ProviderName = TEXT("MiniMax");
            const FString BaseUrl = NormalizeBaseUrl(Settings.MiniMaxEndpoint, TEXT("/v1/chat/completions"));
            Url = BaseUrl.EndsWith(TEXT("/v1"))
                ? BaseUrl + TEXT("/models")
                : BaseUrl + TEXT("/v1/models");
            AddBearerHeaderIfPresent(Headers, Settings.MiniMax_API_Key_UI);
            break;
        }

        case EN2CLLMProvider::Custom:
            ShowResult(TEXT("Select a custom provider connection check from its custom provider section."), false);
            return;

        default:
            ShowResult(TEXT("Unsupported provider for connection check."), false);
            return;
    }

    SendGetRequest(ProviderName, Url, Headers);
}

void FN2CConnectionTester::TestCustomProvider(
    const FString& ProviderName,
    const UN2CCustomProviderSettings& Settings)
{
    const FN2CCustomProviderDefinition* Provider = Settings.GetProvider(ProviderName);
    if (!Provider)
    {
        ShowResult(FString::Printf(TEXT("Custom provider '%s' was not found."), *ProviderName), false);
        return;
    }

    if (Provider->Endpoint.TrimStartAndEnd().IsEmpty())
    {
        ShowResult(
            FString::Printf(TEXT("Custom provider '%s' requires a provider endpoint."), *ProviderName),
            false);
        return;
    }

    if (Provider->ApiType != EN2CCustomProviderApiType::OpenAI)
    {
        ShowResult(TEXT("Unsupported custom provider API type."), false);
        return;
    }

    const FString BaseUrl = NormalizeBaseUrl(Provider->Endpoint, TEXT("/chat/completions"));
    TMap<FString, FString> Headers;
    AddBearerHeaderIfPresent(Headers, Settings.GetApiKey(ProviderName));
    SendGetRequest(ProviderName, BaseUrl + TEXT("/models"), Headers);
}

FString FN2CConnectionTester::NormalizeBaseUrl(const FString& Endpoint, const FString& KnownSuffix)
{
    FString BaseUrl = Endpoint.TrimStartAndEnd();
    while (BaseUrl.EndsWith(TEXT("/")))
    {
        BaseUrl.LeftChopInline(1);
    }

    if (!KnownSuffix.IsEmpty() && BaseUrl.EndsWith(KnownSuffix, ESearchCase::IgnoreCase))
    {
        BaseUrl.LeftChopInline(KnownSuffix.Len());
        while (BaseUrl.EndsWith(TEXT("/")))
        {
            BaseUrl.LeftChopInline(1);
        }
    }

    return BaseUrl;
}

void FN2CConnectionTester::SendGetRequest(
    const FString& ProviderName,
    const FString& Url,
    const TMap<FString, FString>& Headers)
{
    if (Url.IsEmpty())
    {
        ShowResult(FString::Printf(TEXT("%s endpoint is empty."), *ProviderName), false);
        return;
    }

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(Url);
    Request->SetVerb(TEXT("GET"));
    Request->SetTimeout(15.0f);

    for (const TPair<FString, FString>& Header : Headers)
    {
        Request->SetHeader(Header.Key, Header.Value);
    }

    Request->OnProcessRequestComplete().BindLambda(
        [ProviderName](FHttpRequestPtr, FHttpResponsePtr Response, bool bWasSuccessful)
        {
            if (!bWasSuccessful || !Response.IsValid())
            {
                ShowResult(
                    FString::Printf(TEXT("%s connection failed: no HTTP response received."), *ProviderName),
                    false);
                return;
            }

            const int32 ResponseCode = Response->GetResponseCode();
            if (ResponseCode >= 200 && ResponseCode < 300)
            {
                ShowResult(
                    FString::Printf(TEXT("%s connection successful (HTTP %d)."), *ProviderName, ResponseCode),
                    true);
                return;
            }

            if (ResponseCode == 401 || ResponseCode == 403)
            {
                ShowResult(
                    FString::Printf(TEXT("%s is reachable, but authentication failed (HTTP %d)."), *ProviderName, ResponseCode),
                    false);
                return;
            }

            ShowResult(
                FString::Printf(TEXT("%s is reachable, but returned HTTP %d."), *ProviderName, ResponseCode),
                false);
        });

    if (!Request->ProcessRequest())
    {
        ShowResult(FString::Printf(TEXT("Failed to start %s connection check."), *ProviderName), false);
    }
}

void FN2CConnectionTester::ShowResult(const FString& Message, bool bSuccess)
{
    FNotificationInfo Info(FText::FromString(TEXT("Node to Code Connection Check")));
    Info.Text = FText::FromString(Message);
    Info.bFireAndForget = true;
    Info.ExpireDuration = 5.0f;
    Info.bUseSuccessFailIcons = true;

    TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
    if (Notification.IsValid())
    {
        Notification->SetCompletionState(
            bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
    }
}
