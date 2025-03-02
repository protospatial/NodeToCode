// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Code Editor/Models/N2CCodeLanguage.h"

class SMultiLineEditableText;
class FN2CRichTextSyntaxHighlighter;

/**
 * Core code editor widget that provides syntax highlighting and basic editing capabilities
 */
class SN2CCodeEditor : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SN2CCodeEditor)
        : _Text()
        , _Language(EN2CCodeLanguage::Cpp)
        , _ThemeName(TEXT("Unreal Engine"))
    {}
        /** Initial text content */
        SLATE_ATTRIBUTE(FText, Text)

        /** Programming language for syntax highlighting */
        SLATE_ARGUMENT(EN2CCodeLanguage, Language)

        /** Theme name for syntax highlighting */
        SLATE_ARGUMENT(FName, ThemeName)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

    /** Get the current text content */
    FText GetText() const;

    /** Set new text content */
    void SetText(const FText& NewText);

    /** Change the programming language */
    void SetLanguage(EN2CCodeLanguage NewLanguage);

    /** Insert text at cursor position */
    void InsertTextAtCursor(const FString& InText);

    /** Replace selected text */
    void ReplaceSelectedText(const FString& NewText);

    /** Delete selected text */
    void DeleteSelectedText();

    /** Get selected text */
    FText GetSelectedText() const;

    /** Set cursor position */
    void SetCursorPosition(int32 Line, int32 Column);

    /** Get cursor position */
    void GetCursorPosition(int32& OutLine, int32& OutColumn) const;

    /** Select text range using line and column positions */
    void SelectText(int32 StartLine, int32 StartColumn, int32 EndLine, int32 EndColumn);

    /** Get selection range */
    void GetSelection(int32& OutStartIndex, int32& OutEndIndex) const;

    /** Set font size */
    void SetFontSize(int32 NewSize);

    /** Set word wrap */
    void SetWordWrap(bool bEnable);

    /** Set tab size */
    void SetTabSize(int32 NewSize);

    /** Set the current theme */
    void SetTheme(const FName& NewTheme);

private:
    /** Horizontal scrollbar widget */
    TSharedPtr<SScrollBar> HorizontalScrollBar;

    /** Vertical scrollbar widget */
    TSharedPtr<SScrollBar> VerticalScrollBar;

    /** The actual editable text widget */
    TSharedPtr<SMultiLineEditableText> EditableText;

    /** Current syntax highlighter */
    TSharedPtr<FN2CRichTextSyntaxHighlighter> SyntaxHighlighter;

    /** Current language */
    EN2CCodeLanguage CurrentLanguage;

    /** Current theme name */
    FName CurrentTheme;

    /** Current tab size */
    int32 TabSize;

    /** Current font size */
    int32 FontSize;
    
    /** Current text style */
    FTextBlockStyle TextStyle;

    /** Create a new syntax highlighter for the specified language */
    void CreateSyntaxHighlighter(EN2CCodeLanguage Language);

    /** Delegate for text changes */
    DECLARE_DELEGATE_OneParam(FOnTextChanged, const FText&);

    /** Set the callback for text changes */
    void SetOnTextChanged(const FOnTextChanged& InHandler) { OnTextChangedHandler = InHandler; }

private:
    /** Handler for text changes */
    FOnTextChanged OnTextChangedHandler;

public:
    /** Internal callback for text changes */
    void OnTextChanged(const FText& NewText);

    /** Convert language enum to string */
    static FString GetLanguageString(EN2CCodeLanguage Language)
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
};
