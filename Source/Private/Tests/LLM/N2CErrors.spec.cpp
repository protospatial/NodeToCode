// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CEditorIntegration.h"
#include "Core/N2CSettings.h"
#include "Core/Widgets/SN2CBatchProgressModal.h"
#include "Dom/JsonObject.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HttpManager.h"
#include "HttpModule.h"
#include "HttpPath.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "K2Node_CallFunction.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "LLM/N2CBatchTranslationOrchestrator.h"
#include "LLM/N2CLLMModule.h"
#include "LLM/Providers/N2CAnthropicResponseParser.h"
#include "LLM/Providers/N2CDeepSeekResponseParser.h"
#include "LLM/Providers/N2CGeminiResponseParser.h"
#include "LLM/Providers/N2CLMStudioResponseParser.h"
#include "LLM/Providers/N2COllamaResponseParser.h"
#include "LLM/Providers/N2COpenAIResponseParser.h"
#include "MCP/Tools/Implementations/Translation/N2CTranslateBlueprintAsyncTask.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Models/N2CTranslation.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/**
 * A provider error reaches the user as a typed error: the HTTP handler keeps the status and wraps a non-JSON body as
 * valid JSON, the parsers classify it (kind, provider message, retryable), and the MCP translate result, the module's
 * broadcast, batch items with BatchSummary.json and the progress modal carry it instead of "Failed to parse" or
 * "Translation failed".
 */
