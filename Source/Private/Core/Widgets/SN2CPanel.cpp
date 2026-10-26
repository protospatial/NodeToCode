// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/Widgets/SN2CPanel.h"
#include "Core/Style/N2CStyle.h"
#include "Core/Style/N2CUIColors.h"
#include "Widgets/Layout/SBorder.h"

void SN2CPanel::Construct(const FArguments& InArgs)
{
	const FSlateBrush* BorderBrush = nullptr;
	TAttribute<FSlateColor> BgColor;

	switch (InArgs._Variant)
	{
	case EN2CPanelVariant::Light:
		BorderBrush = &N2CStyle::GetPanelBorderBrush();
		BgColor = N2C::UI::Bind(&FN2CUIColors::BgPanel);
		break;

	case EN2CPanelVariant::Dark:
		BorderBrush = &N2CStyle::GetDarkPanelBorderBrush();
		BgColor = N2C::UI::Bind(&FN2CUIColors::BgPanelDarker);
		break;

	case EN2CPanelVariant::Overlay:
		BorderBrush = &N2CStyle::GetPanelBorderBrush();
		BgColor = N2C::UI::BindAlpha(&FN2CUIColors::BgOverlayPanel, InArgs._OverlayAlpha);
		break;
	}

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(BorderBrush)
		.BorderBackgroundColor(BgColor)
		.Padding(InArgs._Padding)
		[
			InArgs._Content.Widget
		]
	];
}
