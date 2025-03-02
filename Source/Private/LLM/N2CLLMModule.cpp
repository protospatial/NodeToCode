// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/N2CLLMModule.h"

#include "Core/N2CSettings.h"
#include "LLM/N2CSystemPromptManager.h"
#include "LLM/IN2CLLMService.h"
#include "LLM/Providers/N2CAnthropicService.h"
#include "LLM/Providers/N2CDeepSeekService.h"
#include "LLM/Providers/N2CGeminiService.h"
#include "LLM/Providers/N2COpenAIService.h"
#include "LLM/Providers/N2COllamaService.h"
#include "Utils/N2CLogger.h"

UN2CLLMModule* UN2CLLMModule::Get()
{
    static UN2CLLMModule* Instance = nullptr;
    if (!Instance)
    {
        Instance = NewObject<UN2CLLMModule>();
        Instance->AddToRoot(); // Prevent garbage collection
        Instance->CurrentStatus = EN2CSystemStatus::Idle;
    }
    return Instance;
}

bool UN2CLLMModule::Initialize()
{
    CurrentStatus = EN2CSystemStatus::Initializing;
    
    // Load settings
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    if (!Settings)
    {
        CurrentStatus = EN2CSystemStatus::Error;
        FN2CLogger::Get().LogError(TEXT("Failed to load plugin settings"), TEXT("LLMModule"));
        return false;
    }

    // Create config from settings
    Config.Provider = Settings->Provider;
    Config.ApiKey = Settings->GetActiveApiKey();
    Config.Model = Settings->GetActiveModel();

    // Initialize components
    if (!InitializeComponents() || !CreateServiceForProvider(Config.Provider))
    {
        CurrentStatus = EN2CSystemStatus::Error;
        return false;
    }

    bIsInitialized = true;
    CurrentStatus = EN2CSystemStatus::Idle;
    FN2CLogger::Get().Log(TEXT("LLM Module initialized successfully"), EN2CLogSeverity::Info, TEXT("LLMModule"));
    return true;
}

void UN2CLLMModule::ProcessN2CJson(
    const FString& JsonInput,
    const FOnLLMResponseReceived& OnComplete)
{
    if (!bIsInitialized)
    {
        CurrentStatus = EN2CSystemStatus::Error;
        FN2CLogger::Get().LogError(TEXT("LLM Module not initialized"), TEXT("LLMModule"));
        const bool bExecuted = OnComplete.ExecuteIfBound(TEXT("{\"error\": \"Module not initialized\"}"));
        return;
    }

    CurrentStatus = EN2CSystemStatus::Processing;
    
    // Broadcast that request is being sent
    OnTranslationRequestSent.Broadcast();

    if (!ActiveService.GetInterface())
    {
        FN2CLogger::Get().LogError(TEXT("No active LLM service"), TEXT("LLMModule"));
        const bool bExecuted = OnComplete.ExecuteIfBound(TEXT("{\"error\": \"No active service\"}"));
        return;
    }

    // Get active service
    TScriptInterface<IN2CLLMService> Service = GetActiveService();
    if (!Service.GetInterface())
    {
        FN2CLogger::Get().LogError(TEXT("No active service"), TEXT("LLMModule"));
        return;
    }

    // Check if service supports system prompts
    bool bSupportsSystemPrompts = false;
    FString Endpoint, AuthToken;
    Service->GetConfiguration(Endpoint, AuthToken, bSupportsSystemPrompts);

    // Get system prompt with language specification
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    FString SystemPrompt = PromptManager->GetLanguageSpecificPrompt(
        TEXT("CodeGen"),
        Settings ? Settings->TargetLanguage : EN2CCodeLanguage::Cpp
    );

    // Connect the HTTP handler's translation response delegate to our module's delegate
    if (HttpHandler)
    {
        HttpHandler->OnTranslationResponseReceived = OnTranslationResponseReceived;
    }

    // Send request through service
    ActiveService->SendRequest(JsonInput, SystemPrompt, FOnLLMResponseReceived::CreateLambda(
        [this](const FString& Response)
        {
            // Create translation response struct
            FN2CTranslationResponse TranslationResponse;
            
            // Get active service's response parser
            TScriptInterface<IN2CLLMService> ActiveServiceParser = GetActiveService();
            if (ActiveServiceParser.GetInterface())
            {
                UN2CResponseParserBase* Parser = ActiveServiceParser->GetResponseParser();
                if (Parser)
                {
                    if (Parser->ParseLLMResponse(Response, TranslationResponse))
                    {
                        CurrentStatus = EN2CSystemStatus::Idle;
                        OnTranslationResponseReceived.Broadcast(TranslationResponse, true);
                        FN2CLogger::Get().Log(TEXT("Successfully parsed LLM response"), EN2CLogSeverity::Info);
                    }
                    else
                    {
                        CurrentStatus = EN2CSystemStatus::Error;
                        FN2CLogger::Get().LogError(TEXT("Failed to parse LLM response"));
                        OnTranslationResponseReceived.Broadcast(TranslationResponse, false);
                    }
                }
                else
                {
                    CurrentStatus = EN2CSystemStatus::Error;
                    FN2CLogger::Get().LogError(TEXT("No response parser available"));
                    OnTranslationResponseReceived.Broadcast(TranslationResponse, false);
                }
            }
            else
            {
                CurrentStatus = EN2CSystemStatus::Error;
                FN2CLogger::Get().LogError(TEXT("No active LLM service"));
                OnTranslationResponseReceived.Broadcast(TranslationResponse, false);
            }
        }));
}

