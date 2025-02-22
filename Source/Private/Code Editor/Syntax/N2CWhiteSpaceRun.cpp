// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Code Editor/Syntax/N2CWhiteSpaceRun.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Fonts/FontMeasure.h"

TSharedRef<FN2CWhiteSpaceRun> FN2CWhiteSpaceRun::Create(
    const FRunInfo& InRunInfo,
    const TSharedRef<const FString>& InText,
    const FTextBlockStyle& InStyle,
    const FTextRange& InRange,
    int32 InNumSpacesPerTab)
{
    return MakeShareable(new FN2CWhiteSpaceRun(InRunInfo, InText, InStyle, InRange, InNumSpacesPerTab));
}

FVector2D FN2CWhiteSpaceRun::Measure(int32 StartIndex, int32 EndIndex, float Scale, const FRunTextContext& TextContext) const
{
    const FVector2D ShadowOffsetToApply(
        (EndIndex == Range.EndIndex) ? FMath::Abs(Style.ShadowOffset.X * Scale) : 0.0f,
        FMath::Abs(Style.ShadowOffset.Y * Scale));

    if (EndIndex - StartIndex == 0)
    {
        return FVector2D(ShadowOffsetToApply.X * Scale, GetMaxHeight(Scale));
    }

    // Count tabs in the range
    int32 TabCount = 0;
    for (int32 Index = StartIndex; Index < EndIndex; Index++)
    {
        if ((*Text)[Index] == TEXT('\t'))
        {
            TabCount++;
        }
    }

    const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
    FVector2D Size = FontMeasure->Measure(**Text, StartIndex, EndIndex, Style.Font, true, Scale) + ShadowOffsetToApply;

    // Adjust size for tabs
    Size.X -= TabWidth * static_cast<float>(TabCount) * Scale;
    Size.X += SpaceWidth * static_cast<float>(TabCount * NumSpacesPerTab) * Scale;

    return Size;
}

FN2CWhiteSpaceRun::FN2CWhiteSpaceRun(
    const FRunInfo& InRunInfo,
    const TSharedRef<const FString>& InText,
    const FTextBlockStyle& InStyle,
    const FTextRange& InRange,
    int32 InNumSpacesPerTab)
    : FSlateTextRun(InRunInfo, InText, InStyle, InRange)
    , NumSpacesPerTab(InNumSpacesPerTab)
{
    // Measure tab and space widths
    const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
    TabWidth = FontMeasure->Measure(TEXT("\t"), 0, 1, Style.Font, true, 1.0f).X;
    SpaceWidth = FontMeasure->Measure(TEXT(" "), 0, 1, Style.Font, true, 1.0f).X;
}
