// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CSettings.h"
#include "Dom/JsonObject.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HttpManager.h"
#include "HttpModule.h"
#include "HttpPath.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "LLM/N2CBaseLLMService.h"
#include "LLM/N2CResponseParserBase.h"
#include "LLM/Providers/N2CAnthropicService.h"
#include "LLM/Providers/N2CDeepSeekService.h"
#include "LLM/Providers/N2CGeminiService.h"
#include "LLM/Providers/N2CLMStudioService.h"
#include "LLM/Providers/N2COpenAIService.h"
#include "Misc/OutputDevice.h"
#include "Misc/ScopeExit.h"
#include "Models/N2CTranslation.h"
#include "Serialization/JsonSerializer.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonWriter.h"
#include "Tests/N2CTestHelpers.h"
#include "UObject/Package.h"
#include "Utils/N2CLogger.h"

/**
 * API key hygiene (card 0159). Every key-taking provider sends a real request to a local stub with a fake key that
 * carries a recognisable marker: the request URL has no key and no 'key=' parameter, and the key arrives in exactly
 * one header (Anthropic x-api-key, Gemini x-goog-api-key, OpenAI, DeepSeek and LM Studio Authorization). With Min
 * Severity at Debug, LogNodeToCode at VeryVerbose and n2c.LogLLMPayloads at its default 0, nothing the run logs holds
 * the key, the prompts, the N2C JSON sent or the model's answer; with n2c.LogLLMPayloads 1 the payloads are logged
 * and the key still is not. An error body that echoes the key is redacted before it is logged or reported, and a
 * Gemini request to an unreachable host logs no key either.
 */
