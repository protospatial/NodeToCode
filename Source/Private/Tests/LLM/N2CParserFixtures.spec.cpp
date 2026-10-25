// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/PlatformMisc.h"
#include "LLM/N2CResponseParserBase.h"
#include "LLM/Providers/N2CAnthropicResponseParser.h"
#include "LLM/Providers/N2CDeepSeekResponseParser.h"
#include "LLM/Providers/N2CGeminiResponseParser.h"
#include "LLM/Providers/N2CLMStudioResponseParser.h"
#include "LLM/Providers/N2COllamaResponseParser.h"
#include "LLM/Providers/N2COpenAIResponseParser.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Models/N2CTranslation.h"
#include "Tests/N2CTestHelpers.h"
#include "UObject/Package.h"

/**
 * Runs every provider's response parser on checked-in provider bodies (Fixtures/LLM/Parser/<provider>/*.json),
 * case by case from Fixtures/LLM/Parser/cases.json: success answers (thinking or reasoning next to the answer,
 * fenced output, multi-part Gemini output, a Responses API answer after a reasoning item), answers cut at the
 * output limit (Truncated), refusals and filter stops, the 400, 401, 429 and 503/529 error bodies each provider
 * documents (sent through the HTTP handler's wrapping, as a non-2xx answer is), string-typed errors from the local
 * servers and malformed graph_name values. A case with known_defect runs as xIt until the card it names fixes it.
 * No case sends a request; every body is a file.
 */
BEGIN_DEFINE_SPEC(FN2CParserFixturesSpec, "NodeToCode.LLM.Parse.Fixtures", N2C_TEST_FLAGS)

	struct FFixtureCase
	{
		FString Name;
		FString Parser;
		FString File;
		int32 HttpStatus = 0;
		FString KnownDefect;
		TSharedPtr<FJsonObject> Expect;
	};

	/** The graph every success fixture holds (cases.json "graph") */
	TSharedPtr<FJsonObject> Graph;

	static FString FixtureDir()
	{
		return N2CTest::FixturesDir(TEXT("LLM/Parser"));
	}

	static UN2CResponseParserBase* MakeParser(const FString& Name)
	{
		if (Name == TEXT("anthropic")) { return NewObject<UN2CAnthropicResponseParser>(GetTransientPackage()); }
		if (Name == TEXT("openai")) { return NewObject<UN2COpenAIResponseParser>(GetTransientPackage()); }
		if (Name == TEXT("gemini")) { return NewObject<UN2CGeminiResponseParser>(GetTransientPackage()); }
		if (Name == TEXT("deepseek")) { return NewObject<UN2CDeepSeekResponseParser>(GetTransientPackage()); }
		if (Name == TEXT("ollama")) { return NewObject<UN2COllamaResponseParser>(GetTransientPackage()); }
		if (Name == TEXT("lmstudio")) { return NewObject<UN2CLMStudioResponseParser>(GetTransientPackage()); }
		return nullptr;
	}

	static FString StringOr(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, const FString& Default = FString())
	{
		FString Value;
		return Object.IsValid() && Object->TryGetStringField(Field, Value) ? Value : Default;
	}

	void RunCase(const FFixtureCase& Case)
	{
		UN2CResponseParserBase* Parser = MakeParser(Case.Parser);
		if (!TestNotNull(TEXT("parser ") + Case.Parser, Parser)) { return; }

		FString Body;
		if (!TestTrue(TEXT("fixture loads: ") + Case.File, FFileHelper::LoadFileToString(Body, *(FixtureDir() / Case.File)))) { return; }
		TestFalse(TEXT("fixture holds no sk- key"), Body.Contains(TEXT("sk-")));
		TestFalse(TEXT("fixture holds no AIza key"), Body.Contains(TEXT("AIza")));
		if (Case.HttpStatus > 0)
		{
			Body = FN2CTranslationError::WrapHttpErrorBody(Case.HttpStatus, Body);
		}

		const TSharedPtr<FJsonObject>& Expect = Case.Expect;
		bool bOk = false;
		Expect->TryGetBoolField(TEXT("ok"), bOk);
		if (!bOk)
		{
			// Every failure path logs the error it reports
			AddExpectedError(TEXT("[ERROR]"), EAutomationExpectedErrorFlags::Contains, 0, /*IsRegex=*/false);
		}

		FN2CTranslationResponse Response;
		const bool bParsed = Parser->ParseLLMResponse(Body, Response);

		if (bOk)
		{
			TestTrue(FString::Printf(TEXT("parses (error: %s %s)"), *FN2CTranslationError::KindToString(Response.Error.Kind), *Response.Error.Message), bParsed);
			TestFalse(TEXT("no error"), Response.Error.IsError());
			if (TestEqual(TEXT("one graph"), Response.Graphs.Num(), 1))
			{
				const FN2CGraphTranslation& Translated = Response.Graphs[0];
				TestEqual(TEXT("graph_name"), Translated.GraphName, StringOr(Expect, TEXT("graph_name"), StringOr(Graph, TEXT("graph_name"))));
				TestEqual(TEXT("graph_type"), Translated.GraphType, StringOr(Graph, TEXT("graph_type")));
				TestEqual(TEXT("graph_class"), Translated.GraphClass, StringOr(Graph, TEXT("graph_class")));
				const FString Has = StringOr(Graph, TEXT("implementation_has"));
				TestTrue(FString::Printf(TEXT("graphImplementation has '%s'"), *Has), Translated.Code.GraphImplementation.Contains(Has));
				TestTrue(TEXT("implementationNotes keep the escaped quotes"), Translated.Code.ImplementationNotes.Contains(TEXT("(\"Hello\"")));
			}
		}
		else
		{
			TestFalse(TEXT("parse reports failure"), bParsed);
			TestEqual(TEXT("kind"), FN2CTranslationError::KindToString(Response.Error.Kind), StringOr(Expect, TEXT("kind")));
			TestEqual(TEXT("retryable follows the kind"), Response.Error.bRetryable, FN2CTranslationError::IsRetryableKind(Response.Error.Kind));
			TestEqual(TEXT("no graphs"), Response.Graphs.Num(), 0);
			if (Case.HttpStatus > 0)
			{
				TestEqual(TEXT("HTTP status"), Response.Error.HttpStatus, Case.HttpStatus);
			}
			FString ProviderType;
			if (Expect->TryGetStringField(TEXT("provider_type"), ProviderType))
			{
				TestEqual(TEXT("provider type"), Response.Error.ProviderErrorType, ProviderType);
			}
			FString RequestId;
			if (Expect->TryGetStringField(TEXT("request_id"), RequestId))
			{
				TestEqual(TEXT("request id"), Response.Error.RequestId, RequestId);
			}
			TestFalse(TEXT("the message is not empty"), Response.Error.Message.TrimStartAndEnd().IsEmpty());
		}

		const TArray<TSharedPtr<FJsonValue>>* MessageHas = nullptr;
		if (Expect->TryGetArrayField(TEXT("message_has"), MessageHas))
		{
			for (const TSharedPtr<FJsonValue>& Text : *MessageHas)
			{
				TestTrue(FString::Printf(TEXT("message has '%s' (was: %s)"), *Text->AsString(), *Response.Error.Message),
					Response.Error.Message.Contains(Text->AsString()));
			}
		}
		int32 Tokens = 0;
		if (Expect->TryGetNumberField(TEXT("input_tokens"), Tokens))
		{
			TestEqual(TEXT("input tokens"), Response.Usage.InputTokens, Tokens);
		}
		if (Expect->TryGetNumberField(TEXT("output_tokens"), Tokens))
		{
			TestEqual(TEXT("output tokens"), Response.Usage.OutputTokens, Tokens);
		}
	}