BEGIN_DEFINE_SPEC(FN2CErrorsSpec, "NodeToCode.LLM.Errors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	/** A Cloudflare-style 502 page: attributes with double quotes, newlines, a backslash */
	static FString Html502()
	{
		return TEXT("<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<title>502 Bad Gateway</title>\n")
			TEXT("<style>body { font-family: \"Segoe UI\", sans-serif; }</style></head>\n")
			TEXT("<body class=\"error\">\n<h1>Bad gateway</h1>\n<p>The origin said \"no\" \\ try again.</p>\n</body></html>\n");
	}

	/** Anthropic's 429 body */
	static FString AnthropicRateLimit()
	{
		return TEXT("{\"type\":\"error\",\"error\":{\"type\":\"rate_limit_error\",")
			TEXT("\"message\":\"Number of request tokens has exceeded your per-minute rate limit\"},\"request_id\":\"req_011N2C\"}");
	}

	static TSharedPtr<FJsonObject> ParseObject(const FString& Json)
	{
		TSharedPtr<FJsonObject> Object;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, Object))
		{
			return nullptr;
		}
		return Object;
	}

	/** Every provider parser, as the services create them */
	static TArray<TPair<FString, UN2CResponseParserBase*>> AllParsers()
	{
		TArray<TPair<FString, UN2CResponseParserBase*>> Parsers;
		Parsers.Emplace(TEXT("Anthropic"), NewObject<UN2CAnthropicResponseParser>(GetTransientPackage()));
		Parsers.Emplace(TEXT("OpenAI"), NewObject<UN2COpenAIResponseParser>(GetTransientPackage()));
		Parsers.Emplace(TEXT("Gemini"), NewObject<UN2CGeminiResponseParser>(GetTransientPackage()));
		Parsers.Emplace(TEXT("DeepSeek"), NewObject<UN2CDeepSeekResponseParser>(GetTransientPackage()));
		Parsers.Emplace(TEXT("LMStudio"), NewObject<UN2CLMStudioResponseParser>(GetTransientPackage()));
		Parsers.Emplace(TEXT("Ollama"), NewObject<UN2COllamaResponseParser>(GetTransientPackage()));
		return Parsers;
	}

	/** Runs an HTTP error (status and body) through the handler's wrapping and Parser; returns the parsed response */
	FN2CTranslationResponse ThroughHandlerAndParser(UN2CResponseParserBase* Parser, int32 Status, const FString& Body,
		const FString& What)
	{
		const FString Wrapped = FN2CTranslationError::WrapHttpErrorBody(Status, Body);
		const TSharedPtr<FJsonObject> Root = ParseObject(Wrapped);
		TestTrue(What + TEXT(": the handler's error payload is valid JSON"), Root.IsValid());
		if (Root.IsValid())
		{
			TestTrue(What + TEXT(": it has a top-level error"), Root->HasField(TEXT("error")));
			int32 WrappedStatus = 0;
			TestTrue(What + TEXT(": it carries the HTTP status"), Root->TryGetNumberField(TEXT("http_status"), WrappedStatus)
				&& WrappedStatus == Status);
		}

		FN2CTranslationResponse Response;
		TestFalse(What + TEXT(": the parser reports failure"), Parser->ParseLLMResponse(Wrapped, Response));
		return Response;
	}

	/**
	 * A local HTTP endpoint that answers every POST to /api/chat with a fixed status and body, so a real request
	 * through the HTTP handler gets a real provider-style error. Ticked by Pump.
	 */
	struct FStubProvider
	{
		uint32 Port = 0;
		TSharedPtr<IHttpRouter> Router;
		FHttpRouteHandle Route;
		TSharedRef<int32> Status = MakeShared<int32>(502);
		TSharedRef<FString> Body = MakeShared<FString>();
		TSharedRef<FString> ContentType = MakeShared<FString>(TEXT("text/html"));
		TSharedRef<int32> Hits = MakeShared<int32>(0);

		bool Start()
		{
			FHttpServerModule& Server = FHttpServerModule::Get();
			*Hits = 0;
			for (int32 Attempt = 0; Attempt < 20 && !Router.IsValid(); ++Attempt)
			{
				Port = 30000 + FMath::RandRange(0, 9999);
				Router = Server.GetHttpRouter(Port, /*bFailOnBindFailure=*/true);
			}
			if (!Router.IsValid())
			{
				return false;
			}
			TSharedRef<int32> StatusRef = Status;
			TSharedRef<FString> BodyRef = Body;
			TSharedRef<FString> TypeRef = ContentType;
			TSharedRef<int32> HitsRef = Hits;
			Route = Router->BindRoute(FHttpPath(TEXT("/api/chat")), EHttpServerRequestVerbs::VERB_POST,
				FHttpRequestHandler::CreateLambda([StatusRef, BodyRef, TypeRef, HitsRef](const FHttpServerRequest&, const FHttpResultCallback& OnComplete)
				{
					++(*HitsRef);
					TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(*BodyRef, *TypeRef);
					Response->Code = static_cast<EHttpServerResponseCodes>(*StatusRef);
					OnComplete(MoveTemp(Response));
					return true;
				}));
			Server.StartAllListeners();
			return Route.IsValid();
		}

		void Stop()
		{
			if (Router.IsValid() && Route.IsValid())
			{
				Router->UnbindRoute(Route);
			}
			Route.Reset();
		}

		FString Endpoint() const
		{
			return FString::Printf(TEXT("http://127.0.0.1:%u"), Port);
		}

		void Answer(int32 InStatus, const FString& InBody, const FString& InContentType)
		{
			*Status = InStatus;
			*Body = InBody;
			*ContentType = InContentType;
		}
	};

	/** Ticks the HTTP server, the HTTP client and the given ticks until Seconds pass or Until is true */
	static void Pump(double Seconds, TFunctionRef<bool()> Until, TFunction<void(float)> ExtraTick = nullptr)
	{
		FHttpManager& Manager = FHttpModule::Get().GetHttpManager();
		FTSTickerObjectBase& ServerTicker = FHttpServerModule::Get();
		const double End = FPlatformTime::Seconds() + Seconds;
		double Last = FPlatformTime::Seconds();
		while (FPlatformTime::Seconds() < End && !Until())
		{
			FPlatformProcess::Sleep(0.005f);
			const double Now = FPlatformTime::Seconds();
			const float Delta = static_cast<float>(Now - Last);
			Last = Now;
			ServerTicker.Tick(Delta);
			Manager.Tick(Delta);
			if (ExtraTick)
			{
				ExtraTick(Delta);
			}
		}
	}

	/** Ollama (no key needed) pointed at the stub for the life of the scope */
	struct FScopedOllamaAt
	{
		explicit FScopedOllamaAt(const FString& Endpoint)
		{
			UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
			SavedEndpoint = Settings->OllamaConfig.OllamaEndpoint;
			SavedProvider = Settings->Provider;
			Settings->OllamaConfig.OllamaEndpoint = Endpoint;
			Settings->Provider = EN2CLLMProvider::Ollama;
		}
		~FScopedOllamaAt()
		{
			UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
			Settings->OllamaConfig.OllamaEndpoint = SavedEndpoint;
			Settings->Provider = SavedProvider;
		}
		FString SavedEndpoint;
		EN2CLLMProvider SavedProvider;
	};

	FStubProvider Stub;

