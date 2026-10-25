// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Core/N2CSettings.h"
#include "LLM/N2CLLMModelRegistry.h"
#include "LLM/N2CLLMModels.h"
#include "LLM/N2CModelCatalog.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"

/**
 * The model catalog (card 0128): the bundled Content/Data/N2CModelCatalog.json loads, lookups by string ID return
 * its rows, a broken file falls back to the compiled-in defaults without touching saved selections, and the
 * enum-keyed FN2CLLMModelRegistry follows the file without a rebuild. Cases that load another file restore the
 * bundled one afterwards.
 */
BEGIN_DEFINE_SPEC(FN2CModelCatalogSpec, "NodeToCode.LLM.Catalog", N2C_TEST_FLAGS)

	/** The bundled file's text */
	FString BundledText()
	{
		FString Text;
		TestTrue(TEXT("bundled catalog readable"), FFileHelper::LoadFileToString(Text, *FN2CModelCatalog::GetBundledPath()));
		return Text;
	}

	/** The bundled file with Edit applied to its root object, as text */
	FString EditedCatalog(TFunctionRef<void(const TSharedRef<FJsonObject>&)> Edit)
	{
		TSharedPtr<FJsonObject> Root = N2CTest::ParseJsonObject(BundledText());
		if (!TestTrue(TEXT("bundled catalog parses"), Root.IsValid())) { return FString(); }
		Edit(Root.ToSharedRef());
		FString Out;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
		return Out;
	}

	/** The model row object with this id (and reasoning_effort, when given) in a parsed catalog */
	TSharedPtr<FJsonObject> ModelRow(const TSharedRef<FJsonObject>& Root, const FString& Id)
	{
		for (const TSharedPtr<FJsonValue>& Value : Root->GetArrayField(TEXT("models")))
		{
			const TSharedPtr<FJsonObject> Row = Value->AsObject();
			if (Row.IsValid() && Row->GetStringField(TEXT("id")) == Id)
			{
				return Row;
			}
		}
		return nullptr;
	}

	/** Writes Text to a scratch file and loads it */
	bool LoadText(const N2CTest::FScratchDir& Dir, const FString& Text)
	{
		const FString Path = Dir / TEXT("N2CModelCatalog.json");
		TestTrue(TEXT("test catalog written"), FFileHelper::SaveStringToFile(Text, *Path));
		return FN2CModelCatalog::Get().LoadFromFile(Path);
	}

	/** Expect the one error a broken catalog file logs */
	void ExpectFallbackError(const TCHAR* Text)
	{
		AddExpectedError(Text, EAutomationExpectedErrorFlags::Contains, 1);
	}

END_DEFINE_SPEC(FN2CModelCatalogSpec)

