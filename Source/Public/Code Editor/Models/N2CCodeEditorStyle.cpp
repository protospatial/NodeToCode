// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CCodeEditorStyle.h"

#include "N2CCodeLanguage.h"
#include "Core/N2CSettings.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"
#include "Styling/CoreStyle.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "N2CCodeEditorStyle"

TSharedPtr<FSlateStyleSet> FN2CCodeEditorStyle::StyleSet = nullptr;

#define DEFAULT_FONT(...) FCoreStyle::GetDefaultFontStyle(__VA_ARGS__)

void FN2CCodeEditorStyle::Initialize()
{
    // Ensure we don't double-initialize
    if (StyleSet.IsValid())
    {
        return;
    }

    // Ensure we're on the game thread
    check(IsInGameThread());

    StyleSet = MakeShareable(new FSlateStyleSet("N2CCodeEditor"));
    
    // Base editor style
    const FTextBlockStyle NormalText = CreateDefaultTextStyle(TEXT("CPP"));

    StyleSet->Set("N2CCodeEditor.TextEditor.NormalText", NormalText);
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    if (Settings && Settings->CPPThemes.Themes.Contains(TEXT("Unreal Engine")))
    {
        const FN2CCodeEditorColors& Colors = *Settings->CPPThemes.Themes.Find(TEXT("Unreal Engine"));
        StyleSet->Set("N2CCodeEditor.TextEditor.Border", new FSlateColorBrush(FLinearColor(Colors.Background)));
    }
    else
    {
        // Fallback to a default color if themes aren't initialized yet
        StyleSet->Set("N2CCodeEditor.TextEditor.Border", new FSlateColorBrush(FLinearColor(0.1f, 0.1f, 0.1f)));
    }

    // Initialize language-specific styles
    InitializeLanguageStyles();

    FSlateStyleRegistry::RegisterSlateStyle(*StyleSet.Get());
}

void FN2CCodeEditorStyle::InitializeLanguageStyles()
{
    InitializeCPPStyles();
    InitializePythonStyles();
    InitializeJavaScriptStyles();
    InitializeCSharpStyles();
    InitializeSwiftStyles();
    InitializePseudocodeStyles();
}

void FN2CCodeEditorStyle::InitializeCPPStyles()
{
    const FTextBlockStyle BaseStyle = CreateDefaultTextStyle(TEXT("CPP"));
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    
    // Initialize styles for each theme
    for (const auto& ThemePair : Settings->CPPThemes.Themes)
    {
        const FName& ThemeName = ThemePair.Key;
        const FN2CCodeEditorColors& Colors = ThemePair.Value;
        
        // Set background color for this theme
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.CPP.%s.Background"), *ThemeName.ToString()), 
            new FSlateColorBrush(FLinearColor(Colors.Background)));

        // Set text styles for this theme
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.CPP.%s.Normal"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.NormalText)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.CPP.%s.Operator"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.Operators)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.CPP.%s.Keyword"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.Keywords)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.CPP.%s.String"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.Strings)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.CPP.%s.Number"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.Numbers)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.CPP.%s.Comment"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.Comments)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.CPP.%s.Preprocessor"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.Preprocessor)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.CPP.%s.Parentheses"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.Parentheses)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.CPP.%s.CurlyBraces"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.CurlyBraces)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.CPP.%s.SquareBrackets"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.SquareBrackets)));
    }
}

void FN2CCodeEditorStyle::InitializePythonStyles()
{
    const FTextBlockStyle BaseStyle = CreateDefaultTextStyle(TEXT("Python"));
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    
    // Initialize styles for each theme
    for (const auto& ThemePair : Settings->PythonThemes.Themes)
    {
        const FName& ThemeName = ThemePair.Key;
        const FN2CCodeEditorColors& Colors = ThemePair.Value;
        
        // Set background color for this theme
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.Python.%s.Background"), *ThemeName.ToString()), 
            new FSlateColorBrush(FLinearColor(Colors.Background)));

        // Set text styles for this theme
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.Python.%s.Normal"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.NormalText)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.Python.%s.Operator"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.Operators)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.Python.%s.Keyword"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.Keywords)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.Python.%s.String"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.Strings)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.Python.%s.Number"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.Numbers)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.Python.%s.Comment"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.Comments)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.Python.%s.Preprocessor"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.Preprocessor)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.Python.%s.Parentheses"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.Parentheses)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.Python.%s.CurlyBraces"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.CurlyBraces)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.Python.%s.SquareBrackets"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.SquareBrackets)));
    }
}

