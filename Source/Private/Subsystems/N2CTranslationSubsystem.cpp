// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Subsystems/N2CTranslationSubsystem.h"

#include "Async/Async.h"
#include "Containers/Ticker.h"
#include "Core/N2CEditorIntegration.h"
#include "Core/N2CEditorWindow.h"
#include "Core/N2CGraphStateManager.h"
#include "Core/N2CSettings.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Notifications/NotificationManager.h"
#include "LLM/N2CBatchTranslationOrchestrator.h"
#include "LLM/N2CLLMModule.h"
#include "Utils/N2CGraphUtils.h"
#include "Utils/N2CLogger.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "N2CTranslationSubsystem"

namespace N2CTranslationSubsystemPrivate
{
	static const TCHAR* StateName(EN2CTranslationSessionState State)
	{
		switch (State)
		{
		case EN2CTranslationSessionState::Queued: return TEXT("Queued");
		case EN2CTranslationSessionState::Capturing: return TEXT("Capturing");
		case EN2CTranslationSessionState::Sending: return TEXT("Sending");
		case EN2CTranslationSessionState::Succeeded: return TEXT("Succeeded");
		case EN2CTranslationSessionState::Failed: return TEXT("Failed");
		case EN2CTranslationSessionState::Cancelled: return TEXT("Cancelled");
		}
		return TEXT("Unknown");
	}

	static const TCHAR* SourceName(EN2CTranslationSource Source)
	{
		switch (Source)
		{
		case EN2CTranslationSource::Overlay: return TEXT("Overlay");
		case EN2CTranslationSource::Row: return TEXT("Row");
		case EN2CTranslationSource::Batch: return TEXT("Batch");
		case EN2CTranslationSource::Mcp: return TEXT("Mcp");
		case EN2CTranslationSource::Python: return TEXT("Python");
		}
		return TEXT("Unknown");
	}

	/** The editor's own Translate buttons: their sessions get a notification or the window's progress */
	static bool IsEditorSource(EN2CTranslationSource Source)
	{
		return Source == EN2CTranslationSource::Overlay || Source == EN2CTranslationSource::Row;
	}

	/** Seconds a finished notification stays before it fades */
	static constexpr float FinishedNotificationSeconds = 10.0f;
}

FN2CTagInfo FN2CTranslationSession::GetGraph() const
{
	FN2CTagInfo Graph;
	Graph.GraphGuid = Info.GraphGuid;
	Graph.GraphName = Info.GraphName;
	Graph.BlueprintPath = Info.BlueprintPath;
	return Graph;
}

const FN2CGraphTranslation* FN2CTranslationSession::GetRootTranslation() const
{
	const FN2CArchivedEntry* Root = Saved.GetRoot();
	if (Root && Response.Graphs.IsValidIndex(Root->ResponseIndex))
	{
		return &Response.Graphs[Root->ResponseIndex];
	}
	return Response.Graphs.Num() > 0 ? &Response.Graphs[0] : nullptr;
}

void UN2CTranslationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// A batch is part of "busy": the N2C bars grey out while one runs. Its first item's progress marks the start; its
	// completion is broadcast before the orchestrator clears its flag, so that one says the batch ended.
	UN2CBatchTranslationOrchestrator& Orchestrator = UN2CBatchTranslationOrchestrator::Get();
	TWeakObjectPtr<UN2CTranslationSubsystem> WeakThis(this);
	BatchProgressHandle = Orchestrator.OnProgressNative.AddLambda([WeakThis](int32, int32, const FString&)
	{
		if (UN2CTranslationSubsystem* This = WeakThis.Get())
		{
			This->UpdateBusy();
		}
	});
	BatchCompleteHandle = Orchestrator.OnBatchCompleteNative.AddLambda([WeakThis](const FN2CBatchTranslationResult&)
	{
		if (UN2CTranslationSubsystem* This = WeakThis.Get())
		{
			This->UpdateBusy(/*bBatchEnded=*/true);
		}
	});
}

