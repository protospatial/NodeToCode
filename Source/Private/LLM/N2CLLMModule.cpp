// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/N2CLLMModule.h"

#include "Models/N2CBlueprint.h"
#include "Core/N2CSerializer.h"
#include "Core/N2CSettings.h"
#include "Core/N2CTargetLanguage.h"
#include "LLM/N2CSystemPromptManager.h"
#include "LLM/N2CBaseLLMService.h"
#include "LLM/N2CLLMProviderRegistry.h"
#include "LLM/Providers/N2CAnthropicService.h"
#include "LLM/Providers/N2CDeepSeekService.h"
#include "LLM/Providers/N2CGeminiService.h"
#include "LLM/Providers/N2CLMStudioService.h"
#include "LLM/Providers/N2COpenAIService.h"
#include "LLM/Providers/N2COllamaService.h"
#include "Utils/N2CLogger.h"
#include "Utils/N2CPathUtils.h"
#include "LLM/N2CTranslationArchive.h"
#include "HAL/PlatformProcess.h"
#include "UObject/StrongObjectPtr.h"

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
    // A request still in flight keeps the module Processing: Initialize does not take its status away
    SetStatusIfIdle(EN2CSystemStatus::Initializing);

    // Load settings
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    if (!Settings)
    {
        SetStatusIfIdle(EN2CSystemStatus::Error);
        FN2CLogger::Get().LogError(TEXT("Failed to load plugin settings"), TEXT("LLMModule"));
        return false;
    }

    // Create config from settings
    Config = MakeConfigFromSettings();
    ServiceEndpointSetting = GetLocalEndpointSetting(Config.Provider);

    // Initialize provider registry
    InitializeProviderRegistry();

    // Initialize components
    if (!InitializeComponents() || !CreateServiceForProvider(Config.Provider))
    {
        SetStatusIfIdle(EN2CSystemStatus::Error);
        return false;
    }

    bIsInitialized = true;
    SetStatusIfIdle(EN2CSystemStatus::Idle);
FN2CLogger::Get().Log(TEXT("LLM Module initialized successfully"), EN2CLogSeverity::Info, TEXT("LLMModule"));
    return true;
}

FN2CLLMConfig UN2CLLMModule::MakeConfigFromSettings()
{
    FN2CLLMConfig Snapshot;
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    if (!Settings)
    {
        return Snapshot;
    }
    Snapshot.Provider = Settings->Provider;
Settings->ResolveRequestModel(Snapshot.Provider, FString(), Snapshot);
    return Snapshot;
}

FString UN2CLLMModule::GetLocalEndpointSetting(EN2CLLMProvider Provider)
{
    // The local providers read their endpoint from the settings when their service is created
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    if (!Settings)
    {
        return FString();
    }
    switch (Provider)
    {
    case EN2CLLMProvider::Ollama:
        return Settings->OllamaConfig.OllamaEndpoint;
    case EN2CLLMProvider::LMStudio:
        return Settings->LMStudioEndpoint;
    default:
        return FString();
    }
}

bool UN2CLLMModule::RefreshFromSettings()
{
    check(IsInGameThread());

    if (!bIsInitialized)
    {
        return Initialize();
    }

    const FN2CLLMConfig Snapshot = MakeConfigFromSettings();
    const FString EndpointSetting = GetLocalEndpointSetting(Snapshot.Provider);
    const bool bUnchanged = ActiveService.GetInterface() != nullptr
        && Snapshot.Provider == Config.Provider
&& Snapshot.Model.Equals(Config.Model, ESearchCase::CaseSensitive)
        && Snapshot.ReasoningEffort.Equals(Config.ReasoningEffort, ESearchCase::CaseSensitive)
        && Snapshot.MaxOutputTokens == Config.MaxOutputTokens
        && FMath::IsNearlyEqual(Snapshot.ActivityTimeoutSeconds, Config.ActivityTimeoutSeconds)
        && EndpointSetting.Equals(ServiceEndpointSetting, ESearchCase::CaseSensitive);
    if (bUnchanged)
    {
        return true;
    }

    FN2CLogger::Get().Log(FString::Printf(TEXT("LLM settings changed: the next request uses %s, model %s"),
        *UEnum::GetValueAsString(Snapshot.Provider), *Snapshot.Model), EN2CLogSeverity::Info, TEXT("LLMModule"));

    Config = Snapshot;
    ServiceEndpointSetting = EndpointSetting;
    if (PromptManager)
    {
        PromptManager->Initialize(Config);
    }
    if (!CreateServiceForProvider(Config.Provider))
    {
        // Never send on the old provider's service with the new settings
        ActiveService = nullptr;
        return false;
    }
    return true;
}

