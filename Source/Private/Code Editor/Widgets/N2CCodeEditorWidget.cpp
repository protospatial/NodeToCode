// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Code Editor/Widgets/N2CCodeEditorWidget.h"
#include "Models/N2CLogging.h"
#include "Code Editor/Widgets/SN2CCodeEditor.h"
#include "Core/N2CSettings.h"

#define LOCTEXT_NAMESPACE "N2CCodeEditorWidget"

UN2CCodeEditorWidget::UN2CCodeEditorWidget()
    : Language(EN2CCodeLanguage::Cpp)
    , FontSize(9)
    , bWordWrap(false)
    , TabSize(4)
{

  N2C_LOG_FUNCTION_ENTRY();
  }

TSharedRef<SWidget> UN2CCodeEditorWidget::RebuildWidget()
{
    N2C_LOG_FUNCTION_ENTRY();
    CodeEditorWidget = SNew(SN2CCodeEditor)
        .Text(Text)
        .Language(Language)
        .ThemeName(ThemeName);
        
    // Apply initial properties
    if (CodeEditorWidget.IsValid())
    {
        CodeEditorWidget->SetFontSize(FontSize);
        CodeEditorWidget->SetWordWrap(bWordWrap);
        CodeEditorWidget->SetTabSize(TabSize);
    }

    return CodeEditorWidget.ToSharedRef();
}

void UN2CCodeEditorWidget::SynchronizeProperties()
{
    N2C_LOG_FUNCTION_ENTRY();
    Super::SynchronizeProperties();

    if (CodeEditorWidget.IsValid())
    {
        CodeEditorWidget->SetText(Text);
        CodeEditorWidget->SetLanguage(Language);
    }
}

FText UN2CCodeEditorWidget::GetText() const
{
    N2C_LOG_FUNCTION_ENTRY();
    if (CodeEditorWidget.IsValid())
    {
        return CodeEditorWidget->GetText();
    }
    return Text;
}

void UN2CCodeEditorWidget::SetText(const FText& NewText)
{
    N2C_LOG_FUNCTION_ENTRY();
    Text = NewText;
    if (CodeEditorWidget.IsValid())
    {
        CodeEditorWidget->SetText(NewText);
    }
}

void UN2CCodeEditorWidget::SetLanguage(EN2CCodeLanguage NewLanguage)
{
    N2C_LOG_FUNCTION_ENTRY();
    Language = NewLanguage;
    if (CodeEditorWidget.IsValid())
    {
        CodeEditorWidget->SetLanguage(NewLanguage);
    }
}

void UN2CCodeEditorWidget::HandleTextChanged(const FText& NewText)
{
    N2C_LOG_FUNCTION_ENTRY();
    Text = NewText;
    OnTextChanged.Broadcast(NewText);
}

void UN2CCodeEditorWidget::InsertTextAtCursor(const FString& InText)
{
    N2C_LOG_FUNCTION_ENTRY();
    if (CodeEditorWidget.IsValid())
    {
        CodeEditorWidget->InsertTextAtCursor(InText);
    }
}

void UN2CCodeEditorWidget::ReplaceSelectedText(const FString& NewText)
{
    N2C_LOG_FUNCTION_ENTRY();
    if (CodeEditorWidget.IsValid())
    {
        CodeEditorWidget->ReplaceSelectedText(NewText);
    }
}

void UN2CCodeEditorWidget::DeleteSelectedText()
{
    N2C_LOG_FUNCTION_ENTRY();
    if (CodeEditorWidget.IsValid())
    {
        CodeEditorWidget->DeleteSelectedText();
    }
}

FText UN2CCodeEditorWidget::GetSelectedText() const
{
    N2C_LOG_FUNCTION_ENTRY();
    if (CodeEditorWidget.IsValid())
    {
        return CodeEditorWidget->GetSelectedText();
    }
    return FText::GetEmpty();
}

void UN2CCodeEditorWidget::SetCursorPosition(int32 Line, int32 Column)
{
    N2C_LOG_FUNCTION_ENTRY();
    if (CodeEditorWidget.IsValid())
    {
        CodeEditorWidget->SetCursorPosition(Line, Column);
    }
}

void UN2CCodeEditorWidget::GetCursorPosition(int32& OutLine, int32& OutColumn) const
{
    N2C_LOG_FUNCTION_ENTRY();
    if (CodeEditorWidget.IsValid())
    {
        CodeEditorWidget->GetCursorPosition(OutLine, OutColumn);
    }
    else
    {
        OutLine = 0;
        OutColumn = 0;
    }
}

