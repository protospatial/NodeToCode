// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/N2CModelCatalog.h"

#include "Dom/JsonObject.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Utils/N2CLogger.h"

namespace N2CModelCatalogData
{
	/** Catalog schema this build reads */
	constexpr int32 SchemaVersion = 1;

	struct FProviderName
	{
		EN2CLLMProvider Provider;
		const TCHAR* Id;
		const TCHAR* EnumName;
	};

	/** Provider enum values and their catalog IDs */
	static const FProviderName ProviderNames[] = {
		{ EN2CLLMProvider::OpenAI, TEXT("openai"), TEXT("OpenAI") },
		{ EN2CLLMProvider::Anthropic, TEXT("anthropic"), TEXT("Anthropic") },
		{ EN2CLLMProvider::Gemini, TEXT("gemini"), TEXT("Gemini") },
		{ EN2CLLMProvider::Ollama, TEXT("ollama"), TEXT("Ollama") },
		{ EN2CLLMProvider::DeepSeek, TEXT("deepseek"), TEXT("DeepSeek") },
		{ EN2CLLMProvider::LMStudio, TEXT("lmstudio"), TEXT("LMStudio") },
	};

	bool ProviderFromEnumName(const FString& Name, EN2CLLMProvider& Out)
	{
		for (const FProviderName& Entry : ProviderNames)
		{
			if (Name == Entry.EnumName)
			{
				Out = Entry.Provider;
				return true;
			}
		}
		return false;
	}

	/**
	 * Enumerators of the legacy model enums whose models the providers have shut down. They exist only so a saved
	 * selection naming one still resolves (and migrates to the provider default), so they are compiled in and not
	 * part of the catalog file, which lists the models NodeToCode offers. The enums are frozen: this list grows only
	 * when an enumerator is retired, never with new models.
	 */
	struct FRetiredRow
	{
		EN2CLLMProvider Provider;
		const TCHAR* EnumName;
		const TCHAR* ModelId;
		const TCHAR* DisplayName;
		const TCHAR* ShutdownDate;
		bool bSystemPrompts;
		bool bStructuredOutput;
	};

	static const FRetiredRow RetiredRows[] = {
		{ EN2CLLMProvider::OpenAI, TEXT("GPT_o4_mini"), TEXT("o4-mini"), TEXT("o4 Mini (retired)"), TEXT("2026-10-23"), true, true },
		{ EN2CLLMProvider::OpenAI, TEXT("GPT_o3_mini"), TEXT("o3-mini"), TEXT("o3 Mini (retired)"), TEXT("2026-10-23"), true, true },
		{ EN2CLLMProvider::OpenAI, TEXT("GPT_o1"), TEXT("o1"), TEXT("o1 (retired)"), TEXT("2026-10-23"), false, true },
		{ EN2CLLMProvider::OpenAI, TEXT("GPT_o1_Preview"), TEXT("o1-preview-2024-09-12"), TEXT("o1 Preview (retired)"), TEXT("2025-07-28"), false, false },
		{ EN2CLLMProvider::OpenAI, TEXT("GPT_o1_Mini"), TEXT("o1-mini-2024-09-12"), TEXT("o1 Mini (retired)"), TEXT("2025-10-27"), false, false },
		{ EN2CLLMProvider::Anthropic, TEXT("Claude4_1_Opus"), TEXT("claude-opus-4-1-20250805"), TEXT("Claude Opus 4.1 (retired)"), TEXT("2026-08-05"), true, false },
		{ EN2CLLMProvider::Anthropic, TEXT("Claude4_Opus"), TEXT("claude-opus-4-20250514"), TEXT("Claude Opus 4 (retired)"), TEXT("2026-06-15"), true, false },
		{ EN2CLLMProvider::Anthropic, TEXT("Claude4_Sonnet"), TEXT("claude-sonnet-4-20250514"), TEXT("Claude Sonnet 4 (retired)"), TEXT("2026-06-15"), true, false },
		{ EN2CLLMProvider::Anthropic, TEXT("Claude3_7_Sonnet"), TEXT("claude-3-7-sonnet-20250219"), TEXT("Claude 3.7 Sonnet (retired)"), TEXT("2026-02-19"), true, false },
		{ EN2CLLMProvider::Anthropic, TEXT("Claude3_5_Sonnet"), TEXT("claude-3-5-sonnet-20241022"), TEXT("Claude 3.5 Sonnet (retired)"), TEXT("2025-10-28"), true, false },
		{ EN2CLLMProvider::Anthropic, TEXT("Claude3_5_Haiku"), TEXT("claude-3-5-haiku-20241022"), TEXT("Claude 3.5 Haiku (retired)"), TEXT("2026-02-19"), true, false },
		{ EN2CLLMProvider::Gemini, TEXT("Gemini_3_Pro_Preview"), TEXT("gemini-3-pro-preview"), TEXT("Gemini 3 Pro Preview (retired)"), TEXT("2026-03-09"), true, true },
		{ EN2CLLMProvider::Gemini, TEXT("Gemini_2_5_Pro"), TEXT("gemini-2.5-pro-preview-05-06"), TEXT("Gemini 2.5 Pro Preview (retired)"), TEXT("2025-12-02"), true, true },
		{ EN2CLLMProvider::Gemini, TEXT("Gemini_2_5_Flash"), TEXT("gemini-2.5-flash-preview-05-20"), TEXT("Gemini 2.5 Flash Preview (retired)"), TEXT("2025-11-18"), true, true },
		{ EN2CLLMProvider::Gemini, TEXT("Gemini_Flash_2_0"), TEXT("gemini-2.0-flash"), TEXT("Gemini 2.0 Flash (retired)"), TEXT("2026-06-01"), true, true },
		{ EN2CLLMProvider::Gemini, TEXT("Gemini_Flash_Lite_2_0"), TEXT("gemini-2.0-flash-lite-preview-02-05"), TEXT("Gemini 2.0 Flash-Lite-Preview-02-05 (retired)"), TEXT("2025-12-09"), true, true },
		{ EN2CLLMProvider::Gemini, TEXT("Gemini_1_5_Flash"), TEXT("gemini-1.5-flash"), TEXT("Gemini 1.5 Flash (retired)"), TEXT(""), true, true },
		{ EN2CLLMProvider::Gemini, TEXT("Gemini_1_5_Pro"), TEXT("gemini-1.5-pro"), TEXT("Gemini 1.5 Pro (retired)"), TEXT(""), true, true },
		{ EN2CLLMProvider::Gemini, TEXT("Gemini_2_0_ProExp_02_05"), TEXT("gemini-2.0-pro-exp-02-05"), TEXT("Gemini 2.0 Pro Exp 02-05 (retired)"), TEXT(""), true, true },
		{ EN2CLLMProvider::Gemini, TEXT("Gemini_2_0_FlashThinkingExp"), TEXT("gemini-2.0-flash-thinking-exp-01-21"), TEXT("Gemini 2.0 Flash Thinking Exp 01-21 (retired)"), TEXT(""), true, true },
	};

