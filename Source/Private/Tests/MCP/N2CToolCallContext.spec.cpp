// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Core/N2CEditorIntegration.h"
#include "LLM/N2CLLMModelRegistry.h"
#include "LLM/N2CLLMRequestHandle.h"
#include "MCP/Async/N2CToolAsyncTaskManager.h"
#include "MCP/Tools/N2CToolCallContext.h"
#include "MCP/Tools/Implementations/Translation/N2CMcpTranslateBlueprintTool.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace N2CToolCallContextSpecPrivate
{
	/** The request the translate tool sent instead of an LLM call: pending until the test completes it */
	struct FFakeRequest
	{
		int32 Starts = 0;
		FN2CLLMRequestHandlePtr Handle;
		FOnLLMResponseReceived OnComplete;
	};

	/** What a call's progress sink and future delivered */
	struct FObserved
	{
		TArray<float> Progress;
		TArray<FString> Messages;
		int32 Completions = 0;
		bool bIsError = false;
		FString Text;
	};

	static FString ResultText(const FMcpToolCallResult& Result)
	{
		FString Text;
		if (Result.Content.Num() > 0 && Result.Content[0].IsValid())
		{
			Result.Content[0]->TryGetStringField(TEXT("text"), Text);
		}
		return Text;
	}
}

/**
 * The call context long-running tools get (FN2CToolCallContext), driven through translate-focused-blueprint's own
 * InvokeAsync with its LLM request replaced by a pending handle: progress reaches the context's sink, a cancel aborts
 * the request and completes the call exactly once, and unknown provider or language IDs are refused with the valid
 * IDs. Review ids: arch-arch-mcp-5, editor-integration-20 (card 0107).
 */
BEGIN_DEFINE_SPEC(FN2CToolCallContextSpec, "NodeToCode.MCP.ToolCallContext", N2C_TEST_FLAGS)

	TSharedPtr<N2CToolCallContextSpecPrivate::FFakeRequest> Fake;
	TSharedPtr<FN2CMcpTranslateBlueprintTool> Tool;
	TArray<FN2CToolCallContextPtr> Contexts;

	/** A context whose progress goes into Observed */
	FN2CToolCallContextRef MakeContext(const TSharedRef<N2CToolCallContextSpecPrivate::FObserved>& Observed)
	{
		const FN2CToolCallContextRef Context = FN2CToolCallContext::Create([Observed](float Progress, const FString& Message)
		{
			Observed->Progress.Add(Progress);
			Observed->Messages.Add(Message);
		});
		Contexts.Add(Context);
		return Context;
	}

	/** Invokes the translate tool with Arguments and counts what its future delivers */
	void Invoke(const FN2CToolCallContextRef& Context, const TSharedRef<N2CToolCallContextSpecPrivate::FObserved>& Observed,
		const TSharedRef<FJsonObject>& Arguments)
	{
		TFuture<FMcpToolCallResult> Future = Tool->InvokeAsync(Context, Arguments);
		if (!TestTrue(TEXT("InvokeAsync returns a valid future"), Future.IsValid()))
		{
			return;
		}
		Future.Next([Observed](FMcpToolCallResult Result)
		{
			++Observed->Completions;
			Observed->bIsError = Result.bIsError;
			Observed->Text = N2CToolCallContextSpecPrivate::ResultText(Result);
		});
	}

	static TSharedRef<FJsonObject> Args(const FString& Provider, const FString& Model, const FString& Language)
	{
		const TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
		if (!Provider.IsEmpty()) { Arguments->SetStringField(TEXT("provider"), Provider); }
		if (!Model.IsEmpty()) { Arguments->SetStringField(TEXT("model"), Model); }
		if (!Language.IsEmpty()) { Arguments->SetStringField(TEXT("language"), Language); }
		return Arguments;
	}

	/** Delivers Response through the fake request as the LLM layer would; false when the handle refused it */
	bool CompleteFake(const FString& Response)
	{
		return Fake.IsValid() && Fake->Handle.IsValid() && Fake->Handle->Complete(Fake->OnComplete, Response);
	}

END_DEFINE_SPEC(FN2CToolCallContextSpec)

