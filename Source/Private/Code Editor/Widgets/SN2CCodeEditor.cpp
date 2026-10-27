// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Code Editor/Widgets/SN2CCodeEditor.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Code Editor/Syntax/N2CRichTextSyntaxHighlighter.h"
#include "Code Editor/Models/N2CCodeEditorStyle.h"
#include "Core/Style/N2CStyle.h"
#include "Runtime/Launch/Resources/Version.h"

const FName SN2CCodeEditor::CurrentTheme = FName(TEXT("Unreal Engine"));

void SN2CCodeEditor::Construct(const FArguments& InArgs)
{
    CurrentLanguage = InArgs._Language;
    FontSize = FMath::Max(1, InArgs._FontSize);
    TabSize = FMath::Max(1, InArgs._TabSize);
    bWordWrap = InArgs._WordWrap;
    OnTextChangedDelegate = InArgs._OnTextChanged;
    OnCursorMovedDelegate = InArgs._OnCursorMoved;
    OnSelectionChangedDelegate = InArgs._OnSelectionChanged;

    HorizontalScrollBar = SNew(SScrollBar)
        .Style(&N2CStyle::GetScrollBarStyle())
        .Orientation(Orient_Horizontal)
        .Padding(2)
        .Thickness(FVector2D(6.0f, 6.0f));

    VerticalScrollBar = SNew(SScrollBar)
        .Style(&N2CStyle::GetScrollBarStyle())
        .Orientation(Orient_Vertical)
        .Padding(2)
        .Thickness(FVector2D(6.0f, 6.0f));

    RebuildEditor(InArgs._Text);
}

void SN2CCodeEditor::RebuildEditor(TOptional<TAttribute<FText>> InitialText)
{
    // Keep the text and the cursor across the rebuild (Construct passes the Text argument instead)
    TAttribute<FText> CurrentText;
    FTextLocation CursorLocation;
    bool bRestoreCursor = false;
    if (InitialText.IsSet())
    {
        CurrentText = InitialText.GetValue();
    }
    else if (EditableText.IsValid())
    {
        CurrentText = EditableText->GetText();
        CursorLocation = EditableText->GetCursorLocation();
        bRestoreCursor = true;
    }

    // The widget and every run start from the same style: the theme's normal text at FontSize
    TextStyle = FN2CCodeEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("N2CCodeEditor.TextEditor.NormalText");
    TextStyle.SetFont(FCoreStyle::GetDefaultFontStyle("Mono", FontSize));

    SyntaxHighlighter = bJsonSyntax
        ? FN2CRichTextSyntaxHighlighter::CreateJson(CurrentTheme, TextStyle)
        : FN2CRichTextSyntaxHighlighter::Create(CurrentLanguage, CurrentTheme, TextStyle);
    SyntaxHighlighter->SetTabSize(TabSize);

    ChildSlot
    [
        SNew(SBorder)
        .BorderImage(UpdateBackgroundBrush(CurrentLanguage))
        [
            SNew(SGridPanel)
            .FillColumn(0, 1.0f)
            .FillRow(0, 1.0f)

            // Main text area
            +SGridPanel::Slot(0, 0)
            [
                SAssignNew(EditableText, SMultiLineEditableText)
                .Text(CurrentText)
                .TextStyle(&TextStyle)
                .Marshaller(SyntaxHighlighter)
                .AutoWrapText(bWordWrap)
                .OnTextChanged(this, &SN2CCodeEditor::HandleTextChanged)
                .OnCursorMoved(this, &SN2CCodeEditor::HandleCursorMoved)
                .AllowMultiLine(true)
                .HScrollBar(HorizontalScrollBar)
                .VScrollBar(VerticalScrollBar)
            ]

            // Vertical scrollbar
            +SGridPanel::Slot(1, 0)
            .Padding(0)
            [
                VerticalScrollBar.ToSharedRef()
            ]

            // Horizontal scrollbar
            +SGridPanel::Slot(0, 1)
            .Padding(0)
            [
                HorizontalScrollBar.ToSharedRef()
            ]
        ]
    ];

    if (bRestoreCursor)
    {
        EditableText->GoTo(CursorLocation);
    }
}

FText SN2CCodeEditor::GetText() const
{
    return EditableText.IsValid() ? EditableText->GetText() : FText::GetEmpty();
}

void SN2CCodeEditor::SetText(const FText& NewText)
{
    if (EditableText.IsValid())
    {
        EditableText->SetText(NewText);
    }
}

void SN2CCodeEditor::SetLanguage(EN2CCodeLanguage NewLanguage)
{
    // Remembered, so a later font size or tab size change rebuilds in this language, not the first one
    CurrentLanguage = NewLanguage;
    bJsonSyntax = false;
    RebuildEditor();
}

void SN2CCodeEditor::SetJsonSyntax()
{
    // The background stays the current language's; only the highlighting changes
    bJsonSyntax = true;
    RebuildEditor();
}

void SN2CCodeEditor::HandleTextChanged(const FText& NewText)
{
    OnTextChangedDelegate.ExecuteIfBound(NewText);
}

void SN2CCodeEditor::HandleCursorMoved(const FTextLocation& Location)
{
    OnCursorMovedDelegate.ExecuteIfBound(Location.GetLineIndex(), Location.GetOffset());

    // Typing, clicking and shift-moving change the selection by moving the cursor (it runs from an anchor to it)
    ReportSelectionIfChanged();
}

