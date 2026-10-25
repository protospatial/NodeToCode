// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CSettings.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "LLM/N2CBaseLLMService.h"
#include "LLM/N2CLLMModelRegistry.h"
#include "LLM/N2CLLMPayloadBuilder.h"
#include "LLM/Providers/N2CAnthropicService.h"
#include "LLM/Providers/N2CDeepSeekService.h"
#include "LLM/Providers/N2CGeminiService.h"
#include "LLM/Providers/N2CLMStudioService.h"
#include "LLM/Providers/N2COllamaService.h"
#include "LLM/Providers/N2COpenAIService.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Tests/N2CTestHelpers.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"

/**
 * Golden request payloads for every provider and every model the registry still serves (retired entries excluded),
 * plus an explicit Max Output Tokens budget and an unregistered model ID per cloud provider. Each case builds the
 * body through the provider's service (FormatRequestPayload via BuildRequestPayloadForTesting; nothing is sent),
 * takes the endpoint from GetConfiguration and the headers the HTTP handler would send (Content-Type, a Bearer
 * Authorization when the service returns an auth token, then GetProviderHeaders), and compares all three with
 * Goldens/LLM/Payload/<provider>.json.
 *
 * Each case also checks the payload rules the live provider matrix (Tests/mcp_probe/live_provider_matrix.py, gate
 * 0374) proved against the real APIs, read from the registry flags, so a golden regenerated from a broken build
 * cannot hide a rule break: no sampling fields unless bAcceptsSamplingParams; the budget in the provider's field
 * (max_tokens, max_completion_tokens per bUsesMaxCompletionTokens, max_output_tokens on the Responses API,
 * generationConfig.maxOutputTokens), Auto = min(MaxOutputTokens, 32000, or 65536 for DeepSeek thinking); strict
 * json_schema per bSupportsStructuredOutput for OpenAI; a Gemini responseSchema without additionalProperties and the
 * key in x-goog-api-key, never in the URL; DeepSeek thinking.type and reasoning_effort per ReasoningEffort and
 * json_object only with thinking off.
 *
 * To regenerate after an intended wire-format change, run this spec with N2C_UPDATE_GOLDENS=1 in the editor's
 * environment, then review the diff of Goldens/LLM/Payload: every change there is a change to what goes on the wire.
 * The API key in every case is a placeholder; no golden may hold a real key.
 */