void UN2CCodeEditorWidget::SelectText(int32 StartLine, int32 StartColumn, int32 EndLine, int32 EndColumn)
{
    N2C_LOG_FUNCTION_ENTRY();
    if (CodeEditorWidget.IsValid())
    {
        CodeEditorWidget->SelectText(StartLine, StartColumn, EndLine, EndColumn);
    }
}

void UN2CCodeEditorWidget::GetSelection(int32& OutStartIndex, int32& OutEndIndex) const
{
    N2C_LOG_FUNCTION_ENTRY();
    if (CodeEditorWidget.IsValid())
    {
        CodeEditorWidget->GetSelection(OutStartIndex, OutEndIndex);
    }
    else
    {
        OutStartIndex = 0;
        OutEndIndex = 0;
    }
}

void UN2CCodeEditorWidget::SetFontSize(int32 NewSize)
{
    N2C_LOG_FUNCTION_ENTRY();
    FontSize = FMath::Clamp(NewSize, 8, 72);
    if (CodeEditorWidget.IsValid())
    {
        CodeEditorWidget->SetFontSize(FontSize);
    }
}

void UN2CCodeEditorWidget::SetWordWrap(bool bEnable)
{
    N2C_LOG_FUNCTION_ENTRY();
    bWordWrap = bEnable;
    if (CodeEditorWidget.IsValid())
    {
        CodeEditorWidget->SetWordWrap(bWordWrap);
    }
}

void UN2CCodeEditorWidget::SetTheme(const FName& NewTheme)
{
    N2C_LOG_FUNCTION_ENTRY();
    ThemeName = NewTheme;
    if (CodeEditorWidget.IsValid())
    {
        CodeEditorWidget->SetTheme(NewTheme);
        SynchronizeProperties();
    }
    
}

void UN2CCodeEditorWidget::SetTabSize(int32 NewSize)
{
    N2C_LOG_FUNCTION_ENTRY();
    TabSize = FMath::Clamp(NewSize, 1, 8);
    if (CodeEditorWidget.IsValid())
    {
        CodeEditorWidget->SetTabSize(TabSize);
    }
}

void UN2CCodeEditorWidget::ReleaseSlateResources(bool bReleaseChildren)
{
    N2C_LOG_FUNCTION_ENTRY();
    Super::ReleaseSlateResources(bReleaseChildren);
    
    // Release the Slate widget
    CodeEditorWidget.Reset();
}

#undef LOCTEXT_NAMESPACE
TArray<FName> UN2CCodeEditorWidget::GetAvailableThemes() const
{
    N2C_LOG_FUNCTION_ENTRY();
    TArray<FName> ThemeNames;
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    
    switch (Language)
    {
        case EN2CCodeLanguage::Cpp:
            Settings->CPPThemes.Themes.GetKeys(ThemeNames);
            break;
        case EN2CCodeLanguage::Python:
            Settings->PythonThemes.Themes.GetKeys(ThemeNames);
            break;
        case EN2CCodeLanguage::JavaScript:
            Settings->JavaScriptThemes.Themes.GetKeys(ThemeNames);
            break;
        case EN2CCodeLanguage::CSharp:
            Settings->CSharpThemes.Themes.GetKeys(ThemeNames);
            break;
        case EN2CCodeLanguage::Swift:
            Settings->SwiftThemes.Themes.GetKeys(ThemeNames);
            break;
        case EN2CCodeLanguage::Pseudocode:
            Settings->PseudocodeThemes.Themes.GetKeys(ThemeNames);
            break;
    }
    
    return ThemeNames;
}

FN2CCodeEditorColors UN2CCodeEditorWidget::GetThemeFromName(const FName Name) const
{
    N2C_LOG_FUNCTION_ENTRY();
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    
    switch (Language)
    {
    case EN2CCodeLanguage::Cpp:
        return *Settings->CPPThemes.Themes.Find(Name);
    case EN2CCodeLanguage::Python:
        return *Settings->PythonThemes.Themes.Find(Name);
    case EN2CCodeLanguage::JavaScript:
        return *Settings->JavaScriptThemes.Themes.Find(Name);
    case EN2CCodeLanguage::CSharp:
        return *Settings->CSharpThemes.Themes.Find(Name);
    case EN2CCodeLanguage::Swift:
        return *Settings->SwiftThemes.Themes.Find(Name);
    case EN2CCodeLanguage::Pseudocode:
        return *Settings->PseudocodeThemes.Themes.Find(Name);
    }

    const FName DefaultThemeName = "Unreal Engine";

    return *Settings->CPPThemes.Themes.Find(DefaultThemeName);
    
}

FName UN2CCodeEditorWidget::GetCurrentTheme() const
{
    N2C_LOG_FUNCTION_ENTRY();
    return ThemeName;
}
