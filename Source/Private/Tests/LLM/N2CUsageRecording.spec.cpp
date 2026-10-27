// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CCapture.h"
#include "Core/N2CNodeCollector.h"
#include "Core/N2CSerializer.h"
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
#include "K2Node.h"
#include "Kismet/KismetSystemLibrary.h"
#include "LLM/N2CBatchTranslationOrchestrator.h"
#include "Core/Widgets/SN2CMainWindow.h"
#include "LLM/N2CLLMModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Models/N2CBatchTranslationTypes.h"
#include "Models/N2CBlueprint.h"
#include "Models/N2CTranslation.h"
#include "Tests/N2CTestBlueprintFactory.h"
#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CTestHttpStub.h"

/**
 * Token usage beyond input and output (card 0185): cached input read from and written to the provider's prompt cache
 * and thinking output are kept in FN2CTranslationUsage and saved, next to the totals, in the translation's
 * N2C_Translation_*.json and in BatchSummary.json. A local LM Studio-style /v1/chat/completions stub answers with an
 * OpenAI-compatible usage object (prompt_tokens_details.cached_tokens, completion_tokens_details.reasoning_tokens) and
 * the translation inside a ```JSON fence between two sentences, so the whole path runs: request, parse, save.
 */
BEGIN_DEFINE_SPEC(FN2CUsageRecordingSpec, "NodeToCode.LLM.Usage", N2C_TEST_FLAGS)

	static constexpr int32 PromptTokens = 5120;
	static constexpr int32 CachedTokens = 4096;
	static constexpr int32 CompletionTokens = 700;
	static constexpr int32 ReasoningTokens = 300;

	/** A local LM Studio-style endpoint that answers every POST with one successful translation */
	struct FStubLMStudio
	{
		uint32 Port = 0;
		TSharedPtr<IHttpRouter> Router;
		FHttpRouteHandle Route;
		TSharedRef<int32> Hits = MakeShared<int32>(0);

		bool Start(const FString& Body)
		{
			FHttpServerModule& Server = FHttpServerModule::Get();
			// A port the OS picked, not one in a range another editor's stub may hold (parallel checkouts)
			if (!Router.IsValid())
			{
				Router = N2CTest::StartStubRouter(Port);
			}
			if (!Router.IsValid())
			{
				return false;
			}
			TSharedRef<int32> HitsRef = Hits;
			Route = Router->BindRoute(FHttpPath(TEXT("/v1/chat/completions")), EHttpServerRequestVerbs::VERB_POST,
				FHttpRequestHandler::CreateLambda([HitsRef, Body](const FHttpServerRequest&, const FHttpResultCallback& OnComplete)
				{
					++(*HitsRef);
					OnComplete(FHttpServerResponse::Create(Body, TEXT("application/json")));
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
	};

	/** LM Studio at the stub as the active provider, and translations saved under Output, for the life of the scope */
	struct FScopedSettings
	{
		FScopedSettings(const FString& Endpoint, const FString& Output)
		{
			UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
			SavedEndpoint = Settings->LMStudioEndpoint;
			SavedProvider = Settings->Provider;
			SavedOutput = Settings->CustomTranslationOutputDirectory.Path;
			Settings->LMStudioEndpoint = Endpoint;
			Settings->Provider = EN2CLLMProvider::LMStudio;
			Settings->CustomTranslationOutputDirectory.Path = Output;
		}
		~FScopedSettings()
		{
			UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
			Settings->LMStudioEndpoint = SavedEndpoint;
			Settings->Provider = SavedProvider;
			Settings->CustomTranslationOutputDirectory.Path = SavedOutput;
		}
		FString SavedEndpoint;
		EN2CLLMProvider SavedProvider;
		FString SavedOutput;
	};

	/** A JSON string literal (quotes included) holding Text */
	static FString Quote(const FString& Text)
	{
		FString Out = TEXT("\"");
		for (const TCHAR Char : Text)
		{
			switch (Char)
			{
			case TEXT('"'): Out += TEXT("\\\""); break;
			case TEXT('\\'): Out += TEXT("\\\\"); break;
			case TEXT('\n'): Out += TEXT("\\n"); break;
			default: Out.AppendChar(Char); break;
			}
		}
		return Out + TEXT("\"");
	}

	/** The stub's chat completion: the translation of GraphClass's event graph in a fence between two sentences */
	static FString Reply(const FString& GraphClass)
	{
		const FString Translation = FString::Printf(
			TEXT("{\"graphs\":[{\"graph_name\":\"EventGraph\",\"graph_type\":\"EventGraph\",\"graph_class\":\"%s\",")
			TEXT("\"code\":{\"graphDeclaration\":\"\",\"graphImplementation\":\"// translated\",\"implementationNotes\":\"\"}}]}"),
			*GraphClass);
		const FString Content = TEXT("Here is the translation:\n```JSON\n") + Translation + TEXT("\n```\nLet me know if you need changes.\n");
		return FString::Printf(
			TEXT("{\"id\":\"chatcmpl-n2c-usage\",\"object\":\"chat.completion\",\"created\":1761264000,\"model\":\"qwen3-32b\",")
			TEXT("\"choices\":[{\"index\":0,\"message\":{\"role\":\"assistant\",\"content\":%s},\"finish_reason\":\"stop\"}],")
			TEXT("\"usage\":{\"prompt_tokens\":%d,\"completion_tokens\":%d,\"total_tokens\":%d,")
			TEXT("\"prompt_tokens_details\":{\"cached_tokens\":%d},\"completion_tokens_details\":{\"reasoning_tokens\":%d}}}"),
			*Quote(Content), PromptTokens, CompletionTokens, PromptTokens + CompletionTokens, CachedTokens, ReasoningTokens);
	}

	/** Ticks the HTTP server and client until Seconds pass or Until is true (the test runs inside the core ticker) */
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

	/** An Actor Blueprint named Name with a Print String call in its event graph */
	TUniquePtr<N2CTest::FTestBlueprint> MakeBlueprint(const FString& Name)
	{
		TUniquePtr<N2CTest::FTestBlueprint> BP = MakeUnique<N2CTest::FTestBlueprint>(Name);
		UEdGraph* Graph = BP->GetBlueprint() ? BP->EventGraph() : nullptr;
		if (Graph)
		{
			BP->AddCall(Graph, UKismetSystemLibrary::StaticClass(), GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString));
		}
		for (const FString& Error : BP->Errors())
		{
			AddError(FString::Printf(TEXT("%s: building the graph: %s"), *Name, *Error));
		}
		return BP;
	}

	static int32 NumberOr(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, int32 Default = -1)
	{
		int32 Value = Default;
		return Object.IsValid() && Object->TryGetNumberField(Field, Value) ? Value : Default;
	}

	/** The usage the stub reports, as the parser should record it */
	void ExpectUsage(const FString& What, const FN2CTranslationUsage& Usage)
	{
		TestEqual(What + TEXT(": input tokens (cached included)"), Usage.InputTokens, PromptTokens);
		TestEqual(What + TEXT(": output tokens (reasoning included)"), Usage.OutputTokens, CompletionTokens);
		TestEqual(What + TEXT(": cache read tokens"), Usage.CacheReadInputTokens, CachedTokens);
		TestEqual(What + TEXT(": cache write tokens"), Usage.CacheWriteInputTokens, 0);
		TestEqual(What + TEXT(": reasoning tokens"), Usage.ReasoningTokens, ReasoningTokens);
	}

END_DEFINE_SPEC(FN2CUsageRecordingSpec)

void FN2CUsageRecordingSpec::Define()
{
	It(TEXT("writes every count to JSON and adds usages count by count"), [this]()
	{
		FN2CTranslationUsage A;
		TestFalse(TEXT("a new usage is all zero"), A.HasAny());
		A.InputTokens = 100;
		A.OutputTokens = 50;
		A.CacheReadInputTokens = 60;
		A.CacheWriteInputTokens = 30;
		A.ReasoningTokens = 20;
		FN2CTranslationUsage B = A;
		B += A;

		const TSharedRef<FJsonObject> Json = B.ToJson();
		TestEqual(TEXT("input_tokens"), NumberOr(Json, TEXT("input_tokens")), 200);
		TestEqual(TEXT("output_tokens"), NumberOr(Json, TEXT("output_tokens")), 100);
		TestEqual(TEXT("cache_read_input_tokens"), NumberOr(Json, TEXT("cache_read_input_tokens")), 120);
		TestEqual(TEXT("cache_write_input_tokens"), NumberOr(Json, TEXT("cache_write_input_tokens")), 60);
		TestEqual(TEXT("reasoning_tokens"), NumberOr(Json, TEXT("reasoning_tokens")), 40);

		FN2CTranslationUsage OnlyCache;
		OnlyCache.CacheReadInputTokens = 1;
		TestTrue(TEXT("a cache count alone counts as usage"), OnlyCache.HasAny());
	});

	It(TEXT("saves cache and reasoning counts with a translation"), [this]()
	{
		FStubLMStudio Stub;
		if (!TestTrue(TEXT("stub endpoint started"), Stub.Start(Reply(TEXT("BP_N2CUsageSave")))))
		{
			return;
		}
		ON_SCOPE_EXIT { Stub.Stop(); };
		N2CTest::FScratchDir Output(TEXT("N2CUsageSave"));
		FScopedSettings Scoped(Stub.Endpoint(), Output.Path);

		UN2CLLMModule* Module = UN2CLLMModule::Get();
		if (!TestTrue(TEXT("module initializes with LM Studio"), Module->Initialize()))
		{
			return;
		}

		const TUniquePtr<N2CTest::FTestBlueprint> BP = MakeBlueprint(TEXT("BP_N2CUsageSave"));
		TArray<UK2Node*> Nodes;
		if (!BP->GetBlueprint() || !TestTrue(TEXT("nodes collected"),
			BP->EventGraph() && FN2CNodeCollector::Get().CollectNodesFromGraph(BP->EventGraph(), Nodes)))
		{
			return;
		}
		TValueOrError<FN2CCapture, FString> Capture = N2C::CaptureNodes(Nodes);
		if (!TestTrue(TEXT("graph captured"), Capture.HasValue()))
		{
			return;
		}
		const FN2CBlueprint Captured = Capture.GetValue().Blueprint;

		// The caller saves the parsed response (card 0252: ProcessN2CJson writes nothing), as the main window does
		const TSharedRef<int32> Completions = MakeShared<int32>(0);
		const TSharedRef<FN2CTranslationResponse> Parsed = MakeShared<FN2CTranslationResponse>();
		Module->ProcessN2CJson(FN2CSerializer::ToJson(Captured, EN2CJsonStyle::Condensed),
			FOnLLMResponseReceived::CreateLambda([Completions](const FString&) { ++(*Completions); }),
			FOnN2CTranslationParsed::CreateLambda([Parsed](const FN2CTranslationResponse& Response, bool) { *Parsed = Response; }));
		Pump(15.0, [Completions] { return *Completions > 0; });
		if (!TestEqual(TEXT("the request completed once"), *Completions, 1))
		{
			return;
		}
		TestEqual(TEXT("the stub answered once"), *Stub.Hits, 1);
		FN2CTagInfo Graph;
		Graph.GraphName = TEXT("EventGraph");
		SN2CMainWindow::SaveSingleTranslation(Graph, *Parsed, Captured);

		const FString Folder = Module->GetLatestTranslationPath();
		const FString TranslationPath = Folder / FString::Printf(TEXT("N2C_Translation_%s.json"), *FPaths::GetBaseFilename(Folder));
		FString Text;
		const TSharedPtr<FJsonObject> Saved = FFileHelper::LoadFileToString(Text, *TranslationPath) ? N2CTest::ParseJsonObject(Text) : nullptr;
		const TSharedPtr<FJsonObject>* Usage = nullptr;
		if (TestTrue(FString::Printf(TEXT("%s is written and parses"), *TranslationPath), Saved.IsValid())
			&& TestTrue(TEXT("it has usage"), Saved->TryGetObjectField(TEXT("usage"), Usage) && Usage))
		{
			TestEqual(TEXT("saved input_tokens"), NumberOr(*Usage, TEXT("input_tokens")), PromptTokens);
			TestEqual(TEXT("saved output_tokens"), NumberOr(*Usage, TEXT("output_tokens")), CompletionTokens);
			TestEqual(TEXT("saved cache_read_input_tokens"), NumberOr(*Usage, TEXT("cache_read_input_tokens")), CachedTokens);
			TestEqual(TEXT("saved cache_write_input_tokens"), NumberOr(*Usage, TEXT("cache_write_input_tokens")), 0);
			TestEqual(TEXT("saved reasoning_tokens"), NumberOr(*Usage, TEXT("reasoning_tokens")), ReasoningTokens);
		}
	});

	It(TEXT("adds cache and reasoning counts to BatchSummary.json"), [this]()
	{
		FStubLMStudio Stub;
		if (!TestTrue(TEXT("stub endpoint started"), Stub.Start(Reply(TEXT("BP_N2CUsageBatch")))))
		{
			return;
		}
		ON_SCOPE_EXIT { Stub.Stop(); };
		N2CTest::FScratchDir Output(TEXT("N2CUsageBatch"));
		FScopedSettings Scoped(Stub.Endpoint(), Output.Path);

		if (!TestTrue(TEXT("module initializes with LM Studio"), UN2CLLMModule::Get()->Initialize()))
		{
			return;
		}

		const TUniquePtr<N2CTest::FTestBlueprint> BP = MakeBlueprint(TEXT("BP_N2CUsageBatch"));
		if (!BP->GetBlueprint() || !BP->EventGraph())
		{
			return;
		}
		FN2CTagInfo Tag;
		Tag.Tag = TEXT("N2CUsageSpec");
		Tag.GraphGuid = BP->EventGraph()->GraphGuid.ToString();
		Tag.GraphName = BP->EventGraph()->GetName();
		Tag.BlueprintPath = BP->GetBlueprint()->GetPathName();

		UN2CBatchTranslationOrchestrator& Orchestrator = UN2CBatchTranslationOrchestrator::Get();
		TSharedRef<FN2CTranslationResponse> ItemResponse = MakeShared<FN2CTranslationResponse>();
		TSharedRef<bool> bItemSuccess = MakeShared<bool>(false);
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
		ON_SCOPE_EXIT
		{
			Orchestrator.OnItemCompleteNative.Remove(ItemHandle);
			Orchestrator.OnBatchCompleteNative.Remove(BatchHandle);
		};

		if (TestTrue(TEXT("batch started"), Orchestrator.StartBatchTranslation({ Tag })))
		{
			Pump(15.0, [bBatchDone] { return *bBatchDone; });
		}
		if (!TestTrue(TEXT("batch completed"), *bBatchDone))
		{
			return;
		}
		ON_SCOPE_EXIT
		{
			if (!BatchResult->BatchOutputPath.IsEmpty())
			{
				IFileManager::Get().DeleteDirectory(*BatchResult->BatchOutputPath, false, true);
			}
		};
		TestTrue(FString::Printf(TEXT("item succeeded (error: %s)"), *ItemResponse->Error.Message), *bItemSuccess);
		ExpectUsage(TEXT("item"), ItemResponse->Usage);
		TestEqual(TEXT("result total input"), BatchResult->TotalInputTokens, PromptTokens);
		TestEqual(TEXT("result total output"), BatchResult->TotalOutputTokens, CompletionTokens);
		TestEqual(TEXT("result total cache read"), BatchResult->TotalCacheReadInputTokens, CachedTokens);
		TestEqual(TEXT("result total cache write"), BatchResult->TotalCacheWriteInputTokens, 0);
		TestEqual(TEXT("result total reasoning"), BatchResult->TotalReasoningTokens, ReasoningTokens);

		FString SummaryText;
		const TSharedPtr<FJsonObject> Summary = FFileHelper::LoadFileToString(SummaryText,
			*FPaths::Combine(BatchResult->BatchOutputPath, TEXT("BatchSummary.json"))) ? N2CTest::ParseJsonObject(SummaryText) : nullptr;
		const TSharedPtr<FJsonObject>* Stats = nullptr;
		if (TestTrue(TEXT("BatchSummary.json written and valid"), Summary.IsValid())
			&& TestTrue(TEXT("it has statistics"), Summary->TryGetObjectField(TEXT("statistics"), Stats) && Stats))
		{
			TestEqual(TEXT("totalInputTokens"), NumberOr(*Stats, TEXT("totalInputTokens")), PromptTokens);
			TestEqual(TEXT("totalOutputTokens"), NumberOr(*Stats, TEXT("totalOutputTokens")), CompletionTokens);
			TestEqual(TEXT("totalCacheReadInputTokens"), NumberOr(*Stats, TEXT("totalCacheReadInputTokens")), CachedTokens);
			TestEqual(TEXT("totalCacheWriteInputTokens"), NumberOr(*Stats, TEXT("totalCacheWriteInputTokens")), 0);
			TestEqual(TEXT("totalReasoningTokens"), NumberOr(*Stats, TEXT("totalReasoningTokens")), ReasoningTokens);
		}
		const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
		const TSharedPtr<FJsonObject>* Item = nullptr;
		const TSharedPtr<FJsonObject>* Tokens = nullptr;
		if (Summary.IsValid()
			&& TestTrue(TEXT("summary has one item"), Summary->TryGetArrayField(TEXT("items"), Items) && Items->Num() == 1)
			&& TestTrue(TEXT("the item is an object"), (*Items)[0]->TryGetObject(Item) && Item)
			&& TestTrue(TEXT("the item has tokensUsed"), (*Item)->TryGetObjectField(TEXT("tokensUsed"), Tokens) && Tokens))
		{
			TestEqual(TEXT("item input"), NumberOr(*Tokens, TEXT("input")), PromptTokens);
			TestEqual(TEXT("item output"), NumberOr(*Tokens, TEXT("output")), CompletionTokens);
			TestEqual(TEXT("item cacheReadInput"), NumberOr(*Tokens, TEXT("cacheReadInput")), CachedTokens);
			TestEqual(TEXT("item cacheWriteInput"), NumberOr(*Tokens, TEXT("cacheWriteInput")), 0);
			TestEqual(TEXT("item reasoning"), NumberOr(*Tokens, TEXT("reasoning")), ReasoningTokens);
		}
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
