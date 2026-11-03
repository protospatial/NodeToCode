// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Code Editor/Models/N2CCodeEditorStyle.h"
#include "Core/N2CGraphStateManager.h"
#include "Core/N2CSettings.h"
#include "Core/N2CUserSettings.h"
#include "Core/Widgets/SN2CBatchProgressModal.h"
#include "Core/Widgets/SN2CMainWindow.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Framework/Application/SlateApplication.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "Kismet/KismetSystemLibrary.h"
#include "LLM/N2CBatchTranslationOrchestrator.h"
#include "Misc/Paths.h"
#include "Subsystems/N2CGraphStateSubsystem.h"
#include "Subsystems/N2CTranslationSubsystem.h"
#include "Tests/N2CTestBlueprintFactory.h"
#include "Tests/N2CTestHelpers.h"
#include "UObject/StrongObjectPtr.h"

/**
 * Batch translation as a session of UN2CTranslationSubsystem (card 0255). Before, the Node to Code window started the
 * orchestrator itself, its Cancel and Escape reset the dialog while the request kept running (and was billed), and
 * nothing owned the batch once the window closed. Now StartBatch starts it and says why when it cannot, the subsystem
 * holds the busy state and ends the session on the orchestrator's completion with or without a window, and Cancel
 * stops the in-flight request and skips the rest. A fake sender (the orchestrator's SenderOverride, and the
 * subsystem's for single sessions) stands in for the provider; the test delivers each result when it chooses. Results
 * go to a scratch output folder and an isolated graph state.
 */
