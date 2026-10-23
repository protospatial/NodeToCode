// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CSettings.h"
#include "Core/Services/N2CTokenEstimationService.h"
#include "LLM/N2CBaseLLMService.h"
#include "LLM/N2CLLMModelRegistry.h"
#include "LLM/N2CLLMPayloadBuilder.h"
#include "LLM/Providers/N2CAnthropicService.h"
#include "LLM/Providers/N2CDeepSeekService.h"
#include "LLM/Providers/N2CGeminiService.h"
#include "LLM/Providers/N2CLMStudioService.h"
#include "LLM/Providers/N2COllamaService.h"
#include "LLM/Providers/N2COpenAIService.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

/**
 * Builds each provider's request body through its service (no request is sent, no key is used) and checks
 * the fields: no sampling parameters where the registry says the model rejects them, the right max-token
 * field, a Gemini schema without additionalProperties, strict json_schema as a JSON boolean for OpenAI and
 * LM Studio, a Gemini API key carried in the x-goog-api-key header rather than the URL, and DeepSeek thinking
 * and JSON mode decided from the request's own model and thinking control rather than from Project Settings.
 */
BEGIN_DEFINE_SPEC(FN2CPayloadBuilderSpec, "NodeToCode.LLM.Payload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	/** Builds the payload FormatRequestPayload would send for this provider and model */
	template <typename TService>
	FString BuildPayload(EN2CLLMProvider Provider, const FString& Model)
	{
		TService* Service = NewObject<TService>(GetTransientPackage());
		FN2CLLMConfig Config;
		Config.Provider = Provider;
		Config.Model = Model;
		Service->Initialize(Config);
		return Service->BuildRequestPayloadForTesting(TEXT("Translate this graph."), TEXT("You are a translator."));
	}

	/**
	 * DeepSeek body for a request resolved the way the editor resolves one: Selected is the Project Settings
	 * selection (set on the settings default object for the duration, so a service that read Settings would see
	 * it) and ModelIdOverride the MCP model argument (empty for none).
	 */
	TSharedPtr<FJsonObject> BuildDeepSeek(EN2CDeepSeekModel Selected, const FString& ModelIdOverride = FString())
	{
		UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
		const EN2CDeepSeekModel Saved = Settings->DeepSeekModel;
		Settings->DeepSeekModel = Selected;

		FN2CLLMConfig Config;
		Config.Provider = EN2CLLMProvider::DeepSeek;
		Settings->ResolveRequestModel(EN2CLLMProvider::DeepSeek, ModelIdOverride, Config);
		UN2CDeepSeekService* Service = NewObject<UN2CDeepSeekService>(GetTransientPackage());
		Service->Initialize(Config);
		const FString Payload = Service->BuildRequestPayloadForTesting(TEXT("Translate this graph to JSON."), TEXT("You are a translator."));

		Settings->DeepSeekModel = Saved;
		return Parse(Payload);
	}

	/** thinking.type is Type; reasoning_effort is Effort, or absent when Effort is null */
	void TestDeepSeekThinking(const FString& What, const TSharedPtr<FJsonObject>& Root, const TCHAR* Type, const TCHAR* Effort)
	{
		const TSharedPtr<FJsonObject> Thinking = Child(Root, TEXT("thinking"));
		if (TestTrue(FString::Printf(TEXT("%s has thinking"), *What), Thinking.IsValid()))
		{
			TestEqual(FString::Printf(TEXT("%s thinking.type"), *What), Thinking->GetStringField(TEXT("type")), FString(Type));
		}
		if (Effort)
		{
			FString Sent;
			TestTrue(FString::Printf(TEXT("%s has reasoning_effort"), *What), Root->TryGetStringField(TEXT("reasoning_effort"), Sent));
			TestEqual(FString::Printf(TEXT("%s reasoning_effort"), *What), Sent, FString(Effort));
		}
		else
		{
			TestFalse(FString::Printf(TEXT("%s has no reasoning_effort"), *What), Root->HasField(TEXT("reasoning_effort")));
		}
	}

	/** response_format is json_object when bJson, else absent */
	void TestDeepSeekJson(const FString& What, const TSharedPtr<FJsonObject>& Root, bool bJson)
	{
		const TSharedPtr<FJsonObject> ResponseFormat = Child(Root, TEXT("response_format"));
		if (!bJson)
		{
			TestFalse(FString::Printf(TEXT("%s has no response_format"), *What), ResponseFormat.IsValid());
		}
		else if (TestTrue(FString::Printf(TEXT("%s has response_format"), *What), ResponseFormat.IsValid()))
		{
			TestEqual(FString::Printf(TEXT("%s response_format type"), *What), ResponseFormat->GetStringField(TEXT("type")), FString(TEXT("json_object")));
		}
	}

	TSharedPtr<FJsonObject> Parse(const FString& Payload)
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Payload);
		FJsonSerializer::Deserialize(Reader, Root);
		return Root;
	}

	/**
	 * Ollama body FormatRequestPayload would send with this Ollama config: the config is set on the settings
	 * default object for the duration, since the service reads it from Settings in Initialize
	 */
	TSharedPtr<FJsonObject> BuildOllama(const FN2COllamaConfig& OllamaConfig)
	{
		UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
		const FN2COllamaConfig Saved = Settings->OllamaConfig;
		Settings->OllamaConfig = OllamaConfig;
		const FString Payload = BuildPayload<UN2COllamaService>(EN2CLLMProvider::Ollama, TEXT("qwen3:32b"));
		Settings->OllamaConfig = Saved;
		return Parse(Payload);
	}

	/** Body the payload builder writes for this Ollama config, without the service */
	TSharedPtr<FJsonObject> BuildOllamaDirect(const FN2COllamaConfig& OllamaConfig)
	{
		UN2CLLMPayloadBuilder* Builder = NewObject<UN2CLLMPayloadBuilder>(GetTransientPackage());
		Builder->Initialize(TEXT("qwen3:32b"));
		Builder->ConfigureForOllama(OllamaConfig);
		return Parse(Builder->Build());
	}

	/** A settings object loaded from an ini holding these lines, as a reload at startup applies saved config */
	UN2CSettings* LoadSettingsFromIni(const FString& Lines)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation")
			/ FString::Printf(TEXT("N2CPayloadSpec_%s.ini"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
		const FString Ini = FString::Printf(TEXT("[%s]\n%s\n"), *UN2CSettings::StaticClass()->GetPathName(), *Lines);
		if (!TestTrue(TEXT("test ini written"), FFileHelper::SaveStringToFile(Ini, *Path))) { return nullptr; }

		GConfig->LoadFile(Path);
		UN2CSettings* Settings = NewObject<UN2CSettings>(GetTransientPackage());
		Settings->LoadConfig(nullptr, *Path, UE::LCPF_ReloadingConfigData);
		GConfig->UnloadFile(Path);
		IFileManager::Get().Delete(*Path);
		return Settings;
	}

	/** OllamaConfig as Project Settings saved it before 2.0: the whole struct, every field at its old default */
	static FString LegacyOllamaLine(const TCHAR* TopP = TEXT("0.500000"), const TCHAR* NumCtx = TEXT("8192"))
	{
		return FString::Printf(TEXT("OllamaConfig=(OllamaEndpoint=\"http://localhost:11434\",bUseSystemPrompts=False,PrependedModelCommand=\"\",KeepAlive=3600,Temperature=0.000000,NumPredict=8192,TopP=%s,TopK=40,MinP=0.050000,RepeatPenalty=1.100000,NumCtx=%s,Mirostat=0,MirostatEta=0.100000,MirostatTau=5.000000,Seed=0)"), TopP, NumCtx);
	}

	/** options has none of the sampler fields Ollama used to get on every request */
	void TestNoOllamaSamplers(const FString& What, const TSharedPtr<FJsonObject>& Root)
	{
		const TSharedPtr<FJsonObject> Options = Child(Root, TEXT("options"));
		if (!Options.IsValid())
		{
			return;
		}
		for (const TCHAR* Field : { TEXT("temperature"), TEXT("top_p"), TEXT("top_k"), TEXT("min_p"), TEXT("repeat_penalty"),
			TEXT("mirostat"), TEXT("mirostat_eta"), TEXT("mirostat_tau"), TEXT("seed") })
		{
			TestFalse(FString::Printf(TEXT("%s has no options.%s"), *What, Field), Options->HasField(Field));
		}
	}

	/** The named sub-object, or null; never the engine's shared empty fallback object */
	static TSharedPtr<FJsonObject> Child(const TSharedPtr<FJsonObject>& Object, const FString& Name)
	{
		const TSharedPtr<FJsonObject>* Found = nullptr;
		if (Object.IsValid() && Object->TryGetObjectField(Name, Found) && Found)
		{
			return *Found;
		}
		return nullptr;
	}

	void TestNoSampling(const FString& What, const TSharedPtr<FJsonObject>& Object)
	{
		if (!Object.IsValid())
		{
			return;
		}
		for (const TCHAR* Field : { TEXT("temperature"), TEXT("top_p"), TEXT("top_k"), TEXT("topP"), TEXT("topK") })
		{
			TestFalse(FString::Printf(TEXT("%s has no %s"), *What, Field), Object->HasField(Field));
		}
	}

	bool RegistryAcceptsSampling(const FString& Model)
	{
		const FN2CModelMetadata* Meta = FN2CLLMModelRegistry::Get().GetModelMetadataById(Model);
		return Meta && Meta->bAcceptsSamplingParams;
	}

	/** response_format is json_schema with a schema and "strict" as the JSON boolean true (not the string "true") */
	void TestStrictJsonSchema(const FString& What, const TSharedPtr<FJsonObject>& Root)
	{
		const TSharedPtr<FJsonObject> ResponseFormat = Child(Root, TEXT("response_format"));
		if (!TestTrue(FString::Printf(TEXT("%s has response_format"), *What), ResponseFormat.IsValid())) { return; }
		TestEqual(FString::Printf(TEXT("%s response_format type"), *What), ResponseFormat->GetStringField(TEXT("type")), FString(TEXT("json_schema")));
		const TSharedPtr<FJsonObject> Wrapper = Child(ResponseFormat, TEXT("json_schema"));
		if (!TestTrue(FString::Printf(TEXT("%s has json_schema"), *What), Wrapper.IsValid())) { return; }
		TestTrue(FString::Printf(TEXT("%s json_schema has a schema"), *What), Child(Wrapper, TEXT("schema")).IsValid());
		const TSharedPtr<FJsonValue> Strict = Wrapper->TryGetField(TEXT("strict"));
		if (!TestTrue(FString::Printf(TEXT("%s json_schema has strict"), *What), Strict.IsValid())) { return; }
		TestEqual(FString::Printf(TEXT("%s strict is a JSON boolean"), *What), static_cast<int32>(Strict->Type), static_cast<int32>(EJson::Boolean));
		bool bStrict = false;
		TestTrue(FString::Printf(TEXT("%s strict is true"), *What), Strict->TryGetBool(bStrict) && bStrict);
	}

	/** Gemini 3+ body: no sampling fields, maxOutputTokens and a responseSchema without additionalProperties */
	void TestGemini3Payload(const FString& Payload)
	{
		const TSharedPtr<FJsonObject> Root = Parse(Payload);
		if (!TestTrue(TEXT("payload parses"), Root.IsValid())) { return; }
TestNoSampling(TEXT("Gemini root"), Root);
		const TSharedPtr<FJsonObject> GenConfig = Child(Root, TEXT("generationConfig"));
		if (!TestTrue(TEXT("Gemini has generationConfig"), GenConfig.IsValid())) { return; }
		TestNoSampling(TEXT("Gemini generationConfig"), GenConfig);
		TestTrue(TEXT("Gemini has maxOutputTokens"), GenConfig->HasField(TEXT("maxOutputTokens")));
		TestTrue(TEXT("Gemini has responseSchema"), Child(GenConfig, TEXT("responseSchema")).IsValid());
		TestFalse(TEXT("Gemini payload has no additionalProperties"), Payload.Contains(TEXT("additionalProperties")));
		TestFalse(TEXT("Gemini has no root max_tokens"), Root->HasField(TEXT("max_tokens")));
	}

END_DEFINE_SPEC(FN2CPayloadBuilderSpec)

void FN2CPayloadBuilderSpec::Define()
{
	Describe(TEXT("Anthropic"), [this]()
	{
		It(TEXT("sends no sampling fields for the current models or an unregistered ID and keeps max_tokens"), [this]()
		{
			// Opus 4.7 and later reject sampling parameters; IDs the registry does not know get none either
			for (const TCHAR* Model : { TEXT("claude-sonnet-5-5"), TEXT("claude-opus-5-5"), TEXT("claude-haiku-5-5"), TEXT("claude-future-not-registered") })
			{
				const TSharedPtr<FJsonObject> Root = Parse(BuildPayload<UN2CAnthropicService>(EN2CLLMProvider::Anthropic, Model));
				if (!TestTrue(FString::Printf(TEXT("%s payload parses"), Model), Root.IsValid())) { continue; }
				TestNoSampling(FString::Printf(TEXT("Anthropic %s root"), Model), Root);
				double MaxTokens = 0.0;
				TestTrue(FString::Printf(TEXT("%s has max_tokens"), Model), Root->TryGetNumberField(TEXT("max_tokens"), MaxTokens));
				TestEqual(FString::Printf(TEXT("%s max_tokens"), Model), MaxTokens, 16000.0);
			}
		});

		It(TEXT("sends temperature for a registered model only when its flag allows it"), [this]()
		{
			const FString Model = TEXT("claude-sonnet-4-5-20250929");
			const TSharedPtr<FJsonObject> Root = Parse(BuildPayload<UN2CAnthropicService>(EN2CLLMProvider::Anthropic, Model));
			if (!TestTrue(TEXT("payload parses"), Root.IsValid())) { return; }
			TestEqual(TEXT("temperature present iff the registry flag is set"), Root->HasField(TEXT("temperature")), RegistryAcceptsSampling(Model));
			TestFalse(TEXT("never top_p alongside temperature"), Root->HasField(TEXT("top_p")));
			TestTrue(TEXT("Anthropic has max_tokens"), Root->HasField(TEXT("max_tokens")));
		});
	});

	Describe(TEXT("OpenAI"), [this]()
	{
		It(TEXT("treats an unregistered ID as a reasoning model"), [this]()
		{
			const TSharedPtr<FJsonObject> Root = Parse(BuildPayload<UN2COpenAIService>(EN2CLLMProvider::OpenAI, TEXT("gpt-7-not-registered")));
			if (!TestTrue(TEXT("payload parses"), Root.IsValid())) { return; }
			TestNoSampling(TEXT("OpenAI unregistered root"), Root);
			TestTrue(TEXT("unregistered ID uses max_completion_tokens"), Root->HasField(TEXT("max_completion_tokens")));
			TestFalse(TEXT("unregistered ID sends no max_tokens"), Root->HasField(TEXT("max_tokens")));
		});

		It(TEXT("sends no temperature for a registered reasoning model"), [this]()
		{
			for (const TCHAR* Model : { TEXT("gpt-5.2"), TEXT("gpt-6.1-sol"), TEXT("gpt-6-astra"), TEXT("gpt-6-luna") })
			{
				const TSharedPtr<FJsonObject> Root = Parse(BuildPayload<UN2COpenAIService>(EN2CLLMProvider::OpenAI, Model));
				if (!TestTrue(FString::Printf(TEXT("%s payload parses"), Model), Root.IsValid())) { continue; }
				TestFalse(FString::Printf(TEXT("registry flag for %s"), Model), RegistryAcceptsSampling(Model));
				TestNoSampling(FString::Printf(TEXT("OpenAI %s root"), Model), Root);
				TestTrue(FString::Printf(TEXT("%s uses max_completion_tokens"), Model), Root->HasField(TEXT("max_completion_tokens")));
				TestFalse(FString::Printf(TEXT("%s sends no max_tokens"), Model), Root->HasField(TEXT("max_tokens")));
			}
		});

		It(TEXT("keeps temperature, max_tokens and the strict schema for a chat model"), [this]()
		{
			const FString Payload = BuildPayload<UN2COpenAIService>(EN2CLLMProvider::OpenAI, TEXT("gpt-4.1"));
			const TSharedPtr<FJsonObject> Root = Parse(Payload);
			if (!TestTrue(TEXT("payload parses"), Root.IsValid())) { return; }
			TestTrue(TEXT("gpt-4.1 accepts sampling"), RegistryAcceptsSampling(TEXT("gpt-4.1")));
			TestTrue(TEXT("gpt-4.1 has temperature"), Root->HasField(TEXT("temperature")));
			TestTrue(TEXT("gpt-4.1 has max_tokens"), Root->HasField(TEXT("max_tokens")));
			TestTrue(TEXT("OpenAI still receives additionalProperties"), Payload.Contains(TEXT("additionalProperties")));
			TestStrictJsonSchema(TEXT("OpenAI gpt-4.1"), Root);
		});

		It(TEXT("sends strict json_schema to every model the registry flags for structured output"), [this]()
		{
			// o3 and o1 were downgraded to json_object by name before; the registry flag now decides
			for (const TCHAR* Model : { TEXT("gpt-5.2"), TEXT("o4-mini"), TEXT("o3"), TEXT("o3-mini"), TEXT("o1"), TEXT("gpt-6.1-sol"), TEXT("gpt-6-astra"), TEXT("gpt-6-luna"), TEXT("gpt-7-not-registered") })
			{
				const TSharedPtr<FJsonObject> Root = Parse(BuildPayload<UN2COpenAIService>(EN2CLLMProvider::OpenAI, Model));
				if (!TestTrue(FString::Printf(TEXT("%s payload parses"), Model), Root.IsValid())) { continue; }
				TestStrictJsonSchema(FString::Printf(TEXT("OpenAI %s"), Model), Root);
			}
		});

		It(TEXT("sends no response_format to models the registry flags without structured output"), [this]()
		{
			for (const TCHAR* Model : { TEXT("o1-preview-2024-09-12"), TEXT("o1-mini-2024-09-12") })
			{
				const FN2CModelMetadata* Meta = FN2CLLMModelRegistry::Get().GetModelMetadataById(Model);
				TestTrue(FString::Printf(TEXT("%s is registered without structured output"), Model), Meta && !Meta->bSupportsStructuredOutput);
				const TSharedPtr<FJsonObject> Root = Parse(BuildPayload<UN2COpenAIService>(EN2CLLMProvider::OpenAI, Model));
				if (!TestTrue(FString::Printf(TEXT("%s payload parses"), Model), Root.IsValid())) { continue; }
				TestFalse(FString::Printf(TEXT("%s has no response_format"), Model), Root->HasField(TEXT("response_format")));
			}
		});
	});

	Describe(TEXT("Gemini"), [this]()
	{
		It(TEXT("sends no sampling fields and no additionalProperties for Gemini 3 and later"), [this]()
		{
			for (const TCHAR* Model : { TEXT("gemini-3-flash-preview"), TEXT("gemini-3.8-flash"), TEXT("gemini-4-argon"), TEXT("gemini-3.5-flash-lite") })
			{
				AddInfo(FString::Printf(TEXT("Gemini model %s"), Model));
				TestGemini3Payload(BuildPayload<UN2CGeminiService>(EN2CLLMProvider::Gemini, Model));
			}
		});

		It(TEXT("sends no sampling fields for an unregistered Gemini ID"), [this]()
		{
			const TSharedPtr<FJsonObject> Root = Parse(BuildPayload<UN2CGeminiService>(EN2CLLMProvider::Gemini, TEXT("gemini-9-not-registered")));
			if (!TestTrue(TEXT("payload parses"), Root.IsValid())) { return; }
TestNoSampling(TEXT("Gemini unregistered root"), Root);
			TestNoSampling(TEXT("Gemini unregistered generationConfig"), Child(Root, TEXT("generationConfig")));
		});

		It(TEXT("keeps the API key out of the endpoint URL and sends it in x-goog-api-key"), [this]()
		{
			const FString FakeKey = TEXT("n2c-test-key-not-real");
			UN2CGeminiService* Service = NewObject<UN2CGeminiService>(GetTransientPackage());
			FN2CLLMConfig Config;
			Config.Provider = EN2CLLMProvider::Gemini;
			Config.Model = TEXT("gemini-3-flash-preview");
			Config.ApiKey = FakeKey;
			Service->Initialize(Config);

			FString Endpoint, AuthToken;
			bool bSupportsSystemPrompts = false;
			Service->GetConfiguration(Endpoint, AuthToken, bSupportsSystemPrompts);
			TMap<FString, FString> Headers;
			Service->GetProviderHeaders(Headers);

			TestEqual(TEXT("Gemini endpoint is <endpoint><model>:generateContent"), Endpoint,
				FString(TEXT("https://generativelanguage.googleapis.com/v1beta/models/gemini-3-flash-preview:generateContent")));
			TestFalse(TEXT("Gemini endpoint has no query string"), Endpoint.Contains(TEXT("?")));
			TestFalse(TEXT("Gemini endpoint does not contain the key"), Endpoint.Contains(FakeKey));
			TestTrue(TEXT("Gemini sends no Bearer token for an API key"), AuthToken.IsEmpty());
			const FString* HeaderKey = Headers.Find(TEXT("x-goog-api-key"));
			if (TestNotNull(TEXT("Gemini has an x-goog-api-key header"), HeaderKey))
			{
				TestEqual(TEXT("x-goog-api-key carries the key"), *HeaderKey, FakeKey);
			}
			TestFalse(TEXT("Gemini API-key route sends no Authorization header"), Headers.Contains(TEXT("Authorization")));
		});
	});

	Describe(TEXT("DeepSeek"), [this]()
	{
		It(TEXT("DeepSeek_V3 sends deepseek-flash with thinking disabled and json_object"), [this]()
		{
			const TSharedPtr<FJsonObject> Root = BuildDeepSeek(EN2CDeepSeekModel::DeepSeek_V3);
			if (!TestTrue(TEXT("payload parses"), Root.IsValid())) { return; }
			TestEqual(TEXT("V3 model"), Root->GetStringField(TEXT("model")), FString(TEXT("deepseek-flash")));
			TestDeepSeekThinking(TEXT("V3"), Root, TEXT("disabled"), nullptr);
			TestDeepSeekJson(TEXT("V3"), Root, true);
			TestTrue(TEXT("V3 has max_tokens"), Root->HasField(TEXT("max_tokens")));
		});

		It(TEXT("DeepSeek_R1 sends deepseek-flash with thinking enabled at high effort and no temperature"), [this]()
		{
			const TSharedPtr<FJsonObject> Root = BuildDeepSeek(EN2CDeepSeekModel::DeepSeek_R1);
			if (!TestTrue(TEXT("payload parses"), Root.IsValid())) { return; }
			TestEqual(TEXT("R1 model"), Root->GetStringField(TEXT("model")), FString(TEXT("deepseek-flash")));
			TestDeepSeekThinking(TEXT("R1"), Root, TEXT("enabled"), TEXT("high"));
			TestNoSampling(TEXT("R1 root"), Root);
			TestDeepSeekJson(TEXT("R1"), Root, false);
			int32 MaxTokens = 0;
			TestTrue(TEXT("R1 has max_tokens"), Root->TryGetNumberField(TEXT("max_tokens"), MaxTokens));
			TestEqual(TEXT("R1 max_tokens leaves room for reasoning"), MaxTokens, 65536);
		});

		It(TEXT("DeepSeek_V4_Pro sends deepseek-v4-pro with thinking enabled at high effort and no temperature"), [this]()
		{
			const TSharedPtr<FJsonObject> Root = BuildDeepSeek(EN2CDeepSeekModel::DeepSeek_V4_Pro);
			if (!TestTrue(TEXT("payload parses"), Root.IsValid())) { return; }
			TestEqual(TEXT("V4 Pro model"), Root->GetStringField(TEXT("model")), FString(TEXT("deepseek-v4-pro")));
			TestDeepSeekThinking(TEXT("V4 Pro"), Root, TEXT("enabled"), TEXT("high"));
			TestNoSampling(TEXT("V4 Pro root"), Root);
			TestDeepSeekJson(TEXT("V4 Pro"), Root, false);
		});

		It(TEXT("an override ID that differs from Settings gets its own model's thinking and JSON mode"), [this]()
		{
			// Settings select the non-thinking JSON mode; the MCP override names another model
			const TSharedPtr<FJsonObject> Root = BuildDeepSeek(EN2CDeepSeekModel::DeepSeek_V3, TEXT("deepseek-v4-pro"));
			if (!TestTrue(TEXT("payload parses"), Root.IsValid())) { return; }
			TestEqual(TEXT("override model"), Root->GetStringField(TEXT("model")), FString(TEXT("deepseek-v4-pro")));
			TestDeepSeekThinking(TEXT("override"), Root, TEXT("enabled"), TEXT("high"));
			TestNoSampling(TEXT("override root"), Root);
			TestDeepSeekJson(TEXT("override"), Root, false);

			// Settings select thinking; the override names deepseek-flash, which is the selected model
			const TSharedPtr<FJsonObject> Same = BuildDeepSeek(EN2CDeepSeekModel::DeepSeek_V3, TEXT("deepseek-flash"));
			if (!TestTrue(TEXT("same-model payload parses"), Same.IsValid())) { return; }
			TestDeepSeekThinking(TEXT("override naming the selection"), Same, TEXT("disabled"), nullptr);
			TestDeepSeekJson(TEXT("override naming the selection"), Same, true);
		});

		It(TEXT("a bare deepseek-flash request gets the API default, thinking at high effort"), [this]()
		{
			const TSharedPtr<FJsonObject> Root = Parse(BuildPayload<UN2CDeepSeekService>(EN2CLLMProvider::DeepSeek, TEXT("deepseek-flash")));
			if (!TestTrue(TEXT("payload parses"), Root.IsValid())) { return; }
			TestDeepSeekThinking(TEXT("bare deepseek-flash"), Root, TEXT("enabled"), TEXT("high"));
			TestNoSampling(TEXT("bare deepseek-flash root"), Root);
		});

		It(TEXT("an unregistered DeepSeek ID gets no thinking, sampling or response_format fields"), [this]()
		{
			const TSharedPtr<FJsonObject> Root = Parse(BuildPayload<UN2CDeepSeekService>(EN2CLLMProvider::DeepSeek, TEXT("deepseek-v9-unknown")));
			if (!TestTrue(TEXT("payload parses"), Root.IsValid())) { return; }
			TestFalse(TEXT("unregistered: no thinking"), Root->HasField(TEXT("thinking")));
			TestFalse(TEXT("unregistered: no reasoning_effort"), Root->HasField(TEXT("reasoning_effort")));
			TestNoSampling(TEXT("unregistered root"), Root);
			TestDeepSeekJson(TEXT("unregistered"), Root, false);
			TestTrue(TEXT("unregistered: has max_tokens"), Root->HasField(TEXT("max_tokens")));
		});
	});

	Describe(TEXT("Ollama"), [this]()
	{
		It(TEXT("has no stray root temperature or max_tokens"), [this]()
		{
			const TSharedPtr<FJsonObject> Root = Parse(BuildPayload<UN2COllamaService>(EN2CLLMProvider::Ollama, TEXT("qwen3:32b")));
			if (!TestTrue(TEXT("payload parses"), Root.IsValid())) { return; }
			TestFalse(TEXT("Ollama has no root temperature"), Root->HasField(TEXT("temperature")));
			TestFalse(TEXT("Ollama has no root max_tokens"), Root->HasField(TEXT("max_tokens")));
			TestFalse(TEXT("Ollama has no root max_completion_tokens"), Root->HasField(TEXT("max_completion_tokens")));
			});

		It(TEXT("sends no num_ctx, num_predict, sampler or think fields with default settings"), [this]()
		{
			const TSharedPtr<FJsonObject> Root = BuildOllama(FN2COllamaConfig());
			if (!TestTrue(TEXT("payload parses"), Root.IsValid())) { return; }
			TestFalse(TEXT("default payload has no options object"), Root->HasField(TEXT("options")));
			TestNoOllamaSamplers(TEXT("default payload"), Root);
			TestFalse(TEXT("default payload has no think"), Root->HasField(TEXT("think")));
			TestTrue(TEXT("default payload keeps the schema in format"), Child(Root, TEXT("format")).IsValid());
			bool bStream = true;
			TestTrue(TEXT("default payload has stream false"), Root->TryGetBoolField(TEXT("stream"), bStream) && !bStream);
			TestTrue(TEXT("default payload has keep_alive"), Root->HasField(TEXT("keep_alive")));

			// bUseSystemPrompts now defaults to true: the system prompt is its own message
			const TArray<TSharedPtr<FJsonValue>>* Messages = nullptr;
			if (TestTrue(TEXT("default payload has a system and a user message"), Root->TryGetArrayField(TEXT("messages"), Messages) && Messages && Messages->Num() == 2))
			{
				TestEqual(TEXT("first message is the system prompt"), (*Messages)[0]->AsObject()->GetStringField(TEXT("role")), FString(TEXT("system")));
			}
		});

		It(TEXT("sends num_ctx and num_predict only when greater than 0"), [this]()
		{
			FN2COllamaConfig Config;
			Config.NumCtx = 32768;
			const TSharedPtr<FJsonObject> Root = BuildOllama(Config);
			const TSharedPtr<FJsonObject> Options = Child(Root, TEXT("options"));
			if (!TestTrue(TEXT("NumCtx 32768 sends options"), Options.IsValid())) { return; }
			TestEqual(TEXT("num_ctx"), Options->GetIntegerField(TEXT("num_ctx")), 32768);
			TestFalse(TEXT("NumPredict 0 sends no num_predict"), Options->HasField(TEXT("num_predict")));
			TestNoOllamaSamplers(TEXT("NumCtx only"), Root);

			Config.NumCtx = 0;
			Config.NumPredict = 4096;
			const TSharedPtr<FJsonObject> Predict = Child(BuildOllama(Config), TEXT("options"));
			if (!TestTrue(TEXT("NumPredict 4096 sends options"), Predict.IsValid())) { return; }
			TestEqual(TEXT("num_predict"), Predict->GetIntegerField(TEXT("num_predict")), 4096);
			TestFalse(TEXT("NumCtx 0 sends no num_ctx"), Predict->HasField(TEXT("num_ctx")));

			// -1 (unlimited) saved by older versions is not sent; the server default is unlimited too
			Config.NumPredict = -1;
			TestFalse(TEXT("NumPredict -1 sends no options"), BuildOllama(Config)->HasField(TEXT("options")));
		});

		It(TEXT("sends each sampler value only when its override is on and sends 0 when set"), [this]()
		{
			FN2COllamaConfig Config;
			Config.bOverride_Temperature = true;
			Config.Temperature = 0.0f;
			Config.bOverride_TopK = true;
			Config.TopK = 20;
			const TSharedPtr<FJsonObject> Options = Child(BuildOllama(Config), TEXT("options"));
			if (!TestTrue(TEXT("overrides send options"), Options.IsValid())) { return; }
			double Temperature = -1.0;
			TestTrue(TEXT("temperature 0.0 is sent"), Options->TryGetNumberField(TEXT("temperature"), Temperature) && Temperature == 0.0);
			TestEqual(TEXT("top_k"), Options->GetIntegerField(TEXT("top_k")), 20);
			for (const TCHAR* Field : { TEXT("top_p"), TEXT("min_p"), TEXT("repeat_penalty"), TEXT("mirostat"), TEXT("seed"), TEXT("num_ctx") })
			{
				TestFalse(FString::Printf(TEXT("no %s without its override"), Field), Options->HasField(Field));
			}
		});

		It(TEXT("maps Think to the think field"), [this]()
		{
			FN2COllamaConfig Config;
			Config.Think = EN2COllamaThink::Off;
			const TSharedPtr<FJsonObject> Off = BuildOllama(Config);
			const TSharedPtr<FJsonValue> OffValue = Off.IsValid() ? Off->TryGetField(TEXT("think")) : nullptr;
			if (TestTrue(TEXT("Think Off sends think"), OffValue.IsValid()))
			{
				TestEqual(TEXT("think is a JSON boolean"), static_cast<int32>(OffValue->Type), static_cast<int32>(EJson::Boolean));
				TestFalse(TEXT("Think Off sends think false"), OffValue->AsBool());
			}

			Config.Think = EN2COllamaThink::On;
			bool bThink = false;
			TestTrue(TEXT("Think On sends think true"), BuildOllama(Config)->TryGetBoolField(TEXT("think"), bThink) && bThink);

			Config.Think = EN2COllamaThink::Level;
			Config.ThinkLevel = TEXT("high");
			FString Level;
			TestTrue(TEXT("Think Level sends the level string"), BuildOllama(Config)->TryGetStringField(TEXT("think"), Level));
			TestEqual(TEXT("think level"), Level, FString(TEXT("high")));

			Config.ThinkLevel = TEXT("  ");
			AddExpectedMessage(TEXT("Think Level is empty"), EAutomationExpectedMessageFlags::Contains, 1);
			TestFalse(TEXT("an empty level sends no think"), BuildOllama(Config)->HasField(TEXT("think")));
		});

		It(TEXT("warns when the estimated prompt is larger than the Context Window"), [this]()
		{
			AddExpectedMessage(TEXT("larger than the Ollama Context Window"), EAutomationExpectedMessageFlags::Contains, 1);
			FN2COllamaConfig Config;
			Config.NumCtx = 1;
			BuildOllama(Config);

			TestFalse(TEXT("no warning when the window is the server default"), FN2CTokenEstimationService::WarnIfPromptExceedsContext(100000, 0, TEXT("Ollama")));
			TestFalse(TEXT("no warning when the prompt fits"), FN2CTokenEstimationService::WarnIfPromptExceedsContext(1000, 32768, TEXT("Ollama")));
		});
	});

	Describe(TEXT("OllamaMigration"), [this]()
	{
		It(TEXT("clears values saved at the old defaults so the server defaults apply"), [this]()
		{
			UN2CSettings* Settings = LoadSettingsFromIni(LegacyOllamaLine());
			if (!TestNotNull(TEXT("settings loaded"), Settings)) { return; }
			const FN2COllamaConfig& Config = Settings->OllamaConfig;
			TestEqual(TEXT("NumCtx 8192 cleared"), Config.NumCtx, 0);
			TestEqual(TEXT("NumPredict 8192 cleared"), Config.NumPredict, 0);
			TestTrue(TEXT("bUseSystemPrompts moved to the new default"), Config.bUseSystemPrompts);
			TestFalse(TEXT("TopP at the old default stays unset"), Config.bOverride_TopP);
			TestFalse(TEXT("Temperature at the old default stays unset"), Config.bOverride_Temperature);
			TestEqual(TEXT("settings version not stamped on load"), Settings->SettingsVersion, 0);
			TestFalse(TEXT("migrated legacy config sends no options"), BuildOllamaDirect(Config)->HasField(TEXT("options")));

			// Running the migration again on its own output changes nothing
			TestEqual(TEXT("migration is idempotent"), Settings->MigrateSavedSettings(), 0);
		});

		It(TEXT("keeps a saved value that differs from the old default as an explicit override"), [this]()
		{
			UN2CSettings* Settings = LoadSettingsFromIni(LegacyOllamaLine(TEXT("0.900000"), TEXT("32768")));
			if (!TestNotNull(TEXT("settings loaded"), Settings)) { return; }
			const FN2COllamaConfig& Config = Settings->OllamaConfig;
			TestTrue(TEXT("TopP 0.9 becomes an override"), Config.bOverride_TopP);
			TestEqual(TEXT("NumCtx 32768 kept"), Config.NumCtx, 32768);
			TestFalse(TEXT("TopK at the old default stays unset"), Config.bOverride_TopK);

			const TSharedPtr<FJsonObject> Options = Child(BuildOllamaDirect(Config), TEXT("options"));
			if (!TestTrue(TEXT("user values still sent"), Options.IsValid())) { return; }
			TestEqual(TEXT("num_ctx kept"), Options->GetIntegerField(TEXT("num_ctx")), 32768);
			double TopP = 0.0;
			TestTrue(TEXT("top_p 0.9 kept"), Options->TryGetNumberField(TEXT("top_p"), TopP) && FMath::IsNearlyEqual(TopP, 0.9, 1e-4));
			TestFalse(TEXT("top_k at the old default not sent"), Options->HasField(TEXT("top_k")));
		});

		It(TEXT("leaves a config saved at the current version alone"), [this]()
		{
			UN2CSettings* Settings = LoadSettingsFromIni(TEXT("SettingsVersion=1\nOllamaConfig=(NumCtx=8192,bUseSystemPrompts=False,TopP=0.900000)"));
			if (!TestNotNull(TEXT("settings loaded"), Settings)) { return; }
			TestEqual(TEXT("an explicit NumCtx 8192 is kept"), Settings->OllamaConfig.NumCtx, 8192);
			TestFalse(TEXT("an explicit bUseSystemPrompts false is kept"), Settings->OllamaConfig.bUseSystemPrompts);
			TestFalse(TEXT("an override left off stays off"), Settings->OllamaConfig.bOverride_TopP);
		});
	});

	Describe(TEXT("LMStudio"), [this]()
	{
		It(TEXT("sends no temperature and still receives the full schema"), [this]()
		{
			const FString Payload = BuildPayload<UN2CLMStudioService>(EN2CLLMProvider::LMStudio, TEXT("qwen3-32b"));
			const TSharedPtr<FJsonObject> Root = Parse(Payload);
			if (!TestTrue(TEXT("payload parses"), Root.IsValid())) { return; }
			TestNoSampling(TEXT("LM Studio root"), Root);
			TestTrue(TEXT("LM Studio has max_tokens"), Root->HasField(TEXT("max_tokens")));
			TestTrue(TEXT("LM Studio still receives additionalProperties"), Payload.Contains(TEXT("additionalProperties")));
			TestStrictJsonSchema(TEXT("LM Studio"), Root);
		});
	});

	Describe(TEXT("Builder"), [this]()
	{
		It(TEXT("strips additionalProperties at every level without touching the source schema"), [this]()
		{
			const TSharedPtr<FJsonObject> Schema = UN2CLLMPayloadBuilder::GetN2CResponseSchema();
			const TSharedPtr<FJsonObject> Stripped = UN2CLLMPayloadBuilder::StripAdditionalProperties(Schema);

			FString StrippedText;
			FJsonSerializer::Serialize(Stripped.ToSharedRef(), TJsonWriterFactory<>::Create(&StrippedText));
			FString SourceText;
			FJsonSerializer::Serialize(Schema.ToSharedRef(), TJsonWriterFactory<>::Create(&SourceText));

			TestFalse(TEXT("copy has no additionalProperties"), StrippedText.Contains(TEXT("additionalProperties")));
			TestTrue(TEXT("copy keeps the nested required lists"), StrippedText.Contains(TEXT("graphImplementation")));
			TestTrue(TEXT("source schema still has additionalProperties"), SourceText.Contains(TEXT("additionalProperties")));
		});

		It(TEXT("creates and attaches generationConfig instead of writing to a shared empty object"), [this]()
		{
			UN2CLLMPayloadBuilder* Builder = NewObject<UN2CLLMPayloadBuilder>(GetTransientPackage());
			Builder->Initialize(TEXT("gemini-2.0-flash"));
			Builder->ConfigureForGemini();
			Builder->SetMaxTokens(1234);
			const TSharedPtr<FJsonObject> Root = Parse(Builder->Build());
			const TSharedPtr<FJsonObject> GenConfig = Child(Root, TEXT("generationConfig"));
			if (!TestTrue(TEXT("generationConfig attached to the payload"), GenConfig.IsValid())) { return; }
			double MaxTokens = 0.0;
			TestTrue(TEXT("maxOutputTokens written into the payload"), GenConfig->TryGetNumberField(TEXT("maxOutputTokens"), MaxTokens));
			TestEqual(TEXT("maxOutputTokens value"), MaxTokens, 1234.0);
		});

		It(TEXT("writes only the model before a provider is configured"), [this]()
		{
			UN2CLLMPayloadBuilder* Builder = NewObject<UN2CLLMPayloadBuilder>(GetTransientPackage());
			Builder->Initialize(TEXT("claude-opus-5-5"));
			const TSharedPtr<FJsonObject> Root = Parse(Builder->Build());
			if (!TestTrue(TEXT("payload parses"), Root.IsValid())) { return; }
			TestEqual(TEXT("Initialize writes one field"), Root->Values.Num(), 1);
			TestTrue(TEXT("that field is the model"), Root->HasField(TEXT("model")));
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