BEGIN_DEFINE_SPEC(FN2CPayloadGoldensSpec, "NodeToCode.LLM.Payload.Golden", N2C_TEST_FLAGS)

	static constexpr const TCHAR* TestKey = TEXT("n2c-placeholder-key-0125");
	static constexpr const TCHAR* SystemText = TEXT("You are a NodeToCode translator.");
	static constexpr const TCHAR* UserText = TEXT("Translate this Blueprint graph: {\"nodes\":[]}");

	/** One request to build: a model ID, its thinking control and the Max Output Tokens setting (0 = Auto) */
	struct FPayloadCase
	{
		FString Id;
		FString Model;
		FString ReasoningEffort;
		int32 MaxOutputTokens = 0;
		/** Registry entry the request resolves to; null for an unregistered ID and the local providers */
		const FN2CModelMetadata* Meta = nullptr;
	};

	/** What a service would put on the wire for one case */
	struct FBuilt
	{
		FString Endpoint;
		TMap<FString, FString> Headers;
		TSharedPtr<FJsonObject> Body;
		FString BodyText;
	};

	/**
	 * Settings the services read from the settings object, set to fixed values for the life of the scope (no
	 * reference source files, default Ollama config, the default LM Studio endpoint, no prepended commands)
	 */
	struct FScopedSettings
	{
		FScopedSettings()
		{
			UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
			SavedFiles = Settings->ReferenceSourceFilePaths;
			SavedOllama = Settings->OllamaConfig;
			SavedLMStudioEndpoint = Settings->LMStudioEndpoint;
			SavedLMStudioCommand = Settings->LMStudioPrependedModelCommand;
			Settings->ReferenceSourceFilePaths.Reset();
			Settings->OllamaConfig = FN2COllamaConfig();
			Settings->LMStudioEndpoint = TEXT("http://localhost:1234");
			Settings->LMStudioPrependedModelCommand.Reset();
		}
		~FScopedSettings()
		{
			UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
			Settings->ReferenceSourceFilePaths = SavedFiles;
			Settings->OllamaConfig = SavedOllama;
			Settings->LMStudioEndpoint = SavedLMStudioEndpoint;
			Settings->LMStudioPrependedModelCommand = SavedLMStudioCommand;
		}
		TArray<FFilePath> SavedFiles;
		FN2COllamaConfig SavedOllama;
		FString SavedLMStudioEndpoint;
		FString SavedLMStudioCommand;
	};

	static FString ProviderSlug(EN2CLLMProvider Provider)
	{
		switch (Provider)
		{
		case EN2CLLMProvider::Anthropic: return TEXT("anthropic");
		case EN2CLLMProvider::OpenAI: return TEXT("openai");
		case EN2CLLMProvider::Gemini: return TEXT("gemini");
		case EN2CLLMProvider::DeepSeek: return TEXT("deepseek");
		case EN2CLLMProvider::Ollama: return TEXT("ollama");
		case EN2CLLMProvider::LMStudio: return TEXT("lmstudio");
		default: return TEXT("unknown");
		}
	}

	static UN2CBaseLLMService* MakeService(EN2CLLMProvider Provider)
	{
		switch (Provider)
		{
		case EN2CLLMProvider::Anthropic: return NewObject<UN2CAnthropicService>(GetTransientPackage());
		case EN2CLLMProvider::OpenAI: return NewObject<UN2COpenAIService>(GetTransientPackage());
		case EN2CLLMProvider::Gemini: return NewObject<UN2CGeminiService>(GetTransientPackage());
		case EN2CLLMProvider::DeepSeek: return NewObject<UN2CDeepSeekService>(GetTransientPackage());
		case EN2CLLMProvider::Ollama: return NewObject<UN2COllamaService>(GetTransientPackage());
		case EN2CLLMProvider::LMStudio: return NewObject<UN2CLMStudioService>(GetTransientPackage());
		default: return nullptr;
		}
	}

	/** Every case for a provider: each registry entry that is not retired, then the extra cases */
	static TArray<FPayloadCase> CasesFor(EN2CLLMProvider Provider)
	{
		TArray<FPayloadCase> Cases;
		const FN2CLLMModelRegistry& Registry = FN2CLLMModelRegistry::Get();
		if (Provider == EN2CLLMProvider::Ollama || Provider == EN2CLLMProvider::LMStudio)
		{
			// Local servers: the model is whatever the user loaded; Auto sends no budget, explicit is sent as-is
			const FString Model = Provider == EN2CLLMProvider::Ollama ? TEXT("qwen3:32b") : TEXT("qwen3-32b");
			Cases.Add({ Model, Model, FString(), 0, nullptr });
			Cases.Add({ Model + TEXT(":max4096"), Model, FString(), 4096, nullptr });
			return Cases;
		}

		for (const FN2CModelMetadata& Entry : Registry.GetAllModelsForProvider(Provider))
		{
			if (Entry.bIsRetired)
			{
				continue;
			}
			FPayloadCase Case;
			Case.Model = Entry.ModelId;
			Case.ReasoningEffort = Entry.ReasoningEffort;
			Case.Id = Entry.ReasoningEffort.IsEmpty() ? Entry.ModelId : Entry.ModelId + TEXT("@") + Entry.ReasoningEffort;
			Case.Meta = Registry.GetModelMetadataById(Entry.ModelId, Entry.ReasoningEffort);
			Cases.Add(Case);
		}

		// An explicit budget on the provider's default model (DeepSeek: the thinking-off entry, so the sampling and
		// json_object path is covered with an explicit value too)
		FString DefaultModel;
		FString DefaultEffort;
		switch (Provider)
		{
		case EN2CLLMProvider::Anthropic: DefaultModel = Registry.GetModelId(N2CDefaultModels::Anthropic); break;
		case EN2CLLMProvider::OpenAI: DefaultModel = Registry.GetModelId(N2CDefaultModels::OpenAI); break;
		case EN2CLLMProvider::Gemini: DefaultModel = Registry.GetModelId(N2CDefaultModels::Gemini); break;
		case EN2CLLMProvider::DeepSeek: DefaultModel = Registry.GetModelId(EN2CDeepSeekModel::DeepSeek_V3); DefaultEffort = TEXT("none"); break;
		default: break;
		}
		FPayloadCase Explicit;
		Explicit.Model = DefaultModel;
		Explicit.ReasoningEffort = DefaultEffort;
		Explicit.MaxOutputTokens = 4096;
		Explicit.Id = (DefaultEffort.IsEmpty() ? DefaultModel : DefaultModel + TEXT("@") + DefaultEffort) + TEXT(":max4096");
		Explicit.Meta = Registry.GetModelMetadataById(DefaultModel, DefaultEffort);
		Cases.Add(Explicit);

		// An ID the registry does not know, as an MCP model override sends one
		FPayloadCase Unregistered;
		Unregistered.Model = FString::Printf(TEXT("%s-n2c-unregistered"), *ProviderSlug(Provider));
		Unregistered.Id = TEXT("unregistered:") + Unregistered.Model;
		Cases.Add(Unregistered);
		return Cases;
	}

	FBuilt Build(EN2CLLMProvider Provider, const FPayloadCase& Case)
	{
		FBuilt Built;
		UN2CBaseLLMService* Service = MakeService(Provider);
		if (!Service)
		{
			return Built;
		}
		FN2CLLMConfig Config;
		Config.Provider = Provider;
		Config.Model = Case.Model;
		Config.ReasoningEffort = Case.ReasoningEffort;
		Config.MaxOutputTokens = Case.MaxOutputTokens;
		Config.ApiKey = (Provider == EN2CLLMProvider::Ollama) ? FString() : FString(TestKey);
		Service->Initialize(Config);

		FString AuthToken;
		bool bSupportsSystemPrompts = true;
		Service->GetConfiguration(Built.Endpoint, AuthToken, bSupportsSystemPrompts);

		// As UN2CHttpHandlerBase::PostLLMRequest sets them; header names are case-insensitive, so they are lowercased
		TMap<FString, FString> ProviderHeaders;
		Service->GetProviderHeaders(ProviderHeaders);
		Built.Headers.Add(TEXT("content-type"), TEXT("application/json"));
		if (!AuthToken.IsEmpty())
		{
			Built.Headers.Add(TEXT("authorization"), FString::Printf(TEXT("Bearer %s"), *AuthToken));
		}
		for (const TPair<FString, FString>& Header : ProviderHeaders)
		{
			Built.Headers.Add(Header.Key.ToLower(), Header.Value);
		}
		Built.Headers.KeySort(TLess<FString>());

		Built.BodyText = Service->BuildRequestPayloadForTesting(UserText, SystemText);
		Built.Body = N2CTest::ParseJsonObject(Built.BodyText);
		return Built;
	}

	static TSharedPtr<FJsonObject> ToGoldenEntry(const FBuilt& Built)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("endpoint"), Built.Endpoint);
		TSharedPtr<FJsonObject> Headers = MakeShared<FJsonObject>();
		for (const TPair<FString, FString>& Header : Built.Headers)
		{
			Headers->SetStringField(Header.Key, Header.Value);
		}
		Entry->SetObjectField(TEXT("headers"), Headers);
		Entry->SetObjectField(TEXT("body"), Built.Body);
		return Entry;
	}

	static FString GoldenPath(EN2CLLMProvider Provider)
	{
		return N2CTest::GoldensDir(TEXT("LLM/Payload")) / ProviderSlug(Provider) + TEXT(".json");
	}

	static bool UpdateGoldens()
	{
		return FPlatformMisc::GetEnvironmentVariable(TEXT("N2C_UPDATE_GOLDENS")) == TEXT("1");
	}

	static FString JsonTypeName(EJson Type)
	{
		switch (Type)
		{
		case EJson::None: return TEXT("none");
		case EJson::Null: return TEXT("null");
		case EJson::String: return TEXT("string");
		case EJson::Number: return TEXT("number");
		case EJson::Boolean: return TEXT("boolean");
		case EJson::Array: return TEXT("array");
		case EJson::Object: return TEXT("object");
		default: return TEXT("?");
		}
	}

	static FString Shorten(const FString& Text)
	{
		return Text.Len() > 120 ? Text.Left(117) + TEXT("...") : Text;
	}

	static FString ValueText(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid())
		{
			return TEXT("(missing)");
		}
		switch (Value->Type)
		{
		case EJson::String: return FString::Printf(TEXT("\"%s\""), *Shorten(Value->AsString()));
		case EJson::Number: return FString::SanitizeFloat(Value->AsNumber());
		case EJson::Boolean: return Value->AsBool() ? TEXT("true") : TEXT("false");
		default: return JsonTypeName(Value->Type);
		}
	}

	/** Every difference between Expected and Actual as "path: expected X, got Y" (object key order is ignored) */
	static void Diff(const FString& Path, const TSharedPtr<FJsonValue>& Expected, const TSharedPtr<FJsonValue>& Actual, TArray<FString>& Out)
	{
		if (!Expected.IsValid() || !Actual.IsValid() || Expected->Type != Actual->Type)
		{
			Out.Add(FString::Printf(TEXT("%s: expected %s, got %s"), *Path, *ValueText(Expected), *ValueText(Actual)));
			return;
		}
		switch (Expected->Type)
		{
		case EJson::Object:
		{
			const TSharedPtr<FJsonObject> E = Expected->AsObject();
			const TSharedPtr<FJsonObject> A = Actual->AsObject();
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : E->Values)
			{
				Diff(Path + TEXT(".") + Pair.Key, Pair.Value, A->TryGetField(Pair.Key), Out);
			}
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : A->Values)
			{
				if (!E->HasField(Pair.Key))
				{
					Out.Add(FString::Printf(TEXT("%s.%s: not in the golden, got %s"), *Path, *Pair.Key, *ValueText(Pair.Value)));
				}
			}
			break;
		}
		case EJson::Array:
		{
			const TArray<TSharedPtr<FJsonValue>>& E = Expected->AsArray();
			const TArray<TSharedPtr<FJsonValue>>& A = Actual->AsArray();
			if (E.Num() != A.Num())
			{
				Out.Add(FString::Printf(TEXT("%s: expected %d items, got %d"), *Path, E.Num(), A.Num()));
			}
			for (int32 Index = 0; Index < FMath::Min(E.Num(), A.Num()); ++Index)
			{
				Diff(FString::Printf(TEXT("%s[%d]"), *Path, Index), E[Index], A[Index], Out);
			}
			break;
		}
		case EJson::String:
			if (Expected->AsString() != Actual->AsString())
			{
				Out.Add(FString::Printf(TEXT("%s: expected %s, got %s"), *Path, *ValueText(Expected), *ValueText(Actual)));
			}
			break;
		case EJson::Number:
			if (Expected->AsNumber() != Actual->AsNumber())
			{
				Out.Add(FString::Printf(TEXT("%s: expected %s, got %s"), *Path, *ValueText(Expected), *ValueText(Actual)));
			}
			break;
		case EJson::Boolean:
			if (Expected->AsBool() != Actual->AsBool())
			{
				Out.Add(FString::Printf(TEXT("%s: expected %s, got %s"), *Path, *ValueText(Expected), *ValueText(Actual)));
			}
			break;
		default:
			break;
		}
	}

	static int32 CountKey(const TSharedPtr<FJsonValue>& Value, const FString& Key)
	{
		if (!Value.IsValid())
		{
			return 0;
		}
		int32 Count = 0;
		if (Value->Type == EJson::Object)
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Value->AsObject()->Values)
			{
				Count += (Pair.Key == Key ? 1 : 0) + CountKey(Pair.Value, Key);
			}
		}
		else if (Value->Type == EJson::Array)
		{
			for (const TSharedPtr<FJsonValue>& Item : Value->AsArray())
			{
				Count += CountKey(Item, Key);
			}
		}
		return Count;
	}

	static TSharedPtr<FJsonObject> Child(const TSharedPtr<FJsonObject>& Object, const FString& Name)
	{
		const TSharedPtr<FJsonObject>* Found = nullptr;
		if (Object.IsValid() && Object->TryGetObjectField(Name, Found) && Found)
		{
			return *Found;
		}
		return nullptr;
	}

	static int64 NumberOr(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		double Value = -1.0;
		if (!Object.IsValid() || !Object->TryGetNumberField(Field, Value))
		{
			return -1;
		}
		return static_cast<int64>(Value);
	}

	/** The live matrix's budget rule: an explicit value capped at the registry maximum, Auto at the provider ceiling */
	static int64 ExpectedBudget(EN2CLLMProvider Provider, const FPayloadCase& Case)
	{
		const FN2CModelMetadata* Meta = Case.Meta;
		if (Case.MaxOutputTokens > 0)
		{
			return Meta ? FMath::Min(Case.MaxOutputTokens, Meta->MaxOutputTokens) : Case.MaxOutputTokens;
		}
		if (Provider == EN2CLLMProvider::Ollama || Provider == EN2CLLMProvider::LMStudio)
		{
			return -1;
		}
		if (!Meta)
		{
			return Provider == EN2CLLMProvider::Anthropic ? UN2CLLMPayloadBuilder::AnthropicUnregisteredMaxTokens : -1;
		}
		const int64 Cap = (Provider == EN2CLLMProvider::DeepSeek && Meta->IsThinkingEnabled()) ? 65536 : 32000;
		return FMath::Min<int64>(Meta->MaxOutputTokens, Cap);
	}

	/** Checks one case's payload against the rules the live matrix proved, from the registry flags */
	void CheckRules(EN2CLLMProvider Provider, const FPayloadCase& Case, const FBuilt& Built)
	{
		const FString What = ProviderSlug(Provider) + TEXT("/") + Case.Id;
		const TSharedPtr<FJsonObject>& Root = Built.Body;
		if (!TestTrue(What + TEXT(": body parses"), Root.IsValid())) { return; }
		const FN2CModelMetadata* Meta = Case.Meta;
		const TSharedPtr<FJsonValue> RootValue = MakeShared<FJsonValueObject>(Root);
		TestEqual(What + TEXT(": model"), Root->HasField(TEXT("model")) ? Root->GetStringField(TEXT("model")) : FString(), Case.Model);

		// Sampling: none unless the registry entry accepts them (Ollama keeps its samplers in options, off by default)
		const bool bSampling = Meta && Meta->bAcceptsSamplingParams;
		const int32 SamplingFields = CountKey(RootValue, TEXT("temperature")) + CountKey(RootValue, TEXT("top_p"))
			+ CountKey(RootValue, TEXT("top_k")) + CountKey(RootValue, TEXT("topP")) + CountKey(RootValue, TEXT("topK"));
		if (bSampling)
		{
			TestEqual(What + TEXT(": sends temperature (registry accepts sampling)"), CountKey(RootValue, TEXT("temperature")), 1);
		}
		else
		{
			TestEqual(What + TEXT(": sends no sampling fields"), SamplingFields, 0);
		}

		// Output budget: one field, in the provider's place
		const TSharedPtr<FJsonObject> GenConfig = Child(Root, TEXT("generationConfig"));
		const bool bResponsesApi = Provider == EN2CLLMProvider::OpenAI && FN2CLLMModelRegistry::Get().RequiresResponsesAPI(Case.Model);
		FString BudgetField;
		switch (Provider)
		{
		case EN2CLLMProvider::OpenAI:
			// Unregistered OpenAI IDs are treated as reasoning models
			BudgetField = bResponsesApi ? TEXT("max_output_tokens")
				: ((!Meta || Meta->bUsesMaxCompletionTokens) ? TEXT("max_completion_tokens") : TEXT("max_tokens"));
			break;
		case EN2CLLMProvider::Gemini: BudgetField = TEXT("maxOutputTokens"); break;
		case EN2CLLMProvider::Ollama: BudgetField = FString(); break; // num_predict in options, from the Ollama config
		default: BudgetField = TEXT("max_tokens"); break;
		}
		if (!BudgetField.IsEmpty())
		{
			const TSharedPtr<FJsonObject> Holder = Provider == EN2CLLMProvider::Gemini ? GenConfig : Root;
			TestEqual(What + FString::Printf(TEXT(": %s"), *BudgetField), NumberOr(Holder, *BudgetField), ExpectedBudget(Provider, Case));
			for (const TCHAR* Other : { TEXT("max_tokens"), TEXT("max_completion_tokens"), TEXT("max_output_tokens"), TEXT("maxOutputTokens") })
			{
				if (BudgetField != Other)
				{
					TestEqual(What + FString::Printf(TEXT(": no %s"), Other), CountKey(RootValue, Other), 0);
				}
			}
		}

		// Response format
		switch (Provider)
		{
		case EN2CLLMProvider::OpenAI:
		{
			if (bResponsesApi)
			{
				const TSharedPtr<FJsonObject> Format = Child(Child(Root, TEXT("text")), TEXT("format"));
				TestTrue(What + TEXT(": Responses API text.format is strict json_schema"), Format.IsValid()
					&& Format->GetStringField(TEXT("type")) == TEXT("json_schema") && Format->HasTypedField<EJson::Boolean>(TEXT("strict"))
					&& Format->GetBoolField(TEXT("strict")));
				break;
			}
			const TSharedPtr<FJsonObject> ResponseFormat = Child(Root, TEXT("response_format"));
			const bool bStructured = !Meta || Meta->bSupportsStructuredOutput;
			if (bStructured)
			{
				const TSharedPtr<FJsonObject> Wrapper = Child(ResponseFormat, TEXT("json_schema"));
				TestTrue(What + TEXT(": response_format json_schema with strict true (a JSON boolean)"), ResponseFormat.IsValid()
					&& ResponseFormat->GetStringField(TEXT("type")) == TEXT("json_schema") && Wrapper.IsValid()
					&& Wrapper->HasTypedField<EJson::Boolean>(TEXT("strict")) && Wrapper->GetBoolField(TEXT("strict"))
					&& Child(Wrapper, TEXT("schema")).IsValid());
			}
			else
			{
				TestFalse(What + TEXT(": no response_format for a model without structured output"), ResponseFormat.IsValid());
			}
			break;
		}
		case EN2CLLMProvider::Gemini:
		{
			TestTrue(What + TEXT(": generationConfig.responseSchema"), Child(GenConfig, TEXT("responseSchema")).IsValid());
			TestEqual(What + TEXT(": responseMimeType"), GenConfig.IsValid() ? GenConfig->GetStringField(TEXT("responseMimeType")) : FString(),
				FString(TEXT("application/json")));
			TestEqual(What + TEXT(": no additionalProperties anywhere"), CountKey(RootValue, TEXT("additionalProperties")), 0);
			TestFalse(What + TEXT(": the key is not in the endpoint URL"), Built.Endpoint.Contains(TestKey) || Built.Endpoint.Contains(TEXT("key=")));
			TestEqual(What + TEXT(": the key is in x-goog-api-key"), Built.Headers.FindRef(TEXT("x-goog-api-key")), FString(TestKey));
			TestFalse(What + TEXT(": no Authorization header"), Built.Headers.Contains(TEXT("authorization")));
			TestTrue(What + TEXT(": endpoint is the model's generateContent"), Built.Endpoint.EndsWith(Case.Model + TEXT(":generateContent")));
			break;
		}
		case EN2CLLMProvider::DeepSeek:
		{
			const bool bThinking = Meta ? Meta->IsThinkingEnabled() : false;
			const TSharedPtr<FJsonObject> Thinking = Child(Root, TEXT("thinking"));
			if (Meta)
			{
				TestEqual(What + TEXT(": thinking.type"), Thinking.IsValid() ? Thinking->GetStringField(TEXT("type")) : FString(),
					FString(bThinking ? TEXT("enabled") : TEXT("disabled")));
			}
			else
			{
				TestFalse(What + TEXT(": no thinking field for an unregistered ID"), Thinking.IsValid());
			}
			FString Effort;
			Root->TryGetStringField(TEXT("reasoning_effort"), Effort);
			TestEqual(What + TEXT(": reasoning_effort"), Effort, bThinking ? Meta->ReasoningEffort : FString());
			const TSharedPtr<FJsonObject> ResponseFormat = Child(Root, TEXT("response_format"));
			const bool bJson = Meta && !bThinking && Meta->bSupportsStructuredOutput;
			TestEqual(What + TEXT(": response_format json_object only with thinking off"),
				ResponseFormat.IsValid() ? ResponseFormat->GetStringField(TEXT("type")) : FString(), FString(bJson ? TEXT("json_object") : TEXT("")));
			break;
		}
		case EN2CLLMProvider::Anthropic:
			TestEqual(What + TEXT(": x-api-key"), Built.Headers.FindRef(TEXT("x-api-key")), FString(TestKey));
			TestEqual(What + TEXT(": anthropic-version"), Built.Headers.FindRef(TEXT("anthropic-version")), FString(TEXT("2023-06-01")));
			TestFalse(What + TEXT(": no response_format (the Messages API has none)"), Root->HasField(TEXT("response_format")));
			break;
		case EN2CLLMProvider::LMStudio:
		{
			const TSharedPtr<FJsonObject> ResponseFormat = Child(Root, TEXT("response_format"));
			TestTrue(What + TEXT(": response_format json_schema"), ResponseFormat.IsValid()
				&& ResponseFormat->GetStringField(TEXT("type")) == TEXT("json_schema"));
			break;
		}
		default:
			break;
		}
	}

	/** Builds every case for Provider, checks the rules and compares (or, with N2C_UPDATE_GOLDENS=1, writes) the golden */
	void RunProvider(EN2CLLMProvider Provider)
	{
		const FScopedSettings Scope;
		const TArray<FPayloadCase> Cases = CasesFor(Provider);
		TestTrue(TEXT("has cases"), Cases.Num() > 0);

		TSharedPtr<FJsonObject> Actual = MakeShared<FJsonObject>();
		for (const FPayloadCase& Case : Cases)
		{
			const FBuilt Built = Build(Provider, Case);
			CheckRules(Provider, Case, Built);
			if (Built.Body.IsValid())
			{
				Actual->SetObjectField(Case.Id, ToGoldenEntry(Built));
			}
			TestFalse(Case.Id + TEXT(": the placeholder key is not in the body"), Built.BodyText.Contains(TestKey));
		}

		const FString Path = GoldenPath(Provider);
		if (UpdateGoldens())
		{
			FString Text;
			const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Text);
			FJsonSerializer::Serialize(Actual.ToSharedRef(), Writer);
			Text.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
			Text += TEXT("\n");
			TestTrue(TEXT("golden written: ") + Path, FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
			AddWarning(FString::Printf(TEXT("N2C_UPDATE_GOLDENS=1: wrote %s; review the diff before committing"), *Path));
			return;
		}

		FString GoldenText;
		if (!TestTrue(FString::Printf(TEXT("golden %s exists (run with N2C_UPDATE_GOLDENS=1 to create it)"), *Path),
			FFileHelper::LoadFileToString(GoldenText, *Path)))
		{
			return;
		}
		const TSharedPtr<FJsonObject> Golden = N2CTest::ParseJsonObject(GoldenText);
		if (!TestTrue(TEXT("golden parses"), Golden.IsValid())) { return; }

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Actual->Values)
		{
			const TSharedPtr<FJsonValue> Expected = Golden->TryGetField(Pair.Key);
			if (!TestTrue(FString::Printf(TEXT("%s has a golden (a new registry model needs one: N2C_UPDATE_GOLDENS=1)"), *Pair.Key), Expected.IsValid()))
			{
				continue;
			}
			TArray<FString> Differences;
			Diff(Pair.Key, Expected, Pair.Value, Differences);
			for (const FString& Difference : Differences)
			{
				AddError(FString::Printf(TEXT("%s differs from the golden: %s"), *ProviderSlug(Provider), *Difference));
			}
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Golden->Values)
		{
			TestTrue(FString::Printf(TEXT("golden case %s is still built (remove it when its model is retired)"), *Pair.Key), Actual->HasField(Pair.Key));
		}
	}

