// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CSettings.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
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
#include "Kismet/KismetSystemLibrary.h"
#include "LLM/N2CBatchTranslationOrchestrator.h"
#include "LLM/N2CLLMModule.h"
#include "LLM/N2CLLMRequestHandle.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Models/N2CBlueprint.h"
#include "Models/N2CTranslation.h"
#include "Tests/N2CTestBlueprintFactory.h"
#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CTestHttpStub.h"

/**
 * The life of one LLM request (card 0149): a provider that accepts the connection and then sends nothing fails after
 * the HTTP Activity Timeout with a typed Timeout error instead of hanging for the hour-long total timeout; a request's
 * Processing status belongs to that request; a batch cancel stops the in-flight HTTP request at once; and a batch
 * picks up a provider changed in the settings from its next item. The provider is a local stub (Ollama's and LM
 * Studio's routes), never a real one.
 */
BEGIN_DEFINE_SPEC(FN2CRequestLifecycleSpec, "NodeToCode.LLM.RequestLifecycle", N2C_TEST_FLAGS)

	/** A completed translation of graph EventGraph of class GraphClass, as the JSON string the model writes */
	static FString TranslationContent(const FString& GraphClass)
	{
		return FString::Printf(
			TEXT("{\\\"graphs\\\":[{\\\"graph_name\\\":\\\"EventGraph\\\",\\\"graph_type\\\":\\\"EventGraph\\\",\\\"graph_class\\\":\\\"%s\\\",")
			TEXT("\\\"code\\\":{\\\"graphDeclaration\\\":\\\"\\\",\\\"graphImplementation\\\":\\\"// translated\\\",\\\"implementationNotes\\\":\\\"\\\"}}]}"),
			*GraphClass);
	}

	static FString OllamaReply()
	{
		return FString::Printf(
			TEXT("{\"model\":\"n2c-lifecycle\",\"created_at\":\"2026-10-25T00:00:00Z\",\"message\":{\"role\":\"assistant\",\"content\":\"%s\"},")
			TEXT("\"done_reason\":\"stop\",\"done\":true,\"prompt_eval_count\":10,\"eval_count\":10}"),
			*TranslationContent(TEXT("BP_Lifecycle")));
	}

	static FString LMStudioReply()
	{
		return FString::Printf(
			TEXT("{\"id\":\"chatcmpl-n2c\",\"object\":\"chat.completion\",\"model\":\"n2c-lifecycle\",")
			TEXT("\"choices\":[{\"index\":0,\"message\":{\"role\":\"assistant\",\"content\":\"%s\"},\"finish_reason\":\"stop\"}],")
			TEXT("\"usage\":{\"prompt_tokens\":10,\"completion_tokens\":10,\"total_tokens\":20}}"),
			*TranslationContent(TEXT("BP_Lifecycle")));
	}

	/**
	 * A local endpoint with Ollama's /api/chat and LM Studio's /v1/chat/completions. DelayFor(hit number, from 1) says
	 * when to answer: 0 at once, a positive delay in seconds, or a negative value never (the request stalls until
	 * Stop). Answers go out from Tick, which Pump calls.
	 */
	struct FStubProvider
	{
		struct FPending
		{
			double At = 0.0;
			FHttpResultCallback OnComplete;
			FString Body;
		};
		struct FState
		{
			int32 Hits = 0;
			TArray<FString> Paths;
			TFunction<double(int32)> DelayFor;
			TArray<FPending> Pending;
		};

		uint32 Port = 0;
		TSharedPtr<IHttpRouter> Router;
		TArray<FHttpRouteHandle> Routes;
		TSharedRef<FState> State = MakeShared<FState>();

		bool Start(TFunction<double(int32)> DelayFor)
		{
			State->DelayFor = MoveTemp(DelayFor);
			FHttpServerModule& Server = FHttpServerModule::Get();
			// A port the OS picked, not a random one in a range another editor's stub may hold (parallel checkouts)
			if (!Router.IsValid())
			{
				Router = N2CTest::StartStubRouter(Port);
			}
			if (!Router.IsValid())
			{
				return false;
			}
			Bind(TEXT("/api/chat"), OllamaReply());
			Bind(TEXT("/v1/chat/completions"), LMStudioReply());
			Server.StartAllListeners();
			return Routes.Num() == 2 && Routes[0].IsValid() && Routes[1].IsValid();
		}

		void Bind(const FString& Path, const FString& Body)
		{
			TSharedRef<FState> StateRef = State;
			Routes.Add(Router->BindRoute(FHttpPath(Path), EHttpServerRequestVerbs::VERB_POST,
				FHttpRequestHandler::CreateLambda([StateRef, Path, Body](const FHttpServerRequest&, const FHttpResultCallback& OnComplete)
				{
					const int32 Hit = ++StateRef->Hits;
					StateRef->Paths.Add(Path);
					const double Delay = StateRef->DelayFor ? StateRef->DelayFor(Hit) : 0.0;
					if (Delay == 0.0)
					{
						OnComplete(FHttpServerResponse::Create(Body, TEXT("application/json")));
						return true;
					}
					FPending Pending;
					Pending.At = Delay < 0.0 ? TNumericLimits<double>::Max() : FPlatformTime::Seconds() + Delay;
					Pending.OnComplete = OnComplete;
					Pending.Body = Body;
					StateRef->Pending.Add(MoveTemp(Pending));
					return true;
				})));
		}

		void Tick()
		{
			const double Now = FPlatformTime::Seconds();
			for (int32 Index = State->Pending.Num() - 1; Index >= 0; --Index)
			{
				if (State->Pending[Index].At <= Now)
				{
					FPending Due = MoveTemp(State->Pending[Index]);
					State->Pending.RemoveAt(Index);
					Due.OnComplete(FHttpServerResponse::Create(Due.Body, TEXT("application/json")));
				}
			}
		}

		void Stop()
		{
			// Answer what is still held, so the server lets go of the connections
			for (FPending& Pending : State->Pending)
			{
				TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(TEXT("{\"error\":\"stub stopped\"}"), TEXT("application/json"));
				Response->Code = EHttpServerResponseCodes::ServiceUnavail;
				Pending.OnComplete(MoveTemp(Response));
			}
			State->Pending.Reset();
			for (FHttpRouteHandle& Route : Routes)
			{
				if (Router.IsValid() && Route.IsValid())
				{
					Router->UnbindRoute(Route);
				}
			}
			Routes.Reset();
		}

		FString Endpoint() const
		{
			return FString::Printf(TEXT("http://127.0.0.1:%u"), Port);
		}
	};

	/** Ticks the HTTP server, the stub and the HTTP client until Seconds pass or Until is true */
	static void Pump(FStubProvider& Stub, double Seconds, TFunctionRef<bool()> Until)
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
			Stub.Tick();
			Manager.Tick(Delta);
		}
	}

	/** Ollama at the stub as the provider, LM Studio at the stub too, the activity timeout and the output folder */
	struct FScopedSettings
	{
		FScopedSettings(const FString& Endpoint, const FString& Output, float ActivityTimeout)
		{
			UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
			SavedProvider = Settings->Provider;
			SavedOllamaEndpoint = Settings->OllamaConfig.OllamaEndpoint;
			SavedLMStudioEndpoint = Settings->LMStudioEndpoint;
			SavedOutput = Settings->CustomTranslationOutputDirectory.Path;
			SavedTimeout = Settings->HttpActivityTimeoutSeconds;
			Settings->Provider = EN2CLLMProvider::Ollama;
			Settings->OllamaConfig.OllamaEndpoint = Endpoint;
			Settings->LMStudioEndpoint = Endpoint;
			Settings->CustomTranslationOutputDirectory.Path = Output;
			Settings->HttpActivityTimeoutSeconds = ActivityTimeout;
		}
		~FScopedSettings()
		{
			UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
			Settings->Provider = SavedProvider;
			Settings->OllamaConfig.OllamaEndpoint = SavedOllamaEndpoint;
			Settings->LMStudioEndpoint = SavedLMStudioEndpoint;
			Settings->CustomTranslationOutputDirectory.Path = SavedOutput;
			Settings->HttpActivityTimeoutSeconds = SavedTimeout;
		}
		EN2CLLMProvider SavedProvider;
		FString SavedOllamaEndpoint;
		FString SavedLMStudioEndpoint;
		FString SavedOutput;
		float SavedTimeout = 300.0f;
	};

	/** What one request delivered */
	struct FObserved
	{
		int32 Completions = 0;
		int32 Parsed = 0;
		bool bSuccess = false;
		FN2CTranslationResponse Response;
	};

	static FN2CLLMRequestHandleRef Send(const TSharedRef<FObserved>& Observed)
	{
		return UN2CLLMModule::Get()->ProcessN2CJson(TEXT("{\"version\":\"1.0.0\"}"), FN2CBlueprint(),
			FOnLLMResponseReceived::CreateLambda([Observed](const FString&) { ++Observed->Completions; }),
			FOnN2CTranslationParsed::CreateLambda([Observed](const FN2CTranslationResponse& Response, bool bSuccess)
			{
				++Observed->Parsed;
				Observed->bSuccess = bSuccess;
				Observed->Response = Response;
			}));
	}

	/** An Actor Blueprint named Name with a Print String call in its event graph, and its batch tag */
	TUniquePtr<N2CTest::FTestBlueprint> MakeTagged(const FString& Name, TArray<FN2CTagInfo>& OutTags)
	{
		TUniquePtr<N2CTest::FTestBlueprint> BP = MakeUnique<N2CTest::FTestBlueprint>(Name);
		UEdGraph* Graph = BP->GetBlueprint() ? BP->EventGraph() : nullptr;
		if (!TestNotNull(*FString::Printf(TEXT("%s has an event graph"), *Name), Graph))
		{
			return BP;
		}
		BP->AddCall(Graph, UKismetSystemLibrary::StaticClass(), GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString));
		for (const FString& Error : BP->Errors())
		{
			AddError(FString::Printf(TEXT("%s: building the graph: %s"), *Name, *Error));
		}
		FN2CTagInfo Tag;
		Tag.Tag = TEXT("N2CRequestLifecycle");
		Tag.GraphGuid = Graph->GraphGuid.ToString();
		Tag.GraphName = Name;
		Tag.BlueprintPath = BP->GetBlueprint()->GetPathName();
		OutTags.Add(Tag);
		return BP;
	}

	/** Collects what a batch reports */
	struct FBatchObserver
	{
		int32 ItemsCompleted = 0;
		bool bDone = false;
		FN2CBatchTranslationResult Result;
		FDelegateHandle ItemHandle;
		FDelegateHandle BatchHandle;
		TFunction<void(int32)> OnItem;
	};

	static void Observe(const TSharedRef<FBatchObserver>& Observer)
	{
		UN2CBatchTranslationOrchestrator& Orchestrator = UN2CBatchTranslationOrchestrator::Get();
		Observer->ItemHandle = Orchestrator.OnItemCompleteNative.AddLambda(
			[Observer](const FN2CTagInfo&, const FN2CTranslationResponse&, bool, int32 Index, int32)
			{
				++Observer->ItemsCompleted;
				if (Observer->OnItem)
				{
					Observer->OnItem(Index);
				}
			});
		Observer->BatchHandle = Orchestrator.OnBatchCompleteNative.AddLambda(
			[Observer](const FN2CBatchTranslationResult& Result)
			{
				Observer->Result = Result;
				Observer->bDone = true;
			});
	}

	static void StopObserving(const TSharedRef<FBatchObserver>& Observer)
	{
		UN2CBatchTranslationOrchestrator& Orchestrator = UN2CBatchTranslationOrchestrator::Get();
		Orchestrator.OnItemCompleteNative.Remove(Observer->ItemHandle);
		Orchestrator.OnBatchCompleteNative.Remove(Observer->BatchHandle);
	}

	/** The status each item has in BatchSummary.json, in order */
	static TArray<FString> SummaryStatuses(const FString& BatchOutputPath)
	{
		TArray<FString> Statuses;
		FString Text;
		const TSharedPtr<FJsonObject> Summary = FFileHelper::LoadFileToString(Text, *(BatchOutputPath / TEXT("BatchSummary.json")))
			? N2CTest::ParseJsonObject(Text) : nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
		if (Summary.IsValid() && Summary->TryGetArrayField(TEXT("items"), Items))
		{
			for (const TSharedPtr<FJsonValue>& Item : *Items)
			{
				FString Status;
				Item->AsObject()->TryGetStringField(TEXT("status"), Status);
				Statuses.Add(Status);
			}
		}
		return Statuses;
	}