END_DEFINE_SPEC(FN2CParserFixturesSpec)

void FN2CParserFixturesSpec::Define()
{
	FString ManifestText;
	const TSharedPtr<FJsonObject> Manifest = FFileHelper::LoadFileToString(ManifestText, *(FixtureDir() / TEXT("cases.json")))
		? N2CTest::ParseJsonObject(ManifestText) : nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Cases = nullptr;
	if (!Manifest.IsValid() || !Manifest->TryGetArrayField(TEXT("cases"), Cases) || !Cases || Cases->Num() == 0)
	{
		It(TEXT("finds Fixtures/LLM/Parser/cases.json"), [this]()
		{
			AddError(FString::Printf(TEXT("No readable cases in %s"), *(FixtureDir() / TEXT("cases.json"))));
		});
		return;
	}
	const bool bRunKnownDefects = FPlatformMisc::GetEnvironmentVariable(TEXT("N2C_RUN_KNOWN_DEFECTS")) == TEXT("1");
	const TSharedPtr<FJsonObject>* GraphObject = nullptr;
	Graph = Manifest->TryGetObjectField(TEXT("graph"), GraphObject) && GraphObject ? *GraphObject : MakeShared<FJsonObject>();

	for (const TSharedPtr<FJsonValue>& Value : *Cases)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(Object) || !Object)
		{
			continue;
		}
		FFixtureCase Case;
		Case.Name = StringOr(*Object, TEXT("name"));
		Case.Parser = StringOr(*Object, TEXT("parser"));
		Case.File = StringOr(*Object, TEXT("file"));
		Case.KnownDefect = StringOr(*Object, TEXT("known_defect"));
		(*Object)->TryGetNumberField(TEXT("http_status"), Case.HttpStatus);
		const TSharedPtr<FJsonObject>* ExpectObject = nullptr;
		Case.Expect = (*Object)->TryGetObjectField(TEXT("expect"), ExpectObject) && ExpectObject ? *ExpectObject : MakeShared<FJsonObject>();

		// Known defect: the case stays, disabled, until the card named in known_defect fixes it (README "Known defects").
		// N2C_RUN_KNOWN_DEFECTS=1 in the editor's environment runs them too, to see which still fail.
		if (Case.KnownDefect.IsEmpty() || bRunKnownDefects)
		{
			It(Case.Name, [this, Case]() { RunCase(Case); });
		}
		else
		{
			xIt(Case.Name, [this, Case]() { RunCase(Case); });
		}
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