	/** Provider rows used when the catalog file cannot be read: the services' built-in endpoints */
	TArray<FN2CProviderProfile> FallbackProviders()
	{
		auto Make = [](EN2CLLMProvider Provider, const TCHAR* Id, const TCHAR* DisplayName, const TCHAR* Dialect,
			const TCHAR* BaseUrl, const TCHAR* Path, const TCHAR* AuthStyle, bool bIsLocal, const TCHAR* ModelsPath)
		{
			FN2CProviderProfile Profile;
			Profile.Id = Id;
			Profile.DisplayName = DisplayName;
			Profile.Dialect = Dialect;
			Profile.BaseUrl = BaseUrl;
			Profile.Path = Path;
			Profile.AuthStyle = AuthStyle;
			Profile.bIsLocal = bIsLocal;
			Profile.bNeedsKey = !bIsLocal;
			Profile.ModelsPath = ModelsPath;
			Profile.Provider = Provider;
			return Profile;
		};
		return {
			Make(EN2CLLMProvider::OpenAI, TEXT("openai"), TEXT("OpenAI"), TEXT("openai_chat"), TEXT("https://api.openai.com"), TEXT("/v1/chat/completions"), TEXT("bearer"), false, TEXT("/v1/models")),
			Make(EN2CLLMProvider::Anthropic, TEXT("anthropic"), TEXT("Anthropic"), TEXT("anthropic_messages"), TEXT("https://api.anthropic.com"), TEXT("/v1/messages"), TEXT("x-api-key"), false, TEXT("/v1/models")),
			Make(EN2CLLMProvider::Gemini, TEXT("gemini"), TEXT("Google Gemini"), TEXT("gemini"), TEXT("https://generativelanguage.googleapis.com"), TEXT("/v1beta/models/"), TEXT("x-goog-api-key"), false, TEXT("/v1beta/models")),
			Make(EN2CLLMProvider::DeepSeek, TEXT("deepseek"), TEXT("DeepSeek"), TEXT("deepseek_chat"), TEXT("https://api.deepseek.com"), TEXT("/chat/completions"), TEXT("bearer"), false, TEXT("/models")),
			Make(EN2CLLMProvider::Ollama, TEXT("ollama"), TEXT("Ollama"), TEXT("ollama"), TEXT("http://localhost:11434"), TEXT("/api/chat"), TEXT("none"), true, TEXT("/api/tags")),
			Make(EN2CLLMProvider::LMStudio, TEXT("lmstudio"), TEXT("LM Studio"), TEXT("openai_chat"), TEXT("http://localhost:1234"), TEXT("/v1/chat/completions"), TEXT("bearer"), true, TEXT("/v1/models")),
		};
	}