void FN2CCodeEditorStyle::InitializeJavaScriptStyles()
{
    const FTextBlockStyle BaseStyle = CreateDefaultTextStyle(TEXT("JavaScript"));
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    
    // Initialize styles for each theme
    for (const auto& ThemePair : Settings->JavaScriptThemes.Themes)
    {
        const FName& ThemeName = ThemePair.Key;
        const FN2CCodeEditorColors& Colors = ThemePair.Value;
        
        // Set background color for this theme
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.JavaScript.%s.Background"), *ThemeName.ToString()), 
            new FSlateColorBrush(FLinearColor(Colors.Background)));

        // Set text styles for this theme
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.JavaScript.%s.Normal"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.NormalText)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.JavaScript.%s.Operator"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.Operators)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.JavaScript.%s.Keyword"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.Keywords)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.JavaScript.%s.String"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.Strings)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.JavaScript.%s.Number"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.Numbers)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.JavaScript.%s.Comment"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.Comments)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.JavaScript.%s.Preprocessor"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.Preprocessor)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.JavaScript.%s.Parentheses"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.Parentheses)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.JavaScript.%s.CurlyBraces"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.CurlyBraces)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.JavaScript.%s.SquareBrackets"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.SquareBrackets)));
    }
}

void FN2CCodeEditorStyle::InitializeCSharpStyles()
{
    const FTextBlockStyle BaseStyle = CreateDefaultTextStyle(TEXT("CSharp"));
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    
    // Initialize styles for each theme
    for (const auto& ThemePair : Settings->CSharpThemes.Themes)
    {
        const FName& ThemeName = ThemePair.Key;
        const FN2CCodeEditorColors& Colors = ThemePair.Value;
        
        // Set background color for this theme
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.CSharp.%s.Background"), *ThemeName.ToString()), 
            new FSlateColorBrush(FLinearColor(Colors.Background)));

        // Set text styles for this theme
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.CSharp.%s.Normal"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.NormalText)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.CSharp.%s.Operator"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.Operators)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.CSharp.%s.Keyword"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.Keywords)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.CSharp.%s.String"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.Strings)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.CSharp.%s.Number"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.Numbers)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.CSharp.%s.Comment"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.Comments)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.CSharp.%s.Preprocessor"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.Preprocessor)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.CSharp.%s.Parentheses"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.Parentheses)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.CSharp.%s.CurlyBraces"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.CurlyBraces)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.CSharp.%s.SquareBrackets"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.SquareBrackets)));
    }
}

void FN2CCodeEditorStyle::InitializeSwiftStyles()
{
    const FTextBlockStyle BaseStyle = CreateDefaultTextStyle(TEXT("Swift"));
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    
    // Initialize styles for each theme
    for (const auto& ThemePair : Settings->SwiftThemes.Themes)
    {
        const FName& ThemeName = ThemePair.Key;
        const FN2CCodeEditorColors& Colors = ThemePair.Value;
        
        // Set background color for this theme
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.Swift.%s.Background"), *ThemeName.ToString()), 
            new FSlateColorBrush(FLinearColor(Colors.Background)));

        // Set text styles for this theme
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.Swift.%s.Normal"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.NormalText)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.Swift.%s.Operator"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.Operators)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.Swift.%s.Keyword"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.Keywords)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.Swift.%s.String"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.Strings)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.Swift.%s.Number"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.Numbers)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.Swift.%s.Comment"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.Comments)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.Swift.%s.Preprocessor"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.Preprocessor)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.Swift.%s.Parentheses"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.Parentheses)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.Swift.%s.CurlyBraces"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.CurlyBraces)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.Swift.%s.SquareBrackets"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.SquareBrackets)));
    }
}

