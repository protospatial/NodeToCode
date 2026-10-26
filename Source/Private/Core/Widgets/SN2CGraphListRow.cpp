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
#include "Styling/AppStyle.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Models/N2CLogging.h"
#include "Core/Style/N2CUIColors.h"
#include "Core/Style/N2CStyle.h"
#include "Core/Widgets/SN2CIconButton.h"
#include "Core/Style/N2CDesignTokens.h"

#define LOCTEXT_NAMESPACE "SN2CGraphListRow"

void SN2CGraphListRow::Construct(const FArguments& InArgs)
{
	Item = InArgs._Item;
	OnCheckboxChangedDelegate = InArgs._OnCheckboxChanged;
	OnTranslateClickedDelegate = InArgs._OnTranslateClicked;
	OnJsonExportClickedDelegate = InArgs._OnJsonExportClicked;
	OnViewTranslationClickedDelegate = InArgs._OnViewTranslationClicked;
	OnDoubleClickedDelegate = InArgs._OnDoubleClicked;
	OnRefreshClickedDelegate = InArgs._OnRefreshClicked;

	if (!Item.IsValid())
	{
		UE_LOG(LogNodeToCode, Warning, TEXT("[SN2CGraphListRow] Construct called with invalid Item"));
		return;
	}

	ChildSlot
	[
		// Use SOverlay to layer buttons on top of content
		SNew(SOverlay)

		// Layer 0: Background and main content
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			// Use a solid brush that will show the background color
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(this, &SN2CGraphListRow::GetBackgroundColor)
			.Padding(FMargin(0.0f, 2.0f))
			[
				SNew(SHorizontalBox)

				// Checkbox column - fixed width to match header (30px)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FN2CSpacing::XS, 0.0f)
				[
					SNew(SBox)
					.WidthOverride(22.0f)
					[
						SNew(SCheckBox)
						.Style(&N2CStyle::GetCheckBoxStyle())
						.IsChecked(this, &SN2CGraphListRow::GetCheckboxState)
						.OnCheckStateChanged(this, &SN2CGraphListRow::OnCheckboxStateChanged)
					]
				]

				// Graph name column - fill width
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				.Padding(FN2CSpacing::XS, 0.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Item->TagInfo.GraphName))
					.Font(FN2CFonts::Small())
					.ColorAndOpacity(FSlateColor::UseForeground())
				]

				// Blueprint name column - fill width
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				.Padding(FN2CSpacing::XS, 0.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Item->GetBlueprintDisplayName()))
					.Font(FN2CFonts::Small())
					.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::AccentBlue)) // Blueprint blue
					.ToolTipText(FText::FromString(Item->TagInfo.BlueprintPath))
				]

				// Context column - fixed width with circular progress, tokens, and cost
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FN2CSpacing::XS, 0.0f)
				[
					SNew(SBox)
					.WidthOverride(120.0f)
					[
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
						]
					]
				]
			]
		]

		// Layer 1: Action buttons overlay (over Blueprint column, visible on hover)
		+ SOverlay::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Fill)
		.Padding(FMargin(0.0f, 0.0f, 128.0f, 0.0f)) // Offset past Context column
		[
			SNew(SBorder)
			.BorderImage(GetButtonOverlayBrush())
			.BorderBackgroundColor(this, &SN2CGraphListRow::GetButtonBackgroundColor)
			.Padding(FMargin(0.0f, 2.0f))
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
		]
	];
}

void SN2CGraphListRow::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);

	if (!bIsHovered)
	{
		bIsHovered = true;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void SN2CGraphListRow::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseLeave(MouseEvent);

	if (bIsHovered)
	{
		bIsHovered = false;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

FReply SN2CGraphListRow::OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		OnDoubleClickedDelegate.ExecuteIfBound(Item);
		return FReply::Handled();
	}
	return FReply::Unhandled();
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
	if (Item->bIsSelected && bIsHovered)
	{
		return FSlateColor(N2C::UI::LinearAlpha(N2C::UI::Colors().AccentGold, 0.3f));
	}
	// Selected: Normal gold
	if (Item->bIsSelected)
	{
		return FSlateColor(N2C::UI::LinearAlpha(N2C::UI::Colors().AccentGold, 0.2f));
	}
	// Hovered: Subtle gray
	if (bIsHovered)
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
	return bIsHovered ? EVisibility::Visible : EVisibility::Collapsed;
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