BEGIN_DEFINE_SPEC(FN2CBatchSessionSpec, "NodeToCode.Editor.BatchSession", N2C_TEST_FLAGS)

	/** An isolated graph state manager in the graph state subsystem, C++ output into OutputDir, for the scope */
	struct FScopedState
	{
		TStrongObjectPtr<UN2CGraphStateManager> Manager;
		EN2CCodeLanguage SavedLanguage;
		FString SavedOutput;

		FScopedState(const FString& StateDir, const FString& OutputDir)
		{
			Manager.Reset(UN2CGraphStateManager::CreateIsolated(StateDir));
			if (UN2CGraphStateSubsystem* State = UN2CGraphStateSubsystem::Get())
			{
				State->SetManagerOverride(Manager.Get());
			}
			UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
			SavedLanguage = Settings->TargetLanguage;
			SavedOutput = GetMutableDefault<UN2CUserSettings>()->CustomTranslationOutputDirectory.Path;
			Settings->TargetLanguage = EN2CCodeLanguage::Cpp;
			GetMutableDefault<UN2CUserSettings>()->CustomTranslationOutputDirectory.Path = OutputDir;
		}

		~FScopedState()
		{
			if (UN2CGraphStateSubsystem* State = UN2CGraphStateSubsystem::Get())
			{
				State->SetManagerOverride(nullptr);
			}
			GetMutableDefault<UN2CSettings>()->TargetLanguage = SavedLanguage;
			GetMutableDefault<UN2CUserSettings>()->CustomTranslationOutputDirectory.Path = SavedOutput;
		}
	};

	/** One request the fake service received */
	struct FSent
	{
		FString Json;
		FN2CLLMRequestHandlePtr Handle;
		FOnN2CTranslationParsed OnParsed;
	};

	UN2CTranslationSubsystem* Subsystem = nullptr;
	TArray<FSent> Sent;
	TArray<EN2CTranslationSessionState> BatchStates;
	TArray<bool> BusyChanges;
	FDelegateHandle StatesHandle;
	FDelegateHandle BusyHandle;

	/** Blueprints built in code, each with an event graph whose custom event calls CollectGarbage */
	TArray<TUniquePtr<N2CTest::FTestBlueprint>> Blueprints;

	/** A tagged graph of a new test Blueprint */
	FN2CTagInfo MakeTagged(const FString& Name)
	{
		TUniquePtr<N2CTest::FTestBlueprint> BP = MakeUnique<N2CTest::FTestBlueprint>(Name);
		UEdGraph* Graph = BP->EventGraph();
		UK2Node_CustomEvent* Event = BP->AddCustomEvent(Graph, TEXT("OnRun"));
		UK2Node_CallFunction* Call = BP->AddCall(Graph, UKismetSystemLibrary::StaticClass(),
			GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, CollectGarbage));
		TestTrue(FString::Printf(TEXT("%s is wired"), *Name), Event && Call
			&& BP->Link(Event, UEdGraphSchema_K2::PN_Then, Call, UEdGraphSchema_K2::PN_Execute));
		FN2CTagInfo Tag;
		Tag.Tag = TEXT("N2CBatchSession");
		Tag.GraphGuid = Graph->GraphGuid.ToString(EGuidFormats::DigitsWithHyphens);
		Tag.GraphName = Graph->GetName();
		Tag.BlueprintPath = BP->GetBlueprint()->GetPathName();
		Blueprints.Add(MoveTemp(BP));
		return Tag;
	}

	TArray<FN2CTagInfo> MakeBatch(int32 Count)
	{
		TArray<FN2CTagInfo> Tags;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Tags.Add(MakeTagged(FString::Printf(TEXT("BP_N2CBatchSession%c"), TEXT('A') + Index)));
		}
		return Tags;
	}

	static FN2CTranslationResponse MakeResponse()
	{
		FN2CTranslationResponse Response;
		FN2CGraphTranslation& Translation = Response.Graphs.AddDefaulted_GetRef();
		Translation.GraphName = TEXT("EventGraph");
		Translation.GraphType = TEXT("EventGraph");
		Translation.Code.GraphDeclaration = TEXT("void OnRun();");
		Translation.Code.GraphImplementation = TEXT("void ABatchTest::OnRun()\n{\n}\n");
		Response.Usage.InputTokens = 100;
		Response.Usage.OutputTokens = 20;
		return Response;
	}

	/** The provider's answer to request Index, as the HTTP completion would deliver it (never after a cancel) */
	void Deliver(int32 Index)
	{
		if (Sent.IsValidIndex(Index) && !Sent[Index].Handle->IsCancelled())
		{
			Sent[Index].OnParsed.ExecuteIfBound(MakeResponse(), true);
		}
	}

	static FString Name(EN2CTranslationSessionState State)
	{
		const FString Full = UEnum::GetValueAsString(State);
		return Full.RightChop(Full.Find(TEXT("::")) + 2);
	}

	FString StatesText() const
	{
		TArray<FString> Names;
		for (const EN2CTranslationSessionState State : BatchStates)
		{
			Names.Add(Name(State));
		}
		return FString::Join(Names, TEXT(", "));
	}

	FString BusyText() const
	{
		TArray<FString> Names;
		for (const bool bBusy : BusyChanges)
		{
			Names.Add(bBusy ? TEXT("busy") : TEXT("idle"));
		}
		return FString::Join(Names, TEXT(", "));
	}

	static FString ItemStatuses()
	{
		TArray<FString> Names;
		for (const FN2CBatchTranslationItem& Item : UN2CBatchTranslationOrchestrator::Get().GetBatchItems())
		{
			const FString Full = UEnum::GetValueAsString(Item.Status);
			Names.Add(Full.RightChop(Full.Find(TEXT("::")) + 2));
		}
		return FString::Join(Names, TEXT(", "));
	}

	static FString RowStatuses(const SN2CBatchProgressModal& Modal)
	{
		TArray<FString> Names;
		for (const TSharedPtr<FN2CBatchProgressItem>& Item : Modal.GetItems())
		{
			Names.Add(SN2CBatchProgressModal::GetStatusLabel(Item->Status).ToString());
		}
		return FString::Join(Names, TEXT(", "));
	}

	bool SlateReady()
	{
		if (!FSlateApplication::IsInitialized())
		{
			AddInfo(TEXT("Slate is not initialized; skipping"));
			return false;
		}
		FN2CCodeEditorStyle::Initialize(); // no-op when the module already did it
		return true;
	}