void UN2CTranslationSubsystem::Deinitialize()
{
	// Nothing may call back into a subsystem that is gone: unfinished requests are cancelled
	for (const TSharedRef<FN2CTranslationSession>& Session : Sessions)
	{
		if (Session->Handle.IsValid())
		{
			Session->Handle->Cancel();
			Session->Handle.Reset();
		}
	}
	Sessions.Reset();
	Notifications.Reset();

	// At editor exit the rooted orchestrator may already be purged; its lambdas hold this weakly, so leaving them is safe
	if (!IsEngineExitRequested())
	{
		UN2CBatchTranslationOrchestrator& Orchestrator = UN2CBatchTranslationOrchestrator::Get();
		Orchestrator.OnProgressNative.Remove(BatchProgressHandle);
		Orchestrator.OnBatchCompleteNative.Remove(BatchCompleteHandle);
	}
	Super::Deinitialize();
}

UN2CTranslationSubsystem* UN2CTranslationSubsystem::Get()
{
	return GEditor ? GEditor->GetEditorSubsystem<UN2CTranslationSubsystem>() : nullptr;
}

FGuid UN2CTranslationSubsystem::StartTranslation(const FN2CTranslationRequest& Request, FString* OutError)
{
	using namespace N2CTranslationSubsystemPrivate;
	check(IsInGameThread());

	if (IsBusy())
	{
		const FString Error = TEXT("TRANSLATION_IN_PROGRESS: another translation is running; wait for it or cancel it");
		FN2CLogger::Get().LogWarning(Error, TEXT("TranslationSubsystem"));
		if (OutError)
		{
			*OutError = Error;
		}
		return FGuid();
	}

	// The snapshot the whole session uses: settings changed while it runs apply to the next one
	const UN2CSettings* Settings = GetDefault<UN2CSettings>();
	TSharedRef<FN2CTranslationSession> Session = MakeShared<FN2CTranslationSession>();
	Session->Config = Request.Config.IsSet() ? Request.Config.GetValue() : UN2CLLMModule::MakeConfigFromSettings();
	FN2CTranslationSessionInfo& Info = Session->Info;
	Info.SessionId = FGuid::NewGuid();
	Info.Source = Request.Source;
	Info.GraphName = Request.GraphName;
	Info.BlueprintPath = Request.BlueprintPath;
	Info.GraphGuid = Request.GraphGuid;
	if (Info.GraphName.IsEmpty() && Request.Capture.IsValid())
	{
		Info.GraphName = Request.Capture->Metadata.Name;
	}
	Info.Language = Request.Language.Get(Settings ? Settings->TargetLanguage : EN2CCodeLanguage::Cpp);
	Info.Provider = UN2CLLMModule::GetProviderName(Session->Config.Provider);
	Info.Model = Session->Config.Model;

	const FGuid SessionId = Info.SessionId;
	Sessions.Add(Session);
	FN2CLogger::Get().Log(FString::Printf(TEXT("Translation session %s started (%s): %s, %s %s, %s"),
		*SessionId.ToString(), SourceName(Request.Source), *FN2CGraphUtils::DescribeGraph(Info.GraphName, Info.BlueprintPath),
		*Info.Provider, *Info.Model, *UEnum::GetValueAsString(Info.Language)), EN2CLogSeverity::Info, TEXT("TranslationSubsystem"));

	SetState(*Session, EN2CTranslationSessionState::Queued);
	RunSession(SessionId, Request);
	PruneSessions();
	return SessionId;
}