void SN2CCodeEditor::ReportSelectionIfChanged()
{
    int32 StartLine = 0;
    int32 EndLine = 0;
    GetSelection(StartLine, EndLine);
    if (StartLine != LastSelectionStartLine || EndLine != LastSelectionEndLine)
    {
        LastSelectionStartLine = StartLine;
        LastSelectionEndLine = EndLine;
        OnSelectionChangedDelegate.ExecuteIfBound(StartLine, EndLine);
    }
}

void SN2CCodeEditor::InsertTextAtCursor(const FString& InText)
{
    if (EditableText.IsValid())
    {
        EditableText->InsertTextAtCursor(InText);
    }
}

void SN2CCodeEditor::ReplaceSelectedText(const FString& NewText)
{
    if (EditableText.IsValid())
    {
        EditableText->InsertTextAtCursor(NewText);
    }
}

void SN2CCodeEditor::DeleteSelectedText()
{
    if (EditableText.IsValid())
    {
        EditableText->DeleteSelectedText();
    }
}

FText SN2CCodeEditor::GetSelectedText() const
{
    if (EditableText.IsValid())
    {
        return EditableText->GetSelectedText();
    }
    return FText::GetEmpty();
}

void SN2CCodeEditor::SetCursorPosition(int32 Line, int32 Column)
{
    if (EditableText.IsValid())
    {
        EditableText->GoTo(FTextLocation(Line, Column));
        EditableText->ScrollTo(FTextLocation(Line, Column));
    }
}

void SN2CCodeEditor::GetCursorPosition(int32& OutLine, int32& OutColumn) const
{
    if (EditableText.IsValid())
    {
        FTextLocation Location = EditableText->GetCursorLocation();
        OutLine = Location.GetLineIndex();
        OutColumn = Location.GetOffset();
    }
    else
    {
        OutLine = 0;
        OutColumn = 0;
    }
}

const FSlateBrush* SN2CCodeEditor::UpdateBackgroundBrush(EN2CCodeLanguage Language)
{
    // The style set lives until module shutdown and Reinitialize retints this brush in place, so the raw pointer
    // the SBorder keeps stays valid for the widget's lifetime
    BackgroundBrush = FN2CCodeEditorStyle::Get().GetBrush(
        *FString::Printf(TEXT("N2CCodeEditor.%s.%s.Background"),
            *FN2CCodeEditorStyle::GetLanguageString(Language),
            *CurrentTheme.ToString()));
    return BackgroundBrush;
}

int32 SN2CCodeEditor::GetLineCount() const
{
    if (!EditableText.IsValid())
    {
        return 0;
    }
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8
    // 5.8 deprecates the non-const overload
    return static_cast<const SMultiLineEditableText*>(EditableText.Get())->GetTextLineCount();
#else
    return EditableText->GetTextLineCount();
#endif
}

FTextLocation SN2CCodeEditor::ClampLocation(int32 Line, int32 Column) const
{
    const int32 NumLines = GetLineCount();
    if (NumLines <= 0)
    {
        return FTextLocation(0, 0);
    }

    // FSlateEditableTextLayout::SelectText indexes the line array without bounds checks, so clamp here
    const int32 ClampedLine = FMath::Clamp(Line, 0, NumLines - 1);
    FString LineText;
    EditableText->GetTextLine(ClampedLine, LineText);
    return FTextLocation(ClampedLine, FMath::Clamp(Column, 0, LineText.Len()));
}

void SN2CCodeEditor::SelectText(int32 StartLine, int32 StartColumn, int32 EndLine, int32 EndColumn)
{
    if (EditableText.IsValid() && GetLineCount() > 0)
    {
        EditableText->SelectText(ClampLocation(StartLine, StartColumn), ClampLocation(EndLine, EndColumn));
        // SelectText does not report a cursor move
        ReportSelectionIfChanged();
    }
}

void SN2CCodeEditor::GetSelection(int32& OutStartLine, int32& OutEndLine) const
{
    int32 StartColumn = 0;
    int32 EndColumn = 0;
    GetSelection(OutStartLine, StartColumn, OutEndLine, EndColumn);
}

void SN2CCodeEditor::GetSelection(int32& OutStartLine, int32& OutStartColumn, int32& OutEndLine, int32& OutEndColumn) const
{
    if (EditableText.IsValid())
    {
        const FTextSelection Selection = EditableText->GetSelection();
        const FTextLocation& Begin = Selection.GetBeginning();
        const FTextLocation& End = Selection.GetEnd();
        OutStartLine = Begin.GetLineIndex();
        OutStartColumn = Begin.GetOffset();
        OutEndLine = End.GetLineIndex();
        OutEndColumn = End.GetOffset();
    }
    else
    {
        OutStartLine = 0;
        OutStartColumn = 0;
        OutEndLine = 0;
        OutEndColumn = 0;
    }
}

void SN2CCodeEditor::SetFontSize(int32 NewSize)
{
    if (NewSize > 0 && NewSize != FontSize)
    {
        FontSize = NewSize;
        RebuildEditor();
    }
}

void SN2CCodeEditor::SetWordWrap(bool bEnable)
{
    bWordWrap = bEnable;
    if (EditableText.IsValid())
    {
        EditableText->SetAutoWrapText(bWordWrap);
    }
}

void SN2CCodeEditor::SetTabSize(int32 NewSize)
{
    if (NewSize > 0 && NewSize != TabSize)
    {
        TabSize = NewSize;
        RebuildEditor();
    }
}
