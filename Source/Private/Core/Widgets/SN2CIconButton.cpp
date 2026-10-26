// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/Widgets/SN2CIconButton.h"
#include "Core/Style/N2CDesignTokens.h"
#include "Core/Style/N2CStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"

void SN2CIconButton::Construct(const FArguments& InArgs)
{
	// Select button style
	const FButtonStyle* ButtonStyle = nullptr;
	switch (InArgs._ButtonVariant)
	{
	case EN2CButtonVariant::Standard: ButtonStyle = &N2CStyle::GetButtonStyle(); break;
	case EN2CButtonVariant::Simple:   ButtonStyle = &N2CStyle::GetSimpleButtonStyle(); break;
	case EN2CButtonVariant::NoBorder: ButtonStyle = &N2CStyle::GetNoBorderStyle(); break;
	}

	const bool bHasIcon = (InArgs._IconBrush != nullptr);
	const bool bHasText = InArgs._Text.IsSet() || InArgs._Text.IsBound();

	// Build content
	TSharedRef<SWidget> Content = SNullWidget::NullWidget;

	if (bHasIcon && bHasText)
	{
		// Icon + Text layout
		TSharedRef<SHorizontalBox> HBox = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SImage)
				.Image(InArgs._IconBrush)
				.DesiredSizeOverride(InArgs._IconSize)
				.ColorAndOpacity(InArgs._IconColor)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(InArgs._Text)
				.Font(InArgs._Font.IsSet() ? InArgs._Font.GetValue() : FN2CFonts::Regular())
				.ColorAndOpacity(InArgs._TextColor)
			];
		Content = HBox;
	}
	else if (bHasIcon)
	{
		// Icon-only layout
		Content = SNew(SImage)
			.Image(InArgs._IconBrush)
			.DesiredSizeOverride(InArgs._IconSize)
			.ColorAndOpacity(InArgs._IconColor);
	}
	else if (bHasText)
	{
		// Text-only layout
		Content = SNew(STextBlock)
			.Text(InArgs._Text)
			.Font(InArgs._Font.IsSet() ? InArgs._Font.GetValue() : FN2CFonts::Regular())
			.ColorAndOpacity(InArgs._TextColor);
	}

	ChildSlot
	[
		SNew(SButton)
		.ButtonStyle(ButtonStyle)
		.ContentPadding(InArgs._ContentPadding)
		.OnClicked(InArgs._OnClicked)
		.IsEnabled(InArgs._IsEnabled)
		.ToolTipText(InArgs._ToolTipText)
		.Visibility(InArgs._Visibility)
		[
			Content
		]
	];
}
