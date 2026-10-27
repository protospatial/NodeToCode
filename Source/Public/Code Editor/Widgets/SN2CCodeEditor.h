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
    /** The text changed (typed, pasted, or set) */
    DECLARE_DELEGATE_OneParam(FOnTextChanged, const FText& /*NewText*/);

    /** The cursor moved: zero-based line and column */
    DECLARE_DELEGATE_TwoParams(FOnCursorMoved, int32 /*Line*/, int32 /*Column*/);

    /** The selection changed: its first and last zero-based line (equal when nothing is selected) */
    DECLARE_DELEGATE_TwoParams(FOnSelectionChanged, int32 /*StartLine*/, int32 /*EndLine*/);

    SLATE_BEGIN_ARGS(SN2CCodeEditor)
        : _Text()
        , _Language(EN2CCodeLanguage::Cpp)
        , _FontSize(9)
        , _TabSize(4)
        , _WordWrap(false)
    {}
        /** Initial text content */
        SLATE_ATTRIBUTE(FText, Text)

        /** Programming language for syntax highlighting */
        SLATE_ARGUMENT(EN2CCodeLanguage, Language)

        /** Font size in points */
        SLATE_ARGUMENT(int32, FontSize)

        /** Spaces a tab character is drawn as */
        SLATE_ARGUMENT(int32, TabSize)

        /** Wrap long lines at the widget's width */
        SLATE_ARGUMENT(bool, WordWrap)

        SLATE_EVENT(FOnTextChanged, OnTextChanged)
        SLATE_EVENT(FOnCursorMoved, OnCursorMoved)
        SLATE_EVENT(FOnSelectionChanged, OnSelectionChanged)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

    /** Get the current text content */
    FText GetText() const;

    /** Set new text content */
    void SetText(const FText& NewText);

    /** Change the programming language (and leave JSON highlighting) */
    void SetLanguage(EN2CCodeLanguage NewLanguage);

    /** The language the editor highlights (and takes its background from) */
    EN2CCodeLanguage GetLanguage() const { return CurrentLanguage; }

    /** Highlight as JSON (the translation viewer's JSON tab) until the next SetLanguage */
    void SetJsonSyntax();

    /** True while the editor highlights as JSON */
    bool IsJsonSyntax() const { return bJsonSyntax; }

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

    /**
     * Select text range using line and column positions. Lines are clamped to [0, NumLines - 1] and columns to
     * [0, line length], so out-of-range values from Blueprint or Python select up to the document edge
     */
    void SelectText(int32 StartLine, int32 StartColumn, int32 EndLine, int32 EndColumn);

    /** Get the first and last line of the selection (zero-based line indices, not character offsets) */
    void GetSelection(int32& OutStartLine, int32& OutEndLine) const;

    /** Get the selection as zero-based line and column positions */
    void GetSelection(int32& OutStartLine, int32& OutStartColumn, int32& OutEndLine, int32& OutEndColumn) const;

    /** Number of lines in the text layout */
    int32 GetLineCount() const;

    /** Background brush the border paints with (owned by FN2CCodeEditorStyle's style set) */
    const FSlateBrush* GetBackgroundBrush() const { return BackgroundBrush; }

    /** Set font size */
    void SetFontSize(int32 NewSize);
    int32 GetFontSize() const { return FontSize; }

    /** Set word wrap: long lines wrap at the widget's width */
    void SetWordWrap(bool bEnable);
    bool GetWordWrap() const { return bWordWrap; }

    /** Set tab size: the number of spaces a tab is drawn as */
    void SetTabSize(int32 NewSize);
    int32 GetTabSize() const { return TabSize; }

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
    EN2CCodeLanguage CurrentLanguage = EN2CCodeLanguage::Cpp;

    /** Highlight as JSON instead of CurrentLanguage (font, tab size and theme rebuilds keep it) */
    bool bJsonSyntax = false;

    /** Theme name (single default) */
    static const FName CurrentTheme;

    /** Current tab size */
    int32 TabSize = 4;

    /** Current font size */
    int32 FontSize = 9;

    /** Whether long lines wrap */
    bool bWordWrap = false;

    /** The style the text widget and the highlighter's runs start from: the theme's normal text at FontSize */
    FTextBlockStyle TextStyle;

    /** Background brush for the current language, looked up from FN2CCodeEditorStyle */
    const FSlateBrush* BackgroundBrush = nullptr;

    /** The last selection reported through OnSelectionChanged */
    int32 LastSelectionStartLine = 0;
    int32 LastSelectionEndLine = 0;

    FOnTextChanged OnTextChangedDelegate;
    FOnCursorMoved OnCursorMovedDelegate;
    FOnSelectionChanged OnSelectionChangedDelegate;

    /** Looks up and stores the background brush for a language */
    const FSlateBrush* UpdateBackgroundBrush(EN2CCodeLanguage Language);

    /** Clamps a line/column pair to the current text layout */
    FTextLocation ClampLocation(int32 Line, int32 Column) const;

    /**
     * The one place the highlighter and the text widget are built: CurrentLanguage (or JSON), TextStyle at FontSize,
     * TabSize and bWordWrap. Keeps the text and the cursor.
     */
    void RebuildEditor(TOptional<TAttribute<FText>> InitialText = TOptional<TAttribute<FText>>());

    /** Text widget callbacks, forwarded to the delegates */
    void HandleTextChanged(const FText& NewText);
    void HandleCursorMoved(const FTextLocation& Location);

    /** Fires OnSelectionChanged when the selection's first or last line differs from the last one reported */
    void ReportSelectionIfChanged();

public:
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
