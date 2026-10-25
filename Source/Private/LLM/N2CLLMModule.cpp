// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/N2CLLMModule.h"

#include "Models/N2CBlueprint.h"
#include "Core/N2CSerializer.h"
#include "Core/N2CSettings.h"
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
        Instance->LatestTranslationPath = TEXT("");
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
    Settings->ResolveRequestModel(Config.Provider, FString(), Config);

    // Initialize provider registry
    InitializeProviderRegistry();

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
        UN2CLLMModule* StrongThis = WeakThis.Get();
        if (StrongThis && StrongThis->CurrentStatus == EN2CSystemStatus::Processing)
        {
            StrongThis->CurrentStatus = EN2CSystemStatus::Idle;
        }
        FN2CLogger::Get().Log(TEXT("LLM request cancelled"), EN2CLogSeverity::Info, TEXT("LLMModule"));
    });
}

FN2CLLMRequestHandleRef UN2CLLMModule::ProcessN2CJson(
    const FString& JsonInput,
    const FN2CBlueprint& CapturedBlueprint,
    const FOnLLMResponseReceived& OnComplete)
{
    // Creates and uses UObjects and writes CurrentStatus: game thread only (objects made elsewhere are never collected)
    check(IsInGameThread());

    if (!bIsInitialized)
    {
        CurrentStatus = EN2CSystemStatus::Error;
        FN2CLogger::Get().LogError(TEXT("LLM Module not initialized"), TEXT("LLMModule"));
        const FN2CTranslationError Error = FN2CTranslationError::Make(TEXT("LLM module not initialized"));
        BroadcastFailure(Error);
        return CompleteWithoutRequest(OnComplete, Error.ToJsonString());
    }

    CurrentStatus = EN2CSystemStatus::Processing;
    
    // Broadcast that request is being sent
    OnTranslationRequestSent.Broadcast();

    // Get active service
    TScriptInterface<IN2CLLMService> Service = GetActiveService();
    if (!Service.GetInterface())
    {
        CurrentStatus = EN2CSystemStatus::Error;
        FN2CLogger::Get().LogError(TEXT("No active LLM service"), TEXT("LLMModule"));
        const FN2CTranslationError Error = FN2CTranslationError::Make(TEXT("No active LLM service"));
        BroadcastFailure(Error);
        return CompleteWithoutRequest(OnComplete, Error.ToJsonString());
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

    // Send request through service, with the JSON in <n2c_json> tags (data, not instructions; see N2CJsonSpec.md).
    // The callback keeps the service it was sent on alive and parses with that
    // service's parser, even if the active provider changes or GC runs while the request is in flight.
    // The translation is saved with the Blueprint captured for this request, shared (not copied) by the delegate's copies
    TStrongObjectPtr<UObject> RequestServiceObject(Service.GetObject());
    const TSharedRef<const FN2CBlueprint> RequestBlueprint = MakeShared<const FN2CBlueprint>(CapturedBlueprint);
    FN2CLLMRequestHandleRef Handle = ActiveService->SendRequest(UN2CSystemPromptManager::WrapN2CJson(JsonInput), SystemPrompt, FOnLLMResponseReceived::CreateLambda(
        [this, OnComplete, RequestServiceObject, RequestService = Service, RequestModel = Config.Model, RequestBlueprint](const FString& Response)
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
                        CurrentStatus = EN2CSystemStatus::Idle;

                        // Save translation to disk, with the Blueprint captured when the request was sent
                        if (SaveTranslationToDisk(TranslationResponse, *RequestBlueprint))
                        {
                            FN2CLogger::Get().Log(TEXT("Successfully saved translation to disk"), EN2CLogSeverity::Info);
                        }

                        OnTranslationResponseReceived.Broadcast(TranslationResponse, true);
                        FN2CLogger::Get().Log(TEXT("Successfully parsed LLM response"), EN2CLogSeverity::Info);
                    }
                    else
                    {
                        // The typed error (HTTP status, provider message, kind) reaches the broadcast's listeners
                        CurrentStatus = EN2CSystemStatus::Error;
                        FillTranslationError(TranslationResponse, ActiveServiceParser->GetProviderType(), RequestModel, Response);
                        FN2CLogger::Get().LogError(FString::Printf(TEXT("Translation failed: %s"),
                            *TranslationResponse.Error.ToDisplayString()), TEXT("LLMModule"));
                        OnTranslationResponseReceived.Broadcast(TranslationResponse, false);
                    }
                }
                else
                {
                    CurrentStatus = EN2CSystemStatus::Error;
                    FN2CLogger::Get().LogError(TEXT("No response parser available"));
                    TranslationResponse.Error = FN2CTranslationError::Make(TEXT("No response parser available"));
                    OnTranslationResponseReceived.Broadcast(TranslationResponse, false);
                }
            }
            else
            {
                CurrentStatus = EN2CSystemStatus::Error;
                FN2CLogger::Get().LogError(TEXT("No active LLM service"));
                TranslationResponse.Error = FN2CTranslationError::Make(TEXT("No active LLM service"));
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

void UN2CLLMModule::OpenTranslationFolder(bool& Success)
{
    FString PathToOpen = LatestTranslationPath;
    
    if (PathToOpen.IsEmpty())
    {
        FN2CLogger::Get().LogWarning(TEXT("No translation path available, opening the base path"));
        Success = true;
        PathToOpen = GetTranslationBasePath();
    }

    if (!FPaths::DirectoryExists(PathToOpen))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Translation directory does not exist: %s \n\nOpening the base path"), *PathToOpen));
        Success = true;
        PathToOpen = GetTranslationBasePath();
    }

    FPlatformProcess::ExploreFolder(*PathToOpen);
    Success = true;
    
}

bool UN2CLLMModule::SaveTranslationToDisk(const FN2CTranslationResponse& Response, const FN2CBlueprint& Blueprint)
{
    // Get blueprint name from metadata
    FString BlueprintName = Blueprint.Metadata.Name;
    if (BlueprintName.IsEmpty())
    {
        BlueprintName = TEXT("UnknownBlueprint");
    }
    
    // Generate root path for this translation
    FString RootPath = GenerateTranslationRootPath(BlueprintName);
    
    // Ensure the directory exists
    if (!EnsureDirectoryExists(RootPath))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to create translation directory: %s"), *RootPath));
        return false;
    }
    
    // Store the path for later reference
    LatestTranslationPath = RootPath;
    
    // Save the Blueprint JSON (pretty-printed)
    FString JsonFileName = FString::Printf(TEXT("N2C_BP_%s.json"), *FPaths::GetBaseFilename(RootPath));
    FString JsonFilePath = FPaths::Combine(RootPath, JsonFileName);
    
    // Serialize the Blueprint to JSON with pretty printing
    FString JsonContent = FN2CSerializer::ToJson(Blueprint, EN2CJsonStyle::Pretty);
    
    if (!FFileHelper::SaveStringToFile(JsonContent, *JsonFilePath))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to save JSON file: %s"), *JsonFilePath));
        return false;
    }
    
    // Save minified version of the Blueprint JSON
    FString MinifiedJsonFileName = FString::Printf(TEXT("N2C_BP_Minified_%s.json"), *FPaths::GetBaseFilename(RootPath));
    FString MinifiedJsonFilePath = FPaths::Combine(RootPath, MinifiedJsonFileName);
    
    // Serialize the Blueprint to JSON without pretty printing
    FString MinifiedJsonContent = FN2CSerializer::ToJson(Blueprint, EN2CJsonStyle::Condensed);
    
    if (!FFileHelper::SaveStringToFile(MinifiedJsonContent, *MinifiedJsonFilePath))
    {
        FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Failed to save minified JSON file: %s"), *MinifiedJsonFilePath));
        // Continue even if minified version fails
    }
    
    // Save the raw LLM translation response JSON
    FString TranslationJsonFileName = FString::Printf(TEXT("N2C_Translation_%s.json"), *FPaths::GetBaseFilename(RootPath));
    FString TranslationJsonFilePath = FPaths::Combine(RootPath, TranslationJsonFileName);
    
    // Serialize the Translation response to JSON
    TSharedPtr<FJsonObject> TranslationJsonObject = MakeShared<FJsonObject>();
    
    // Create graphs array
    TArray<TSharedPtr<FJsonValue>> GraphsArray;
    for (const FN2CGraphTranslation& Graph : Response.Graphs)
    {
        TSharedPtr<FJsonObject> GraphObject = MakeShared<FJsonObject>();
        GraphObject->SetStringField(TEXT("graph_name"), Graph.GraphName);
        GraphObject->SetStringField(TEXT("graph_type"), Graph.GraphType);
        GraphObject->SetStringField(TEXT("graph_class"), Graph.GraphClass);
        
        // Create code object
        TSharedPtr<FJsonObject> CodeObject = MakeShared<FJsonObject>();
        CodeObject->SetStringField(TEXT("graphDeclaration"), Graph.Code.GraphDeclaration);
        CodeObject->SetStringField(TEXT("graphImplementation"), Graph.Code.GraphImplementation);
        CodeObject->SetStringField(TEXT("implementationNotes"), Graph.Code.ImplementationNotes);
        
        GraphObject->SetObjectField(TEXT("code"), CodeObject);
        GraphsArray.Add(MakeShared<FJsonValueObject>(GraphObject));
    }
    
    TranslationJsonObject->SetArrayField(TEXT("graphs"), GraphsArray);
    
    // Add usage information if available
    if (Response.Usage.InputTokens > 0 || Response.Usage.OutputTokens > 0)
    {
        TSharedPtr<FJsonObject> UsageObject = MakeShared<FJsonObject>();
        UsageObject->SetNumberField(TEXT("input_tokens"), Response.Usage.InputTokens);
        UsageObject->SetNumberField(TEXT("output_tokens"), Response.Usage.OutputTokens);
        TranslationJsonObject->SetObjectField(TEXT("usage"), UsageObject);
    }
    
    // Serialize to string with pretty printing
    FString TranslationJsonContent;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&TranslationJsonContent);
    FJsonSerializer::Serialize(TranslationJsonObject.ToSharedRef(), Writer);
    
    if (!FFileHelper::SaveStringToFile(TranslationJsonContent, *TranslationJsonFilePath))
    {
        FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Failed to save translation JSON file: %s"), *TranslationJsonFilePath));
        // Continue even if translation JSON fails
    }
    
    // Get the target language from settings
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    EN2CCodeLanguage TargetLanguage = Settings ? Settings->TargetLanguage : EN2CCodeLanguage::Cpp;
    
    // Save each graph's files
    for (const FN2CGraphTranslation& Graph : Response.Graphs)
    {
        // The graph name comes from the LLM response: never use it as a path without making it a safe stem
        const FString Stem = FN2CPathUtils::GetTranslationFileStem(Graph.GraphName);
        if (Stem.IsEmpty())
        {
            continue;
        }

        // Create directory for this graph
        FString GraphDir = FPaths::Combine(RootPath, Stem);
        if (!FN2CPathUtils::IsUnderDirectoryResolved(GraphDir, RootPath))
        {
            FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Skipping graph '%s': its folder would be outside the translation root"), *Graph.GraphName));
            continue;
        }
        if (!EnsureDirectoryExists(GraphDir))
        {
            FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Failed to create graph directory: %s"), *GraphDir));
            continue;
        }
        
        // Save declaration file (C++ only)
        if (TargetLanguage == EN2CCodeLanguage::Cpp && !Graph.Code.GraphDeclaration.IsEmpty())
        {
            FString HeaderPath = FPaths::Combine(GraphDir, Stem + TEXT(".h"));
            if (!FFileHelper::SaveStringToFile(Graph.Code.GraphDeclaration, *HeaderPath))
            {
                FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Failed to save header file: %s"), *HeaderPath));
            }
        }
        
        // Save implementation file with appropriate extension
        if (!Graph.Code.GraphImplementation.IsEmpty())
        {
            FString Extension = GetFileExtensionForLanguage(TargetLanguage);
            FString ImplPath = FPaths::Combine(GraphDir, Stem + Extension);
            if (!FFileHelper::SaveStringToFile(Graph.Code.GraphImplementation, *ImplPath))
            {
                FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Failed to save implementation file: %s"), *ImplPath));
            }
        }
        
        // Save implementation notes
        if (!Graph.Code.ImplementationNotes.IsEmpty())
        {
            FString NotesPath = FPaths::Combine(GraphDir, Stem + TEXT("_Notes.txt"));
            if (!FFileHelper::SaveStringToFile(Graph.Code.ImplementationNotes, *NotesPath))
            {
                FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Failed to save notes file: %s"), *NotesPath));
            }
        }
    }
    
    FN2CLogger::Get().Log(FString::Printf(TEXT("Translation saved to: %s"), *RootPath), EN2CLogSeverity::Info);
    return true;
}