void FN2CModelCatalogSpec::Define()
{
	AfterEach([this]()
	{
		// Every case leaves the shipped catalog loaded, whatever it loaded itself
		FN2CModelCatalog::Get().ReloadBundled();
	});

	Describe(TEXT("Shipped file"), [this]()
	{
		It(TEXT("loads from Content/Data with every provider and no fallback"), [this]()
		{
			FN2CModelCatalog& Catalog = FN2CModelCatalog::Get();
			TestTrue(TEXT("bundled file loads"), Catalog.ReloadBundled());
			TestFalse(TEXT("not the fallback"), Catalog.IsFallback());
			TestTrue(TEXT("path is Content/Data/N2CModelCatalog.json"),
				FPaths::GetCleanFilename(Catalog.GetSourcePath()) == TEXT("N2CModelCatalog.json")
				&& FPaths::GetPathLeaf(FPaths::GetPath(Catalog.GetSourcePath())) == TEXT("Data"));
			for (const EN2CLLMProvider Provider : { EN2CLLMProvider::OpenAI, EN2CLLMProvider::Anthropic, EN2CLLMProvider::Gemini,
				EN2CLLMProvider::DeepSeek, EN2CLLMProvider::Ollama, EN2CLLMProvider::LMStudio })
			{
				const FName Id = FN2CModelCatalog::ProviderIdFor(Provider);
				const FN2CProviderProfile* Profile = Catalog.FindProvider(Id);
				if (!TestNotNull(FString::Printf(TEXT("provider row %s"), *Id.ToString()), Profile)) { continue; }
				TestEqual(FString::Printf(TEXT("%s maps to its enum value"), *Id.ToString()), Profile->Provider, Provider);
				TestFalse(FString::Printf(TEXT("%s has a base URL"), *Id.ToString()), Profile->BaseUrl.IsEmpty());
				TestEqual(FString::Printf(TEXT("%s needs a key exactly when it is not local"), *Id.ToString()), Profile->bNeedsKey, !Profile->bIsLocal);
			}
			TestEqual(TEXT("six providers"), Catalog.Providers().Num(), 6);
		});

		It(TEXT("lists only models the providers still serve, each with a lifecycle and a cached price field"), [this]()
		{
			// Retired models are not in the file: they come from the compiled-in legacy list
			const TSharedPtr<FJsonObject> Root = N2CTest::ParseJsonObject(BundledText());
			if (!TestTrue(TEXT("parses"), Root.IsValid())) { return; }
			int32 Rows = 0;
			for (const TSharedPtr<FJsonValue>& Value : Root->GetArrayField(TEXT("models")))
			{
				const TSharedPtr<FJsonObject> Row = Value->AsObject();
				const FString Id = Row->GetStringField(TEXT("id"));
				const FString Status = Row->GetObjectField(TEXT("lifecycle"))->GetStringField(TEXT("status"));
				TestTrue(FString::Printf(TEXT("%s is active or deprecated"), *Id), Status == TEXT("active") || Status == TEXT("deprecated"));
				TestTrue(FString::Printf(TEXT("%s has pricing.cached_input"), *Id), Row->GetObjectField(TEXT("pricing"))->HasField(TEXT("cached_input")));
				TestTrue(FString::Printf(TEXT("%s names its enumerator"), *Id), Row->HasTypedField<EJson::String>(TEXT("legacy_enum")));
				++Rows;
			}
			TestEqual(TEXT("24 live models (non-retired registry entries after E03)"), Rows, 24);
		});

		It(TEXT("finds each provider's default ID with pricing, context window and max output"), [this]()
		{
			const FN2CModelCatalog& Catalog = FN2CModelCatalog::Get();
			struct FExpected { EN2CLLMProvider Provider; const TCHAR* Id; FString EnumName; const TCHAR* SettingsDefault; };
			for (const FExpected& E : {
				FExpected{ EN2CLLMProvider::OpenAI, TEXT("gpt-6.1-sol"), StaticEnum<EN2COpenAIModel>()->GetNameStringByValue(static_cast<int64>(N2CDefaultModels::OpenAI)), N2CDefaultModelIds::OpenAI },
				FExpected{ EN2CLLMProvider::Anthropic, TEXT("claude-sonnet-5-5"), StaticEnum<EN2CAnthropicModel>()->GetNameStringByValue(static_cast<int64>(N2CDefaultModels::Anthropic)), N2CDefaultModelIds::Anthropic },
				FExpected{ EN2CLLMProvider::Gemini, TEXT("gemini-3.8-flash"), StaticEnum<EN2CGeminiModel>()->GetNameStringByValue(static_cast<int64>(N2CDefaultModels::Gemini)), N2CDefaultModelIds::Gemini },
				FExpected{ EN2CLLMProvider::DeepSeek, TEXT("deepseek-flash"), StaticEnum<EN2CDeepSeekModel>()->GetNameStringByValue(static_cast<int64>(N2CDefaultModels::DeepSeek)), N2CDefaultModelIds::DeepSeek } })
			{
				TestEqual(FString::Printf(TEXT("%s is the N2CDefaultModelIds default of Project Settings"), E.Id), FString(E.SettingsDefault), FString(E.Id));
				const FName ProviderId = FN2CModelCatalog::ProviderIdFor(E.Provider);
				const FN2CModelMetadata* Meta = Catalog.Find(ProviderId, E.Id);
				if (!TestNotNull(FString::Printf(TEXT("%s found"), E.Id), Meta)) { continue; }
				TestTrue(FString::Printf(TEXT("%s has input and output prices"), E.Id), Meta->InputCostPer1M > 0.0f && Meta->OutputCostPer1M > 0.0f);
				TestTrue(FString::Printf(TEXT("%s has a cached input price below its input price"), E.Id),
					Meta->CachedInputCostPer1M > 0.0f && Meta->CachedInputCostPer1M < Meta->InputCostPer1M);
				TestTrue(FString::Printf(TEXT("%s has a context window"), E.Id), Meta->ContextWindowSize >= 1000000);
				TestTrue(FString::Printf(TEXT("%s has a max output"), E.Id), Meta->MaxOutputTokens > 0 && Meta->MaxOutputTokens <= Meta->ContextWindowSize);
				TestTrue(FString::Printf(TEXT("%s is the provider default"), E.Id), Meta->bIsDefault && Catalog.FindDefault(ProviderId) == Meta);
				TestFalse(FString::Printf(TEXT("%s is not deprecated"), E.Id), Meta->bIsDeprecated);
				TestEqual(FString::Printf(TEXT("%s is the N2CDefaultModels enumerator"), E.Id), Meta->LegacyEnumName, E.EnumName);
				if (E.Provider == EN2CLLMProvider::DeepSeek)
				{
					TestEqual(TEXT("DeepSeek's default thinking mode is its default row's"), FString(N2CDefaultModelIds::DeepSeekReasoningEffort), Meta->ReasoningEffort);
				}
			}
			TestNull(TEXT("Ollama has no catalog models"), Catalog.FindDefault(TEXT("ollama")));
		});

		It(TEXT("returns null for an unknown model ID or provider"), [this]()
		{
			const FN2CModelCatalog& Catalog = FN2CModelCatalog::Get();
			TestNull(TEXT("unknown OpenAI ID"), Catalog.Find(TEXT("openai"), TEXT("gpt-7-not-real")));
			TestNull(TEXT("ID under the wrong provider"), Catalog.Find(TEXT("anthropic"), TEXT("gpt-6.1-sol")));
			TestNull(TEXT("unknown provider"), Catalog.Find(TEXT("no-such-provider"), TEXT("gpt-6.1-sol")));
			TestNull(TEXT("unknown ID under any provider"), Catalog.FindById(TEXT("gpt-7-not-real")));
			TestNull(TEXT("registry by ID"), FN2CLLMModelRegistry::Get().GetModelMetadataById(TEXT("gpt-7-not-real")));
		});

		It(TEXT("keys DeepSeek thinking modes by model ID plus reasoning effort"), [this]()
		{
			const FN2CModelCatalog& Catalog = FN2CModelCatalog::Get();
			const FN2CModelMetadata* Default = Catalog.Find(TEXT("deepseek"), TEXT("deepseek-flash"));
			const FN2CModelMetadata* Off = Catalog.Find(TEXT("deepseek"), TEXT("deepseek-flash"), TEXT("none"));
			if (!TestNotNull(TEXT("default mode"), Default) || !TestNotNull(TEXT("thinking off"), Off)) { return; }
			TestEqual(TEXT("bare ID gives thinking at high"), Default->ReasoningEffort, FString(TEXT("high")));
			TestEqual(TEXT("none gives the JSON row"), Off->StructuredOutputMode, FString(TEXT("json_object")));
			TestTrue(TEXT("legacy enum DeepSeek_V3 selects the same row"), Catalog.FindByLegacyEnum(TEXT("deepseek"), TEXT("DeepSeek_V3")) == Off);
			TestTrue(TEXT("both modes listed"), Catalog.List(TEXT("deepseek")).Num() == 3);
		});

		It(TEXT("keeps the retired enumerators as compiled-in retired rows that resolve but are not listed"), [this]()
		{
			const FN2CModelCatalog& Catalog = FN2CModelCatalog::Get();
			const FN2CModelMetadata* Retired = Catalog.FindByLegacyEnum(TEXT("anthropic"), TEXT("Claude3_7_Sonnet"));
			if (!TestNotNull(TEXT("Claude3_7_Sonnet resolves"), Retired)) { return; }
			TestTrue(TEXT("retired"), Retired->bIsRetired && Retired->bIsDeprecated);
			TestEqual(TEXT("shutdown date"), Retired->ShutdownDate, FString(TEXT("2026-02-19")));
			TestFalse(TEXT("not listed"), Catalog.List(TEXT("anthropic")).Contains(Retired));
			TestTrue(TEXT("listed with retired rows"), Catalog.List(TEXT("anthropic"), true).Contains(Retired));
			const FN2CModelMetadata* O1 = Catalog.FindById(TEXT("o1"));
			TestTrue(TEXT("o1 keeps its no-system-prompt flag"), O1 && !O1->bSupportsSystemPrompts);
		});

		It(TEXT("matches the compiled-in fallback rows field for field"), [this]()
		{
			// The fallback copies each default row; this fails when the file's default changes and the copy does not
			FN2CModelCatalog& Catalog = FN2CModelCatalog::Get();
			TMap<FName, FN2CModelMetadata> Shipped;
			for (const FName Id : { FName(TEXT("openai")), FName(TEXT("anthropic")), FName(TEXT("gemini")), FName(TEXT("deepseek")) })
			{
				if (const FN2CModelMetadata* Default = Catalog.FindDefault(Id))
				{
					Shipped.Add(Id, *Default);
				}
			}
			TestEqual(TEXT("four shipped defaults"), Shipped.Num(), 4);

			ExpectFallbackError(TEXT("is malformed"));
			TestFalse(TEXT("empty text does not load"), Catalog.LoadFromString(TEXT("{}"), TEXT("test")));
			for (const TPair<FName, FN2CModelMetadata>& Pair : Shipped)
			{
				const FN2CModelMetadata* Fallback = Catalog.FindDefault(Pair.Key);
				if (!TestNotNull(FString::Printf(TEXT("%s fallback default"), *Pair.Key.ToString()), Fallback)) { continue; }
				TestTrue(FString::Printf(TEXT("%s fallback equals the shipped row"), *Pair.Key.ToString()),
					FN2CModelMetadata::StaticStruct()->CompareScriptStruct(Fallback, &Pair.Value, PPF_None));
			}
		});
	});

	Describe(TEXT("Broken file"), [this]()
	{
		It(TEXT("falls back to the compiled-in defaults and logs an error when the file is malformed"), [this]()
		{
			N2CTest::FScratchDir Dir(TEXT("N2CCatalog"));
			ExpectFallbackError(TEXT("is malformed"));
			TestFalse(TEXT("malformed file does not load"), LoadText(Dir, TEXT("{ \"schema_version\": 1, \"providers\": [ { \"id\": ")));

			const FN2CModelCatalog& Catalog = FN2CModelCatalog::Get();
			TestTrue(TEXT("fallback"), Catalog.IsFallback());
			for (const TCHAR* Id : { TEXT("openai"), TEXT("anthropic"), TEXT("gemini"), TEXT("deepseek") })
			{
				TestEqual(FString::Printf(TEXT("%s has one live model"), Id), Catalog.List(Id).Num(), 1);
				TestNotNull(FString::Printf(TEXT("%s has its default"), Id), Catalog.FindDefault(Id));
			}
			TestEqual(TEXT("six providers still known"), Catalog.Providers().Num(), 6);

			const FN2CLLMModelRegistry& Registry = FN2CLLMModelRegistry::Get();
			TestEqual(TEXT("registry default ID still resolves"), Registry.GetModelId(N2CDefaultModels::Anthropic), FString(TEXT("claude-sonnet-5-5")));
			TestNull(TEXT("a non-default model is unknown"), Registry.GetModelMetadata(EN2COpenAIModel::GPT_6_Astra));
			TestTrue(TEXT("retired enumerators still resolve"), Registry.GetModelMetadata(EN2COpenAIModel::GPT_o1) && Registry.GetModelMetadata(EN2COpenAIModel::GPT_o1)->bIsRetired);

			// A broken install must not rewrite saved selections the catalog would know
			UN2CSettings* Settings = NewObject<UN2CSettings>(GetTransientPackage());
			Settings->OpenAIModelId = TEXT("gpt-6-astra");
			TestEqual(TEXT("selection kept in fallback"), Settings->MigrateRetiredModelSelections(), 0);
			TestEqual(TEXT("GPT-6 Astra still selected"), Settings->OpenAIModelId, FString(TEXT("gpt-6-astra")));
			TestEqual(TEXT("and sent"), Settings->GetActiveModelForProvider(EN2CLLMProvider::OpenAI), FString(TEXT("gpt-6-astra")));
		});

		It(TEXT("falls back and logs an error when the file is missing"), [this]()
		{
			N2CTest::FScratchDir Dir(TEXT("N2CCatalog"));
			ExpectFallbackError(TEXT("could not be read"));
			TestFalse(TEXT("missing file does not load"), FN2CModelCatalog::Get().LoadFromFile(Dir / TEXT("missing.json")));
			TestTrue(TEXT("fallback"), FN2CModelCatalog::Get().IsFallback());
		});

		It(TEXT("rejects a file whose rows break the rules"), [this]()
		{
			N2CTest::FScratchDir Dir(TEXT("N2CCatalog"));
			struct FCase { const TCHAR* What; TFunction<void(const TSharedRef<FJsonObject>&)> Edit; };
			const TArray<FCase> Cases = {
				{ TEXT("newer schema"), [](const TSharedRef<FJsonObject>& Root) { Root->SetNumberField(TEXT("schema_version"), 2); } },
				{ TEXT("negative price"), [this](const TSharedRef<FJsonObject>& Root) { ModelRow(Root, TEXT("gpt-6-luna"))->GetObjectField(TEXT("pricing"))->SetNumberField(TEXT("input"), -1); } },
				{ TEXT("unknown status"), [this](const TSharedRef<FJsonObject>& Root) { ModelRow(Root, TEXT("gpt-6-luna"))->GetObjectField(TEXT("lifecycle"))->SetStringField(TEXT("status"), TEXT("sunset")); } },
				{ TEXT("two defaults"), [this](const TSharedRef<FJsonObject>& Root) { ModelRow(Root, TEXT("gpt-6-luna"))->SetBoolField(TEXT("is_default"), true); } },
				{ TEXT("unknown provider"), [this](const TSharedRef<FJsonObject>& Root) { ModelRow(Root, TEXT("gpt-6-luna"))->SetStringField(TEXT("provider"), TEXT("acme")); } },
				{ TEXT("max output over the context window"), [this](const TSharedRef<FJsonObject>& Root) { ModelRow(Root, TEXT("gpt-6-luna"))->SetNumberField(TEXT("max_output"), 2000000); } },
			};
			for (const FCase& Case : Cases)
			{
				ExpectFallbackError(TEXT("is malformed"));
				TestFalse(FString::Printf(TEXT("%s is rejected"), Case.What), LoadText(Dir, EditedCatalog(Case.Edit)));
				TestTrue(FString::Printf(TEXT("%s leaves the fallback"), Case.What), FN2CModelCatalog::Get().IsFallback());
			}
		});
	});

	Describe(TEXT("Registry"), [this]()
	{
		It(TEXT("follows a changed price in the file without a rebuild"), [this]()
		{
			N2CTest::FScratchDir Dir(TEXT("N2CCatalog"));
			const FN2CModelMetadata* Before = FN2CLLMModelRegistry::Get().GetModelMetadataById(TEXT("claude-sonnet-5-5"));
			if (!TestNotNull(TEXT("shipped row"), Before)) { return; }
			TestEqual(TEXT("shipped input price"), Before->InputCostPer1M, 2.0f);

			const FString Text = EditedCatalog([this](const TSharedRef<FJsonObject>& Root)
			{
				const TSharedPtr<FJsonObject> Pricing = ModelRow(Root, TEXT("claude-sonnet-5-5"))->GetObjectField(TEXT("pricing"));
				Pricing->SetNumberField(TEXT("input"), 7.25);
				Pricing->SetNumberField(TEXT("output"), 31.5);
			});
			TestTrue(TEXT("edited copy loads"), LoadText(Dir, Text));

			const FN2CLLMModelRegistry& Registry = FN2CLLMModelRegistry::Get();
			const FN2CModelMetadata* After = Registry.GetModelMetadataById(TEXT("claude-sonnet-5-5"));
			if (!TestNotNull(TEXT("row after reload"), After)) { return; }
			TestEqual(TEXT("registry input price follows the file"), After->InputCostPer1M, 7.25f);
			TestEqual(TEXT("registry output price follows the file"), After->OutputCostPer1M, 31.5f);
			TestEqual(TEXT("enum pricing getter follows the file"), Registry.GetAnthropicPricing(EN2CAnthropicModel::Claude5_5_Sonnet).InputCost, 7.25f);
		});

		It(TEXT("files every catalog row under the enumerator it names and every enumerator has a row"), [this]()
		{
			const FN2CModelCatalog& Catalog = FN2CModelCatalog::Get();
			const FN2CLLMModelRegistry& Registry = FN2CLLMModelRegistry::Get();
			int32 Filed = Registry.GetAllOpenAIModels().Num() + Registry.GetAllAnthropicModels().Num()
				+ Registry.GetAllGeminiModels().Num() + Registry.GetAllDeepSeekModels().Num();
			TestEqual(TEXT("every row has an enumerator"), Filed, Catalog.AllModels().Num());
			TestEqual(TEXT("every OpenAI enumerator has a row"), Registry.GetAllOpenAIModels().Num(), StaticEnum<EN2COpenAIModel>()->NumEnums() - 1);
			TestEqual(TEXT("every Anthropic enumerator has a row"), Registry.GetAllAnthropicModels().Num(), StaticEnum<EN2CAnthropicModel>()->NumEnums() - 1);
			TestEqual(TEXT("every Gemini enumerator has a row"), Registry.GetAllGeminiModels().Num(), StaticEnum<EN2CGeminiModel>()->NumEnums() - 1);
			TestEqual(TEXT("every DeepSeek enumerator has a row"), Registry.GetAllDeepSeekModels().Num(), StaticEnum<EN2CDeepSeekModel>()->NumEnums() - 1);
			for (const FN2CModelMetadata& Row : Catalog.AllModels())
			{
				const FN2CModelMetadata* ById = Registry.GetModelMetadataById(Row.ModelId, Row.ReasoningEffort);
				TestTrue(FString::Printf(TEXT("%s resolves by ID to the same data"), *Row.ModelId),
					ById && (Row.bIsRetired || FN2CModelMetadata::StaticStruct()->CompareScriptStruct(ById, &Row, PPF_None)));
			}
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
