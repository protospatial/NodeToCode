// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "Code Editor/Models/N2CCodeLanguage.h"
#include "Core/N2CTagTypes.h"
#include "LLM/N2CBatchTranslationOrchestrator.h"
#include "LLM/N2CLLMRequestHandle.h"
#include "LLM/N2CLLMTypes.h"
#include "LLM/N2CTranslationArchive.h"
#include "LLM/Verify/N2CVerifyRefine.h"
#include "Models/N2CBatchTranslationTypes.h"
#include "Models/N2CBlueprint.h"
#include "Models/N2CTranslation.h"
#include "N2CTranslationSubsystem.generated.h"

class SNotificationItem;

/** Who asked for a translation */
UENUM(BlueprintType)
enum class EN2CTranslationSource : uint8
{
	/** The N2C bar's Translate button on a graph tab */
	Overlay,
	/** A row's Translate button in the Node to Code window */
	Row,
	Batch,
	Mcp,
	Python
};

/** Where a translation session is. Succeeded, Failed and Cancelled are final. */
UENUM(BlueprintType)
enum class EN2CTranslationSessionState : uint8
{
	Queued,
	/** Resolving the graph and capturing it as N2C JSON */
	Capturing,
	/** The request is with the provider */
	Sending,
	/** Verify and Refine found errors: a refine turn with the findings is with the provider (card 0313) */
	Refining,
	Succeeded,
	Failed,
	Cancelled
};

/** What Blueprints and Python see of a translation session */
USTRUCT(BlueprintType)
struct NODETOCODE_API FN2CTranslationSessionInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Translation")
	FGuid SessionId;

	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Translation")
	EN2CTranslationSource Source = EN2CTranslationSource::Overlay;

	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Translation")
	EN2CTranslationSessionState State = EN2CTranslationSessionState::Queued;

	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Translation")
	FString GraphName;

	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Translation")
	FString BlueprintPath;

	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Translation")
	FString GraphGuid;

	/** Provider and model short names the request was sent with (Anthropic, claude-haiku-5-5) */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Translation")
	FString Provider;

	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Translation")
	FString Model;

	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Translation")
	EN2CCodeLanguage Language = EN2CCodeLanguage::Cpp;

	/** Why the session failed (empty unless State is Failed) */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Translation")
	FString ErrorMessage;

	/** The archive run the result was written to (empty until it succeeded; a batch's run folder once it ended) */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Translation")
	FString RunFolder;

	/** The graphs of a batch session (Source Batch); 0 for a single translation */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Translation")
	int32 ItemCount = 0;

	/** How Verify and Refine ended (Off when it is off for the language, or the session has not finished) */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Translation")
	EN2CVerifyStatus VerifyStatus = EN2CVerifyStatus::Off;

	/** Refine turns sent so far */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Translation")
	int32 RefineTurns = 0;

	bool IsFinished() const
	{
		return State == EN2CTranslationSessionState::Succeeded || State == EN2CTranslationSessionState::Failed
			|| State == EN2CTranslationSessionState::Cancelled;
	}
};

/**
 * One translation to start. Either a tagged graph (BlueprintPath and GraphGuid; the subsystem resolves and captures it)
 * or a capture made by the caller (Capture). Unset Language and Config mean the settings as they are when the session
 * starts: the snapshot is taken then and the whole session uses it.
 */
struct NODETOCODE_API FN2CTranslationRequest
{
	FString BlueprintPath;
	FString GraphGuid;
	FString GraphName;

	/** A capture to send instead of resolving the graph */
	TSharedPtr<const FN2CBlueprint> Capture;

	TOptional<EN2CCodeLanguage> Language;
	TOptional<FN2CLLMConfig> Config;

	EN2CTranslationSource Source = EN2CTranslationSource::Overlay;
};

/** A session as C++ observers see it: the info plus what was sent and what came back */
struct NODETOCODE_API FN2CTranslationSession
{
	FN2CTranslationSessionInfo Info;

	/** The N2C JSON that was sent (the translation viewer's JSON tab) */
	FString Json;

	/** The capture Json was serialized from */
	TSharedPtr<const FN2CBlueprint> Capture;

	/** The parsed result (its typed error when the session failed) */
	FN2CTranslationResponse Response;

	/** What the archive wrote (empty unless the session succeeded) */
	FN2CArchivedResponse Saved;

	/** The request in flight while Sending or Refining (the verify-and-refine loop's current turn) */
	FN2CLLMRequestHandlePtr Handle;

	/** The verify-and-refine loop of the session while it runs */
	TSharedPtr<FN2CVerifyRefineLoop> Loop;

	/** What Verify and Refine did: each attempt's reports and the final status (also the run's Verify.json) */
	FN2CVerifyOutcome Verify;

	/** The config snapshot and language the session was started with */
	FN2CLLMConfig Config;

	/** A batch session's summary once it ended (counts, tokens, run folder, bCancelled) */
	FN2CBatchTranslationResult BatchResult;

	/** The tagged graph, as the record names it */
	FN2CTagInfo GetGraph() const;

