// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Core/Widgets/SN2CGetStartedCard.h"
#include "Core/N2CGraphStateManager.h"
#include "Core/N2CSettings.h"
#include "Core/N2CTargetLanguage.h"
#include "Core/N2CUserSettings.h"
#include "Core/Services/N2CConnectionTest.h"
#include "Core/Style/N2CDesignTokens.h"
#include "Core/Style/N2CStyle.h"
#include "Core/Style/N2CUIColors.h"
#include "Core/Widgets/SN2CIconButton.h"
#include "Core/Widgets/SN2CPanel.h"
#include "LLM/N2CLLMModule.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SN2CGetStartedCard"

namespace N2CGetStartedCardPrivate
{
	/** One numbered step: "1  Provider" on the left, its controls on the right */
	TSharedRef<SWidget> MakeStep(const FText& Number, const FText& Label, const TSharedRef<SWidget>& Content)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(150.0f)
				[
					SNew(STextBlock)
					.Text(FText::Format(LOCTEXT("StepLabel", "{0}   {1}"), Number, Label))
					.Font(FN2CFonts::RegularBold())
					.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::TextSecondary))
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				Content
			];
	}
}

void SN2CGetStartedCard::Construct(const FArguments& InArgs)
{
	using namespace N2CGetStartedCardPrivate;
	bAlwaysShow = InArgs._AlwaysShow;

	for (const EN2CLLMProvider Provider : GetProviders())
	{
		ProviderOptions.Add(MakeShared<EN2CLLMProvider>(Provider));
	}
	for (const FN2CTargetLanguage& Language : N2C::GetLanguages())
	{
		LanguageOptions.Add(MakeShared<EN2CCodeLanguage>(Language.Enum));
	}

	RefreshSavedTranslationCount();
	TranslationUpdatedHandle = UN2CGraphStateManager::Get().OnGraphTranslationUpdated.AddSP(this, &SN2CGetStartedCard::HandleTranslationUpdated);

	auto FindProvider = [this](EN2CLLMProvider Provider)
	{
		const TSharedPtr<EN2CLLMProvider>* Found = ProviderOptions.FindByPredicate([Provider](const TSharedPtr<EN2CLLMProvider>& Item) { return *Item == Provider; });
		return Found ? *Found : TSharedPtr<EN2CLLMProvider>();
	};
	auto FindLanguage = [this](EN2CCodeLanguage Language)
	{
		const TSharedPtr<EN2CCodeLanguage>* Found = LanguageOptions.FindByPredicate([Language](const TSharedPtr<EN2CCodeLanguage>& Item) { return *Item == Language; });
		return Found ? *Found : TSharedPtr<EN2CCodeLanguage>();
	};

	ChildSlot
	[
		SNew(SN2CPanel)
		.Variant(EN2CPanelVariant::Dark)
		.Padding(FMargin(FN2CSpacing::LG, FN2CSpacing::MD))
		.Visibility_Lambda([this]() { return IsShown() ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SVerticalBox)
			// Title and close
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, FN2CSpacing::SM)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Title", "Get started"))
					.Font(FN2CFonts::LargeBold())
					.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::TextPrimary))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SN2CIconButton)
					.ButtonVariant(EN2CButtonVariant::NoBorder)
					.IconBrush(FAppStyle::GetBrush("Icons.X"))
					.IconSize(FVector2D(FN2CSizing::IconMD, FN2CSizing::IconMD))
					.IconColor(N2C::UI::Bind(&FN2CUIColors::TextSecondary))
					.ToolTipText(LOCTEXT("CloseTooltip", "Hide this card. Editor Preferences > Plugins > Node to Code (User) > Show Get Started Card brings it back."))
					.OnClicked(this, &SN2CGetStartedCard::HandleCloseClicked)
				]
			]
			// 1. Provider
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, FN2CSpacing::XS)
			[
				MakeStep(FText::FromString(TEXT("1")), LOCTEXT("ProviderStep", "Provider"),
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SComboBox<TSharedPtr<EN2CLLMProvider>>)
						.ComboBoxStyle(&N2CStyle::GetComboBoxStyle())
						.ItemStyle(&N2CStyle::GetComboRowStyle())
						.OptionsSource(&ProviderOptions)
						.InitiallySelectedItem(FindProvider(GetProvider()))
						.OnGenerateWidget_Lambda([](TSharedPtr<EN2CLLMProvider> Item)
						{
							return SNew(STextBlock).Text(FText::FromString(UN2CLLMModule::GetProviderName(*Item)));
						})
						.OnSelectionChanged_Lambda([this](TSharedPtr<EN2CLLMProvider> Item, ESelectInfo::Type SelectInfo)
						{
							// The provider is the user's own setting: saved to their per-user config, never the project's
							UN2CUserSettings* UserSettings = GetMutableDefault<UN2CUserSettings>();
							if (!Item.IsValid() || SelectInfo == ESelectInfo::Direct || UserSettings->Provider == *Item)
							{
								return;
							}
							UserSettings->Provider = *Item;
							UserSettings->SaveToUserConfig({ GET_MEMBER_NAME_CHECKED(UN2CUserSettings, Provider) });
							GetMutableDefault<UN2CSettings>()->NotifyModelSettingsChanged();
							TestState = ETestState::None;
							KeySaveMessage.Reset();
						})
						[
							SNew(STextBlock)
							.Text_Lambda([this]() { return FText::FromString(UN2CLLMModule::GetProviderName(GetProvider())); })
							.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::AccentOrange))
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					.Padding(FN2CSpacing::MD, 0.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("ProviderHint", "The model list above (Model) offers this provider's models."))
						.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::TextMuted))
					])
			]
			// 2. API key
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, FN2CSpacing::XS)
			[
				MakeStep(FText::FromString(TEXT("2")), LOCTEXT("KeyStep", "API key"),
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SBox)
						.WidthOverride(260.0f)
						.Visibility_Lambda([this]() { return IsLocalProvider(GetProvider()) ? EVisibility::Collapsed : EVisibility::Visible; })
						[
							SAssignNew(KeyBox, SEditableTextBox)
							.Style(&N2CStyle::GetEditableTextBoxStyle())
							.IsPassword(true)
							.HintText(LOCTEXT("KeyHint", "Paste your API key"))
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(FN2CSpacing::SM, 0.0f, 0.0f, 0.0f)
					[
						SNew(SN2CIconButton)
						.Text(LOCTEXT("SaveKey", "Save Key"))
						.ButtonVariant(EN2CButtonVariant::Standard)
						.Visibility_Lambda([this]() { return IsLocalProvider(GetProvider()) ? EVisibility::Collapsed : EVisibility::Visible; })
						.ToolTipText(LOCTEXT("SaveKeyTooltip", "Save the key in your system's credential store (Windows Credential Manager or the macOS Keychain), as the API Key field in Project Settings does. It is never written to a project file."))
						.OnClicked(this, &SN2CGetStartedCard::HandleSaveKeyClicked)
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					.Padding(FN2CSpacing::MD, 0.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(this, &SN2CGetStartedCard::GetKeyStatusText)
						.AutoWrapText(true)
						.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::TextMuted))
					])
			]
			// 3. Connection test
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, FN2CSpacing::XS)
			[
				MakeStep(FText::FromString(TEXT("3")), LOCTEXT("TestStep", "Connection"),
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SN2CIconButton)
						.IconBrush(FAppStyle::GetBrush("Icons.Refresh"))
						.Text(LOCTEXT("TestButton", "Test Connection"))
						.ButtonVariant(EN2CButtonVariant::Standard)
						.IsEnabled_Lambda([this]() { return TestState != ETestState::Running; })
						.ToolTipText(LOCTEXT("TestTooltip", "Send one short request (\"Reply with OK\") to the provider and model above"))
						.OnClicked(this, &SN2CGetStartedCard::HandleTestClicked)
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					.Padding(FN2CSpacing::MD, 0.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(this, &SN2CGetStartedCard::GetTestStatusText)
						.AutoWrapText(true)
						.ColorAndOpacity(this, &SN2CGetStartedCard::GetTestStatusColor)
					])
			]
			// 4. Target language
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, FN2CSpacing::XS)
			[
				MakeStep(FText::FromString(TEXT("4")), LOCTEXT("LanguageStep", "Target language"),
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SComboBox<TSharedPtr<EN2CCodeLanguage>>)
						.ComboBoxStyle(&N2CStyle::GetComboBoxStyle())
						.ItemStyle(&N2CStyle::GetComboRowStyle())
						.OptionsSource(&LanguageOptions)
						.InitiallySelectedItem(FindLanguage(GetDefault<UN2CSettings>()->TargetLanguage))
						.OnGenerateWidget_Lambda([](TSharedPtr<EN2CCodeLanguage> Item)
						{
							return SNew(STextBlock).Text(FText::FromString(N2C::GetLanguage(*Item).DisplayName));
						})
						.OnSelectionChanged_Lambda([](TSharedPtr<EN2CCodeLanguage> Item, ESelectInfo::Type SelectInfo)
						{
							UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
							if (!Item.IsValid() || SelectInfo == ESelectInfo::Direct || Settings->TargetLanguage == *Item)
							{
								return;
							}
							Settings->TargetLanguage = *Item;
							Settings->SaveToProjectConfig({ GET_MEMBER_NAME_CHECKED(UN2CSettings, TargetLanguage) });
						})
						[
							SNew(STextBlock)
							.Text_Lambda([]() { return FText::FromString(N2C::GetLanguage(GetDefault<UN2CSettings>()->TargetLanguage).DisplayName); })
							.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::AccentOrange))
						]
					])
			]
			// 5. Tag a first graph, with a picture of the N2C bar
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, FN2CSpacing::XS)
			[
				MakeStep(FText::FromString(TEXT("5")), LOCTEXT("TagStep", "First graph"),
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SImage)
						.Image(N2CStyle::Get().GetBrush("NodeToCode.GetStarted.Overlay"))
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					.Padding(FN2CSpacing::MD, 0.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("TagHint", "Open a Blueprint. Every graph shows this N2C bar at its bottom left: click the tag button to tag the graph (it then shows up in the list below), or click Translate. This card hides after the first saved translation."))
						.AutoWrapText(true)
						.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::TextSecondary))
					])
			]
		]
	];
}

