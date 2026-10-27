// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/Widgets/SN2CGraphListRow.h"
#include "Core/Widgets/SN2CCircularProgressBar.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SNullWidget.h"
#include "Styling/AppStyle.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Models/N2CLogging.h"
#include "Core/Style/N2CUIColors.h"
#include "Core/Style/N2CStyle.h"
#include "Core/Widgets/SN2CIconButton.h"
#include "Core/Style/N2CDesignTokens.h"

#define LOCTEXT_NAMESPACE "SN2CGraphListRow"

const FName SN2CGraphListRow::Column_Checkbox = TEXT("Checkbox");
const FName SN2CGraphListRow::Column_GraphName = TEXT("GraphName");
const FName SN2CGraphListRow::Column_Blueprint = TEXT("Blueprint");
const FName SN2CGraphListRow::Column_Context = TEXT("Context");

void SN2CGraphListRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable)
{
	Item = InArgs._Item;
	OnCheckboxChangedDelegate = InArgs._OnCheckboxChanged;
	OnTranslateClickedDelegate = InArgs._OnTranslateClicked;
	OnJsonExportClickedDelegate = InArgs._OnJsonExportClicked;
	OnViewTranslationClickedDelegate = InArgs._OnViewTranslationClicked;
	OnRefreshClickedDelegate = InArgs._OnRefreshClicked;

	if (!Item.IsValid())
	{
		UE_LOG(LogNodeToCode, Warning, TEXT("[SN2CGraphListRow] Construct called with invalid Item"));
	}

	// One cell per header column (GenerateWidgetForColumn), so the cells follow the header's sizes (card 0267)
	SMultiColumnTableRow<TSharedPtr<FN2CGraphListItem>>::Construct(
		FSuperRowType::FArguments()
			.Style(&N2CStyle::GetTableRowStyle())
			.Padding(0),
		OwnerTable);
}

TSharedRef<SWidget> SN2CGraphListRow::MakeCell(const TSharedRef<SWidget>& CellContent, bool bFill)
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(this, &SN2CGraphListRow::GetBackgroundColor)
		.Padding(FMargin(FN2CSpacing::XS, 2.0f))
		.VAlign(VAlign_Center)
		.HAlign(bFill ? HAlign_Fill : HAlign_Left)
		[
			CellContent
		];
}