void UN2CTranslationSubsystem::RunSession(const FGuid& SessionId, const FN2CTranslationRequest& Request)
{
	TSharedPtr<FN2CTranslationSession> Session;
	for (const TSharedRef<FN2CTranslationSession>& Candidate : Sessions)
	{
		if (Candidate->Info.SessionId == SessionId)
		{
			Session = Candidate;
		}
	}
	if (!Session.IsValid() || Session->Info.IsFinished())
	{
		return;
	}

	// Capture: the caller's capture, or the tagged graph resolved (every graph of the Blueprint) and captured as the
	// MCP tools capture it. A missing or refused graph fails the session with the reason.
	SetState(*Session, EN2CTranslationSessionState::Capturing);
	FString Json;
	TSharedPtr<const FN2CBlueprint> Capture = Request.Capture;
	if (Capture.IsValid())
	{
		Json = FN2CEditorIntegration::Get().SerializeN2CBlueprintToJson(*Capture, /*bPrettyPrint=*/false);
		if (Json.IsEmpty())
		{
			SetState(*Session, EN2CTranslationSessionState::Failed, TEXT("The capture could not be serialized to N2C JSON."));
			return;
		}
	}
	else
	{
		UBlueprint* Blueprint = nullptr;
		FString Error;
		UEdGraph* Graph = FN2CGraphUtils::ResolveTaggedGraph(Request.BlueprintPath, Request.GraphGuid, Request.GraphName, Blueprint, Error);
		if (Graph && !FN2CGraphUtils::CanTranslateGraph(Graph, &Error))
		{
			Graph = nullptr;
		}
		if (!Graph)
		{
			SetState(*Session, EN2CTranslationSessionState::Failed, Error);
			return;
		}
		FN2CBlueprint Captured;
		Json = FN2CEditorIntegration::Get().GetGraphAsJson(Graph, /*bPrettyPrint=*/false, Error, &Captured);
		if (Json.IsEmpty())
		{
			SetState(*Session, EN2CTranslationSessionState::Failed,
				FString::Printf(TEXT("Could not capture %s: %s"), *FN2CGraphUtils::DescribeGraph(Request.GraphName, Request.BlueprintPath), *Error));
			return;
		}
		Capture = MakeShared<const FN2CBlueprint>(MoveTemp(Captured));
	}
	Session->Json = Json;
	Session->Capture = Capture;

	SetState(*Session, EN2CTranslationSessionState::Sending);

	// The result arrives on the game thread (HTTP completions do); a sender that answers elsewhere is moved there
	TWeakObjectPtr<UN2CTranslationSubsystem> WeakThis(this);
	const FOnN2CTranslationParsed OnParsed = FOnN2CTranslationParsed::CreateLambda(
		[WeakThis, SessionId](const FN2CTranslationResponse& Response, bool bSuccess)
		{
			auto Deliver = [WeakThis, SessionId, Response, bSuccess]()
			{
				if (UN2CTranslationSubsystem* This = WeakThis.Get())
				{
					This->HandleResult(SessionId, Response, bSuccess);
				}
			};
			if (IsInGameThread())
			{
				Deliver();
			}
			else
			{
				AsyncTask(ENamedThreads::GameThread, MoveTemp(Deliver));
			}
		});

	FN2CLLMRequestHandlePtr Handle;
	if (SenderOverride)
	{
		Handle = SenderOverride(Json, Session->Config, Session->Info.Language, OnParsed);
	}
	else
	{
		UN2CLLMModule* LLMModule = UN2CLLMModule::Get();
		if (!LLMModule || (!LLMModule->IsInitialized() && !LLMModule->Initialize()))
		{
			SetState(*Session, EN2CTranslationSessionState::Failed,
				TEXT("The LLM module could not be initialized. Check the provider settings and the Output Log."));
			return;
		}
		// Blueprint listeners of the module's broadcasts still hear about the editor's translations
		LLMModule->OnTranslationRequestSent.Broadcast();
		// The session writes its own archive run (with the tagged graph), so no MCP run
		Handle = LLMModule->ProcessN2CJsonWithOverrides(Json, Session->Config, Session->Info.Language, FOnLLMResponseReceived(),
			OnParsed, /*bArchiveRun=*/false);
	}

	// A request that failed before it was sent has already delivered its result
	if (!Session->Info.IsFinished())
	{
		Session->Handle = Handle;
	}
}