void UN2CLLMModule::BeginRequest()
{
    ++InFlightRequests;
    CurrentStatus = EN2CSystemStatus::Processing;
}

void UN2CLLMModule::EndRequest(EN2CSystemStatus Result)
{
    InFlightRequests = FMath::Max(0, InFlightRequests - 1);
    CurrentStatus = InFlightRequests > 0 ? EN2CSystemStatus::Processing : Result;
}

void UN2CLLMModule::SetStatusIfIdle(EN2CSystemStatus Status)
{
    if (InFlightRequests == 0)
    {
        CurrentStatus = Status;
    }
}

FN2CLLMRequestHandleRef UN2CLLMModule::CompleteWithoutRequest(const FOnLLMResponseReceived& OnComplete, const FString& Response)
{
    FN2CLLMRequestHandleRef Handle = FN2CLLMRequestHandle::Create();
    Handle->Complete(OnComplete, Response);
    return Handle;
}

void UN2CLLMModule::BroadcastFailure(const FN2CTranslationError& Error)
{
    FN2CTranslationResponse Failed;
    Failed.Error = Error;
    OnTranslationResponseReceived.Broadcast(Failed, false);
}

void UN2CLLMModule::FillTranslationError(FN2CTranslationResponse& Response, EN2CLLMProvider Provider, const FString& Model,
    const FString& RawResponse)
{
    FN2CTranslationError& Error = Response.Error;
    if (!Error.IsError())
    {
        FString Snippet = RawResponse.Left(300).Replace(TEXT("\r"), TEXT(" ")).Replace(TEXT("\n"), TEXT(" "));
        if (RawResponse.Len() > 300)
        {
            Snippet += TEXT("...");
        }
        Error.Kind = EN2CTranslationErrorKind::Parse;
        Error.Message = Snippet.IsEmpty()
            ? FString(TEXT("The provider returned an empty response"))
            : FString::Printf(TEXT("The response could not be read as a translation. It starts with: %s"), *Snippet);
        Error.bRetryable = false;
    }
    if (Error.Provider.IsEmpty())
    {
        Error.Provider = StaticEnum<EN2CLLMProvider>()->GetNameStringByValue(static_cast<int64>(Provider));
    }
    if (Error.Model.IsEmpty())
    {
        Error.Model = Model;
    }
}

void UN2CLLMModule::ResetStatusOnCancel(const FN2CLLMRequestHandleRef& Handle)
{
    TWeakObjectPtr<UN2CLLMModule> WeakThis(this);
    Handle->OnCancelled([WeakThis]()
    {
        // The cancelled request gives up its status; another request still in flight keeps the module Processing
        if (UN2CLLMModule* StrongThis = WeakThis.Get())
        {
            StrongThis->EndRequest(EN2CSystemStatus::Idle);
        }
        FN2CLogger::Get().Log(TEXT("LLM request cancelled"), EN2CLogSeverity::Info, TEXT("LLMModule"));
    });
}

FN2CLLMRequestHandleRef UN2CLLMModule::ProcessN2CJson(
    const FString& JsonInput,
    const FOnLLMResponseReceived& OnComplete)
{
    return ProcessN2CJson(JsonInput, OnComplete, FOnN2CTranslationParsed());
}

