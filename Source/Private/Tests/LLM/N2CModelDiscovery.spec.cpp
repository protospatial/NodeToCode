// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Containers/Ticker.h"
#include "Core/N2CSettings.h"
#include "Core/N2CUserSettings.h"
#include "Core/Services/N2CTokenEstimationService.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "ISinglePropertyView.h"
#include "LLM/N2CHttpTransport.h"
#include "LLM/N2CModelCatalog.h"
#include "LLM/N2CModelDiscovery.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "Tests/N2CTestHelpers.h"

/**
 * Live model lists (card 0216, decision 0004 (c)). Model lists were compiled in: a model the provider released or shut
 * down needed a plugin release, and nothing told the user that the selected model was about to stop working. Now
 * FN2CModelDiscovery fetches a provider's list from its profile's models_path, one parser per format, and Project
 * Settings shows it under the catalog. Checks, on recorded provider answers (Fixtures/LLM/ModelLists, no keys) and the
 * fixture transport: Anthropic's two pages are both read (has_more, then after_id); an OpenAI entry with shutdown_date is
 * flagged; Gemini follows nextPageToken and keeps only generateContent models; Ollama tags and OpenAI-style local lists
 * parse; a model only the live list names has an unknown price, not $0; a deprecated model is marked in the Model
 * drop-down's options (as the property editor reads them); no request is sent in a commandlet or an -unattended run, or
 * for a provider not opted in; and the cache file round-trips.
 */
BEGIN_DEFINE_SPEC(FN2CModelDiscoverySpec, "NodeToCode.LLM.ModelDiscovery", N2C_TEST_FLAGS)

	TSharedPtr<FN2CFixtureHttpTransport> Transport;

	/** The settings the spec changes, restored afterwards */
	struct FScopedSettings
	{
		TArray<FString> LiveModelListProviders;
		FString AnthropicModelId;
		FString AnthropicCustomModelId;
		TMap<FString, FN2CModelPricing> ModelPricing;
		EN2CLLMProvider Provider;

		FScopedSettings()
		{
			const UN2CSettings* Settings = GetDefault<UN2CSettings>();
			LiveModelListProviders = Settings->LiveModelListProviders;
			AnthropicModelId = Settings->AnthropicModelId;
			AnthropicCustomModelId = Settings->AnthropicCustomModelId;
			ModelPricing = Settings->ModelPricing;
			Provider = GetDefault<UN2CUserSettings>()->Provider;
		}

		~FScopedSettings()
		{
			UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
			Settings->LiveModelListProviders = LiveModelListProviders;
			Settings->AnthropicModelId = AnthropicModelId;
			Settings->AnthropicCustomModelId = AnthropicCustomModelId;
			Settings->ModelPricing = ModelPricing;
			GetMutableDefault<UN2CUserSettings>()->Provider = Provider;
			FN2CModelDiscovery::Get().RemoveList(TEXT("anthropic"));
			FN2CTokenEstimationService::Get().RefreshModelInfo();
		}
	};

	static FString ReadFixture(const FString& Name)
	{
		FString Text;
		FFileHelper::LoadFileToString(Text, *(N2CTest::FixturesDir(TEXT("LLM/ModelLists")) / Name));
		return Text;
	}

	static FN2CModelDiscovery::FTarget MakeCatalogTarget(EN2CLLMProvider Provider)
	{
		FN2CModelDiscovery::FTarget Target;
		FString Error;
		FN2CModelDiscovery::MakeTarget(Provider, FString(), Target, Error);
		Target.Credential = FN2CCredential::Make(Provider, TEXT("n2c-test-key-not-real-0216"));
		return Target;
	}

	/** Fetches Target through the fixture transport and ticks until it finishes */
	TOptional<TTuple<bool, FN2CModelList, FString>> RunFetch(const FN2CModelDiscovery::FTarget& Target)
	{
		const TSharedRef<TOptional<TTuple<bool, FN2CModelList, FString>>> Result = MakeShared<TOptional<TTuple<bool, FN2CModelList, FString>>>();
		FN2CModelDiscovery::Get().Fetch(Target, [Result](bool bOk, const FN2CModelList& List, const FString& Error)
		{
			*Result = MakeTuple(bOk, List, Error);
		});
		const double End = FPlatformTime::Seconds() + 10.0;
		double Last = FPlatformTime::Seconds();
		while (FPlatformTime::Seconds() < End && !Result->IsSet())
		{
			FPlatformProcess::Sleep(0.005f);
			const double Now = FPlatformTime::Seconds();
			FTSTicker::GetCoreTicker().Tick(static_cast<float>(Now - Last));
			Last = Now;
		}
		return *Result;
	}

	static bool HasHeader(const FN2CHttpRequestSpec& Request, const FString& Name, const FString& Value)
	{
		return Request.Headers.ContainsByPredicate([&](const TPair<FString, FString>& Header)
		{
			return Header.Key.Equals(Name, ESearchCase::IgnoreCase) && Header.Value == Value;
		});
	}

	static FN2CModelList ParseFixture(EN2CModelListFormat Format, const FString& Name, FName ProviderId)
	{
		FN2CModelListPage Page;
		FString Error;
		FN2CModelDiscovery::ParsePage(Format, ReadFixture(Name), Page, Error);
		FN2CModelList List;
		List.ProviderId = ProviderId;
		List.FetchedAt = FDateTime(2026, 11, 3);
		List.Models = Page.Models;
		return List;
	}

