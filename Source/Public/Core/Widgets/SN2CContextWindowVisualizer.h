// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Core/N2CTagTypes.h"
#include "LLM/N2CLLMTypes.h"
#include "Core/Services/N2CTokenEstimationService.h"

class STextBlock;
class SButton;

/**
 * Widget that displays context window summary information.
 * Shows model name, total tokens for selection, and estimated batch cost.
 *
 * Note: Per-graph context visualization is now handled by SN2CGraphListRow
 * with circular progress bars. This widget provides the batch summary.
 */
class NODETOCODE_API SN2CContextWindowVisualizer : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SN2CContextWindowVisualizer)
	{}
	SLATE_END_ARGS()

	/** Constructs this widget */
	void Construct(const FArguments& InArgs);

	/** Destructor - cleans up delegate bindings */
	virtual ~SN2CContextWindowVisualizer();

	/**
	 * Update the display based on selected graphs
	 * @param SelectedGraphs Array of selected graph tag infos
	 */
	void UpdateForSelection(const TArray<FN2CTagInfo>& SelectedGraphs);

	/**
	 * Saves settings the quick picker changed (provider, model) through UN2CSettings::SaveToProjectConfig, the path
	 * Project Settings edits take, so a later Project Settings edit is not overridden at the next start
	 */
	static void SaveSettings(TConstArrayView<FName> PropertyNames);

private:
	/** Get total token count display text */
	FText GetTokenCountText() const;

	/** Get estimated cost display text */
	FText GetEstimatedCostText() const;

	/** Get model name display text */
	FText GetModelNameText() const;

	/** Get nesting info display text */
	FText GetNestingInfoText() const;

	/** Handle model changes from the token estimation service */
	void OnModelChanged();

	// ==================== Provider/Model Selection ====================

	/** Populate the provider options array */
	void PopulateProviderOptions();

	/** Populate the model options array based on current provider */
	void PopulateModelOptions();

	/** Rebuild the model selection widget (dropdown vs text input) */
	void RebuildModelSelectionWidget();

	/** Handle provider selection change */
	void OnProviderSelectionChanged(TSharedPtr<FString> NewValue, ESelectInfo::Type SelectInfo);

	/** Handle model selection change (dropdown) */
	void OnModelSelectionChanged(TSharedPtr<FString> NewValue, ESelectInfo::Type SelectInfo);

	/** Handle model text committed (text input for local providers) */
	void OnLocalModelTextCommitted(const FText& NewText, ETextCommit::Type CommitType);

	/** Generate widget for provider dropdown item */
	TSharedRef<SWidget> GenerateProviderWidget(TSharedPtr<FString> Item);

	/** Generate widget for model dropdown item */
	TSharedRef<SWidget> GenerateModelWidget(TSharedPtr<FString> Item);

	/** Get current provider display text */
	FText GetCurrentProviderText() const;

	/** Get current model display text for local providers */
	FText GetLocalModelText() const;

	/** Check if current provider is a local provider (Ollama/LM Studio) */
	bool IsLocalProvider() const;

	/** Handle settings button click */
	FReply OnSettingsClicked();

	// UI Elements
	TSharedPtr<STextBlock> ModelNameText;
	TSharedPtr<STextBlock> TokenCountTextBlock;
	TSharedPtr<STextBlock> CostTextBlock;

	// Provider/Model selection widgets
	TSharedPtr<SComboBox<TSharedPtr<FString>>> ProviderComboBox;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> ModelComboBox;
	TSharedPtr<SEditableTextBox> LocalModelTextBox;
	TSharedPtr<SBox> ModelSelectionContainer;

	// Provider/Model option arrays
	TArray<TSharedPtr<FString>> ProviderOptions;
	TArray<TSharedPtr<FString>> ModelOptions;

	// Cached state for display
	int32 TotalTokens = 0;
	float TotalCost = 0.0f;

	/** Input and output tokens and prices of the selection (card 0220) */
	FN2CCostEstimate TotalEstimate;

	/** The selection UpdateForSelection was last given, so a model, price or provider change reprices it */
	TArray<FN2CTagInfo> LastSelection;
	int32 SelectedGraphCount = 0;
	int32 TotalNestedGraphs = 0;

	// Cached provider for detecting changes
	EN2CLLMProvider CachedProvider = EN2CLLMProvider::Anthropic;

	// Delegate handle for model changes
	FDelegateHandle ModelChangedHandle;
};
