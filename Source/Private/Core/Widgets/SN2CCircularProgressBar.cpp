// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/Widgets/SN2CCircularProgressBar.h"
#include "Rendering/DrawElements.h"
#include "Core/Style/N2CUIColors.h"

void SN2CCircularProgressBar::Construct(const FArguments& InArgs)
{
	Percent = InArgs._Percent;
	Radius = InArgs._Radius;
	Thickness = InArgs._Thickness;
	OnClickedDelegate = InArgs._OnClicked;
}

FVector2D SN2CCircularProgressBar::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	float R = Radius.Get();
	// Add a small margin for the thickness
	float Size = (R + Thickness.Get()) * 2.0f;
	return FVector2D(Size, Size);
}

int32 SN2CCircularProgressBar::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{
	const bool bEnabled = ShouldBeEnabled(bParentEnabled);
	const ESlateDrawEffect DrawEffects = bEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;

	const float R = Radius.Get();
	const float T = Thickness.Get();
	const float P = FMath::Clamp(Percent.Get(), 0.0f, 1.0f);

	// Calculate center of the widget
	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	const FVector2D Center = LocalSize * 0.5f;

	// Convert local center to absolute position for drawing
	const FVector2D AbsoluteCenter = AllottedGeometry.LocalToAbsolute(Center);

	// Start from top (-PI/2 in standard math coords, but Slate Y is inverted)
	// In Slate: 0 = right, positive = clockwise
	const float StartAngle = -PI / 2.0f;
	const float FullEndAngle = StartAngle + 2.0f * PI;

	// Draw background arc (full circle)
	DrawArc(
		OutDrawElements,
		LayerId,
		AbsoluteCenter,
		R,
		T,
		StartAngle,
		FullEndAngle,
		N2C::UI::Linear(N2C::UI::Colors().BorderColor),
		MyCullingRect
	);

	// Draw fill arc if there's any progress
	if (P > 0.0f)
	{
		const float FillEndAngle = StartAngle + (2.0f * PI * P);
		DrawArc(
			OutDrawElements,
			LayerId + 1,
			AbsoluteCenter,
			R,
			T,
			StartAngle,
			FillEndAngle,
			GetFillColor(),
			MyCullingRect
		);
	}

	return LayerId + 2;
}

FReply SN2CCircularProgressBar::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		OnClickedDelegate.ExecuteIfBound();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FCursorReply SN2CCircularProgressBar::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	if (OnClickedDelegate.IsBound())
	{
		return FCursorReply::Cursor(EMouseCursor::Hand);
	}
	return FCursorReply::Unhandled();
}

FLinearColor SN2CCircularProgressBar::GetFillColor() const
{
	const float P = FMath::Clamp(Percent.Get(), 0.0f, 1.0f);
	const FN2CUIColors& Colors = N2C::UI::Colors();

	if (P <= 0.5f)
	{
		// Green to Yellow (0-50%)
		float T = P * 2.0f; // 0-1 over 0-50%
		return FLinearColor::LerpUsingHSV(N2C::UI::Linear(Colors.AccentGreen), N2C::UI::Linear(Colors.AccentYellow), T);
	}
	else
	{
		// Yellow to Red (50-100%)
		float T = (P - 0.5f) * 2.0f; // 0-1 over 50-100%
		return FLinearColor::LerpUsingHSV(N2C::UI::Linear(Colors.AccentYellow), N2C::UI::Linear(Colors.AccentRed), T);
	}
}

void SN2CCircularProgressBar::DrawArc(
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FVector2D& Center,
	float InRadius,
	float InThickness,
	float StartAngle,
	float EndAngle,
	const FLinearColor& Color,
	const FSlateRect& ClippingRect) const
{
	// Number of segments based on arc length
	const float ArcLength = FMath::Abs(EndAngle - StartAngle);
	const int32 NumSegments = FMath::Max(8, FMath::CeilToInt(ArcLength * InRadius / 2.0f));

	if (NumSegments < 2)
	{
		return;
	}

	// Generate line points along the arc
	TArray<FVector2D> LinePoints;
	LinePoints.Reserve(NumSegments + 1);

	const float AngleStep = (EndAngle - StartAngle) / NumSegments;
	for (int32 i = 0; i <= NumSegments; ++i)
	{
		const float Angle = StartAngle + (AngleStep * i);
		const float X = Center.X + InRadius * FMath::Cos(Angle);
		const float Y = Center.Y + InRadius * FMath::Sin(Angle);
		LinePoints.Add(FVector2D(X, Y));
	}

	// Draw the arc as a series of line segments
	FSlateDrawElement::MakeLines(
		OutDrawElements,
		LayerId,
		FPaintGeometry(),
		LinePoints,
		ESlateDrawEffect::None,
		Color,
		true, // bAntialias
		InThickness
	);
}