	/**
	 * Model rows used when the catalog file cannot be read: each cloud provider's default, as in the shipped file
	 * (NodeToCode.LLM.Catalog checks the two agree).
	 */
	TArray<FN2CModelMetadata> FallbackModels()
	{
		TArray<FN2CModelMetadata> Rows;

		FN2CModelMetadata& OpenAI = Rows.Add_GetRef(FN2CModelMetadata(TEXT("gpt-6.1-sol"), TEXT("GPT-6.1 Sol"), EN2CLLMProvider::OpenAI,
			2.0f, 10.0f, 1050000, 128000, true, true, true, 0.0f, false, false, true));
		OpenAI.ProviderId = TEXT("openai");
		OpenAI.LegacyEnumName = TEXT("GPT_6_1_Sol");
		OpenAI.Dialect = TEXT("openai_chat");
		OpenAI.CachedInputCostPer1M = 0.1f;
		OpenAI.StructuredOutputMode = TEXT("json_schema");
		OpenAI.ReasoningParam = TEXT("reasoning_effort");
		OpenAI.ReasoningLevels = { TEXT("low"), TEXT("medium"), TEXT("high"), TEXT("xhigh"), TEXT("max") };
		OpenAI.MaxTokensField = TEXT("max_completion_tokens");
		OpenAI.bIsDefault = true;

		FN2CModelMetadata& Anthropic = Rows.Add_GetRef(FN2CModelMetadata(TEXT("claude-sonnet-5-5"), TEXT("Claude Sonnet 5.5"), EN2CLLMProvider::Anthropic,
			2.0f, 10.0f, 1000000, 128000, true, false, false, 0.0f, false, false, false));
		Anthropic.ProviderId = TEXT("anthropic");
		Anthropic.LegacyEnumName = TEXT("Claude5_5_Sonnet");
		Anthropic.Dialect = TEXT("anthropic_messages");
		Anthropic.CachedInputCostPer1M = 0.1f;
		Anthropic.StructuredOutputMode = TEXT("none");
		Anthropic.ReasoningParam = TEXT("effort");
		Anthropic.ReasoningLevels = { TEXT("low"), TEXT("medium"), TEXT("high"), TEXT("xhigh"), TEXT("max") };
		Anthropic.MaxTokensField = TEXT("max_tokens");
		Anthropic.bIsDefault = true;

		FN2CModelMetadata& Gemini = Rows.Add_GetRef(FN2CModelMetadata(TEXT("gemini-3.8-flash"), TEXT("Gemini 3.8 Flash"), EN2CLLMProvider::Gemini,
			0.75f, 3.75f, 1048576, 65536, true, true, false, 0.0f, false, false, false));
		Gemini.ProviderId = TEXT("gemini");
		Gemini.LegacyEnumName = TEXT("Gemini_3_8_Flash");
		Gemini.Dialect = TEXT("gemini");
		Gemini.CachedInputCostPer1M = 0.075f;
		Gemini.StructuredOutputMode = TEXT("response_schema");
		Gemini.ReasoningParam = TEXT("thinking_level");
		Gemini.ReasoningLevels = { TEXT("low"), TEXT("medium"), TEXT("high") };
		Gemini.MaxTokensField = TEXT("maxOutputTokens");
		Gemini.bIsDefault = true;

		FN2CModelMetadata& DeepSeek = Rows.Add_GetRef(FN2CModelMetadata(TEXT("deepseek-flash"), TEXT("DeepSeek Flash, thinking (high effort)"), EN2CLLMProvider::DeepSeek,
			0.3f, 1.2f, 1000000, 393216, true, false, true, 0.0f, false, false, false).WithReasoningEffort(TEXT("high")));
		DeepSeek.ProviderId = TEXT("deepseek");
		DeepSeek.LegacyEnumName = TEXT("DeepSeek_R1");
		DeepSeek.Dialect = TEXT("deepseek_chat");
		DeepSeek.CachedInputCostPer1M = 0.006f;
		DeepSeek.StructuredOutputMode = TEXT("none");
		DeepSeek.ReasoningParam = TEXT("thinking");
		DeepSeek.ReasoningLevels = { TEXT("none"), TEXT("low"), TEXT("high"), TEXT("max") };
		DeepSeek.MaxTokensField = TEXT("max_tokens");
		DeepSeek.bIsDefault = true;

		return Rows;
	}

