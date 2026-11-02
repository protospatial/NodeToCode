// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

/**
 * A circular progress bar widget that renders an arc-based progress indicator.
 * Used for showing context window usage per graph row in the tag manager.
 *
 * Features:
 * - Renders a background arc (full circle) and fill arc (proportional to percentage)
 * - Color gradient from green (0%) to yellow (50%) to red (100%)
 * - Clickable for manual refresh of token estimates
 * - Compact size suitable for table rows (16px diameter default)
 */
class NODETOCODE_API SN2CCircularProgressBar : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SN2CCircularProgressBar)
		: _Percent(0.0f)
		, _Radius(8.0f)
		, _Thickness(2.0f)
	{}
		/** Progress percentage (0.0 - 1.0) */
		SLATE_ATTRIBUTE(float, Percent)
		/** Radius of the progress bar in pixels */
		SLATE_ATTRIBUTE(float, Radius)
		/** Thickness of the arc in pixels */
		SLATE_ATTRIBUTE(float, Thickness)
		/** Called when the progress bar is clicked */
		SLATE_EVENT(FSimpleDelegate, OnClicked)
	SLATE_END_ARGS()

	/** Constructs this widget */
	void Construct(const FArguments& InArgs);

	// SWidget interface
	virtual int32 OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;

	/** The fraction the ring draws (0-1): the pinned value while one is set, else the Percent attribute, clamped */
	float GetDrawnPercent() const;

	/**
	 * For the docs-shot harness only (card 0522): every ring draws this value instead of its Percent while set. A ring
	 * shows the request's share of the model's context window, and the request includes the system prompt, so its
	 * arc changed with every prompt edit and made retakes differ by a few pixels. Unset (the default) in normal use.
	 */
	static void SetPinnedPercentForCapture(TOptional<float> InPercent);
	static TOptional<float> GetPinnedPercentForCapture();

private:
	/**
	 * Get the fill color based on current percentage
	 * Green (0%) -> Yellow (50%) -> Red (100%)
	 */
	FLinearColor GetFillColor() const;

	/**
	 * Draw an arc using line segments, in the widget's local space so it scales with DPI and render transforms
	 * @param OutDrawElements The draw elements list
	 * @param LayerId The layer to draw on
	 * @param AllottedGeometry The widget's geometry; the points are local to it
	 * @param Center The center of the arc, in local units
	 * @param Radius The radius of the arc, in local units
	 * @param Thickness The thickness of the arc, in local units
	 * @param StartAngle The start angle in radians (0 = right, PI/2 = down)
	 * @param EndAngle The end angle in radians
	 * @param Color The color of the arc (already multiplied by the widget's tint)
	 * @param DrawEffects The draw effects (DisabledEffect when the widget is disabled)
	 */
	void DrawArc(
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FGeometry& AllottedGeometry,
		const FVector2D& Center,
		float Radius,
		float Thickness,
		float StartAngle,
		float EndAngle,
		const FLinearColor& Color,
		ESlateDrawEffect DrawEffects) const;

	TAttribute<float> Percent;
	TAttribute<float> Radius;
	TAttribute<float> Thickness;
	FSimpleDelegate OnClickedDelegate;
};