void UN2CTranslationSubsystem::HandleResult(const FGuid& SessionId, const FN2CTranslationResponse& Response, bool bSuccess)
{
	FN2CTranslationSession* Session = FindMutable(SessionId);
	if (!Session || Session->Info.IsFinished())
	{
		return;
	}
	Session->Handle.Reset();
	Session->Response = Response;

	if (bSuccess && Session->Capture.IsValid())
	{
		// Written and recorded with the provider, model and language it was sent with, whatever changed meanwhile
		const FN2CTranslationRunInfo RunInfo = MakeSingleRunInfo(*Session->Capture, Session->Config, Session->Info.Language);
		SaveSingleTranslation(Session->GetGraph(), Response, *Session->Capture, &RunInfo, &Session->Saved);
		Session->Info.RunFolder = Session->Saved.Folder;
		SetState(*Session, EN2CTranslationSessionState::Succeeded);
	}
	else
	{
		FString Error = Response.Error.ToDisplayString();
		if (Error.IsEmpty())
		{
			Error = TEXT("The translation failed; see the Output Log.");
		}
		SetState(*Session, EN2CTranslationSessionState::Failed, Error);
	}

	if (!SenderOverride)
	{
		if (UN2CLLMModule* LLMModule = UN2CLLMModule::Get())
		{
			LLMModule->OnTranslationResponseReceived.Broadcast(Response, bSuccess);
		}
	}
}

bool UN2CTranslationSubsystem::Cancel(const FGuid& SessionId)
{
	FN2CTranslationSession* Session = FindMutable(SessionId);
	if (!Session || Session->Info.IsFinished())
	{
		return false;
	}
	// False: the result is being delivered now, so the session finishes with it
	if (Session->Handle.IsValid() && !Session->Handle->Cancel())
	{
		return false;
	}
	Session->Handle.Reset();
	SetState(*Session, EN2CTranslationSessionState::Cancelled);
	return true;
}

bool UN2CTranslationSubsystem::IsBusy() const
{
	for (const TSharedRef<FN2CTranslationSession>& Session : Sessions)
	{
		if (!Session->Info.IsFinished())
		{
			return true;
		}
	}
	return UN2CBatchTranslationOrchestrator::Get().IsBatchInProgress();
}

const FN2CTranslationSession* UN2CTranslationSubsystem::FindSession(const FGuid& SessionId) const
{
	for (const TSharedRef<FN2CTranslationSession>& Session : Sessions)
	{
		if (Session->Info.SessionId == SessionId)
		{
			return &Session.Get();
		}
	}
	return nullptr;
}

FN2CTranslationSession* UN2CTranslationSubsystem::FindMutable(const FGuid& SessionId)
{
	return const_cast<FN2CTranslationSession*>(FindSession(SessionId));
}

bool UN2CTranslationSubsystem::GetSessionInfo(const FGuid& SessionId, FN2CTranslationSessionInfo& OutInfo) const
{
	if (const FN2CTranslationSession* Session = FindSession(SessionId))
	{
		OutInfo = Session->Info;
		return true;
	}
	return false;
}

bool UN2CTranslationSubsystem::ShowResult(const FGuid& SessionId)
{
	const FN2CTranslationSession* Session = FindSession(SessionId);
	if (!Session || Session->Info.State != EN2CTranslationSessionState::Succeeded || !Session->GetRootTranslation())
	{
		return false;
	}
	// The window's SN2CMainWindow registers as a presenter while it is built
	if (!HasPresenter())
	{
		if (OpenWindowOverride)
		{
			OpenWindowOverride();
		}
		else
		{
			FGlobalTabmanager::Get()->TryInvokeTab(SN2CEditorWindow::TabId);
		}
	}
	if (!HasPresenter())
	{
		FN2CGraphUtils::NotifyFailure(FString::Printf(TEXT("Could not open the Node to Code window to show the translation of %s."),
			*Session->Info.GraphName));
		return false;
	}
	// Opening the window may have run anything: look the session up again
	Session = FindSession(SessionId);
	if (!Session)
	{
		return false;
	}
	OnShowResultRequested.Broadcast(*Session);
	return true;
}