void FN2CCodeEditorStyle::InitializePseudocodeStyles()
{
    const FTextBlockStyle BaseStyle = CreateDefaultTextStyle(TEXT("Pseudocode"));
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    
    // Initialize styles for each theme
    for (const auto& ThemePair : Settings->PseudocodeThemes.Themes)
    {
        const FName& ThemeName = ThemePair.Key;
        const FN2CCodeEditorColors& Colors = ThemePair.Value;
        
        // Set background color for this theme
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.Pseudocode.%s.Background"), *ThemeName.ToString()), 
            new FSlateColorBrush(FLinearColor(Colors.Background)));

        // For Pseudocode, we only need the Normal style since we want all text to be the same color
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.Pseudocode.%s.Normal"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.NormalText)));
            
        // Set all other styles to match Normal for consistency
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.Pseudocode.%s.Operator"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.NormalText)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.Pseudocode.%s.Keyword"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.NormalText)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.Pseudocode.%s.String"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.NormalText)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.Pseudocode.%s.Number"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.NormalText)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.Pseudocode.%s.Comment"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.NormalText)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.Pseudocode.%s.Preprocessor"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.NormalText)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.Pseudocode.%s.Parentheses"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.NormalText)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.Pseudocode.%s.CurlyBraces"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.NormalText)));
        StyleSet->Set(*FString::Printf(TEXT("N2CCodeEditor.Pseudocode.%s.SquareBrackets"), *ThemeName.ToString()), 
            FTextBlockStyle(BaseStyle).SetColorAndOpacity(FLinearColor(Colors.NormalText)));
    }
}

FTextBlockStyle FN2CCodeEditorStyle::CreateDefaultTextStyle(const FName& TypeName)
{
    // Get current font size from settings if available, otherwise use default
    int32 FontSize = 12;

    return FTextBlockStyle()
        .SetFont(DEFAULT_FONT("Mono", FontSize))
        .SetColorAndOpacity(FSlateColor(FLinearColor::White))
        .SetShadowOffset(FVector2D::ZeroVector)
        .SetShadowColorAndOpacity(FLinearColor::Black)
        .SetHighlightColor(FLinearColor(0.02f, 0.3f, 0.0f));
}

void FN2CCodeEditorStyle::Shutdown()
{
    if (StyleSet.IsValid())
    {
        // Ensure no references remain before unregistering
        FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet.Get());
        StyleSet.Reset();
    }
}

const ISlateStyle& FN2CCodeEditorStyle::Get()
{
    check(StyleSet.IsValid());
    return *StyleSet.Get();
}

const FName& FN2CCodeEditorStyle::GetStyleSetName()
{
    check(StyleSet.IsValid());
    return StyleSet->GetStyleSetName();
}

const FTextBlockStyle& FN2CCodeEditorStyle::GetLanguageStyle(const FName& LanguageId, const FName& ThemeName, const FName& StyleId)
{
    check(StyleSet.IsValid());
    const FName FullStyleId = FName(*FString::Printf(TEXT("N2CCodeEditor.%s.%s.%s"), 
        *LanguageId.ToString(), *ThemeName.ToString(), *StyleId.ToString()));
    
    // Add fallback in case the style isn't found
    if (!StyleSet->HasWidgetStyle<FTextBlockStyle>(FullStyleId))
    {
        // Try the default theme
        const FName DefaultStyleId = FName(*FString::Printf(TEXT("N2CCodeEditor.%s.Unreal Engine.%s"), 
            *LanguageId.ToString(), *StyleId.ToString()));
            
        if (!StyleSet->HasWidgetStyle<FTextBlockStyle>(DefaultStyleId))
        {
            return StyleSet->GetWidgetStyle<FTextBlockStyle>("N2CCodeEditor.TextEditor.NormalText");
        }
        return StyleSet->GetWidgetStyle<FTextBlockStyle>(DefaultStyleId);
    }
    
    return StyleSet->GetWidgetStyle<FTextBlockStyle>(FullStyleId);
}

FString FN2CCodeEditorStyle::GetLanguageString(EN2CCodeLanguage Language)
{
    switch (Language)
    {
        case EN2CCodeLanguage::Cpp:
            return TEXT("CPP");
        case EN2CCodeLanguage::Python:
            return TEXT("Python");
        case EN2CCodeLanguage::JavaScript:
            return TEXT("JavaScript");
        case EN2CCodeLanguage::CSharp:
            return TEXT("CSharp");
        case EN2CCodeLanguage::Swift:
            return TEXT("Swift");
        case EN2CCodeLanguage::Pseudocode:
            return TEXT("Pseudocode");
        default:
            return TEXT("CPP");
    }
}

#undef DEFAULT_FONT
#undef LOCTEXT_NAMESPACE