END_DEFINE_SPEC(FN2CRequestLifecycleSpec)

void FN2CRequestLifecycleSpec::Define()
{
	Describe(TEXT("ActivityTimeout"), [this]()
	{
		It(TEXT("fails a request the provider never answers with a Timeout error after the activity timeout"), [this]()
		{
			AddExpectedError(TEXT("HTTP request failed"), EAutomationExpectedErrorFlags::Contains, 0);
			AddExpectedError(TEXT("Translation failed"), EAutomationExpectedErrorFlags::Contains, 0);
			AddExpectedError(TEXT("request failed"), EAutomationExpectedErrorFlags::Contains, 0);

			// Stop answers the stalled connection the client already dropped; the server logs that it could not send
			AddExpectedError(TEXT("socket_send_failure"), EAutomationExpectedErrorFlags::Contains, 0);
			FStubProvider Stub;
			if (!TestTrue(TEXT("stub started"), Stub.Start([](int32) { return -1.0; })))
			{
				return;
			}
			ON_SCOPE_EXIT { Stub.Stop(); };
			N2CTest::FScratchDir Output(TEXT("N2CRequestLifecycle"));
			FScopedSettings Scoped(Stub.Endpoint(), Output.Path, 2.0f);

			UN2CLLMModule* Module = UN2CLLMModule::Get();
			if (!TestTrue(TEXT("module initializes with Ollama"), Module->Initialize()))
			{
				return;
			}
			TestEqual(TEXT("the request config carries the activity timeout setting"), Module->GetConfig().ActivityTimeoutSeconds, 2.0f);

			const TSharedRef<FObserved> Observed = MakeShared<FObserved>();
			const double Start = FPlatformTime::Seconds();
			FN2CLLMRequestHandleRef Handle = Send(Observed);
			Pump(Stub, 30.0, [Observed] { return Observed->Completions > 0; });
			const double Elapsed = FPlatformTime::Seconds() - Start;

			TestEqual(TEXT("the stub received the request"), Stub.State->Hits, 1);
			if (!TestEqual(TEXT("the stalled request completed once"), Observed->Completions, 1))
			{
				Handle->Cancel();
				return;
			}
			TestTrue(FString::Printf(TEXT("it failed after the activity timeout (%.1f s), not the total timeout"), Elapsed),
				Elapsed >= 1.5 && Elapsed < 15.0);
			TestEqual(TEXT("its own result arrived once"), Observed->Parsed, 1);
			TestFalse(TEXT("the result is a failure"), Observed->bSuccess);
			TestEqual(TEXT("the error kind is Timeout"), Observed->Response.Error.Kind, EN2CTranslationErrorKind::Timeout);
			TestTrue(TEXT("a timeout is retryable"), Observed->Response.Error.bRetryable);
			TestTrue(TEXT("the message names the setting"), Observed->Response.Error.Message.Contains(TEXT("HTTP Activity Timeout")));
			TestTrue(TEXT("the handle completed"), Handle->IsCompleted());
			TestTrue(TEXT("the module reports the error"), Module->GetSystemStatus() == EN2CSystemStatus::Error);
		});

		It(TEXT("lets a slow answer through when data arrives within the activity timeout"), [this]()
		{
			// The request carries an empty capture: saving it logs that it has no name, which is not what this checks
			AddExpectedError(TEXT("Missing Blueprint name"), EAutomationExpectedErrorFlags::Contains, 0);
			FStubProvider Stub;
			if (!TestTrue(TEXT("stub started"), Stub.Start([](int32) { return 1.0; })))
			{
				return;
			}
			ON_SCOPE_EXIT { Stub.Stop(); };
			N2CTest::FScratchDir Output(TEXT("N2CRequestLifecycle"));
			FScopedSettings Scoped(Stub.Endpoint(), Output.Path, 4.0f);

			UN2CLLMModule* Module = UN2CLLMModule::Get();
			if (!TestTrue(TEXT("module initializes with Ollama"), Module->Initialize()))
			{
				return;
			}
			const TSharedRef<FObserved> Observed = MakeShared<FObserved>();
			FN2CLLMRequestHandleRef Handle = Send(Observed);
			Pump(Stub, 15.0, [Observed] { return Observed->Completions > 0; });
			TestEqual(TEXT("completed once"), Observed->Completions, 1);
			TestTrue(TEXT("the slow answer is a translation"), Observed->bSuccess);
			TestEqual(TEXT("one graph"), Observed->Response.Graphs.Num(), 1);
			TestTrue(TEXT("the module is idle"), Module->GetSystemStatus() == EN2CSystemStatus::Idle);
			Handle->Cancel();
		});
	});

	Describe(TEXT("Status"), [this]()
	{
		It(TEXT("stays Processing while another caller's request is still in flight"), [this]()
		{
			// Stop answers the stalled connection the client already dropped; the server logs that it could not send
			AddExpectedError(TEXT("socket_send_failure"), EAutomationExpectedErrorFlags::Contains, 0);
			FStubProvider Stub;
			if (!TestTrue(TEXT("stub started"), Stub.Start([](int32) { return -1.0; })))
			{
				return;
			}
			ON_SCOPE_EXIT { Stub.Stop(); };
			N2CTest::FScratchDir Output(TEXT("N2CRequestLifecycle"));
			FScopedSettings Scoped(Stub.Endpoint(), Output.Path, 60.0f);

			UN2CLLMModule* Module = UN2CLLMModule::Get();
			if (!TestTrue(TEXT("module initializes with Ollama"), Module->Initialize()))
			{
				return;
			}

			const TSharedRef<FObserved> First = MakeShared<FObserved>();
			FN2CLLMRequestHandleRef FirstHandle = Send(First);

			FN2CLLMConfig Override = UN2CLLMModule::MakeConfigFromSettings();
			const TSharedRef<int32> SecondCalls = MakeShared<int32>(0);
			FN2CLLMRequestHandleRef SecondHandle = Module->ProcessN2CJsonWithOverrides(TEXT("{}"), Override, EN2CCodeLanguage::Cpp,
				FOnLLMResponseReceived::CreateLambda([SecondCalls](const FString&) { ++(*SecondCalls); }));
			Pump(Stub, 5.0, [&Stub] { return Stub.State->Hits >= 2; });
			TestEqual(TEXT("both requests reached the stub"), Stub.State->Hits, 2);
			TestTrue(TEXT("Processing with two requests in flight"), Module->GetSystemStatus() == EN2CSystemStatus::Processing);

			const double CancelStart = FPlatformTime::Seconds();
			TestTrue(TEXT("the second request cancels"), SecondHandle->Cancel());
			TestTrue(TEXT("cancel completes within 1 s"), FPlatformTime::Seconds() - CancelStart < 1.0);
			TestTrue(TEXT("the first request still owns Processing"), Module->GetSystemStatus() == EN2CSystemStatus::Processing);

			TestTrue(TEXT("the first request cancels"), FirstHandle->Cancel());
			TestTrue(TEXT("Idle once no request is in flight"), Module->GetSystemStatus() == EN2CSystemStatus::Idle);

			Pump(Stub, 1.0, [] { return false; });
			TestEqual(TEXT("the first request's callbacks never ran"), First->Completions + First->Parsed, 0);
			TestEqual(TEXT("the second request's callback never ran"), *SecondCalls, 0);
		});
	});

	Describe(TEXT("Batch"), [this]()
	{
		It(TEXT("cancels the in-flight item's request at once and skips the rest"), [this]()
		{
			// Item 1 is answered at once, item 2's request stalls until the batch cancels it
			// Stop answers the stalled connection the client already dropped; the server logs that it could not send
			AddExpectedError(TEXT("socket_send_failure"), EAutomationExpectedErrorFlags::Contains, 0);
			FStubProvider Stub;
			if (!TestTrue(TEXT("stub started"), Stub.Start([](int32 Hit) { return Hit == 1 ? 0.0 : -1.0; })))
			{
				return;
			}
			ON_SCOPE_EXIT { Stub.Stop(); };
			N2CTest::FScratchDir Output(TEXT("N2CRequestLifecycle"));
			FScopedSettings Scoped(Stub.Endpoint(), Output.Path, 600.0f);

			TArray<FN2CTagInfo> Tags;
			TArray<TUniquePtr<N2CTest::FTestBlueprint>> Blueprints;
			Blueprints.Add(MakeTagged(TEXT("BP_N2CLifecycleA"), Tags));
			Blueprints.Add(MakeTagged(TEXT("BP_N2CLifecycleB"), Tags));
			Blueprints.Add(MakeTagged(TEXT("BP_N2CLifecycleC"), Tags));
			if (!TestEqual(TEXT("three tagged graphs"), Tags.Num(), 3))
			{
				return;
			}

			const TSharedRef<FBatchObserver> Observer = MakeShared<FBatchObserver>();
			Observe(Observer);
			ON_SCOPE_EXIT { StopObserving(Observer); };

			UN2CBatchTranslationOrchestrator& Orchestrator = UN2CBatchTranslationOrchestrator::Get();
			if (!TestTrue(TEXT("batch started"), Orchestrator.StartBatchTranslation(Tags)))
			{
				return;
			}
			Pump(Stub, 15.0, [&Stub, Observer] { return Stub.State->Hits >= 2 && Observer->ItemsCompleted >= 1; });
			if (!TestEqual(TEXT("the second item's request is in flight"), Stub.State->Hits, 2))
			{
				Orchestrator.CancelBatch();
				Pump(Stub, 2.0, [Observer] { return Observer->bDone; });
				return;
			}
			TestEqual(TEXT("the second item is being translated"), Orchestrator.GetCurrentItemIndex(), 1);

			const double CancelStart = FPlatformTime::Seconds();
			Orchestrator.CancelBatch();
			const double CancelTime = FPlatformTime::Seconds() - CancelStart;

			TestTrue(TEXT("the batch completed inside CancelBatch, without waiting for the provider"), Observer->bDone);
			TestTrue(FString::Printf(TEXT("cancel took %.3f s (under 1 s)"), CancelTime), CancelTime < 1.0);
			TestFalse(TEXT("no batch is in progress"), Orchestrator.IsBatchInProgress());
			TestTrue(TEXT("the module is idle (the cancelled request gave up its status)"),
				UN2CLLMModule::Get()->GetSystemStatus() == EN2CSystemStatus::Idle);
			TestEqual(TEXT("one item succeeded"), Observer->Result.SuccessCount, 1);
			TestEqual(TEXT("two items skipped (the cancelled one and the last)"), Observer->Result.SkippedCount, 2);
			TestEqual(TEXT("none failed"), Observer->Result.FailureCount, 0);
			TestEqual(TEXT("only the first item reported a result"), Observer->ItemsCompleted, 1);
			TestEqual(TEXT("BatchSummary.json statuses"), SummaryStatuses(Observer->Result.BatchOutputPath),
				TArray<FString>({ TEXT("EN2CBatchItemStatus::Completed"), TEXT("EN2CBatchItemStatus::Skipped"), TEXT("EN2CBatchItemStatus::Skipped") }));

			// The cancelled request never reports back, and the third item is never sent
			Pump(Stub, 1.0, [] { return false; });
			TestEqual(TEXT("no request after the cancel"), Stub.State->Hits, 2);
			TestEqual(TEXT("still one item result"), Observer->ItemsCompleted, 1);
		});

		It(TEXT("sends the next item to the provider the settings name now"), [this]()
		{
			FStubProvider Stub;
			if (!TestTrue(TEXT("stub started"), Stub.Start([](int32) { return 0.0; })))
			{
				return;
			}
			ON_SCOPE_EXIT { Stub.Stop(); };
			N2CTest::FScratchDir Output(TEXT("N2CRequestLifecycle"));
			FScopedSettings Scoped(Stub.Endpoint(), Output.Path, 600.0f);

			TArray<FN2CTagInfo> Tags;
			TArray<TUniquePtr<N2CTest::FTestBlueprint>> Blueprints;
			Blueprints.Add(MakeTagged(TEXT("BP_N2CLifecycleD"), Tags));
			Blueprints.Add(MakeTagged(TEXT("BP_N2CLifecycleE"), Tags));
			if (!TestEqual(TEXT("two tagged graphs"), Tags.Num(), 2))
			{
				return;
			}

			// The user picks LM Studio once the first item is done, as the quick picker or Project Settings would
			const TSharedRef<FBatchObserver> Observer = MakeShared<FBatchObserver>();
			Observer->OnItem = [](int32 Index)
			{
				if (Index == 0)
				{
					GetMutableDefault<UN2CSettings>()->Provider = EN2CLLMProvider::LMStudio;
				}
			};
			Observe(Observer);
			ON_SCOPE_EXIT { StopObserving(Observer); };

			UN2CBatchTranslationOrchestrator& Orchestrator = UN2CBatchTranslationOrchestrator::Get();
			if (!TestTrue(TEXT("batch started"), Orchestrator.StartBatchTranslation(Tags)))
			{
				return;
			}
			Pump(Stub, 15.0, [Observer] { return Observer->bDone; });
			if (!TestTrue(TEXT("batch completed"), Observer->bDone))
			{
				Orchestrator.CancelBatch();
				return;
			}
			TestEqual(TEXT("the first item went to Ollama, the second to LM Studio"), Stub.State->Paths,
				TArray<FString>({ TEXT("/api/chat"), TEXT("/v1/chat/completions") }));
			TestEqual(TEXT("both items succeeded"), Observer->Result.SuccessCount, 2);
			TestEqual(TEXT("the module now uses LM Studio"), UN2CLLMModule::Get()->GetConfig().Provider, EN2CLLMProvider::LMStudio);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
