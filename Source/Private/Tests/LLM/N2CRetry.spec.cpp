// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "LLM/N2CCredentialSource.h"
#include "LLM/N2CHttpHandler.h"
#include "LLM/N2CHttpTransport.h"
#include "Models/N2CTranslation.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Tests/N2CTestHelpers.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/**
 * Retries and the HTTP transport seam (card 0215). Every request goes through an IN2CHttpTransport; here the fixture
 * transport answers with canned responses and records what was sent, so no request reaches the network. A 429, 529 or
 * 5xx answer is sent again after the provider's Retry-After (or a capped backoff) until Max Request Retries runs out;
 * a 4xx client error is not retried; a cancel during the wait sends nothing more.
 */
BEGIN_DEFINE_SPEC(FN2CRetrySpec, "NodeToCode.LLM.Retry", N2C_TEST_FLAGS)

	TStrongObjectPtr<UN2CHttpHandler> Handler;
	TSharedPtr<FN2CFixtureHttpTransport> Transport;

	void MakeHandler(EN2CLLMProvider Provider, int32 MaxRetries)
	{
		Handler.Reset(NewObject<UN2CHttpHandler>(GetTransientPackage()));
		FN2CLLMConfig Config;
		Config.Provider = Provider;
		Config.Model = TEXT("n2c-retry-test");
		Config.MaxRetries = MaxRetries;
		Handler->Initialize(Config);
		// Short backoff steps so the spec stays fast; a Retry-After header is still honoured as given
		Handler->RetryPolicy.BaseDelaySeconds = 0.05f;
		Transport = MakeShared<FN2CFixtureHttpTransport>();
		Handler->SetTransport(Transport);
	}

	/** Sends one request and ticks until it completes (or Seconds pass); returns what OnComplete got */
	TOptional<FString> Run(double Seconds = 10.0, EN2CLLMProvider Provider = EN2CLLMProvider::OpenAI, FN2CLLMRequestHandlePtr* OutHandle = nullptr)
	{
		const TSharedRef<TOptional<FString>> Result = MakeShared<TOptional<FString>>();
		const FN2CLLMRequestHandleRef Handle = Handler->PostLLMRequest(TEXT("https://example.invalid/v1/chat"),
			FN2CCredential::Make(Provider, TEXT("n2c-test-key-not-real-0215")), TEXT("{\"messages\":[]}"),
			FOnLLMResponseReceived::CreateLambda([Result](const FString& Response) { *Result = Response; }));
		if (OutHandle)
		{
			*OutHandle = Handle;
		}
		Pump(Seconds, [Result] { return Result->IsSet(); });
		return *Result;
	}

	static void Pump(double Seconds, TFunctionRef<bool()> Until)
	{
		const double End = FPlatformTime::Seconds() + Seconds;
		double Last = FPlatformTime::Seconds();
		while (FPlatformTime::Seconds() < End && !Until())
		{
			FPlatformProcess::Sleep(0.005f);
			const double Now = FPlatformTime::Seconds();
			FTSTicker::GetCoreTicker().Tick(static_cast<float>(Now - Last));
			Last = Now;
		}
	}

	static TSharedPtr<FJsonObject> ParseObject(const FString& Text)
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		return FJsonSerializer::Deserialize(Reader, Root) ? Root : nullptr;
	}

	static bool HasHeader(const FN2CHttpRequestSpec& Request, const FString& Name)
	{
		return Request.Headers.ContainsByPredicate([&Name](const TPair<FString, FString>& Header) { return Header.Key.Equals(Name, ESearchCase::IgnoreCase); });
	}

END_DEFINE_SPEC(FN2CRetrySpec)

