// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "Framework/Text/SlateTextRun.h"

/**
 * Custom text run for handling whitespace in the code editor
 */
class FN2CWhiteSpaceRun : public FSlateTextRun
{
public:
    static TSharedRef<FN2CWhiteSpaceRun> Create(
        const FRunInfo& InRunInfo,
        const TSharedRef<const FString>& InText,
        const FTextBlockStyle& InStyle,
        const FTextRange& InRange,
        int32 InNumSpacesPerTab);

    virtual FVector2D Measure(int32 StartIndex, int32 EndIndex, float Scale, const FRunTextContext& TextContext) const override;

protected:
    FN2CWhiteSpaceRun(
        const FRunInfo& InRunInfo,
        const TSharedRef<const FString>& InText,
        const FTextBlockStyle& InStyle,
        const FTextRange& InRange,
        int32 InNumSpacesPerTab);

private:
    int32 NumSpacesPerTab;
    float TabWidth;
    float SpaceWidth;
};
