// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "N2CCodeLanguage.h"
#include "Styling/SlateStyle.h"

class FN2CCodeEditorStyle
{
public:
    static void Initialize();

    /** Destroys the style set and every brush in it. Only for module shutdown: live widgets hold raw brush pointers */
    static void Shutdown();

    /**
     * Re-reads the theme colors from settings. When the style set already exists it is kept, and its brushes and
     * text styles are updated in place, so the raw FSlateBrush / FTextBlockStyle pointers live widgets hold stay valid
     */
    static void Reinitialize();

    static const ISlateStyle& Get();

    /** Shared handle to the style set (null before Initialize). Lets tests check the set survives Reinitialize */
    static TSharedPtr<const FSlateStyleSet> GetStyleSetPtr();
    static const FName& GetStyleSetName();

    // Get style set for a specific language
    static const FTextBlockStyle& GetLanguageStyle(const FName& LanguageId, const FName& ThemeName, const FName& StyleId);
    static FString GetLanguageString(EN2CCodeLanguage Language);

private:
    static void InitializeLanguageStyles();
    static void InitializeStylesForLanguage(const FName& LanguageId, const struct FN2CCodeEditorColors& Colors, bool bIsPseudocode = false);

    /** Fills (first run) or updates in place (later runs) every brush and text style from the current settings */
    static void ApplyThemeColors();

    /** Adds a color brush, or retints the existing one so pointers to it stay valid */
    static void SetOrUpdateColorBrush(const FName& Name, const FLinearColor& Color);

    /** Adds a text style, or overwrites the existing one in place so pointers to it stay valid */
    static void SetOrUpdateTextStyle(const FName& Name, const FTextBlockStyle& Style);

    static TSharedPtr<FSlateStyleSet> StyleSet;
    static FTextBlockStyle CreateDefaultTextStyle(const FName& TypeName);
};
