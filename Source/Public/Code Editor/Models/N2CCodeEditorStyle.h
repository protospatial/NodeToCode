// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "N2CCodeLanguage.h"
#include "Styling/SlateStyle.h"

class FN2CCodeEditorStyle
{
public:
    static void Initialize();
    static void Shutdown();
    
    static const ISlateStyle& Get();
    static const FName& GetStyleSetName();

    // Get style set for a specific language
    static const FTextBlockStyle& GetLanguageStyle(const FName& LanguageId, const FName& ThemeName, const FName& StyleId);
    static FString GetLanguageString(EN2CCodeLanguage Language);

private:
    static void InitializeLanguageStyles();
    static void InitializeCPPStyles();
    static void InitializePythonStyles();
    static void InitializeJavaScriptStyles();
    static void InitializeCSharpStyles();
    static void InitializeSwiftStyles();
    static void InitializePseudocodeStyles();

    static TSharedPtr<FSlateStyleSet> StyleSet;
    static FTextBlockStyle CreateDefaultTextStyle(const FName& TypeName);
};
