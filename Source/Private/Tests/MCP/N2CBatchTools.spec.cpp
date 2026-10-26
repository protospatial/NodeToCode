// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CToolSpecHarness.h"
#include "Tests/N2CTestBlueprintFactory.h"
#include "Tests/N2CTestHttpStub.h"
#include "Core/N2CGraphStateManager.h"
#include "Core/N2CSettings.h"
#include "Core/N2CTagManager.h"
#include "LLM/N2CBatchTranslationOrchestrator.h"
#include "LLM/N2CLLMModule.h"
#include "MCP/Tools/N2CToolCallContext.h"
#include "MCP/Utils/N2CMcpBlueprintExport.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
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
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

namespace N2CBatchToolsSpec
{
	/** A translation of graph EventGraph, as the JSON string the model writes, inside an Ollama /api/chat reply */
	FString OllamaReply()
	{
		const FString Content = TEXT("{\\\"graphs\\\":[{\\\"graph_name\\\":\\\"EventGraph\\\",\\\"graph_type\\\":\\\"EventGraph\\\",\\\"graph_class\\\":\\\"BP_Batch\\\",")
			TEXT("\\\"code\\\":{\\\"graphDeclaration\\\":\\\"\\\",\\\"graphImplementation\\\":\\\"# translated by the stub\\\",\\\"implementationNotes\\\":\\\"\\\"}}]}");
		return FString::Printf(
			TEXT("{\"model\":\"n2c-batch\",\"created_at\":\"2026-10-25T00:00:00Z\",\"message\":{\"role\":\"assistant\",\"content\":\"%s\"},")
			TEXT("\"done_reason\":\"stop\",\"done\":true,\"prompt_eval_count\":10,\"eval_count\":7}"), *Content);
	}

	/**
	 * A local Ollama endpoint (/api/chat). DelayFor(hit number, from 1): 0 answers at once, a negative value never
	 * (the request stalls until Stop). Pending answers go out from Tick.
	 */
	struct FStubOllama
	{
		struct FState
		{
			int32 Hits = 0;
			TFunction<double(int32)> DelayFor;
			TArray<FHttpResultCallback> Stalled;
		};

		uint32 Port = 0;
		TSharedPtr<IHttpRouter> Router;
		FHttpRouteHandle Route;
		TSharedRef<FState> State = MakeShared<FState>();

		bool Start(TFunction<double(int32)> DelayFor)
		{
			State->DelayFor = MoveTemp(DelayFor);
			// A port the OS picked, so a stub in another editor (another checkout's tests) never collides with this one
			Router = N2CTest::StartStubRouter(Port);
			if (!Router.IsValid())
			{
				return false;
			}
			const TSharedRef<FState> StateRef = State;
			const FString Body = OllamaReply();
			Route = Router->BindRoute(FHttpPath(TEXT("/api/chat")), EHttpServerRequestVerbs::VERB_POST,
				FHttpRequestHandler::CreateLambda([StateRef, Body](const FHttpServerRequest&, const FHttpResultCallback& OnComplete)
				{
					const int32 Hit = ++StateRef->Hits;
					if (StateRef->DelayFor && StateRef->DelayFor(Hit) < 0.0)
					{
						StateRef->Stalled.Add(OnComplete);
						return true;
					}
					OnComplete(FHttpServerResponse::Create(Body, TEXT("application/json")));
					return true;
				}));
			FHttpServerModule::Get().StartAllListeners();
			return Route.IsValid();
		}

		void Stop()
		{
			for (FHttpResultCallback& OnComplete : State->Stalled)
			{
				TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(TEXT("{\"error\":\"stub stopped\"}"), TEXT("application/json"));
				Response->Code = EHttpServerResponseCodes::ServiceUnavail;
				OnComplete(MoveTemp(Response));
			}
			State->Stalled.Reset();
			if (Router.IsValid() && Route.IsValid())
			{
				Router->UnbindRoute(Route);
			}
			Route.Reset();
		}

		FString Endpoint() const { return FString::Printf(TEXT("http://127.0.0.1:%u"), Port); }
	};

	/** Ticks the HTTP server and client until Seconds pass or Until is true */
	void Pump(double Seconds, TFunctionRef<bool()> Until)
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

