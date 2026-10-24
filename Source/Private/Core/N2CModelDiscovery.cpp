// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CModelDiscovery.h"

#include "Core/N2CSettings.h"
#include "HAL/PlatformTime.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Utils/N2CLogger.h"

namespace N2CModelDiscoveryPrivate
{
struct FCachedModelList
{
    double CachedAtSeconds = 0.0;
    TArray<FString> Models;
};

TMap<FString, FCachedModelList> ModelCache;
constexpr double ModelCacheLifetimeSeconds = 300.0;

FString TrimTrailingSlashes(FString Url)
{
    while (Url.EndsWith(TEXT("/")))
    {
        Url.LeftChopInline(1, EAllowShrinking::No);
    }
    return Url;
}

FString GetOpenAICompatibleRoot(const FString& ConfiguredEndpoint)
{
    FString Root = TrimTrailingSlashes(ConfiguredEndpoint.TrimStartAndEnd());
    const TCHAR* CompletionSuffix = TEXT("/chat/completions");
    if (Root.EndsWith(CompletionSuffix, ESearchCase::IgnoreCase))
    {
        Root.LeftChopInline(FCString::Strlen(CompletionSuffix), EAllowShrinking::No);
    }
    return TrimTrailingSlashes(Root);
}

FString GetLMStudioRoot(const FString& ConfiguredEndpoint)
{
    FString Root = TrimTrailingSlashes(ConfiguredEndpoint.TrimStartAndEnd());
    const TCHAR* KnownSuffixes[] =
    {
        TEXT("/v1/chat/completions"),
        TEXT("/chat/completions"),
        TEXT("/v1")
    };

    for (const TCHAR* Suffix : KnownSuffixes)
    {
        if (Root.EndsWith(Suffix, ESearchCase::IgnoreCase))
        {
            Root.LeftChopInline(FCString::Strlen(Suffix), EAllowShrinking::No);
            break;
        }
    }
    return TrimTrailingSlashes(Root);
}

FString GetMiniMaxModelsEndpoint(const FString& ConfiguredEndpoint)
{
    FString Root = TrimTrailingSlashes(ConfiguredEndpoint.TrimStartAndEnd());
    return Root.EndsWith(TEXT("/v1"), ESearchCase::IgnoreCase)
        ? Root + TEXT("/models")
        : Root + TEXT("/v1/models");
}

FString BuildCacheKey(const FN2CResolvedRequestProvider& Provider, const FString& Url)
{
    return FString::Printf(
        TEXT("%d|%s|%u"),
        static_cast<int32>(Provider.Provider),
        *Url,
        GetTypeHash(Provider.ApiKey));
}

bool IsLocalEndpoint(const FString& Url)
{
    return Url.Contains(TEXT("://127."), ESearchCase::IgnoreCase) ||
           Url.Contains(TEXT("://localhost"), ESearchCase::IgnoreCase) ||
           Url.Contains(TEXT("://[::1]"), ESearchCase::IgnoreCase);
}

void AddUniqueModel(TArray<FString>& Models, const FString& Candidate)
{
    FString Model = Candidate.TrimStartAndEnd();
    if (Model.StartsWith(TEXT("models/"), ESearchCase::IgnoreCase))
    {
        Model.RightChopInline(7, EAllowShrinking::No);
    }
    if (Model.IsEmpty())
    {
        return;
    }

    if (!Models.ContainsByPredicate([&Model](const FString& Existing)
        {
            return Existing.Equals(Model, ESearchCase::IgnoreCase);
        }))
    {
        Models.Add(MoveTemp(Model));
    }
}

bool IsLikelyOpenAITextGenerationModel(const FString& ModelId)
{
    const FString Lower = ModelId.ToLower();
    const TCHAR* ExcludedFragments[] =
    {
        TEXT("embedding"), TEXT("moderation"), TEXT("whisper"), TEXT("tts"),
        TEXT("dall-e"), TEXT("image"), TEXT("transcribe"), TEXT("realtime"),
        TEXT("audio-preview"), TEXT("search-preview")
    };

    for (const TCHAR* Fragment : ExcludedFragments)
    {
        if (Lower.Contains(Fragment))
        {
            return false;
        }
    }

    return Lower.StartsWith(TEXT("gpt-")) ||
           Lower.StartsWith(TEXT("o1")) ||
           Lower.StartsWith(TEXT("o3")) ||
           Lower.StartsWith(TEXT("o4")) ||
           Lower.StartsWith(TEXT("chatgpt-")) ||
           Lower.StartsWith(TEXT("codex-"));
}

bool ParseOpenAIStyleModels(
    const TSharedPtr<FJsonObject>& Root,
    TArray<FString>& OutModels,
    bool bFilterOpenAIModels)
{
    const TArray<TSharedPtr<FJsonValue>>* Data = nullptr;
    if (!Root.IsValid() || !Root->TryGetArrayField(TEXT("data"), Data) || !Data)
    {
        return false;
    }

    for (const TSharedPtr<FJsonValue>& Value : *Data)
    {
        const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
        FString ModelId;
        if (Object.IsValid() && Object->TryGetStringField(TEXT("id"), ModelId) &&
            (!bFilterOpenAIModels || IsLikelyOpenAITextGenerationModel(ModelId)))
        {
            AddUniqueModel(OutModels, ModelId);
        }
    }
    return true;
}

bool ParseGeminiModels(const TSharedPtr<FJsonObject>& Root, TArray<FString>& OutModels)
{
    const TArray<TSharedPtr<FJsonValue>>* Models = nullptr;
    if (!Root.IsValid() || !Root->TryGetArrayField(TEXT("models"), Models) || !Models)
    {
        return false;
    }

    for (const TSharedPtr<FJsonValue>& Value : *Models)
    {
        const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
        if (!Object.IsValid())
        {
            continue;
        }

        bool bSupportsGenerateContent = false;
        const TArray<TSharedPtr<FJsonValue>>* Methods = nullptr;
        if (Object->TryGetArrayField(TEXT("supportedGenerationMethods"), Methods) && Methods)
        {
            for (const TSharedPtr<FJsonValue>& Method : *Methods)
            {
                FString MethodName;
                if (Method.IsValid() && Method->TryGetString(MethodName) &&
                    MethodName.Equals(TEXT("generateContent"), ESearchCase::IgnoreCase))
                {
                    bSupportsGenerateContent = true;
                    break;
                }
            }
        }
        if (!bSupportsGenerateContent)
        {
            continue;
        }

        FString ModelId;
        if (!Object->TryGetStringField(TEXT("baseModelId"), ModelId) || ModelId.IsEmpty())
        {
            Object->TryGetStringField(TEXT("name"), ModelId);
        }
        AddUniqueModel(OutModels, ModelId);
    }
    return true;
}

bool ParseOllamaModels(const TSharedPtr<FJsonObject>& Root, TArray<FString>& OutModels)
{
    const TArray<TSharedPtr<FJsonValue>>* Models = nullptr;
    if (!Root.IsValid() || !Root->TryGetArrayField(TEXT("models"), Models) || !Models)
    {
        return false;
    }

    for (const TSharedPtr<FJsonValue>& Value : *Models)
    {
        const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
        FString ModelId;
        if (Object.IsValid())
        {
            if (!Object->TryGetStringField(TEXT("model"), ModelId) || ModelId.IsEmpty())
            {
                Object->TryGetStringField(TEXT("name"), ModelId);
            }
            AddUniqueModel(OutModels, ModelId);
        }
    }
    return true;
}

bool ParseLMStudioModels(const TSharedPtr<FJsonObject>& Root, TArray<FString>& OutModels)
{
    const TArray<TSharedPtr<FJsonValue>>* Models = nullptr;
    if (!Root.IsValid() || !Root->TryGetArrayField(TEXT("models"), Models) || !Models)
    {
        return false;
    }

    for (const TSharedPtr<FJsonValue>& Value : *Models)
    {
        const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
        if (!Object.IsValid())
        {
            continue;
        }

        FString Type;
        if (Object->TryGetStringField(TEXT("type"), Type) &&
            !Type.IsEmpty() &&
            !Type.Equals(TEXT("llm"), ESearchCase::IgnoreCase))
        {
            continue;
        }

        FString ModelId;
        if (!Object->TryGetStringField(TEXT("key"), ModelId) || ModelId.IsEmpty())
        {
            Object->TryGetStringField(TEXT("id"), ModelId);
        }
        AddUniqueModel(OutModels, ModelId);
    }
    return true;
}

bool ParseModelsResponse(
    EN2CLLMProvider Provider,
    const FString& Body,
    TArray<FString>& OutModels,
    FString& OutError)
{
    TSharedPtr<FJsonObject> Root;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        OutError = TEXT("Model-list response was not valid JSON");
        return false;
    }