END_DEFINE_SPEC(FN2CErrorsSpec)

void FN2CErrorsSpec::Define()
{
	Describe(TEXT("HandlerAndParser"), [this]()
	{
		It(TEXT("turns an HTML 502 page with quotes and newlines into valid JSON and a Network error in every parser"), [this]()
		{
			// Each parser logs the error it found
			AddExpectedError(TEXT("failed with HTTP 502"), EAutomationExpectedErrorFlags::Contains, 0);
			for (const TPair<FString, UN2CResponseParserBase*>& Parser : AllParsers())
			{
				const FN2CTranslationResponse Response = ThroughHandlerAndParser(Parser.Value, 502, Html502(), Parser.Key);
				const FN2CTranslationError& Error = Response.Error;
				TestEqual(Parser.Key + TEXT(": kind"), Error.Kind, EN2CTranslationErrorKind::Network);
				TestEqual(Parser.Key + TEXT(": HTTP status"), Error.HttpStatus, 502);
				TestEqual(Parser.Key + TEXT(": message is the page title, not raw HTML"), Error.Message, FString(TEXT("502 Bad Gateway")));
				TestTrue(Parser.Key + TEXT(": a gateway error is retryable"), Error.bRetryable);
				TestTrue(Parser.Key + TEXT(": display names the status"), Error.ToDisplayString().Contains(TEXT("HTTP 502")));
			}
		});

		It(TEXT("keeps a plain-text body with quotes readable"), [this]()
		{
			AddExpectedError(TEXT("failed with HTTP 400"), EAutomationExpectedErrorFlags::Contains, 0);
			UN2CResponseParserBase* Parser = NewObject<UN2CLMStudioResponseParser>(GetTransientPackage());
			const FN2CTranslationResponse Response = ThroughHandlerAndParser(Parser, 400,
				TEXT("Error: \"model\" field is required\r\nsee \"docs\""), TEXT("LM Studio text"));
			TestEqual(TEXT("kind"), Response.Error.Kind, EN2CTranslationErrorKind::InvalidRequest);
			TestEqual(TEXT("message keeps the quotes, newlines collapsed"), Response.Error.Message,
				FString(TEXT("Error: \"model\" field is required see \"docs\"")));
			TestFalse(TEXT("not retryable"), Response.Error.bRetryable);
		});

		It(TEXT("classifies a JSON rate_limit_error body as RateLimited with the provider's message"), [this]()
		{
			AddExpectedError(TEXT("failed with HTTP 429"), EAutomationExpectedErrorFlags::Contains, 0);
			UN2CResponseParserBase* Parser = NewObject<UN2CAnthropicResponseParser>(GetTransientPackage());
			const FN2CTranslationResponse Response = ThroughHandlerAndParser(Parser, 429, AnthropicRateLimit(), TEXT("Anthropic 429"));
			const FN2CTranslationError& Error = Response.Error;
			TestEqual(TEXT("kind"), Error.Kind, EN2CTranslationErrorKind::RateLimited);
			TestEqual(TEXT("HTTP status"), Error.HttpStatus, 429);
			TestEqual(TEXT("provider message"), Error.Message,
				FString(TEXT("Number of request tokens has exceeded your per-minute rate limit")));
			TestEqual(TEXT("provider type"), Error.ProviderErrorType, FString(TEXT("rate_limit_error")));
			TestEqual(TEXT("request id"), Error.RequestId, FString(TEXT("req_011N2C")));
			TestTrue(TEXT("retryable"), Error.bRetryable);
		});

		It(TEXT("classifies the provider errors seen live (Gemini 503 and 429, DeepSeek 402, OpenAI 401, Ollama 404)"), [this]()
		{
			AddExpectedError(TEXT("failed with HTTP"), EAutomationExpectedErrorFlags::Contains, 0);
			struct FCase
			{
				const TCHAR* Name;
				UN2CResponseParserBase* Parser;
				int32 Status;
				FString Body;
				EN2CTranslationErrorKind Kind;
				const TCHAR* Message;
				bool bRetryable;
			};
			const TArray<FCase> Cases = {
				{ TEXT("Gemini 503"), NewObject<UN2CGeminiResponseParser>(GetTransientPackage()), 503,
					TEXT("{\"error\":{\"code\":503,\"message\":\"This model is currently experiencing high demand.\",\"status\":\"UNAVAILABLE\"}}"),
					EN2CTranslationErrorKind::Overloaded, TEXT("This model is currently experiencing high demand."), true },
				{ TEXT("Gemini 429"), NewObject<UN2CGeminiResponseParser>(GetTransientPackage()), 429,
					TEXT("[{\"error\":{\"code\":429,\"message\":\"You exceeded your current quota.\",\"status\":\"RESOURCE_EXHAUSTED\"}}]"),
					EN2CTranslationErrorKind::RateLimited, TEXT("You exceeded your current quota."), true },
				{ TEXT("DeepSeek 402"), NewObject<UN2CDeepSeekResponseParser>(GetTransientPackage()), 402,
					TEXT("{\"error\":{\"message\":\"Insufficient Balance\",\"type\":\"unknown_error\",\"param\":null,\"code\":\"invalid_request_error\"}}"),
					EN2CTranslationErrorKind::Billing, TEXT("Insufficient Balance"), false },
				{ TEXT("OpenAI 401"), NewObject<UN2COpenAIResponseParser>(GetTransientPackage()), 401,
					TEXT("{\"error\":{\"message\":\"Incorrect API key provided: sk-n2c***test.\",\"type\":\"invalid_request_error\",\"param\":null,\"code\":\"invalid_api_key\"}}"),
					EN2CTranslationErrorKind::Auth, TEXT("Incorrect API key provided: sk-n2c***test."), false },
				{ TEXT("Ollama 404"), NewObject<UN2COllamaResponseParser>(GetTransientPackage()), 404,
					TEXT("{\"error\":\"model \\\"n2c-missing\\\" not found, try pulling it first\"}"),
					EN2CTranslationErrorKind::InvalidRequest, TEXT("model \"n2c-missing\" not found, try pulling it first"), false },
			};
			for (const FCase& Case : Cases)
			{
				const FN2CTranslationResponse Response = ThroughHandlerAndParser(Case.Parser, Case.Status, Case.Body, Case.Name);
				TestEqual(FString(Case.Name) + TEXT(": kind"), Response.Error.Kind, Case.Kind);
				TestEqual(FString(Case.Name) + TEXT(": HTTP status"), Response.Error.HttpStatus, Case.Status);
				TestEqual(FString(Case.Name) + TEXT(": provider message"), Response.Error.Message, FString(Case.Message));
				TestEqual(FString(Case.Name) + TEXT(": retryable"), Response.Error.bRetryable, Case.bRetryable);
			}
		});

		It(TEXT("reports an unparseable success body as a Parse error"), [this]()
		{
			AddExpectedError(TEXT("Failed to parse"), EAutomationExpectedErrorFlags::Contains, 0);
			UN2CResponseParserBase* Parser = NewObject<UN2COpenAIResponseParser>(GetTransientPackage());
			FN2CTranslationResponse Response;
			TestFalse(TEXT("parse fails"), Parser->ParseLLMResponse(TEXT("<html>not json</html>"), Response));
			TestEqual(TEXT("kind"), Response.Error.Kind, EN2CTranslationErrorKind::Parse);
		});
	});

	Describe(TEXT("ErrorJson"), [this]()
	{
		It(TEXT("round-trips through ToJsonString and FromErrorJson with escaped text"), [this]()
		{
			FN2CTranslationError Error;
			Error.Kind = EN2CTranslationErrorKind::Overloaded;
			Error.HttpStatus = 529;
			Error.Message = TEXT("Overloaded \"now\"\nretry \\ later");
			Error.Provider = TEXT("Anthropic");
			Error.Model = TEXT("claude-haiku-5-5");
			Error.bRetryable = true;

			const FString Json = Error.ToJsonString();
			TestTrue(TEXT("starts with the error field"), Json.StartsWith(TEXT("{\"error\":")));
			const TSharedPtr<FJsonObject> Root = ParseObject(Json);
			if (!TestTrue(TEXT("valid JSON"), Root.IsValid()))
			{
				return;
			}
			FString Kind;
			TestTrue(TEXT("error_kind"), Root->TryGetStringField(TEXT("error_kind"), Kind) && Kind == TEXT("Overloaded"));

			const FN2CTranslationError Back = FN2CTranslationError::FromErrorJson(Root);
			TestEqual(TEXT("kind"), Back.Kind, Error.Kind);
			TestEqual(TEXT("status"), Back.HttpStatus, Error.HttpStatus);
			TestEqual(TEXT("message"), Back.Message, Error.Message);
			TestEqual(TEXT("provider"), Back.Provider, Error.Provider);
			TestEqual(TEXT("model"), Back.Model, Error.Model);
			TestTrue(TEXT("retryable"), Back.bRetryable);
		});
	});

	Describe(TEXT("EndToEnd"), [this]()
	{
		BeforeEach([this]()
		{
			TestTrue(TEXT("stub provider listening"), Stub.Start());
		});

		AfterEach([this]()
		{
			Stub.Stop();
		});

		It(TEXT("puts the provider's status and message in the MCP translate result"), [this]()
		{
			AddExpectedError(TEXT("HTTP 502 error"), EAutomationExpectedErrorFlags::Contains, 0);
			AddExpectedError(TEXT("HTTP 429 error"), EAutomationExpectedErrorFlags::Contains, 0);
			AddExpectedError(TEXT("failed with HTTP"), EAutomationExpectedErrorFlags::Contains, 0);
			FScopedOllamaAt ScopedOllama(Stub.Endpoint());

			struct FObserved
			{
				int32 Completions = 0;
				bool bIsError = false;
				FString Text;
			};

			auto RunTask = [this](const FString& What) -> FObserved
			{
				const TSharedRef<FObserved> Observed = MakeShared<FObserved>();
				TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
				Arguments->SetStringField(TEXT("provider"), TEXT("Ollama"));
				Arguments->SetStringField(TEXT("model"), TEXT("n2c-errors-test"));
				Arguments->SetStringField(TEXT("language"), TEXT("Cpp"));
				TSharedRef<FN2CTranslateBlueprintAsyncTask, ESPMode::ThreadSafe> Task =
					MakeShared<FN2CTranslateBlueprintAsyncTask, ESPMode::ThreadSafe>(FGuid::NewGuid(), FGuid::NewGuid().ToString(), Arguments);
				Task->SetRequestStarter([](const FString& ProviderId, const FString& ModelId, const FString& LanguageId,
					const FOnLLMResponseReceived& OnComplete)
				{
					return FN2CEditorIntegration::Get().TranslateJsonAsync(TEXT("{}"), ProviderId, ModelId, LanguageId, OnComplete);
				});
				Task->SetCompleteDelegate(FN2CAsyncTaskCompleteDelegate::CreateLambda([Observed](const FMcpToolCallResult& Result)
				{
					++Observed->Completions;
					Observed->bIsError = Result.bIsError;
					if (Result.Content.Num() > 0 && Result.Content[0].IsValid())
					{
						Result.Content[0]->TryGetStringField(TEXT("text"), Observed->Text);
					}
				}));
				Task->Execute();
				Pump(15.0, [Observed] { return Observed->Completions > 0; },
					[Task](float Delta) { Task->Tick(Delta); });
				TestEqual(What + TEXT(": the task reported once"), Observed->Completions, 1);
				return *Observed;
			};

			// 1. A proxy's HTML 502
			Stub.Answer(502, Html502(), TEXT("text/html"));
			{
				const FObserved Observed = RunTask(TEXT("502"));
				TestTrue(TEXT("502: isError"), Observed.bIsError);
				TestFalse(TEXT("502: no 'Failed to parse'"), Observed.Text.Contains(TEXT("Failed to parse")));
				const TSharedPtr<FJsonObject> Root = ParseObject(Observed.Text);
				if (TestTrue(TEXT("502: the result text is valid JSON"), Root.IsValid()))
				{
					FString Kind, Message, Display, Provider;
					int32 Status = 0;
					bool bRetryable = false;
					TestTrue(TEXT("502: error_kind Network"), Root->TryGetStringField(TEXT("error_kind"), Kind) && Kind == TEXT("Network"));
					TestTrue(TEXT("502: http_status 502"), Root->TryGetNumberField(TEXT("http_status"), Status) && Status == 502);
					TestTrue(TEXT("502: retryable"), Root->TryGetBoolField(TEXT("retryable"), bRetryable) && bRetryable);
					TestTrue(TEXT("502: message from the page"), Root->TryGetStringField(TEXT("message"), Message) && Message == TEXT("502 Bad Gateway"));
					TestTrue(TEXT("502: provider named"), Root->TryGetStringField(TEXT("provider"), Provider) && Provider == TEXT("Ollama"));
					TestTrue(TEXT("502: readable error line"), Root->TryGetStringField(TEXT("error"), Display)
						&& Display.Contains(TEXT("Ollama request failed with HTTP 502 (Network, retryable)")));
				}
			}

			// 2. A JSON rate limit
			Stub.Answer(429, AnthropicRateLimit(), TEXT("application/json"));
			{
				const FObserved Observed = RunTask(TEXT("429"));
				TestTrue(TEXT("429: isError"), Observed.bIsError);
				const TSharedPtr<FJsonObject> Root = ParseObject(Observed.Text);
				if (TestTrue(TEXT("429: the result text is valid JSON"), Root.IsValid()))
				{
					const FN2CTranslationError Error = FN2CTranslationError::FromErrorJson(Root);
					TestEqual(TEXT("429: kind"), Error.Kind, EN2CTranslationErrorKind::RateLimited);
					TestEqual(TEXT("429: status"), Error.HttpStatus, 429);
					TestEqual(TEXT("429: provider message"), Error.Message,
						FString(TEXT("Number of request tokens has exceeded your per-minute rate limit")));
				}
			}
			TestEqual(TEXT("the stub answered both requests"), *Stub.Hits, 2);
		});

		It(TEXT("records the typed error on batch items and in BatchSummary.json"), [this]()
		{
			AddExpectedError(TEXT("HTTP 503 error"), EAutomationExpectedErrorFlags::Contains, 0);
			AddExpectedError(TEXT("failed with HTTP 503"), EAutomationExpectedErrorFlags::Contains, 0);
			FScopedOllamaAt ScopedOllama(Stub.Endpoint());
			Stub.Answer(503, TEXT("{\"error\":\"server busy, \\\"retry\\\" later\"}"), TEXT("application/json"));

			UN2CLLMModule* Module = UN2CLLMModule::Get();
			if (!TestTrue(TEXT("module initializes with Ollama"), Module->Initialize()))
			{
				return;
			}

			// A transient Actor Blueprint with one call node in its event graph, tagged like the batch UI does
			UPackage* Package = CreatePackage(*FString::Printf(TEXT("/Temp/N2CErrorsSpec_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
			TStrongObjectPtr<UBlueprint> Blueprint(FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), Package,
				TEXT("BP_N2CErrorsSpec"), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()));
			if (!TestTrue(TEXT("blueprint created"), Blueprint.IsValid() && Blueprint->UbergraphPages.Num() > 0))
			{
				return;
			}
			UEdGraph* Graph = Blueprint->UbergraphPages[0];
			UK2Node_CallFunction* Call = NewObject<UK2Node_CallFunction>(Graph);
			Call->FunctionReference.SetExternalMember(GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString), UKismetSystemLibrary::StaticClass());
			Graph->AddNode(Call, false, false);
			Call->CreateNewGuid();
			Call->PostPlacedNewNode();
			Call->AllocateDefaultPins();

			FN2CTagInfo Tag;
			Tag.Tag = TEXT("N2CErrorsSpec");
			Tag.GraphGuid = Graph->GraphGuid.ToString();
			Tag.GraphName = Graph->GetName();
			Tag.BlueprintPath = Blueprint->GetPathName();

			UN2CBatchTranslationOrchestrator& Orchestrator = UN2CBatchTranslationOrchestrator::Get();
			TSharedRef<FN2CTranslationResponse> ItemResponse = MakeShared<FN2CTranslationResponse>();
			TSharedRef<bool> bItemSuccess = MakeShared<bool>(true);
			TSharedRef<FN2CBatchTranslationResult> BatchResult = MakeShared<FN2CBatchTranslationResult>();
			TSharedRef<bool> bBatchDone = MakeShared<bool>(false);
			const FDelegateHandle ItemHandle = Orchestrator.OnItemCompleteNative.AddLambda(
				[ItemResponse, bItemSuccess](const FN2CTagInfo&, const FN2CTranslationResponse& Response, bool bSuccess, int32, int32)
				{
					*ItemResponse = Response;
					*bItemSuccess = bSuccess;
				});
			const FDelegateHandle BatchHandle = Orchestrator.OnBatchCompleteNative.AddLambda(
				[BatchResult, bBatchDone](const FN2CBatchTranslationResult& Result)
				{
					*BatchResult = Result;
					*bBatchDone = true;
				});

			const bool bStarted = Orchestrator.StartBatchTranslation({ Tag });
			if (TestTrue(TEXT("batch started"), bStarted))
			{
				Pump(15.0, [bBatchDone] { return *bBatchDone; });
			}
			Orchestrator.OnItemCompleteNative.Remove(ItemHandle);
			Orchestrator.OnBatchCompleteNative.Remove(BatchHandle);

			if (TestTrue(TEXT("batch completed"), *bBatchDone))
			{
				TestFalse(TEXT("item failed"), *bItemSuccess);
				TestEqual(TEXT("item error kind"), ItemResponse->Error.Kind, EN2CTranslationErrorKind::Overloaded);
				TestEqual(TEXT("item HTTP status"), ItemResponse->Error.HttpStatus, 503);
				TestEqual(TEXT("item provider message"), ItemResponse->Error.Message, FString(TEXT("server busy, \"retry\" later")));
				TestEqual(TEXT("one failure"), BatchResult->FailureCount, 1);

				const FString SummaryPath = FPaths::Combine(BatchResult->BatchOutputPath, TEXT("BatchSummary.json"));
				FString SummaryText;
				const TSharedPtr<FJsonObject> Summary = FFileHelper::LoadFileToString(SummaryText, *SummaryPath) ? ParseObject(SummaryText) : nullptr;
				const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
				if (TestTrue(TEXT("BatchSummary.json written and valid"), Summary.IsValid())
					&& TestTrue(TEXT("summary has one item"), Summary->TryGetArrayField(TEXT("items"), Items) && Items->Num() == 1))
				{
					const TSharedPtr<FJsonObject> Item = (*Items)[0]->AsObject();
					FString ErrorText, Kind, ProviderMessage;
					int32 Status = 0;
					bool bRetryable = false;
					TestTrue(TEXT("summary error is specific"), Item->TryGetStringField(TEXT("error"), ErrorText)
						&& ErrorText.Contains(TEXT("HTTP 503")) && ErrorText != TEXT("Translation failed"));
					TestTrue(TEXT("summary errorKind"), Item->TryGetStringField(TEXT("errorKind"), Kind) && Kind == TEXT("Overloaded"));
					TestTrue(TEXT("summary httpStatus"), Item->TryGetNumberField(TEXT("httpStatus"), Status) && Status == 503);
					TestTrue(TEXT("summary retryable"), Item->TryGetBoolField(TEXT("retryable"), bRetryable) && bRetryable);
					TestTrue(TEXT("summary providerMessage"), Item->TryGetStringField(TEXT("providerMessage"), ProviderMessage)
						&& ProviderMessage == TEXT("server busy, \"retry\" later"));
				}
				if (!BatchResult->BatchOutputPath.IsEmpty())
				{
					IFileManager::Get().DeleteDirectory(*BatchResult->BatchOutputPath, false, true);
				}
			}
			Blueprint->MarkAsGarbage();
		});
	});

	Describe(TEXT("ProgressModal"), [this]()
	{
		It(TEXT("keeps a failed item's error message for display"), [this]()
		{
			if (!FSlateApplication::IsInitialized())
			{
				AddInfo(TEXT("Slate is not initialized; skipping"));
				return;
			}
			FN2CTranslationError Error;
			Error.Kind = EN2CTranslationErrorKind::RateLimited;
			Error.HttpStatus = 429;
			Error.Message = TEXT("You exceeded your current quota.");
			Error.Provider = TEXT("Gemini");
			Error.bRetryable = true;

			TSharedRef<SN2CBatchProgressModal> Modal = SNew(SN2CBatchProgressModal);
			Modal->Initialize({ TEXT("GraphA"), TEXT("GraphB") });
			Modal->MarkItemComplete(TEXT("GraphA"), false, Error.ToDisplayString());
			Modal->MarkItemComplete(TEXT("GraphB"), true, TEXT("ignored on success"));

			TestEqual(TEXT("failed item keeps the error"), Modal->GetItemErrorMessage(TEXT("GraphA")),
				FString(TEXT("Gemini request failed with HTTP 429 (RateLimited, retryable): You exceeded your current quota.")));
			TestTrue(TEXT("successful item has none"), Modal->GetItemErrorMessage(TEXT("GraphB")).IsEmpty());
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