void UN2CTranslationSubsystem::SetState(FN2CTranslationSession& InSession, EN2CTranslationSessionState State, const FString& Error)
{
	using namespace N2CTranslationSubsystemPrivate;

	// A listener may start another session (which prunes): hold this one until the broadcasts are done
	TSharedPtr<FN2CTranslationSession> Hold;
	for (const TSharedRef<FN2CTranslationSession>& Candidate : Sessions)
	{
		if (&Candidate.Get() == &InSession)
		{
			Hold = Candidate;
		}
	}
	if (!Hold.IsValid())
	{
		return;
	}
	FN2CTranslationSession& Session = *Hold;
	const EN2CTranslationSessionState Previous = Session.Info.State;
	Session.Info.State = State;
	Session.Info.ErrorMessage = State == EN2CTranslationSessionState::Failed ? Error : FString();

	const FString Line = FString::Printf(TEXT("Translation session %s: %s%s%s"), *Session.Info.SessionId.ToString(), StateName(State),
		Error.IsEmpty() ? TEXT("") : TEXT(": "), *Error);
	if (State == EN2CTranslationSessionState::Failed)
	{
		FN2CLogger::Get().LogWarning(Line, TEXT("TranslationSubsystem"));
	}
	else
	{
		FN2CLogger::Get().Log(Line, State == EN2CTranslationSessionState::Succeeded ? EN2CLogSeverity::Info : EN2CLogSeverity::Debug,
			TEXT("TranslationSubsystem"));
	}

	// An editor translation that failed before it was sent (the graph is gone, or cannot be read) is said in a
	// notification when it has none of its own; a window shows a provider's failure in its progress dialog
	if (State == EN2CTranslationSessionState::Failed && IsEditorSource(Session.Info.Source)
		&& Previous != EN2CTranslationSessionState::Sending && !Notifications.Contains(Session.Info.SessionId)
		&& FSlateApplication::IsInitialized())
	{
		FN2CGraphUtils::NotifyFailure(Error);
	}

	UpdateNotification(Session);
	OnSessionChangedNative.Broadcast(Session);
	OnSessionChanged.Broadcast(Session.Info);
	UpdateBusy();
}

void UN2CTranslationSubsystem::UpdateBusy(bool bBatchEnded)
{
	bool bBusy = false;
	for (const TSharedRef<FN2CTranslationSession>& Session : Sessions)
	{
		bBusy |= !Session->Info.IsFinished();
	}
	if (!bBatchEnded)
	{
		bBusy |= UN2CBatchTranslationOrchestrator::Get().IsBatchInProgress();
	}
	if (bBusy != bLastBusy)
	{
		bLastBusy = bBusy;
		FN2CLogger::Get().Log(FString::Printf(TEXT("Translation state: %s"), bBusy ? TEXT("busy") : TEXT("idle")),
			EN2CLogSeverity::Debug, TEXT("TranslationSubsystem"));
		OnBusyChanged.Broadcast(bBusy);
	}
}

void UN2CTranslationSubsystem::PruneSessions()
{
	int32 Finished = 0;
	for (const TSharedRef<FN2CTranslationSession>& Session : Sessions)
	{
		Finished += Session->Info.IsFinished() ? 1 : 0;
	}
	for (int32 Index = 0; Index < Sessions.Num() && Finished > MaxFinishedSessions; )
	{
		if (Sessions[Index]->Info.IsFinished())
		{
			Notifications.Remove(Sessions[Index]->Info.SessionId);
			Sessions.RemoveAt(Index);
			--Finished;
		}
		else
		{
			++Index;
		}
	}
}