BEGIN_DEFINE_SPEC(FN2CKeyHygieneSpec, "NodeToCode.LLM.KeyHygiene", N2C_TEST_FLAGS)

	/** A fake key with a marker no real key or log line contains */
	static constexpr const TCHAR* FakeKey = TEXT("n2cFAKEKEY0159marker7d41c9e2b8a6");
	static constexpr const TCHAR* GraphMarker = TEXT("N2C_GRAPH_MARKER_0159");
	static constexpr const TCHAR* SystemMarker = TEXT("N2C_SYSTEM_MARKER_0159");
	static constexpr const TCHAR* AnswerMarker = TEXT("N2C_ANSWER_MARKER_0159");

	/** Everything said through GLog while it lives, every category and verbosity */
	struct FLogCapture : public FOutputDevice
	{
		TArray<FString> Lines;
		FLogCapture() { GLog->AddOutputDevice(this); }
		virtual ~FLogCapture() override { GLog->RemoveOutputDevice(this); }
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type, const FName& Category) override
		{
			Lines.Add(FString::Printf(TEXT("%s: %s"), *Category.ToString(), V));
		}
		TArray<FString> Containing(const FString& Text, ESearchCase::Type Case = ESearchCase::CaseSensitive) const
		{
			GLog->Flush();
			return Lines.FilterByPredicate([&Text, Case](const FString& Line) { return Line.Contains(Text, Case); });
		}
	};

	/** Min Severity Debug (LogNodeToCode VeryVerbose) and n2c.LogLLMPayloads set for one case, put back afterwards */
	struct FScopedVerbosity
	{
		EN2CLogSeverity SavedSeverity;
		bool bSavedPayloads = false;
		IConsoleVariable* Variable = nullptr;

		explicit FScopedVerbosity(bool bLogPayloads)
		{
			SavedSeverity = FN2CLogger::Get().GetMinSeverity();
			FN2CLogger::Get().SetMinSeverity(EN2CLogSeverity::Debug);
			Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("n2c.LogLLMPayloads"));
			if (Variable)
			{
				bSavedPayloads = Variable->GetBool();
				Variable->Set(bLogPayloads, ECVF_SetByConsole);
			}
		}
		~FScopedVerbosity()
		{
			if (Variable)
			{
				Variable->Set(bSavedPayloads, ECVF_SetByConsole);
			}
			FN2CLogger::Get().SetMinSeverity(SavedSeverity);
		}
	};

	/** One request the stub received: its path, query parameters and headers (names as sent) */
	struct FSeenRequest
	{
		FString Route;
		FString RelativePath;
		TMap<FString, FString> Query;
		TArray<TPair<FString, FString>> Headers;
	};

	static FString ToJson(const TSharedRef<FJsonObject>& Object)
	{
		FString Out;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Object, Writer);
		return Out;
	}

	/** The model's answer: one translated graph whose implementation carries AnswerMarker */
	static FString AnswerText()
	{
		const TSharedRef<FJsonObject> Code = MakeShared<FJsonObject>();
		Code->SetStringField(TEXT("graphDeclaration"), TEXT(""));
		Code->SetStringField(TEXT("graphImplementation"), FString::Printf(TEXT("// %s"), AnswerMarker));
		Code->SetStringField(TEXT("implementationNotes"), TEXT(""));
		const TSharedRef<FJsonObject> Graph = MakeShared<FJsonObject>();
		Graph->SetStringField(TEXT("graph_name"), TEXT("EventGraph"));
		Graph->SetStringField(TEXT("graph_type"), TEXT("EventGraph"));
		Graph->SetStringField(TEXT("graph_class"), TEXT("BP_KeyHygiene"));
		Graph->SetObjectField(TEXT("code"), Code);
		const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetArrayField(TEXT("graphs"), { MakeShared<FJsonValueObject>(Graph) });
		return ToJson(Root);
	}

	/** A provider-shaped success body for Route (anthropic, gemini, or an OpenAI-compatible chat completion) */
	static FString ReplyFor(const FString& Route)
	{
		if (Route == TEXT("anthropic"))
		{
			return FString::Printf(TEXT("{\"id\":\"msg_n2c\",\"type\":\"message\",\"role\":\"assistant\",\"model\":\"n2c\",")
				TEXT("\"content\":[{\"type\":\"text\",\"text\":%s}],\"stop_reason\":\"end_turn\",")
				TEXT("\"usage\":{\"input_tokens\":10,\"output_tokens\":10}}"), *Quoted(AnswerText()));
		}
		if (Route == TEXT("gemini"))
		{
			return FString::Printf(TEXT("{\"candidates\":[{\"content\":{\"role\":\"model\",\"parts\":[{\"text\":%s}]},")
				TEXT("\"finishReason\":\"STOP\"}],\"usageMetadata\":{\"promptTokenCount\":10,\"candidatesTokenCount\":10}}"),
				*Quoted(AnswerText()));
		}
		return FString::Printf(TEXT("{\"id\":\"chatcmpl-n2c\",\"object\":\"chat.completion\",\"model\":\"n2c\",")
			TEXT("\"choices\":[{\"index\":0,\"message\":{\"role\":\"assistant\",\"content\":%s},\"finish_reason\":\"stop\"}],")
			TEXT("\"usage\":{\"prompt_tokens\":10,\"completion_tokens\":10,\"total_tokens\":20}}"), *Quoted(AnswerText()));
	}

	/** Text as a JSON string literal */
	static FString Quoted(const FString& Text)
	{
		const TSharedRef<FJsonObject> Holder = MakeShared<FJsonObject>();
		Holder->SetStringField(TEXT("v"), Text);
		const FString Json = ToJson(Holder);
		// {"v":<literal>}
		return Json.Mid(5, Json.Len() - 6);
	}

	/**
	 * A local endpoint with a route per provider under /n2c. Each records the request and answers at once; 'echo401'
	 * answers 401 with an error message that repeats every header value it received, as a misbehaving proxy might.
	 */
	struct FStub
	{
		uint32 Port = 0;
		TSharedPtr<IHttpRouter> Router;
		TArray<FHttpRouteHandle> Routes;
		TSharedRef<TArray<FSeenRequest>> Seen = MakeShared<TArray<FSeenRequest>>();

		bool Start()
		{
			FHttpServerModule& Server = FHttpServerModule::Get();
			for (int32 Attempt = 0; Attempt < 20 && !Router.IsValid(); ++Attempt)
			{
				Port = 50000 + FMath::RandRange(0, 9999);
				Router = Server.GetHttpRouter(Port, /*bFailOnBindFailure=*/true);
			}
			if (!Router.IsValid())
			{
				return false;
			}
			for (const TCHAR* Route : { TEXT("anthropic"), TEXT("openai"), TEXT("gemini"), TEXT("deepseek"), TEXT("lmstudio"), TEXT("echo401") })
			{
				Bind(Route);
			}
			Server.StartAllListeners();
			return Routes.Num() == 6 && !Routes.ContainsByPredicate([](const FHttpRouteHandle& Handle) { return !Handle.IsValid(); });
		}

		void Bind(const FString& Route)
		{
			TSharedRef<TArray<FSeenRequest>> SeenRef = Seen;
			Routes.Add(Router->BindRoute(FHttpPath(TEXT("/n2c/") + Route), EHttpServerRequestVerbs::VERB_POST,
				FHttpRequestHandler::CreateLambda([SeenRef, Route](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
				{
					FSeenRequest Record;
					Record.Route = Route;
					Record.RelativePath = Request.RelativePath.GetPath();
					Record.Query = Request.QueryParams;
					FString Echo;
					for (const TPair<FString, TArray<FString>>& Header : Request.Headers)
					{
						for (const FString& Value : Header.Value)
						{
							Record.Headers.Emplace(Header.Key, Value);
							Echo += Header.Key + TEXT("=") + Value + TEXT("; ");
						}
					}
					SeenRef->Add(MoveTemp(Record));
					if (Route == TEXT("echo401"))
					{
						const FString Body = FString::Printf(
							TEXT("{\"error\":{\"message\":%s,\"type\":\"invalid_request_error\",\"code\":\"invalid_api_key\"}}"),
							*Quoted(TEXT("Incorrect API key provided. Received headers: ") + Echo));
						TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(Body, TEXT("application/json"));
						Response->Code = EHttpServerResponseCodes::Denied;
						OnComplete(MoveTemp(Response));
						return true;
					}
					OnComplete(FHttpServerResponse::Create(ReplyFor(Route), TEXT("application/json")));
					return true;
				})));
		}

		void Stop()
		{
			for (FHttpRouteHandle& Route : Routes)
			{
				if (Router.IsValid() && Route.IsValid())
				{
					Router->UnbindRoute(Route);
				}
			}
			Routes.Reset();
		}

		FString Url(const FString& Route) const
		{
			return FString::Printf(TEXT("http://127.0.0.1:%u/n2c/%s"), Port, *Route);
		}
	};

	/** Ticks the HTTP server and client until Seconds pass or Until is true */
	static void Pump(double Seconds, TFunctionRef<bool()> Until)
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
		}
	}

	/** One provider case: the service class, the stub route its endpoint points at and the header that carries the key */
	struct FCase
	{
		EN2CLLMProvider Provider;
		const TCHAR* Model;
		const TCHAR* Route;
		const TCHAR* KeyHeader;
		bool bBearer;
	};

	static TArray<FCase> Cases()
	{
		return {
			{ EN2CLLMProvider::Anthropic, TEXT("claude-sonnet-5-5"), TEXT("anthropic"), TEXT("x-api-key"), false },
			{ EN2CLLMProvider::OpenAI, TEXT("gpt-6.1-sol"), TEXT("openai"), TEXT("Authorization"), true },
			{ EN2CLLMProvider::Gemini, TEXT("gemini-3.8-flash"), TEXT("gemini"), TEXT("x-goog-api-key"), false },
			{ EN2CLLMProvider::DeepSeek, TEXT("deepseek-flash"), TEXT("deepseek"), TEXT("Authorization"), true },
			{ EN2CLLMProvider::LMStudio, TEXT("n2c-key-hygiene"), TEXT("lmstudio"), TEXT("Authorization"), true },
		};
	}

	/** A service of Provider with the fake key, posting to Endpoint (for Gemini, the base the model path is added to) */
	static UN2CBaseLLMService* MakeService(EN2CLLMProvider Provider, const FString& Model, const FString& Endpoint)
	{
		UN2CBaseLLMService* Service = nullptr;
		switch (Provider)
		{
		case EN2CLLMProvider::Anthropic: Service = NewObject<UN2CAnthropicService>(GetTransientPackage()); break;
		case EN2CLLMProvider::OpenAI: Service = NewObject<UN2COpenAIService>(GetTransientPackage()); break;
		case EN2CLLMProvider::Gemini: Service = NewObject<UN2CGeminiService>(GetTransientPackage()); break;
		case EN2CLLMProvider::DeepSeek: Service = NewObject<UN2CDeepSeekService>(GetTransientPackage()); break;
		case EN2CLLMProvider::LMStudio: Service = NewObject<UN2CLMStudioService>(GetTransientPackage()); break;
		default: return nullptr;
		}
		FN2CLLMConfig Config;
		Config.Provider = Provider;
		Config.Model = Model;
		Config.ApiKey = FakeKey;
		Config.ApiEndpoint = Endpoint;
		Config.TimeoutSeconds = 30.0f;
		Config.ActivityTimeoutSeconds = 30.0f;
		Service->Initialize(Config);
		return Service;
	}

	/** LM Studio reads its endpoint from the settings, not the request config */
	struct FScopedLMStudioEndpoint
	{
		FString Saved;
		explicit FScopedLMStudioEndpoint(const FString& Endpoint)
		{
			UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
			Saved = Settings->LMStudioEndpoint;
			Settings->LMStudioEndpoint = Endpoint;
		}
		~FScopedLMStudioEndpoint() { GetMutableDefault<UN2CSettings>()->LMStudioEndpoint = Saved; }
	};

	/** Sends one translation request through Service and waits for its response */
	static FString SendAndWait(UN2CBaseLLMService* Service)
	{
		struct FResult { bool bDone = false; FString Response; };
		const TSharedRef<FResult> Result = MakeShared<FResult>();
		const FString UserMessage = FString::Printf(TEXT("<n2c_json>{\"graphs\":[{\"name\":\"%s\"}]}</n2c_json>"), GraphMarker);
		const FString SystemMessage = FString::Printf(TEXT("You translate Blueprint graphs. %s"), SystemMarker);
		FN2CLLMRequestHandleRef Handle = Service->SendRequest(UserMessage, SystemMessage,
			FOnLLMResponseReceived::CreateLambda([Result](const FString& Response)
			{
				Result->bDone = true;
				Result->Response = Response;
			}));
		Pump(20.0, [Result] { return Result->bDone; });
		if (!Result->bDone)
		{
			Handle->Cancel();
		}
		return Result->Response;
	}

	/** Every request in this run goes to the stub; the run's log is checked once all of them have been sent */
	void RunProviders(bool bLogPayloads, TMap<EN2CLLMProvider, FSeenRequest>& OutSeen, FLogCapture& Log)
	{
		FStub Stub;
		if (!TestTrue(TEXT("stub listening"), Stub.Start()))
		{
			return;
		}
		ON_SCOPE_EXIT { Stub.Stop(); };
		FScopedVerbosity Verbosity(bLogPayloads);
		FScopedLMStudioEndpoint LMStudio(Stub.Url(TEXT("lmstudio")));

		for (const FCase& Case : Cases())
		{
			const FString Name = UEnum::GetDisplayValueAsText(Case.Provider).ToString();
			const FString Endpoint = Case.Provider == EN2CLLMProvider::Gemini ? Stub.Url(Case.Route) + TEXT("/") : Stub.Url(Case.Route);
			UN2CBaseLLMService* Service = MakeService(Case.Provider, Case.Model, Endpoint);
			if (!TestNotNull(Name + TEXT(" service"), Service))
			{
				continue;
			}
			const int32 Before = Stub.Seen->Num();
			const FString Response = SendAndWait(Service);
			if (!TestEqual(Name + TEXT(": the stub received one request"), Stub.Seen->Num(), Before + 1))
			{
				continue;
			}
			OutSeen.Add(Case.Provider, (*Stub.Seen)[Before]);

			// The answer goes through the parser too, as the LLM module does, so its content logging is covered
			FN2CTranslationResponse Parsed;
			UN2CResponseParserBase* Parser = Service->GetResponseParser();
			if (TestNotNull(Name + TEXT(" parser"), Parser))
			{
				TestTrue(Name + TEXT(": the stub's answer parses"), Parser->ParseLLMResponse(Response, Parsed));
				TestTrue(Name + TEXT(": the answer reached the translation"), Parsed.Graphs.Num() == 1
					&& Parsed.Graphs[0].Code.GraphImplementation.Contains(AnswerMarker));
			}
		}
	}