SN2CGetStartedCard::~SN2CGetStartedCard()
{
	if (TranslationUpdatedHandle.IsValid())
	{
		UN2CGraphStateManager::Get().OnGraphTranslationUpdated.Remove(TranslationUpdatedHandle);
	}
}

bool SN2CGetStartedCard::IsShown() const
{
	return bAlwaysShow || ShouldShow(GetDefault<UN2CUserSettings>()->bShowGetStartedCard, SavedTranslationCount);
}

void SN2CGetStartedCard::Dismiss(bool bSave)
{
	UN2CUserSettings* UserSettings = GetMutableDefault<UN2CUserSettings>();
	UserSettings->bShowGetStartedCard = false;
	if (bSave)
	{
		// Per user: Saved/Config/<Platform>Editor/EditorPerProjectUserSettings.ini
		UserSettings->SaveConfig();
	}
}

TArray<EN2CLLMProvider> SN2CGetStartedCard::GetProviders()
{
	return { EN2CLLMProvider::Anthropic, EN2CLLMProvider::OpenAI, EN2CLLMProvider::Gemini, EN2CLLMProvider::DeepSeek,
		EN2CLLMProvider::Ollama, EN2CLLMProvider::LMStudio };
}

bool SN2CGetStartedCard::IsLocalProvider(EN2CLLMProvider Provider)
{
	return Provider == EN2CLLMProvider::Ollama || Provider == EN2CLLMProvider::LMStudio || Provider == EN2CLLMProvider::OpenAICompatible;
}

