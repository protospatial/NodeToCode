// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Code Editor/Models/N2CCodeEditorStyle.h"
#include "Core/N2CGraphStateManager.h"
#include "Core/N2CSettings.h"
#include "Core/N2CUserSettings.h"
#include "Core/Widgets/SN2CTranslationViewer.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Framework/Application/SlateApplication.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "Kismet/KismetSystemLibrary.h"
#include "LLM/N2CBatchTranslationOrchestrator.h"
#include "LLM/Verify/N2CVerifyRefine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Subsystems/N2CGraphStateSubsystem.h"
#include "Subsystems/N2CTranslationSubsystem.h"
#include "Tests/N2CTestBlueprintFactory.h"
#include "Tests/N2CTestHelpers.h"
#include "UObject/StrongObjectPtr.h"

/**
 * Verify and Refine (card 0313): a translation whose code the verifiers find errors in is sent back with the findings
 * (a refine turn) until it passes or Max Refine Turns are used. A fixture transport (a fake sender) returns canned
 * answers: Python with a syntax error, then valid Python. The loop is checked alone, then in a translation session of
 * UN2CTranslationSubsystem (states, the run's Verify.json, the viewer's status, Cancel) and in a batch item.
 */
BEGIN_DEFINE_SPEC(FN2CVerifyRefineSpec, "NodeToCode.Translation.VerifyRefine", N2C_TEST_FLAGS)

	/** Verify and Refine on for Python with MaxTurns, an isolated graph state, output into OutputDir, for the scope */
	struct FScopedVerify
	{
		TStrongObjectPtr<UN2CGraphStateManager> Manager;
		TArray<EN2CCodeLanguage> SavedLanguages;
		int32 SavedTurns;
		EN2CCodeLanguage SavedTarget;
		FString SavedOutput;

		FScopedVerify(int32 MaxTurns, const FString& StateDir, const FString& OutputDir)
		{
			UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
			SavedLanguages = Settings->VerifyLanguages;
			SavedTurns = Settings->MaxRefineTurns;
			SavedTarget = Settings->TargetLanguage;
			Settings->VerifyLanguages = { EN2CCodeLanguage::Python };
			Settings->MaxRefineTurns = MaxTurns;
			Settings->TargetLanguage = EN2CCodeLanguage::Python;
			SavedOutput = GetMutableDefault<UN2CUserSettings>()->CustomTranslationOutputDirectory.Path;
			GetMutableDefault<UN2CUserSettings>()->CustomTranslationOutputDirectory.Path = OutputDir;
			Manager.Reset(UN2CGraphStateManager::CreateIsolated(StateDir));
			if (UN2CGraphStateSubsystem* State = UN2CGraphStateSubsystem::Get())
			{
				State->SetManagerOverride(Manager.Get());
			}
		}

		~FScopedVerify()
		{
			if (UN2CGraphStateSubsystem* State = UN2CGraphStateSubsystem::Get())
			{
				State->SetManagerOverride(nullptr);
			}
			UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
			Settings->VerifyLanguages = SavedLanguages;
			Settings->MaxRefineTurns = SavedTurns;
			Settings->TargetLanguage = SavedTarget;
			GetMutableDefault<UN2CUserSettings>()->CustomTranslationOutputDirectory.Path = SavedOutput;
		}
	};

	/** One turn the fixture transport received */
	struct FSent
	{
		FString Json;
		FString RefineTurn;
		FN2CLLMRequestHandlePtr Handle;
		FOnN2CTranslationParsed OnParsed;
	};

	TArray<FSent> Sent;

	FN2CLLMRequestHandleRef Record(const FString& Json, const FString& RefineTurn, const FOnN2CTranslationParsed& OnParsed)
	{
		FN2CLLMRequestHandleRef Handle = FN2CLLMRequestHandle::Create();
		Sent.Add({Json, RefineTurn, Handle, OnParsed});
		return Handle;
	}

	/** The fixture's answer to turn Index (never after a cancel) */
	void Answer(int32 Index, const FN2CTranslationResponse& Response, bool bSuccess = true)
	{
		if (Sent.IsValidIndex(Index) && !Sent[Index].Handle->IsCancelled())
		{
			Sent[Index].OnParsed.ExecuteIfBound(Response, bSuccess);
		}
	}

	static FN2CTranslationResponse PythonAnswer(const FString& Graph, const FString& Code, int32 InputTokens = 100)
	{
		FN2CTranslationResponse Response;
		FN2CGraphTranslation& Translation = Response.Graphs.AddDefaulted_GetRef();
		Translation.GraphName = Graph;
		Translation.GraphType = TEXT("EventGraph");
		Translation.Code.GraphImplementation = Code;
		Response.Usage.InputTokens = InputTokens;
		Response.Usage.OutputTokens = 10;
		return Response;
	}

	static FString Broken() { return TEXT("def on_run(:\n    unreal.SystemLibrary.collect_garbage()\n"); }
	static FString Valid() { return TEXT("def on_run():\n    unreal.SystemLibrary.collect_garbage()\n"); }

	static FN2CTranslationResponse Failure()
	{
		FN2CTranslationResponse Failed;
		Failed.Error = FN2CTranslationError::Make(TEXT("N2CVerifyRefineSpec: the provider refused the refine turn"));
		return Failed;
	}

	/** A loop over an empty capture in Python with these turns; the outcome lands in Done */
	struct FRun
	{
		TSharedPtr<FN2CVerifyRefineLoop> Loop;
		bool bDone = false;
		bool bSuccess = false;
		FN2CTranslationResponse Response;
		FN2CVerifyOutcome Outcome;
		TArray<int32> RefineTurns;
	};

	TSharedRef<FRun> StartLoop(bool bVerify, int32 MaxTurns)
	{
		const TSharedRef<FRun> Run = MakeShared<FRun>();
		Run->Loop = MakeShared<FN2CVerifyRefineLoop>(MakeShared<const FN2CBlueprint>(), EN2CCodeLanguage::Python, bVerify, MaxTurns);
		Run->Loop->Start(
			[this](const FString& RefineTurn, const FOnN2CTranslationParsed& OnParsed) { return Record(TEXT("{}"), RefineTurn, OnParsed); },
			[Run](const FN2CTranslationResponse& Response, bool bSuccess, const FN2CVerifyOutcome& Outcome)
			{
				Run->bDone = true;
				Run->bSuccess = bSuccess;
				Run->Response = Response;
				Run->Outcome = Outcome;
			},
			[Run](int32 Turn, const FN2CVerifyAttempt&) { Run->RefineTurns.Add(Turn); });
		return Run;
	}

	/** An Actor Blueprint whose event graph has a custom event OnRun calling CollectGarbage, tagged */
	TUniquePtr<N2CTest::FTestBlueprint> MakeBlueprint(const FString& Name, FN2CTagInfo& OutTag)
	{
		TUniquePtr<N2CTest::FTestBlueprint> BP = MakeUnique<N2CTest::FTestBlueprint>(Name);
		UEdGraph* Graph = BP->EventGraph();
		UK2Node_CustomEvent* Event = BP->AddCustomEvent(Graph, TEXT("OnRun"));
		UK2Node_CallFunction* Call = BP->AddCall(Graph, UKismetSystemLibrary::StaticClass(),
			GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, CollectGarbage));
		TestTrue(TEXT("the test graph is wired"), Event && Call
			&& BP->Link(Event, UEdGraphSchema_K2::PN_Then, Call, UEdGraphSchema_K2::PN_Execute));
		OutTag.Tag = TEXT("N2CVerifyRefine");
		OutTag.GraphGuid = Graph->GraphGuid.ToString(EGuidFormats::DigitsWithHyphens);
		OutTag.GraphName = Graph->GetName();
		OutTag.BlueprintPath = BP->GetBlueprint()->GetPathName();
		return BP;
	}

	static FString StateName(EN2CTranslationSessionState State)
	{
		const FString Full = UEnum::GetValueAsString(State);
		return Full.RightChop(Full.Find(TEXT("::")) + 2);
	}

