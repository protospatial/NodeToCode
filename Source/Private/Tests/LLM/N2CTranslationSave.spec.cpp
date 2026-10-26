// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CNodeCollector.h"
#include "Core/N2CCapture.h"
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
#include "LLM/N2CLLMModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Models/N2CBlueprint.h"
#include "Tests/N2CTestBlueprintFactory.h"
#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CTestHttpStub.h"

/**
 * A finished translation is saved with the Blueprint captured when its request was sent (card 0131). Anything that
 * captures while the request is in flight (an overlay's token estimate on open or compile, Copy JSON, find-nodes, a
 * second translation) and anything the caller does with its own copy afterwards must not change the saved folder or
 * its N2C_BP_*.json: they name and describe the Blueprint that was translated. Since card 0133 every capture is its
 * own value (N2C::CaptureNodes), and ProcessN2CJson copies the caller's capture when the request is sent.
 */
BEGIN_DEFINE_SPEC(FN2CTranslationSaveSpec, "NodeToCode.LLM.TranslationSave", N2C_TEST_FLAGS)

	/** A local Ollama-style /api/chat endpoint that answers every POST with one successful translation */
	struct FStubOllama
	{
		uint32 Port = 0;
		TSharedPtr<IHttpRouter> Router;
		FHttpRouteHandle Route;
		TSharedRef<int32> Hits = MakeShared<int32>(0);

		bool Start(const FString& Body)
		{
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
			TSharedRef<int32> HitsRef = Hits;
			Route = Router->BindRoute(FHttpPath(TEXT("/api/chat")), EHttpServerRequestVerbs::VERB_POST,
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

	/** Ollama's reply to a translation of graph EventGraph of class GraphClass */
	static FString OllamaReply(const FString& GraphClass)
	{
		const FString Translation = FString::Printf(
			TEXT("{\\\"graphs\\\":[{\\\"graph_name\\\":\\\"EventGraph\\\",\\\"graph_type\\\":\\\"EventGraph\\\",\\\"graph_class\\\":\\\"%s\\\",")
			TEXT("\\\"code\\\":{\\\"graphDeclaration\\\":\\\"\\\",\\\"graphImplementation\\\":\\\"// translated\\\",\\\"implementationNotes\\\":\\\"\\\"}}]}"),
			*GraphClass);
		return FString::Printf(
			TEXT("{\"model\":\"n2c-save-test\",\"created_at\":\"2026-10-24T00:00:00Z\",\"message\":{\"role\":\"assistant\",\"content\":\"%s\"},")
			TEXT("\"done_reason\":\"stop\",\"done\":true,\"prompt_eval_count\":10,\"eval_count\":10}"),
			*Translation);
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

	/** Ollama at the stub as the active provider, and translations saved under Output, for the life of the scope */
	struct FScopedSettings
	{
		FScopedSettings(const FString& Endpoint, const FString& Output)
		{
			UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
			SavedEndpoint = Settings->OllamaConfig.OllamaEndpoint;
			SavedProvider = Settings->Provider;
			SavedOutput = Settings->CustomTranslationOutputDirectory.Path;
			Settings->OllamaConfig.OllamaEndpoint = Endpoint;
			Settings->Provider = EN2CLLMProvider::Ollama;
			Settings->CustomTranslationOutputDirectory.Path = Output;
		}
		~FScopedSettings()
		{
			UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
			Settings->OllamaConfig.OllamaEndpoint = SavedEndpoint;
			Settings->Provider = SavedProvider;
			Settings->CustomTranslationOutputDirectory.Path = SavedOutput;
		}
		FString SavedEndpoint;
		EN2CLLMProvider SavedProvider;
		FString SavedOutput;
	};

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

	/** Captures BP's event graph, as every translate, estimate and Copy JSON path does */
	bool CaptureEventGraph(const N2CTest::FTestBlueprint& BP, FN2CBlueprint& OutCapture)
	{
		TArray<UK2Node*> Nodes;
		if (!TestTrue(TEXT("nodes collected"), BP.EventGraph() && FN2CNodeCollector::Get().CollectNodesFromGraph(BP.EventGraph(), Nodes)))
		{
			return false;
		}
		TValueOrError<FN2CCapture, FString> Capture = N2C::CaptureNodes(Nodes);
		if (!TestTrue(TEXT("graph captured"), Capture.HasValue()))
		{
			return false;
		}
		OutCapture = MoveTemp(Capture.GetValue().Blueprint);
		return true;
	}

	/** Checks the module saved its latest translation under Output in a <Expected>_<timestamp> folder describing Expected */
	void ExpectSavedAs(const FString& Output, const FString& Expected, const FString& Other)
	{
		const FString Folder = UN2CLLMModule::Get()->GetLatestTranslationPath();
		const FString FolderName = FPaths::GetCleanFilename(Folder);
		TestTrue(TEXT("the translation is saved under the test output folder"), FPaths::IsUnderDirectory(Folder, Output));
		TestTrue(FString::Printf(TEXT("the folder (%s) is named after %s"), *FolderName, *Expected),
			FolderName.StartsWith(Expected + TEXT("_")));

		FString Json;
		// Named as SaveTranslationToDisk names it: GetBaseFilename drops the timestamp's last ".<seconds>"
		const FString JsonPath = Folder / FString::Printf(TEXT("N2C_BP_%s.json"), *FPaths::GetBaseFilename(Folder));
		const TSharedPtr<FJsonObject> Root = FFileHelper::LoadFileToString(Json, *JsonPath) ? N2CTest::ParseJsonObject(Json) : nullptr;
		const TSharedPtr<FJsonObject>* Metadata = nullptr;
		FString Name;
		if (TestTrue(TEXT("N2C_BP_*.json is written and parses"), Root.IsValid())
			&& TestTrue(TEXT("it has metadata"), Root->TryGetObjectField(TEXT("metadata"), Metadata)))
		{
			(*Metadata)->TryGetStringField(TEXT("name"), Name);
			TestEqual(TEXT("its metadata names the translated Blueprint"), Name, Expected);
		}

		TArray<FString> Found;
		IFileManager::Get().FindFiles(Found, *(Output / (Other + TEXT("_*"))), false, true);
		TestEqual(FString::Printf(TEXT("nothing is saved under %s"), *Other), Found.Num(), 0);
	}

	/**
	 * Initializes the module with Ollama at a stub, captures BP_N2CSaveA, sends it through Send (which starts the
	 * request), captures BP_N2CSaveB while the request is in flight, into the very variable that held A's capture when
	 * bReuseCapture is set, then waits for the reply and checks the save.
	 */
	void RunScenario(bool bReuseCapture,
		TFunctionRef<void(const FString& JsonA, const FN2CBlueprint& CapturedA, const FOnLLMResponseReceived& OnComplete)> Send)
	{
		FStubOllama Stub;
		if (!TestTrue(TEXT("stub endpoint started"), Stub.Start(OllamaReply(TEXT("BP_N2CSaveA")))))
		{
			return;
		}
		ON_SCOPE_EXIT { Stub.Stop(); };
		N2CTest::FScratchDir Output(TEXT("N2CTranslationSave"));
		FScopedSettings Scoped(Stub.Endpoint(), Output.Path);

		UN2CLLMModule* Module = UN2CLLMModule::Get();
		if (!TestTrue(TEXT("module initializes with Ollama"), Module->Initialize()))
		{
			return;
		}

		const TUniquePtr<N2CTest::FTestBlueprint> A = MakeBlueprint(TEXT("BP_N2CSaveA"));
		const TUniquePtr<N2CTest::FTestBlueprint> B = MakeBlueprint(TEXT("BP_N2CSaveB"));
		FN2CBlueprint CapturedA;
		if (!A->GetBlueprint() || !B->GetBlueprint() || !CaptureEventGraph(*A, CapturedA))
		{
			return;
		}
		const FString JsonA = FN2CSerializer::ToJson(CapturedA, EN2CJsonStyle::Condensed);

		const TSharedRef<int32> Completions = MakeShared<int32>(0);
		Send(JsonA, CapturedA, FOnLLMResponseReceived::CreateLambda([Completions](const FString&) { ++(*Completions); }));
		TestEqual(TEXT("the request is still in flight"), *Completions, 0);

		// Another capture before the reply, as an overlay's token estimate does when a graph tab opens or a Blueprint compiles
		FN2CBlueprint CapturedB;
		if (!CaptureEventGraph(*B, bReuseCapture ? CapturedA : CapturedB))
		{
			return;
		}
		TestEqual(TEXT("the latest capture holds BP_N2CSaveB"), (bReuseCapture ? CapturedA : CapturedB).Metadata.Name, FString(TEXT("BP_N2CSaveB")));

		Pump(15.0, [Completions] { return *Completions > 0; });
		TestEqual(TEXT("the stub answered once"), *Stub.Hits, 1);
		if (TestEqual(TEXT("the request completed once"), *Completions, 1))
		{
			TestTrue(TEXT("the module is idle after a successful translation"), Module->GetSystemStatus() == EN2CSystemStatus::Idle);
			ExpectSavedAs(Output.Path, TEXT("BP_N2CSaveA"), TEXT("BP_N2CSaveB"));
		}
	}

END_DEFINE_SPEC(FN2CTranslationSaveSpec)

void FN2CTranslationSaveSpec::Define()
{
	It(TEXT("saves with the Blueprint as it was when the request was sent, whatever the caller's copy holds later"), [this]()
	{
		// The caller's variable is reused for the next capture while the request is in flight
		RunScenario(true, [](const FString& JsonA, const FN2CBlueprint& CapturedA, const FOnLLMResponseReceived& OnComplete)
		{
			UN2CLLMModule::Get()->ProcessN2CJson(JsonA, CapturedA, OnComplete);
		});
	});

	It(TEXT("saves with the Blueprint its caller captured, whatever is captured next"), [this]()
	{
		// The translation window's, editor-integration and batch paths: each passes the capture it serialized
		RunScenario(false, [](const FString& JsonA, const FN2CBlueprint& CapturedA, const FOnLLMResponseReceived& OnComplete)
		{
			UN2CLLMModule::Get()->ProcessN2CJson(JsonA, CapturedA, OnComplete);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