	// --- JSON helpers: each returns false (with OutError set) on a missing or mistyped field ---

	bool ReadString(const FJsonObject& Object, const TCHAR* Field, FString& Out, FString& OutError, bool bRequired = true)
	{
		const TSharedPtr<FJsonValue> Value = Object.TryGetField(Field);
		if (!Value.IsValid() || Value->IsNull())
		{
			if (bRequired)
			{
				OutError = FString::Printf(TEXT("missing \"%s\""), Field);
				return false;
			}
			Out.Reset();
			return true;
		}
		if (Value->Type != EJson::String)
		{
			OutError = FString::Printf(TEXT("\"%s\" is not a string"), Field);
			return false;
		}
		Out = Value->AsString();
		if (bRequired && Out.IsEmpty())
		{
			OutError = FString::Printf(TEXT("\"%s\" is empty"), Field);
			return false;
		}
		return true;
	}

	bool ReadNumber(const FJsonObject& Object, const TCHAR* Field, double& Out, FString& OutError, bool bRequired = true)
	{
		const TSharedPtr<FJsonValue> Value = Object.TryGetField(Field);
		if (!Value.IsValid() || Value->IsNull())
		{
			if (bRequired)
			{
				OutError = FString::Printf(TEXT("missing \"%s\""), Field);
				return false;
			}
			Out = 0.0;
			return true;
		}
		if (Value->Type != EJson::Number || Value->AsNumber() < 0.0)
		{
			OutError = FString::Printf(TEXT("\"%s\" is not a number of at least 0"), Field);
			return false;
		}
		Out = Value->AsNumber();
		return true;
	}

	bool ReadBool(const FJsonObject& Object, const TCHAR* Field, bool& Out, FString& OutError, bool bDefault)
	{
		const TSharedPtr<FJsonValue> Value = Object.TryGetField(Field);
		if (!Value.IsValid() || Value->IsNull())
		{
			Out = bDefault;
			return true;
		}
		if (Value->Type != EJson::Boolean)
		{
			OutError = FString::Printf(TEXT("\"%s\" is not true or false"), Field);
			return false;
		}
		Out = Value->AsBool();
		return true;
	}

	bool ReadObject(const FJsonObject& Object, const TCHAR* Field, TSharedPtr<FJsonObject>& Out, FString& OutError)
	{
		const TSharedPtr<FJsonObject>* Found = nullptr;
		if (!Object.TryGetObjectField(Field, Found) || !Found || !Found->IsValid())
		{
			OutError = FString::Printf(TEXT("missing object \"%s\""), Field);
			return false;
		}
		Out = *Found;
		return true;
	}

	bool ReadPositiveInt(const FJsonObject& Object, const TCHAR* Field, int32& Out, FString& OutError)
	{
		double Number = 0.0;
		if (!ReadNumber(Object, Field, Number, OutError))
		{
			return false;
		}
		if (Number < 1.0 || Number > static_cast<double>(MAX_int32) || FMath::Frac(Number) != 0.0)
		{
			OutError = FString::Printf(TEXT("\"%s\" is not a positive whole number"), Field);
			return false;
		}
		Out = static_cast<int32>(Number);
		return true;
	}

	bool ParseProvider(const FJsonObject& Row, FN2CProviderProfile& Out, FString& OutError)
	{
		FString Id, EnumName;
		if (!ReadString(Row, TEXT("id"), Id, OutError) || !ReadString(Row, TEXT("legacy_enum"), EnumName, OutError)
			|| !ReadString(Row, TEXT("display_name"), Out.DisplayName, OutError) || !ReadString(Row, TEXT("dialect"), Out.Dialect, OutError)
			|| !ReadString(Row, TEXT("base_url"), Out.BaseUrl, OutError) || !ReadString(Row, TEXT("path"), Out.Path, OutError, false)
			|| !ReadString(Row, TEXT("auth_style"), Out.AuthStyle, OutError) || !ReadString(Row, TEXT("models_path"), Out.ModelsPath, OutError, false)
			|| !ReadBool(Row, TEXT("is_local"), Out.bIsLocal, OutError, false) || !ReadBool(Row, TEXT("needs_key"), Out.bNeedsKey, OutError, true))
		{
			return false;
		}
		if (!ProviderFromEnumName(EnumName, Out.Provider))
		{
			// A provider needs a service until the dialect adapters land (decision 0004), so it names one
			OutError = FString::Printf(TEXT("\"legacy_enum\" %s names no EN2CLLMProvider value"), *EnumName);
			return false;
		}
		Out.Id = FName(*Id);
		return true;
	}

