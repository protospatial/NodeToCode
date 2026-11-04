// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "LLM/N2CHttpTransport.h"
#include "Containers/Ticker.h"
#include "HAL/PlatformTime.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Models/N2CTranslation.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

FString FN2CHttpResponseData::GetHeader(const FString& Name) const
{
	const FString* Value = Headers.Find(Name.ToLower());
	return Value ? *Value : FString();
}

// ---------------------------------------------------------------------------------------------------------------------
// FN2CHttpModuleTransport

TSharedRef<IN2CHttpTransport> FN2CHttpModuleTransport::Get()
{
	static TSharedRef<IN2CHttpTransport> Transport = MakeShared<FN2CHttpModuleTransport>();
	return Transport;
}

void FN2CHttpModuleTransport::Send(const FN2CHttpRequestSpec& Spec, const FN2CLLMRequestHandleRef& Handle,
	TFunction<void(const FN2CHttpResponseData&)> OnResponse)
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Spec.Url);
	Request->SetVerb(Spec.Verb.IsEmpty() ? FString(TEXT("POST")) : Spec.Verb);
	for (const TPair<FString, FString>& Header : Spec.Headers)
	{
		Request->SetHeader(Header.Key, Header.Value);
	}
	if (!Spec.Verb.Equals(TEXT("GET"), ESearchCase::IgnoreCase))
	{
		Request->SetContentAsString(Spec.Body);
	}
	Request->SetTimeout(Spec.TimeoutSeconds);
	// A request that receives nothing for this long fails (UE 5.4+; 5.6 is the floor)
	Request->SetActivityTimeout(Spec.ActivityTimeoutSeconds);

	const float TotalTimeout = Spec.TimeoutSeconds;
	const float ActivityTimeout = Spec.ActivityTimeoutSeconds;
	// The lambda holds the handle strongly; the handle holds the request only weakly, so there is no cycle
	Request->OnProcessRequestComplete().BindLambda(
		[Handle, OnResponse, TotalTimeout, ActivityTimeout](FHttpRequestPtr InRequest, FHttpResponsePtr InResponse, bool bWasSuccessful)
		{
			if (Handle->IsFinished())
			{
				return;
			}
			FN2CHttpResponseData Data;
			if (bWasSuccessful && InResponse.IsValid())
			{
				Data.bReceived = true;
				Data.Status = InResponse->GetResponseCode();
				Data.Body = InResponse->GetContentAsString();
				for (const FString& Line : InResponse->GetAllHeaders())
				{
					FString Name, Value;
					if (Line.Split(TEXT(":"), &Name, &Value))
					{
						Data.Headers.Add(Name.TrimStartAndEnd().ToLower(), Value.TrimStartAndEnd());
					}
				}
				OnResponse(Data);
				return;
			}

			// No response: connection refused, DNS, TLS or a timeout
			const EHttpFailureReason Failure = InRequest.IsValid() ? InRequest->GetFailureReason() : EHttpFailureReason::Other;
			const float Elapsed = InRequest.IsValid() ? InRequest->GetElapsedTime() : 0.0f;
			if (Failure == EHttpFailureReason::TimedOut)
			{
				Data.bTimedOut = true;
				Data.FailureMessage = FString::Printf(TEXT("The request ran past its total timeout of %.0f s (%.0f s elapsed)"), TotalTimeout, Elapsed);
			}
			// UE reports an activity timeout as ConnectionError; it takes at least the activity timeout of silence,
			// a refused or dropped connection fails at once
			else if (Failure == EHttpFailureReason::ConnectionError && ActivityTimeout > 0.0f && Elapsed >= ActivityTimeout * 0.95f)
			{
				Data.bTimedOut = true;
				Data.FailureMessage = FString::Printf(TEXT("No data from the provider for %.0f s (HTTP Activity Timeout). Raise Project Settings > Plugins > Node to Code > LLM Provider > HTTP Activity Timeout for very long translations or slow local models."), ActivityTimeout);
			}
			else
			{
				Data.FailureMessage = FString::Printf(TEXT("No response from the provider (%s)"), LexToString(Failure));
			}
			OnResponse(Data);
		});

	Handle->SetHttpRequest(Request);
	if (!Request->ProcessRequest())
	{
		// A failed ProcessRequest may also fire the completion delegate; the caller's handle keeps the result to one
		FN2CHttpResponseData Data;
		Data.FailureMessage = TEXT("Failed to send request");
		OnResponse(Data);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// FN2CFixtureHttpTransport

void FN2CFixtureHttpTransport::Enqueue(int32 Status, const FString& Body, const TMap<FString, FString>& Headers)
{
	FN2CHttpResponseData Data;
	Data.bReceived = true;
	Data.Status = Status;
	Data.Body = Body;
	for (const TPair<FString, FString>& Header : Headers)
	{
		Data.Headers.Add(Header.Key.ToLower(), Header.Value);
	}
	Queue.Add(MoveTemp(Data));
}

void FN2CFixtureHttpTransport::EnqueueNoResponse(const FString& FailureMessage)
{
	FN2CHttpResponseData Data;
	Data.FailureMessage = FailureMessage;
	Queue.Add(MoveTemp(Data));
}

void FN2CFixtureHttpTransport::Send(const FN2CHttpRequestSpec& Request, const FN2CLLMRequestHandleRef& Handle,
	TFunction<void(const FN2CHttpResponseData&)> OnResponse)
{
	Sent.Add({ Request, FPlatformTime::Seconds() });
	FN2CHttpResponseData Response;
	if (Queue.Num() > 0)
	{
		Response = Queue[0];
		Queue.RemoveAt(0);
	}
	else
	{
		Response.FailureMessage = TEXT("The fixture transport has no response queued");
	}
	// On a later tick, as a real response arrives
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Handle, OnResponse, Response](float)
	{
		if (!Handle->IsFinished())
		{
			OnResponse(Response);
		}
		return false;
	}));
}

