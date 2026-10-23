// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

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

/**
 * Builds each provider's request body through its service (no request is sent, no key is used) and checks
 * the fields: no sampling parameters where the registry says the model rejects them, the right max-token
 * field, and a Gemini schema without additionalProperties.
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

	TSharedPtr<FJsonObject> Parse(const FString& Payload)
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Payload);
		FJsonSerializer::Deserialize(Reader, Root);
		return Root;
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

END_DEFINE_SPEC(FN2CPayloadBuilderSpec)

void FN2CPayloadBuilderSpec::Define()
{
	Describe(TEXT("Anthropic"), [this]()
	{
		It(TEXT("sends no sampling fields for a current model ID and keeps max_tokens"), [this]()
		{
			// claude-opus-5-5 is not in the registry; Opus 4.7 and later reject sampling parameters
			const TSharedPtr<FJsonObject> Root = Parse(BuildPayload<UN2CAnthropicService>(EN2CLLMProvider::Anthropic, TEXT("claude-opus-5-5")));
			if (!TestTrue(TEXT("payload parses"), Root.IsValid())) { return; }
			TestNoSampling(TEXT("Anthropic root"), Root);
			double MaxTokens = 0.0;
			TestTrue(TEXT("Anthropic has max_tokens"), Root->TryGetNumberField(TEXT("max_tokens"), MaxTokens));
			TestEqual(TEXT("Anthropic max_tokens"), MaxTokens, 16000.0);
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
			const TSharedPtr<FJsonObject> Root = Parse(BuildPayload<UN2COpenAIService>(EN2CLLMProvider::OpenAI, TEXT("gpt-6.1-sol")));
			if (!TestTrue(TEXT("payload parses"), Root.IsValid())) { return; }
			TestNoSampling(TEXT("OpenAI unregistered root"), Root);
			TestTrue(TEXT("unregistered ID uses max_completion_tokens"), Root->HasField(TEXT("max_completion_tokens")));
			TestFalse(TEXT("unregistered ID sends no max_tokens"), Root->HasField(TEXT("max_tokens")));
		});

		It(TEXT("sends no temperature for a registered reasoning model"), [this]()
		{
			const TSharedPtr<FJsonObject> Root = Parse(BuildPayload<UN2COpenAIService>(EN2CLLMProvider::OpenAI, TEXT("gpt-5.2")));
			if (!TestTrue(TEXT("payload parses"), Root.IsValid())) { return; }
			TestFalse(TEXT("registry flag for gpt-5.2"), RegistryAcceptsSampling(TEXT("gpt-5.2")));
			TestNoSampling(TEXT("OpenAI gpt-5.2 root"), Root);
			TestTrue(TEXT("gpt-5.2 uses max_completion_tokens"), Root->HasField(TEXT("max_completion_tokens")));
			TestFalse(TEXT("gpt-5.2 sends no max_tokens"), Root->HasField(TEXT("max_tokens")));
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
		});
	});

	Describe(TEXT("Gemini"), [this]()
	{
		It(TEXT("sends no sampling fields and no additionalProperties for Gemini 3"), [this]()
		{
			const FString Payload = BuildPayload<UN2CGeminiService>(EN2CLLMProvider::Gemini, TEXT("gemini-3-flash-preview"));
			TSharedPtr<FJsonObject> Root = Parse(Payload);
			if (!TestTrue(TEXT("payload parses"), Root.IsValid())) { return; }
			// The Code Assist (OAuth) route wraps the same body in "request"
			if (const TSharedPtr<FJsonObject> Wrapped = Child(Root, TEXT("request")))
			{
				Root = Wrapped;
			}
			TestNoSampling(TEXT("Gemini root"), Root);
			const TSharedPtr<FJsonObject> GenConfig = Child(Root, TEXT("generationConfig"));
			if (!TestTrue(TEXT("Gemini has generationConfig"), GenConfig.IsValid())) { return; }
			TestNoSampling(TEXT("Gemini generationConfig"), GenConfig);
			TestTrue(TEXT("Gemini has maxOutputTokens"), GenConfig->HasField(TEXT("maxOutputTokens")));
			TestTrue(TEXT("Gemini has responseSchema"), Child(GenConfig, TEXT("responseSchema")).IsValid());
			TestFalse(TEXT("Gemini payload has no additionalProperties"), Payload.Contains(TEXT("additionalProperties")));
			TestFalse(TEXT("Gemini has no root max_tokens"), Root->HasField(TEXT("max_tokens")));
		});

		It(TEXT("sends no sampling fields for an unregistered Gemini ID"), [this]()
		{
			TSharedPtr<FJsonObject> Root = Parse(BuildPayload<UN2CGeminiService>(EN2CLLMProvider::Gemini, TEXT("gemini-3.8-flash")));
			if (!TestTrue(TEXT("payload parses"), Root.IsValid())) { return; }
			if (const TSharedPtr<FJsonObject> Wrapped = Child(Root, TEXT("request")))
			{
				Root = Wrapped;
			}
			TestNoSampling(TEXT("Gemini unregistered root"), Root);
			TestNoSampling(TEXT("Gemini unregistered generationConfig"), Child(Root, TEXT("generationConfig")));
		});
	});

	Describe(TEXT("DeepSeek"), [this]()
	{
		It(TEXT("sends no temperature for the reasoner and keeps max_tokens"), [this]()
		{
			const TSharedPtr<FJsonObject> Root = Parse(BuildPayload<UN2CDeepSeekService>(EN2CLLMProvider::DeepSeek, TEXT("deepseek-reasoner")));
			if (!TestTrue(TEXT("payload parses"), Root.IsValid())) { return; }
			TestNoSampling(TEXT("DeepSeek reasoner root"), Root);
			TestTrue(TEXT("DeepSeek has max_tokens"), Root->HasField(TEXT("max_tokens")));
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
			TestTrue(TEXT("Ollama keeps its options object"), Child(Root, TEXT("options")).IsValid());
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