END_DEFINE_SPEC(FN2CVerifyRefineSpec)

void FN2CVerifyRefineSpec::Define()
{
	BeforeEach([this]() { Sent.Reset(); });

	Describe(TEXT("the loop"), [this]()
	{
		It(TEXT("sends exactly one refine turn with the diagnostics when a failing answer is followed by a passing one"), [this]()
		{
			const TSharedRef<FRun> Run = StartLoop(true, 2);
			if (!TestEqual(TEXT("the first turn was sent"), Sent.Num(), 1)) { return; }
			TestTrue(TEXT("the first turn has no refine text"), Sent[0].RefineTurn.IsEmpty());

			Answer(0, PythonAnswer(TEXT("EventGraph"), Broken()));
			if (!TestEqual(TEXT("a refine turn followed"), Sent.Num(), 2)) { return; }
			TestEqual(TEXT("OnRefine said turn 1"), Run->RefineTurns, TArray<int32>({ 1 }));
			const FString& Refine = Sent[1].RefineTurn;
			TestTrue(TEXT("it carries the previous response"), Refine.Contains(TEXT("<previous_response>")) && Refine.Contains(TEXT("def on_run(:")));
			TestTrue(TEXT("and the diagnostics"), Refine.Contains(TEXT("<verify_diagnostics>")) && Refine.Contains(TEXT("SYNTAX_ERROR")));
			TestTrue(TEXT("and asks for the same JSON shape"), Refine.Contains(TEXT("same JSON shape")) && Refine.Contains(TEXT("refine turn 1 of 2")));
			TestFalse(TEXT("not done yet"), Run->bDone);

			Answer(1, PythonAnswer(TEXT("EventGraph"), Valid(), 150));
			TestEqual(TEXT("no third turn"), Sent.Num(), 2);
			TestTrue(TEXT("done, successfully"), Run->bDone && Run->bSuccess);
			TestEqual(TEXT("status"), Run->Outcome.GetStatusString(), FString(TEXT("passed")));
			TestEqual(TEXT("one refine turn"), Run->Outcome.RefineTurns, 1);
			TestEqual(TEXT("two attempts"), Run->Outcome.Attempts.Num(), 2);
			TestFalse(TEXT("the first attempt failed"), Run->Outcome.Attempts.Num() > 0 && Run->Outcome.Attempts[0].Passed());
			TestTrue(TEXT("the answer is the corrected one"), Run->Response.Graphs.Num() == 1
				&& Run->Response.Graphs[0].Code.GraphImplementation == Valid());
			TestEqual(TEXT("both turns' input tokens are counted"), Run->Response.Usage.InputTokens, 250);
		});

		It(TEXT("stops after MaxRefineTurns with failed-after-N when every answer fails"), [this]()
		{
			const TSharedRef<FRun> Run = StartLoop(true, 2);
			for (int32 Turn = 0; Turn < 5 && Sent.IsValidIndex(Turn); ++Turn)
			{
				Answer(Turn, PythonAnswer(TEXT("EventGraph"), Broken()));
			}
			TestEqual(TEXT("the first turn and two refine turns"), Sent.Num(), 3);
			TestTrue(TEXT("done, with the last answer"), Run->bDone && Run->bSuccess);
			TestEqual(TEXT("status"), Run->Outcome.GetStatusString(), FString(TEXT("failed-after-2")));
			TestEqual(TEXT("three attempts"), Run->Outcome.Attempts.Num(), 3);
			TestTrue(TEXT("the status line counts the errors left"), Run->Outcome.GetStatusText().ToString().Contains(TEXT("left after 2 refine turns")));
		});

		It(TEXT("sends one request and checks nothing when Verify and Refine is off"), [this]()
		{
			const TSharedRef<FRun> Run = StartLoop(false, 2);
			Answer(0, PythonAnswer(TEXT("EventGraph"), Broken()));
			TestEqual(TEXT("one request"), Sent.Num(), 1);
			TestTrue(TEXT("done"), Run->bDone && Run->bSuccess);
			TestTrue(TEXT("status Off"), Run->Outcome.Status == EN2CVerifyStatus::Off);
			TestEqual(TEXT("no attempt"), Run->Outcome.Attempts.Num(), 0);
		});

		It(TEXT("stops on Cancel during a refine turn: the request is cancelled and nothing more is sent"), [this]()
		{
			const TSharedRef<FRun> Run = StartLoop(true, 2);
			Answer(0, PythonAnswer(TEXT("EventGraph"), Broken()));
			if (!TestEqual(TEXT("a refine turn is in flight"), Sent.Num(), 2)) { return; }
			TestTrue(TEXT("Cancel takes"), Run->Loop->Cancel());
			TestTrue(TEXT("the refine turn's request is cancelled"), Sent[1].Handle->IsCancelled());
			Sent[1].OnParsed.ExecuteIfBound(PythonAnswer(TEXT("EventGraph"), Valid()), true); // a late answer
			TestFalse(TEXT("OnDone never ran"), Run->bDone);
			TestEqual(TEXT("nothing more was sent"), Sent.Num(), 2);
		});

		It(TEXT("keeps the last good answer when a refine turn's request fails"), [this]()
		{
			AddExpectedMessage(TEXT("refine turn 1 failed"), EAutomationExpectedMessageFlags::Contains, 1);
			const TSharedRef<FRun> Run = StartLoop(true, 2);
			Answer(0, PythonAnswer(TEXT("EventGraph"), Broken()));
			Answer(1, Failure(), false);
			TestTrue(TEXT("done, successfully (the first answer stands)"), Run->bDone && Run->bSuccess);
			TestTrue(TEXT("status Failed"), Run->Outcome.Status == EN2CVerifyStatus::Failed);
			TestTrue(TEXT("with the refine turn's error"), Run->Outcome.Error.Contains(TEXT("refused the refine turn")));
			TestTrue(TEXT("the answer is the first"), Run->Response.Graphs.Num() == 1 && Run->Response.Graphs[0].Code.GraphImplementation == Broken());
		});

		It(TEXT("writes and reads back Verify.json"), [this]()
		{
			const TSharedRef<FRun> Run = StartLoop(true, 1);
			Answer(0, PythonAnswer(TEXT("EventGraph"), Broken()));
			Answer(1, PythonAnswer(TEXT("EventGraph"), Broken()));
			const FString Text = Run->Outcome.ToJsonString();
			FN2CVerifyOutcome Read;
			if (!TestTrue(TEXT("it reads back"), FN2CVerifyOutcome::FromJsonString(Text, Read))) { return; }
			TestEqual(TEXT("status"), Read.GetStatusString(), FString(TEXT("failed-after-1")));
			TestEqual(TEXT("attempts"), Read.Attempts.Num(), 2);
			TestTrue(TEXT("the diagnostics come back"), Read.Attempts.Num() == 2
				&& Read.Attempts[1].CountSeverity(EN2CDiagnosticSeverity::Error) == Run->Outcome.Attempts[1].CountSeverity(EN2CDiagnosticSeverity::Error)
				&& Read.Attempts[1].CountSeverity(EN2CDiagnosticSeverity::Error) > 0);
			TestTrue(TEXT("the file has every attempt's report"), Text.Contains(TEXT("\"python-syntax\"")) && Text.Contains(TEXT("\"attempts\"")));
		});
	});

	Describe(TEXT("a translation session"), [this]()
	{
		It(TEXT("refines once, records passed in the run's Verify.json, and the viewer shows it"), [this]()
		{
			UN2CTranslationSubsystem* Subsystem = UN2CTranslationSubsystem::Get();
			if (!TestNotNull(TEXT("the subsystem"), Subsystem)) { return; }
			N2CTest::FScratchDir StateDir(TEXT("N2CVerifyRefineState"));
			N2CTest::FScratchDir Output(TEXT("N2CVerifyRefineOut"));
			FScopedVerify Scoped(2, StateDir.Path, Output.Path);
			FN2CTagInfo Tag;
			TUniquePtr<N2CTest::FTestBlueprint> BP = MakeBlueprint(TEXT("BP_N2CVerifyRefineSession"), Tag);

			TArray<FString> States;
			const FDelegateHandle StatesHandle = Subsystem->OnSessionChangedNative.AddLambda([&States](const FN2CTranslationSession& Session)
			{
				States.Add(StateName(Session.Info.State));
			});
			Subsystem->SenderOverride = [this](const FString& Json, const FN2CLLMConfig&, EN2CCodeLanguage, const FString& RefineTurn,
				const FOnN2CTranslationParsed& OnParsed) { return Record(Json, RefineTurn, OnParsed); };
			ON_SCOPE_EXIT
			{
				Subsystem->SenderOverride = nullptr;
				Subsystem->OnSessionChangedNative.Remove(StatesHandle);
			};

			FN2CTranslationRequest Request;
			Request.BlueprintPath = Tag.BlueprintPath;
			Request.GraphGuid = Tag.GraphGuid;
			Request.GraphName = Tag.GraphName;
			Request.Source = EN2CTranslationSource::Row;
			const FGuid Id = Subsystem->StartTranslation(Request);
			if (!TestEqual(TEXT("the first turn was sent"), Sent.Num(), 1)) { Subsystem->Cancel(Id); return; }
			Answer(0, PythonAnswer(Tag.GraphName, Broken()));
			if (!TestEqual(TEXT("a refine turn was sent"), Sent.Num(), 2)) { Subsystem->Cancel(Id); return; }
			TestTrue(TEXT("with the graph's N2C JSON again"), Sent[1].Json == Sent[0].Json);
			TestTrue(TEXT("busy while refining"), Subsystem->IsBusy());
			Answer(1, PythonAnswer(Tag.GraphName, Valid()));

			TestEqual(TEXT("the session's states"), FString::Join(States, TEXT(", ")),
				FString(TEXT("Queued, Capturing, Sending, Refining, Succeeded")));
			TestFalse(TEXT("idle afterwards"), Subsystem->IsBusy());
			const FN2CTranslationSession* Session = Subsystem->FindSession(Id);
			if (!TestNotNull(TEXT("the session is kept"), Session)) { return; }
			TestTrue(TEXT("its status is Passed"), Session->Info.VerifyStatus == EN2CVerifyStatus::Passed);
			TestEqual(TEXT("one refine turn"), Session->Info.RefineTurns, 1);
			FString Text;
			FN2CVerifyOutcome Written;
			TestTrue(TEXT("the run has Verify.json"), FFileHelper::LoadFileToString(Text, *(Session->Info.RunFolder / TEXT("Verify.json")))
				&& FN2CVerifyOutcome::FromJsonString(Text, Written) && Written.GetStatusString() == TEXT("passed") && Written.Attempts.Num() == 2);

			if (FSlateApplication::IsInitialized())
			{
				FN2CCodeEditorStyle::Initialize();
				const TSharedRef<SN2CTranslationViewer> Viewer = SNew(SN2CTranslationViewer);
				if (TestTrue(TEXT("the viewer loads the recorded translation"), Viewer->LoadTranslation(Tag)))
				{
					TestEqual(TEXT("and shows its verify status"), Viewer->GetVerifyStatusText().ToString(),
						FString(TEXT("Verified: passed after 1 refine turn")));
				}
			}
		});

		It(TEXT("cancels while refining: the session is Cancelled and nothing is written"), [this]()
		{
			UN2CTranslationSubsystem* Subsystem = UN2CTranslationSubsystem::Get();
			if (!TestNotNull(TEXT("the subsystem"), Subsystem)) { return; }
			N2CTest::FScratchDir StateDir(TEXT("N2CVerifyRefineState"));
			N2CTest::FScratchDir Output(TEXT("N2CVerifyRefineOut"));
			FScopedVerify Scoped(2, StateDir.Path, Output.Path);
			FN2CTagInfo Tag;
			TUniquePtr<N2CTest::FTestBlueprint> BP = MakeBlueprint(TEXT("BP_N2CVerifyRefineCancel"), Tag);
			Subsystem->SenderOverride = [this](const FString& Json, const FN2CLLMConfig&, EN2CCodeLanguage, const FString& RefineTurn,
				const FOnN2CTranslationParsed& OnParsed) { return Record(Json, RefineTurn, OnParsed); };
			ON_SCOPE_EXIT { Subsystem->SenderOverride = nullptr; };

			FN2CTranslationRequest Request;
			Request.BlueprintPath = Tag.BlueprintPath;
			Request.GraphGuid = Tag.GraphGuid;
			Request.GraphName = Tag.GraphName;
			Request.Source = EN2CTranslationSource::Row;
			const FGuid Id = Subsystem->StartTranslation(Request);
			Answer(0, PythonAnswer(Tag.GraphName, Broken()));
			if (!TestEqual(TEXT("a refine turn is in flight"), Sent.Num(), 2)) { Subsystem->Cancel(Id); return; }
			TestTrue(TEXT("Cancel takes"), Subsystem->Cancel(Id));
			TestTrue(TEXT("the refine turn's request is cancelled"), Sent[1].Handle->IsCancelled());
			FN2CTranslationSessionInfo Info;
			if (TestTrue(TEXT("the session is known"), Subsystem->GetSessionInfo(Id, Info)))
			{
				TestEqual(TEXT("it is Cancelled"), StateName(Info.State), FString(TEXT("Cancelled")));
				TestTrue(TEXT("nothing was written"), Info.RunFolder.IsEmpty());
			}
			TestFalse(TEXT("idle"), Subsystem->IsBusy());
			TestEqual(TEXT("nothing more was sent"), Sent.Num(), 2);
		});
	});

	Describe(TEXT("a batch item"), [this]()
	{
		It(TEXT("refines an item and writes Verify.json in its folder and its status in BatchSummary.json"), [this]()
		{
			UN2CBatchTranslationOrchestrator& Orchestrator = UN2CBatchTranslationOrchestrator::Get();
			N2CTest::FScratchDir StateDir(TEXT("N2CVerifyRefineState"));
			N2CTest::FScratchDir Output(TEXT("N2CVerifyRefineOut"));
			FScopedVerify Scoped(2, StateDir.Path, Output.Path);
			FN2CTagInfo Tag;
			TUniquePtr<N2CTest::FTestBlueprint> BP = MakeBlueprint(TEXT("BP_N2CVerifyRefineBatch"), Tag);
			Orchestrator.SenderOverride = [this](const FString& Json, const FString& RefineTurn, const FOnN2CTranslationParsed& OnParsed)
			{
				return Record(Json, RefineTurn, OnParsed);
			};
			bool bDone = false;
			FN2CBatchTranslationResult Result;
			const FDelegateHandle Done = Orchestrator.OnBatchCompleteNative.AddLambda([&bDone, &Result](const FN2CBatchTranslationResult& In)
			{
				bDone = true;
				Result = In;
			});
			ON_SCOPE_EXIT
			{
				Orchestrator.CancelBatch();
				Orchestrator.SenderOverride = nullptr;
				Orchestrator.OnBatchCompleteNative.Remove(Done);
			};

			FString Error;
			if (!TestTrue(TEXT("the batch started"), Orchestrator.StartBatchTranslationWithOptions({ Tag }, FN2CBatchTranslationOptions(), Error))) { return; }
			Answer(0, PythonAnswer(Tag.GraphName, Broken()));
			if (!TestEqual(TEXT("a refine turn was sent"), Sent.Num(), 2)) { return; }
			Answer(1, PythonAnswer(Tag.GraphName, Valid()));
			if (!TestTrue(TEXT("the batch completed"), bDone)) { return; }
			TestEqual(TEXT("the item succeeded"), Result.SuccessCount, 1);
			const FN2CVerifyOutcome* Verify = Orchestrator.GetItemVerifyOutcome(0);
			TestTrue(TEXT("the item's outcome is passed after one turn"), Verify && Verify->Status == EN2CVerifyStatus::Passed && Verify->RefineTurns == 1);
			const TArray<FN2CBatchTranslationItem>& Items = Orchestrator.GetBatchItems();
			FString Text;
			TestTrue(TEXT("the item folder has Verify.json"), Items.Num() == 1
				&& FFileHelper::LoadFileToString(Text, *(Orchestrator.GetItemOutputDirectory(Items[0]) / TEXT("Verify.json")))
				&& Text.Contains(TEXT("\"passed\"")));
			FString Summary;
			TestTrue(TEXT("BatchSummary.json has the item's verify status"),
				FFileHelper::LoadFileToString(Summary, *(Result.BatchOutputPath / TEXT("BatchSummary.json"))) && Summary.Contains(TEXT("\"verifyStatus\"")) && Summary.Contains(TEXT("\"passed\"")));
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
