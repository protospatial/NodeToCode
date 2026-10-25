// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/Widgets/SN2CContextWindowVisualizer.h"
#include "Core/Services/N2CTokenEstimationService.h"
#include "Core/N2CSettings.h"
#include "Models/N2CStyle.h"
#include "Core/Widgets/SN2CPanel.h"
#include "Core/N2CDesignTokens.h"
#include "LLM/N2CModelCatalog.h"
#include "LLM/N2CLLMModels.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateIconFinder.h"
#include "ISettingsModule.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "SN2CContextWindowVisualizer"

void SN2CContextWindowVisualizer::Construct(const FArguments& InArgs)
{
	// Initialize provider and model options
	PopulateProviderOptions();
	PopulateModelOptions();

	// Cache initial provider state
	const UN2CSettings* Settings = GetDefault<UN2CSettings>();
	CachedProvider = Settings->Provider;

	// Subscribe to model changes from the token estimation service
	FN2CTokenEstimationService& TokenService = FN2CTokenEstimationService::Get();
	ModelChangedHandle = TokenService.OnModelChanged.AddSP(this, &SN2CContextWindowVisualizer::OnModelChanged);

	ChildSlot
	[
		SNew(SN2CPanel)
		.Variant(EN2CPanelVariant::Light)
		.Padding(FN2CSpacing::MD)
		[
			SNew(SVerticalBox)

			// Provider/Model selection row
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, FN2CSpacing::SM)
			[
				SNew(SHorizontalBox)

				// Provider label and dropdown
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ProviderLabel", "Provider: "))
					.ColorAndOpacity(UIBind(&FN2CUIColors::TextSecondary))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, FN2CSpacing::LG, 0.0f)
				[
					SAssignNew(ProviderComboBox, SComboBox<TSharedPtr<FString>>)
					.ComboBoxStyle(&N2CStyle::GetComboBoxStyle())
					.ItemStyle(&N2CStyle::GetComboRowStyle())
					.OptionsSource(&ProviderOptions)
					.OnSelectionChanged(this, &SN2CContextWindowVisualizer::OnProviderSelectionChanged)
					.OnGenerateWidget(this, &SN2CContextWindowVisualizer::GenerateProviderWidget)
					.Content()
					[
						SNew(STextBlock)
						.Text(this, &SN2CContextWindowVisualizer::GetCurrentProviderText)
						.ColorAndOpacity(UIBind(&FN2CUIColors::AccentOrange))
					]
				]

				// Model label
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ModelLabel", "Model: "))
					.ColorAndOpacity(UIBind(&FN2CUIColors::TextSecondary))
				]

				// Model selection container (will hold either dropdown or text input)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, FN2CSpacing::MD, 0.0f)
				[
					SAssignNew(ModelSelectionContainer, SBox)
				]

				// Settings button
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(&N2CStyle::GetSimpleButtonStyle())
					.OnClicked(this, &SN2CContextWindowVisualizer::OnSettingsClicked)
					.ToolTipText(LOCTEXT("SettingsTooltip", "Open NodeToCode Settings"))
					.ContentPadding(FMargin(FN2CSpacing::XXS))
					[
						SNew(SImage)
						.Image(FSlateIcon(FName("CoreStyle"), "Icons.Toolbar.Settings").GetIcon())
						.ColorAndOpacity(UIBind(&FN2CUIColors::TextSecondary))
					]
				]
			]

			// Total token count row
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, FN2CSpacing::XXS)
			[
				SAssignNew(TokenCountTextBlock, STextBlock)
				.Text(this, &SN2CContextWindowVisualizer::GetTokenCountText)
				.ColorAndOpacity(UIBind(&FN2CUIColors::TextPrimary))
			]

			// Estimated batch cost row
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(CostTextBlock, STextBlock)
				.Text(this, &SN2CContextWindowVisualizer::GetEstimatedCostText)
				.ColorAndOpacity(UIBind(&FN2CUIColors::AccentGreen))
			]

			// Nested translation warning row (only visible when nesting is enabled)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, FN2CSpacing::SM, 0.0f, 0.0f)
			[
				SNew(SN2CPanel)
				.Visibility_Lambda([]() -> EVisibility
				{
					return FN2CTokenEstimationService::Get().IsNestedTranslationEnabled()
						? EVisibility::Visible
						: EVisibility::Collapsed;
				})
				.Variant(EN2CPanelVariant::Dark)
				.Padding(FMargin(FN2CSpacing::SM, FN2CSpacing::XS))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, FN2CSpacing::XS, 0.0f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("\u26A0"))) // Warning symbol
						.ColorAndOpacity(UIBind(&FN2CUIColors::AccentOrange))
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(this, &SN2CContextWindowVisualizer::GetNestingInfoText)
						.ColorAndOpacity(UIBind(&FN2CUIColors::AccentOrange))
						.AutoWrapText(true)
					]
				]
			]
		]
	];

	// Build the initial model selection widget
	RebuildModelSelectionWidget();
}

