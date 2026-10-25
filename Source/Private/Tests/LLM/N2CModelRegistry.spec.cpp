// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CSettings.h"
#include "LLM/N2CLLMModelRegistry.h"
#include "LLM/N2CLLMModels.h"
#include "LLM/Providers/N2COpenAIService.h"
#include "MCP/Tools/Implementations/Translation/N2CMcpGetAvailableLLMProvidersTool.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

/**
 * Checks the static model registry against the rules the settings and services rely on: every model still offered
 * has a unique ID and real limits, retired models are hidden everywhere, each provider default is a live model,
 * and a saved retired selection loads as the current default.
 */
BEGIN_DEFINE_SPEC(FN2CModelRegistrySpec, "NodeToCode.LLM.Registry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	/**
	 * Checks the per-entry rules for one provider's map; adds offered requests to SeenIds to catch duplicates across
	 * providers. A request is the model ID plus the thinking control (entries may share an ID when they differ in
	 * ReasoningEffort), and each must resolve back to its own entry, so every request's thinking mode is known.
	 */
	template <typename TEnum>
	void CheckEntries(const TMap<TEnum, FN2CModelMetadata>& Models, EN2CLLMProvider Provider, TSet<FString>& SeenIds)
	{
		const UEnum* Enum = StaticEnum<TEnum>();
		if (!TestNotNull(TEXT("model enum"), Enum)) { return; }

		// Every enumerator (except _MAX) has a registry entry, so a saved value never falls through to a fallback ID
		for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
		{
			const TEnum Value = static_cast<TEnum>(Enum->GetValueByIndex(Index));
			const FString Name = Enum->GetNameStringByIndex(Index);
			const FN2CModelMetadata* Meta = Models.Find(Value);
			if (!TestNotNull(FString::Printf(TEXT("%s has a registry entry"), *Name), Meta)) { continue; }

			TestEqual(FString::Printf(TEXT("%s provider"), *Name), Meta->Provider, Provider);
			TestEqual(FString::Printf(TEXT("%s is UMETA(Hidden) exactly when retired"), *Name),
				Enum->HasMetaData(TEXT("Hidden"), Index), Meta->bIsRetired);
			if (Meta->bIsRetired)
			{
				TestTrue(FString::Printf(TEXT("%s retired implies deprecated"), *Name), Meta->bIsDeprecated);
			}
			if (Meta->bIsDeprecated && !Meta->bIsRetired)
			{
				TestFalse(FString::Printf(TEXT("%s deprecated but live has a shutdown date"), *Name), Meta->ShutdownDate.IsEmpty());
				TestTrue(FString::Printf(TEXT("%s display name shows its shutdown date"), *Name),
					Enum->GetDisplayNameTextByIndex(Index).ToString().Contains(Meta->ShutdownDate));
			}
			if (!Meta->ShutdownDate.IsEmpty())
			{
				FDateTime Parsed;
				TestTrue(FString::Printf(TEXT("%s shutdown date %s is YYYY-MM-DD"), *Name, *Meta->ShutdownDate),
					Meta->ShutdownDate.Len() == 10 && FDateTime::ParseIso8601(*Meta->ShutdownDate, Parsed));
			}

			if (!Meta->bIsDeprecated)
			{
				TestFalse(FString::Printf(TEXT("%s has a model ID"), *Name), Meta->ModelId.IsEmpty());
				const FString RequestKey = FString::Printf(TEXT("%s|%s"), *Meta->ModelId, *Meta->ReasoningEffort);
				TestFalse(FString::Printf(TEXT("%s model ID %s with thinking '%s' is unique"), *Name, *Meta->ModelId, *Meta->ReasoningEffort),
					SeenIds.Contains(RequestKey));
				TestTrue(FString::Printf(TEXT("%s resolves from its model ID and thinking control"), *Name),
					FN2CLLMModelRegistry::Get().GetModelMetadataById(Meta->ModelId, Meta->ReasoningEffort) == Meta);
				TestTrue(FString::Printf(TEXT("%s has a context window"), *Name), Meta->ContextWindowSize > 0);
				TestTrue(FString::Printf(TEXT("%s has a max output that fits its context window"), *Name),
					Meta->MaxOutputTokens > 0 && Meta->MaxOutputTokens <= Meta->ContextWindowSize);
				SeenIds.Add(RequestKey);
			}
		}
	}

	/** The default maps to a live entry, and the fallback ID for an unknown enum value is the default's ID */
	template <typename TEnum>
	void CheckDefault(TEnum Default, const TCHAR* What)
	{
		const FN2CLLMModelRegistry& Registry = FN2CLLMModelRegistry::Get();
		const FN2CModelMetadata* Meta = Registry.GetModelMetadata(Default);
		if (!TestNotNull(FString::Printf(TEXT("%s default has a registry entry"), What), Meta)) { return; }
		TestFalse(FString::Printf(TEXT("%s default %s is not deprecated"), What, *Meta->ModelId), Meta->bIsDeprecated);
		TestEqual(FString::Printf(TEXT("%s fallback ID matches the default"), What),
			Registry.GetModelId(static_cast<TEnum>(250)), Meta->ModelId);
	}

	/** Model IDs the MCP tool offers for a provider */
	TArray<FString> OfferedIds(EN2CLLMProvider Provider)
	{
		TArray<FString> Ids;
		FN2CMcpGetAvailableLLMProvidersTool Tool;
		for (const TSharedPtr<FJsonValue>& Value : Tool.GetAvailableModels(Provider))
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (Value.IsValid() && Value->TryGetObject(Object) && Object && (*Object).IsValid())
			{
				Ids.Add((*Object)->GetStringField(TEXT("id")));
			}
		}
		return Ids;
	}

	/** A settings object loaded from an ini file with the given [/Script/NodeToCode.N2CSettings] lines */
	UN2CSettings* LoadSettingsFromIni(const FString& Lines, uint32 PropagationFlags = UE::LCPF_None)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation")
			/ FString::Printf(TEXT("N2CRegistrySpec_%s.ini"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
		const FString Ini = FString::Printf(TEXT("[%s]\n%s\n"), *UN2CSettings::StaticClass()->GetPathName(), *Lines);
		if (!TestTrue(TEXT("test ini written"), FFileHelper::SaveStringToFile(Ini, *Path))) { return nullptr; }

		GConfig->LoadFile(Path);
		UN2CSettings* Settings = NewObject<UN2CSettings>(GetTransientPackage());
		Settings->LoadConfig(nullptr, *Path, PropagationFlags);
		GConfig->UnloadFile(Path);
		IFileManager::Get().Delete(*Path);
		return Settings;
	}

END_DEFINE_SPEC(FN2CModelRegistrySpec)

void FN2CModelRegistrySpec::Define()
{
	Describe(TEXT("Entries"), [this]()
	{
		It(TEXT("give every offered model a unique ID, a context window and a max output"), [this]()
		{
			const FN2CLLMModelRegistry& Registry = FN2CLLMModelRegistry::Get();
			TSet<FString> SeenIds;
			CheckEntries(Registry.GetAllOpenAIModels(), EN2CLLMProvider::OpenAI, SeenIds);
			CheckEntries(Registry.GetAllAnthropicModels(), EN2CLLMProvider::Anthropic, SeenIds);
			CheckEntries(Registry.GetAllGeminiModels(), EN2CLLMProvider::Gemini, SeenIds);
			CheckEntries(Registry.GetAllDeepSeekModels(), EN2CLLMProvider::DeepSeek, SeenIds);
		});

		It(TEXT("send no sampling parameters to the new Claude GPT-6 and Gemini models"), [this]()
		{
			const FN2CLLMModelRegistry& Registry = FN2CLLMModelRegistry::Get();
			for (const TCHAR* Id : { TEXT("claude-sonnet-5-5"), TEXT("claude-opus-5-5"), TEXT("claude-haiku-5-5"),
				TEXT("gpt-6.1-sol"), TEXT("gpt-6-astra"), TEXT("gpt-6-luna"),
				TEXT("gemini-3.8-flash"), TEXT("gemini-4-argon"), TEXT("gemini-3.5-flash-lite") })
			{
				const FN2CModelMetadata* Meta = Registry.GetModelMetadataById(Id);
				if (!TestNotNull(FString::Printf(TEXT("%s is registered"), Id), Meta)) { continue; }
				TestFalse(FString::Printf(TEXT("%s takes no sampling parameters"), Id), Meta->bAcceptsSamplingParams);
				TestEqual(FString::Printf(TEXT("%s uses max_completion_tokens only on OpenAI"), Id),
					Meta->bUsesMaxCompletionTokens, Meta->Provider == EN2CLLMProvider::OpenAI);
				TestFalse(FString::Printf(TEXT("%s is not deprecated"), Id), Meta->bIsDeprecated);
			}
		});
	});

	Describe(TEXT("Defaults"), [this]()
	{
		It(TEXT("map each provider default to a model that is not deprecated"), [this]()
		{
			CheckDefault(N2CDefaultModels::OpenAI, TEXT("OpenAI"));
			CheckDefault(N2CDefaultModels::Anthropic, TEXT("Anthropic"));
			CheckDefault(N2CDefaultModels::Gemini, TEXT("Gemini"));
			CheckDefault(N2CDefaultModels::DeepSeek, TEXT("DeepSeek"));
		});

		It(TEXT("are the re-verified model IDs"), [this]()
		{
			// Checked on 2026-10-22 against each provider's models and deprecations pages
			const FN2CLLMModelRegistry& Registry = FN2CLLMModelRegistry::Get();
			TestEqual(TEXT("OpenAI default"), Registry.GetModelId(N2CDefaultModels::OpenAI), FString(TEXT("gpt-6.1-sol")));
			TestEqual(TEXT("Anthropic default"), Registry.GetModelId(N2CDefaultModels::Anthropic), FString(TEXT("claude-sonnet-5-5")));
			TestEqual(TEXT("Gemini default"), Registry.GetModelId(N2CDefaultModels::Gemini), FString(TEXT("gemini-3.8-flash")));
			// Checked on 2026-10-22 against api-docs.deepseek.com/api/create-chat-completion (deepseek-flash, deepseek-v4-pro)
			TestEqual(TEXT("DeepSeek default"), Registry.GetModelId(N2CDefaultModels::DeepSeek), FString(TEXT("deepseek-flash")));
		});
	});

	Describe(TEXT("DeepSeek"), [this]()
	{
		It(TEXT("maps every entry to a current model ID with a thinking control, a 1M context and current prices"), [this]()
		{
			const FN2CLLMModelRegistry& Registry = FN2CLLMModelRegistry::Get();
			struct FExpected { EN2CDeepSeekModel Model; const TCHAR* Id; const TCHAR* Effort; bool bJson; };
			for (const FExpected& E : {
				FExpected{ EN2CDeepSeekModel::DeepSeek_R1, TEXT("deepseek-flash"), TEXT("high"), false },
				FExpected{ EN2CDeepSeekModel::DeepSeek_V3, TEXT("deepseek-flash"), TEXT("none"), true },
				FExpected{ EN2CDeepSeekModel::DeepSeek_V4_Pro, TEXT("deepseek-v4-pro"), TEXT("high"), false } })
			{
				const FN2CModelMetadata* Meta = Registry.GetModelMetadata(E.Model);
				if (!TestNotNull(FString::Printf(TEXT("%s registered"), E.Id), Meta)) { continue; }
				TestEqual(FString::Printf(TEXT("%s/%s model ID"), E.Id, E.Effort), Meta->ModelId, FString(E.Id));
				TestEqual(FString::Printf(TEXT("%s/%s thinking"), E.Id, E.Effort), Meta->ReasoningEffort, FString(E.Effort));
				TestEqual(FString::Printf(TEXT("%s/%s JSON output"), E.Id, E.Effort), Meta->bSupportsStructuredOutput, E.bJson);
				TestEqual(FString::Printf(TEXT("%s/%s sampling only without thinking"), E.Id, E.Effort), Meta->bAcceptsSamplingParams, !Meta->IsThinkingEnabled());
				TestEqual(FString::Printf(TEXT("%s/%s 1M context"), E.Id, E.Effort), Meta->ContextWindowSize, 1000000);
				TestEqual(FString::Printf(TEXT("%s/%s 384K max output"), E.Id, E.Effort), Meta->MaxOutputTokens, 393216);
				TestFalse(FString::Printf(TEXT("%s/%s not deprecated"), E.Id, E.Effort), Meta->bIsDeprecated);
			}
			// Peak-hour cache-miss prices from api-docs.deepseek.com/quick_start/pricing on 2026-10-22
			TestEqual(TEXT("deepseek-flash input price"), Registry.GetDeepSeekPricing(EN2CDeepSeekModel::DeepSeek_V3).InputCost, 0.3f);
			TestEqual(TEXT("deepseek-flash output price"), Registry.GetDeepSeekPricing(EN2CDeepSeekModel::DeepSeek_R1).OutputCost, 1.2f);
			TestEqual(TEXT("deepseek-v4-pro input price"), Registry.GetDeepSeekPricing(EN2CDeepSeekModel::DeepSeek_V4_Pro).InputCost, 1.32f);
			TestEqual(TEXT("deepseek-v4-pro output price"), Registry.GetDeepSeekPricing(EN2CDeepSeekModel::DeepSeek_V4_Pro).OutputCost, 3.96f);

			// The discontinued IDs are gone from the registry
			TestNull(TEXT("deepseek-chat not registered"), Registry.GetModelMetadataById(TEXT("deepseek-chat")));
			TestNull(TEXT("deepseek-reasoner not registered"), Registry.GetModelMetadataById(TEXT("deepseek-reasoner")));
		});

		It(TEXT("resolves a bare deepseek-flash to the API default, thinking at high effort"), [this]()
		{
			const FN2CLLMModelRegistry& Registry = FN2CLLMModelRegistry::Get();
			TestTrue(TEXT("ID alone gives the R1 entry"), Registry.GetModelMetadataById(TEXT("deepseek-flash")) == Registry.GetModelMetadata(EN2CDeepSeekModel::DeepSeek_R1));
			TestTrue(TEXT("ID with none gives the V3 entry"), Registry.GetModelMetadataById(TEXT("deepseek-flash"), TEXT("none")) == Registry.GetModelMetadata(EN2CDeepSeekModel::DeepSeek_V3));
			TestTrue(TEXT("unknown effort falls back to the ID's entry"), Registry.GetModelMetadataById(TEXT("deepseek-flash"), TEXT("max")) == Registry.GetModelMetadata(EN2CDeepSeekModel::DeepSeek_R1));
		});

		It(TEXT("fills the request model and thinking from the selection, and leaves an override's thinking to its entry"), [this]()
		{
			UN2CSettings* Settings = NewObject<UN2CSettings>(GetTransientPackage());
			FN2CLLMConfig Config;
			Settings->DeepSeekModelId = TEXT("deepseek-flash");
			Settings->DeepSeekReasoningEffort = TEXT("none");
			Settings->DeepSeekCustomModelId.Reset();
			Settings->ResolveRequestModel(EN2CLLMProvider::DeepSeek, FString(), Config);
			TestEqual(TEXT("V3 selected: model"), Config.Model, FString(TEXT("deepseek-flash")));
			TestEqual(TEXT("V3 selected: thinking off"), Config.ReasoningEffort, FString(TEXT("none")));

			Settings->ResolveRequestModel(EN2CLLMProvider::DeepSeek, TEXT("deepseek-flash"), Config);
			TestEqual(TEXT("override naming the selection keeps its thinking"), Config.ReasoningEffort, FString(TEXT("none")));

			Settings->ResolveRequestModel(EN2CLLMProvider::DeepSeek, TEXT("deepseek-v4-pro"), Config);
			TestEqual(TEXT("other override: model"), Config.Model, FString(TEXT("deepseek-v4-pro")));
			TestTrue(TEXT("other override: thinking left to the registry"), Config.ReasoningEffort.IsEmpty());

			Settings->ResolveRequestModel(EN2CLLMProvider::OpenAI, FString(), Config);
			TestTrue(TEXT("OpenAI request carries no thinking control"), Config.ReasoningEffort.IsEmpty());
		});
	});

	Describe(TEXT("Migration"), [this]()
	{
		It(TEXT("moves a selected model ID the provider retired to the current default"), [this]()
		{
			UN2CSettings* Settings = LoadSettingsFromIni(TEXT("SettingsVersion=4\n")
				TEXT("AnthropicModelId=claude-3-7-sonnet-20250219\nGeminiModelId=gemini-2.5-flash-preview-05-20\nOpenAIModelId=o4-mini"));
			if (!TestNotNull(TEXT("settings loaded"), Settings)) { return; }

			// A string ID loads as saved; the catalog's retired rows say it no longer works
			TestEqual(TEXT("retired Anthropic ID loads as saved"), Settings->AnthropicModelId, FString(TEXT("claude-3-7-sonnet-20250219")));

			AddExpectedMessage(TEXT("no longer served by its provider"), EAutomationExpectedMessageFlags::Contains, 1);
			TestEqual(TEXT("three selections migrated"), Settings->MigrateRetiredModelSelections(), 3);
			TestEqual(TEXT("Anthropic moved to the default"), Settings->AnthropicModelId, FString(N2CDefaultModelIds::Anthropic));
			TestEqual(TEXT("Gemini moved to the default"), Settings->GeminiModelId, FString(N2CDefaultModelIds::Gemini));
			TestEqual(TEXT("OpenAI moved to the default"), Settings->OpenAIModelId, FString(N2CDefaultModelIds::OpenAI));
			TestEqual(TEXT("active Anthropic model ID"), Settings->GetActiveModelForProvider(EN2CLLMProvider::Anthropic), FString(TEXT("claude-sonnet-5-5")));
		});

		It(TEXT("migrates when the config is reloaded as the plugin config reaches the settings at startup"), [this]()
		{
			// Reloading fires PostReloadConfig, which must run the migration without an explicit call
			AddExpectedMessage(TEXT("no longer served by its provider"), EAutomationExpectedMessageFlags::Contains, 1);
			UN2CSettings* Settings = LoadSettingsFromIni(
				TEXT("SettingsVersion=4\nAnthropicModelId=claude-sonnet-4-20250514\nGeminiModelId=gemini-3-pro-preview"), UE::LCPF_ReloadingConfigData);
			if (!TestNotNull(TEXT("settings loaded"), Settings)) { return; }
			TestEqual(TEXT("Anthropic moved to the default on reload"), Settings->AnthropicModelId, FString(N2CDefaultModelIds::Anthropic));
			TestEqual(TEXT("Gemini moved to the default on reload"), Settings->GeminiModelId, FString(N2CDefaultModelIds::Gemini));
		});

		It(TEXT("leaves live selections alone, deprecated and unlisted ones included"), [this]()
		{
			UN2CSettings* Settings = LoadSettingsFromIni(TEXT("SettingsVersion=4\n")
				TEXT("AnthropicModelId=claude-sonnet-4-5-20250929\nGeminiModelId=gemini-3-flash-preview\nOpenAIModelId=o3\n")
				TEXT("DeepSeekModelId=deepseek-flash\nDeepSeekReasoningEffort=none\nGeminiCustomModelId=gemini-9-not-listed"));
			if (!TestNotNull(TEXT("settings loaded"), Settings)) { return; }
			TestEqual(TEXT("nothing migrated"), Settings->MigrateRetiredModelSelections(), 0);
			TestEqual(TEXT("Sonnet 4.5 kept until it retires"), Settings->AnthropicModelId, FString(TEXT("claude-sonnet-4-5-20250929")));
			TestEqual(TEXT("o3 kept until it shuts down"), Settings->OpenAIModelId, FString(TEXT("o3")));
			TestEqual(TEXT("DeepSeek thinking off kept"), Settings->GetActiveReasoningEffortForProvider(EN2CLLMProvider::DeepSeek), FString(TEXT("none")));
			TestEqual(TEXT("a custom Gemini ID is what requests send"), Settings->GetActiveModelForProvider(EN2CLLMProvider::Gemini), FString(TEXT("gemini-9-not-listed")));
			TestNull(TEXT("and has no catalog row"), Settings->GetActiveModelMetadataForProvider(EN2CLLMProvider::Gemini));
		});

		It(TEXT("sends deepseek-flash with the thinking mode Thinking names, or the entry's own when it is empty"), [this]()
		{
			for (const TCHAR* Effort : { TEXT("high"), TEXT("none"), TEXT("") })
			{
				UN2CSettings* Settings = LoadSettingsFromIni(FString::Printf(
					TEXT("SettingsVersion=4\nProvider=DeepSeek\nDeepSeekModelId=deepseek-flash\nDeepSeekReasoningEffort=%s"), Effort));
				if (!TestNotNull(TEXT("settings loaded"), Settings)) { continue; }
				TestEqual(FString::Printf(TEXT("'%s' not migrated"), Effort), Settings->MigrateRetiredModelSelections(), 0);
				TestEqual(FString::Printf(TEXT("'%s' sends deepseek-flash"), Effort), Settings->GetActiveModel(), FString(TEXT("deepseek-flash")));
				TestEqual(FString::Printf(TEXT("'%s' thinking"), Effort), Settings->GetActiveReasoningEffortForProvider(EN2CLLMProvider::DeepSeek),
					FString(*Effort ? Effort : TEXT("high")));
			}
		});
	});

	Describe(TEXT("Consumers"), [this]()
	{
		It(TEXT("get-available-llm-providers offers no retired model and dates the deprecated ones"), [this]()
		{
			const FN2CLLMModelRegistry& Registry = FN2CLLMModelRegistry::Get();
			const TArray<FString> OpenAI = OfferedIds(EN2CLLMProvider::OpenAI);
			const TArray<FString> Anthropic = OfferedIds(EN2CLLMProvider::Anthropic);
			const TArray<FString> Gemini = OfferedIds(EN2CLLMProvider::Gemini);
			for (const TArray<FString>* Ids : { &OpenAI, &Anthropic, &Gemini })
			{
				for (const FString& Id : *Ids)
				{
					const FN2CModelMetadata* Meta = Registry.GetModelMetadataById(Id);
					TestTrue(FString::Printf(TEXT("%s offered and not retired"), *Id), Meta && !Meta->bIsRetired);
				}
			}
			TestTrue(TEXT("default OpenAI model offered"), OpenAI.Contains(TEXT("gpt-6.1-sol")));
			TestTrue(TEXT("default Anthropic model offered"), Anthropic.Contains(TEXT("claude-sonnet-5-5")));
			TestTrue(TEXT("default Gemini model offered"), Gemini.Contains(TEXT("gemini-3.8-flash")));
			TestFalse(TEXT("o4-mini not offered"), OpenAI.Contains(TEXT("o4-mini")));
			TestFalse(TEXT("Claude 3.7 Sonnet not offered"), Anthropic.Contains(TEXT("claude-3-7-sonnet-20250219")));
			TestFalse(TEXT("Gemini 2.5 Flash Preview not offered"), Gemini.Contains(TEXT("gemini-2.5-flash-preview-05-20")));
			TestTrue(TEXT("o3 still offered until it shuts down"), OpenAI.Contains(TEXT("o3")));

			FN2CMcpGetAvailableLLMProvidersTool Tool;
			bool bFoundO3 = false;
			for (const TSharedPtr<FJsonValue>& Value : Tool.GetAvailableModels(EN2CLLMProvider::OpenAI))
			{
				const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
				if (Object.IsValid() && Object->GetStringField(TEXT("id")) == TEXT("o3"))
				{
					bFoundO3 = true;
					bool bDeprecated = false;
					TestTrue(TEXT("o3 is flagged deprecated"), Object->TryGetBoolField(TEXT("deprecated"), bDeprecated) && bDeprecated);
					FString Date;
					TestTrue(TEXT("o3 carries its shutdown date"), Object->TryGetStringField(TEXT("shutdownDate"), Date));
					TestEqual(TEXT("o3 shutdown date"), Date, FString(TEXT("2026-12-11")));
				}
				else if (Object.IsValid() && Object->GetStringField(TEXT("id")) == TEXT("gpt-6.1-sol"))
				{
					TestFalse(TEXT("gpt-6.1-sol is not flagged deprecated"), Object->HasField(TEXT("deprecated")));
				}
			}
			TestTrue(TEXT("o3 entry found"), bFoundO3);
		});

		It(TEXT("get-available-llm-providers offers each DeepSeek ID once with its thinking control"), [this]()
		{
			const TArray<FString> DeepSeek = OfferedIds(EN2CLLMProvider::DeepSeek);
			TestEqual(TEXT("two DeepSeek IDs offered"), DeepSeek.Num(), 2);
			TestTrue(TEXT("deepseek-flash offered"), DeepSeek.Contains(TEXT("deepseek-flash")));
			TestTrue(TEXT("deepseek-v4-pro offered"), DeepSeek.Contains(TEXT("deepseek-v4-pro")));

			FN2CMcpGetAvailableLLMProvidersTool Tool;
			for (const TSharedPtr<FJsonValue>& Value : Tool.GetAvailableModels(EN2CLLMProvider::DeepSeek))
			{
				const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
				if (!Object.IsValid()) { continue; }
				FString Effort;
				TestTrue(FString::Printf(TEXT("%s carries reasoningEffort"), *Object->GetStringField(TEXT("id"))), Object->TryGetStringField(TEXT("reasoningEffort"), Effort));
				TestEqual(FString::Printf(TEXT("%s reasoningEffort"), *Object->GetStringField(TEXT("id"))), Effort, FString(TEXT("high")));
			}
		});

		It(TEXT("prices Gemini from the registry when there is no override"), [this]()
		{
			UN2CSettings* Settings = NewObject<UN2CSettings>(GetTransientPackage());
			Settings->Provider = EN2CLLMProvider::Gemini;
			Settings->GeminiModelId = N2CDefaultModelIds::Gemini;
			Settings->GeminiCustomModelId.Reset();
			Settings->ModelPricing.Empty();
			const FN2CGeminiPricing Expected = FN2CLLMModelRegistry::Get().GetGeminiPricing(N2CDefaultModels::Gemini);
			TestTrue(TEXT("registry has a Gemini price"), Expected.InputCost > 0.0f && Expected.OutputCost > 0.0f);
			TestEqual(TEXT("Gemini input cost"), Settings->GetCurrentInputCost(), Expected.InputCost);
			TestEqual(TEXT("Gemini output cost"), Settings->GetCurrentOutputCost(), Expected.OutputCost);

			Settings->ModelPricing.Add(N2CDefaultModelIds::Gemini, FN2CModelPricing(9.0f, 19.0f));
			TestEqual(TEXT("Gemini input cost override"), Settings->GetCurrentInputCost(), 9.0f);
			TestEqual(TEXT("Gemini output cost override"), Settings->GetCurrentOutputCost(), 19.0f);
		});

		It(TEXT("OpenAI reads system prompt support from the registry by model ID"), [this]()
		{
			for (const TCHAR* Id : { TEXT("o1"), TEXT("o1-mini-2024-09-12"), TEXT("gpt-6.1-sol"), TEXT("gpt-4.1"), TEXT("gpt-7-not-registered") })
			{
				UN2COpenAIService* Service = NewObject<UN2COpenAIService>(GetTransientPackage());
				FN2CLLMConfig Config;
				Config.Provider = EN2CLLMProvider::OpenAI;
				Config.Model = Id;
				Service->Initialize(Config);
				FString Endpoint, AuthToken;
				bool bSupportsSystemPrompts = false;
				Service->GetConfiguration(Endpoint, AuthToken, bSupportsSystemPrompts);
				const FN2CModelMetadata* Meta = FN2CLLMModelRegistry::Get().GetModelMetadataById(Id);
				TestEqual(FString::Printf(TEXT("%s system prompt support"), Id), bSupportsSystemPrompts, Meta ? Meta->bSupportsSystemPrompts : true);
			}
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