// ---------------------------------------------------------------------------------------------------------------------
// FN2CRetryPolicy

bool FN2CRetryPolicy::IsRetryableStatus(int32 Status)
{
	return Status == 429 || Status == 529 || (Status >= 500 && Status <= 599);
}

bool FN2CRetryPolicy::IsQuotaExhausted(const FN2CHttpResponseData& Response)
{
	if (!Response.bReceived || Response.Body.IsEmpty())
	{
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response.Body);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid() || !Root->HasField(TEXT("error")))
	{
		return false;
	}
	return FN2CTranslationError::FromErrorJson(Root).Kind == EN2CTranslationErrorKind::Billing;
}

float FN2CRetryPolicy::GetServerDelaySeconds(const FN2CHttpResponseData& Response, const FDateTime& Now)
{
	float Number = 0.0f;
	const FString Milliseconds = Response.GetHeader(TEXT("retry-after-ms")).TrimStartAndEnd();
	if (!Milliseconds.IsEmpty() && LexTryParseString(Number, *Milliseconds))
	{
		return FMath::Max(0.0f, Number / 1000.0f);
	}
	const FString After = Response.GetHeader(TEXT("retry-after")).TrimStartAndEnd();
	if (After.IsEmpty())
	{
		return -1.0f;
	}
	if (LexTryParseString(Number, *After))
	{
		return FMath::Max(0.0f, Number);
	}
	FDateTime When;
	if (FDateTime::ParseHttpDate(After, When))
	{
		return FMath::Max(0.0f, static_cast<float>((When - Now).GetTotalSeconds()));
	}
	return -1.0f;
}

float FN2CRetryPolicy::GetDelaySeconds(int32 RetryIndex, const FN2CHttpResponseData& Response, float Jitter01, const FDateTime& Now) const
{
	if (!Response.bReceived || !IsRetryableStatus(Response.Status) || RetryIndex < 0 || RetryIndex >= MaxRetries)
	{
		return -1.0f;
	}
	// Out of credit or quota (card 0529): the same request fails again however long it waits
	if (IsQuotaExhausted(Response))
	{
		return -1.0f;
	}
	const float ServerDelay = GetServerDelaySeconds(Response, Now);
	if (ServerDelay >= 0.0f)
	{
		// The server knows when it will take the request again; waiting longer than the cap is giving up instead
		return ServerDelay <= MaxDelaySeconds ? ServerDelay : -1.0f;
	}
	const float Backoff = FMath::Min(MaxDelaySeconds, BaseDelaySeconds * FMath::Pow(2.0f, static_cast<float>(RetryIndex)));
	return FMath::Min(MaxDelaySeconds, Backoff * (1.0f + 0.25f * FMath::Clamp(Jitter01, 0.0f, 1.0f)));
}
