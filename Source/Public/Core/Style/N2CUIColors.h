// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Misc/Attribute.h"
#include "Styling/SlateColor.h"
#include "N2CUIColors.generated.h"

/**
 * Color tokens for every NodeToCode widget. The live palette is the UIColors property on UN2CSettings
 * (Project Settings > Plugins > Node to Code > Theming); read it through N2C::UI::Colors() so widget code
 * does not include Core/N2CSettings.h. Spacing, font and sizing tokens are in Core/Style/N2CDesignTokens.h.
 */
USTRUCT(BlueprintType)
struct FN2CUIColors
{
    GENERATED_BODY()

    // ── Background Colors ──────────────────────────────────────────

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Backgrounds", meta = (DisplayName = "Panel Background"))
    FColor BgPanel = FColor(37, 37, 38);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Backgrounds", meta = (DisplayName = "Panel Background Dark"))
    FColor BgPanelDarker = FColor(26, 26, 26);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Backgrounds", meta = (DisplayName = "Input Background"))
    FColor BgInput = FColor(45, 45, 45);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Backgrounds", meta = (DisplayName = "Hover Background"))
    FColor BgHover = FColor(51, 51, 51);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Backgrounds", meta = (DisplayName = "Overlay Panel Background"))
    FColor BgOverlayPanel = FColor(10, 10, 10);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Backgrounds", meta = (DisplayName = "Button Dark Background"))
    FColor BgButtonDark = FColor(28, 28, 28);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Backgrounds", meta = (DisplayName = "Button Dark Background Selected"))
    FColor BgButtonDarkSelected = FColor(42, 42, 42);

    // ── Button Style Colors ───────────────────────────────────────

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Buttons", meta = (DisplayName = "Button Normal"))
    FColor BtnNormal = FColor(55, 55, 55);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Buttons", meta = (DisplayName = "Button Hovered"))
    FColor BtnHovered = FColor(70, 70, 70);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Buttons", meta = (DisplayName = "Button Pressed"))
    FColor BtnPressed = FColor(40, 40, 40);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Buttons", meta = (DisplayName = "Button Disabled"))
    FColor BtnDisabled = FColor(35, 35, 35);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Buttons", meta = (DisplayName = "Button Foreground"))
    FColor BtnForeground = FColor(204, 204, 204);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Buttons", meta = (DisplayName = "Button Foreground Hovered"))
    FColor BtnForegroundHover = FColor(230, 230, 230);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Buttons", meta = (DisplayName = "Button Foreground Disabled"))
    FColor BtnForegroundDisabled = FColor(107, 107, 107);

    // ── Border Colors ──────────────────────────────────────────────

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Borders", meta = (DisplayName = "Border"))
    FColor BorderColor = FColor(60, 60, 60);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Borders", meta = (DisplayName = "Border Subtle"))
    FColor BorderSubtle = FColor(42, 42, 42);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Borders", meta = (DisplayName = "Fallback Border"))
    FColor FallbackBorder = FColor(28, 28, 28);

    // ── Text Colors ────────────────────────────────────────────────

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Text", meta = (DisplayName = "Text Primary"))
    FColor TextPrimary = FColor(204, 204, 204);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Text", meta = (DisplayName = "Text Secondary"))
    FColor TextSecondary = FColor(157, 157, 157);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Text", meta = (DisplayName = "Text Muted"))
    FColor TextMuted = FColor(107, 107, 107);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Text", meta = (DisplayName = "Text Shadow"))
    FColor TextShadow = FColor(15, 15, 15);

    // ── Accent Colors ──────────────────────────────────────────────

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Accents", meta = (DisplayName = "Accent Orange"))
    FColor AccentOrange = FColor(212, 160, 74);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Accents", meta = (DisplayName = "Accent Orange Dim"))
    FColor AccentOrangeDim = FColor(139, 105, 20);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Accents", meta = (DisplayName = "Accent Green"))
    FColor AccentGreen = FColor(78, 201, 176);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Accents", meta = (DisplayName = "Accent Red"))
    FColor AccentRed = FColor(241, 76, 76);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Accents", meta = (DisplayName = "Accent Gold"))
    FColor AccentGold = FColor(212, 161, 74);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Accents", meta = (DisplayName = "Accent Blue"))
    FColor AccentBlue = FColor(79, 194, 255);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Accents", meta = (DisplayName = "Accent Yellow"))
    FColor AccentYellow = FColor(229, 192, 123);

    // ── Editor Colors ───────────────────────────────────────────────

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Editor", meta = (DisplayName = "Text Highlight"))
    FColor TextHighlight = FColor(5, 77, 0);

    // ── Helper Methods ─────────────────────────────────────────────

    static FLinearColor ToLinear(const FColor& Color)
    {
        return FLinearColor::FromSRGBColor(Color);
    }

    static FLinearColor ToLinearWithAlpha(const FColor& Color, float Alpha)
    {
        FLinearColor Linear = FLinearColor::FromSRGBColor(Color);
        Linear.A = Alpha;
        return Linear;
    }
};

namespace N2C::UI
{
    /** The live palette (UN2CSettings::UIColors). Defined in N2CUIColors.cpp, the one place that reads the settings. */
    NODETOCODE_API const FN2CUIColors& Colors();

    /** sRGB palette color to linear. */
    inline FLinearColor Linear(const FColor& Color) { return FN2CUIColors::ToLinear(Color); }

    /** sRGB palette color to linear with the given alpha. */
    inline FLinearColor LinearAlpha(const FColor& Color, float Alpha) { return FN2CUIColors::ToLinearWithAlpha(Color, Alpha); }

    /** Attribute that reads a palette member each time it is evaluated, so a Project Settings edit shows at once. */
    inline TAttribute<FSlateColor> Bind(FColor FN2CUIColors::* Member)
    {
        return TAttribute<FSlateColor>::CreateLambda([Member]() -> FSlateColor
        {
            return FSlateColor(FN2CUIColors::ToLinear(Colors().*Member));
        });
    }

    /** As Bind, with a fixed alpha. */
    inline TAttribute<FSlateColor> BindAlpha(FColor FN2CUIColors::* Member, float Alpha)
    {
        return TAttribute<FSlateColor>::CreateLambda([Member, Alpha]() -> FSlateColor
        {
            return FSlateColor(FN2CUIColors::ToLinearWithAlpha(Colors().*Member, Alpha));
        });
    }
}