SN2CContextWindowVisualizer::~SN2CContextWindowVisualizer()
{
	// Unsubscribe from model changes
	if (ModelChangedHandle.IsValid())
	{
		FN2CTokenEstimationService::Get().OnModelChanged.Remove(ModelChangedHandle);
	}
}

void SN2CContextWindowVisualizer::UpdateForSelection(const TArray<FN2CTagInfo>& SelectedGraphs)
{
	TotalTokens = 0;
	TotalCost = 0.0f;
	TotalNestedGraphs = 0;
	SelectedGraphCount = SelectedGraphs.Num();

	if (SelectedGraphs.IsEmpty())
	{
		return;
	}

	FN2CTokenEstimationService& TokenService = FN2CTokenEstimationService::Get();

	// Accumulate token estimates for all selected graphs (with nesting if enabled)
	for (const FN2CTagInfo& GraphInfo : SelectedGraphs)
	{
		int32 NestedCount = 0;
		int32 GraphTokens = TokenService.GetTokenEstimateWithNesting(GraphInfo, NestedCount);
		TotalTokens += GraphTokens;
		TotalNestedGraphs += NestedCount;
	}

	// Calculate total cost
	TotalCost = TokenService.CalculateCost(TotalTokens);
}

void SN2CContextWindowVisualizer::OnModelChanged()
{
	// Model changed, force repaint to update displayed values
	Invalidate(EInvalidateWidgetReason::Paint);
}

// ==================== Provider/Model Selection ====================

void SN2CContextWindowVisualizer::PopulateProviderOptions()
{
	ProviderOptions.Empty();

	// Add all providers
	ProviderOptions.Add(MakeShared<FString>(TEXT("OpenAI")));
	ProviderOptions.Add(MakeShared<FString>(TEXT("Anthropic")));
	ProviderOptions.Add(MakeShared<FString>(TEXT("Gemini")));
	ProviderOptions.Add(MakeShared<FString>(TEXT("DeepSeek")));
	ProviderOptions.Add(MakeShared<FString>(TEXT("Ollama")));
	ProviderOptions.Add(MakeShared<FString>(TEXT("LM Studio")));
}

void SN2CContextWindowVisualizer::PopulateModelOptions()
{
	ModelOptions.Empty();

	const UN2CSettings* Settings = GetDefault<UN2CSettings>();

	// The provider's live catalog rows (retired ones are not offered); Ollama and LM Studio have none (text input)
	for (const FN2CModelMetadata* Row : FN2CModelCatalog::Get().List(FN2CModelCatalog::ProviderIdFor(Settings->Provider)))
	{
		ModelOptions.Add(MakeShared<FString>(Row->DisplayName));
	}

	// Refresh combo box if it exists
	if (ModelComboBox.IsValid())
	{
		ModelComboBox->RefreshOptions();
	}
}

void SN2CContextWindowVisualizer::RebuildModelSelectionWidget()
{
	if (!ModelSelectionContainer.IsValid())
	{
		return;
	}

	const UN2CSettings* Settings = GetDefault<UN2CSettings>();
	const bool bIsLocal = IsLocalProvider();

	if (bIsLocal)
	{
		// For local providers, use text input
		ModelComboBox.Reset();
		ModelSelectionContainer->SetContent(
			SAssignNew(LocalModelTextBox, SEditableTextBox)
			.Style(&N2CStyle::GetEditableTextBoxStyle())
			.Text(this, &SN2CContextWindowVisualizer::GetLocalModelText)
			.OnTextCommitted(this, &SN2CContextWindowVisualizer::OnLocalModelTextCommitted)
			.MinDesiredWidth(150.0f)
			.SelectAllTextWhenFocused(true)
		);
	}
	else
	{
		// For cloud providers, use dropdown
		LocalModelTextBox.Reset();
		PopulateModelOptions();
		ModelSelectionContainer->SetContent(
			SAssignNew(ModelComboBox, SComboBox<TSharedPtr<FString>>)
			.ComboBoxStyle(&N2CStyle::GetComboBoxStyle())
			.ItemStyle(&N2CStyle::GetComboRowStyle())
			.OptionsSource(&ModelOptions)
			.OnSelectionChanged(this, &SN2CContextWindowVisualizer::OnModelSelectionChanged)
			.OnGenerateWidget(this, &SN2CContextWindowVisualizer::GenerateModelWidget)
			.Content()
			[
				SNew(STextBlock)
				.Text(this, &SN2CContextWindowVisualizer::GetModelNameText)
				.ColorAndOpacity(UIBind(&FN2CUIColors::AccentOrange))
			]
		);
	}
}

