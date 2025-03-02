// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/N2CLLMModule.h"

#include "Core/N2CNodeTranslator.h"
#include "Core/N2CSerializer.h"
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
                            
                        // Save translation to disk
                        const FN2CBlueprint& Blueprint = FN2CNodeTranslator::Get().GetN2CBlueprint();
                        if (SaveTranslationToDisk(TranslationResponse, Blueprint))
                        {
                            FN2CLogger::Get().Log(TEXT("Successfully saved translation to disk"), EN2CLogSeverity::Info);
                        }
                            
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

void UN2CLLMModule::OpenTranslationFolder(bool& Success)
{
    if (LatestTranslationPath.IsEmpty())
    {
        FN2CLogger::Get().LogWarning(TEXT("No translation path available"));
        Success = false;
    }

    if (!FPaths::DirectoryExists(LatestTranslationPath))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Translation directory does not exist: %s"), *LatestTranslationPath));
        Success = false;
    }

#if PLATFORM_WINDOWS
    FPlatformProcess::ExploreFolder(*LatestTranslationPath);
    Success = false;
#endif
    
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
    FString JsonFileName = FString::Printf(TEXT("N2C_JSON_%s.json"), *FPaths::GetBaseFilename(RootPath));
    FString JsonFilePath = FPaths::Combine(RootPath, JsonFileName);
    
    // Serialize the Blueprint to JSON with pretty printing
    FN2CSerializer::SetPrettyPrint(true);
    FString JsonContent = FN2CSerializer::ToJson(Blueprint);
    
    if (!FFileHelper::SaveStringToFile(JsonContent, *JsonFilePath))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to save JSON file: %s"), *JsonFilePath));
        return false;
    }
    
    // Save minified version of the Blueprint JSON
    FString MinifiedJsonFileName = FString::Printf(TEXT("N2C_JSON_%s_minified.json"), *FPaths::GetBaseFilename(RootPath));
    FString MinifiedJsonFilePath = FPaths::Combine(RootPath, MinifiedJsonFileName);
    
    // Serialize the Blueprint to JSON without pretty printing
    FN2CSerializer::SetPrettyPrint(false);
    FString MinifiedJsonContent = FN2CSerializer::ToJson(Blueprint);
    
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
        // Skip graphs with empty names
        if (Graph.GraphName.IsEmpty())
        {
            continue;
        }
        
        // Create directory for this graph
        FString GraphDir = FPaths::Combine(RootPath, Graph.GraphName);
        if (!EnsureDirectoryExists(GraphDir))
        {
            FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Failed to create graph directory: %s"), *GraphDir));
            continue;
        }
        
        // Save declaration file (C++ only)
        if (TargetLanguage == EN2CCodeLanguage::Cpp && !Graph.Code.GraphDeclaration.IsEmpty())
        {
            FString HeaderPath = FPaths::Combine(GraphDir, Graph.GraphName + TEXT(".h"));
            if (!FFileHelper::SaveStringToFile(Graph.Code.GraphDeclaration, *HeaderPath))
            {
                FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Failed to save header file: %s"), *HeaderPath));
            }
        }
        
        // Save implementation file with appropriate extension
        if (!Graph.Code.GraphImplementation.IsEmpty())
        {
            FString Extension = GetFileExtensionForLanguage(TargetLanguage);
            FString ImplPath = FPaths::Combine(GraphDir, Graph.GraphName + Extension);
            if (!FFileHelper::SaveStringToFile(Graph.Code.GraphImplementation, *ImplPath))
            {
                FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Failed to save implementation file: %s"), *ImplPath));
            }
        }
        
        // Save implementation notes
        if (!Graph.Code.ImplementationNotes.IsEmpty())
        {
            FString NotesPath = FPaths::Combine(GraphDir, Graph.GraphName + TEXT("_Notes.txt"));
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
    FString FolderName = FString::Printf(TEXT("%s_%s"), *BlueprintName, *Timestamp);
    
    // Build full path
    FString BasePath = FPaths::ProjectSavedDir() / TEXT("NodeToCode") / TEXT("Translations");
    return FPaths::Combine(BasePath, FolderName);
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
        return FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*DirectoryPath);
    }
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
