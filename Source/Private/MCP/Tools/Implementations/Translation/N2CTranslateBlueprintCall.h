// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"
#include "LLM/N2CLLMRequestHandle.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "MCP/Tools/N2CToolCallContext.h"
#include "Containers/Ticker.h"
#include <atomic>

/**
 * One translate-focused-blueprint call.
 *
 * Everything runs on the game thread: Start() collects the Blueprint JSON, builds the request and sends it, then
 * returns the call's future at once. The LLM completion (an HTTP completion, also on the game thread) sets the
 * result. An FTSTicker reports progress through the call context and enforces the timeout. Cancelling the context
 * (or the timeout) cancels the in-flight request through its FN2CLLMRequestHandle and sets a cancelled result, so the
 * result is set exactly once and no LLM callback arrives after it.
 *
 * The call keeps itself alive while it runs (until its result is set); the context, the ticker and the LLM callback
 * hold it weakly.
 */
class FN2CTranslateBlueprintCall : public TSharedFromThis<FN2CTranslateBlueprintCall, ESPMode::ThreadSafe>
{
public:
    /** Starts the LLM request for the given provider, model and language overrides and returns its handle */
    using FRequestStarter = TFunction<FN2CLLMRequestHandleRef(const FString& /*ProviderId*/, const FString& /*ModelId*/,
        const FString& /*LanguageId*/, const FOnLLMResponseReceived& /*OnComplete*/)>;

    /**
     * @param InContext The call's progress channel and cancellation signal
     * @param InProviderId, InModelId, InLanguageId Overrides as the client sent them (already checked; empty = default)
     * @param InWarnings Added as a "warnings" array to the result JSON (for example an unrecognised model ID)
     */
    FN2CTranslateBlueprintCall(const FN2CToolCallContextRef& InContext, const FString& InProviderId, const FString& InModelId,
        const FString& InLanguageId, const TArray<FString>& InWarnings = TArray<FString>());

    /** Sets a result if none was set (a call dropped before it started), so the promise is never broken */
    ~FN2CTranslateBlueprintCall();

    /** Game thread only, once. Sends the request and returns the call's result; it is set later, on the game thread. */
    TFuture<FMcpToolCallResult> Start();

    /**
     * Progress and timeout step, run by the call's FTSTicker every ProgressIntervalSeconds. Returns true while the
     * call is still waiting for the LLM. Public so tests can drive it without ticking the core ticker.
     */
    bool Tick(float DeltaTime);

    /** Replaces the default request (translate the focused Blueprint). For tests that cannot focus a Blueprint headless. */
    void SetRequestStarter(FRequestStarter InStarter) { RequestStarter = MoveTemp(InStarter); }

    /** Overrides the 1-hour timeout (tests) */
    void SetTimeoutSeconds(double InSeconds) { TimeoutSeconds = InSeconds; }

    /** The in-flight request's handle, or null before Start() */
    FN2CLLMRequestHandlePtr GetRequestHandle() const { return RequestHandle; }

    /** The call's context (progress and cancellation) */
    const FN2CToolCallContextRef& GetContext() const { return Context; }

    /** True once the call's result is set */
    bool IsComplete() const { return bFinished.load(); }

    /** Default time a translation may take before the call cancels it */
    static constexpr double DefaultTimeoutSeconds = 3600.0;

    /** How often the ticker reports progress and checks the timeout (MCP clients get a heartbeat at least this often) */
    static constexpr float ProgressIntervalSeconds = 0.5f;

    /** Progress after the request is sent; waiting rises from here towards ProgressParsing */
    static constexpr float ProgressAfterSend = 0.1f;

    /** Progress once the LLM response arrived and is being parsed */
    static constexpr float ProgressParsing = 0.95f;

    /** Progress while waiting Elapsed seconds for the LLM: strictly rising, always below ProgressParsing */
    static float GetWaitProgress(double ElapsedSeconds)
    {
        const double Elapsed = FMath::Max(ElapsedSeconds, 0.0);
        return ProgressAfterSend + (ProgressParsing - 0.01f - ProgressAfterSend) * static_cast<float>(Elapsed / (Elapsed + 60.0));
    }

    /**
     * Results longer than this (characters of text) are saved to a file and returned as a resource_link (card 0207).
     * Equals the tool's _meta["anthropic/maxResultSizeChars"] (NodeToCode.MCP.StructuredOutput checks it).
     */
    static constexpr int32 MaxInlineChars = 200000;

    /**
     * The successful result of a translation (TranslationJson: the LLM module's {graphs, usage} plus warnings), marked
     * as untrusted data in its text and structuredContent. Up to MaxInline characters it is inline; above that the
     * whole translation is written to Saved/NodeToCode/Exports/Translations and the result is a summary (each graph's
     * name, type and class, usage, warnings, savedTo, bytes) plus a resource_link to the file. Unset when
     * TranslationJson is not a JSON object.
     */
    static TOptional<FMcpToolCallResult> BuildSuccessResult(const FString& TranslationJson, int32 MaxInline = MaxInlineChars);

    /** translate-focused-blueprint's outputSchema: the inline translation and the saved-file summary both match it */
    static TSharedRef<FJsonObject> BuildOutputSchema();

private:
    /** Turns the LLM response into the call's result (game thread) */
    void HandleLLMResponse(const FString& LLMResponse);

    /** Cancels the request and sets Message as the call's error result, once */
    void AbortRequest(const FString& Message);

    /** Sets the call's result once, stops the ticker and lets the call go */
    void Finish(const FMcpToolCallResult& Result);

    /** Adds the warnings array to a JSON object result text; other text is returned unchanged */
    FString WithWarnings(const FString& Text) const;

    /** Removes the progress ticker */
    void StopTicker();

    FN2CToolCallContextRef Context;
    FString ProviderId;
    FString ModelId;
    FString LanguageId;
    TArray<FString> Warnings;

    TPromise<FMcpToolCallResult> Promise;
    std::atomic<bool> bFinished{false};
    bool bStarted = false;

    /** Keeps the call alive from Start() until its result is set */
    TSharedPtr<FN2CTranslateBlueprintCall, ESPMode::ThreadSafe> SelfWhileRunning;

    FRequestStarter RequestStarter;
    FN2CLLMRequestHandlePtr RequestHandle;
    FTSTicker::FDelegateHandle TickerHandle;

    double TimeoutSeconds = DefaultTimeoutSeconds;
    double StartTime = 0.0;
};
