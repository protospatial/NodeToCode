// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/Widgets/SN2CHeaderBar.h"
#include "Core/Widgets/SN2CPanel.h"
#include "Core/Widgets/SN2CIconButton.h"
#include "Core/Style/N2CUIColors.h"
#include "Core/Style/N2CDesignTokens.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "SN2CHeaderBar"

void SN2CHeaderBar::Construct(const FArguments& InArgs)
{
	TSharedRef<SHorizontalBox> HeaderBox = SNew(SHorizontalBox)
		// Title
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SAssignNew(TitleTextBlock, STextBlock)
			.Text(InArgs._Title)
			.Font(FN2CFonts::LargeBold())
			.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::TextPrimary))
		];

	// Optional right content slot
	if (InArgs._RightContent.Widget != SNullWidget::NullWidget)
	{
		HeaderBox->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FN2CSpacing::MD, 0.0f, 0.0f, 0.0f)
			[
				InArgs._RightContent.Widget
			];
	}

	// Optional close button
	if (InArgs._ShowCloseButton)
	{
		HeaderBox->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SN2CIconButton)
				.ButtonVariant(EN2CButtonVariant::NoBorder)
				.ContentPadding(FMargin(4.0f))
				.Text(FText::FromString(TEXT("\u2715"))) // X character
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 18))
				.TextColor(N2C::UI::Bind(&FN2CUIColors::TextMuted))
				.OnClicked(InArgs._OnClose)
				.ToolTipText(LOCTEXT("CloseTooltip", "Close"))
			];
	}

	ChildSlot
	[
		SNew(SN2CPanel)
		.Variant(EN2CPanelVariant::Dark)
		.Padding(InArgs._Padding)
		[
			HeaderBox
		]
	];
}

void SN2CHeaderBar::SetTitle(const FText& NewTitle)
{
	if (TitleTextBlock.IsValid())
	{
		TitleTextBlock->SetText(NewTitle);
	}
}

#undef LOCTEXT_NAMESPACE
