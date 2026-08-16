// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/N2CLLMModule.h"

#include "Core/N2CNodeTranslator.h"
#include "Core/N2CSerializer.h"
#include "Core/N2CSettings.h"
#include "LLM/N2CBatchTranslationConsolidator.h"
#include "LLM/N2CSystemPromptManager.h"
#include "LLM/N2CBaseLLMService.h"
#include "LLM/N2CLLMProviderRegistry.h"
#include "LLM/Providers/N2CAnthropicService.h"
#include "LLM/Providers/N2CDeepSeekService.h"
#include "LLM/Providers/N2CGeminiService.h"
#include "LLM/Providers/N2CLMStudioService.h"
#include "LLM/Providers/N2COpenAIService.h"
#include "LLM/Providers/N2COllamaService.h"
#include "LLM/Providers/N2CMiniMaxService.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
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
    bIsInitialized = false;
    ResetRequestSession();
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

void UN2CLLMModule::ResetRequestSession()
{
    InFlightRequestCount = 0;
    NextRequestId = 1;
    bSessionHadError = false;
    SessionRawResponses.Reset();
    SessionTranslationResponse.Graphs.Reset();
    SessionTranslationResponse.Usage.InputTokens = 0;
    SessionTranslationResponse.Usage.OutputTokens = 0;

    // A new Initialize() starts a new user operation. Clear any stale batch path left by an
    // interrupted/failed prior batch, while intentionally retaining LatestTranslationPath so the
    // Open Folder action can still reach the last completed output.
    CurrentBatchRootPath.Empty();
    CurrentStatus = EN2CSystemStatus::Idle;
}

void UN2CLLMModule::AppendSessionResponse(const FN2CTranslationResponse& Response)
{
    for (const FN2CGraphTranslation& Graph : Response.Graphs)
    {
        const int32 ExistingIndex = SessionTranslationResponse.Graphs.IndexOfByPredicate(
            [&Graph](const FN2CGraphTranslation& Existing)
            {
                return Existing.GraphName.Equals(Graph.GraphName, ESearchCase::CaseSensitive) &&
                       Existing.GraphType.Equals(Graph.GraphType, ESearchCase::CaseSensitive) &&
                       Existing.GraphClass.Equals(Graph.GraphClass, ESearchCase::CaseSensitive);
            });

        if (ExistingIndex == INDEX_NONE)
        {
            SessionTranslationResponse.Graphs.Add(Graph);
        }
        else
        {
            SessionTranslationResponse.Graphs[ExistingIndex] = Graph;
        }
    }

    SessionTranslationResponse.Usage.InputTokens += Response.Usage.InputTokens;
    SessionTranslationResponse.Usage.OutputTokens += Response.Usage.OutputTokens;
}

void UN2CLLMModule::FinishRequest(bool bSuccess)
{
    bSessionHadError |= !bSuccess;
    InFlightRequestCount = FMath::Max(0, InFlightRequestCount - 1);

    if (InFlightRequestCount > 0)
    {
        CurrentStatus = EN2CSystemStatus::Processing;
    }
    else
    {
        CurrentStatus = bSessionHadError ? EN2CSystemStatus::Error : EN2CSystemStatus::Idle;
    }
}

FString UN2CLLMModule::FormatRawResponseForDisplay(const FString& RawResponse) const
{
    if (RawResponse.IsEmpty())
    {
        return RawResponse;
    }

    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RawResponse);
    if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
    {
        return RawResponse;
    }

    FString FormattedResponse;
    TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&FormattedResponse);
    FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);
    return FormattedResponse;
}