END_DEFINE_SPEC(FN2CPayloadGoldensSpec)

void FN2CPayloadGoldensSpec::Define()
{
	Describe(TEXT("Anthropic"), [this]()
	{
		It(TEXT("sends the golden body, endpoint and headers for every current model"), [this]() { RunProvider(EN2CLLMProvider::Anthropic); });
	});
	Describe(TEXT("OpenAI"), [this]()
	{
		It(TEXT("sends the golden body, endpoint and headers for every current model"), [this]() { RunProvider(EN2CLLMProvider::OpenAI); });
	});
	Describe(TEXT("Gemini"), [this]()
	{
		It(TEXT("sends the golden body, endpoint and headers for every current model"), [this]() { RunProvider(EN2CLLMProvider::Gemini); });
	});
	Describe(TEXT("DeepSeek"), [this]()
	{
		It(TEXT("sends the golden body, endpoint and headers for every current model and thinking mode"), [this]() { RunProvider(EN2CLLMProvider::DeepSeek); });
	});
	Describe(TEXT("Ollama"), [this]()
	{
		It(TEXT("sends the golden body, endpoint and headers with default settings"), [this]() { RunProvider(EN2CLLMProvider::Ollama); });
	});
	Describe(TEXT("LMStudio"), [this]()
	{
		It(TEXT("sends the golden body, endpoint and headers with default settings"), [this]() { RunProvider(EN2CLLMProvider::LMStudio); });
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