	/** Ollama at the stub as the settings' provider, the translation output folder, a long activity timeout */
	struct FScopedSettings
	{
		FScopedSettings(const FString& Endpoint, const FString& Output)
		{
			UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
			SavedProvider = Settings->Provider;
			SavedEndpoint = Settings->OllamaConfig.OllamaEndpoint;
			SavedOutput = Settings->CustomTranslationOutputDirectory.Path;
			SavedTimeout = Settings->HttpActivityTimeoutSeconds;
			SavedLanguage = Settings->TargetLanguage;
			Settings->Provider = EN2CLLMProvider::Ollama;
			Settings->OllamaConfig.OllamaEndpoint = Endpoint;
			Settings->CustomTranslationOutputDirectory.Path = Output;
			Settings->HttpActivityTimeoutSeconds = 600.0f;
			Settings->TargetLanguage = EN2CCodeLanguage::Cpp;
		}
		~FScopedSettings()
		{
			UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
			Settings->Provider = SavedProvider;
			Settings->OllamaConfig.OllamaEndpoint = SavedEndpoint;
			Settings->CustomTranslationOutputDirectory.Path = SavedOutput;
			Settings->HttpActivityTimeoutSeconds = SavedTimeout;
			Settings->TargetLanguage = SavedLanguage;
		}
		EN2CLLMProvider SavedProvider = EN2CLLMProvider::Ollama;
		FString SavedEndpoint;
		FString SavedOutput;
		float SavedTimeout = 300.0f;
		EN2CCodeLanguage SavedLanguage = EN2CCodeLanguage::Cpp;
	};

	/** An Actor Blueprint with a Print String call in its event graph */
	TUniquePtr<N2CTest::FTestBlueprint> MakeBlueprint(FAutomationTestBase& Test, const FString& Name)
	{
		TUniquePtr<N2CTest::FTestBlueprint> BP = MakeUnique<N2CTest::FTestBlueprint>(Name);
		UEdGraph* Graph = BP->GetBlueprint() ? BP->EventGraph() : nullptr;
		if (Graph)
		{
			BP->AddCall(Graph, UKismetSystemLibrary::StaticClass(), GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString));
		}
		else
		{
			Test.AddError(FString::Printf(TEXT("%s has no event graph"), *Name));
		}
		for (const FString& Error : BP->Errors())
		{
			Test.AddError(FString::Printf(TEXT("%s: building the graph: %s"), *Name, *Error));
		}
		return BP;
	}

	TSharedPtr<FJsonValue> Target(const UBlueprint* Blueprint, const FString& GraphName = FString())
	{
		const TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("blueprintPath"), Blueprint->GetPathName());
		if (!GraphName.IsEmpty())
		{
			Json->SetStringField(TEXT("graphName"), GraphName);
		}
		return MakeShared<FJsonValueObject>(Json);
	}

	TSharedPtr<FJsonValue> TargetPath(const FString& Path)
	{
		const TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("blueprintPath"), Path);
		return MakeShared<FJsonValueObject>(Json);
	}

	TSharedPtr<FJsonObject> TargetsArgs(const TArray<TSharedPtr<FJsonValue>>& Targets)
	{
		const TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetArrayField(TEXT("targets"), Targets);
		return Json;
	}

	TArray<TSharedPtr<FJsonObject>> Objects(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field)
	{
		TArray<TSharedPtr<FJsonObject>> Out;
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (Json.IsValid() && Json->TryGetArrayField(Field, Array))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Array)
			{
				Out.Add(Value.IsValid() ? Value->AsObject() : nullptr);
			}
		}
		return Out;
	}

	TArray<FString> Codes(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field)
	{
		TArray<FString> Out;
		for (const TSharedPtr<FJsonObject>& Object : Objects(Json, Field))
		{
			Out.Add(Object.IsValid() ? Object->GetStringField(TEXT("code")) : FString());
		}
		return Out;
	}

	bool HasK2Node(const UEdGraph* Graph)
	{
		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			if (Cast<UK2Node>(Node))
			{
				return true;
			}
		}
		return false;
	}

	/** What a translate-batch call reported while it ran */
	struct FCallObserver
	{
		TArray<float> Progress;
		TArray<FString> Messages;
	};

	/** Starts translate-batch the way the MCP transport does: InvokeAsync with a context whose progress is recorded */
	TFuture<FMcpToolCallResult> InvokeTranslateBatch(const TSharedPtr<FJsonObject>& Args, const TSharedRef<FCallObserver>& Observer,
		FN2CToolCallContextPtr& OutContext)
	{
		const TSharedPtr<IN2CMcpTool> Tool = N2CTest::FindRegisteredTool(TEXT("translate-batch"));
		const FN2CToolCallContextRef Context = FN2CToolCallContext::Create([Observer](float Progress, const FString& Message)
		{
			Observer->Progress.Add(Progress);
			Observer->Messages.Add(Message);
		});
		OutContext = Context;
		if (!Tool.IsValid())
		{
			return MakeFulfilledPromise<FMcpToolCallResult>(FMcpToolCallResult::CreateErrorResult(TEXT("translate-batch is not registered"))).GetFuture();
		}
		return Tool->InvokeAsync(Context, Args);
	}
}

/**
 * export-n2cjson-batch and translate-batch (card 0169, review next-6): many graphs by asset path or by tag, with no
 * Blueprint editor open. The export writes one file per graph that parses, lists empty graphs and failed targets with
 * codes (a non-Blueprint asset gets UNSUPPORTED_ASSET_TYPE) and keeps its files inside the export folder. The batch
 * translation runs through UN2CBatchTranslationOrchestrator against a local stub provider (Ollama's route, never a real
 * provider) with the call's language, answers in-band with every item's status, folder and code, saves folders the
 * translation viewer loads (two EventGraphs in separate folders), reports progress, and a cancel stops the in-flight
 * request at once.
 */