END_DEFINE_SPEC(FN2CKeyHygieneSpec)

void FN2CKeyHygieneSpec::Define()
{
	Describe(TEXT("Requests"), [this]()
	{
		It(TEXT("carry the key in exactly one header and never in the URL, for every key-taking provider"), [this]()
		{
			FLogCapture Log;
			TMap<EN2CLLMProvider, FSeenRequest> Seen;
			RunProviders(false, Seen, Log);

			for (const FCase& Case : Cases())
			{
				const FString Name = UEnum::GetDisplayValueAsText(Case.Provider).ToString();
				const FSeenRequest* Request = Seen.Find(Case.Provider);
				if (!TestNotNull(Name + TEXT(" request seen"), Request))
				{
					continue;
				}
				TestFalse(Name + TEXT(": no key in the request path"), Request->RelativePath.Contains(FakeKey));
				TestFalse(Name + TEXT(": no 'key' query parameter"), Request->Query.Contains(TEXT("key")));
				for (const TPair<FString, FString>& Param : Request->Query)
				{
					TestFalse(Name + TEXT(": no query parameter carries the key"), Param.Value.Contains(FakeKey));
				}

				TArray<FString> Carrying;
				for (const TPair<FString, FString>& Header : Request->Headers)
				{
					if (Header.Value.Contains(FakeKey))
					{
						Carrying.Add(Header.Key);
					}
				}
				if (TestEqual(Name + TEXT(": exactly one header carries the key"), Carrying.Num(), 1))
				{
					TestTrue(FString::Printf(TEXT("%s: the key is in %s (got %s)"), *Name, Case.KeyHeader, *Carrying[0]),
						Carrying[0].Equals(Case.KeyHeader, ESearchCase::IgnoreCase));
				}
				const int32 Authorizations = Request->Headers.FilterByPredicate([](const TPair<FString, FString>& Header)
					{ return Header.Key.Equals(TEXT("Authorization"), ESearchCase::IgnoreCase); }).Num();
				TestEqual(Name + TEXT(": Authorization headers"), Authorizations, Case.bBearer ? 1 : 0);
				if (Case.Provider == EN2CLLMProvider::Gemini)
				{
					TestTrue(TEXT("Gemini posts to <base><model>:generateContent"),
						Request->RelativePath.EndsWith(FString(Case.Model) + TEXT(":generateContent")));
				}
			}
		});
	});

	Describe(TEXT("Logs"), [this]()
	{
		It(TEXT("hold no key, prompt, N2C JSON or answer at Debug while n2c.LogLLMPayloads is 0"), [this]()
		{
			FLogCapture Log;
			TMap<EN2CLLMProvider, FSeenRequest> Seen;
			RunProviders(false, Seen, Log);

			TestEqual(TEXT("no log line holds the key"), Log.Containing(FakeKey).Num(), 0);
			TestEqual(TEXT("no log line holds the N2C JSON sent"), Log.Containing(GraphMarker).Num(), 0);
			TestEqual(TEXT("no log line holds the system prompt"), Log.Containing(SystemMarker).Num(), 0);
			TestEqual(TEXT("no log line holds the model's answer"), Log.Containing(AnswerMarker).Num(), 0);
			TestEqual(TEXT("no 'LLM Request Payload' line"), Log.Containing(TEXT("LLM Request Payload")).Num(), 0);
			TestEqual(TEXT("no 'key=' in any log line"), Log.Containing(TEXT("key="), ESearchCase::IgnoreCase).Num(), 0);
			// The run did log at Debug: the sizes stand in for the payloads
			TestTrue(TEXT("each request body is logged by size"),
				Log.Containing(TEXT("LLM request body:")).Num() >= Cases().Num());
			TestTrue(TEXT("each response is logged with provider, model, status and size"),
				Log.Containing(TEXT("HTTP 200")).Num() >= Cases().Num());
		});

		It(TEXT("hold the payloads but still no key while n2c.LogLLMPayloads is 1"), [this]()
		{
			AddExpectedMessage(TEXT("n2c.LogLLMPayloads is on"), EAutomationExpectedMessageFlags::Contains, 0);
			FLogCapture Log;
			TMap<EN2CLLMProvider, FSeenRequest> Seen;
			RunProviders(true, Seen, Log);

			TestTrue(TEXT("turning it on warns once"), Log.Containing(TEXT("n2c.LogLLMPayloads is on")).Num() >= 1);
			TestTrue(TEXT("the request payloads are logged"), Log.Containing(TEXT("LLM Request Payload:")).Num() >= Cases().Num());
			TestTrue(TEXT("the N2C JSON sent is logged"), Log.Containing(GraphMarker).Num() >= 1);
			TestTrue(TEXT("the model's answer is logged"), Log.Containing(AnswerMarker).Num() >= 1);
			TestEqual(TEXT("no log line holds the key"), Log.Containing(FakeKey).Num(), 0);
		});

		It(TEXT("redact a key the provider's error body echoes, in the log and in the reported error"), [this]()
		{
			AddExpectedError(TEXT("HTTP 401 error"), EAutomationExpectedErrorFlags::Contains, 0);
			AddExpectedError(TEXT("401"), EAutomationExpectedErrorFlags::Contains, 0);
			FStub Stub;
			if (!TestTrue(TEXT("stub listening"), Stub.Start()))
			{
				return;
			}
			ON_SCOPE_EXIT { Stub.Stop(); };
			FScopedVerbosity Verbosity(false);
			FLogCapture Log;

			UN2CBaseLLMService* Service = MakeService(EN2CLLMProvider::OpenAI, TEXT("gpt-6.1-sol"), Stub.Url(TEXT("echo401")));
			const FString Response = SendAndWait(Service);
			TestEqual(TEXT("the stub received the request"), Stub.Seen->Num(), 1);
			TestTrue(TEXT("the provider's message reaches the caller"), Response.Contains(TEXT("Incorrect API key provided")));
			TestFalse(TEXT("the reported error does not hold the key"), Response.Contains(FakeKey));
			TestTrue(TEXT("the echoed key is replaced"), Response.Contains(TEXT("[redacted]")));

			FN2CTranslationResponse Parsed;
			if (UN2CResponseParserBase* Parser = Service->GetResponseParser())
			{
				TestFalse(TEXT("the error does not parse as a translation"), Parser->ParseLLMResponse(Response, Parsed));
				TestFalse(TEXT("the typed error does not hold the key"), Parsed.Error.ToDisplayString().Contains(FakeKey));
				TestEqual(TEXT("the typed error carries the 401"), Parsed.Error.HttpStatus, 401);
			}
			TestEqual(TEXT("no log line holds the key"), Log.Containing(FakeKey).Num(), 0);
		});

		It(TEXT("hold no key for a Gemini request to an unreachable host"), [this]()
		{
			AddExpectedError(TEXT("HTTP request failed"), EAutomationExpectedErrorFlags::Contains, 0);
			AddExpectedError(TEXT("request failed"), EAutomationExpectedErrorFlags::Contains, 0);
			FScopedVerbosity Verbosity(false);
			FLogCapture Log;

			// Port 1 on the loopback refuses the connection at once
			UN2CBaseLLMService* Service = MakeService(EN2CLLMProvider::Gemini, TEXT("gemini-3.8-flash"),
				TEXT("http://127.0.0.1:1/v1beta/models/"));
			const FString Response = SendAndWait(Service);
			TestTrue(TEXT("the request failed without a response"), Response.Contains(TEXT("No response from the provider")));
			TestEqual(TEXT("no log line holds the key"), Log.Containing(FakeKey).Num(), 0);
			TestEqual(TEXT("no 'key=' in any log line"), Log.Containing(TEXT("key="), ESearchCase::IgnoreCase).Num(), 0);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