    bool bRecognized = false;
    switch (Provider)
    {
        case EN2CLLMProvider::OpenAI:
            bRecognized = ParseOpenAIStyleModels(Root, OutModels, true);
            break;
        case EN2CLLMProvider::Anthropic:
        case EN2CLLMProvider::DeepSeek:
        case EN2CLLMProvider::MiniMax:
        case EN2CLLMProvider::Custom:
            bRecognized = ParseOpenAIStyleModels(Root, OutModels, false);
            break;
        case EN2CLLMProvider::Gemini:
            bRecognized = ParseGeminiModels(Root, OutModels);
            break;
        case EN2CLLMProvider::Ollama:
            bRecognized = ParseOllamaModels(Root, OutModels);
            break;
        case EN2CLLMProvider::LMStudio:
            bRecognized = ParseLMStudioModels(Root, OutModels);
            break;
        default:
            break;
    }

    if (!bRecognized)
    {
        OutError = TEXT("Model-list response did not match the expected provider schema");
        return false;
    }
    if (OutModels.IsEmpty())
    {
        OutError = TEXT("Provider returned no compatible generation models");
        return false;
    }
    return true;
}

bool BuildRequest(
    const FN2CResolvedRequestProvider& Provider,
    FString& OutUrl,
    TMap<FString, FString>& OutHeaders,
    FString& OutError)
{
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    if (!Settings)
    {
        OutError = TEXT("NodeToCode settings are unavailable");
        return false;
    }

    switch (Provider.Provider)
    {
        case EN2CLLMProvider::OpenAI:
            if (Provider.ApiKey.IsEmpty()) { OutError = TEXT("OpenAI API key is not configured"); return false; }
            OutUrl = TEXT("https://api.openai.com/v1/models");
            OutHeaders.Add(TEXT("Authorization"), TEXT("Bearer ") + Provider.ApiKey);
            break;
        case EN2CLLMProvider::Anthropic:
            if (Provider.ApiKey.IsEmpty()) { OutError = TEXT("Anthropic API key is not configured"); return false; }
            OutUrl = TEXT("https://api.anthropic.com/v1/models?limit=1000");
            OutHeaders.Add(TEXT("X-Api-Key"), Provider.ApiKey);
            OutHeaders.Add(TEXT("anthropic-version"), TEXT("2023-06-01"));
            break;
        case EN2CLLMProvider::Gemini:
            if (Provider.ApiKey.IsEmpty()) { OutError = TEXT("Gemini API key is not configured"); return false; }
            OutUrl = TEXT("https://generativelanguage.googleapis.com/v1beta/models?pageSize=1000");
            OutHeaders.Add(TEXT("x-goog-api-key"), Provider.ApiKey);
            break;
        case EN2CLLMProvider::DeepSeek:
            if (Provider.ApiKey.IsEmpty()) { OutError = TEXT("DeepSeek API key is not configured"); return false; }
            OutUrl = TEXT("https://api.deepseek.com/models");
            OutHeaders.Add(TEXT("Authorization"), TEXT("Bearer ") + Provider.ApiKey);
            break;
        case EN2CLLMProvider::Ollama:
        {
            FString Root = TrimTrailingSlashes(Settings->OllamaConfig.OllamaEndpoint.TrimStartAndEnd());
            if (Root.IsEmpty()) { Root = TEXT("http://localhost:11434"); }
            OutUrl = Root + TEXT("/api/tags");
            if (!Provider.ApiKey.IsEmpty()) { OutHeaders.Add(TEXT("Authorization"), TEXT("Bearer ") + Provider.ApiKey); }
            break;
        }
        case EN2CLLMProvider::LMStudio:
        {
            FString Root = GetLMStudioRoot(Settings->LMStudioEndpoint);
            if (Root.IsEmpty()) { Root = TEXT("http://localhost:1234"); }
            OutUrl = Root + TEXT("/api/v1/models");
            if (!Provider.ApiKey.IsEmpty()) { OutHeaders.Add(TEXT("Authorization"), TEXT("Bearer ") + Provider.ApiKey); }
            break;
        }
        case EN2CLLMProvider::MiniMax:
            if (Provider.ApiKey.IsEmpty()) { OutError = TEXT("MiniMax API key is not configured"); return false; }
            OutUrl = GetMiniMaxModelsEndpoint(Settings->MiniMaxEndpoint);
            OutHeaders.Add(TEXT("Authorization"), TEXT("Bearer ") + Provider.ApiKey);
            break;
        default:
            OutError = TEXT("Provider does not support built-in model discovery");
            return false;
    }

    OutHeaders.Add(TEXT("Accept"), TEXT("application/json"));
    return true;
}