void UN2CTranslationSubsystem::UpdateNotification(const FN2CTranslationSession& Session)
{
	using namespace N2CTranslationSubsystemPrivate;
	if (!FSlateApplication::IsInitialized())
	{
		return;
	}
	const FGuid SessionId = Session.Info.SessionId;
	const FText GraphName = FText::FromString(Session.Info.GraphName);

	// Only the editor's own buttons, and only while no window shows the session
	if (Session.Info.State == EN2CTranslationSessionState::Queued)
	{
		if (!IsEditorSource(Session.Info.Source))
		{
			return;
		}
		if (HasPresenter())
		{
			// The window shows it: bring its tab forward, which may be docked behind the Blueprint editor (a layout
			// restores it open). A window hosted elsewhere (the legacy UMG wrapper) has no tab and is left alone.
			if (FGlobalTabmanager::Get()->FindExistingLiveTab(FTabId(SN2CEditorWindow::TabId)).IsValid())
			{
				FGlobalTabmanager::Get()->TryInvokeTab(SN2CEditorWindow::TabId);
			}
			return;
		}
		TWeakObjectPtr<UN2CTranslationSubsystem> WeakThis(this);
		FNotificationInfo Info(FText::Format(LOCTEXT("Translating", "Translating {0}..."), GraphName));
		Info.bFireAndForget = false;
		Info.bUseThrobber = true;
		Info.bUseSuccessFailIcons = true;
		Info.ExpireDuration = FinishedNotificationSeconds;
		Info.ButtonDetails.Add(FNotificationButtonInfo(
			LOCTEXT("Cancel", "Cancel"),
			LOCTEXT("CancelToolTip", "Cancel this translation"),
			FSimpleDelegate::CreateLambda([WeakThis, SessionId]()
			{
				if (UN2CTranslationSubsystem* This = WeakThis.Get())
				{
					This->Cancel(SessionId);
				}
			}),
			SNotificationItem::CS_Pending));
		Info.ButtonDetails.Add(FNotificationButtonInfo(
			LOCTEXT("OpenResult", "Open Result"),
			LOCTEXT("OpenResultToolTip", "Open the Node to Code window on this translation"),
			FSimpleDelegate::CreateLambda([WeakThis, SessionId]()
			{
				if (UN2CTranslationSubsystem* This = WeakThis.Get())
				{
					This->ShowResult(SessionId);
				}
			}),
			SNotificationItem::CS_Success));
		const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
		if (Item.IsValid())
		{
			Item->SetCompletionState(SNotificationItem::CS_Pending);
			Notifications.Add(SessionId, Item);
		}
		return;
	}

	const TWeakPtr<SNotificationItem>* Found = Notifications.Find(SessionId);
	const TSharedPtr<SNotificationItem> Item = Found ? Found->Pin() : nullptr;
	if (!Item.IsValid())
	{
		return;
	}
	switch (Session.Info.State)
	{
	case EN2CTranslationSessionState::Sending:
		Item->SetText(FText::Format(LOCTEXT("Sending", "Translating {0} with {1}..."), GraphName, FText::FromString(Session.Info.Model)));
		return;
	case EN2CTranslationSessionState::Succeeded:
		Item->SetText(FText::Format(LOCTEXT("Succeeded", "Translated {0}"), GraphName));
		Item->SetCompletionState(SNotificationItem::CS_Success);
		break;
	case EN2CTranslationSessionState::Failed:
		Item->SetText(FText::Format(LOCTEXT("Failed", "Translation of {0} failed: {1}"), GraphName, FText::FromString(Session.Info.ErrorMessage)));
		Item->SetCompletionState(SNotificationItem::CS_Fail);
		break;
	case EN2CTranslationSessionState::Cancelled:
		Item->SetText(FText::Format(LOCTEXT("Cancelled", "Translation of {0} cancelled"), GraphName));
		Item->SetCompletionState(SNotificationItem::CS_None);
		break;
	default:
		return;
	}

	// A finished notification stays a while (time to click Open Result), then fades
	const TWeakPtr<SNotificationItem> WeakItem = Item;
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakItem](float)
	{
		if (const TSharedPtr<SNotificationItem> Pinned = WeakItem.Pin())
		{
			Pinned->ExpireAndFadeout();
		}
		return false;
	}), Session.Info.State == EN2CTranslationSessionState::Cancelled ? 2.0f : FinishedNotificationSeconds);
	Notifications.Remove(SessionId);
}