	bool ParseModel(const FJsonObject& Row, const TArray<FN2CProviderProfile>& Providers, FN2CModelMetadata& Out, FString& OutError)
	{
		FString ProviderId;
		if (!ReadString(Row, TEXT("id"), Out.ModelId, OutError) || !ReadString(Row, TEXT("provider"), ProviderId, OutError)
			|| !ReadString(Row, TEXT("display_name"), Out.DisplayName, OutError)
			|| !ReadString(Row, TEXT("legacy_enum"), Out.LegacyEnumName, OutError, false)
			|| !ReadString(Row, TEXT("dialect"), Out.Dialect, OutError, false)
			|| !ReadPositiveInt(Row, TEXT("context_window"), Out.ContextWindowSize, OutError)
			|| !ReadPositiveInt(Row, TEXT("max_output"), Out.MaxOutputTokens, OutError)
			|| !ReadBool(Row, TEXT("is_default"), Out.bIsDefault, OutError, false))
		{
			return false;
		}
		const FN2CProviderProfile* Provider = Providers.FindByPredicate([&ProviderId](const FN2CProviderProfile& P) { return P.Id == FName(*ProviderId); });
		if (!Provider)
		{
			OutError = FString::Printf(TEXT("\"provider\" %s is not a provider row"), *ProviderId);
			return false;
		}
		Out.ProviderId = Provider->Id;
		Out.Provider = Provider->Provider;
		if (Out.Dialect.IsEmpty())
		{
			Out.Dialect = Provider->Dialect;
		}
		if (Out.MaxOutputTokens > Out.ContextWindowSize)
		{
			OutError = TEXT("\"max_output\" is larger than \"context_window\"");
			return false;
		}

		TSharedPtr<FJsonObject> Pricing, Caps, Lifecycle;
		double Input = 0.0, Output = 0.0, Cached = 0.0, Temperature = 0.0;
		if (!ReadObject(Row, TEXT("pricing"), Pricing, OutError) || !ReadNumber(*Pricing, TEXT("input"), Input, OutError)
			|| !ReadNumber(*Pricing, TEXT("output"), Output, OutError) || !ReadNumber(*Pricing, TEXT("cached_input"), Cached, OutError, false))
		{
			return false;
		}
		Out.InputCostPer1M = static_cast<float>(Input);
		Out.OutputCostPer1M = static_cast<float>(Output);
		Out.CachedInputCostPer1M = static_cast<float>(Cached);

		if (!ReadObject(Row, TEXT("caps"), Caps, OutError)
			|| !ReadBool(*Caps, TEXT("system_prompt"), Out.bSupportsSystemPrompts, OutError, true)
			|| !ReadBool(*Caps, TEXT("temperature"), Out.bAcceptsSamplingParams, OutError, false)
			|| !ReadNumber(*Caps, TEXT("default_temperature"), Temperature, OutError, false)
			|| !ReadString(*Caps, TEXT("structured_output_mode"), Out.StructuredOutputMode, OutError)
			|| !ReadString(*Caps, TEXT("reasoning_param"), Out.ReasoningParam, OutError, false)
			|| !ReadString(*Caps, TEXT("reasoning_effort"), Out.ReasoningEffort, OutError, false)
			|| !ReadBool(*Caps, TEXT("thinking_model"), Out.bIsReasoningModel, OutError, false)
			|| !ReadString(*Caps, TEXT("max_tokens_field"), Out.MaxTokensField, OutError)
			|| !ReadBool(*Caps, TEXT("responses_only"), Out.bRequiresResponsesAPI, OutError, false)
			|| !ReadBool(*Caps, TEXT("streaming"), Out.bSupportsStreaming, OutError, true))
		{
			return false;
		}
		Out.DefaultTemperature = static_cast<float>(Temperature);
		Out.bSupportsStructuredOutput = Out.StructuredOutputMode != TEXT("none");
		Out.bUsesMaxCompletionTokens = Out.MaxTokensField == TEXT("max_completion_tokens");
		Out.ReasoningLevels.Reset();
		const TArray<TSharedPtr<FJsonValue>>* Levels = nullptr;
		if (Caps->TryGetArrayField(TEXT("reasoning_levels"), Levels) && Levels)
		{
			for (const TSharedPtr<FJsonValue>& Level : *Levels)
			{
				if (!Level.IsValid() || Level->Type != EJson::String)
				{
					OutError = TEXT("\"reasoning_levels\" holds a value that is not a string");
					return false;
				}
				Out.ReasoningLevels.Add(Level->AsString());
			}
		}
		if (!Out.ReasoningEffort.IsEmpty() && Out.ReasoningLevels.Num() > 0 && !Out.ReasoningLevels.Contains(Out.ReasoningEffort))
		{
			OutError = FString::Printf(TEXT("\"reasoning_effort\" %s is not one of its \"reasoning_levels\""), *Out.ReasoningEffort);
			return false;
		}

		FString Status;
		if (!ReadObject(Row, TEXT("lifecycle"), Lifecycle, OutError) || !ReadString(*Lifecycle, TEXT("status"), Status, OutError)
			|| !ReadString(*Lifecycle, TEXT("shutdown_date"), Out.ShutdownDate, OutError, false))
		{
			return false;
		}
		if (Status != TEXT("active") && Status != TEXT("deprecated") && Status != TEXT("retired"))
		{
			OutError = FString::Printf(TEXT("lifecycle \"status\" %s is not active, deprecated or retired"), *Status);
			return false;
		}
		FDateTime Parsed;
		if (!Out.ShutdownDate.IsEmpty() && (Out.ShutdownDate.Len() != 10 || !FDateTime::ParseIso8601(*Out.ShutdownDate, Parsed)))
		{
			OutError = FString::Printf(TEXT("\"shutdown_date\" %s is not YYYY-MM-DD"), *Out.ShutdownDate);
			return false;
		}
		if (Status == TEXT("deprecated") && Out.ShutdownDate.IsEmpty())
		{
			OutError = TEXT("a deprecated model needs a \"shutdown_date\"");
			return false;
		}
		Out.bIsDeprecated = Status != TEXT("active");
		Out.bIsRetired = Status == TEXT("retired");
		return true;
	}

