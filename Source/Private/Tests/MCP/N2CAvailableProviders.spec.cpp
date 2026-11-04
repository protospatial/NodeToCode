// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CToolSpecHarness.h"
#include "Core/N2CEditorIntegration.h"
#include "Core/N2CSettings.h"
#include "LLM/N2CModelCatalog.h"
#include "LLM/N2CModelDiscovery.h"
#include "Core/N2CUserSettings.h"
#include "MCP/Python/N2CPythonBridge.h"
#include "MCP/Tools/Implementations/Translation/N2CMcpGetAvailableLLMProvidersTool.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/ScopeExit.h"

/**
 * Card 0217: get-available-llm-providers lists what translation accepts, from the model catalog. Every offered model ID
 * passes translate-focused-blueprint's override parser without a warning and carries a status; a deprecation the
 * provider's live model list reports (card 0216's cache, read only) shows as status deprecated with its shutdown date;
 * provider names and locality are the catalog rows', and each provider says whether a key is configured.
 */
BEGIN_DEFINE_SPEC(FN2CAvailableProvidersSpec, "NodeToCode.MCP.AvailableProviders", N2C_TEST_FLAGS)
END_DEFINE_SPEC(FN2CAvailableProvidersSpec)

void FN2CAvailableProvidersSpec::Define()
{
	It(TEXT("offers only model IDs translation accepts without a warning, each with a status"), [this]()
	{
		FN2CMcpGetAvailableLLMProvidersTool Tool;
		int32 Checked = 0;
		for (const TCHAR* ProviderId : { TEXT("openai"), TEXT("anthropic"), TEXT("gemini"), TEXT("deepseek") })
		{
			FN2CTranslationOverrides Probe;
			FString ProbeError;
			if (!TestTrue(FString::Printf(TEXT("%s is a provider ID"), ProviderId),
				FN2CEditorIntegration::ResolveTranslationOverrides(ProviderId, FString(), FString(), Probe, ProbeError) && Probe.Provider.IsSet()))
			{
				continue;
			}
			for (const TSharedPtr<FJsonValue>& Value : Tool.GetAvailableModels(Probe.Provider.GetValue()))
			{
				const TSharedPtr<FJsonObject> Model = Value.IsValid() ? Value->AsObject() : nullptr;
				if (!Model.IsValid())
				{
					continue;
				}
				const FString Id = Model->GetStringField(TEXT("id"));
				FN2CTranslationOverrides Overrides;
				FString Error;
				const bool bAccepted = FN2CEditorIntegration::ResolveTranslationOverrides(ProviderId, Id, FString(), Overrides, Error);
				TestTrue(FString::Printf(TEXT("%s %s: accepted with no warning (%s %s)"), ProviderId, *Id, *Error,
					*FString::Join(Overrides.Warnings, TEXT("; "))), bAccepted && Overrides.Warnings.Num() == 0);
				const FString Status = Model->GetStringField(TEXT("status"));
				TestTrue(FString::Printf(TEXT("%s %s: status %s"), ProviderId, *Id, *Status), Status == TEXT("active") || Status == TEXT("deprecated"));
				TestTrue(FString::Printf(TEXT("%s %s: limits"), ProviderId, *Id),
					Model->GetNumberField(TEXT("contextWindow")) > 0 && Model->GetNumberField(TEXT("maxOutput")) > 0);
				++Checked;
			}
		}
		TestTrue(TEXT("models were checked"), Checked > 4);
	});

	It(TEXT("marks a model the live list deprecates, with its shutdown date, and the selected model"), [this]()
	{
		UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
		const TArray<FString> OptedIn = Settings->LiveModelListProviders;
		const FN2CModelList* Before = FN2CModelDiscovery::Get().GetList(TEXT("anthropic"));
		const TOptional<FN2CModelList> Saved = Before ? TOptional<FN2CModelList>(*Before) : TOptional<FN2CModelList>();
		ON_SCOPE_EXIT
		{
			Settings->LiveModelListProviders = OptedIn;
			if (Saved.IsSet())
			{
				FN2CModelDiscovery::Get().SetList(Saved.GetValue());
			}
			else
			{
				FN2CModelDiscovery::Get().RemoveList(TEXT("anthropic"));
			}
		};

		FN2CMcpGetAvailableLLMProvidersTool Tool;
		const FString Selected = Settings->GetActiveModelForProvider(EN2CLLMProvider::Anthropic);
		const TArray<const FN2CModelMetadata*> Rows = FN2CModelCatalog::Get().List(TEXT("anthropic"));
		const FN2CModelMetadata* Target = nullptr;
		for (const FN2CModelMetadata* Row : Rows)
		{
			if (!Row->bIsDeprecated)
			{
				Target = Row;
				break;
			}
		}
		if (!TestNotNull(TEXT("an active Anthropic catalog row"), Target))
		{
			return;
		}

		auto Find = [&Tool](const FString& Id) -> TSharedPtr<FJsonObject>
		{
			for (const TSharedPtr<FJsonValue>& Value : Tool.GetAvailableModels(EN2CLLMProvider::Anthropic))
			{
				const TSharedPtr<FJsonObject> Model = Value.IsValid() ? Value->AsObject() : nullptr;
				if (Model.IsValid() && Model->GetStringField(TEXT("id")) == Id)
				{
					return Model;
				}
			}
			return nullptr;
		};

		Settings->LiveModelListProviders.Reset();
		const TSharedPtr<FJsonObject> Plain = Find(Target->ModelId);
		TestTrue(TEXT("active from the catalog"), Plain.IsValid() && Plain->GetStringField(TEXT("status")) == TEXT("active"));

		// The provider's list says it shuts down: the cache is read, never fetched
		FN2CModelList List;
		List.ProviderId = TEXT("anthropic");
		FN2CDiscoveredModel& Live = List.Models.AddDefaulted_GetRef();
		Live.Id = Target->ModelId;
		Live.bDeprecated = true;
		Live.ShutdownDate = TEXT("2027-02-01");
		FN2CModelDiscovery::Get().SetList(List);
		Settings->LiveModelListProviders = { TEXT("anthropic") };
		const TSharedPtr<FJsonObject> Marked = Find(Target->ModelId);
		if (TestTrue(TEXT("still offered"), Marked.IsValid()))
		{
			TestEqual(TEXT("status deprecated"), Marked->GetStringField(TEXT("status")), FString(TEXT("deprecated")));
			TestEqual(TEXT("its shutdown date"), Marked->GetStringField(TEXT("shutdownDate")), FString(TEXT("2027-02-01")));
		}

		// Not opted in: the live list is ignored
		Settings->LiveModelListProviders.Reset();
		const TSharedPtr<FJsonObject> Ignored = Find(Target->ModelId);
		TestTrue(TEXT("not opted in: active again"), Ignored.IsValid() && Ignored->GetStringField(TEXT("status")) == TEXT("active"));

		const TSharedPtr<FJsonObject> SelectedModel = Find(Selected);
		TestTrue(TEXT("the selected model is marked isSelected: ") + Selected, SelectedModel.IsValid() && SelectedModel->GetBoolField(TEXT("isSelected")));
	});

	It(TEXT("gives the Python bridge one entry per provider with exactly one is_current, and the active provider's catalog endpoint"), [this]()
	{
		const TSharedPtr<FJsonObject> Root = N2CTest::ParseJsonObject(UN2CPythonBridge::GetLLMProviders());
		const TSharedPtr<FJsonObject>* Data = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Providers = nullptr;
		if (!TestTrue(TEXT("get_llm_providers data"), Root.IsValid() && Root->TryGetObjectField(TEXT("data"), Data)
			&& (*Data)->TryGetArrayField(TEXT("providers"), Providers)))
		{
			return;
		}
		const UEnum* Enum = StaticEnum<EN2CLLMProvider>();
		TestEqual(TEXT("one entry per provider"), Providers->Num(), Enum->NumEnums() - 1);
		int32 Current = 0;
		FString CurrentName;
		for (const TSharedPtr<FJsonValue>& Value : *Providers)
		{
			if (Value->AsObject()->GetBoolField(TEXT("is_current")))
			{
				++Current;
				CurrentName = Value->AsObject()->GetStringField(TEXT("name"));
			}
		}
		TestEqual(TEXT("exactly one is_current"), Current, 1);
		TestEqual(TEXT("it is current_provider"), CurrentName, (*Data)->GetStringField(TEXT("current_provider")));

		const TSharedPtr<FJsonObject> Active = N2CTest::ParseJsonObject(UN2CPythonBridge::GetActiveProvider());
		const TSharedPtr<FJsonObject>* ActiveData = nullptr;
		if (TestTrue(TEXT("get_active_provider data"), Active.IsValid() && Active->TryGetObjectField(TEXT("data"), ActiveData)))
		{
			TestEqual(TEXT("the same provider"), (*ActiveData)->GetStringField(TEXT("name")), CurrentName);
			TestFalse(TEXT("an endpoint"), (*ActiveData)->GetStringField(TEXT("endpoint")).IsEmpty());
		}
	});

	It(TEXT("names providers and their locality from the catalog, with keyConfigured"), [this]()
	{
		const FMcpToolCallResult Result = N2CTest::RunTool(TEXT("get-available-llm-providers"), MakeShared<FJsonObject>());
		const TSharedPtr<FJsonObject> Json = N2CTest::ResultJson(Result);
		const TArray<TSharedPtr<FJsonValue>>* Providers = nullptr;
		if (!TestTrue(TEXT("a providers array"), Json.IsValid() && Json->TryGetArrayField(TEXT("providers"), Providers)))
		{
			return;
		}
		bool bSawOllama = false;
		for (const TSharedPtr<FJsonValue>& Value : *Providers)
		{
			const TSharedPtr<FJsonObject> Provider = Value->AsObject();
			const FString Id = Provider->GetStringField(TEXT("id"));
			TestTrue(Id + TEXT(": keyConfigured"), Provider->HasTypedField<EJson::Boolean>(TEXT("keyConfigured")));
			if (const FN2CProviderProfile* Row = FN2CModelCatalog::Get().FindProvider(FName(*Id)))
			{
				TestEqual(Id + TEXT(": the catalog's display name"), Provider->GetStringField(TEXT("displayName")), Row->DisplayName);
				TestEqual(Id + TEXT(": the catalog's locality"), Provider->GetBoolField(TEXT("isLocal")), Row->bIsLocal);
			}
			bSawOllama |= Id == TEXT("ollama");
		}
		TestTrue(TEXT("Ollama (local, no key) is listed"), bSawOllama);
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