FN2CTranslationRunInfo UN2CTranslationSubsystem::MakeSingleRunInfo(const FN2CBlueprint& Capture, const FN2CLLMConfig& Config,
	EN2CCodeLanguage Language)
{
	FN2CTranslationRunInfo Info;
	Info.Kind = EN2CTranslationRunKind::Single;
	Info.Name = Capture.Metadata.Name.IsEmpty() ? FString(TEXT("UnknownBlueprint")) : Capture.Metadata.Name;
	Info.Provider = UN2CLLMModule::GetProviderName(Config.Provider);
	Info.Model = Config.Model;
	Info.Language = Language;
	return Info;
}

FN2CTranslationRunInfo UN2CTranslationSubsystem::MakeSingleRunInfo(const FN2CBlueprint& Capture)
{
	const UN2CSettings* Settings = GetDefault<UN2CSettings>();
	return MakeSingleRunInfo(Capture, UN2CLLMModule::MakeConfigFromSettings(), Settings ? Settings->TargetLanguage : EN2CCodeLanguage::Cpp);
}

bool UN2CTranslationSubsystem::SaveSingleTranslation(const FN2CTagInfo& Graph, const FN2CTranslationResponse& Response,
	const FN2CBlueprint& Capture, const FN2CTranslationRunInfo* RunInfo, FN2CArchivedResponse* OutSaved)
{
	const FN2CTranslationRunInfo Info = RunInfo ? *RunInfo : MakeSingleRunInfo(Capture);
	const TSharedPtr<FN2CTranslationArchive> Run = FN2CTranslationArchive::BeginRun(Info);
	if (!Run.IsValid())
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Translation of %s was not saved: no run folder"), *Graph.GraphName),
			TEXT("TranslationSubsystem"));
		return false;
	}

	FN2CArchiveSource Source;
	Source.Capture = &Capture;
	Source.GraphName = Graph.GraphName;
	Source.BlueprintPath = Graph.BlueprintPath;
	Source.GraphGuid = Graph.GraphGuid;
	FN2CArchivedResponse Saved;
	Run->WriteResponse(Response, Source, FString(), Saved);
	if (OutSaved)
	{
		*OutSaved = Saved;
	}

	// The tagged graph is recorded at its root entry's folder: <run>/<entry>/<entry><ext>
	const FN2CArchivedEntry* Root = Saved.GetRoot();
	FGuid GraphGuid;
	if (!Root || !FGuid::Parse(Graph.GraphGuid, GraphGuid) || !GraphGuid.IsValid() || !Response.Graphs.IsValidIndex(Root->ResponseIndex))
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Translation of %s was not recorded"), *Graph.GraphName),
			TEXT("TranslationSubsystem"));
		return false;
	}

	FN2CTranslationSummary Summary;
	const FN2CGraphTranslation& Translation = Response.Graphs[Root->ResponseIndex];
	Summary.DeclarationPreview = Translation.Code.GraphDeclaration.Left(100);
	TArray<FString> Lines;
	Translation.Code.GraphImplementation.ParseIntoArrayLines(Lines, false);
	Summary.ImplementationLines = Lines.Num();
	Summary.bHasNotes = !Translation.Code.ImplementationNotes.IsEmpty();

	// With the run, its manifest, the root entry's files and the usage
	UN2CGraphStateManager::Get().RecordArchivedTranslation(
		GraphGuid,
		Graph.GraphName,
		FSoftObjectPath(Graph.BlueprintPath),
		*Root,
		Run->GetFolder(),
		Run->GetRunId(),
		Info.Provider,
		Info.Model,
		Info.Language,
		Summary,
		Response.Usage.InputTokens,
		Response.Usage.OutputTokens
	);
	return true;
}

#undef LOCTEXT_NAMESPACE