TSharedRef<SWidget> SN2CGraphListRow::GenerateWidgetForColumn(const FName& ColumnName)
{
	if (!Item.IsValid())
	{
		return SNullWidget::NullWidget;
	}

	if (ColumnName == Column_Checkbox)
	{
		return MakeCell(
			SNew(SCheckBox)
			.Style(&N2CStyle::GetCheckBoxStyle())
			.IsChecked(this, &SN2CGraphListRow::GetCheckboxState)
			.OnCheckStateChanged(this, &SN2CGraphListRow::OnCheckboxStateChanged));
	}

	if (ColumnName == Column_GraphName)
	{
		return MakeCell(
			SNew(STextBlock)
			.Text(FText::FromString(Item->TagInfo.GraphName))
			.Font(FN2CFonts::Small())
			.ColorAndOpacity(FSlateColor::UseForeground())
			.ToolTipText(this, &SN2CGraphListRow::GetTagsTooltip));
	}

	if (ColumnName == Column_Blueprint)
	{
		// The Blueprint name, with the row's actions over its right end while the row is hovered
		return MakeCell(
			SNew(SOverlay)
			+ SOverlay::Slot()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Item->GetBlueprintDisplayName()))
				.Font(FN2CFonts::Small())
				.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::AccentBlue)) // Blueprint blue
				.ToolTipText(FText::FromString(Item->TagInfo.BlueprintPath))
			]
			+ SOverlay::Slot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				SNew(SBorder)
				.BorderImage(GetButtonOverlayBrush())
				.BorderBackgroundColor(this, &SN2CGraphListRow::GetButtonBackgroundColor)
				.Padding(FMargin(0.0f, 0.0f))
				.Visibility(this, &SN2CGraphListRow::GetActionButtonsVisibility)
				[
					SNew(SHorizontalBox)

					// Translate button
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(FN2CSpacing::XXS, 0.0f)
					[
						SNew(SN2CIconButton)
						.IconBrush(FAppStyle::GetBrush("Icons.Convert"))
						.IconSize(FVector2D(FN2CSizing::IconSM, FN2CSizing::IconSM))
						.ButtonVariant(EN2CButtonVariant::Simple)
						.ContentPadding(FMargin(FN2CSpacing::XS, FN2CSpacing::XXS))
						.ToolTipText(LOCTEXT("TranslateTooltip", "Translate this graph"))
						.OnClicked(this, &SN2CGraphListRow::HandleTranslateClicked)
					]

					// JSON Export button
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(FN2CSpacing::XXS, 0.0f)
					[
						SNew(SN2CIconButton)
						.IconBrush(FAppStyle::GetBrush("MainFrame.RefreshSourceCodeEditor"))
						.IconSize(FVector2D(FN2CSizing::IconSM, FN2CSizing::IconSM))
						.ButtonVariant(EN2CButtonVariant::Simple)
						.ContentPadding(FMargin(FN2CSpacing::XS, FN2CSpacing::XXS))
						.ToolTipText(LOCTEXT("JsonExportTooltip", "Export as JSON"))
						.OnClicked(this, &SN2CGraphListRow::HandleJsonExportClicked)
					]

					// View Translation button
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(FN2CSpacing::XXS, 0.0f)
					[
						SNew(SN2CIconButton)
						.IconBrush(FAppStyle::GetBrush("Icons.OpenInExternalEditor"))
						.IconSize(FVector2D(FN2CSizing::IconSM, FN2CSizing::IconSM))
						.ButtonVariant(EN2CButtonVariant::Simple)
						.ContentPadding(FMargin(FN2CSpacing::XS, FN2CSpacing::XXS))
						.IsEnabled(this, &SN2CGraphListRow::IsViewButtonEnabled)
						.ToolTipText(TAttribute<FText>::CreateSP(this, &SN2CGraphListRow::GetViewButtonTooltip))
						.OnClicked(this, &SN2CGraphListRow::HandleViewTranslationClicked)
					]
				]
			]);
	}

	if (ColumnName == Column_Context)
	{
		return MakeCell(
			SNew(SHorizontalBox)

			// Circular progress bar (clickable for refresh)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, FN2CSpacing::XS, 0.0f)
			[
				SNew(SN2CCircularProgressBar)
				.Percent(this, &SN2CGraphListRow::GetContextUsagePercent)
				.Radius(7.0f)
				.Thickness(2.5f)
				.OnClicked(this, &SN2CGraphListRow::HandleRefreshClicked)
				.ToolTipText(LOCTEXT("RefreshTooltip", "Click to refresh estimate"))
			]

			// Token count
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, FN2CSpacing::XS, 0.0f)
			[
				SNew(STextBlock)
				.Text(this, &SN2CGraphListRow::GetTokensText)
				.Font(FN2CFonts::Tiny())
				.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::TextMuted))
			]

			// Cost
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SN2CGraphListRow::GetCostText)
				.Font(FN2CFonts::Tiny())
				.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::AccentGreen)) // Green for cost
			]);
	}

	return SNullWidget::NullWidget;
}

