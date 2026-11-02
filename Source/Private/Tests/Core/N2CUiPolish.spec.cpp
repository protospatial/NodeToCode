// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/Style/N2CDesignTokens.h"
#include "Core/Style/N2CStyle.h"
#include "Core/Style/N2CUIColors.h"
#include "Core/Widgets/SN2CBatchProgressModal.h"
#include "Core/Widgets/SN2CCircularProgressBar.h"
#include "Input/HittestGrid.h"
#include "Misc/Paths.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "Tests/N2CTestHelpers.h"
#include "Types/PaintArgs.h"
#include "Widgets/SWindow.h"

/**
 * DPI drawing, contrast, small text and status icons (card 0271).
 * - The context ring draws its arcs in local space through the widget's paint geometry, so at 200% the ring is twice
 *   as large and stays in its slot; it passes the disabled draw effect and is tinted by the widget's color and opacity.
 * - Muted text meets 4.5:1 on the panel and input backgrounds, and no font token is under 8pt.
 * - The batch progress status icons are FAppStyle brushes with a label, not Unicode glyphs.
 */
BEGIN_DEFINE_SPEC(FN2CUiPolishSpec, "NodeToCode.UI.DpiContrastIcons", N2C_TEST_FLAGS)

	/** Paint the ring at Scale and Offset; returns its line elements */
	TArray<const FSlateLineElement*> PaintRing(const TSharedRef<SN2CCircularProgressBar>& Ring, FSlateWindowElementList& List,
		float Scale, const FVector2D& Offset, const FLinearColor& StyleTint, FVector2D& OutLocalSize)
	{
		OutLocalSize = Ring->ComputeDesiredSize(1.0f);
		const FGeometry Geometry = FGeometry::MakeRoot(OutLocalSize, FSlateLayoutTransform(Scale, Offset));
		FHittestGrid Grid;
		const FPaintArgs Args(nullptr, Grid, FVector2D::ZeroVector, 0.0, 0.0f);
		FWidgetStyle Style;
		Style.BlendColorAndOpacityTint(StyleTint);
		Ring->OnPaint(Args, Geometry, FSlateRect(0.0f, 0.0f, 4000.0f, 4000.0f), List, 0, Style, true);

		TArray<const FSlateLineElement*> Lines;
		for (const FSlateLineElement& Line : List.GetUncachedDrawElements().Get<(uint8)EElementType::ET_Line>())
		{
			Lines.Add(&Line);
		}
		return Lines;
	}

	static double Luminance(const FColor& C)
	{
		auto Channel = [](uint8 V)
		{
			const double S = V / 255.0;
			return S <= 0.03928 ? S / 12.92 : FMath::Pow((S + 0.055) / 1.055, 2.4);
		};
		return 0.2126 * Channel(C.R) + 0.7152 * Channel(C.G) + 0.0722 * Channel(C.B);
	}

	static double Contrast(const FColor& A, const FColor& B)
	{
		const double LA = Luminance(A);
		const double LB = Luminance(B);
		return (FMath::Max(LA, LB) + 0.05) / (FMath::Min(LA, LB) + 0.05);
	}

END_DEFINE_SPEC(FN2CUiPolishSpec)

