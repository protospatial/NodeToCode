// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CSettings.h"
#include "Core/N2CUserSettings.h"
#include "Dom/JsonObject.h"
#include "Misc/ScopeExit.h"
#include "LLM/Dialects/N2COpenAIChatDialect.h"
#include "LLM/N2CCredentialSource.h"
#include "LLM/N2CLLMModule.h"
#include "LLM/N2CLLMPayloadBuilder.h"
#include "LLM/N2CLLMProviderRegistry.h"
#include "LLM/N2CModelCatalog.h"
#include "LLM/Providers/N2COpenAICompatibleService.h"
#include "Tests/N2CTestHelpers.h"
#include "UObject/Package.h"

/**
 * One OpenAI-compatible adapter with presets (card 0218). DeepSeek and LM Studio had their own service and parser, and
 * vLLM, llama.cpp, OpenRouter, Groq, Azure and the rest had no path. Now FN2COpenAIChatDialect writes every
 * OpenAI-compatible request from a provider profile (a catalog row or a profile of the user's own): the output
 * format, budget field, thinking fragments, extra headers and key header are data. Checks: each preset's encoded
 * request (vLLM strict json_schema with max_tokens, DeepSeek json_object and its thinking fragment, LM Studio's strict
 * as a JSON boolean, OpenRouter's attribution headers, Azure's api-key header and max_completion_tokens); a custom
 * profile at http://localhost:8000/v1 sends the model of each request, not one held anywhere else; Provider=DeepSeek
 * resolves to the DeepSeek preset; and the dialect reads answers, usage, reasoning and a cut-off. The DeepSeek and
 * LM Studio payload goldens (NodeToCode.LLM.Payload.Golden) prove the rewrite sends what the old services sent.
 */
BEGIN_DEFINE_SPEC(FN2COpenAICompatibleSpec, "NodeToCode.LLM.OpenAICompatible", N2C_TEST_FLAGS)

	/** The user's OpenAI-compatible settings, restored when the scope ends */
	struct FScopedUserSettings
	{
		EN2CLLMProvider Provider;
		FString Profile;
		FString Model;
		FString BaseUrl;
		TArray<FN2CCustomProviderProfile> Custom;

		FScopedUserSettings()
		{
			const UN2CUserSettings* User = GetDefault<UN2CUserSettings>();
			Provider = User->Provider;
			Profile = User->ProviderProfile;
			Model = User->OpenAICompatibleModel;
			BaseUrl = User->OpenAICompatibleBaseUrl;
			Custom = User->CustomProviderProfiles;
			GetMutableDefault<UN2CUserSettings>()->OpenAICompatibleBaseUrl.Reset();
		}

		~FScopedUserSettings()
		{
			UN2CUserSettings* User = GetMutableDefault<UN2CUserSettings>();
			User->Provider = Provider;
			User->ProviderProfile = Profile;
			User->OpenAICompatibleModel = Model;
			User->OpenAICompatibleBaseUrl = BaseUrl;
			User->CustomProviderProfiles = Custom;
		}
	};

	static FN2CProviderProfile Preset(const TCHAR* Id)
	{
		const FN2CProviderProfile* Row = FN2CModelCatalog::Get().FindProvider(FName(Id));
		return Row ? *Row : FN2CProviderProfile();
	}

	static FN2CLLMRequest MakeRequest(const FString& Model, int32 MaxOutputTokens = 0)
	{
		FN2CLLMRequest Request;
		Request.Model = Model;
		Request.SystemPrompt = TEXT("You are a NodeToCode translator.");
		Request.UserMessage = TEXT("Translate this Blueprint graph: {\"nodes\":[]}");
		Request.MaxOutputTokens = MaxOutputTokens;
		Request.ResponseSchema = UN2CLLMPayloadBuilder::GetN2CResponseSchema();
		return Request;
	}

	/** EncodeRequest's URL, headers and body (parsed) */
	bool Encode(const FN2CLLMRequest& Request, const FN2CProviderProfile& Profile, FN2CHttpRequestSpec& OutSpec, TSharedPtr<FJsonObject>& OutBody)
	{
		FString Error;
		if (!TestTrue(FString::Printf(TEXT("%s encodes (%s)"), *Profile.Id.ToString(), *Error),
			FN2COpenAIChatDialect::Get().EncodeRequest(Request, Profile, OutSpec, Error)))
		{
			return false;
		}
		OutBody = N2CTest::ParseJsonObject(OutSpec.Body);
		return TestTrue(TEXT("the body is JSON"), OutBody.IsValid());
	}

	static FString Header(const FN2CHttpRequestSpec& Spec, const FString& Name)
	{
		for (const TPair<FString, FString>& Pair : Spec.Headers)
		{
			if (Pair.Key.Equals(Name, ESearchCase::IgnoreCase))
			{
				return Pair.Value;
			}
		}
		return FString();
	}

	/** response_format.json_schema.strict as the JSON value it is sent as */
	static TSharedPtr<FJsonValue> StrictValue(const TSharedPtr<FJsonObject>& Body)
	{
		const TSharedPtr<FJsonObject>* Format = nullptr;
		const TSharedPtr<FJsonObject>* Schema = nullptr;
		if (Body.IsValid() && Body->TryGetObjectField(TEXT("response_format"), Format) && (*Format)->TryGetObjectField(TEXT("json_schema"), Schema))
		{
			return (*Schema)->TryGetField(TEXT("strict"));
		}
		return nullptr;
	}

	/** The body a service would send for Config (FormatRequestPayload) */
	TSharedPtr<FJsonObject> ServiceBody(const FN2CLLMConfig& Config, UN2COpenAICompatibleService*& OutService)
	{
		OutService = NewObject<UN2COpenAICompatibleService>(GetTransientPackage());
		if (!TestTrue(TEXT("the service initializes"), OutService->Initialize(Config)))
		{
			return nullptr;
		}
		return N2CTest::ParseJsonObject(OutService->BuildRequestPayloadForTesting(TEXT("Translate this graph."), TEXT("You are a translator.")));
	}

