// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Code Editor/Models/N2CCodeEditorStyle.h"

#include "Code Editor/Models/N2CCodeLanguage.h"
#include "Core/N2CSettings.h"
#include "Core/Style/N2CUIColors.h"
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

    ApplyThemeColors();

    FSlateStyleRegistry::RegisterSlateStyle(*StyleSet.Get());
}

void FN2CCodeEditorStyle::ApplyThemeColors()
{
    check(StyleSet.IsValid());

    // Base editor style
    SetOrUpdateTextStyle("N2CCodeEditor.TextEditor.NormalText", CreateDefaultTextStyle(TEXT("CPP")));

    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    if (Settings && Settings->CodeEditorTheme.Themes.Contains(TEXT("Unreal Engine")))
    {
        const FN2CCodeEditorColors& Colors = *Settings->CodeEditorTheme.Themes.Find(TEXT("Unreal Engine"));
        SetOrUpdateColorBrush("N2CCodeEditor.TextEditor.Border", FLinearColor(Colors.Background));
    }
    else
    {
        // Fallback to a default color if themes aren't initialized yet
        SetOrUpdateColorBrush("N2CCodeEditor.TextEditor.Border", FN2CUIColors::ToLinear(N2C::UI::Colors().FallbackBorder));
    }

    // Initialize language-specific styles
    InitializeLanguageStyles();
}

void FN2CCodeEditorStyle::SetOrUpdateColorBrush(const FName& Name, const FLinearColor& Color)
{
    // GetOptionalBrush returns the default (null here) when the name is not registered yet
    if (const FSlateBrush* Existing = StyleSet->GetOptionalBrush(Name, nullptr, nullptr))
    {
        // The set owns this brush and widgets point at it: retint it, never replace it
        const_cast<FSlateBrush*>(Existing)->TintColor = FSlateColor(Color);
        return;
    }
    StyleSet->Set(Name, new FSlateColorBrush(Color));
}

void FN2CCodeEditorStyle::SetOrUpdateTextStyle(const FName& Name, const FTextBlockStyle& Style)
{
    if (StyleSet->HasWidgetStyle<FTextBlockStyle>(Name))
    {
        // FSlateStyleSet::Set would swap in a new shared object and free the one widgets point at, so assign in place
        const_cast<FTextBlockStyle&>(StyleSet->GetWidgetStyle<FTextBlockStyle>(Name)) = Style;
        return;
    }
    StyleSet->Set(Name, Style);
}

void FN2CCodeEditorStyle::InitializeLanguageStyles()
{
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    const FN2CCodeEditorColors* Colors = Settings->GetThemeColors(EN2CCodeLanguage::Cpp, FName("Unreal Engine"));
    if (!Colors) return;

    // Register styles for each language using the same theme colors
    InitializeStylesForLanguage(TEXT("CPP"), *Colors);
    InitializeStylesForLanguage(TEXT("Python"), *Colors);
    InitializeStylesForLanguage(TEXT("JavaScript"), *Colors);
    InitializeStylesForLanguage(TEXT("CSharp"), *Colors);
    InitializeStylesForLanguage(TEXT("Swift"), *Colors);
    InitializeStylesForLanguage(TEXT("Pseudocode"), *Colors, true);
}