FN2CLLMRequestHandleRef UN2CLLMModule::ProcessN2CJson(
    const FString& JsonInput,
    const FOnLLMResponseReceived& OnComplete,
    const FOnN2CTranslationParsed& OnParsed)
{
    // Creates and uses UObjects and writes CurrentStatus: game thread only (objects made elsewhere are never collected)
    check(IsInGameThread());

    // Errors found before anything is sent: this request's own callbacks, then the shared broadcast
    auto FailBeforeSend = [this, &OnComplete, &OnParsed](const FString& Message)
    {
        SetStatusIfIdle(EN2CSystemStatus::Error);
        FN2CLogger::Get().LogError(Message, TEXT("LLMModule"));
        FN2CTranslationResponse Failed;
        Failed.Error = FN2CTranslationError::Make(Message);
        OnParsed.ExecuteIfBound(Failed, false);
        BroadcastFailure(Failed.Error);
        return CompleteWithoutRequest(OnComplete, Failed.Error.ToJsonString());
    };

    // The provider, model, key and timeouts the settings give now, so a change applies to this request (and to the
    // next item of a running batch)
    if (!RefreshFromSettings())
    {
        return FailBeforeSend(bIsInitialized
            ? FString(TEXT("Could not create the LLM service for the selected provider"))
            : FString(TEXT("LLM module not initialized")));
    }

    // Get active service
    TScriptInterface<IN2CLLMService> Service = GetActiveService();
    if (!Service.GetInterface())
    {
        return FailBeforeSend(TEXT("No active LLM service"));
    }

    // This request owns the Processing status until it completes or is cancelled
    BeginRequest();

    // Broadcast that request is being sent
    OnTranslationRequestSent.Broadcast();

    // Check if service supports system prompts
    bool bSupportsSystemPrompts = false;
    FString Endpoint;
    Service->GetConfiguration(Endpoint, bSupportsSystemPrompts);

    // Get system prompt with language specification
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    FString SystemPrompt = PromptManager->GetLanguageSpecificPrompt(
        N2C::BlueprintPromptFamily,
        Settings ? Settings->TargetLanguage : EN2CCodeLanguage::Cpp
    );

// Send request through service, with the JSON in <n2c_json> tags (data, not instructions; see N2CJsonSpec.md).
    // The callback keeps the service it was sent on alive and parses with that
    // service's parser, even if the active provider changes or GC runs while the request is in flight.
    // Nothing is saved here: the caller writes the translation (FN2CTranslationArchive) if it keeps it (card 0252)
    TStrongObjectPtr<UObject> RequestServiceObject(Service.GetObject());
    FN2CLLMRequestHandleRef Handle = ActiveService->SendRequest(UN2CSystemPromptManager::WrapN2CJson(JsonInput), SystemPrompt, FOnLLMResponseReceived::CreateLambda(
        [this, OnComplete, OnParsed, RequestServiceObject, RequestService = Service, RequestModel = Config.Model](const FString& Response)
        {
            // Create translation response struct
            FN2CTranslationResponse TranslationResponse;

            // Parser of the service this request was sent on
            const TScriptInterface<IN2CLLMService>& ActiveServiceParser = RequestService;
            if (RequestServiceObject.IsValid() && ActiveServiceParser.GetInterface())
            {
                UN2CResponseParserBase* Parser = ActiveServiceParser->GetResponseParser();
                if (Parser)
                {
                    if (Parser->ParseLLMResponse(Response, TranslationResponse))
                    {
                        EndRequest(EN2CSystemStatus::Idle);

                        OnParsed.ExecuteIfBound(TranslationResponse, true);
                        OnTranslationResponseReceived.Broadcast(TranslationResponse, true);
                        FN2CLogger::Get().Log(TEXT("Successfully parsed LLM response"), EN2CLogSeverity::Info);
                    }
                    else
                    {
                        // The typed error (HTTP status, provider message, kind) reaches the broadcast's listeners
                        EndRequest(EN2CSystemStatus::Error);
                        FillTranslationError(TranslationResponse, ActiveServiceParser->GetProviderType(), RequestModel, Response);
                        FN2CLogger::Get().LogError(FString::Printf(TEXT("Translation failed: %s"),
                            *TranslationResponse.Error.ToDisplayString()), TEXT("LLMModule"));
                        OnParsed.ExecuteIfBound(TranslationResponse, false);
                        OnTranslationResponseReceived.Broadcast(TranslationResponse, false);
                    }
                }
                else
                {
                    EndRequest(EN2CSystemStatus::Error);
                    FN2CLogger::Get().LogError(TEXT("No response parser available"));
                    TranslationResponse.Error = FN2CTranslationError::Make(TEXT("No response parser available"));
                    OnParsed.ExecuteIfBound(TranslationResponse, false);
                    OnTranslationResponseReceived.Broadcast(TranslationResponse, false);
                }
            }
            else
            {
                EndRequest(EN2CSystemStatus::Error);
                FN2CLogger::Get().LogError(TEXT("No active LLM service"));
                TranslationResponse.Error = FN2CTranslationError::Make(TEXT("No active LLM service"));
                OnParsed.ExecuteIfBound(TranslationResponse, false);
                OnTranslationResponseReceived.Broadcast(TranslationResponse, false);
            }

            // Invoke the completion callback with the raw response
            OnComplete.ExecuteIfBound(Response);
        }));
    ResetStatusOnCancel(Handle);
    return Handle;
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

FString UN2CLLMModule::GetLatestTranslationPath() const
{
    return FN2CTranslationArchive::GetLatestRunFolder();
}

void UN2CLLMModule::OpenTranslationFolder(bool& Success)
{
    FString PathToOpen = GetLatestTranslationPath();
    
    if (PathToOpen.IsEmpty())
    {
        FN2CLogger::Get().LogWarning(TEXT("No translation path available, opening the base path"));
        Success = true;
        PathToOpen = FN2CPathUtils::GetTranslationsBasePath();
    }

    if (!FPaths::DirectoryExists(PathToOpen))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Translation directory does not exist: %s \n\nOpening the base path"), *PathToOpen));
        Success = true;
        PathToOpen = FN2CPathUtils::GetTranslationsBasePath();
    }

    FPlatformProcess::ExploreFolder(*PathToOpen);
    Success = true;
    
}