void SN2CGetStartedCard::HandleTranslationUpdated(const FN2CGraphKey& Graph)
{
	RefreshSavedTranslationCount();
}

void SN2CGetStartedCard::RefreshSavedTranslationCount()
{
	SavedTranslationCount = UN2CGraphStateManager::Get().GetGraphsWithTranslation().Num();
}

EN2CLLMProvider SN2CGetStartedCard::GetProvider() const
{
	return GetDefault<UN2CUserSettings>()->Provider;
}

FReply SN2CGetStartedCard::HandleSaveKeyClicked()
{
	const FString Key = KeyBox.IsValid() ? KeyBox->GetText().ToString().TrimStartAndEnd() : FString();
	if (Key.IsEmpty())
	{
		KeySaveMessage = TEXT("Paste a key first.");
		return FReply::Handled();
	}
	const bool bSaved = UN2CSettings::SetApiKey(GetProvider(), Key);
	KeySaveMessage = bSaved ? FString() : TEXT("The key could not be saved: see the Output Log.");
	if (KeyBox.IsValid())
	{
		KeyBox->SetText(FText::GetEmpty());
	}
	TestState = ETestState::None;
	return FReply::Handled();
}

FReply SN2CGetStartedCard::HandleTestClicked()
{
	TestState = ETestState::Running;
	TestMessage.Reset();
	TWeakPtr<SN2CGetStartedCard> WeakSelf = SharedThis(this);
	FN2CConnectionTest::Run(FN2CConnectionTest::FOnDone::CreateLambda([WeakSelf](const FN2CConnectionTestResult& Result)
	{
		if (const TSharedPtr<SN2CGetStartedCard> Self = WeakSelf.Pin())
		{
			Self->TestState = Result.bOk ? ETestState::Ok : ETestState::Failed;
			Self->TestMessage = Result.Message;
		}
	}));
	return FReply::Handled();
}

FReply SN2CGetStartedCard::HandleCloseClicked()
{
	Dismiss(true);
	return FReply::Handled();
}

FText SN2CGetStartedCard::GetKeyStatusText() const
{
	const EN2CLLMProvider Provider = GetProvider();
	if (IsLocalProvider(Provider))
	{
		return FText::Format(LOCTEXT("LocalNoKey", "{0} runs on your computer and needs no key: start it, then test the connection."),
			FText::FromString(UN2CLLMModule::GetProviderName(Provider)));
	}
	if (!KeySaveMessage.IsEmpty())
	{
		return FText::FromString(KeySaveMessage);
	}
	return FText::FromString(UN2CSettings::DescribeApiKeySource(Provider));
}

FText SN2CGetStartedCard::GetTestStatusText() const
{
	switch (TestState)
	{
	case ETestState::Running: return LOCTEXT("TestRunning", "Testing...");
	case ETestState::Ok:
	case ETestState::Failed:  return FText::FromString(TestMessage);
	default:                  return LOCTEXT("TestNone", "Not tested yet.");
	}
}

FSlateColor SN2CGetStartedCard::GetTestStatusColor() const
{
	switch (TestState)
	{
	case ETestState::Ok:     return N2C::UI::Linear(N2C::UI::Colors().AccentGreen);
	case ETestState::Failed: return N2C::UI::Linear(N2C::UI::Colors().AccentOrange);
	default:                 return N2C::UI::Linear(N2C::UI::Colors().TextMuted);
	}
}

#undef LOCTEXT_NAMESPACE