FString UN2CLLMModule::GenerateTranslationRootPath(const FString& BlueprintName) const
{
    // Get current date/time
    FDateTime Now = FDateTime::Now();
    FString Timestamp = Now.ToString(TEXT("%Y-%m-%d-%H.%M.%S"));
    
    // Create folder name
    FString FolderName = FString::Printf(TEXT("%s_%s"), *FN2CPathUtils::GetTranslationFileStem(BlueprintName), *Timestamp);

    // Get the saved translations base path
    FString BasePath = GetTranslationBasePath();
    
    return FPaths::Combine(BasePath, FolderName);
}

FString UN2CLLMModule::GetTranslationBasePath() const
{
    // Check if custom output directory is set in settings
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    FString BasePath;
    
    if (Settings && !Settings->CustomTranslationOutputDirectory.Path.IsEmpty())
    {
        // Use custom path if specified
        BasePath = Settings->CustomTranslationOutputDirectory.Path;
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Using custom translation output directory: %s"), *BasePath),
            EN2CLogSeverity::Info);
    }
    else
    {
        // Use default path
        BasePath = FPaths::ProjectSavedDir() / TEXT("NodeToCode") / TEXT("Translations");
    }

    return BasePath;
}

FString UN2CLLMModule::GetFileExtensionForLanguage(EN2CCodeLanguage Language) const
{
    switch (Language)
    {
        case EN2CCodeLanguage::Cpp:
            return TEXT(".cpp");
        case EN2CCodeLanguage::Python:
            return TEXT(".py");
        case EN2CCodeLanguage::JavaScript:
            return TEXT(".js");
        case EN2CCodeLanguage::CSharp:
            return TEXT(".cs");
        case EN2CCodeLanguage::Swift:
            return TEXT(".swift");
        case EN2CCodeLanguage::Pseudocode:
            return TEXT(".md");
        default:
            return TEXT(".txt");
    }
}