	/** The translation the viewer shows: the root entry's graph, else the first; null when there is none */
	const FN2CGraphTranslation* GetRootTranslation() const;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnN2CTranslationSessionChanged, const FN2CTranslationSession& /* Session */);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnN2CTranslationBusyChanged, bool /* bBusy */);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnN2CTranslationSessionChangedDynamic, const FN2CTranslationSessionInfo&, Session);

/**
 * @class UN2CTranslationSubsystem
 * @brief Owns the editor's single-graph translation sessions
 *
 * The overlay's and a row's Translate start a session here, so a translation works with the Node to Code window
 * closed, and one click is one request however many windows are open. A session captures the graph, sends it with a
 * config snapshot taken at its start, writes the result through the translation archive (FN2CTranslationArchive) and
 * records it in the graph state (UN2CGraphStateManager::RecordArchivedTranslation). Every state change is broadcast
 * (OnSessionChangedNative, OnSessionChanged). The Node to Code window only observes; while no window is open, a
 * notification shows the progress with Cancel and, on success, Open Result.
 *
 * A batch is a session too (StartBatch, Source Batch): the subsystem starts the batch orchestrator, holds the busy
 * state and ends the session on the orchestrator's completion, whether or not a window is open. Cancel stops a single
 * session's request or the batch (its remaining items are Skipped). Batches started on the orchestrator directly
 * (translate-batch, the Blueprint library) have no session but still count as busy.
 *
 * IsBusy() is the editor's one "a translation is running" flag (a session in flight or a batch); the N2C bars grey
 * out their Translate on OnBusyChanged. MCP and Python single translations still have their own paths.
 * Game thread only.
 */
UCLASS(BlueprintType)
class NODETOCODE_API UN2CTranslationSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** The editor's instance; null without an editor (or after it shut down) */
	static UN2CTranslationSubsystem* Get();

	/**
	 * Starts a session. Returns its id, or an invalid id when another translation is running (IsBusy(); OutError then
	 * says so). A graph that cannot be resolved or captured still gets an id: its session fails at once and that is
	 * broadcast like any other result.
	 */
	FGuid StartTranslation(const FN2CTranslationRequest& Request, FString* OutError = nullptr);

	/**
	 * Starts a batch session: one request per graph, in order, through UN2CBatchTranslationOrchestrator. Returns its id,
	 * or an invalid id with OutError when it did not start: TRANSLATION_IN_PROGRESS (IsBusy()), or the orchestrator's
	 * BATCH_IN_PROGRESS, LLM_BUSY, LLM_UNAVAILABLE, INVALID_ARGUMENT or NO_VALID_ITEMS (then GetBatchItems() says why
	 * per item). The session is Sending while the batch runs and ends Succeeded (at least one item translated, or none
	 * failed), Failed (every item that ran failed) or Cancelled; BatchResult holds the summary. A batch whose every item
	 * fails before a request is sent has ended when this returns.
	 */
	FGuid StartBatch(const TArray<FN2CTagInfo>& Graphs, const FN2CBatchTranslationOptions& Options = FN2CBatchTranslationOptions(),
		FString* OutError = nullptr);

	/** The batch session that is running, or an invalid id */
	FGuid GetActiveBatchSession() const { return ActiveBatchSession; }

	/**
	 * Cancels a session that has not finished. A single session's request is cancelled through its handle; a batch's
	 * in-flight request is cancelled and its remaining items are Skipped (CancelBatch). The session is Cancelled when
	 * this returns, except a batch whose in-flight result was already being handled: it ends Cancelled right after that
	 * item. False when the session had finished (or its result is being delivered now).
	 */
	bool Cancel(const FGuid& SessionId);

	/** StartBatch with the settings' provider, model and language, for Blueprints and Python: invalid id and OutError when it did not start */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Translation", meta = (DisplayName = "Start Batch Translation"))
	FGuid StartBatchTranslation(const TArray<FN2CTagInfo>& Graphs, FString& OutError);

	/** Cancel, for Blueprints and Python */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Translation", meta = (DisplayName = "Cancel Translation Session"))
	bool CancelSession(const FGuid& SessionId) { return Cancel(SessionId); }

	/** A batch session's summary (counts, tokens, run folder, cancelled); false when the id is not a known batch session */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Translation")
	bool GetBatchSessionResult(const FGuid& SessionId, FN2CBatchTranslationResult& OutResult) const;

	/** True while a session is unfinished or a batch translation runs */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NodeToCode|Translation")
	bool IsBusy() const;

	/** A session by id (the most recent sessions are kept), null when unknown */
	const FN2CTranslationSession* FindSession(const FGuid& SessionId) const;

	/** A session's info by id; false when unknown */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Translation")
	bool GetSessionInfo(const FGuid& SessionId, FN2CTranslationSessionInfo& OutInfo) const;

	/**
	 * Shows a finished session's result in the Node to Code window, opening it first when it is closed (the
	 * notification's Open Result). False when the session is unknown or has no result, or no window took it.
	 */
	bool ShowResult(const FGuid& SessionId);

	/** A window that shows sessions itself registers while it exists; with none, sessions get a notification */
	void AddPresenter() { ++PresenterCount; }
	void RemovePresenter() { PresenterCount = FMath::Max(0, PresenterCount - 1); }
	bool HasPresenter() const { return PresenterCount > 0; }

	/** Every state change of every session */
	FOnN2CTranslationSessionChanged OnSessionChangedNative;

	/** Every state change of every session, for Blueprints and Python */
	UPROPERTY(BlueprintAssignable, Category = "NodeToCode|Translation")
	FOnN2CTranslationSessionChangedDynamic OnSessionChanged;

	/** IsBusy() changed */
	FOnN2CTranslationBusyChanged OnBusyChanged;

	/** ShowResult asks the open windows to show this session's result */
	FOnN2CTranslationSessionChanged OnShowResultRequested;

	/**
	 * Writes a single translation as a run of the translation archive: every response entry in its own folder, the
	 * capture beside them, a manifest. Then records Graph (the tagged graph) at its root entry's folder with the run's
	 * provider, model, language and usage, so the translation viewer can load it again. RunInfo null: a Single run named
	 * after the capture, with the settings' provider, model and language. OutSaved, when given, gets what was written.
	 * False when nothing was written or Graph has no valid GUID.
	 */
	static bool SaveSingleTranslation(const FN2CTagInfo& Graph, const FN2CTranslationResponse& Response,
		const FN2CBlueprint& Capture, const FN2CTranslationRunInfo* RunInfo = nullptr, FN2CArchivedResponse* OutSaved = nullptr,
		const FN2CVerifyOutcome* Verify = nullptr);

	/** A Single run named after Capture with Config's provider and model and Language */
	static FN2CTranslationRunInfo MakeSingleRunInfo(const FN2CBlueprint& Capture, const FN2CLLMConfig& Config, EN2CCodeLanguage Language);

	/** A Single run named after Capture with the settings' provider, model and target language */
	static FN2CTranslationRunInfo MakeSingleRunInfo(const FN2CBlueprint& Capture);

	/**
	 * Sends one turn of a session: its N2C JSON, followed by RefineTurn for a verify-and-refine turn (empty for the
	 * first). Unset: UN2CLLMModule::ProcessN2CJsonWithOverrides with the session's snapshot, no MCP archive run (the
	 * session writes its own). Specs set a fake that answers without a provider.
	 */
	using FSender = TFunction<FN2CLLMRequestHandleRef(const FString& Json, const FN2CLLMConfig& Config,
		EN2CCodeLanguage Language, const FString& RefineTurn, const FOnN2CTranslationParsed& OnParsed)>;
	FSender SenderOverride;

	/** Opens the Node to Code window for ShowResult. Unset: invokes its nomad tab. Specs replace it. */
	TFunction<void()> OpenWindowOverride;

	/** Finished sessions kept for FindSession (the oldest go first) */
	static constexpr int32 MaxFinishedSessions = 20;

