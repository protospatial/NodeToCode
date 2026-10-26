// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CSettings.h"
#include "Core/Style/N2CStyle.h"
#include "Core/Style/N2CUIColors.h"
#include "Tests/N2CTestHelpers.h"

/**
 * The UI color tokens and their N2C::UI helpers (card 0187): they live in Core/Style/N2CUIColors.h, apart from the
 * settings header, and still read the live palette, the UIColors property on UN2CSettings. A bound color is re-read
 * on every evaluation, so an edit in Project Settings shows in open widgets without rebuilding them; the style set's
 * brushes follow once the settings change handler calls N2CStyle's Update functions.
 */
BEGIN_DEFINE_SPEC(FN2CUIColorsSpec, "NodeToCode.Style.UIColors", N2C_TEST_FLAGS)

	FN2CUIColors Saved;

	static FN2CUIColors& Palette() { return GetMutableDefault<UN2CSettings>()->UIColors; }

	static bool Near(const FLinearColor& A, const FLinearColor& B) { return A.Equals(B, 1.e-4f); }

END_DEFINE_SPEC(FN2CUIColorsSpec)

void FN2CUIColorsSpec::Define()
{
	BeforeEach([this]()
	{
		Saved = Palette();
	});

	AfterEach([this]()
	{
		Palette() = Saved;
		N2CStyle::UpdateButtonStyles();
		N2CStyle::UpdateBorderBrushes();
	});

	It(TEXT("Colors() is the UIColors property of the settings object"), [this]()
	{
		TestTrue(TEXT("same object"), &N2C::UI::Colors() == &GetDefault<UN2CSettings>()->UIColors);
	});

	It(TEXT("Linear and LinearAlpha convert a palette color from sRGB"), [this]()
	{
		const FColor C(212, 160, 74);
		TestTrue(TEXT("Linear"), Near(N2C::UI::Linear(C), FLinearColor::FromSRGBColor(C)));

		FLinearColor Expected = FLinearColor::FromSRGBColor(C);
		Expected.A = 0.3f;
		TestTrue(TEXT("LinearAlpha"), Near(N2C::UI::LinearAlpha(C, 0.3f), Expected));
	});

	It(TEXT("a bound color follows a palette edit without being bound again"), [this]()
	{
		const TAttribute<FSlateColor> Bound = N2C::UI::Bind(&FN2CUIColors::TextPrimary);
		TestTrue(TEXT("bound to a getter"), Bound.IsBound());

		const FColor First(10, 200, 30);
		Palette().TextPrimary = First;
		TestTrue(TEXT("first edit"), Near(Bound.Get().GetSpecifiedColor(), FLinearColor::FromSRGBColor(First)));

		const FColor Second(240, 20, 120);
		Palette().TextPrimary = Second;
		TestTrue(TEXT("second edit"), Near(Bound.Get().GetSpecifiedColor(), FLinearColor::FromSRGBColor(Second)));
	});

	It(TEXT("a bound color with alpha follows a palette edit and keeps its alpha"), [this]()
	{
		const TAttribute<FSlateColor> Bound = N2C::UI::BindAlpha(&FN2CUIColors::AccentGold, 0.25f);

		const FColor Edited(30, 60, 90);
		Palette().AccentGold = Edited;
		FLinearColor Expected = FLinearColor::FromSRGBColor(Edited);
		Expected.A = 0.25f;
		TestTrue(TEXT("edited color, fixed alpha"), Near(Bound.Get().GetSpecifiedColor(), Expected));
	});

	It(TEXT("the style set's button and panel brushes take a palette edit when updated"), [this]()
	{
		const FColor Button(90, 20, 160);
		const FColor Border(0, 150, 140);
		Palette().BtnNormal = Button;
		Palette().BorderSubtle = Border;
		N2CStyle::UpdateButtonStyles();
		N2CStyle::UpdateBorderBrushes();

		TestTrue(TEXT("button normal brush"), Near(N2CStyle::GetButtonStyle().Normal.TintColor.GetSpecifiedColor(),
			FLinearColor::FromSRGBColor(Button)));
		TestTrue(TEXT("panel border outline"), Near(N2CStyle::GetPanelBorderBrush().OutlineSettings.Color.GetSpecifiedColor(),
			FLinearColor::FromSRGBColor(Border)));
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