	/** Where a row came from, for error messages */
	FString Describe(const FN2CModelMetadata& Row)
	{
		return Row.ReasoningEffort.IsEmpty()
			? FString::Printf(TEXT("%s/%s"), *Row.ProviderId.ToString(), *Row.ModelId)
			: FString::Printf(TEXT("%s/%s (reasoning_effort %s)"), *Row.ProviderId.ToString(), *Row.ModelId, *Row.ReasoningEffort);
	}
}

FN2CModelCatalog& FN2CModelCatalog::Get()
{
	static FN2CModelCatalog Instance;
	if (!Instance.bIsLoaded)
	{
		Instance.ReloadBundled();
	}
	return Instance;
}

FString FN2CModelCatalog::GetBundledPath()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NodeToCode"));
	return Plugin.IsValid()
		? FPaths::ConvertRelativePathToFull(FPaths::Combine(Plugin->GetContentDir(), TEXT("Data"), TEXT("N2CModelCatalog.json")))
		: FString();
}

FName FN2CModelCatalog::ProviderIdFor(EN2CLLMProvider Provider)
{
	for (const N2CModelCatalogData::FProviderName& Entry : N2CModelCatalogData::ProviderNames)
	{
		if (Entry.Provider == Provider)
		{
			return FName(Entry.Id);
		}
	}
	return NAME_None;
}

bool FN2CModelCatalog::LoadFromFile(const FString& Path)
{
	FString JsonText;
	if (Path.IsEmpty() || !FFileHelper::LoadFileToString(JsonText, *Path))
	{
		FN2CLogger::Get().LogError(FString::Printf(
			TEXT("Model catalog %s could not be read; only each provider's default model is available. Reinstall the plugin to restore Content/Data/N2CModelCatalog.json."),
			Path.IsEmpty() ? TEXT("(NodeToCode plugin not found)") : *Path), TEXT("ModelCatalog"));
		Install(N2CModelCatalogData::FallbackProviders(), N2CModelCatalogData::FallbackModels(), FString(), true);
		return false;
	}
	return LoadFromString(JsonText, Path);
}

bool FN2CModelCatalog::LoadFromString(const FString& JsonText, const FString& SourceLabel)
{
	TArray<FN2CProviderProfile> ParsedProviders;
	TArray<FN2CModelMetadata> ParsedModels;
	FString Error;
	if (!Parse(JsonText, ParsedProviders, ParsedModels, Error))
	{
		FN2CLogger::Get().LogError(FString::Printf(
			TEXT("Model catalog %s is malformed (%s); only each provider's default model is available. Reinstall the plugin to restore Content/Data/N2CModelCatalog.json."),
			*SourceLabel, *Error), TEXT("ModelCatalog"));
		Install(N2CModelCatalogData::FallbackProviders(), N2CModelCatalogData::FallbackModels(), FString(), true);
		return false;
	}
	Install(MoveTemp(ParsedProviders), MoveTemp(ParsedModels), SourceLabel, false);
	FN2CLogger::Get().Log(FString::Printf(TEXT("Model catalog loaded from %s: %d providers, %d models"),
		*SourceLabel, ProviderRows.Num(), Models.Num()), EN2CLogSeverity::Debug, TEXT("ModelCatalog"));
	return true;
}

