// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Code Editor/Models/N2CCodeLanguage.h"
#include "LLM/N2CLLMRequestHandle.h"
#include "LLM/N2CLLMTypes.h"
#include "LLM/Verify/IN2CCodeVerifier.h"
#include "Models/N2CBlueprint.h"
#include "Models/N2CTranslation.h"
#include "N2CVerifyRefine.generated.h"

/** How a translation's verification ended (card 0313) */
UENUM(BlueprintType)
enum class EN2CVerifyStatus : uint8
{
	/** Verify and Refine is off for the language: nothing was checked */
	Off,
	/** The last response has no verifier error */
	Passed,
	/** Still has errors after MaxRefineTurns refine turns (or a refine turn's request failed) */
	Failed,
	/** Verification is on, but no verifier could run on this response */
	Skipped
};

/** One response the loop checked: the first answer is turn 0, each refine turn's answer the next */
struct NODETOCODE_API FN2CVerifyAttempt
{
	int32 Turn = 0;
	TArray<FN2CVerifyReport> Reports;

	/** At least one verifier ran */
	bool AnyRan() const;
	/** No report has an error */
	bool Passed() const;
	int32 CountSeverity(EN2CDiagnosticSeverity Severity) const;
};

/** What verify-and-refine did for one translation: written as Verify.json beside its entry folders */
struct NODETOCODE_API FN2CVerifyOutcome
{
	EN2CVerifyStatus Status = EN2CVerifyStatus::Off;
	/** Refine turns sent (0 when the first answer passed) */
	int32 RefineTurns = 0;
	int32 MaxRefineTurns = 0;
	EN2CCodeLanguage Language = EN2CCodeLanguage::Cpp;
	TArray<FN2CVerifyAttempt> Attempts;
	/** Why the loop stopped early (a refine turn's request failed); empty otherwise */
	FString Error;

	/** "off", "passed", "failed-after-<N>" or "skipped" */
	FString GetStatusString() const;

	/** One line for people: "Verified: passed after 1 refine turn" */
	FText GetStatusText() const;

	/** {"status", "refine_turns", "max_refine_turns", "language", "error", "attempts": [{"turn", "reports": [...]}]} */
	FString ToJsonString() const;

	/** Reads what ToJsonString wrote (status, turns, counts of the last attempt); false when Text is not such a file */
	static bool FromJsonString(const FString& Text, FN2CVerifyOutcome& OutOutcome);

	/** The file name in an archived response's folder */
	static const TCHAR* FileName() { return TEXT("Verify.json"); }
};

namespace N2CVerifyRefine
{
	/** Verify and Refine is on for Language (Project Settings > Node to Code > Code Generation) */
	NODETOCODE_API bool IsEnabledFor(EN2CCodeLanguage Language);

	/** The settings' Max Refine Turns, clamped to 0..5 */
	NODETOCODE_API int32 GetMaxRefineTurns();

	/** Runs every verifier that supports Language on Response: Python syntax, capture coverage, the C++ compile (when switched on) */
	NODETOCODE_API FN2CVerifyAttempt VerifyResponse(const FN2CTranslationResponse& Response, const FN2CBlueprint& Capture,
		EN2CCodeLanguage Language, int32 Turn);

	/** The response as the prompt's JSON contract has it: {"graphs": [{graph_name, graph_type, graph_class, code}]} */
	NODETOCODE_API FString ResponseToContractJson(const FN2CTranslationResponse& Response);

	/**
	 * The text of a refine turn, sent after the N2C JSON: the previous response in <previous_response>, the diagnostics in
	 * <verify_diagnostics> and <refine_instructions> asking for the whole corrected response in the same JSON shape
	 */
	NODETOCODE_API FString BuildRefineTurn(const FN2CTranslationResponse& Previous, const FN2CVerifyAttempt& Attempt,
		int32 TurnNumber, int32 MaxTurns);
}

/**
 * The verify-and-refine loop of one translation (card 0313, arch-arch-core-llm-ui-13): sends the N2C JSON, verifies the
 * parsed answer and, while it has verifier errors and turns are left, sends a refine turn carrying the diagnostics.
 * The answer it ends with is the last one parsed; the usage of every turn is added up in it. A failed first request ends
 * the loop with that failure; a failed refine turn ends it with the last good answer and status Failed. Off for the
 * language: one request, no verification (status Off). Cancel stops it: nothing more is sent and OnDone never runs.
 *
 * Game thread (the Python verifier runs Python). The caller keeps the loop alive (a shared pointer) while it runs.
 */
class NODETOCODE_API FN2CVerifyRefineLoop : public TSharedFromThis<FN2CVerifyRefineLoop>
{
public:
	/** Sends one turn: RefineTurn empty for the first, else the text that follows the N2C JSON */
	using FSendTurn = TFunction<FN2CLLMRequestHandleRef(const FString& RefineTurn, const FOnN2CTranslationParsed& OnParsed)>;
	using FOnDone = TFunction<void(const FN2CTranslationResponse& Response, bool bSuccess, const FN2CVerifyOutcome& Outcome)>;
	/** A refine turn is about to be sent (TurnNumber from 1) */
	using FOnRefine = TFunction<void(int32 TurnNumber, const FN2CVerifyAttempt& Failed)>;

	FN2CVerifyRefineLoop(TSharedPtr<const FN2CBlueprint> InCapture, EN2CCodeLanguage InLanguage, bool bInVerify, int32 InMaxTurns);

	/** Sends the first turn; the result comes to OnDone (inside this call when the request fails before it is sent) */
	void Start(FSendTurn InSend, FOnDone InOnDone, FOnRefine InOnRefine = nullptr);

	/** Cancels the request in flight and stops; false when its result is being delivered now (the loop then finishes) */
	bool Cancel();

	bool IsFinished() const { return bFinished; }
	bool IsCancelled() const { return bCancelled; }
	const FN2CLLMRequestHandlePtr& GetHandle() const { return Handle; }
	const FN2CVerifyOutcome& GetOutcome() const { return Outcome; }

private:
	void SendTurn(const FString& RefineTurn);
	void HandleParsed(const FN2CTranslationResponse& Response, bool bSuccess);
	void Finish(const FN2CTranslationResponse& Response, bool bSuccess);

	TSharedPtr<const FN2CBlueprint> Capture;
	EN2CCodeLanguage Language;
	bool bVerify;
	int32 MaxTurns;

	FSendTurn Send;
	FOnDone OnDone;
	FOnRefine OnRefine;

	FN2CLLMRequestHandlePtr Handle;
	FN2CVerifyOutcome Outcome;
	/** The last answer that parsed, and the usage of every turn so far */
	FN2CTranslationResponse LastGood;
	bool bHasGood = false;
	FN2CTranslationUsage Usage;
	bool bFinished = false;
	bool bCancelled = false;
};