END_DEFINE_SPEC(FN2CModelDiscoverySpec)

void FN2CModelDiscoverySpec::Define()
{
	BeforeEach([this]()
	{
		Transport = MakeShared<FN2CFixtureHttpTransport>();
		FN2CModelDiscovery::Get().SetTransport(Transport);
	});

	AfterEach([this]()
	{
		FN2CModelDiscovery::Get().SetTransport(nullptr);
		Transport.Reset();
	});

	Describe(TEXT("fetching"), [this]()
	{
		It(TEXT("reads both Anthropic pages: has_more true, then after_id with the last ID until has_more is false"), [this]()
		{
			Transport->Enqueue(200, ReadFixture(TEXT("anthropic_page1.json")));
			Transport->Enqueue(200, ReadFixture(TEXT("anthropic_page2.json")));
			const auto Result = RunFetch(MakeCatalogTarget(EN2CLLMProvider::Anthropic));
			if (!TestTrue(TEXT("it finished"), Result.IsSet()) || !TestTrue(TEXT("it succeeded"), Result->Get<0>()))
			{
				return;
			}
			const FN2CModelList& List = Result->Get<1>();
			TestEqual(TEXT("models from both pages"), List.Models.Num(), 6);
			const TArray<FN2CFixtureHttpTransport::FSent>& Sent = Transport->GetSent();
			if (TestEqual(TEXT("two requests"), Sent.Num(), 2))
			{
				TestEqual(TEXT("a GET"), Sent[0].Request.Verb, FString(TEXT("GET")));
				TestTrue(TEXT("no body"), Sent[0].Request.Body.IsEmpty());
				TestEqual(TEXT("first page"), Sent[0].Request.Url, FString(TEXT("https://api.anthropic.com/v1/models?limit=1000")));
				TestEqual(TEXT("second page starts after the last ID"), Sent[1].Request.Url,
					FString(TEXT("https://api.anthropic.com/v1/models?limit=1000&after_id=claude-fable-5-1")));
				TestTrue(TEXT("the key in x-api-key"), HasHeader(Sent[0].Request, TEXT("x-api-key"), TEXT("n2c-test-key-not-real-0216")));
				TestTrue(TEXT("anthropic-version"), HasHeader(Sent[1].Request, TEXT("anthropic-version"), TEXT("2023-06-01")));
				TestFalse(TEXT("no key in the URL"), Sent[0].Request.Url.Contains(TEXT("n2c-test-key")));
			}
			const FN2CDiscoveredModel* Haiku = List.Find(TEXT("claude-haiku-5-5"));
			if (TestNotNull(TEXT("claude-haiku-5-5 listed"), Haiku))
			{
				TestEqual(TEXT("display name"), Haiku->DisplayName, FString(TEXT("Claude Haiku 5.5")));
				TestEqual(TEXT("max_input_tokens"), Haiku->ContextWindow, 1000000);
				TestEqual(TEXT("max_tokens"), Haiku->MaxOutput, 128000);
				TestFalse(TEXT("active"), Haiku->bDeprecated);
			}
			const FN2CDiscoveredModel* Sonnet45 = List.Find(TEXT("claude-sonnet-4-5-20250929"));
			if (TestNotNull(TEXT("the second page's deprecated model"), Sonnet45))
			{
				TestTrue(TEXT("deprecated"), Sonnet45->bDeprecated);
				TestEqual(TEXT("retires_at as the shutdown date"), Sonnet45->ShutdownDate, FString(TEXT("2026-11-30")));
			}
		});

		It(TEXT("flags an OpenAI entry that has a shutdown_date and keeps the others active"), [this]()
		{
			Transport->Enqueue(200, ReadFixture(TEXT("openai_models.json")));
			const auto Result = RunFetch(MakeCatalogTarget(EN2CLLMProvider::OpenAI));
			if (!TestTrue(TEXT("it succeeded"), Result.IsSet() && Result->Get<0>()))
			{
				return;
			}
			TestEqual(TEXT("one request"), Transport->GetSent().Num(), 1);
			TestEqual(TEXT("url"), Transport->GetSent()[0].Request.Url, FString(TEXT("https://api.openai.com/v1/models")));
			TestTrue(TEXT("bearer key"), HasHeader(Transport->GetSent()[0].Request, TEXT("Authorization"), TEXT("Bearer n2c-test-key-not-real-0216")));
			const FN2CDiscoveredModel* Mini = Result->Get<1>().Find(TEXT("gpt-5-mini"));
			if (TestNotNull(TEXT("gpt-5-mini"), Mini))
			{
				TestTrue(TEXT("flagged"), Mini->bDeprecated);
				TestEqual(TEXT("shutdown date"), Mini->ShutdownDate, FString(TEXT("2026-12-11")));
			}
			const FN2CDiscoveredModel* Sol = Result->Get<1>().Find(TEXT("gpt-6.1-sol"));
			TestTrue(TEXT("a null shutdown_date is not a flag"), Sol && !Sol->bDeprecated && Sol->ShutdownDate.IsEmpty());
		});

		It(TEXT("follows Gemini's nextPageToken and keeps only models that answer generateContent"), [this]()
		{
			Transport->Enqueue(200, ReadFixture(TEXT("gemini_page1.json")));
			Transport->Enqueue(200, ReadFixture(TEXT("gemini_page2.json")));
			const auto Result = RunFetch(MakeCatalogTarget(EN2CLLMProvider::Gemini));
			if (!TestTrue(TEXT("it succeeded"), Result.IsSet() && Result->Get<0>()))
			{
				return;
			}
			const TArray<FN2CFixtureHttpTransport::FSent>& Sent = Transport->GetSent();
			if (TestEqual(TEXT("two requests"), Sent.Num(), 2))
			{
				TestEqual(TEXT("second page token"), Sent[1].Request.Url,
					FString(TEXT("https://generativelanguage.googleapis.com/v1beta/models?pageSize=1000&pageToken=Chltb2RlbHMvZ2VtaW5pLTMuNS1mbGFzaC1saXRl")));
				TestTrue(TEXT("x-goog-api-key"), HasHeader(Sent[0].Request, TEXT("x-goog-api-key"), TEXT("n2c-test-key-not-real-0216")));
			}
			const FN2CModelList& List = Result->Get<1>();
			TestEqual(TEXT("three generateContent models (embedding and live-only dropped)"), List.Models.Num(), 3);
			const FN2CDiscoveredModel* Argon = List.Find(TEXT("gemini-4-argon"));
			if (TestNotNull(TEXT("models/ prefix removed"), Argon))
			{
				TestTrue(TEXT("inputTokenLimit"), Argon->ContextWindow > 0);
				TestTrue(TEXT("outputTokenLimit"), Argon->MaxOutput > 0);
			}
			TestNull(TEXT("no embedding model"), List.Find(TEXT("gemini-embedding-2")));
		});

		It(TEXT("fails with the status and the provider's message, and sends nothing more"), [this]()
		{
			Transport->Enqueue(401, TEXT("{\"type\":\"error\",\"error\":{\"type\":\"authentication_error\",\"message\":\"invalid x-api-key\"}}"));
			const auto Result = RunFetch(MakeCatalogTarget(EN2CLLMProvider::Anthropic));
			if (!TestTrue(TEXT("it finished"), Result.IsSet()))
			{
				return;
			}
			TestFalse(TEXT("failed"), Result->Get<0>());
			TestTrue(TEXT("names the status and message"), Result->Get<2>().Contains(TEXT("HTTP 401")) && Result->Get<2>().Contains(TEXT("invalid x-api-key")));
			TestEqual(TEXT("one request"), Transport->GetSent().Num(), 1);
		});
	});

	Describe(TEXT("parsing"), [this]()
	{
		It(TEXT("reads an Ollama tags list"), [this]()
		{
			const FN2CModelList List = ParseFixture(EN2CModelListFormat::Ollama, TEXT("ollama_tags.json"), TEXT("ollama"));
			TestEqual(TEXT("two models"), List.Models.Num(), 2);
			TestNotNull(TEXT("qwen3:8b"), List.Find(TEXT("qwen3:8b")));
			TestNotNull(TEXT("gpt-oss:20b"), List.Find(TEXT("gpt-oss:20b")));
		});

		It(TEXT("reads OpenAI-style local lists (LM Studio, vLLM with max_model_len) and DeepSeek's limits"), [this]()
		{
			const FN2CModelList LMStudio = ParseFixture(EN2CModelListFormat::OpenAI, TEXT("lmstudio_models.json"), TEXT("lmstudio"));
			TestEqual(TEXT("LM Studio models"), LMStudio.Models.Num(), 2);
			TestNotNull(TEXT("qwen/qwen3-coder-30b"), LMStudio.Find(TEXT("qwen/qwen3-coder-30b")));
			const FN2CModelList VLLM = ParseFixture(EN2CModelListFormat::OpenAI, TEXT("vllm_models.json"), TEXT("vllm"));
			const FN2CDiscoveredModel* Qwen = VLLM.Find(TEXT("Qwen/Qwen3-8B"));
			TestTrue(TEXT("vLLM max_model_len as the context window"), Qwen && Qwen->ContextWindow == 32768);
			const FN2CModelList DeepSeek = ParseFixture(EN2CModelListFormat::OpenAI, TEXT("deepseek_models.json"), TEXT("deepseek"));
			const FN2CDiscoveredModel* Flash = DeepSeek.Find(TEXT("deepseek-flash"));
			TestTrue(TEXT("DeepSeek context_window and max_output_tokens"), Flash && Flash->ContextWindow > 0 && Flash->MaxOutput > 0);
		});

		It(TEXT("picks the format from the profile's dialect and builds the URL from base_url and models_path"), [this]()
		{
			const FN2CModelCatalog& Catalog = FN2CModelCatalog::Get();
			const FN2CProviderProfile* Vllm = Catalog.FindProvider(TEXT("vllm"));
			const FN2CProviderProfile* Ollama = Catalog.FindProvider(TEXT("ollama"));
			if (!TestNotNull(TEXT("vllm row"), Vllm) || !TestNotNull(TEXT("ollama row"), Ollama))
			{
				return;
			}
			TestEqual(TEXT("vllm format"), FN2CModelDiscovery::FormatFor(*Vllm), EN2CModelListFormat::OpenAI);
			TestEqual(TEXT("vllm url"), FN2CModelDiscovery::MakePageUrl(*Vllm, FString()), FString(TEXT("http://localhost:8000/v1/models")));
			TestEqual(TEXT("ollama format"), FN2CModelDiscovery::FormatFor(*Ollama), EN2CModelListFormat::Ollama);
			TestEqual(TEXT("ollama url"), FN2CModelDiscovery::MakePageUrl(*Ollama, FString()), FString(TEXT("http://localhost:11434/api/tags")));
			FN2CCustomProviderProfile Custom;
			Custom.Id = TEXT("gpu-box");
			Custom.BaseUrl = TEXT("http://gpu-box:8000/v1/");
			TestEqual(TEXT("a custom profile's list"), FN2CModelDiscovery::MakePageUrl(Custom.ToProfile(), FString()), FString(TEXT("http://gpu-box:8000/v1/models")));
		});

		It(TEXT("round-trips the cache file"), [this]()
		{
			N2CTest::FScratchDir Dir(TEXT("ModelCache0216"));
			FN2CModelList List = ParseFixture(EN2CModelListFormat::Anthropic, TEXT("anthropic_page2.json"), TEXT("anthropic"));
			List.Endpoint = TEXT("https://api.anthropic.com/v1/models?limit=1000");
			List.FetchedAt = FDateTime(2026, 11, 3, 14, 5, 0);
			if (!TestTrue(TEXT("saved"), FN2CModelDiscovery::SaveCache(List, Dir.Path)))
			{
				return;
			}
			FN2CModelList Loaded;
			if (!TestTrue(TEXT("loaded"), FN2CModelDiscovery::LoadCache(TEXT("anthropic"), Dir.Path, Loaded)))
			{
				return;
			}
			TestEqual(TEXT("provider"), Loaded.ProviderId, FName(TEXT("anthropic")));
			TestEqual(TEXT("endpoint"), Loaded.Endpoint, List.Endpoint);
			TestEqual(TEXT("fetched at"), Loaded.FetchedAt, List.FetchedAt);
			if (TestEqual(TEXT("models"), Loaded.Models.Num(), List.Models.Num()))
			{
				for (int32 Index = 0; Index < List.Models.Num(); ++Index)
				{
					const FN2CDiscoveredModel& A = List.Models[Index];
					const FN2CDiscoveredModel& B = Loaded.Models[Index];
					TestTrue(FString::Printf(TEXT("model %d equal"), Index), A.Id == B.Id && A.DisplayName == B.DisplayName
						&& A.ContextWindow == B.ContextWindow && A.MaxOutput == B.MaxOutput && A.bDeprecated == B.bDeprecated && A.ShutdownDate == B.ShutdownDate);
				}
			}
		});
	});

	Describe(TEXT("gating"), [this]()
	{
		It(TEXT("never runs in a commandlet or an unattended session"), [this]()
		{
			TestEqual(TEXT("commandlet"), FN2CModelDiscovery::GetEnvironmentSkip(true, false), EN2CModelDiscoverySkip::Commandlet);
			TestEqual(TEXT("unattended"), FN2CModelDiscovery::GetEnvironmentSkip(false, true), EN2CModelDiscoverySkip::Unattended);
			TestEqual(TEXT("an editor someone uses"), FN2CModelDiscovery::GetEnvironmentSkip(false, false), EN2CModelDiscoverySkip::None);
		});

		It(TEXT("sends no request when IsRunningCommandlet() is true, even for an opted-in provider with a key"), [this]()
		{
			FScopedSettings Scope;
			GetMutableDefault<UN2CSettings>()->LiveModelListProviders = { TEXT("anthropic") };
			const bool bWasCommandlet = PRIVATE_GIsRunningCommandlet;
			PRIVATE_GIsRunningCommandlet = true;
			ON_SCOPE_EXIT { PRIVATE_GIsRunningCommandlet = bWasCommandlet; };
			TestTrue(TEXT("IsRunningCommandlet()"), IsRunningCommandlet());
			const EN2CModelDiscoverySkip Skip = FN2CModelDiscovery::Get().RequestRefresh(EN2CLLMProvider::Anthropic, FString(), TEXT("spec"));
			TestEqual(TEXT("skipped as a commandlet"), Skip, EN2CModelDiscoverySkip::Commandlet);
			TestEqual(TEXT("nothing sent"), Transport->GetSent().Num(), 0);
		});

		It(TEXT("sends no request in this -unattended automation run, and none for a provider not opted in"), [this]()
		{
			FScopedSettings Scope;
			if (!FApp::IsUnattended())
			{
				AddInfo(TEXT("This editor is not -unattended (build.ps1 -Test always is): the unattended half is not checked here"));
				return;
			}
			GetMutableDefault<UN2CSettings>()->LiveModelListProviders = { TEXT("anthropic") };
			const EN2CModelDiscoverySkip Skip = FN2CModelDiscovery::Get().RequestRefresh(EN2CLLMProvider::Anthropic, FString(), TEXT("spec"));
			TestEqual(TEXT("skipped as unattended"), Skip, EN2CModelDiscoverySkip::Unattended);
			TestNotEqual(TEXT("not sent"), Skip, EN2CModelDiscoverySkip::None);
			TestEqual(TEXT("nothing sent"), Transport->GetSent().Num(), 0);
			GetMutableDefault<UN2CSettings>()->LiveModelListProviders.Reset();
			TestFalse(TEXT("not opted in"), FN2CModelDiscovery::IsOptedIn(TEXT("anthropic")));
		});
	});

	Describe(TEXT("the discovery layer in Project Settings"), [this]()
	{
		It(TEXT("prices a model only the live list names as unknown, not $0, and takes its limits from the list"), [this]()
		{
			FScopedSettings Scope;
			UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
			Settings->LiveModelListProviders = { TEXT("anthropic") };
			Settings->ModelPricing.Remove(TEXT("claude-fable-5-1"));
			Settings->AnthropicCustomModelId.Reset();
			GetMutableDefault<UN2CUserSettings>()->Provider = EN2CLLMProvider::Anthropic;
			FN2CModelDiscovery::Get().SetList(ParseFixture(EN2CModelListFormat::Anthropic, TEXT("anthropic_page1.json"), TEXT("anthropic")));

			TestTrue(TEXT("offered as a Model option"), Settings->GetAnthropicModelOptions().Contains(TEXT("claude-fable-5-1")));
			Settings->AnthropicModelId = TEXT("claude-fable-5-1");
			TestNull(TEXT("no catalog row"), FN2CModelCatalog::Get().Find(TEXT("anthropic"), TEXT("claude-fable-5-1")));
			TestFalse(TEXT("price unknown"), Settings->IsActivePricingKnown(EN2CLLMProvider::Anthropic));
			FN2CTokenEstimationService& Service = FN2CTokenEstimationService::Get();
			Service.RefreshModelInfo();
			TestFalse(TEXT("the estimate says unknown"), Service.IsPriceKnown());
			TestFalse(TEXT("not local"), Service.IsLocalProvider());
			TestEqual(TEXT("the list's display name"), Service.GetCurrentModelName(), FString(TEXT("Claude Fable 5.1")));

			const TArray<FPropertyTextString> Choices = Settings->GetAnthropicModelChoices();
			const FPropertyTextString* Fable = Choices.FindByPredicate([](const FPropertyTextString& C) { return C.ValueString == TEXT("claude-fable-5-1"); });
			TestTrue(TEXT("labelled price unknown"), Fable && Fable->DisplayName.ToString().Contains(TEXT("price unknown")));

			// A catalog model keeps its curated price
			Settings->AnthropicModelId = TEXT("claude-haiku-5-5");
			TestTrue(TEXT("catalog price known"), Settings->IsActivePricingKnown(EN2CLLMProvider::Anthropic));
		});

		It(TEXT("marks a deprecated model in the Model drop-down as the property editor reads it"), [this]()
		{
			FScopedSettings Scope;
			UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
			FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
			const TSharedPtr<ISinglePropertyView> View = PropertyEditor.CreateSingleProperty(Settings,
				GET_MEMBER_NAME_CHECKED(UN2CSettings, AnthropicModelId), FSinglePropertyParams());
			const TSharedPtr<IPropertyHandle> Handle = View.IsValid() ? View->GetPropertyHandle() : nullptr;
			if (!TestTrue(TEXT("a handle for Model"), Handle.IsValid()))
			{
				return;
			}
			TArray<FString> Values;
			TArray<FText> ToolTips;
			TArray<bool> Restricted;
			TArray<FText> DisplayNames;
			TestTrue(TEXT("the options have display names"), Handle->GeneratePossibleValues(Values, ToolTips, Restricted, &DisplayNames));
			const int32 Index = Values.IndexOfByKey(FString(TEXT("claude-sonnet-4-5-20250929")));
			if (TestTrue(TEXT("the deprecated catalog row is an option"), Index != INDEX_NONE) && TestEqual(TEXT("one name per value"), DisplayNames.Num(), Values.Num()))
			{
				TestTrue(TEXT("marked deprecated with its date"), DisplayNames[Index].ToString().Contains(TEXT("deprecated, shuts down 2026-11-30")));
			}
			const int32 Haiku = Values.IndexOfByKey(FString(TEXT("claude-haiku-5-5")));
			TestTrue(TEXT("an active model's value is its ID alone"), Haiku != INDEX_NONE && DisplayNames[Haiku].ToString() == TEXT("claude-haiku-5-5"));
		});

		It(TEXT("warns when the selected model is missing from the list or the provider deprecates it"), [this]()
		{
			FScopedSettings Scope;
			UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
			Settings->LiveModelListProviders = { TEXT("anthropic") };
			Settings->AnthropicCustomModelId.Reset();
			FN2CModelDiscovery::Get().SetList(ParseFixture(EN2CModelListFormat::Anthropic, TEXT("anthropic_page1.json"), TEXT("anthropic")));

			Settings->AnthropicModelId = TEXT("claude-haiku-5-5");
			TestTrue(TEXT("a listed active model: no warning"), FN2CModelDiscovery::Get().GetSelectedModelWarning(EN2CLLMProvider::Anthropic).IsEmpty());

			Settings->AnthropicModelId = TEXT("claude-opus-4-5-20251101");
			TestTrue(TEXT("missing from the list"), FN2CModelDiscovery::Get().GetSelectedModelWarning(EN2CLLMProvider::Anthropic).Contains(TEXT("not in the model list")));

			Settings->AnthropicModelId = TEXT("claude-sonnet-4-5-20250929");
			const FString Deprecated = FN2CModelDiscovery::Get().GetSelectedModelWarning(EN2CLLMProvider::Anthropic);
			TestTrue(TEXT("the catalog's deprecation and date"), Deprecated.Contains(TEXT("deprecated")) && Deprecated.Contains(TEXT("2026-11-30")));

			// Not opted in: the list is not read
			Settings->LiveModelListProviders.Reset();
			Settings->AnthropicModelId = TEXT("claude-opus-4-5-20251101");
			TestTrue(TEXT("no list, no missing-model warning"), FN2CModelDiscovery::Get().GetSelectedModelWarning(EN2CLLMProvider::Anthropic).IsEmpty());
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