void SN2CGraphListRow::UpdateVisuals()
{
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SN2CGraphListRow::OnCheckboxStateChanged(ECheckBoxState NewState)
{
	if (Item.IsValid())
	{
		Item->bIsSelected = (NewState == ECheckBoxState::Checked);
		OnCheckboxChangedDelegate.ExecuteIfBound(Item);
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

FReply SN2CGraphListRow::HandleTranslateClicked()
{
	if (Item.IsValid())
	{
		UE_LOG(LogNodeToCode, Log, TEXT("[SN2CGraphListRow] Translate clicked for graph: %s"), *Item->TagInfo.GraphName);
		OnTranslateClickedDelegate.ExecuteIfBound(Item);
	}
	return FReply::Handled();
}

FReply SN2CGraphListRow::HandleJsonExportClicked()
{
	if (Item.IsValid())
	{
		UE_LOG(LogNodeToCode, Log, TEXT("[SN2CGraphListRow] JSON Export clicked for graph: %s"), *Item->TagInfo.GraphName);
		OnJsonExportClickedDelegate.ExecuteIfBound(Item);
	}
	return FReply::Handled();
}

FReply SN2CGraphListRow::HandleViewTranslationClicked()
{
	if (Item.IsValid())
	{
		UE_LOG(LogNodeToCode, Log, TEXT("[SN2CGraphListRow] View Translation clicked for graph: %s"), *Item->TagInfo.GraphName);
		OnViewTranslationClickedDelegate.ExecuteIfBound(Item);
	}
	return FReply::Handled();
}

FSlateColor SN2CGraphListRow::GetBackgroundColor() const
{
	if (!Item.IsValid())
	{
		return FSlateColor(FLinearColor::Transparent);
	}

	// Selected + Hovered: Brighter gold
	const bool bHovered = IsHovered();
	if (Item->bIsSelected && bHovered)
	{
		return FSlateColor(N2C::UI::LinearAlpha(N2C::UI::Colors().AccentGold, 0.3f));
	}
	// Selected: Normal gold
	if (Item->bIsSelected)
	{
		return FSlateColor(N2C::UI::LinearAlpha(N2C::UI::Colors().AccentGold, 0.2f));
	}
	// Hovered: Subtle gray
	if (bHovered)
	{
		return FSlateColor(N2C::UI::LinearAlpha(N2C::UI::Colors().TextMuted, 0.15f));
	}
	// Normal: Transparent
	return FSlateColor(FLinearColor::Transparent);
}

const FSlateBrush* SN2CGraphListRow::GetButtonOverlayBrush()
{
	static FSlateRoundedBoxBrush Brush(FLinearColor::White, 4.0f);
	return &Brush;
}

FSlateColor SN2CGraphListRow::GetButtonBackgroundColor() const
{
	// Use a semi-transparent dark background so buttons are visible over text
	// Match the row background when selected, otherwise use dark gray
	if (Item.IsValid() && Item->bIsSelected)
	{
		return FSlateColor(N2C::UI::LinearAlpha(N2C::UI::Colors().BgButtonDarkSelected, 0.95f));
	}
	return FSlateColor(N2C::UI::LinearAlpha(N2C::UI::Colors().BgButtonDark, 0.9f));
}

EVisibility SN2CGraphListRow::GetActionButtonsVisibility() const
{
	return IsHovered() ? EVisibility::Visible : EVisibility::Collapsed;
}

ECheckBoxState SN2CGraphListRow::GetCheckboxState() const
{
	if (Item.IsValid() && Item->bIsSelected)
	{
		return ECheckBoxState::Checked;
	}
	return ECheckBoxState::Unchecked;
}

bool SN2CGraphListRow::IsViewButtonEnabled() const
{
	return Item.IsValid() && Item->bHasTranslation;
}

FText SN2CGraphListRow::GetViewButtonTooltip() const
{
	if (Item.IsValid() && Item->bHasTranslation)
	{
		return LOCTEXT("ViewTranslationTooltipEnabled", "View translation");
	}
	return LOCTEXT("ViewTranslationTooltipDisabled", "No translation available");
}

FText SN2CGraphListRow::GetTagsTooltip() const
{
	if (!Item.IsValid() || Item->Tags.Num() == 0)
	{
		return FText::GetEmpty();
	}
	TArray<FString> Labels;
	for (const FN2CTagInfo& TagEntry : Item->Tags)
	{
		Labels.AddUnique(FString::Printf(TEXT("%s (%s)"), *TagEntry.Tag, *TagEntry.Category));
	}
	return FText::Format(LOCTEXT("GraphTagsTooltip", "Tags: {0}"), FText::FromString(FString::Join(Labels, TEXT(", "))));
}

void SN2CGraphListRow::HandleRefreshClicked()
{
	if (Item.IsValid())
	{
		UE_LOG(LogNodeToCode, Log, TEXT("[SN2CGraphListRow] Refresh clicked for graph: %s"), *Item->TagInfo.GraphName);
		OnRefreshClickedDelegate.ExecuteIfBound(Item);
	}
}

float SN2CGraphListRow::GetContextUsagePercent() const
{
	if (Item.IsValid() && Item->bTokenEstimateValid)
	{
		return Item->ContextUsagePercent;
	}
	return 0.0f;
}

FText SN2CGraphListRow::GetTokensText() const
{
	if (Item.IsValid())
	{
		return FText::FromString(Item->GetFormattedTokens());
	}
	return FText::FromString(TEXT("-"));
}

FText SN2CGraphListRow::GetCostText() const
{
	if (Item.IsValid())
	{
		return FText::FromString(Item->GetFormattedCost());
	}
	return FText::FromString(TEXT("-"));
}

#undef LOCTEXT_NAMESPACE