bool UN2CLLMModule::EnsureDirectoryExists(const FString& DirectoryPath) const
{
    if (!FPaths::DirectoryExists(DirectoryPath))
    {
        bool bSuccess = FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*DirectoryPath);
        if (!bSuccess)
        {
            FN2CLogger::Get().LogError(
                FString::Printf(TEXT("Failed to create directory: %s"), *DirectoryPath));
            return false;
        }
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Created directory: %s"), *DirectoryPath),
            EN2CLogSeverity::Info);
        return true;
    }
    return true;
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
    const FOnLLMResponseReceived& OnComplete)
{
    // Creates a provider service and a prompt manager and writes CurrentStatus: game thread only. Objects created on
    // another thread get EInternalObjectFlags::Async and are never collected.
    check(IsInGameThread());

    CurrentStatus = EN2CSystemStatus::Processing;
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
        CurrentStatus = EN2CSystemStatus::Error;
        return CompleteWithoutRequest(OnComplete, FN2CTranslationError::MakeErrorJson(ErrorMsg));
    }
    
    // The temporary service is referenced only by this request: hold it strongly so GC cannot free it mid-request
    TStrongObjectPtr<UObject> TempServiceObject(TempService.GetObject());

    // Create a temporary prompt manager for this request to ensure correct language-specific prompt. Nothing else
    // references it, so hold it strongly while it builds the prompt; it is collected after this call.
    TStrongObjectPtr<UN2CSystemPromptManager> TempPromptManager(NewObject<UN2CSystemPromptManager>(this));
    TempPromptManager->Initialize(RequestConfig); // Initialize with the request-specific config
    FString SystemPrompt = TempPromptManager->GetLanguageSpecificPrompt(TEXT("CodeGen"), RequestLanguage);

    TempPromptManager.Reset();

    // The JSON goes in <n2c_json> tags, as in ProcessN2CJson.
    // The callback keeps TempServiceObject (and so the service) alive until the request completes or is cancelled
    FN2CLLMRequestHandleRef Handle = TempService->SendRequest(UN2CSystemPromptManager::WrapN2CJson(JsonInput), SystemPrompt, FOnLLMResponseReceived::CreateLambda(
        [this, TempService, TempServiceObject, OnComplete, RequestConfigCopy = RequestConfig](const FString& RawResponse)
        {
            FN2CTranslationResponse TranslationResponse;
            UN2CResponseParserBase* Parser = TempServiceObject.IsValid() ? TempService->GetResponseParser() : nullptr; // Parser from the temp service
            bool bParseSuccess = Parser && Parser->ParseLLMResponse(RawResponse, TranslationResponse);

            if (bParseSuccess)
            {
                CurrentStatus = EN2CSystemStatus::Idle; // Reset status after successful processing

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
                
                TSharedPtr<FJsonObject> UsageJson = MakeShared<FJsonObject>();
                UsageJson->SetNumberField(TEXT("input_tokens"), TranslationResponse.Usage.InputTokens);
                UsageJson->SetNumberField(TEXT("output_tokens"), TranslationResponse.Usage.OutputTokens);
                ResultJson->SetObjectField(TEXT("usage"), UsageJson);
                
                FString OutputJsonString;
                TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputJsonString);
                FJsonSerializer::Serialize(ResultJson.ToSharedRef(), Writer);

                OnComplete.ExecuteIfBound(OutputJsonString);
            }
            else
            {
                CurrentStatus = EN2CSystemStatus::Error; // Set error status

                // The typed error as escaped JSON: the provider's HTTP status and message, not a parse failure
                if (!Parser)
                {
                    TranslationResponse.Error = FN2CTranslationError::Make(TEXT("No response parser available"));
                }
                FillTranslationError(TranslationResponse, RequestConfigCopy.Provider, RequestConfigCopy.Model, RawResponse);
                FN2CLogger::Get().LogError(FString::Printf(TEXT("Translation failed: %s"),
                    *TranslationResponse.Error.ToDisplayString()), TEXT("LLMModule"));
                OnComplete.ExecuteIfBound(TranslationResponse.Error.ToJsonString());
            }
        }
    ));
    ResetStatusOnCancel(Handle);
    return Handle;
}