END_DEFINE_SPEC(FN2COpenAICompatibleSpec)

void FN2COpenAICompatibleSpec::Define()
{
	Describe(TEXT("presets"), [this]()
	{
		It(TEXT("vLLM: json_schema with strict true, the budget in max_tokens, at localhost:8000"), [this]()
		{
			FN2CHttpRequestSpec Spec;
			TSharedPtr<FJsonObject> Body;
			const FN2CProviderProfile Profile = Preset(TEXT("vllm"));
			if (!Encode(MakeRequest(TEXT("Qwen/Qwen3-32B"), 4096), Profile, Spec, Body)) { return; }
			TestEqual(TEXT("endpoint"), Spec.Url, FString(TEXT("http://localhost:8000/v1/chat/completions")));
			TestEqual(TEXT("model from the request"), Body->GetStringField(TEXT("model")), FString(TEXT("Qwen/Qwen3-32B")));
			TestEqual(TEXT("max_tokens"), static_cast<int32>(Body->GetNumberField(TEXT("max_tokens"))), 4096);
			TestFalse(TEXT("no max_completion_tokens"), Body->HasField(TEXT("max_completion_tokens")));
			const TSharedPtr<FJsonValue> Strict = StrictValue(Body);
			TestTrue(TEXT("strict is the JSON boolean true"), Strict.IsValid() && Strict->Type == EJson::Boolean && Strict->AsBool());
			TestFalse(TEXT("no thinking fragment unless asked"), Body->HasField(TEXT("chat_template_kwargs")));

			FN2CLLMRequest Thinking = MakeRequest(TEXT("Qwen/Qwen3-32B"));
			Thinking.ReasoningEffort = TEXT("none");
			if (Encode(Thinking, Profile, Spec, Body))
			{
				const TSharedPtr<FJsonObject>* Kwargs = nullptr;
				TestTrue(TEXT("thinking off is vLLM's chat_template_kwargs.enable_thinking false"),
					Body->TryGetObjectField(TEXT("chat_template_kwargs"), Kwargs) && !(*Kwargs)->GetBoolField(TEXT("enable_thinking")));
				TestFalse(TEXT("an Auto budget sends no max_tokens"), Body->HasField(TEXT("max_tokens")));
			}
		});

		It(TEXT("DeepSeek: json_object and the thinking fragment, through the DeepSeek provider"), [this]()
		{
			FN2CLLMConfig Config;
			Config.Provider = EN2CLLMProvider::DeepSeek;
			Config.Model = TEXT("deepseek-flash");
			Config.ReasoningEffort = TEXT("none");
			UN2COpenAICompatibleService* Service = nullptr;
			const TSharedPtr<FJsonObject> Off = ServiceBody(Config, Service);
			if (!TestTrue(TEXT("body"), Off.IsValid())) { return; }
			TestEqual(TEXT("the DeepSeek preset"), Service->GetProfile().Id, FName(TEXT("deepseek")));
			const TSharedPtr<FJsonObject>* Format = nullptr;
			TestTrue(TEXT("json_object without thinking"), Off->TryGetObjectField(TEXT("response_format"), Format)
				&& (*Format)->GetStringField(TEXT("type")) == TEXT("json_object"));
			const TSharedPtr<FJsonObject>* Thinking = nullptr;
			TestTrue(TEXT("thinking disabled"), Off->TryGetObjectField(TEXT("thinking"), Thinking)
				&& (*Thinking)->GetStringField(TEXT("type")) == TEXT("disabled"));

			Config.ReasoningEffort = TEXT("high");
			const TSharedPtr<FJsonObject> On = ServiceBody(Config, Service);
			if (!TestTrue(TEXT("body"), On.IsValid())) { return; }
			TestTrue(TEXT("thinking enabled"), On->TryGetObjectField(TEXT("thinking"), Thinking)
				&& (*Thinking)->GetStringField(TEXT("type")) == TEXT("enabled"));
			TestEqual(TEXT("the level in reasoning_effort"), On->GetStringField(TEXT("reasoning_effort")), FString(TEXT("high")));
			TestFalse(TEXT("no response_format while thinking"), On->HasField(TEXT("response_format")));
		});

		It(TEXT("LM Studio: strict is sent as a JSON boolean, stream false"), [this]()
		{
			FN2CHttpRequestSpec Spec;
			TSharedPtr<FJsonObject> Body;
			if (!Encode(MakeRequest(TEXT("qwen3-32b")), Preset(TEXT("lmstudio")), Spec, Body)) { return; }
			const TSharedPtr<FJsonValue> Strict = StrictValue(Body);
			TestTrue(TEXT("strict is a boolean, not the string \"true\""), Strict.IsValid() && Strict->Type == EJson::Boolean && Strict->AsBool());
			TestTrue(TEXT("stream false"), Body->HasTypedField<EJson::Boolean>(TEXT("stream")) && !Body->GetBoolField(TEXT("stream")));
		});

		It(TEXT("OpenRouter: its attribution headers go with every request"), [this]()
		{
			FN2CHttpRequestSpec Spec;
			TSharedPtr<FJsonObject> Body;
			const FN2CProviderProfile Profile = Preset(TEXT("openrouter"));
			if (!Encode(MakeRequest(TEXT("qwen/qwen3-coder")), Profile, Spec, Body)) { return; }
			TestEqual(TEXT("endpoint"), Spec.Url, FString(TEXT("https://openrouter.ai/api/v1/chat/completions")));
			TestEqual(TEXT("HTTP-Referer"), Header(Spec, TEXT("HTTP-Referer")), FString(TEXT("https://github.com/protospatial/NodeToCode")));
			TestEqual(TEXT("X-Title"), Header(Spec, TEXT("X-Title")), FString(TEXT("NodeToCode")));
			TestEqual(TEXT("key as Bearer"), FN2CCredential::AuthStyleFromName(Profile.AuthStyle), EN2CAuthStyle::Bearer);
		});

		It(TEXT("Azure OpenAI: the key in api-key, the budget in max_completion_tokens"), [this]()
		{
			FN2CHttpRequestSpec Spec;
			TSharedPtr<FJsonObject> Body;
			const FN2CProviderProfile Profile = Preset(TEXT("azure-openai"));
			if (!Encode(MakeRequest(TEXT("my-gpt-deployment"), 8000), Profile, Spec, Body)) { return; }
			TestEqual(TEXT("max_completion_tokens"), static_cast<int32>(Body->GetNumberField(TEXT("max_completion_tokens"))), 8000);
			TestFalse(TEXT("no max_tokens"), Body->HasField(TEXT("max_tokens")));
			FN2CCredential Credential;
			Credential.Value = TEXT("n2c-placeholder-key-0218");
			Credential.AuthStyle = FN2CCredential::AuthStyleFromName(Profile.AuthStyle);
			FString Name, Value;
			TestTrue(TEXT("a credential header"), Credential.GetHeader(Name, Value));
			TestEqual(TEXT("named api-key"), Name, FString(TEXT("api-key")));
			TestEqual(TEXT("holding the key as is"), Value, FString(TEXT("n2c-placeholder-key-0218")));
			TestTrue(TEXT("api-key counts as a credential header (redacted, never an extra header)"), FN2CCredential::IsCredentialHeader(TEXT("API-Key")));
		});

		It(TEXT("every preset row encodes a request to its own endpoint"), [this]()
		{
			const TArray<const FN2CProviderProfile*> Presets = FN2CModelCatalog::Get().OpenAICompatiblePresets();
			TestTrue(FString::Printf(TEXT("the catalog lists the presets (%d)"), Presets.Num()), Presets.Num() >= 15);
			for (const FN2CProviderProfile* Profile : Presets)
			{
				FN2CHttpRequestSpec Spec;
				TSharedPtr<FJsonObject> Body;
				if (Encode(MakeRequest(TEXT("some-model")), *Profile, Spec, Body))
				{
					TestTrue(FString::Printf(TEXT("%s: an http(s) endpoint ending in its path"), *Profile->Id.ToString()),
						Spec.Url.StartsWith(TEXT("http")) && Spec.Url.EndsWith(Profile->Path));
					TestEqual(FString::Printf(TEXT("%s: the request's model"), *Profile->Id.ToString()), Body->GetStringField(TEXT("model")), FString(TEXT("some-model")));
				}
			}
		});
	});

	Describe(TEXT("profiles"), [this]()
	{
		It(TEXT("a custom profile at http://localhost:8000/v1 sends each request's own model"), [this]()
		{
			FScopedUserSettings Scoped;
			FN2CCustomProviderProfile Custom;
			Custom.Id = TEXT("n2c-test-box");
			Custom.BaseUrl = TEXT("http://localhost:8000/v1");
			Custom.AuthStyle = TEXT("none");
			GetMutableDefault<UN2CUserSettings>()->CustomProviderProfiles = { Custom };
			GetMutableDefault<UN2CUserSettings>()->ProviderProfile = TEXT("n2c-test-box");

			FN2CLLMConfig First;
			First.Provider = EN2CLLMProvider::OpenAICompatible;
			First.Model = TEXT("model-a");
			FN2CLLMConfig Second = First;
			Second.Model = TEXT("model-b");

			UN2COpenAICompatibleService* A = nullptr;
			UN2COpenAICompatibleService* B = nullptr;
			const TSharedPtr<FJsonObject> BodyA = ServiceBody(First, A);
			const TSharedPtr<FJsonObject> BodyB = ServiceBody(Second, B);
			if (!TestTrue(TEXT("bodies"), BodyA.IsValid() && BodyB.IsValid())) { return; }
			TestEqual(TEXT("the custom profile"), A->GetProfile().Id, FName(TEXT("n2c-test-box")));
			FString Endpoint;
			bool bSystem = false;
			A->GetConfiguration(Endpoint, bSystem);
			TestEqual(TEXT("its endpoint"), Endpoint, FString(TEXT("http://localhost:8000/v1/chat/completions")));
			TestEqual(TEXT("the first request's model"), BodyA->GetStringField(TEXT("model")), FString(TEXT("model-a")));
			TestEqual(TEXT("the second request's model"), BodyB->GetStringField(TEXT("model")), FString(TEXT("model-b")));
			// The first service is untouched by the second
			const TSharedPtr<FJsonObject> Again = N2CTest::ParseJsonObject(A->BuildRequestPayloadForTesting(TEXT("x"), TEXT("y")));
			TestEqual(TEXT("still the first request's model"), Again.IsValid() ? Again->GetStringField(TEXT("model")) : FString(), FString(TEXT("model-a")));

			// The profile named in the request wins over the user's selection
			FN2CLLMConfig Named = First;
			Named.ProviderProfile = TEXT("vllm");
			UN2COpenAICompatibleService* C = nullptr;
			ServiceBody(Named, C);
			TestEqual(TEXT("the request's profile"), C ? C->GetProfile().Id : NAME_None, FName(TEXT("vllm")));
		});

		It(TEXT("Provider=DeepSeek in the settings resolves to the DeepSeek preset, LM Studio to its preset with the user's endpoint"), [this]()
		{
			FScopedUserSettings Scoped;
			const FString SavedEndpoint = GetDefault<UN2CUserSettings>()->LMStudioEndpoint;
			ON_SCOPE_EXIT { GetMutableDefault<UN2CUserSettings>()->LMStudioEndpoint = SavedEndpoint; };

			GetMutableDefault<UN2CUserSettings>()->Provider = EN2CLLMProvider::DeepSeek;
			const FN2CLLMConfig Config = UN2CLLMModule::MakeConfigFromSettings();
			TestEqual(TEXT("the settings' provider"), Config.Provider, EN2CLLMProvider::DeepSeek);
			if (!UN2CLLMProviderRegistry::Get()->IsProviderRegistered(EN2CLLMProvider::DeepSeek))
			{
				UN2CLLMModule::Get()->Initialize(); // registers the providers
			}
			const TScriptInterface<IN2CLLMService> Created = UN2CLLMProviderRegistry::Get()->CreateProvider(Config.Provider, GetTransientPackage());
			UN2COpenAICompatibleService* Service = Cast<UN2COpenAICompatibleService>(Created.GetObject());
			if (!TestNotNull(TEXT("DeepSeek is served by the OpenAI-compatible service"), Service)) { return; }
			TestTrue(TEXT("initializes"), Service->Initialize(Config));
			TestEqual(TEXT("with the DeepSeek preset"), Service->GetProfile().Id, FName(TEXT("deepseek")));
			TestEqual(TEXT("json_object"), Service->GetProfile().StructuredOutput, EN2CStructuredOutputMode::JsonObject);

			GetMutableDefault<UN2CUserSettings>()->LMStudioEndpoint = TEXT("http://gpu-box:1234/");
			FN2CProviderProfile Studio;
			TestTrue(TEXT("LM Studio resolves"), UN2COpenAICompatibleService::ResolveProfile(EN2CLLMProvider::LMStudio, FString(), Studio));
			TestEqual(TEXT("to its preset"), Studio.Id, FName(TEXT("lmstudio")));
			TestEqual(TEXT("at the user's server"), Studio.GetEndpoint(), FString(TEXT("http://gpu-box:1234/v1/chat/completions")));
			GetMutableDefault<UN2CUserSettings>()->LMStudioEndpoint = TEXT("http://gpu-box:1234/v1/chat/completions");
			UN2COpenAICompatibleService::ResolveProfile(EN2CLLMProvider::LMStudio, FString(), Studio);
			TestEqual(TEXT("a full endpoint is not doubled"), Studio.GetEndpoint(), FString(TEXT("http://gpu-box:1234/v1/chat/completions")));

			FN2CProviderProfile Unknown;
			FString Error;
			TestFalse(TEXT("an unknown profile does not resolve"), UN2COpenAICompatibleService::ResolveProfile(EN2CLLMProvider::OpenAICompatible,
				TEXT("n2c-no-such-profile"), Unknown, &Error));
			TestTrue(TEXT("and says where to pick one"), Error.Contains(TEXT("Provider Profile")));
		});
	});

	Describe(TEXT("decoding"), [this]()
	{
		It(TEXT("reads the answer, usage and reasoning, and reports a cut-off as Truncated with the hint"), [this]()
		{
			const FN2CProviderProfile Profile = Preset(TEXT("vllm"));
			const FString Answer = TEXT("{\"choices\":[{\"finish_reason\":\"stop\",\"message\":{\"role\":\"assistant\",")
				TEXT("\"reasoning_content\":\"thinking...\",\"content\":\"{\\\"graphs\\\":[]}\"}}],")
				TEXT("\"usage\":{\"prompt_tokens\":120,\"completion_tokens\":30}}");
			const FN2CLLMResult Result = FN2COpenAIChatDialect::Get().DecodeResponse(Answer, Profile, TEXT("the hint"));
			TestFalse(TEXT("no error"), Result.IsError());
			TestEqual(TEXT("the answer"), Result.Content, FString(TEXT("{\"graphs\":[]}")));
			TestEqual(TEXT("reasoning kept apart"), Result.Reasoning, FString(TEXT("thinking...")));
			TestEqual(TEXT("input tokens"), Result.Usage.InputTokens, 120);
			TestEqual(TEXT("output tokens"), Result.Usage.OutputTokens, 30);

			AddExpectedError(TEXT("hint"), EAutomationExpectedErrorFlags::Contains, 0);
			const FString Cut = TEXT("{\"choices\":[{\"finish_reason\":\"length\",\"message\":{\"role\":\"assistant\",\"content\":\"{\\\"graphs\\\":[\"}}]}");
			const FN2CLLMResult Truncated = FN2COpenAIChatDialect::Get().DecodeResponse(Cut, Profile, TEXT("the hint"));
			TestEqual(TEXT("Truncated"), Truncated.Error.Kind, EN2CTranslationErrorKind::Truncated);
			TestTrue(TEXT("names where to raise the budget"), Truncated.Error.Message.Contains(TEXT("the hint")));

			AddExpectedError(TEXT("429"), EAutomationExpectedErrorFlags::Contains, 0);
			const FN2CLLMResult Limited = FN2COpenAIChatDialect::Get().DecodeResponse(
				TEXT("{\"http_status\":429,\"error\":{\"message\":\"slow down\",\"type\":\"rate_limit_error\"}}"), Profile, TEXT("x"));
			TestEqual(TEXT("a host error keeps its kind"), Limited.Error.Kind, EN2CTranslationErrorKind::RateLimited);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