FString UN2CLLMModule::GetFileExtensionForLanguage(EN2CCodeLanguage Language) const
{
    return N2C::GetLanguage(Language).ImplementationExtension;
}

bool UN2CLLMModule::CreateServiceForProvider(EN2CLLMProvider Provider)
{
    // Get the provider registry
    UN2CLLMProviderRegistry* Registry = UN2CLLMProviderRegistry::Get();
    
    // Check if the provider is registered
    if (!Registry->IsProviderRegistered(Provider))
    {
        FN2CLogger::Get().LogError(
            FString::Printf(TEXT("Provider type not registered: %s"), 
                *UEnum::GetValueAsString(Provider)),
            TEXT("LLMModule")
        );
        return false;
    }
    
    // Create the provider service
    TScriptInterface<IN2CLLMService> ServiceInterface = Registry->CreateProvider(Provider, this);
    
    if (!ServiceInterface.GetInterface())
    {
        FN2CLogger::Get().LogError(
            FString::Printf(TEXT("Failed to create service for provider type: %s"), 
                *UEnum::GetValueAsString(Provider)),
            TEXT("LLMModule")
        );
        return false;
    }

    // Initialize service
    if (!ServiceInterface.GetInterface()->Initialize(Config))
    {
        FN2CLogger::Get().LogError(TEXT("Failed to initialize service"), TEXT("LLMModule"));
        return false;
    }

    // Store active service
    ActiveService = ServiceInterface;
    return true;
}
void UN2CLLMModule::InitializeProviderRegistry()
{
    // Get the provider registry
    UN2CLLMProviderRegistry* Registry = UN2CLLMProviderRegistry::Get();
    
    // Register all provider classes
    Registry->RegisterProvider(EN2CLLMProvider::OpenAI, UN2COpenAIService::StaticClass());
    Registry->RegisterProvider(EN2CLLMProvider::Anthropic, UN2CAnthropicService::StaticClass());
    Registry->RegisterProvider(EN2CLLMProvider::Gemini, UN2CGeminiService::StaticClass());
    Registry->RegisterProvider(EN2CLLMProvider::DeepSeek, UN2CDeepSeekService::StaticClass());
    Registry->RegisterProvider(EN2CLLMProvider::Ollama, UN2COllamaService::StaticClass());
    Registry->RegisterProvider(EN2CLLMProvider::LMStudio, UN2CLMStudioService::StaticClass());
    
    FN2CLogger::Get().Log(TEXT("Provider registry initialized"), EN2CLogSeverity::Info, TEXT("LLMModule"));
}