bool UN2CLLMModule::InitializeComponents()
{
    // Create and initialize prompt manager
    PromptManager = NewObject<UN2CSystemPromptManager>(this);
    if (!PromptManager)
    {
        FN2CLogger::Get().LogError(TEXT("Failed to create prompt manager"), TEXT("LLMModule"));
        return false;
    }
    PromptManager->Initialize(Config);

    // Note: HTTP Handler and Response Parser will be created by the specific service
    return true;
}

bool UN2CLLMModule::CreateServiceForProvider(EN2CLLMProvider Provider)
{
    UObject* ServiceObject = nullptr;

    switch (Provider)
    {
        case EN2CLLMProvider::Anthropic:
            ServiceObject = NewObject<UN2CAnthropicService>(this);
            break;

        case EN2CLLMProvider::OpenAI:
            ServiceObject = NewObject<UN2COpenAIService>(this);
            break;

        case EN2CLLMProvider::Ollama:
            ServiceObject = NewObject<UN2COllamaService>(this);
            break;
        
        case EN2CLLMProvider::DeepSeek:
            ServiceObject = NewObject<UN2CDeepSeekService>(this);
            break;

    case EN2CLLMProvider::Gemini:
            ServiceObject = NewObject<UN2CGeminiService>(this);
            break;

        // Other providers will be added here
        default:
            FN2CLogger::Get().LogError(
                FString::Printf(TEXT("Unsupported provider type: %d"), static_cast<int32>(Provider)),
                TEXT("LLMModule")
            );
            return false;
    }

    if (!ServiceObject)
    {
        FN2CLogger::Get().LogError(TEXT("Failed to create service object"), TEXT("LLMModule"));
        return false;
    }

    // Get service interface
    IN2CLLMService* Service = Cast<IN2CLLMService>(ServiceObject);
    if (!Service)
    {
        FN2CLogger::Get().LogError(TEXT("Service does not implement IN2CLLMService"), TEXT("LLMModule"));
        return false;
    }

    // Initialize service
    if (!Service->Initialize(Config))
    {
        FN2CLogger::Get().LogError(TEXT("Failed to initialize service"), TEXT("LLMModule"));
        return false;
    }

    // Store active service
    ActiveService.SetObject(ServiceObject);
    ActiveService.SetInterface(Cast<IN2CLLMService>(ServiceObject));
    return true;
}