void FN2CCodeEditorStyle::InitializeStylesForLanguage(const FName& LanguageId, const FN2CCodeEditorColors& Colors, bool bIsPseudocode)
{
    const FTextBlockStyle BaseStyle = CreateDefaultTextStyle(LanguageId);
    const FName ThemeName = TEXT("Unreal Engine");

    // Set background color
    SetOrUpdateColorBrush(*FString::Printf(TEXT("N2CCodeEditor.%s.%s.Background"), *LanguageId.ToString(), *ThemeName.ToString()),
        FLinearColor(Colors.Background));

    // For Pseudocode, all syntax elements use NormalText color
    const FLinearColor NormalColor(Colors.NormalText);

    SetOrUpdateTextStyle(*FString::Printf(TEXT("N2CCodeEditor.%s.%s.Normal"), *LanguageId.ToString(), *ThemeName.ToString()),
        FTextBlockStyle(BaseStyle).SetColorAndOpacity(NormalColor));
    SetOrUpdateTextStyle(*FString::Printf(TEXT("N2CCodeEditor.%s.%s.Operator"), *LanguageId.ToString(), *ThemeName.ToString()),
        FTextBlockStyle(BaseStyle).SetColorAndOpacity(bIsPseudocode ? NormalColor : FLinearColor(Colors.Operators)));
    SetOrUpdateTextStyle(*FString::Printf(TEXT("N2CCodeEditor.%s.%s.Keyword"), *LanguageId.ToString(), *ThemeName.ToString()),
        FTextBlockStyle(BaseStyle).SetColorAndOpacity(bIsPseudocode ? NormalColor : FLinearColor(Colors.Keywords)));
    SetOrUpdateTextStyle(*FString::Printf(TEXT("N2CCodeEditor.%s.%s.String"), *LanguageId.ToString(), *ThemeName.ToString()),
        FTextBlockStyle(BaseStyle).SetColorAndOpacity(bIsPseudocode ? NormalColor : FLinearColor(Colors.Strings)));
    SetOrUpdateTextStyle(*FString::Printf(TEXT("N2CCodeEditor.%s.%s.Number"), *LanguageId.ToString(), *ThemeName.ToString()),
        FTextBlockStyle(BaseStyle).SetColorAndOpacity(bIsPseudocode ? NormalColor : FLinearColor(Colors.Numbers)));
    SetOrUpdateTextStyle(*FString::Printf(TEXT("N2CCodeEditor.%s.%s.Comment"), *LanguageId.ToString(), *ThemeName.ToString()),
        FTextBlockStyle(BaseStyle).SetColorAndOpacity(bIsPseudocode ? NormalColor : FLinearColor(Colors.Comments)));
    SetOrUpdateTextStyle(*FString::Printf(TEXT("N2CCodeEditor.%s.%s.Preprocessor"), *LanguageId.ToString(), *ThemeName.ToString()),
        FTextBlockStyle(BaseStyle).SetColorAndOpacity(bIsPseudocode ? NormalColor : FLinearColor(Colors.Preprocessor)));
    SetOrUpdateTextStyle(*FString::Printf(TEXT("N2CCodeEditor.%s.%s.Parentheses"), *LanguageId.ToString(), *ThemeName.ToString()),
        FTextBlockStyle(BaseStyle).SetColorAndOpacity(bIsPseudocode ? NormalColor : FLinearColor(Colors.Parentheses)));
    SetOrUpdateTextStyle(*FString::Printf(TEXT("N2CCodeEditor.%s.%s.CurlyBraces"), *LanguageId.ToString(), *ThemeName.ToString()),
        FTextBlockStyle(BaseStyle).SetColorAndOpacity(bIsPseudocode ? NormalColor : FLinearColor(Colors.CurlyBraces)));
    SetOrUpdateTextStyle(*FString::Printf(TEXT("N2CCodeEditor.%s.%s.SquareBrackets"), *LanguageId.ToString(), *ThemeName.ToString()),
        FTextBlockStyle(BaseStyle).SetColorAndOpacity(bIsPseudocode ? NormalColor : FLinearColor(Colors.SquareBrackets)));
}

FTextBlockStyle FN2CCodeEditorStyle::CreateDefaultTextStyle(const FName& TypeName)
{
    // Get current font size from settings if available, otherwise use default
    int32 FontSize = 12;

    return FTextBlockStyle()
        .SetFont(DEFAULT_FONT("Mono", FontSize))
        .SetColorAndOpacity(FSlateColor(FLinearColor::White))
        .SetShadowOffset(FVector2D::ZeroVector)
        .SetShadowColorAndOpacity(FN2CUIColors::ToLinear(N2C::UI::Colors().TextShadow))
        .SetHighlightColor(FN2CUIColors::ToLinear(N2C::UI::Colors().TextHighlight));
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

void FN2CCodeEditorStyle::Reinitialize()
{
    if (!StyleSet.IsValid())
    {
        Initialize();
        return;
    }

    // Never Shutdown() here: SN2CCodeEditor's SBorder keeps the raw brush pointer it got from GetBrush and would
    // paint through freed memory on the next frame. Updating in place keeps every pointer valid and retints the
    // background of open editors right away; syntax colors copied into a highlighter apply when it is rebuilt
    check(IsInGameThread());
    ApplyThemeColors();

    // Open code editors copied the old run styles into their highlighters: let them rebuild (card 0490)
    OnThemeChanged().Broadcast();
}

FN2CCodeEditorStyle::FOnThemeChanged& FN2CCodeEditorStyle::OnThemeChanged()
{
    static FOnThemeChanged Delegate;
    return Delegate;
}

TSharedPtr<const FSlateStyleSet> FN2CCodeEditorStyle::GetStyleSetPtr()
{
    return StyleSet;
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