FN2CLLMRequestHandleRef UN2CLLMModule::ProcessN2CJsonWithOverrides(
    const FString& JsonInput,
    const FN2CLLMConfig& RequestConfig,
    EN2CCodeLanguage RequestLanguage,
    const FOnLLMResponseReceived& OnComplete,
    const FOnN2CTranslationParsed& OnParsed,
    bool bArchiveRun)
{
    // Creates a provider service and a prompt manager and writes CurrentStatus: game thread only. Objects created on
    // another thread get EInternalObjectFlags::Async and are never collected.
    check(IsInGameThread());

    // This request owns the Processing status until it completes or is cancelled
    BeginRequest();
    // Note: We don't broadcast OnTranslationRequestSent here as this is for a specific tool call,
    // not necessarily a UI-driven translation. The async task can report its own start.

    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Processing N2C JSON with overrides. Provider: %s, Model: %s, Language: %s"),
            *UEnum::GetValueAsString(RequestConfig.Provider), *RequestConfig.Model, *UEnum::GetValueAsString(RequestLanguage)),
        EN2CLogSeverity::Info, TEXT("LLMModule")
    );

    TScriptInterface<IN2CLLMService> TempService = UN2CLLMProviderRegistry::Get()->CreateProvider(RequestConfig.Provider, this);
    if (!TempService.GetInterface() || !TempService->Initialize(RequestConfig))
    {
        FString ErrorMsg = TEXT("Failed to create/initialize temporary service for override request.");
        FN2CLogger::Get().LogError(ErrorMsg, TEXT("LLMModule"));
        EndRequest(EN2CSystemStatus::Error);
        FN2CTranslationResponse Failed;
        Failed.Error = FN2CTranslationError::Make(ErrorMsg);
        FillTranslationError(Failed, RequestConfig.Provider, RequestConfig.Model, FString());
        OnParsed.ExecuteIfBound(Failed, false);
        return CompleteWithoutRequest(OnComplete, FN2CTranslationError::MakeErrorJson(ErrorMsg));
    }
    
    // The temporary service is referenced only by this request: hold it strongly so GC cannot free it mid-request
    TStrongObjectPtr<UObject> TempServiceObject(TempService.GetObject());

    // Create a temporary prompt manager for this request to ensure correct language-specific prompt. Nothing else
    // references it, so hold it strongly while it builds the prompt; it is collected after this call.
    TStrongObjectPtr<UN2CSystemPromptManager> TempPromptManager(NewObject<UN2CSystemPromptManager>(this));
    TempPromptManager->Initialize(RequestConfig); // Initialize with the request-specific config
    FString SystemPrompt = TempPromptManager->GetLanguageSpecificPrompt(N2C::BlueprintPromptFamily, RequestLanguage);

    TempPromptManager.Reset();

    // The JSON goes in <n2c_json> tags, as in ProcessN2CJson.
    // The callback keeps TempServiceObject (and so the service) alive until the request completes or is cancelled
    FN2CLLMRequestHandleRef Handle = TempService->SendRequest(UN2CSystemPromptManager::WrapN2CJson(JsonInput), SystemPrompt, FOnLLMResponseReceived::CreateLambda(
        [this, TempService, TempServiceObject, OnComplete, OnParsed, RequestConfigCopy = RequestConfig, RequestLanguage, JsonInput, bArchiveRun](const FString& RawResponse)
        {
            FN2CTranslationResponse TranslationResponse;
            UN2CResponseParserBase* Parser = TempServiceObject.IsValid() ? TempService->GetResponseParser() : nullptr; // Parser from the temp service
            bool bParseSuccess = Parser && Parser->ParseLLMResponse(RawResponse, TranslationResponse);

            if (bParseSuccess)
            {
                EndRequest(EN2CSystemStatus::Idle);

                // An MCP or Python translation is kept as a run of the translation archive (card 0252)
                if (bArchiveRun)
                {
                    ArchiveMcpTranslation(TranslationResponse, JsonInput, RequestConfigCopy, RequestLanguage);
                }

                TSharedPtr<FJsonObject> ResultJson = MakeShared<FJsonObject>();
                TArray<TSharedPtr<FJsonValue>> GraphsJsonArray;
                for (const auto& graph : TranslationResponse.Graphs)
                {
                    TSharedPtr<FJsonObject> GraphJson = MakeShared<FJsonObject>();
                    GraphJson->SetStringField(TEXT("graph_name"), graph.GraphName);
                    GraphJson->SetStringField(TEXT("graph_type"), graph.GraphType);
                    GraphJson->SetStringField(TEXT("graph_class"), graph.GraphClass);
                    
                    TSharedPtr<FJsonObject> CodeJson = MakeShared<FJsonObject>();
                    CodeJson->SetStringField(TEXT("graphDeclaration"), graph.Code.GraphDeclaration);
                    CodeJson->SetStringField(TEXT("graphImplementation"), graph.Code.GraphImplementation);
                    CodeJson->SetStringField(TEXT("implementationNotes"), graph.Code.ImplementationNotes);
                    GraphJson->SetObjectField(TEXT("code"), CodeJson);
                    
                    GraphsJsonArray.Add(MakeShared<FJsonValueObject>(GraphJson));
                }
                ResultJson->SetArrayField(TEXT("graphs"), GraphsJsonArray);
                
                ResultJson->SetObjectField(TEXT("usage"), TranslationResponse.Usage.ToJson());
                
                FString OutputJsonString;
                TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputJsonString);
                FJsonSerializer::Serialize(ResultJson.ToSharedRef(), Writer);

                OnParsed.ExecuteIfBound(TranslationResponse, true);
                OnComplete.ExecuteIfBound(OutputJsonString);
            }
            else
            {
                EndRequest(EN2CSystemStatus::Error);

                // The typed error as escaped JSON: the provider's HTTP status and message, not a parse failure
                if (!Parser)
                {
                    TranslationResponse.Error = FN2CTranslationError::Make(TEXT("No response parser available"));
                }
                FillTranslationError(TranslationResponse, RequestConfigCopy.Provider, RequestConfigCopy.Model, RawResponse);
                FN2CLogger::Get().LogError(FString::Printf(TEXT("Translation failed: %s"),
                    *TranslationResponse.Error.ToDisplayString()), TEXT("LLMModule"));
                OnParsed.ExecuteIfBound(TranslationResponse, false);
                OnComplete.ExecuteIfBound(TranslationResponse.Error.ToJsonString());
            }
        }
    ));
    ResetStatusOnCancel(Handle);
    return Handle;
}

