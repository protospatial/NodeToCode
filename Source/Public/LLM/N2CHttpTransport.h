// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "LLM/N2CLLMRequestHandle.h"

/** One HTTP POST as the LLM layer sends it (card 0215) */
struct NODETOCODE_API FN2CHttpRequestSpec
{
	FString Url;

	/** In the order they are set (UN2CHttpHandlerBase::BuildRequestHeaders) */
	TArray<TPair<FString, FString>> Headers;

	FString Body;

	/** Whole-request timeout in seconds */
	float TimeoutSeconds = 3600.0f;

	/** Seconds without receiving data before the request fails */
	float ActivityTimeoutSeconds = 300.0f;
};

/** What came back for one HTTP attempt: a response, or why there was none */
struct NODETOCODE_API FN2CHttpResponseData
{
	/** True when the server answered (any status); false for a connection failure or a timeout */
	bool bReceived = false;

	int32 Status = 0;

	FString Body;

	/** Response headers by lower-case name */
	TMap<FString, FString> Headers;

	/** No response: the request ran past its total timeout or received nothing for the activity timeout */
	bool bTimedOut = false;

	/** No response: why, for the log and the typed error (a timeout's message names the setting to raise) */
	FString FailureMessage;

	/** The header Name's value (any case), empty when absent */
	FString GetHeader(const FString& Name) const;
};

/**
 * The seam between the LLM layer and the network (card 0215): UN2CHttpHandlerBase sends every attempt through one.
 * FN2CHttpModuleTransport is the engine's HTTP; FN2CFixtureHttpTransport replays canned responses and records what was
 * sent, so retries, errors and headers are tested without a network. Used on the game thread.
 */
class NODETOCODE_API IN2CHttpTransport
{
public:
	virtual ~IN2CHttpTransport() = default;

	/**
	 * Sends Request. OnResponse runs once, on the game thread, unless Handle is cancelled first; a transport that cannot
	 * send at all calls it at once with bReceived false. Handle's Cancel() cancels the request in flight.
	 */
	virtual void Send(const FN2CHttpRequestSpec& Request, const FN2CLLMRequestHandleRef& Handle,
		TFunction<void(const FN2CHttpResponseData&)> OnResponse) = 0;
};

/** The engine's HTTP module */
class NODETOCODE_API FN2CHttpModuleTransport : public IN2CHttpTransport
{
public:
	virtual void Send(const FN2CHttpRequestSpec& Request, const FN2CLLMRequestHandleRef& Handle,
		TFunction<void(const FN2CHttpResponseData&)> OnResponse) override;

	/** The shared instance UN2CHttpHandlerBase uses unless a test sets another */
	static TSharedRef<IN2CHttpTransport> Get();
};

/**
 * A transport for specs: each Send records the request (with the time it was sent) and answers with the next queued
 * response on a later tick, or with a connection failure once the queue is empty.
 */
class NODETOCODE_API FN2CFixtureHttpTransport : public IN2CHttpTransport
{
public:
	struct FSent
	{
		FN2CHttpRequestSpec Request;
		double Time = 0.0;
	};

	/** Queues a response with Status, Body and headers (names any case) */
	void Enqueue(int32 Status, const FString& Body, const TMap<FString, FString>& Headers = {});

	/** Queues an attempt that gets no response (a dropped connection) */
	void EnqueueNoResponse(const FString& FailureMessage = TEXT("Connection failed"));

	const TArray<FSent>& GetSent() const { return Sent; }

	virtual void Send(const FN2CHttpRequestSpec& Request, const FN2CLLMRequestHandleRef& Handle,
		TFunction<void(const FN2CHttpResponseData&)> OnResponse) override;

private:
	TArray<FN2CHttpResponseData> Queue;
	TArray<FSent> Sent;
};

/** When and whether the LLM layer retries a failed attempt (card 0215) */
struct NODETOCODE_API FN2CRetryPolicy
{
	/** Retries after the first attempt (the Max Request Retries setting); 0 never retries */
	int32 MaxRetries = 3;

	/** First backoff delay; it doubles each retry, with up to 25 % random jitter added */
	float BaseDelaySeconds = 1.0f;

	/** Longest wait: a backoff is capped here, and a Retry-After longer than this is not waited for */
	float MaxDelaySeconds = 60.0f;

	/** 429 (rate limited), 529 (Anthropic overloaded) and every 5xx; not 4xx client errors */
	static bool IsRetryableStatus(int32 Status);

	/**
	 * The server's requested wait from Retry-After (seconds, or an HTTP date) or retry-after-ms, in seconds; negative
	 * when neither header is there or readable
	 */
	static float GetServerDelaySeconds(const FN2CHttpResponseData& Response, const FDateTime& Now);

	/**
	 * Seconds to wait before retry number RetryIndex (0 = the first retry) after Response, or a negative value when it
	 * must not be retried: not a retryable status, no retries left, or the server asks for longer than MaxDelaySeconds.
	 * Jitter01 is a random number in [0, 1).
	 */
	float GetDelaySeconds(int32 RetryIndex, const FN2CHttpResponseData& Response, float Jitter01, const FDateTime& Now) const;
};