private:
	/** Resolves, captures and sends a Queued session; fails it with the reason when a step cannot be done */
	void RunSession(const FGuid& SessionId, const FN2CTranslationRequest& Request);

	/** The session's final result (after its verify-and-refine turns) */
	void HandleResult(const FGuid& SessionId, const FN2CTranslationResponse& Response, bool bSuccess, const FN2CVerifyOutcome& Verify);

	/** Moves a session to State (with Error when it failed), broadcasts it and updates busy */
	void SetState(FN2CTranslationSession& Session, EN2CTranslationSessionState State, const FString& Error = FString());

	/** Broadcasts OnBusyChanged when IsBusy() differs from the last broadcast; bBatchEnded: a batch just finished */
	void UpdateBusy(bool bBatchEnded = false);

	/** The orchestrator's completion of any batch: ends the batch session it belongs to */
	void HandleBatchComplete(const FN2CBatchTranslationResult& Result);

	/** Moves a batch session to its final state from the orchestrator's summary */
	void FinishBatchSession(FN2CTranslationSession& Session, const FN2CBatchTranslationResult& Result);

	/** Drops the oldest finished sessions beyond MaxFinishedSessions */
	void PruneSessions();

	/** The notification of a session no window shows: progress and Cancel, then the result and Open Result */
	void UpdateNotification(const FN2CTranslationSession& Session);

	FN2CTranslationSession* FindMutable(const FGuid& SessionId);

	/** In start order */
	TArray<TSharedRef<FN2CTranslationSession>> Sessions;

	TMap<FGuid, TWeakPtr<SNotificationItem>> Notifications;

	int32 PresenterCount = 0;
	bool bLastBusy = false;

	FDelegateHandle BatchProgressHandle;
	FDelegateHandle BatchCompleteHandle;

	/** The batch session running, and the orchestrator serial of its batch */
	FGuid ActiveBatchSession;
	int32 ActiveBatchSerial = INDEX_NONE;

	/** Inside StartBatch's call to the orchestrator: a completion then is kept in StartedBatchResult */
	bool bStartingBatch = false;
	TOptional<FN2CBatchTranslationResult> StartedBatchResult;

	/** Inside the orchestrator's completion broadcast, while it still reports the batch in progress */
	bool bBatchEnding = false;
};