bool StartModelListRequest(
    const FN2CResolvedRequestProvider& Provider,
    const FString& Url,
    const TMap<FString, FString>& Headers,
    FN2CModelDiscovery::FOnModelsResolved OnComplete)
{
    const FString CacheKey = BuildCacheKey(Provider, Url);
    const double NowSeconds = FPlatformTime::Seconds();
    if (const FCachedModelList* Cached = ModelCache.Find(CacheKey))
    {
        if (NowSeconds - Cached->CachedAtSeconds <= ModelCacheLifetimeSeconds)
        {
            OnComplete(true, Cached->Models, FString());
            return true;
        }
    }

    const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(Url);
    Request->SetVerb(TEXT("GET"));
    Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);

    const bool bLocalProvider =
        Provider.Provider == EN2CLLMProvider::Ollama ||
        Provider.Provider == EN2CLLMProvider::LMStudio ||
        IsLocalEndpoint(Url);
    Request->SetTimeout(bLocalProvider ? 1.0f : 3.0f);

    for (const TPair<FString, FString>& Header : Headers)
    {
        Request->SetHeader(Header.Key, Header.Value);
    }

    const EN2CLLMProvider ProviderType = Provider.Provider;
    Request->OnProcessRequestComplete().BindLambda(
        [ProviderType, CacheKey, OnComplete = MoveTemp(OnComplete)](
            FHttpRequestPtr,
            FHttpResponsePtr Response,
            bool bConnectedSuccessfully) mutable
        {
            if (!bConnectedSuccessfully || !Response.IsValid())
            {
                OnComplete(false, TArray<FString>(), TEXT("Model-list request failed to connect"));
                return;
            }

            const int32 ResponseCode = Response->GetResponseCode();
            if (ResponseCode < 200 || ResponseCode >= 300)
            {
                OnComplete(
                    false,
                    TArray<FString>(),
                    FString::Printf(TEXT("Model-list request returned HTTP %d"), ResponseCode));
                return;
            }

            TArray<FString> Models;
            FString ParseError;
            if (!ParseModelsResponse(
                    ProviderType,
                    Response->GetContentAsString(),
                    Models,
                    ParseError))
            {
                OnComplete(false, TArray<FString>(), MoveTemp(ParseError));
                return;
            }

            FCachedModelList& CacheEntry = ModelCache.FindOrAdd(CacheKey);
            CacheEntry.CachedAtSeconds = FPlatformTime::Seconds();
            CacheEntry.Models = Models;

            FN2CLogger::Get().Log(
                FString::Printf(
                    TEXT("Discovered %d model(s) for %s"),
                    Models.Num(),
                    *UEnum::GetValueAsString(ProviderType)),
                EN2CLogSeverity::Debug,
                TEXT("ModelDiscovery"));

            OnComplete(true, MoveTemp(Models), FString());
        });

    if (!Request->ProcessRequest())
    {
        Request->OnProcessRequestComplete().Unbind();
        return false;
    }

    return true;
}
}