void UN2CLLMModule::ProcessN2CJson(
    const FString& JsonInput,
    const FOnLLMResponseReceived& OnComplete)
{
    if (!bIsInitialized)
    {
        CurrentStatus = EN2CSystemStatus::Error;
        FN2CLogger::Get().LogError(TEXT("LLM Module not initialized"), TEXT("LLMModule"));
        OnComplete.ExecuteIfBound(TEXT("{\"error\": \"Module not initialized\"}"));
        return;
    }

    if (!ActiveService.GetInterface())
    {
        CurrentStatus = EN2CSystemStatus::Error;
        FN2CLogger::Get().LogError(TEXT("No active LLM service"), TEXT("LLMModule"));
        OnComplete.ExecuteIfBound(TEXT("{\"error\": \"No active service\"}"));
        return;
    }

    // Get active service
    TScriptInterface<IN2CLLMService> Service = GetActiveService();
    if (!Service.GetInterface())
    {
        CurrentStatus = EN2CSystemStatus::Error;
        FN2CLogger::Get().LogError(TEXT("No active service"), TEXT("LLMModule"));
        OnComplete.ExecuteIfBound(TEXT("{\"error\": \"No active service\"}"));
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

    const int32 RequestId = NextRequestId++;
    const EN2CLLMProvider RequestProvider = Config.Provider;
    const FString RequestModel = Config.Model;
    ++InFlightRequestCount;
    CurrentStatus = EN2CSystemStatus::Processing;

    // Broadcast that request is being sent
    OnTranslationRequestSent.Broadcast();

    // Send request through service
    ActiveService->SendRequest(JsonInput, SystemPrompt, FOnLLMResponseReceived::CreateLambda(
        [this, OnComplete, RequestId, RequestProvider, RequestModel](const FString& Response)
        {
            FN2CTranslationResponse TranslationResponse;
            TranslationResponse.Usage.InputTokens = 0;
            TranslationResponse.Usage.OutputTokens = 0;
            bool bParsedSuccessfully = false;
            FString RequestLabel = FString::Printf(TEXT("Request %d"), RequestId);
            
            // Get active service's response parser
            TScriptInterface<IN2CLLMService> ActiveServiceParser = GetActiveService();
            if (ActiveServiceParser.GetInterface())
            {
                UN2CResponseParserBase* Parser = ActiveServiceParser->GetResponseParser();
                if (Parser)
                {
                    if (Parser->ParseLLMResponse(Response, TranslationResponse))
                    {
                        bParsedSuccessfully = true;
                        if (!TranslationResponse.Graphs.IsEmpty() &&
                            !TranslationResponse.Graphs[0].GraphName.IsEmpty())
                        {
                            RequestLabel = TranslationResponse.Graphs[0].GraphName;
                        }

                        // Merge before saving/broadcasting so a batch manifest and the existing UI
                        // always see every successful result accumulated in this translation session.
                        AppendSessionResponse(TranslationResponse);
                            
                        // Save translation to disk
                        const FN2CBlueprint& Blueprint = FN2CNodeTranslator::Get().GetN2CBlueprint();
                        if (SaveTranslationToDisk(TranslationResponse, Blueprint))
                        {
                            FN2CLogger::Get().Log(TEXT("Successfully saved translation to disk"), EN2CLogSeverity::Info);
                        }
                            
                        OnTranslationResponseReceived.Broadcast(SessionTranslationResponse, true);
                        FN2CLogger::Get().Log(TEXT("Successfully parsed LLM response"), EN2CLogSeverity::Info);
                    }
                    else
                    {
                        FN2CLogger::Get().LogError(TEXT("Failed to parse LLM response"));
                        SaveRawResponseToDisk(Response);
                        OnTranslationResponseReceived.Broadcast(SessionTranslationResponse, false);
                    }
                }
                else
                {
                    FN2CLogger::Get().LogError(TEXT("No response parser available"));
                    OnTranslationResponseReceived.Broadcast(SessionTranslationResponse, false);
                }
            }
            else
            {
                FN2CLogger::Get().LogError(TEXT("No active LLM service"));
                OnTranslationResponseReceived.Broadcast(SessionTranslationResponse, false);
            }

            FN2CRawResponseRecord RawRecord;
            RawRecord.RequestId = RequestId;
            RawRecord.RequestLabel = RequestLabel;
            RawRecord.Provider = RequestProvider;
            RawRecord.Model = RequestModel;
            RawRecord.Timestamp = FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S"));
            RawRecord.FormattedResponse = FormatRawResponseForDisplay(Response);
            RawRecord.bParsedSuccessfully = bParsedSuccessfully;
            SessionRawResponses.Add(MoveTemp(RawRecord));

            // The caller's completion delegate is part of the request lifecycle. This was
            // previously omitted for normal HTTP completions, which prevented Translate Entire
            // Blueprint's remaining-response counter from ever reaching zero.
            OnComplete.ExecuteIfBound(Response);

            // Keep Processing active until both the HTTP response and all caller completion work
            // for this request have finished.
            FinishRequest(bParsedSuccessfully);
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

#if PLATFORM_WINDOWS
    FPlatformProcess::ExploreFolder(*PathToOpen);
    Success = true;
#endif
#if PLATFORM_MAC
    FMacPlatformProcess::ExploreFolder(*PathToOpen);
    Success = true;
#endif
    
}

void UN2CLLMModule::BeginBatchTranslation(const FString& BlueprintName)
{
    // Generate a shared root path for this batch
    FString BlueprintNameToUse = BlueprintName;
    if (BlueprintNameToUse.IsEmpty())
    {
        BlueprintNameToUse = TEXT("UnknownBlueprint");
    }
    CurrentBatchRootPath = GenerateTranslationRootPath(BlueprintNameToUse);
    FN2CLogger::Get().Log(FString::Printf(TEXT("Batch translation started, root path: %s"), *CurrentBatchRootPath), EN2CLogSeverity::Info);
}

void UN2CLLMModule::EndBatchTranslation()
{
    if (CurrentBatchRootPath.IsEmpty())
    {
        FN2CLogger::Get().Log(TEXT("Batch translation ended"), EN2CLogSeverity::Info);
        return;
    }

    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    const EN2CCodeLanguage TargetLanguage = Settings ? Settings->TargetLanguage : EN2CCodeLanguage::Cpp;

    // Preserve the existing non-C++ batch behavior. C++ full-Blueprint translations receive one
    // additional semantic reconciliation request after all graph requests have completed.
    if (TargetLanguage != EN2CCodeLanguage::Cpp || SessionTranslationResponse.Graphs.IsEmpty())
    {
        CurrentBatchRootPath.Empty();
        FN2CLogger::Get().Log(TEXT("Batch translation ended"), EN2CLogSeverity::Info);
        return;
    }

    if (!ActiveService.GetInterface())
    {
        bSessionHadError = true;
        CurrentBatchRootPath.Empty();
        FN2CLogger::Get().LogError(
            TEXT("Cannot run final Blueprint consolidation because no active LLM service is available"),
            TEXT("BatchConsolidation"));
        OnTranslationResponseReceived.Broadcast(SessionTranslationResponse, false);
        return;
    }

    const FN2CBlueprint& Blueprint = FN2CNodeTranslator::Get().GetN2CBlueprint();
    FString ConsolidationPayload;
    if (!FN2CBatchTranslationConsolidator::BuildRequestPayload(
            SessionTranslationResponse,
            Blueprint,
            ConsolidationPayload))
    {
        bSessionHadError = true;
        CurrentBatchRootPath.Empty();
        FN2CLogger::Get().LogError(
            TEXT("Failed to build final Blueprint consolidation request"),
            TEXT("BatchConsolidation"));
        OnTranslationResponseReceived.Broadcast(SessionTranslationResponse, false);
        return;
    }

    const FString ConsolidationPrompt = FN2CBatchTranslationConsolidator::GetSystemPrompt();
    const FString BatchRootPath = CurrentBatchRootPath;
    const int32 PriorInputTokens = SessionTranslationResponse.Usage.InputTokens;
    const int32 PriorOutputTokens = SessionTranslationResponse.Usage.OutputTokens;
    const int32 RequestId = NextRequestId++;
    const EN2CLLMProvider RequestProvider = Config.Provider;
    const FString RequestModel = Config.Model;

    ++InFlightRequestCount;
    CurrentStatus = EN2CSystemStatus::Processing;
    OnTranslationRequestSent.Broadcast();

    FN2CLogger::Get().Log(
        FString::Printf(
            TEXT("Starting final Blueprint consolidation request with %d parsed graph translation(s)"),
            SessionTranslationResponse.Graphs.Num()),
        EN2CLogSeverity::Info,
        TEXT("BatchConsolidation"));

    // Use the same active provider/model selected for this translation operation. Calling the
    // service directly gives this pass its dedicated reconciliation system prompt while still
    // flowing through UN2CBaseLLMService, so global/model/ad-hoc instructions and attached context
    // files continue to apply consistently.
    ActiveService->SendRequest(
        ConsolidationPayload,
        ConsolidationPrompt,
        FOnLLMResponseReceived::CreateLambda(
            [this,
             BatchRootPath,
             PriorInputTokens,
             PriorOutputTokens,
             RequestId,
             RequestProvider,
             RequestModel](const FString& Response)
            {
                bool bSuccess = false;
                FN2CTranslationResponse FinalResponse;
                FinalResponse.Usage.InputTokens = 0;
                FinalResponse.Usage.OutputTokens = 0;
                FString ValidationError;

                TScriptInterface<IN2CLLMService> Service = GetActiveService();
                UN2CResponseParserBase* Parser = Service.GetInterface()
                    ? Service->GetResponseParser()
                    : nullptr;

                if (!Parser)
                {
                    ValidationError = TEXT("No response parser available for final Blueprint consolidation");
                }
                else if (!Parser->ParseLLMResponse(Response, FinalResponse))
                {
                    ValidationError = TEXT("Failed to parse final Blueprint consolidation response");
                }
                else if (!FN2CBatchTranslationConsolidator::ValidateFinalResponse(
                             FinalResponse,
                             ValidationError))
                {
                    // ValidationError is populated by the structural validator.
                }
                else
                {
                    // Preserve aggregate usage from the graph translation phase while adding any
                    // usage supplied by the final provider parser.
                    FinalResponse.Usage.InputTokens += PriorInputTokens;
                    FinalResponse.Usage.OutputTokens += PriorOutputTokens;
                    SessionTranslationResponse = MoveTemp(FinalResponse);

                    const FN2CBlueprint& CurrentBlueprint = FN2CNodeTranslator::Get().GetN2CBlueprint();
                    const bool bManifestSaved = SaveTranslationToDisk(
                        SessionTranslationResponse,
                        CurrentBlueprint);
                    const bool bCodeSaved = FN2CBatchTranslationConsolidator::SaveCppFiles(
                        SessionTranslationResponse,
                        BatchRootPath);

                    bSuccess = bManifestSaved && bCodeSaved;
                    if (!bManifestSaved)
                    {
                        ValidationError = TEXT("Failed to save final consolidated translation manifest");
                    }
                    else if (!bCodeSaved)
                    {
                        ValidationError = TEXT("Failed to save final consolidated C++ pair");
                    }
                }

                FN2CRawResponseRecord RawRecord;
                RawRecord.RequestId = RequestId;
                RawRecord.RequestLabel = TEXT("Final Consolidation");
                RawRecord.Provider = RequestProvider;
                RawRecord.Model = RequestModel;
                RawRecord.Timestamp = FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S"));
                RawRecord.FormattedResponse = FormatRawResponseForDisplay(Response);
                RawRecord.bParsedSuccessfully = bSuccess;
                SessionRawResponses.Add(MoveTemp(RawRecord));

                if (bSuccess)
                {
                    FN2CLogger::Get().Log(
                        TEXT("Final Blueprint consolidation completed successfully"),
                        EN2CLogSeverity::Info,
                        TEXT("BatchConsolidation"));
                    OnTranslationResponseReceived.Broadcast(SessionTranslationResponse, true);
                }
                else
                {
                    bSessionHadError = true;
                    FN2CLogger::Get().LogError(
                        FString::Printf(
                            TEXT("Final Blueprint consolidation failed: %s"),
                            *ValidationError),
                        TEXT("BatchConsolidation"));
                    SaveRawResponseToDisk(Response);
                    OnTranslationResponseReceived.Broadcast(SessionTranslationResponse, false);
                }

                // The batch root must remain active through parsing/saving so failures and the final
                // manifest are written into the same translation session directory.
                CurrentBatchRootPath.Empty();
                FN2CLogger::Get().Log(TEXT("Batch translation ended"), EN2CLogSeverity::Info);
                FinishRequest(bSuccess);
            }));
}

bool UN2CLLMModule::SaveTranslationToDisk(const FN2CTranslationResponse& Response, const FN2CBlueprint& Blueprint)
{
    // Get blueprint name from metadata
    FString BlueprintName = Blueprint.Metadata.Name;
    if (BlueprintName.IsEmpty())
    {
        BlueprintName = TEXT("UnknownBlueprint");
    }

    // Use batch root path if in batch mode, otherwise generate a new timestamped path for each translation
    FString RootPath;
    if (!CurrentBatchRootPath.IsEmpty())
    {
        // Batch mode: reuse the shared root path
        RootPath = CurrentBatchRootPath;
    }
    else
    {
        // Single translation mode: generate a new timestamped directory for each translation
        RootPath = GenerateTranslationRootPath(BlueprintName);
    }

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
    FN2CSerializer::SetPrettyPrint(true);
    FString JsonContent = FN2CSerializer::ToJson(Blueprint);

    if (!FFileHelper::SaveStringToFile(JsonContent, *JsonFilePath))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to save JSON file: %s"), *JsonFilePath));
        return false;
    }

    // Save minified version of the Blueprint JSON
    FString MinifiedJsonFileName = FString::Printf(TEXT("N2C_BP_Minified_%s.json"), *FPaths::GetBaseFilename(RootPath));
    FString MinifiedJsonFilePath = FPaths::Combine(RootPath, MinifiedJsonFileName);

    // Serialize the Blueprint JSON without pretty printing
    FN2CSerializer::SetPrettyPrint(false);
    FString MinifiedJsonContent = FN2CSerializer::ToJson(Blueprint);

    if (!FFileHelper::SaveStringToFile(MinifiedJsonContent, *MinifiedJsonFilePath))
    {
        FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Failed to save minified JSON file: %s"), *MinifiedJsonFilePath));
        // Continue even if minified version fails
    }

    // During a batch, write the accumulated session response so this manifest contains every graph
    // completed so far rather than being overwritten by only the most recent response.
    const FN2CTranslationResponse& ManifestResponse =
        !CurrentBatchRootPath.IsEmpty() ? SessionTranslationResponse : Response;

    FString TranslationJsonFileName = FString::Printf(TEXT("N2C_Translation_%s.json"), *FPaths::GetBaseFilename(RootPath));
    FString TranslationJsonFilePath = FPaths::Combine(RootPath, TranslationJsonFileName);

    // Serialize the Translation response to JSON
    TSharedPtr<FJsonObject> TranslationJsonObject = MakeShared<FJsonObject>();

    // Create graphs array
    TArray<TSharedPtr<FJsonValue>> GraphsArray;
    for (const FN2CGraphTranslation& Graph : ManifestResponse.Graphs)
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
    if (ManifestResponse.Usage.InputTokens > 0 || ManifestResponse.Usage.OutputTokens > 0)
    {
        TSharedPtr<FJsonObject> UsageObject = MakeShared<FJsonObject>();
        UsageObject->SetNumberField(TEXT("input_tokens"), ManifestResponse.Usage.InputTokens);
        UsageObject->SetNumberField(TEXT("output_tokens"), ManifestResponse.Usage.OutputTokens);
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

    // Determine if we're in batch mode (when CurrentBatchRootPath is set)
    const bool bIsBatchMode = !CurrentBatchRootPath.IsEmpty();

    if (bIsBatchMode)
    {
        // Full-Blueprint C++ output is emitted once in EndBatchTranslation after every graph
        // response has been parsed. Do not create intermediate per-graph .h/.cpp directories.
        // Preserve the existing behavior for non-C++ targets.
        if (TargetLanguage != EN2CCodeLanguage::Cpp)
        {
            SaveGraphFilesWithBatchFeatures(Response, RootPath, TargetLanguage);
        }
    }
    else
    {
        SaveGraphFilesOriginal(Response, RootPath, TargetLanguage);
    }

    FN2CLogger::Get().Log(FString::Printf(TEXT("Translation saved to: %s"), *RootPath), EN2CLogSeverity::Info);
    return true;
}

void UN2CLLMModule::SaveRawResponseToDisk(const FString& RawResponse)
{
    if (RawResponse.IsEmpty())
    {
        return;
    }

    FString RootPath;
    if (!CurrentBatchRootPath.IsEmpty())
    {
        RootPath = CurrentBatchRootPath;
    }
    else
    {
        RootPath = GenerateTranslationRootPath(TEXT("ParseFailure"));
    }

    if (!EnsureDirectoryExists(RootPath))
    {
        return;
    }

    // Save the raw response with a unique timestamp
    FDateTime Now = FDateTime::Now();
    FString Timestamp = Now.ToString(TEXT("%Y-%m-%d-%H.%M.%S.%f"));
    FString RawFileName = FString::Printf(TEXT("RAW_RESPONSE_%s.txt"), *Timestamp);
    FString RawFilePath = FPaths::Combine(RootPath, RawFileName);

    if (FFileHelper::SaveStringToFile(RawResponse, *RawFilePath))
    {
        FN2CLogger::Get().LogWarning(
            FString::Printf(TEXT("Raw LLM response saved for debugging: %s"), *RawFilePath),
            TEXT("LLMModule")
        );
    }
    else
    {
        FN2CLogger::Get().LogError(
            FString::Printf(TEXT("Failed to save raw response to: %s"), *RawFilePath),
            TEXT("LLMModule")
        );
    }
}

void UN2CLLMModule::SaveGraphFilesWithBatchFeatures(
    const FN2CTranslationResponse& Response,
    const FString& RootPath,
    EN2CCodeLanguage TargetLanguage) const
{
    const bool bIsCpp = (TargetLanguage == EN2CCodeLanguage::Cpp);
    
    // Helper to sanitize graph names for use as filesystem paths while keeping the
    // original GraphName intact for logical/JSON purposes.
    auto SanitizeNameForFilesystem = [](const FString& InName) -> FString
    {
        FString Result = InName;
        Result = Result.TrimStartAndEnd();

        // Replace Windows-invalid filename characters with underscores
        const TCHAR InvalidChars[] =
        {
            TEXT('<'), TEXT('>'), TEXT(':'), TEXT('"'),
            TEXT('/'), TEXT('\\'), TEXT('|'), TEXT('?'), TEXT('*')
        };

        for (TCHAR Ch : InvalidChars)
        {
            FString From;
            From.AppendChar(Ch);
            Result.ReplaceInline(*From, TEXT("_"), ESearchCase::CaseSensitive);
        }

        return Result;
    };

    // Save each graph's files
    for (const FN2CGraphTranslation& Graph : Response.Graphs)
    {
        // Skip graphs with empty names
        if (Graph.GraphName.IsEmpty())
        {
            continue;
        }

        const FString SanitizedGraphName = SanitizeNameForFilesystem(Graph.GraphName);
        const bool bIsClassItSelf = Graph.GraphType.Equals(TEXT("ClassItSelf"), ESearchCase::IgnoreCase);
        const bool bHasGraphClass = !Graph.GraphClass.IsEmpty();

        // Log graph information for debugging
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("[SaveGraphFiles] Processing graph: Name='%s', Type='%s', Class='%s', IsClassItSelf=%d, HasGraphClass=%d"),
                *Graph.GraphName, *Graph.GraphType, *Graph.GraphClass, bIsClassItSelf ? 1 : 0, bHasGraphClass ? 1 : 0),
            EN2CLogSeverity::Debug);

        // For ClassItSelf graphs with a class name, save directly to class-centric directory
        // Skip the graph-specific directory to avoid duplicate files
        if (bIsClassItSelf && bHasGraphClass)
        {
            FString ClassDir = FPaths::Combine(RootPath, Graph.GraphClass);
            if (!EnsureDirectoryExists(ClassDir))
            {
                FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Failed to create class directory: %s"), *ClassDir));
                continue;
            }

            // Save declaration file (C++ only)
            if (bIsCpp && !Graph.Code.GraphDeclaration.IsEmpty())
            {
                FString ClassHeaderPath = FPaths::Combine(ClassDir, Graph.GraphClass + TEXT(".h"));
                FN2CLogger::Get().Log(
                    FString::Printf(TEXT("[SaveGraphFiles] Saving ClassItSelf header to class-centric path: %s (Graph: %s)"),
                        *ClassHeaderPath, *Graph.GraphName),
                    EN2CLogSeverity::Debug);
                if (!FFileHelper::SaveStringToFile(Graph.Code.GraphDeclaration, *ClassHeaderPath))
                {
                    FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Failed to save class header file: %s"), *ClassHeaderPath));
                }
                else
                {
                    FN2CLogger::Get().Log(
                        FString::Printf(TEXT("[SaveGraphFiles] Successfully saved class header file: %s"), *ClassHeaderPath),
                        EN2CLogSeverity::Debug);
                }
            }

            // Save implementation file
            if (!Graph.Code.GraphImplementation.IsEmpty())
            {
                FString Extension = GetFileExtensionForLanguage(TargetLanguage);
                FString ClassImplPath = FPaths::Combine(ClassDir, Graph.GraphClass + Extension);
                FN2CLogger::Get().Log(
                    FString::Printf(TEXT("[SaveGraphFiles] Saving ClassItSelf implementation to class-centric path: %s (Graph: %s)"),
                        *ClassImplPath, *Graph.GraphName),
                    EN2CLogSeverity::Debug);
                if (!FFileHelper::SaveStringToFile(Graph.Code.GraphImplementation, *ClassImplPath))
                {
                    FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Failed to save class implementation file: %s"), *ClassImplPath));
                }
                else
                {
                    FN2CLogger::Get().Log(
                        FString::Printf(TEXT("[SaveGraphFiles] Successfully saved class implementation file: %s"), *ClassImplPath),
                        EN2CLogSeverity::Debug);
                }
            }

            // Save implementation notes to class directory
            if (!Graph.Code.ImplementationNotes.IsEmpty())
            {
                FString NotesPath = FPaths::Combine(ClassDir, Graph.GraphClass + TEXT("_Notes.txt"));
                if (!FFileHelper::SaveStringToFile(Graph.Code.ImplementationNotes, *NotesPath))
                {
                    FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Failed to save notes file: %s"), *NotesPath));
                }
            }

            // Skip normal graph directory processing for ClassItSelf graphs
            continue;
        }

        // For non-ClassItSelf graphs (or ClassItSelf without class name), use normal graph directory
        FString GraphDir = FPaths::Combine(RootPath, SanitizedGraphName);
        if (!EnsureDirectoryExists(GraphDir))
        {
            FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Failed to create graph directory: %s"), *GraphDir));
            continue;
        }

        const FString FileBaseName = SanitizedGraphName;

        // Save declaration file (C++ only)
        if (bIsCpp && !Graph.Code.GraphDeclaration.IsEmpty())
        {
            FString HeaderPath = FPaths::Combine(GraphDir, FileBaseName + TEXT(".h"));
            FN2CLogger::Get().Log(
                FString::Printf(TEXT("[SaveGraphFiles] Saving header file: %s (Graph: %s)"), *HeaderPath, *Graph.GraphName),
                EN2CLogSeverity::Debug);
            if (!FFileHelper::SaveStringToFile(Graph.Code.GraphDeclaration, *HeaderPath))
            {
                FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Failed to save header file: %s"), *HeaderPath));
            }
            else
            {
                FN2CLogger::Get().Log(
                    FString::Printf(TEXT("[SaveGraphFiles] Successfully saved header file: %s"), *HeaderPath),
                    EN2CLogSeverity::Debug);
            }
        }

        // Save implementation file with appropriate extension
        if (!Graph.Code.GraphImplementation.IsEmpty())
        {
            FString Extension = GetFileExtensionForLanguage(TargetLanguage);
            FString ImplPath = FPaths::Combine(GraphDir, FileBaseName + Extension);
            FN2CLogger::Get().Log(
                FString::Printf(TEXT("[SaveGraphFiles] Saving implementation file: %s (Graph: %s)"), *ImplPath, *Graph.GraphName),
                EN2CLogSeverity::Debug);
            if (!FFileHelper::SaveStringToFile(Graph.Code.GraphImplementation, *ImplPath))
            {
                FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Failed to save implementation file: %s"), *ImplPath));
            }
            else
            {
                FN2CLogger::Get().Log(
                    FString::Printf(TEXT("[SaveGraphFiles] Successfully saved implementation file: %s"), *ImplPath),
                    EN2CLogSeverity::Debug);
            }
        }
        
        // Save implementation notes
        if (!Graph.Code.ImplementationNotes.IsEmpty())
        {
            FString NotesPath = FPaths::Combine(GraphDir, FileBaseName + TEXT("_Notes.txt"));
            if (!FFileHelper::SaveStringToFile(Graph.Code.ImplementationNotes, *NotesPath))
            {
                FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Failed to save notes file: %s"), *NotesPath));
            }
        }
    }
}

void UN2CLLMModule::SaveGraphFilesOriginal(
    const FN2CTranslationResponse& Response,
    const FString& RootPath,
    EN2CCodeLanguage TargetLanguage) const
{
    // Save each graph's files using original simple logic
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
}

FString UN2CLLMModule::GenerateTranslationRootPath(const FString& BlueprintName) const
{
    // Get current date/time
    FDateTime Now = FDateTime::Now();
    FString Timestamp = Now.ToString(TEXT("%Y-%m-%d-%H.%M.%S"));
    
    // Create folder name
    FString FolderName = FString::Printf(TEXT("%s_%s"), *BlueprintName, *Timestamp);

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
    Registry->RegisterProvider(EN2CLLMProvider::MiniMax, UN2CMiniMaxService::StaticClass());
    
    FN2CLogger::Get().Log(TEXT("Provider registry initialized"), EN2CLogSeverity::Info, TEXT("LLMModule"));
}