bool FN2CModelCatalog::Parse(const FString& JsonText, TArray<FN2CProviderProfile>& OutProviders, TArray<FN2CModelMetadata>& OutModels, FString& OutError)
{
	using namespace N2CModelCatalogData;

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = FString::Printf(TEXT("not a JSON object: %s"), *Reader->GetErrorMessage());
		return false;
	}

	double Version = 0.0;
	if (!ReadNumber(*Root, TEXT("schema_version"), Version, OutError))
	{
		return false;
	}
	if (static_cast<int32>(Version) != SchemaVersion)
	{
		OutError = FString::Printf(TEXT("schema_version %g, this build reads %d"), Version, SchemaVersion);
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* ProviderValues = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* ModelValues = nullptr;
	if (!Root->TryGetArrayField(TEXT("providers"), ProviderValues) || !ProviderValues || ProviderValues->Num() == 0)
	{
		OutError = TEXT("no \"providers\" array");
		return false;
	}
	if (!Root->TryGetArrayField(TEXT("models"), ModelValues) || !ModelValues)
	{
		OutError = TEXT("no \"models\" array");
		return false;
	}

	for (int32 Index = 0; Index < ProviderValues->Num(); ++Index)
	{
		const TSharedPtr<FJsonValue>& Value = (*ProviderValues)[Index];
		FN2CProviderProfile Profile;
		FString RowError;
		if (!Value.IsValid() || Value->Type != EJson::Object || !ParseProvider(*Value->AsObject(), Profile, RowError))
		{
			OutError = FString::Printf(TEXT("providers[%d]: %s"), Index, RowError.IsEmpty() ? TEXT("not an object") : *RowError);
			return false;
		}
		if (OutProviders.ContainsByPredicate([&Profile](const FN2CProviderProfile& P) { return P.Id == Profile.Id; }))
		{
			OutError = FString::Printf(TEXT("providers[%d]: id %s appears twice"), Index, *Profile.Id.ToString());
			return false;
		}
		OutProviders.Add(MoveTemp(Profile));
	}

	for (int32 Index = 0; Index < ModelValues->Num(); ++Index)
	{
		const TSharedPtr<FJsonValue>& Value = (*ModelValues)[Index];
		FN2CModelMetadata Row;
		FString RowError;
		if (!Value.IsValid() || Value->Type != EJson::Object || !ParseModel(*Value->AsObject(), OutProviders, Row, RowError))
		{
			OutError = FString::Printf(TEXT("models[%d]: %s"), Index, RowError.IsEmpty() ? TEXT("not an object") : *RowError);
			return false;
		}
		for (const FN2CModelMetadata& Other : OutModels)
		{
			if (Other.ProviderId == Row.ProviderId && Other.ModelId == Row.ModelId && Other.ReasoningEffort == Row.ReasoningEffort)
			{
				OutError = FString::Printf(TEXT("models[%d]: %s appears twice"), Index, *Describe(Row));
				return false;
			}
			if (!Row.LegacyEnumName.IsEmpty() && Other.ProviderId == Row.ProviderId && Other.LegacyEnumName == Row.LegacyEnumName)
			{
				OutError = FString::Printf(TEXT("models[%d]: legacy_enum %s appears twice"), Index, *Row.LegacyEnumName);
				return false;
			}
		}
		OutModels.Add(MoveTemp(Row));
	}

	// One default per provider that has models, and it is a model NodeToCode can keep offering
	for (const FN2CProviderProfile& Profile : OutProviders)
	{
		int32 Count = 0;
		int32 Defaults = 0;
		for (const FN2CModelMetadata& Row : OutModels)
		{
			if (Row.ProviderId == Profile.Id)
			{
				++Count;
				if (Row.bIsDefault)
				{
					++Defaults;
					if (Row.bIsDeprecated)
					{
						OutError = FString::Printf(TEXT("default model %s is deprecated"), *Describe(Row));
						return false;
					}
				}
			}
		}
		if (Count > 0 && Defaults != 1)
		{
			OutError = FString::Printf(TEXT("provider %s has %d default models, not 1"), *Profile.Id.ToString(), Defaults);
			return false;
		}
	}
	return true;
}