void UN2CLLMModule::ArchiveMcpTranslation(const FN2CTranslationResponse& Response, const FString& JsonInput,
    const FN2CLLMConfig& RequestConfig, EN2CCodeLanguage Language)
{
    // The run is named after the Blueprint the N2C JSON describes (metadata.name)
    FString BlueprintName;
    TSharedPtr<FJsonObject> Input;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonInput);
    const TSharedPtr<FJsonObject>* Metadata = nullptr;
    if (FJsonSerializer::Deserialize(Reader, Input) && Input.IsValid() && Input->TryGetObjectField(TEXT("metadata"), Metadata))
    {
        (*Metadata)->TryGetStringField(TEXT("name"), BlueprintName);
    }

    FN2CTranslationRunInfo Info;
    Info.Kind = EN2CTranslationRunKind::Mcp;
    Info.Name = BlueprintName.IsEmpty() ? FString(TEXT("Translation")) : BlueprintName;
    Info.Provider = GetProviderName(RequestConfig.Provider);
    Info.Model = RequestConfig.Model;
    Info.Language = Language;
    const TSharedPtr<FN2CTranslationArchive> Run = FN2CTranslationArchive::BeginRun(Info);
    if (!Run.IsValid())
    {
        FN2CLogger::Get().LogWarning(TEXT("The translation could not be saved: no run folder"), TEXT("LLMModule"));
        return;
    }
    FN2CArchiveSource Source;
    Source.InputJson = JsonInput;
    FN2CArchivedResponse Written;
    Run->WriteResponse(Response, Source, FString(), Written);
}

FString UN2CLLMModule::GetProviderName(EN2CLLMProvider Provider)
{
    return StaticEnum<EN2CLLMProvider>()->GetNameStringByValue(static_cast<int64>(Provider));
}