END_DEFINE_SPEC(FN2CBatchSessionSpec)

void FN2CBatchSessionSpec::Define()
{
	BeforeEach([this]()
	{
		Subsystem = UN2CTranslationSubsystem::Get();
		if (!TestNotNull(TEXT("the translation subsystem exists"), Subsystem))
		{
			return;
		}
		TestFalse(TEXT("nothing runs before the test"), Subsystem->IsBusy());
		Sent.Reset();
		BatchStates.Reset();
		BusyChanges.Reset();
		Blueprints.Reset();
		auto Record = [this](const FString& Json, const FOnN2CTranslationParsed& OnParsed)
		{
			FN2CLLMRequestHandleRef Handle = FN2CLLMRequestHandle::Create();
			Sent.Add({Json, Handle, OnParsed});
			return Handle;
		};
		UN2CBatchTranslationOrchestrator::Get().SenderOverride = Record;
		Subsystem->SenderOverride = [Record](const FString& Json, const FN2CLLMConfig&, EN2CCodeLanguage, const FOnN2CTranslationParsed& OnParsed)
		{
			return Record(Json, OnParsed);
		};
		StatesHandle = Subsystem->OnSessionChangedNative.AddLambda([this](const FN2CTranslationSession& Session)
		{
			if (Session.Info.Source == EN2CTranslationSource::Batch)
			{
				BatchStates.Add(Session.Info.State);
			}
		});
		BusyHandle = Subsystem->OnBusyChanged.AddLambda([this](bool bBusy) { BusyChanges.Add(bBusy); });
	});

	AfterEach([this]()
	{
		if (Subsystem)
		{
			// Nothing may stay running into the next spec
			if (Subsystem->GetActiveBatchSession().IsValid())
			{
				Subsystem->Cancel(Subsystem->GetActiveBatchSession());
			}
			UN2CBatchTranslationOrchestrator::Get().CancelBatch();
			UN2CBatchTranslationOrchestrator::Get().SenderOverride = nullptr;
			Subsystem->SenderOverride = nullptr;
			Subsystem->OnSessionChangedNative.Remove(StatesHandle);
			Subsystem->OnBusyChanged.Remove(BusyHandle);
		}
		Blueprints.Reset();
	});

	It(TEXT("refuses a batch of missing Blueprints with the reason and leaves IsBusy() false"), [this]()
	{
		if (!Subsystem)
		{
			return;
		}
		AddExpectedMessage(TEXT("BP_N2CBatchMissing"), EAutomationExpectedMessageFlags::Contains, 0);
		AddExpectedMessage(TEXT("No valid items to process in batch"), EAutomationExpectedMessageFlags::Contains, 0);
		AddExpectedMessage(TEXT("Some items could not be resolved"), EAutomationExpectedMessageFlags::Contains, 0);
		FN2CTagInfo Missing;
		Missing.GraphName = TEXT("EventGraph");
		Missing.GraphGuid = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
		Missing.BlueprintPath = TEXT("/Game/N2CTestMissing/BP_N2CBatchMissing.BP_N2CBatchMissing");

		FString Error;
		const FGuid Id = Subsystem->StartBatch({ Missing, Missing }, FN2CBatchTranslationOptions(), &Error);
		TestFalse(TEXT("the batch did not start"), Id.IsValid());
		TestTrue(FString::Printf(TEXT("and says why (%s)"), *Error), Error.StartsWith(TEXT("NO_VALID_ITEMS")));
		TestTrue(TEXT("naming the Blueprint"), Error.Contains(TEXT("BP_N2CBatchMissing")));
		TestFalse(TEXT("not busy"), Subsystem->IsBusy());
		TestFalse(TEXT("no batch session is running"), Subsystem->GetActiveBatchSession().IsValid());
		TestEqual(TEXT("no session was broadcast"), StatesText(), FString());
		TestEqual(TEXT("busy never changed"), BusyText(), FString());
		TestEqual(TEXT("nothing was sent"), Sent.Num(), 0);
	});

	It(TEXT("refuses a batch while a single translation runs"), [this]()
	{
		if (!Subsystem)
		{
			return;
		}
		AddExpectedMessage(TEXT("TRANSLATION_IN_PROGRESS"), EAutomationExpectedMessageFlags::Contains, 0);
		const TArray<FN2CTagInfo> Tags = MakeBatch(1);
		FN2CTranslationRequest Request;
		Request.BlueprintPath = Tags[0].BlueprintPath;
		Request.GraphGuid = Tags[0].GraphGuid;
		Request.GraphName = Tags[0].GraphName;
		Request.Source = EN2CTranslationSource::Row;
		const FGuid Single = Subsystem->StartTranslation(Request);
		TestTrue(TEXT("the single translation started"), Single.IsValid());

		FString Error;
		TestFalse(TEXT("the batch did not start"), Subsystem->StartBatch(Tags, FN2CBatchTranslationOptions(), &Error).IsValid());
		TestTrue(TEXT("and says why"), Error.StartsWith(TEXT("TRANSLATION_IN_PROGRESS")));
		TestFalse(TEXT("the orchestrator did not start one"), UN2CBatchTranslationOrchestrator::Get().IsBatchInProgress());
		TestEqual(TEXT("one request (the single one)"), Sent.Num(), 1);
		Subsystem->Cancel(Single);
		TestFalse(TEXT("not busy once it is cancelled"), Subsystem->IsBusy());
	});

	It(TEXT("ends the batch session and clears IsBusy() after the window that started it closed"), [this]()
	{
		if (!Subsystem || !SlateReady())
		{
			return;
		}
		N2CTest::FScratchDir StateDir(TEXT("N2CBatchSessionState"));
		N2CTest::FScratchDir Output(TEXT("N2CBatchSessionOut"));
		FScopedState Scoped(StateDir.Path, Output.Path);
		const TArray<FN2CTagInfo> Tags = MakeBatch(2);

		TSharedPtr<SN2CMainWindow> Window = SNew(SN2CMainWindow);
		Window->StartBatchTranslation(Tags);
		const FGuid Id = Subsystem->GetActiveBatchSession();
		if (!TestTrue(TEXT("a batch session runs"), Id.IsValid()) || !TestEqual(TEXT("the first item was sent"), Sent.Num(), 1))
		{
			return;
		}
		TestTrue(TEXT("the window shows the progress"), Window->IsBatchProgressVisible());
		TestTrue(TEXT("busy while the batch runs"), Subsystem->IsBusy());
		FN2CTranslationSessionInfo Info;
		if (TestTrue(TEXT("the session is known"), Subsystem->GetSessionInfo(Id, Info)))
		{
			TestTrue(TEXT("it is a batch"), Info.Source == EN2CTranslationSource::Batch);
			TestEqual(TEXT("of two graphs"), Info.ItemCount, 2);
			TestEqual(TEXT("it is sending"), Name(Info.State), Name(EN2CTranslationSessionState::Sending));
		}

		// The user closes the window mid-batch; the batch runs on and ends without it
		Window.Reset();
		Deliver(0);
		if (!TestEqual(TEXT("the second item was sent"), Sent.Num(), 2))
		{
			return;
		}
		Deliver(1);

		TestFalse(TEXT("not busy after the batch"), Subsystem->IsBusy());
		TestFalse(TEXT("no batch session is running"), Subsystem->GetActiveBatchSession().IsValid());
		TestEqual(TEXT("the batch session's states"), StatesText(), FString(TEXT("Queued, Sending, Succeeded")));
		TestEqual(TEXT("busy was broadcast on and off once"), BusyText(), FString(TEXT("busy, idle")));
		const FN2CTranslationSession* Session = Subsystem->FindSession(Id);
		if (TestNotNull(TEXT("the session is kept"), Session))
		{
			TestEqual(TEXT("both items succeeded"), Session->BatchResult.SuccessCount, 2);
			TestFalse(TEXT("not cancelled"), Session->BatchResult.bCancelled);
			TestTrue(TEXT("the run folder is under the output folder"), FPaths::IsUnderDirectory(Session->Info.RunFolder, Output.Path));
		}
	});

	It(TEXT("cancels a batch during an in-flight request: Cancelled within 1 s, the rest Skipped, a late result dropped"), [this]()
	{
		if (!Subsystem)
		{
			return;
		}
		N2CTest::FScratchDir StateDir(TEXT("N2CBatchSessionState"));
		N2CTest::FScratchDir Output(TEXT("N2CBatchSessionOut"));
		FScopedState Scoped(StateDir.Path, Output.Path);
		const TArray<FN2CTagInfo> Tags = MakeBatch(3);

		const FGuid Id = Subsystem->StartBatch(Tags);
		if (!TestTrue(TEXT("the batch started"), Id.IsValid()))
		{
			return;
		}
		Deliver(0);
		if (!TestEqual(TEXT("the second item's request is in flight"), Sent.Num(), 2))
		{
			return;
		}

		const double CancelStart = FPlatformTime::Seconds();
		TestTrue(TEXT("Cancel takes the batch"), Subsystem->Cancel(Id));
		const double CancelTime = FPlatformTime::Seconds() - CancelStart;
		TestTrue(FString::Printf(TEXT("cancel took %.3f s (under 1 s)"), CancelTime), CancelTime < 1.0);

		FN2CTranslationSessionInfo Info;
		if (TestTrue(TEXT("the session is known"), Subsystem->GetSessionInfo(Id, Info)))
		{
			TestEqual(TEXT("it ended Cancelled"), Name(Info.State), Name(EN2CTranslationSessionState::Cancelled));
		}
		TestTrue(TEXT("the in-flight request was cancelled"), Sent[1].Handle->IsCancelled());
		TestFalse(TEXT("not busy once cancelled"), Subsystem->IsBusy());
		TestEqual(TEXT("busy was broadcast on and off once"), BusyText(), FString(TEXT("busy, idle")));
		TestEqual(TEXT("the items: one done, the in-flight and the last skipped"), ItemStatuses(), FString(TEXT("Completed, Skipped, Skipped")));
		const FN2CTranslationSession* Session = Subsystem->FindSession(Id);
		if (TestNotNull(TEXT("the session is kept"), Session))
		{
			TestTrue(TEXT("its summary says cancelled"), Session->BatchResult.bCancelled);
			TestEqual(TEXT("one succeeded"), Session->BatchResult.SuccessCount, 1);
			TestEqual(TEXT("two skipped"), Session->BatchResult.SkippedCount, 2);
		}
		TestFalse(TEXT("a second Cancel does nothing"), Subsystem->Cancel(Id));

		// The cancelled request's late completion is dropped, and the third item is never sent
		Sent[1].OnParsed.ExecuteIfBound(MakeResponse(), true);
		TestEqual(TEXT("the items are unchanged"), ItemStatuses(), FString(TEXT("Completed, Skipped, Skipped")));
		TestEqual(TEXT("no request after the cancel"), Sent.Num(), 2);
		if (TestTrue(TEXT("the session is still known"), Subsystem->GetSessionInfo(Id, Info)))
		{
			TestEqual(TEXT("it stays Cancelled"), Name(Info.State), Name(EN2CTranslationSessionState::Cancelled));
		}
	});

	It(TEXT("shows Cancelling... in the window until the cancelled completion, then the summary with Skipped rows"), [this]()
	{
		if (!Subsystem || !SlateReady())
		{
			return;
		}
		N2CTest::FScratchDir StateDir(TEXT("N2CBatchSessionState"));
		N2CTest::FScratchDir Output(TEXT("N2CBatchSessionOut"));
		FScopedState Scoped(StateDir.Path, Output.Path);
		const TArray<FN2CTagInfo> Tags = MakeBatch(3);

		const TSharedRef<SN2CMainWindow> Window = SNew(SN2CMainWindow);
		Window->StartBatchTranslation(Tags);
		const FGuid Id = Subsystem->GetActiveBatchSession();
		const TSharedPtr<SN2CBatchProgressModal> Modal = Window->GetBatchProgressModal();
		if (!TestTrue(TEXT("a batch session runs"), Id.IsValid()) || !TestTrue(TEXT("the dialog exists"), Modal.IsValid())
			|| !TestEqual(TEXT("the first item was sent"), Sent.Num(), 1))
		{
			return;
		}

		// The first item's response is being handled when Cancel is clicked: its request can no longer be cancelled
		Sent[0].Handle->Complete(FOnLLMResponseReceived(), FString());
		Window->CancelOrCloseProgress();
		TestTrue(TEXT("the dialog is still up"), Window->IsBatchProgressVisible());
		TestTrue(TEXT("it says Cancelling..."), Modal->IsCancelling() && Modal->GetStatusText().ToString() == TEXT("Cancelling..."));
		TestTrue(TEXT("busy until the batch ends"), Subsystem->IsBusy());
		Window->CancelOrCloseProgress();
		TestTrue(TEXT("a second click waits too"), Window->IsBatchProgressVisible() && Modal->IsCancelling());

		// The first item's result lands; the batch then ends Cancelled with the rest skipped
		Sent[0].OnParsed.ExecuteIfBound(MakeResponse(), true);
		TestEqual(TEXT("nothing more was sent"), Sent.Num(), 1);
		TestFalse(TEXT("not busy"), Subsystem->IsBusy());
		FN2CTranslationSessionInfo Info;
		if (TestTrue(TEXT("the session is known"), Subsystem->GetSessionInfo(Id, Info)))
		{
			TestEqual(TEXT("it ended Cancelled"), Name(Info.State), Name(EN2CTranslationSessionState::Cancelled));
		}
		TestTrue(TEXT("the dialog shows the summary"), Modal->IsComplete() && !Modal->IsCancelling());
		TestTrue(FString::Printf(TEXT("which says cancelled (%s)"), *Modal->GetStatusText().ToString()),
			Modal->GetStatusText().ToString().StartsWith(TEXT("Cancelled - 1 succeeded, 0 failed, 2 skipped")));
		TestEqual(TEXT("the rows"), RowStatuses(*Modal), FString(TEXT("Completed, Skipped (cancelled), Skipped (cancelled)")));

		// Close (the same button, or Escape)
		Window->CancelOrCloseProgress();
		TestFalse(TEXT("Close hides the dialog"), Window->IsBatchProgressVisible());
	});

	It(TEXT("takes the dialog down when the window's batch cannot start"), [this]()
	{
		if (!Subsystem || !SlateReady())
		{
			return;
		}
		AddExpectedMessage(TEXT("BP_N2CBatchMissing"), EAutomationExpectedMessageFlags::Contains, 0);
		AddExpectedMessage(TEXT("No valid items to process in batch"), EAutomationExpectedMessageFlags::Contains, 0);
		AddExpectedMessage(TEXT("Some items could not be resolved"), EAutomationExpectedMessageFlags::Contains, 0);
		FN2CTagInfo Missing;
		Missing.GraphName = TEXT("EventGraph");
		Missing.GraphGuid = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
		Missing.BlueprintPath = TEXT("/Game/N2CTestMissing/BP_N2CBatchMissing.BP_N2CBatchMissing");

		const TSharedRef<SN2CMainWindow> Window = SNew(SN2CMainWindow);
		Window->StartBatchTranslation({ Missing });
		TestFalse(TEXT("the dialog is not left up"), Window->IsBatchProgressVisible());
		TestFalse(TEXT("not busy"), Subsystem->IsBusy());
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