void SN2CContextWindowVisualizer::OnProviderSelectionChanged(TSharedPtr<FString> NewValue, ESelectInfo::Type SelectInfo)
{
	if (!NewValue.IsValid() || SelectInfo == ESelectInfo::Direct)
	{
		return;
	}

	UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();

	// Map string to enum
	EN2CLLMProvider NewProvider = EN2CLLMProvider::Anthropic;
	if (*NewValue == TEXT("OpenAI"))
	{
		NewProvider = EN2CLLMProvider::OpenAI;
	}
	else if (*NewValue == TEXT("Anthropic"))
	{
		NewProvider = EN2CLLMProvider::Anthropic;
	}
	else if (*NewValue == TEXT("Gemini"))
	{
		NewProvider = EN2CLLMProvider::Gemini;
	}
	else if (*NewValue == TEXT("DeepSeek"))
	{
		NewProvider = EN2CLLMProvider::DeepSeek;
	}
	else if (*NewValue == TEXT("Ollama"))
	{
		NewProvider = EN2CLLMProvider::Ollama;
	}
	else if (*NewValue == TEXT("LM Studio"))
	{
		NewProvider = EN2CLLMProvider::LMStudio;
	}

	// Check if provider actually changed
	if (Settings->Provider == NewProvider)
	{
		return;
	}

	Settings->Provider = NewProvider;
	CachedProvider = NewProvider;

	// Rebuild model selection widget (dropdown vs text input)
	RebuildModelSelectionWidget();

	// Save and notify
	SaveSettings();
	Settings->NotifyModelSettingsChanged();
}

void SN2CContextWindowVisualizer::OnModelSelectionChanged(TSharedPtr<FString> NewValue, ESelectInfo::Type SelectInfo)
{
	if (!NewValue.IsValid() || SelectInfo == ESelectInfo::Direct)
	{
		return;
	}

	UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
	const FString& SelectedDisplayName = *NewValue;

	// The catalog row with that display name sets the model ID (and the DeepSeek thinking mode its entry names)
	for (const FN2CModelMetadata* Row : FN2CModelCatalog::Get().List(FN2CModelCatalog::ProviderIdFor(Settings->Provider)))
	{
		if (Row->DisplayName == SelectedDisplayName)
		{
			Settings->SelectModel(*Row);
			break;
		}
	}

	// Save and notify
	SaveSettings();
	Settings->NotifyModelSettingsChanged();
}

void SN2CContextWindowVisualizer::OnLocalModelTextCommitted(const FText& NewText, ETextCommit::Type CommitType)
{
	if (CommitType == ETextCommit::OnCleared)
	{
		return;
	}

	UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
	const FString ModelName = NewText.ToString();

	switch (Settings->Provider)
	{
	case EN2CLLMProvider::Ollama:
		Settings->OllamaModel = ModelName;
		break;
	case EN2CLLMProvider::LMStudio:
		Settings->LMStudioModel = ModelName;
		break;
	default:
		break;
	}

	// Save and notify
	SaveSettings();
	Settings->NotifyModelSettingsChanged();
}

TSharedRef<SWidget> SN2CContextWindowVisualizer::GenerateProviderWidget(TSharedPtr<FString> Item)
{
	return SNew(STextBlock)
		.Text(FText::FromString(*Item))
		.ColorAndOpacity(UIBind(&FN2CUIColors::TextPrimary))
		.Margin(FMargin(FN2CSpacing::XS, FN2CSpacing::XXS));
}

TSharedRef<SWidget> SN2CContextWindowVisualizer::GenerateModelWidget(TSharedPtr<FString> Item)
{
	return SNew(STextBlock)
		.Text(FText::FromString(*Item))
		.ColorAndOpacity(UIBind(&FN2CUIColors::TextPrimary))
		.Margin(FMargin(FN2CSpacing::XS, FN2CSpacing::XXS));
}

FText SN2CContextWindowVisualizer::GetCurrentProviderText() const
{
	const UN2CSettings* Settings = GetDefault<UN2CSettings>();

	switch (Settings->Provider)
	{
	case EN2CLLMProvider::OpenAI:
		return FText::FromString(TEXT("OpenAI"));
	case EN2CLLMProvider::Anthropic:
		return FText::FromString(TEXT("Anthropic"));
	case EN2CLLMProvider::Gemini:
		return FText::FromString(TEXT("Gemini"));
	case EN2CLLMProvider::DeepSeek:
		return FText::FromString(TEXT("DeepSeek"));
	case EN2CLLMProvider::Ollama:
		return FText::FromString(TEXT("Ollama"));
	case EN2CLLMProvider::LMStudio:
		return FText::FromString(TEXT("LM Studio"));
	default:
		return FText::FromString(TEXT("Unknown"));
	}
}