void FN2CRetrySpec::Define()
{
	AfterEach([this]()
	{
		Handler.Reset();
		Transport.Reset();
	});

	It(TEXT("retries a 429 no sooner than its Retry-After and then succeeds"), [this]()
	{
		MakeHandler(EN2CLLMProvider::OpenAI, 3);
		AddExpectedMessage(TEXT("HTTP 429 from"), EAutomationExpectedMessageFlags::Contains, 1);
		Transport->Enqueue(429, TEXT("{\"error\":{\"message\":\"Rate limit reached\",\"type\":\"rate_limit_error\"}}"), { { TEXT("Retry-After"), TEXT("2") } });
		Transport->Enqueue(200, TEXT("{\"ok\":true}"));
		const TOptional<FString> Result = Run();
		if (!TestTrue(TEXT("it completed"), Result.IsSet())) { return; }
		TestEqual(TEXT("with the second answer"), Result.GetValue(), FString(TEXT("{\"ok\":true}")));
		const TArray<FN2CFixtureHttpTransport::FSent>& Sent = Transport->GetSent();
		if (TestEqual(TEXT("two attempts"), Sent.Num(), 2))
		{
			const double Gap = Sent[1].Time - Sent[0].Time;
			TestTrue(FString::Printf(TEXT("the retry waited for Retry-After (%.2f s)"), Gap), Gap >= 1.95);
			TestEqual(TEXT("the same body is sent again"), Sent[1].Request.Body, Sent[0].Request.Body);
		}
	});

	It(TEXT("succeeds after a 529 overload, and after a 503"), [this]()
	{
		MakeHandler(EN2CLLMProvider::Anthropic, 3);
		AddExpectedMessage(TEXT("retrying in"), EAutomationExpectedMessageFlags::Contains, 2);
		Transport->Enqueue(529, TEXT("{\"type\":\"error\",\"error\":{\"type\":\"overloaded_error\",\"message\":\"Overloaded\"}}"));
		Transport->Enqueue(503, TEXT("<html>Service Unavailable</html>"));
		Transport->Enqueue(200, TEXT("{\"ok\":true}"));
		const TOptional<FString> Result = Run(10.0, EN2CLLMProvider::Anthropic);
		TestTrue(TEXT("it succeeded"), Result.IsSet() && Result.GetValue() == TEXT("{\"ok\":true}"));
		TestEqual(TEXT("three attempts"), Transport->GetSent().Num(), 3);
	});

	It(TEXT("does not retry a 400 and returns InvalidRequest with the status and the provider's message"), [this]()
	{
		MakeHandler(EN2CLLMProvider::OpenAI, 3);
		AddExpectedError(TEXT("HTTP 400 error"), EAutomationExpectedErrorFlags::Contains, 1);
		Transport->Enqueue(400, TEXT("{\"error\":{\"message\":\"Unsupported parameter: 'temperature'\",\"type\":\"invalid_request_error\"}}"));
		const TOptional<FString> Result = Run();
		if (!TestTrue(TEXT("it completed"), Result.IsSet())) { return; }
		TestEqual(TEXT("one attempt"), Transport->GetSent().Num(), 1);
		const FN2CTranslationError Error = FN2CTranslationError::FromErrorJson(ParseObject(Result.GetValue()));
		TestEqual(TEXT("kind"), Error.Kind, EN2CTranslationErrorKind::InvalidRequest);
		TestEqual(TEXT("status"), Error.HttpStatus, 400);
		TestTrue(TEXT("the provider's message"), Error.Message.Contains(TEXT("Unsupported parameter")));
	});

	It(TEXT("gives up after Max Request Retries with a non-JSON 502 body that still parses as valid JSON"), [this]()
	{
		MakeHandler(EN2CLLMProvider::OpenAI, 2);
		AddExpectedMessage(TEXT("retrying in"), EAutomationExpectedMessageFlags::Contains, 2);
		AddExpectedError(TEXT("HTTP 502 error"), EAutomationExpectedErrorFlags::Contains, 1);
		for (int32 Attempt = 0; Attempt < 3; ++Attempt)
		{
			Transport->Enqueue(502, TEXT("<html><head><title>502 Bad Gateway</title></head><body>\"bad\" \\ gateway</body></html>"));
		}
		const TOptional<FString> Result = Run();
		if (!TestTrue(TEXT("it completed"), Result.IsSet())) { return; }
		TestEqual(TEXT("the first attempt and two retries"), Transport->GetSent().Num(), 3);
		TestTrue(TEXT("the error is valid JSON"), ParseObject(Result.GetValue()).IsValid());
		TestEqual(TEXT("with the status"), FN2CTranslationError::FromErrorJson(ParseObject(Result.GetValue())).HttpStatus, 502);
	});

	It(TEXT("does not wait for a Retry-After longer than the cap"), [this]()
	{
		MakeHandler(EN2CLLMProvider::OpenAI, 3);
		AddExpectedError(TEXT("HTTP 429 error"), EAutomationExpectedErrorFlags::Contains, 1);
		Transport->Enqueue(429, TEXT("{\"error\":{\"message\":\"quota\"}}"), { { TEXT("retry-after"), TEXT("3600") } });
		const TOptional<FString> Result = Run();
		TestTrue(TEXT("it failed at once"), Result.IsSet());
		TestEqual(TEXT("one attempt"), Transport->GetSent().Num(), 1);
	});

	It(TEXT("sends nothing more when cancelled during the wait"), [this]()
	{
		MakeHandler(EN2CLLMProvider::OpenAI, 3);
		AddExpectedMessage(TEXT("HTTP 429 from"), EAutomationExpectedMessageFlags::Contains, 1);
		Transport->Enqueue(429, TEXT("{}"), { { TEXT("Retry-After"), TEXT("1") } });
		Transport->Enqueue(200, TEXT("{\"ok\":true}"));
		FN2CLLMRequestHandlePtr Handle;
		const TSharedRef<bool> bCalled = MakeShared<bool>(false);
		Handle = Handler->PostLLMRequest(TEXT("https://example.invalid/v1/chat"), FN2CCredential::Make(EN2CLLMProvider::OpenAI, TEXT("n2c-test-key-not-real-0215")),
			TEXT("{}"), FOnLLMResponseReceived::CreateLambda([bCalled](const FString&) { *bCalled = true; }));
		Pump(0.5, [this] { return false; });
		TestEqual(TEXT("the first attempt went out"), Transport->GetSent().Num(), 1);
		TestTrue(TEXT("the cancel stops it"), Handle->Cancel());
		Pump(1.5, [] { return false; });
		TestEqual(TEXT("no retry after the cancel"), Transport->GetSent().Num(), 1);
		TestFalse(TEXT("OnComplete never runs"), *bCalled);
	});

	It(TEXT("sends an Anthropic key in x-api-key and no Authorization header"), [this]()
	{
		MakeHandler(EN2CLLMProvider::Anthropic, 0);
		Transport->Enqueue(200, TEXT("{}"));
		Run(5.0, EN2CLLMProvider::Anthropic);
		if (TestEqual(TEXT("one request"), Transport->GetSent().Num(), 1))
		{
			TestTrue(TEXT("x-api-key"), HasHeader(Transport->GetSent()[0].Request, TEXT("x-api-key")));
			TestFalse(TEXT("no Authorization"), HasHeader(Transport->GetSent()[0].Request, TEXT("Authorization")));
		}
	});

	It(TEXT("reads Retry-After in seconds, as an HTTP date and as retry-after-ms"), [this]()
	{
		const FDateTime Now(2026, 11, 3, 12, 0, 0);
		FN2CHttpResponseData Response;
		Response.bReceived = true;
		Response.Status = 429;
		Response.Headers.Add(TEXT("retry-after"), TEXT("7"));
		TestTrue(TEXT("seconds"), FMath::IsNearlyEqual(FN2CRetryPolicy::GetServerDelaySeconds(Response, Now), 7.0f));
		Response.Headers.Add(TEXT("retry-after"), TEXT("Tue, 03 Nov 2026 12:00:12 GMT"));
		TestTrue(TEXT("an HTTP date"), FMath::IsNearlyEqual(FN2CRetryPolicy::GetServerDelaySeconds(Response, Now), 12.0f, 0.01f));
		Response.Headers.Add(TEXT("retry-after-ms"), TEXT("1500"));
		const float Milliseconds = FN2CRetryPolicy::GetServerDelaySeconds(Response, Now);
		TestTrue(FString::Printf(TEXT("milliseconds win (%f)"), Milliseconds), FMath::IsNearlyEqual(Milliseconds, 1.5f, 0.001f));

		FN2CRetryPolicy Policy;
		FN2CHttpResponseData Plain;
		Plain.bReceived = true;
		Plain.Status = 500;
		TestTrue(TEXT("backoff doubles"), FMath::IsNearlyEqual(Policy.GetDelaySeconds(1, Plain, 0.0f, Now), 2.0f));
		TestTrue(TEXT("jitter adds at most a quarter"), Policy.GetDelaySeconds(1, Plain, 0.999f, Now) < 2.51f);
		TestTrue(TEXT("no retries left"), Policy.GetDelaySeconds(3, Plain, 0.0f, Now) < 0.0f);
		Plain.Status = 404;
		TestTrue(TEXT("a 404 is not retried"), Policy.GetDelaySeconds(0, Plain, 0.0f, Now) < 0.0f);
		FN2CHttpResponseData NoResponse;
		TestTrue(TEXT("a dropped connection is not retried here"), Policy.GetDelaySeconds(0, NoResponse, 0.0f, Now) < 0.0f);
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