void FN2CToolCallContextSpec::Define()
{
	using namespace N2CToolCallContextSpecPrivate;

	BeforeEach([this]()
	{
		Fake = MakeShared<FFakeRequest>();
		Tool = MakeShared<FN2CMcpTranslateBlueprintTool>();
		const TSharedPtr<FFakeRequest> Captured = Fake;
		FN2CMcpTranslateBlueprintTool::SetRequestStarterForTesting([Captured](const FString&, const FString&, const FString&,
			const FOnLLMResponseReceived& OnComplete)
		{
			++Captured->Starts;
			const FN2CLLMRequestHandleRef Handle = FN2CLLMRequestHandle::Create();
			Captured->Handle = Handle;
			Captured->OnComplete = OnComplete;
			return Handle;
		});
	});

	AfterEach([this]()
	{
		// A call still waiting is cancelled, so no ticker or call outlives the case
		for (const FN2CToolCallContextPtr& Context : Contexts)
		{
			Context->Cancel();
		}
		Contexts.Reset();
		FN2CMcpTranslateBlueprintTool::SetRequestStarterForTesting(nullptr);
		FN2CToolAsyncTaskManager::Get().SetToolResolverForTesting(nullptr);
		Tool.Reset();
		Fake.Reset();
	});

	Describe(TEXT("progress"), [this]()
	{
		It(TEXT("reaches the context sink, rising, through to the result"), [this]()
		{
			const TSharedRef<FObserved> Observed = MakeShared<FObserved>();
			const FN2CToolCallContextRef Context = MakeContext(Observed);
			Invoke(Context, Observed, Args(TEXT("ollama"), TEXT("n2c-test"), TEXT("cpp")));

			TestEqual(TEXT("the request was started once"), Fake->Starts, 1);
			TestEqual(TEXT("nothing completed while the request is pending"), Observed->Completions, 0);
			TestTrue(TEXT("collect and sent stages reached the sink"), Observed->Progress.Num() >= 2);

			TestTrue(TEXT("the fake LLM response was delivered"), CompleteFake(TEXT("{\"graphs\":[]}")));
			TestEqual(TEXT("one result"), Observed->Completions, 1);
			TestFalse(TEXT("the result is not an error"), Observed->bIsError);
			TestTrue(TEXT("parse and complete stages reached the sink"), Observed->Progress.Num() >= 4);
			if (Observed->Progress.Num() > 0)
			{
				TestEqual(TEXT("the last progress is 1"), Observed->Progress.Last(), 1.0f);
			}
			for (int32 Index = 1; Index < Observed->Progress.Num(); ++Index)
			{
				TestTrue(FString::Printf(TEXT("progress %d rises"), Index), Observed->Progress[Index] > Observed->Progress[Index - 1]);
			}

			const int32 Reports = Observed->Progress.Num();
			Context->MarkCompleted(); // what the manager does once it delivered the result
			Context->ReportProgress(0.99f, TEXT("late"));
			TestEqual(TEXT("progress after the result is dropped"), Observed->Progress.Num(), Reports);
		});
	});

	Describe(TEXT("cancel"), [this]()
	{
		It(TEXT("before completion gives one cancelled result, aborts the request and allows no later callback"), [this]()
		{
			const TSharedRef<FObserved> Observed = MakeShared<FObserved>();
			const FN2CToolCallContextRef Context = MakeContext(Observed);
			Invoke(Context, Observed, Args(TEXT("ollama"), TEXT("n2c-test"), TEXT("cpp")));

			TestTrue(TEXT("first cancel is accepted"), Context->Cancel());
			TestEqual(TEXT("one result at once"), Observed->Completions, 1);
			TestTrue(TEXT("the result is an error"), Observed->bIsError);
			TestTrue(TEXT("the result says cancelled"), Observed->Text.Contains(TEXT("cancelled")));
			TestTrue(TEXT("the provider request was cancelled"), Fake->Handle.IsValid() && Fake->Handle->IsCancelled());

			TestFalse(TEXT("a late LLM response is refused by the handle"), CompleteFake(TEXT("{\"graphs\":[]}")));
			TestFalse(TEXT("a second cancel is refused"), Context->Cancel());
			TestEqual(TEXT("still exactly one result"), Observed->Completions, 1);
		});

		It(TEXT("after completion changes nothing"), [this]()
		{
			const TSharedRef<FObserved> Observed = MakeShared<FObserved>();
			const FN2CToolCallContextRef Context = MakeContext(Observed);
			Invoke(Context, Observed, Args(TEXT("ollama"), TEXT("n2c-test"), TEXT("cpp")));
			CompleteFake(TEXT("{\"graphs\":[]}"));
			Context->MarkCompleted(); // what the manager does once it delivered the result
			TestFalse(TEXT("cancel after the result is refused"), Context->Cancel());
			TestEqual(TEXT("one result"), Observed->Completions, 1);
			TestFalse(TEXT("the result is the translation"), Observed->bIsError);
		});

		It(TEXT("through the manager writes one cancelled response and finds the call by request id first"), [this]()
		{
			const TSharedRef<int32> Responses = MakeShared<int32>(0);
			const TSharedRef<FString> LastResponse = MakeShared<FString>();
			FN2CAsyncTaskResponder Responder;
			Responder.SendResponse = [Responses, LastResponse](const FString& Json) { ++*Responses; *LastResponse = Json; };

			FN2CToolAsyncTaskManager& Manager = FN2CToolAsyncTaskManager::Get();
			const FGuid TaskId = Manager.LaunchToolCall(Tool.ToSharedRef(), FGuid::NewGuid(), TEXT("translate-focused-blueprint"),
				Args(TEXT("ollama"), TEXT("n2c-test"), TEXT("cpp")), FGuid::NewGuid().ToString(), TEXT("n2c-spec-session"),
				MakeShared<FJsonValueNumber>(42), Responder);

			TestTrue(TEXT("the call is running"), Manager.IsTaskRunning(TaskId));
			TestEqual(TEXT("found by session and numeric id"), Manager.FindTaskByRequestId(TEXT("n2c-spec-session"), MakeShared<FJsonValueNumber>(42)), TaskId);
			TestFalse(TEXT("the string \"42\" is another id"), Manager.FindTaskByRequestId(TEXT("n2c-spec-session"), MakeShared<FJsonValueString>(TEXT("42"))).IsValid());
			TestFalse(TEXT("another session's id does not match"), Manager.FindTaskByRequestId(TEXT("other-session"), MakeShared<FJsonValueNumber>(42)).IsValid());

			TestTrue(TEXT("cancel is accepted"), Manager.CancelTask(TaskId));
			TestEqual(TEXT("one response"), *Responses, 1);
			TestTrue(TEXT("the response carries the cancelled error"), LastResponse->Contains(TEXT("cancelled")) && LastResponse->Contains(TEXT("\"isError\":true")));
			TestTrue(TEXT("the provider request was cancelled"), Fake->Handle.IsValid() && Fake->Handle->IsCancelled());
			TestFalse(TEXT("the call is no longer running"), Manager.IsTaskRunning(TaskId));
			TestFalse(TEXT("a second cancel is refused"), Manager.CancelTask(TaskId));
			TestFalse(TEXT("a late LLM response is refused"), CompleteFake(TEXT("{\"graphs\":[]}")));
			TestEqual(TEXT("still one response"), *Responses, 1);
		});
	});

	Describe(TEXT("overrides"), [this]()
	{
		It(TEXT("refuse an unknown language with isError listing the valid IDs"), [this]()
		{
			const TSharedRef<FObserved> Observed = MakeShared<FObserved>();
			Invoke(MakeContext(Observed), Observed, Args(FString(), FString(), TEXT("rust")));
			TestEqual(TEXT("one result at once"), Observed->Completions, 1);
			TestTrue(TEXT("isError"), Observed->bIsError);
			TestTrue(TEXT("names the bad ID"), Observed->Text.Contains(TEXT("'rust'")));
			for (const FString& Id : FN2CEditorIntegration::GetTranslationLanguageIds())
			{
				TestTrue(FString::Printf(TEXT("lists %s"), *Id), Observed->Text.Contains(Id));
			}
			TestEqual(TEXT("no request was sent"), Fake->Starts, 0);
		});

		It(TEXT("refuse an unknown provider with isError listing the valid IDs"), [this]()
		{
			const TSharedRef<FObserved> Observed = MakeShared<FObserved>();
			Invoke(MakeContext(Observed), Observed, Args(TEXT("openia"), FString(), TEXT("cpp")));
			TestEqual(TEXT("one result at once"), Observed->Completions, 1);
			TestTrue(TEXT("isError"), Observed->bIsError);
			TestTrue(TEXT("names the bad ID"), Observed->Text.Contains(TEXT("'openia'")));
			TestTrue(TEXT("lists anthropic"), Observed->Text.Contains(TEXT("anthropic")));
			TestTrue(TEXT("lists lmstudio"), Observed->Text.Contains(TEXT("lmstudio")));
			TestEqual(TEXT("no request was sent"), Fake->Starts, 0);
		});

		It(TEXT("match the listed IDs in any case"), [this]()
		{
			FN2CTranslationOverrides Overrides;
			FString Error;
			TestTrue(TEXT("ANTHROPIC and Python resolve"), FN2CEditorIntegration::ResolveTranslationOverrides(TEXT("ANTHROPIC"), FString(), TEXT("Python"), Overrides, Error));
			TestTrue(TEXT("provider is Anthropic"), Overrides.Provider.IsSet() && Overrides.Provider.GetValue() == EN2CLLMProvider::Anthropic);
			TestTrue(TEXT("language is Python"), Overrides.Language.IsSet() && Overrides.Language.GetValue() == EN2CCodeLanguage::Python);
			TestTrue(TEXT("lmstudio and csharp resolve"), FN2CEditorIntegration::ResolveTranslationOverrides(TEXT("lmstudio"), FString(), TEXT("csharp"), Overrides, Error));
			TestTrue(TEXT("empty IDs mean the defaults"), FN2CEditorIntegration::ResolveTranslationOverrides(FString(), FString(), FString(), Overrides, Error)
				&& !Overrides.Provider.IsSet() && !Overrides.Language.IsSet());
			TestTrue(TEXT("the provider list is the lowercase enum names"), FN2CEditorIntegration::GetTranslationProviderIds().Contains(TEXT("deepseek")));
		});

		It(TEXT("add a warnings entry for an unrecognised model ID and still translate"), [this]()
		{
			const TSharedRef<FObserved> Observed = MakeShared<FObserved>();
			Invoke(MakeContext(Observed), Observed, Args(TEXT("anthropic"), TEXT("claude-n2c-not-a-model"), TEXT("cpp")));
			TestEqual(TEXT("the request was sent"), Fake->Starts, 1);
			CompleteFake(TEXT("{\"graphs\":[]}"));
			TestFalse(TEXT("not an error"), Observed->bIsError);

			TSharedPtr<FJsonObject> Root;
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Observed->Text), Root);
			const TArray<TSharedPtr<FJsonValue>>* Warnings = nullptr;
			if (TestTrue(TEXT("the result has a warnings array"), Root.IsValid() && Root->TryGetArrayField(TEXT("warnings"), Warnings))
				&& TestEqual(TEXT("one warning"), Warnings->Num(), 1))
			{
				TestTrue(TEXT("it names the model"), (*Warnings)[0]->AsString().Contains(TEXT("claude-n2c-not-a-model")));
			}
			TestTrue(TEXT("the translation is still there"), Root.IsValid() && Root->HasField(TEXT("graphs")));
		});

		It(TEXT("add no warning for a listed model or a local provider"), [this]()
		{
			FString Listed;
			for (const FN2CModelMetadata& Meta : FN2CLLMModelRegistry::Get().GetAllModelsForProvider(EN2CLLMProvider::Anthropic))
			{
				if (!Meta.bIsRetired)
				{
					Listed = Meta.ModelId;
					break;
				}
			}
			FN2CTranslationOverrides Overrides;
			FString Error;
			if (TestFalse(TEXT("the registry lists a live Anthropic model"), Listed.IsEmpty()))
			{
				FN2CEditorIntegration::ResolveTranslationOverrides(TEXT("anthropic"), Listed, TEXT("cpp"), Overrides, Error);
				TestEqual(TEXT("no warning for a listed model"), Overrides.Warnings.Num(), 0);
			}
			FN2CEditorIntegration::ResolveTranslationOverrides(TEXT("ollama"), TEXT("any-local-model"), TEXT("cpp"), Overrides, Error);
			TestEqual(TEXT("no warning for a local provider's model"), Overrides.Warnings.Num(), 0);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