void FN2CModelCatalog::Install(TArray<FN2CProviderProfile>&& InProviders, TArray<FN2CModelMetadata>&& InModels, const FString& InSourcePath, bool bInFallback)
{
	using namespace N2CModelCatalogData;

	// Retired enumerators the file does not describe itself
	for (const FRetiredRow& Retired : RetiredRows)
	{
		const FName ProviderId = ProviderIdFor(Retired.Provider);
		const bool bInFile = InModels.ContainsByPredicate([&](const FN2CModelMetadata& Row)
		{
			return Row.ProviderId == ProviderId && Row.LegacyEnumName == Retired.EnumName;
		});
		if (bInFile)
		{
			continue;
		}
		FN2CModelMetadata& Row = InModels.Add_GetRef(FN2CModelMetadata(Retired.ModelId, Retired.DisplayName, Retired.Provider, 0.0f, 0.0f));
		Row.Retired(Retired.ShutdownDate);
		Row.ProviderId = ProviderId;
		Row.LegacyEnumName = Retired.EnumName;
		Row.bSupportsSystemPrompts = Retired.bSystemPrompts;
		Row.bSupportsStructuredOutput = Retired.bStructuredOutput;
		Row.StructuredOutputMode = !Retired.bStructuredOutput ? TEXT("none")
			: (Retired.Provider == EN2CLLMProvider::Gemini ? TEXT("response_schema") : TEXT("json_schema"));
		if (const FN2CProviderProfile* Profile = InProviders.FindByPredicate([ProviderId](const FN2CProviderProfile& P) { return P.Id == ProviderId; }))
		{
			Row.Dialect = Profile->Dialect;
		}
	}

	ProviderRows = MoveTemp(InProviders);
	Models = MoveTemp(InModels);
	SourcePath = InSourcePath;
	bIsFallback = bInFallback;
	bIsLoaded = true;
	++Generation;
}

const FN2CProviderProfile* FN2CModelCatalog::FindProvider(FName ProviderId) const
{
	return ProviderRows.FindByPredicate([ProviderId](const FN2CProviderProfile& P) { return P.Id == ProviderId; });
}

TArray<const FN2CModelMetadata*> FN2CModelCatalog::List(FName ProviderId, bool bIncludeRetired) const
{
	TArray<const FN2CModelMetadata*> Result;
	for (const FN2CModelMetadata& Row : Models)
	{
		if (Row.ProviderId == ProviderId && (bIncludeRetired || !Row.bIsRetired))
		{
			Result.Add(&Row);
		}
	}
	return Result;
}

const FN2CModelMetadata* FN2CModelCatalog::Find(FName ProviderId, const FString& ModelId) const
{
	const FN2CModelMetadata* RetiredMatch = nullptr;
	for (const FN2CModelMetadata& Row : Models)
	{
		if (Row.ProviderId == ProviderId && Row.ModelId == ModelId)
		{
			if (!Row.bIsRetired)
			{
				return &Row;
			}
			RetiredMatch = RetiredMatch ? RetiredMatch : &Row;
		}
	}
	return RetiredMatch;
}

const FN2CModelMetadata* FN2CModelCatalog::Find(FName ProviderId, const FString& ModelId, const FString& ReasoningEffort) const
{
	if (!ReasoningEffort.IsEmpty())
	{
		for (const FN2CModelMetadata& Row : Models)
		{
			if (Row.ProviderId == ProviderId && Row.ModelId == ModelId && Row.ReasoningEffort == ReasoningEffort && !Row.bIsRetired)
			{
				return &Row;
			}
		}
	}
	return Find(ProviderId, ModelId);
}

const FN2CModelMetadata* FN2CModelCatalog::FindById(const FString& ModelId, const FString& ReasoningEffort) const
{
	for (const FN2CModelMetadata& Row : Models)
	{
		if (Row.ModelId == ModelId)
		{
			if (const FN2CModelMetadata* Found = Find(Row.ProviderId, ModelId, ReasoningEffort))
			{
				return Found;
			}
		}
	}
	return nullptr;
}

const FN2CModelMetadata* FN2CModelCatalog::FindByLegacyEnum(FName ProviderId, const FString& EnumName) const
{
	return Models.FindByPredicate([ProviderId, &EnumName](const FN2CModelMetadata& Row)
	{
		return Row.ProviderId == ProviderId && !EnumName.IsEmpty() && Row.LegacyEnumName == EnumName;
	});
}

const FN2CModelMetadata* FN2CModelCatalog::FindDefault(FName ProviderId) const
{
	return Models.FindByPredicate([ProviderId](const FN2CModelMetadata& Row)
	{
		return Row.ProviderId == ProviderId && Row.bIsDefault;
	});
}