FText SN2CContextWindowVisualizer::GetLocalModelText() const
{
	const UN2CSettings* Settings = GetDefault<UN2CSettings>();

	switch (Settings->Provider)
	{
	case EN2CLLMProvider::Ollama:
		return FText::FromString(Settings->OllamaModel);
	case EN2CLLMProvider::LMStudio:
		return FText::FromString(Settings->LMStudioModel);
	default:
		return FText::GetEmpty();
	}
}

bool SN2CContextWindowVisualizer::IsLocalProvider() const
{
	const UN2CSettings* Settings = GetDefault<UN2CSettings>();
	return Settings->Provider == EN2CLLMProvider::Ollama ||
		   Settings->Provider == EN2CLLMProvider::LMStudio;
}

FReply SN2CContextWindowVisualizer::OnSettingsClicked()
{
	// Open Project Settings to NodeToCode section
	// Section name is the class name without 'U' prefix (N2CSettings)
	ISettingsModule& SettingsModule = FModuleManager::LoadModuleChecked<ISettingsModule>("Settings");
	SettingsModule.ShowViewer("Project", "Plugins", "N2CSettings");

	return FReply::Handled();
}

void SN2CContextWindowVisualizer::SaveSettings()
{
	UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
	const FString ConfigPath = Settings->GetDefaultConfigFilename();
	Settings->SaveConfig();
}

// ==================== Display Text Getters ====================

FText SN2CContextWindowVisualizer::GetTokenCountText() const
{
	if (SelectedGraphCount == 0)
	{
		return LOCTEXT("SelectGraphsHint", "Select graphs to estimate tokens");
	}

	// Format token count with commas
	FString TokensStr;
	{
		FString Result = FString::Printf(TEXT("%d"), TotalTokens);
		int32 InsertPosition = Result.Len() - 3;
		while (InsertPosition > 0)
		{
			Result.InsertAt(InsertPosition, TEXT(","));
			InsertPosition -= 3;
		}
		TokensStr = Result;
	}

	if (SelectedGraphCount == 1)
	{
		return FText::Format(
			LOCTEXT("TotalTokensSingle", "Total for selection: {0} tokens"),
			FText::FromString(TokensStr)
		);
	}

	return FText::Format(
		LOCTEXT("TotalTokensMultiple", "Total for {0} graphs: {1} tokens"),
		FText::AsNumber(SelectedGraphCount),
		FText::FromString(TokensStr)
	);
}

FText SN2CContextWindowVisualizer::GetEstimatedCostText() const
{
	FN2CTokenEstimationService& TokenService = FN2CTokenEstimationService::Get();

	if (TokenService.IsLocalProvider())
	{
		return LOCTEXT("LocalModelCost", "Estimated batch cost: Free (local model)");
	}

	if (TotalTokens == 0)
	{
		return LOCTEXT("NoCost", "Estimated batch cost: $0.00");
	}

	// Create formatting options for currency display
	FNumberFormattingOptions CostFormatOptions;
	CostFormatOptions.SetUseGrouping(true);
	CostFormatOptions.SetMinimumFractionalDigits(2);
	CostFormatOptions.SetMaximumFractionalDigits(4);

	return FText::Format(
		LOCTEXT("EstimatedCostFormat", "Estimated batch cost: ${0}"),
		FText::AsNumber(TotalCost, &CostFormatOptions)
	);
}

FText SN2CContextWindowVisualizer::GetModelNameText() const
{
	return FText::FromString(FN2CTokenEstimationService::Get().GetCurrentModelName());
}

FText SN2CContextWindowVisualizer::GetNestingInfoText() const
{
	FN2CTokenEstimationService& TokenService = FN2CTokenEstimationService::Get();
	int32 Depth = TokenService.GetTranslationDepth();

	FString InfoStr = FString::Printf(
		TEXT("Nested Translation: Depth %d"),
		Depth
	);

	if (TotalNestedGraphs > 0)
	{
		InfoStr += FString::Printf(TEXT(" (%d nested graph(s) included)"), TotalNestedGraphs);
	}

	InfoStr += TEXT("\nToken estimates include referenced user functions. This can significantly increase costs.");

	return FText::FromString(InfoStr);
}

#undef LOCTEXT_NAMESPACE