bool FN2CModelDiscovery::SupportsProvider(EN2CLLMProvider Provider)
{
    return Provider != EN2CLLMProvider::Custom;
}

bool FN2CModelDiscovery::FetchAvailableModels(
    const FN2CResolvedRequestProvider& Provider,
    FOnModelsResolved OnComplete)
{
    if (!SupportsProvider(Provider.Provider))
    {
        return false;
    }

    FString Url;
    FString BuildError;
    TMap<FString, FString> Headers;
    if (!N2CModelDiscoveryPrivate::BuildRequest(Provider, Url, Headers, BuildError))
    {
        FN2CLogger::Get().Log(
            FString::Printf(
                TEXT("Skipping dynamic model discovery for %s: %s"),
                *UEnum::GetValueAsString(Provider.Provider),
                *BuildError),
            EN2CLogSeverity::Debug,
            TEXT("ModelDiscovery"));
        return false;
    }

    return N2CModelDiscoveryPrivate::StartModelListRequest(
        Provider,
        Url,
        Headers,
        MoveTemp(OnComplete));
}

bool FN2CModelDiscovery::FetchOpenAICompatibleModels(
    const FString& ApiBaseEndpoint,
    const FString& ApiKey,
    FOnModelsResolved OnComplete)
{
    const FString Root = N2CModelDiscoveryPrivate::GetOpenAICompatibleRoot(ApiBaseEndpoint);
    if (Root.IsEmpty())
    {
        FN2CLogger::Get().Log(
            TEXT("Skipping custom model discovery because the API base endpoint is empty"),
            EN2CLogSeverity::Debug,
            TEXT("ModelDiscovery"));
        return false;
    }

    FN2CResolvedRequestProvider Provider;
    Provider.Provider = EN2CLLMProvider::Custom;
    Provider.ApiKey = ApiKey;

    TMap<FString, FString> Headers;
    Headers.Add(TEXT("Accept"), TEXT("application/json"));
    if (!ApiKey.IsEmpty())
    {
        Headers.Add(TEXT("Authorization"), TEXT("Bearer ") + ApiKey);
    }

    return N2CModelDiscoveryPrivate::StartModelListRequest(
        Provider,
        Root + TEXT("/models"),
        Headers,
        MoveTemp(OnComplete));
}
