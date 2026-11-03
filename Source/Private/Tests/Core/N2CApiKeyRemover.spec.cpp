// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CSecretStore.h"
#include "Core/N2CSettings.h"
#include "Core/N2CSettingsCustomization.h"
#include "Tests/N2CTestHelpers.h"
#include "Framework/Application/SlateApplication.h"
#include "ISinglePropertyView.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Runtime/Launch/Resources/Version.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

/**
 * The Remove Saved <Provider> Key buttons in Project Settings (card 0488, gate 0389). They were CallInEditor functions,
 * which the settings viewer does not show, so a user had no way to delete a saved key. Each cloud provider now has a
 * Saved Key row (an FN2CApiKeyRemover property) that FN2CApiKeyRemoverCustomization draws as the button. No key is
 * removed here: the button's action is UN2CSettings::SetApiKey with an empty key, covered by the secret store specs.
 */
BEGIN_DEFINE_SPEC(FN2CApiKeyRemoverSpec, "NodeToCode.Settings.RemoveSavedKey", N2C_TEST_FLAGS)

	/** Every text shown in Widget's tree */
	static void CollectTexts(const TSharedRef<SWidget>& Widget, TArray<FString>& OutTexts)
	{
		if (Widget->GetType() == FName(TEXT("STextBlock")))
		{
			OutTexts.Add(StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString());
		}
		FChildren* Children = Widget->GetChildren();
		for (int32 Index = 0; Children && Index < Children->Num(); ++Index)
		{
			CollectTexts(Children->GetChildAt(Index), OutTexts);
		}
	}

END_DEFINE_SPEC(FN2CApiKeyRemoverSpec)

void FN2CApiKeyRemoverSpec::Define()
{
	It(TEXT("gives every cloud provider one Saved Key row, in its own category"), [this]()
	{
		const UN2CSettings* Settings = GetDefault<UN2CSettings>();
		struct FCase { EN2CLLMProvider Provider; const TCHAR* KeyField; };
		const FCase Cases[] = {
			{ EN2CLLMProvider::Anthropic, TEXT("Anthropic_API_Key_UI") }, { EN2CLLMProvider::OpenAI, TEXT("OpenAI_API_Key_UI") },
			{ EN2CLLMProvider::Gemini, TEXT("Gemini_API_Key_UI") }, { EN2CLLMProvider::DeepSeek, TEXT("DeepSeek_API_Key_UI") } };
		for (const FCase& Case : Cases)
		{
			const FProperty* KeyField = UN2CSettings::StaticClass()->FindPropertyByName(Case.KeyField);
			if (!TestNotNull(FString::Printf(TEXT("%s exists"), Case.KeyField), KeyField)) { continue; }
			int32 Rows = 0;
			for (TFieldIterator<FStructProperty> It(UN2CSettings::StaticClass()); It; ++It)
			{
				if (It->Struct != FN2CApiKeyRemover::StaticStruct())
				{
					continue;
				}
				const FN2CApiKeyRemover* Row = It->ContainerPtrToValuePtr<FN2CApiKeyRemover>(Settings);
				if (Row->Provider != Case.Provider)
				{
					continue;
				}
				++Rows;
				TestEqual(FString::Printf(TEXT("%s: the row sits with the API Key field"), *It->GetName()),
					It->GetMetaData(TEXT("Category")), KeyField->GetMetaData(TEXT("Category")));
				TestTrue(FString::Printf(TEXT("%s: shown and never saved"), *It->GetName()),
					It->HasAnyPropertyFlags(CPF_Edit) && It->HasAnyPropertyFlags(CPF_Transient) && !It->HasAnyPropertyFlags(CPF_Config));
				TestFalse(FString::Printf(TEXT("%s: not visible to Blueprints"), *It->GetName()),
					It->HasAnyPropertyFlags(CPF_BlueprintVisible));
			}
			TestEqual(FString::Printf(TEXT("one Saved Key row for %s"), *FN2CSecretStore::GetProviderId(Case.Provider)), Rows, 1);
			TestEqual(TEXT("the button names the provider"), FN2CApiKeyRemoverCustomization::GetButtonLabel(Case.Provider).ToString(),
				FString::Printf(TEXT("Remove Saved %s Key"), *FN2CSecretStore::GetProviderId(Case.Provider)));
		}
		TestFalse(TEXT("a local provider has no saved key to remove"), FN2CApiKeyRemoverCustomization::CanRemove(EN2CLLMProvider::Ollama));
		TestNull(TEXT("the CallInEditor buttons the viewer never showed are gone"),
			UN2CSettings::StaticClass()->FindFunctionByName(TEXT("RemoveSavedAnthropicKey")));
	});

	It(TEXT("draws the Saved Key row as the button"), [this]()
	{
		if (!FSlateApplication::IsInitialized())
		{
			AddInfo(TEXT("Slate is not initialized; skipping"));
			return;
		}
		// The row as the settings panel builds it: the property's customized widget, drawn at once
		FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
		const TSharedPtr<ISinglePropertyView> Row = PropertyEditor.CreateSingleProperty(GetMutableDefault<UN2CSettings>(),
			TEXT("DeepSeekKeyRemover"), FSinglePropertyParams());
		if (!TestTrue(TEXT("the row is built"), Row.IsValid()))
		{
			return;
		}
		Row->SlatePrepass(1.0f);
		TArray<FString> Texts;
		CollectTexts(Row.ToSharedRef(), Texts);
		TestTrue(FString::Printf(TEXT("the row shows Remove Saved DeepSeek Key (texts: %s)"), *FString::Join(Texts, TEXT(" | "))),
			Texts.Contains(TEXT("Remove Saved DeepSeek Key")));
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
