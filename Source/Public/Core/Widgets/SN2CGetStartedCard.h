// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "LLM/N2CLLMTypes.h"
#include "Code Editor/Models/N2CCodeLanguage.h"

struct FN2CGraphKey;
class SEditableTextBox;

/**
 * The Get Started card at the top of the Node to Code window (card 0269): choose a provider, save its API key in the
 * OS keychain (the E04 flow: UN2CSettings::SetApiKey), test the connection, pick the target language, and tag a first
 * graph with the N2C bar (with a picture of it). It shows while Show Get Started Card (Editor Preferences > Node to
 * Code (User)) is on and the project has no saved translation; its close button turns the setting off.
 */
class NODETOCODE_API SN2CGetStartedCard : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SN2CGetStartedCard)
		: _AlwaysShow(false)
	{}
		/** Show the card whatever the setting and saved translations say (the docs shot) */
		SLATE_ARGUMENT(bool, AlwaysShow)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SN2CGetStartedCard() override;

	/** Whether the card shows: the setting is on and the project has no saved translation */
	static bool ShouldShow(bool bSettingOn, int32 SavedTranslationCount) { return bSettingOn && SavedTranslationCount == 0; }

	/** ShouldShow for the current setting and graph state */
	bool IsShown() const;

	/** Turns Show Get Started Card off (saved to the user's config unless bSave is false, as in tests) */
	static void Dismiss(bool bSave = true);

	/** The providers the card offers, in the order it lists them */
	static TArray<EN2CLLMProvider> GetProviders();

	/** Ollama and LM Studio run locally and take no key */
	static bool IsLocalProvider(EN2CLLMProvider Provider);

private:
	void HandleTranslationUpdated(const FN2CGraphKey& Graph);
	void RefreshSavedTranslationCount();

	FReply HandleSaveKeyClicked();
	FReply HandleTestClicked();
	FReply HandleCloseClicked();

	EN2CLLMProvider GetProvider() const;
	FText GetKeyStatusText() const;
	FText GetTestStatusText() const;
	FSlateColor GetTestStatusColor() const;

	TArray<TSharedPtr<EN2CLLMProvider>> ProviderOptions;
	TArray<TSharedPtr<EN2CCodeLanguage>> LanguageOptions;
	TSharedPtr<SEditableTextBox> KeyBox;

	int32 SavedTranslationCount = 0;
	bool bAlwaysShow = false;
	FDelegateHandle TranslationUpdatedHandle;

	enum class ETestState : uint8 { None, Running, Ok, Failed };
	ETestState TestState = ETestState::None;
	FString TestMessage;
	FString KeySaveMessage;
};