void FN2CUiPolishSpec::Define()
{
	Describe(TEXT("the context ring"), [this]()
	{
		It(TEXT("draws in local space, so at 200% it is twice the size and stays in its slot"), [this]()
		{
			const TSharedRef<SWindow> Window = SNew(SWindow);
			const TSharedRef<SN2CCircularProgressBar> Ring = SNew(SN2CCircularProgressBar)
				.Percent(0.5f)
				.Radius(7.0f)
				.Thickness(2.5f);

			FSlateWindowElementList List(Window);
			FVector2D LocalSize;
			const FVector2D Offset(300.0f, 120.0f);
			const TArray<const FSlateLineElement*> Lines = PaintRing(Ring, List, 2.0f, Offset, FLinearColor::White, LocalSize);
			if (!TestEqual(TEXT("background and fill arcs"), Lines.Num(), 2))
			{
				return;
			}

			for (const FSlateLineElement* Line : Lines)
			{
				// Every point is inside the widget's local box (it was an absolute position before)
				FBox2f Bounds(ForceInit);
				for (const FVector2f& P : Line->GetPoints())
				{
					Bounds += P;
				}
				TestTrue(TEXT("points are local: inside the widget's local size"),
					Bounds.Min.X >= -0.01f && Bounds.Min.Y >= -0.01f
					&& Bounds.Max.X <= LocalSize.X + 0.01f && Bounds.Max.Y <= LocalSize.Y + 0.01f);

				// The paint geometry maps the local box onto the slot at 2x
				const FSlateRenderTransform& Transform = Line->GetRenderTransform();
				const FVector2f Origin = Transform.TransformPoint(FVector2f::ZeroVector);
				const FVector2f Corner = Transform.TransformPoint(FVector2f(LocalSize));
				TestTrue(TEXT("local origin lands on the slot"), Origin.Equals(FVector2f(Offset), 0.01f));
				TestTrue(TEXT("the local box is scaled by the DPI scale"),
					Corner.Equals(FVector2f(Offset) + FVector2f(LocalSize) * 2.0f, 0.01f));
			}
		});

		It(TEXT("is tinted by the widget's color and opacity, and draws disabled when disabled"), [this]()
		{
			const TSharedRef<SWindow> Window = SNew(SWindow);
			const TSharedRef<SN2CCircularProgressBar> Ring = SNew(SN2CCircularProgressBar).Percent(1.0f);
			FVector2D LocalSize;

			{
				FSlateWindowElementList List(Window);
				const TArray<const FSlateLineElement*> Lines =
					PaintRing(Ring, List, 1.0f, FVector2D::ZeroVector, FLinearColor(1.0f, 1.0f, 1.0f, 0.25f), LocalSize);
				TestEqual(TEXT("two arcs"), Lines.Num(), 2);
				for (const FSlateLineElement* Line : Lines)
				{
					TestTrue(TEXT("opacity multiplied in"), Line->GetTint().A <= 0.25f + KINDA_SMALL_NUMBER);
					TestFalse(TEXT("enabled: no disabled effect"),
						EnumHasAnyFlags(Line->GetDrawEffects(), ESlateDrawEffect::DisabledEffect));
				}
			}

			Ring->SetEnabled(false);
			{
				FSlateWindowElementList List(Window);
				const TArray<const FSlateLineElement*> Lines =
					PaintRing(Ring, List, 1.0f, FVector2D::ZeroVector, FLinearColor::White, LocalSize);
				TestEqual(TEXT("two arcs"), Lines.Num(), 2);
				for (const FSlateLineElement* Line : Lines)
				{
					TestTrue(TEXT("disabled effect passed"),
						EnumHasAnyFlags(Line->GetDrawEffects(), ESlateDrawEffect::DisabledEffect));
				}
			}
		});
	});

	It(TEXT("muted text meets 4.5:1 on the panel and input backgrounds"), [this]()
	{
		const FN2CUIColors Defaults;
		const double OnPanel = Contrast(Defaults.TextMuted, Defaults.BgPanel);
		const double OnInput = Contrast(Defaults.TextMuted, Defaults.BgInput);
		TestTrue(*FString::Printf(TEXT("on BgPanel %.2f:1"), OnPanel), OnPanel >= 4.5);
		TestTrue(*FString::Printf(TEXT("on BgInput %.2f:1"), OnInput), OnInput >= 4.5);
		TestTrue(TEXT("still dimmer than secondary text"),
			Luminance(Defaults.TextMuted) < Luminance(Defaults.TextSecondary));
	});

	It(TEXT("no font token is under 8pt"), [this]()
	{
		const TArray<TPair<const TCHAR*, FSlateFontInfo>> Tokens = {
			{ TEXT("Small"), FN2CFonts::Small() }, { TEXT("Regular"), FN2CFonts::Regular() },
			{ TEXT("Medium"), FN2CFonts::Medium() }, { TEXT("Large"), FN2CFonts::Large() },
			{ TEXT("SmallBold"), FN2CFonts::SmallBold() }, { TEXT("RegularBold"), FN2CFonts::RegularBold() },
			{ TEXT("MediumBold"), FN2CFonts::MediumBold() }, { TEXT("LargeBold"), FN2CFonts::LargeBold() },
			{ TEXT("Mono"), FN2CFonts::Mono() }, { TEXT("MonoSmall"), FN2CFonts::MonoSmall() },
		};
		for (const TPair<const TCHAR*, FSlateFontInfo>& Token : Tokens)
		{
			TestTrue(*FString::Printf(TEXT("%s is %.0fpt"), Token.Key, Token.Value.Size), Token.Value.Size >= 8.0f);
		}
	});

	It(TEXT("the logo brushes are SVG vectors at the size they are shown at: 20 px, and 16 px for the tab"), [this]()
	{
		const FSlateBrush* Large = N2CStyle::Get().GetBrush("NodeToCode.ToolbarButton");
		const FSlateBrush* Small = N2CStyle::Get().GetBrush("NodeToCode.ToolbarButton.Small");
		TestEqual(TEXT("toolbar and overlay"), FVector2D(Large->ImageSize), FVector2D(20.0f, 20.0f));
		TestEqual(TEXT("tab"), FVector2D(Small->ImageSize), FVector2D(16.0f, 16.0f));
		for (const FSlateBrush* Brush : { Large, Small })
		{
			TestTrue(TEXT("a vector brush, rasterized at the DPI scale"), Brush->GetImageType() == ESlateBrushImageType::Vector);
			const FString Resource = Brush->GetResourceName().ToString();
			TestTrue(*FString::Printf(TEXT("%s is an .svg that ships"), *Resource),
				Resource.EndsWith(TEXT(".svg")) && FPaths::FileExists(Resource));
		}
	});

	It(TEXT("batch progress status icons are FAppStyle brushes with a label"), [this]()
	{
		const EN2CBatchProgressItemStatus All[] = {
			EN2CBatchProgressItemStatus::Pending, EN2CBatchProgressItemStatus::InProgress,
			EN2CBatchProgressItemStatus::Completed, EN2CBatchProgressItemStatus::Failed };
		TSet<const FSlateBrush*> Seen;
		for (EN2CBatchProgressItemStatus Status : All)
		{
			const FSlateBrush* Brush = SN2CBatchProgressModal::GetStatusBrush(Status);
			TestNotNull(TEXT("a brush"), Brush);
			TestTrue(TEXT("a real icon, not the style's missing-brush default"), Brush != FAppStyle::Get().GetDefaultBrush());
			TestFalse(TEXT("a label"), SN2CBatchProgressModal::GetStatusLabel(Status).IsEmpty());
			Seen.Add(Brush);
		}
		TestEqual(TEXT("each status has its own icon"), Seen.Num(), 4);
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