BEGIN_DEFINE_SPEC(FN2CBatchToolsSpec, "NodeToCode.MCP.BatchTools", N2C_TEST_FLAGS)
	TUniquePtr<N2CTest::FScratchDir> Exports;
END_DEFINE_SPEC(FN2CBatchToolsSpec)

void FN2CBatchToolsSpec::Define()
{
	using namespace N2CBatchToolsSpec;

	Describe(TEXT("export-n2cjson-batch"), [this]()
	{
		BeforeEach([this]()
		{
			Exports = MakeUnique<N2CTest::FScratchDir>(TEXT("N2CBatchExport"));
			N2CMcpBlueprintExport::SetExportDirectoryForTests(Exports->Path);
		});
		AfterEach([this]()
		{
			N2CMcpBlueprintExport::SetExportDirectoryForTests(FString());
			Exports.Reset();
		});

		It(TEXT("writes one file per graph for two Blueprints, and each file parses as the graph's N2C JSON"), [this]()
		{
			const TUniquePtr<N2CTest::FTestBlueprint> A = MakeBlueprint(*this, TEXT("BP_N2CBatchExportA"));
			const TUniquePtr<N2CTest::FTestBlueprint> B = MakeBlueprint(*this, TEXT("BP_N2CBatchExportB"));
			const TSharedPtr<FJsonObject> Args = TargetsArgs({ Target(A->GetBlueprint(), TEXT("EventGraph")), Target(B->GetBlueprint(), TEXT("EventGraph")) });
			Args->SetStringField(TEXT("outDir"), TEXT("Spec/Run1"));

			const FMcpToolCallResult Result = N2CTest::RunTool(TEXT("export-n2cjson-batch"), Args);
			TestFalse(TEXT("not an error"), Result.bIsError);
			TestTrue(TEXT("marked as untrusted Blueprint text"), N2CTest::IsMarkedUntrusted(Result));
			const TSharedPtr<FJsonObject> Json = N2CTest::ResultJson(Result);
			if (!TestNotNull(TEXT("result JSON"), Json.Get()))
			{
				return;
			}
			TestEqual(TEXT("fileCount"), static_cast<int32>(Json->GetNumberField(TEXT("fileCount"))), 2);
			TestEqual(TEXT("no failures"), Objects(Json, TEXT("failures")).Num(), 0);
			const FString OutDir = Json->GetStringField(TEXT("outDir"));
			TestEqual(TEXT("outDir is the folder under the export folder"), FPaths::ConvertRelativePathToFull(OutDir),
				FPaths::ConvertRelativePathToFull(Exports->Path / TEXT("Spec/Run1")));

			TSet<FString> Paths;
			TSet<FString> Blueprints;
			for (const TSharedPtr<FJsonObject>& File : Objects(Json, TEXT("files")))
			{
				const FString Path = File->GetStringField(TEXT("path"));
				Paths.Add(Path);
				Blueprints.Add(File->GetStringField(TEXT("blueprintPath")));
				TestTrue(*FString::Printf(TEXT("%s is inside outDir"), *Path), FPaths::IsUnderDirectory(Path, OutDir));
				TestEqual(TEXT("graphName"), File->GetStringField(TEXT("graphName")), FString(TEXT("EventGraph")));
				TestTrue(TEXT("bytes > 0"), File->GetNumberField(TEXT("bytes")) > 0);
				FString Text;
				if (!TestTrue(*FString::Printf(TEXT("%s exists"), *Path), FFileHelper::LoadFileToString(Text, *Path)))
				{
					continue;
				}
				const TSharedPtr<FJsonObject> Document = N2CTest::ParseJsonObject(Text);
				if (!TestNotNull(*FString::Printf(TEXT("%s parses"), *Path), Document.Get()))
				{
					continue;
				}
				const TArray<TSharedPtr<FJsonObject>> Graphs = Objects(Document, TEXT("graphs"));
				TestTrue(TEXT("the document has the graph"), Graphs.Num() >= 1 && Graphs[0].IsValid()
					&& Graphs[0]->GetStringField(TEXT("name")) == TEXT("EventGraph"));
				TestTrue(TEXT("the document lists nodes"), Graphs.Num() >= 1 && Objects(Graphs[0], TEXT("nodes")).Num() > 0);
				TestEqual(TEXT("estimatedTokens"), static_cast<int32>(File->GetNumberField(TEXT("estimatedTokens"))),
					N2CMcpBlueprintExport::EstimateTokens(Text));
			}
			TestEqual(TEXT("two distinct files"), Paths.Num(), 2);
			TestTrue(TEXT("one per Blueprint"), Blueprints.Contains(A->GetBlueprint()->GetPathName()) && Blueprints.Contains(B->GetBlueprint()->GetPathName()));

			// The same export again replaces the same two files
			const TSharedPtr<FJsonObject> Again = N2CTest::ResultJson(N2CTest::RunTool(TEXT("export-n2cjson-batch"), Args));
			TArray<FString> Found;
			IFileManager::Get().FindFiles(Found, *(OutDir / TEXT("*.json")), true, false);
			TestEqual(TEXT("still two files after a second export"), Found.Num(), 2);
		});

		It(TEXT("exports every graph with nodes for a whole-Blueprint target and lists the empty ones as skipped"), [this]()
		{
			const TUniquePtr<N2CTest::FTestBlueprint> BP = MakeBlueprint(*this, TEXT("BP_N2CBatchExportWhole"));
			UBlueprint* Blueprint = BP->GetBlueprint();
			BP->AddFunction(TEXT("DoThing"));
			UEdGraph* EmptyPage = FBlueprintEditorUtils::CreateNewGraph(Blueprint, TEXT("EmptyPage"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
			Blueprint->UbergraphPages.Add(EmptyPage);

			int32 WithNodes = 0;
			for (const UEdGraph* Graph : N2CMcpBlueprintExport::GraphsToCapture(*Blueprint))
			{
				WithNodes += HasK2Node(Graph) ? 1 : 0;
			}

			const TSharedPtr<FJsonObject> Json = N2CTest::ResultJson(N2CTest::RunTool(TEXT("export-n2cjson-batch"),
				TargetsArgs({ Target(Blueprint) })));
			if (!TestNotNull(TEXT("result JSON"), Json.Get()))
			{
				return;
			}
			TestTrue(TEXT("at least the event graph and DoThing"), WithNodes >= 2);
			TestEqual(TEXT("one file per graph with nodes"), Objects(Json, TEXT("files")).Num(), WithNodes);
			TSet<FString> Names;
			for (const TSharedPtr<FJsonObject>& File : Objects(Json, TEXT("files")))
			{
				Names.Add(File->GetStringField(TEXT("graphName")));
			}
			TestTrue(TEXT("EventGraph and DoThing exported"), Names.Contains(TEXT("EventGraph")) && Names.Contains(TEXT("DoThing")));
			TestTrue(TEXT("the default Batch_ folder"), FPaths::GetCleanFilename(Json->GetStringField(TEXT("outDir"))).StartsWith(TEXT("Batch_")));
			const TArray<TSharedPtr<FJsonObject>> Skipped = Objects(Json, TEXT("skipped"));
			TestTrue(TEXT("the empty page is skipped with EMPTY_GRAPH"), Skipped.ContainsByPredicate([](const TSharedPtr<FJsonObject>& Entry)
			{
				return Entry.IsValid() && Entry->GetStringField(TEXT("graphName")) == TEXT("EmptyPage") && Entry->GetStringField(TEXT("code")) == TEXT("EMPTY_GRAPH");
			}));
		});

		It(TEXT("reports each failed target with its code, refuses non-Blueprint assets, and still exports the rest"), [this]()
		{
			const TUniquePtr<N2CTest::FTestBlueprint> BP = MakeBlueprint(*this, TEXT("BP_N2CBatchExportMixed"));
			const TSharedPtr<FJsonObject> Args = TargetsArgs({
				Target(BP->GetBlueprint(), TEXT("EventGraph")),
				Target(BP->GetBlueprint(), TEXT("NoSuchGraph")),
				TargetPath(TEXT("/Game/N2CBatchSpec/BP_DoesNotExist")),
				TargetPath(TEXT("/Engine/EngineMaterials/WorldGridMaterial")) });
			const FMcpToolCallResult Result = N2CTest::RunTool(TEXT("export-n2cjson-batch"), Args);
			TestFalse(TEXT("not an error: one file was written"), Result.bIsError);
			const TSharedPtr<FJsonObject> Json = N2CTest::ResultJson(Result);
			if (!TestNotNull(TEXT("result JSON"), Json.Get()))
			{
				return;
			}
			TestEqual(TEXT("one file"), Objects(Json, TEXT("files")).Num(), 1);
			TestEqual(*FString::Printf(TEXT("three failures with their codes (%s)"), *FString::Join(Codes(Json, TEXT("failures")), TEXT(", "))), Codes(Json, TEXT("failures")),
				TArray<FString>({ TEXT("GRAPH_NOT_FOUND"), TEXT("ASSET_NOT_FOUND"), TEXT("UNSUPPORTED_ASSET_TYPE") }));
			const TArray<TSharedPtr<FJsonObject>> Failures = Objects(Json, TEXT("failures"));
			for (const TSharedPtr<FJsonObject>& Failure : Failures)
			{
				AddInfo(Failure->GetStringField(TEXT("error")));
			}
			if (Failures.Num() == 3)
			{
				TestTrue(TEXT("the refusal names the asset's class"), Failures[2]->GetStringField(TEXT("error")).Contains(TEXT("Material")));
			}

			// Only failures: an error result that still lists them
			const FMcpToolCallResult AllFailed = N2CTest::RunTool(TEXT("export-n2cjson-batch"),
				TargetsArgs({ TargetPath(TEXT("/Engine/EngineMaterials/WorldGridMaterial")) }));
			TestTrue(TEXT("nothing exported is an error"), AllFailed.bIsError);
			TestEqual(TEXT("its failure"), Codes(N2CTest::ResultJson(AllFailed), TEXT("failures")), TArray<FString>({ TEXT("UNSUPPORTED_ASSET_TYPE") }));
		});

		It(TEXT("refuses a call without a selection, with two selections, or with an outDir outside the export folder"), [this]()
		{
			const TUniquePtr<N2CTest::FTestBlueprint> BP = MakeBlueprint(*this, TEXT("BP_N2CBatchExportArgs"));
			auto ErrorOf = [](const TSharedPtr<FJsonObject>& Args)
			{
				const FMcpToolCallResult Result = N2CTest::RunTool(TEXT("export-n2cjson-batch"), Args);
				return Result.bIsError ? N2CTest::ResultText(Result) : FString(TEXT("(not an error)"));
			};
			TestTrue(TEXT("no selection"), ErrorOf(MakeShared<FJsonObject>()).StartsWith(TEXT("INVALID_ARGUMENT")));

			const TSharedPtr<FJsonObject> Both = TargetsArgs({ Target(BP->GetBlueprint()) });
			Both->SetStringField(TEXT("tag"), TEXT("Anything"));
			TestTrue(TEXT("targets and a tag"), ErrorOf(Both).StartsWith(TEXT("INVALID_ARGUMENT")));

			TestTrue(TEXT("empty targets"), ErrorOf(TargetsArgs({})).StartsWith(TEXT("INVALID_ARGUMENT")));
			TestTrue(TEXT("a target without blueprintPath"), ErrorOf(TargetsArgs({ MakeShared<FJsonValueObject>(MakeShared<FJsonObject>()) })).StartsWith(TEXT("INVALID_ARGUMENT")));

			for (const TCHAR* OutDir : { TEXT("../Escape"), TEXT("C:/Windows"), TEXT("/abs"), TEXT("a/../../b"), TEXT("bad name") })
			{
				const TSharedPtr<FJsonObject> Args = TargetsArgs({ Target(BP->GetBlueprint()) });
				Args->SetStringField(TEXT("outDir"), OutDir);
				TestTrue(*FString::Printf(TEXT("outDir '%s' refused"), OutDir), ErrorOf(Args).StartsWith(TEXT("INVALID_ARGUMENT")));
			}
			TArray<FString> Left;
			IFileManager::Get().FindFilesRecursive(Left, *FPaths::GetPath(Exports->Path), TEXT("*.json"), true, false);
			TestFalse(TEXT("nothing written next to the export folder"), Left.ContainsByPredicate([](const FString& Path) { return Path.Contains(TEXT("Escape")); }));

			const TSharedPtr<FJsonObject> Depth = TargetsArgs({ Target(BP->GetBlueprint()) });
			Depth->SetNumberField(TEXT("depth"), 9);
			TestTrue(TEXT("depth out of range"), ErrorOf(Depth).StartsWith(TEXT("INVALID_ARGUMENT")));
		});

		It(TEXT("exports the graphs a tag names"), [this]()
		{
			const TUniquePtr<N2CTest::FTestBlueprint> BP = MakeBlueprint(*this, TEXT("BP_N2CBatchExportTagged"));
			const FString Tag = FString::Printf(TEXT("N2CBatchSpec_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
			FN2CTaggedBlueprintGraph Tagged;
			Tagged.Tag = Tag;
			Tagged.Category = TEXT("N2CBatchSpec");
			Tagged.GraphGuid = BP->EventGraph()->GraphGuid;
			Tagged.GraphName = BP->EventGraph()->GetName();
			Tagged.OwningBlueprint = FSoftObjectPath(BP->GetBlueprint());
			if (!TestTrue(TEXT("tag added"), UN2CTagManager::Get().AddTag(Tagged)))
			{
				return;
			}
			ON_SCOPE_EXIT { UN2CTagManager::Get().RemoveTag(Tagged.GraphGuid, Tag, Tagged.Category); };

			const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("tag"), Tag);
			const TSharedPtr<FJsonObject> Json = N2CTest::ResultJson(N2CTest::RunTool(TEXT("export-n2cjson-batch"), Args));
			const TArray<TSharedPtr<FJsonObject>> Files = Objects(Json, TEXT("files"));
			if (TestEqual(TEXT("one file"), Files.Num(), 1))
			{
				TestEqual(TEXT("the tagged graph"), Files[0]->GetStringField(TEXT("graphGuid")), BP->EventGraph()->GraphGuid.ToString());
				TestEqual(TEXT("with its tag"), Files[0]->GetStringField(TEXT("tag")), Tag);
			}

			const TSharedPtr<FJsonObject> Unknown = MakeShared<FJsonObject>();
			Unknown->SetStringField(TEXT("tag"), Tag + TEXT("_none"));
			const FMcpToolCallResult None = N2CTest::RunTool(TEXT("export-n2cjson-batch"), Unknown);
			TestTrue(TEXT("an unknown tag is NO_TAGGED_GRAPHS"), None.bIsError && N2CTest::ResultText(None).StartsWith(TEXT("NO_TAGGED_GRAPHS")));
		});
	});

	Describe(TEXT("translate-batch"), [this]()
	{
		It(TEXT("declares itself long-running, open world and not read-only; the export read-only"), [this]()
		{
			const TSharedPtr<IN2CMcpTool> Translate = N2CTest::FindRegisteredTool(TEXT("translate-batch"));
			const TSharedPtr<IN2CMcpTool> Export = N2CTest::FindRegisteredTool(TEXT("export-n2cjson-batch"));
			if (!TestTrue(TEXT("both registered"), Translate.IsValid() && Export.IsValid()))
			{
				return;
			}
			const FMcpToolDefinition T = Translate->GetDefinition();
			TestTrue(TEXT("long-running"), T.bIsLongRunning);
			TestTrue(TEXT("readOnlyHint false"), T.Annotations.ReadOnlyHint == TOptional<bool>(false));
			TestTrue(TEXT("openWorldHint true"), T.Annotations.OpenWorldHint == TOptional<bool>(true));
			TestTrue(TEXT("destructiveHint false"), T.Annotations.DestructiveHint == TOptional<bool>(false));
			TestEqual(TEXT("Translation category"), T.Category, FString(TEXT("Translation")));
			TestFalse(TEXT("titled"), T.Title.IsEmpty());
			const FMcpToolDefinition E = Export->GetDefinition();
			TestTrue(TEXT("export is read-only"), E.Annotations.IsReadOnly());
			TestFalse(TEXT("export is not long-running"), E.bIsLongRunning);
			TestEqual(TEXT("Blueprint Discovery category"), E.Category, FString(TEXT("Blueprint Discovery")));
		});

		It(TEXT("translates two Blueprints in-band with the call's language and saves folders the viewer loads"), [this]()
		{
			FStubOllama Stub;
			if (!TestTrue(TEXT("stub started"), Stub.Start([](int32) { return 0.0; })))
			{
				return;
			}
			ON_SCOPE_EXIT { Stub.Stop(); };
			N2CTest::FScratchDir Output(TEXT("N2CBatchTranslate"));
			FScopedSettings Scoped(Stub.Endpoint(), Output.Path);

			const TUniquePtr<N2CTest::FTestBlueprint> A = MakeBlueprint(*this, TEXT("BP_N2CBatchTranslateA"));
			const TUniquePtr<N2CTest::FTestBlueprint> B = MakeBlueprint(*this, TEXT("BP_N2CBatchTranslateB"));
			const TSharedPtr<FJsonObject> Args = TargetsArgs({ Target(A->GetBlueprint(), TEXT("EventGraph")), Target(B->GetBlueprint(), TEXT("EventGraph")) });
			Args->SetStringField(TEXT("language"), TEXT("Python"));
			ON_SCOPE_EXIT
			{
				UN2CGraphStateManager::Get().ClearTranslationState(A->EventGraph()->GraphGuid);
				UN2CGraphStateManager::Get().ClearTranslationState(B->EventGraph()->GraphGuid);
			};

			const TSharedRef<FCallObserver> Observer = MakeShared<FCallObserver>();
			FN2CToolCallContextPtr Context;
			TFuture<FMcpToolCallResult> Future = InvokeTranslateBatch(Args, Observer, Context);
			Pump(20.0, [&Future] { return Future.IsReady(); });
			if (!TestTrue(TEXT("the call finished"), Future.IsReady()))
			{
				Context->Cancel();
				Pump(2.0, [&Future] { return Future.IsReady(); });
				return;
			}
			const FMcpToolCallResult Result = Future.Get();
			TestFalse(TEXT("not an error"), Result.bIsError);
			TestTrue(TEXT("marked as untrusted"), N2CTest::IsMarkedUntrusted(Result));
			TestEqual(TEXT("two requests"), Stub.State->Hits, 2);
			const TSharedPtr<FJsonObject> Json = N2CTest::ResultJson(Result);
			if (!TestNotNull(TEXT("result JSON"), Json.Get()))
			{
				return;
			}
			TestEqual(TEXT("language"), Json->GetStringField(TEXT("language")), FString(TEXT("python")));
			TestEqual(TEXT("provider"), Json->GetStringField(TEXT("provider")), FString(TEXT("ollama")));
			TestEqual(TEXT("succeeded"), static_cast<int32>(Json->GetNumberField(TEXT("succeeded"))), 2);
			TestTrue(TEXT("code inline"), Json->GetBoolField(TEXT("codeInline")));
			TestTrue(TEXT("usage summed"), Json->GetObjectField(TEXT("usage"))->GetNumberField(TEXT("output_tokens")) == 14);

			TSet<FString> Folders;
			for (const TSharedPtr<FJsonObject>& Item : Objects(Json, TEXT("items")))
			{
				TestEqual(TEXT("completed"), Item->GetStringField(TEXT("status")), FString(TEXT("completed")));
				const FString Folder = Item->GetStringField(TEXT("outputDirectory"));
				Folders.Add(Folder);
				TestTrue(TEXT("inside the output folder"), FPaths::IsUnderDirectory(Folder, Output.Path));
				TestTrue(TEXT("the .py file is saved"), FPaths::FileExists(Folder / TEXT("EventGraph.py")));
				const TArray<TSharedPtr<FJsonValue>>* Files = nullptr;
				TestTrue(TEXT("files listed"), Item->TryGetArrayField(TEXT("files"), Files) && Files->ContainsByPredicate(
					[](const TSharedPtr<FJsonValue>& File) { return File->AsString().EndsWith(TEXT("EventGraph.py")); }));
				const TArray<TSharedPtr<FJsonObject>> Graphs = Objects(Item, TEXT("graphs"));
				TestTrue(TEXT("the code is in the result"), Graphs.Num() == 1
					&& Graphs[0]->GetObjectField(TEXT("code"))->GetStringField(TEXT("graphImplementation")) == TEXT("# translated by the stub"));

				// The translation viewer's loader reads it back, in Python
				FGuid Guid;
				FGuid::Parse(Item->GetStringField(TEXT("graphGuid")), Guid);
				FN2CGraphTranslation Loaded;
				EN2CCodeLanguage Language = EN2CCodeLanguage::Cpp;
				TestTrue(TEXT("LoadTranslation finds it"), UN2CGraphStateManager::Get().LoadTranslation(Guid, Loaded, Language));
				TestEqual(TEXT("loaded as Python"), Language, EN2CCodeLanguage::Python);
				TestEqual(TEXT("loaded code"), Loaded.Code.GraphImplementation, FString(TEXT("# translated by the stub")));
			}
			TestEqual(TEXT("two EventGraphs, two folders"), Folders.Num(), 2);

			TestTrue(TEXT("progress was reported"), Observer->Progress.Num() >= 2);
			for (int32 Index = 1; Index < Observer->Progress.Num(); ++Index)
			{
				TestTrue(TEXT("progress rises"), Observer->Progress[Index] > Observer->Progress[Index - 1]);
			}
			TestTrue(TEXT("progress names the graph"), Observer->Messages.ContainsByPredicate([](const FString& Message) { return Message.Contains(TEXT("EventGraph (2 of 2)")); }));
			TestFalse(TEXT("no batch left running"), UN2CBatchTranslationOrchestrator::Get().IsBatchInProgress());
		});

		It(TEXT("cancels the in-flight request at once, skips the rest and refuses a second batch meanwhile"), [this]()
		{
			// Stop answers the stalled connection the client already dropped; the server logs that it could not send
			AddExpectedError(TEXT("socket_send_failure"), EAutomationExpectedErrorFlags::Contains, 0);
			FStubOllama Stub;
			if (!TestTrue(TEXT("stub started"), Stub.Start([](int32 Hit) { return Hit == 1 ? 0.0 : -1.0; })))
			{
				return;
			}
			ON_SCOPE_EXIT { Stub.Stop(); };
			N2CTest::FScratchDir Output(TEXT("N2CBatchTranslate"));
			FScopedSettings Scoped(Stub.Endpoint(), Output.Path);

			const TUniquePtr<N2CTest::FTestBlueprint> A = MakeBlueprint(*this, TEXT("BP_N2CBatchCancelA"));
			const TUniquePtr<N2CTest::FTestBlueprint> B = MakeBlueprint(*this, TEXT("BP_N2CBatchCancelB"));
			const TUniquePtr<N2CTest::FTestBlueprint> C = MakeBlueprint(*this, TEXT("BP_N2CBatchCancelC"));
			const TSharedPtr<FJsonObject> Args = TargetsArgs({ Target(A->GetBlueprint(), TEXT("EventGraph")),
				Target(B->GetBlueprint(), TEXT("EventGraph")), Target(C->GetBlueprint(), TEXT("EventGraph")) });
			ON_SCOPE_EXIT { UN2CGraphStateManager::Get().ClearTranslationState(A->EventGraph()->GraphGuid); };

			const TSharedRef<FCallObserver> Observer = MakeShared<FCallObserver>();
			FN2CToolCallContextPtr Context;
			TFuture<FMcpToolCallResult> Future = InvokeTranslateBatch(Args, Observer, Context);
			Pump(15.0, [&Stub] { return Stub.State->Hits >= 2; });
			if (!TestEqual(TEXT("the second item's request is in flight"), Stub.State->Hits, 2) || !TestFalse(TEXT("still running"), Future.IsReady()))
			{
				Context->Cancel();
				Pump(2.0, [&Future] { return Future.IsReady(); });
				return;
			}

			// A second batch while this one runs is refused, not queued
			const TSharedRef<FCallObserver> SecondObserver = MakeShared<FCallObserver>();
			FN2CToolCallContextPtr SecondContext;
			TFuture<FMcpToolCallResult> Second = InvokeTranslateBatch(TargetsArgs({ Target(A->GetBlueprint(), TEXT("EventGraph")) }), SecondObserver, SecondContext);
			if (TestTrue(TEXT("the second call ends at once"), Second.IsReady()))
			{
				const FMcpToolCallResult SecondResult = Second.Get();
				TestTrue(TEXT("as BATCH_IN_PROGRESS"), SecondResult.bIsError && N2CTest::ResultText(SecondResult).Contains(TEXT("BATCH_IN_PROGRESS")));
			}

			const double CancelStart = FPlatformTime::Seconds();
			Context->Cancel(TEXT("spec cancel"));
			const double CancelTime = FPlatformTime::Seconds() - CancelStart;
			if (!TestTrue(TEXT("the call ended inside the cancel"), Future.IsReady()))
			{
				Pump(2.0, [&Future] { return Future.IsReady(); });
				return;
			}
			TestTrue(FString::Printf(TEXT("cancel took %.3f s (under 1 s)"), CancelTime), CancelTime < 1.0);
			const FMcpToolCallResult Result = Future.Get();
			TestTrue(TEXT("a cancelled result"), Result.bIsError && N2CTest::ResultText(Result) == TEXT("spec cancel"));
			TestFalse(TEXT("no batch is in progress"), UN2CBatchTranslationOrchestrator::Get().IsBatchInProgress());
			TestTrue(TEXT("the module is idle"), UN2CLLMModule::Get()->GetSystemStatus() != EN2CSystemStatus::Processing);
			const FN2CBatchTranslationResult& Batch = UN2CBatchTranslationOrchestrator::Get().GetCurrentResult();
			TestEqual(TEXT("one item done"), Batch.SuccessCount, 1);
			TestEqual(TEXT("two skipped"), Batch.SkippedCount, 2);

			Pump(1.0, [] { return false; });
			TestEqual(TEXT("no request after the cancel"), Stub.State->Hits, 2);
		});

		It(TEXT("refuses unknown IDs and selections before sending anything"), [this]()
		{
			const TUniquePtr<N2CTest::FTestBlueprint> BP = MakeBlueprint(*this, TEXT("BP_N2CBatchTranslateArgs"));
			auto Run = [this](const TSharedPtr<FJsonObject>& Args)
			{
				const TSharedRef<FCallObserver> Observer = MakeShared<FCallObserver>();
				FN2CToolCallContextPtr Context;
				TFuture<FMcpToolCallResult> Future = InvokeTranslateBatch(Args, Observer, Context);
				TestTrue(TEXT("answered at once"), Future.IsReady());
				return Future.IsReady() ? Future.Get() : FMcpToolCallResult::CreateErrorResult(TEXT("(pending)"));
			};

			const TSharedPtr<FJsonObject> Language = TargetsArgs({ Target(BP->GetBlueprint()) });
			Language->SetStringField(TEXT("language"), TEXT("COBOL"));
			const FMcpToolCallResult LanguageResult = Run(Language);
			TestTrue(TEXT("unknown language"), LanguageResult.bIsError && N2CTest::ResultText(LanguageResult).Contains(TEXT("Unknown language 'COBOL'")));

			const TSharedPtr<FJsonObject> Provider = TargetsArgs({ Target(BP->GetBlueprint()) });
			Provider->SetStringField(TEXT("provider"), TEXT("acme"));
			TestTrue(TEXT("unknown provider"), N2CTest::ResultText(Run(Provider)).Contains(TEXT("Unknown provider 'acme'")));

			TestTrue(TEXT("no selection"), N2CTest::ResultText(Run(MakeShared<FJsonObject>())).StartsWith(TEXT("INVALID_ARGUMENT")));

			const FMcpToolCallResult Material = Run(TargetsArgs({ TargetPath(TEXT("/Engine/EngineMaterials/WorldGridMaterial")) }));
			TestTrue(TEXT("only a material: an error"), Material.bIsError);
			TestEqual(TEXT("with the refusal"), Codes(N2CTest::ResultJson(Material), TEXT("failures")), TArray<FString>({ TEXT("UNSUPPORTED_ASSET_TYPE") }));
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
