// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Code Editor/Widgets/SN2CCodeEditor.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Code Editor/Syntax/N2CRichTextSyntaxHighlighter.h"
#include "Code Editor/Models/N2CCodeEditorStyle.h"

void SN2CCodeEditor::Construct(const FArguments& InArgs)
{
    CurrentLanguage = InArgs._Language;
    CurrentTheme = InArgs._ThemeName.IsNone() ? FName(TEXT("Midnight Code")) : InArgs._ThemeName;
    TabSize = 4; // Default tab size
    FontSize = 9; // Default font size
    CreateSyntaxHighlighter(CurrentLanguage);

    // Create scrollbars
    HorizontalScrollBar = SNew(SScrollBar)
        .Orientation(Orient_Horizontal)
        .Padding(2)
        .Thickness(FVector2D(6.0f, 6.0f));

    VerticalScrollBar = SNew(SScrollBar)
        .Orientation(Orient_Vertical)
        .Padding(2)
        .Thickness(FVector2D(6.0f, 6.0f));

    // Initialize the text style
    TextStyle = FN2CCodeEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("N2CCodeEditor.TextEditor.NormalText");
    TextStyle.SetFont(FCoreStyle::GetDefaultFontStyle("Mono", FontSize));
    
    // Create the main text widget first so we can reference its font size
    TSharedRef<SMultiLineEditableText> TextWidget = SAssignNew(EditableText, SMultiLineEditableText)
        .Text(InArgs._Text)
        .TextStyle(&TextStyle)
        .Marshaller(SyntaxHighlighter)
        .AutoWrapText(false)
        .OnTextChanged(this, &SN2CCodeEditor::OnTextChanged)
        .AllowMultiLine(true)
        .HScrollBar(HorizontalScrollBar)
        .VScrollBar(VerticalScrollBar);

    ChildSlot
    [
        SNew(SBorder)
        .BorderImage(FN2CCodeEditorStyle::Get().GetBrush(
            *FString::Printf(TEXT("N2CCodeEditor.%s.%s.Background"), 
                *FN2CCodeEditorStyle::GetLanguageString(CurrentLanguage),
                *CurrentTheme.ToString())))
        [
            SNew(SHorizontalBox)

            // Main editor area
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            [
                TextWidget
            ]

            // Vertical scrollbar
            + SHorizontalBox::Slot()
            .AutoWidth()
            [
                VerticalScrollBar.ToSharedRef()
            ]
        ]
    ];

    // Add horizontal scrollbar at bottom
    if (HorizontalScrollBar.IsValid())
    {
        ChildSlot
        [
            SNew(SVerticalBox)
            +SVerticalBox::Slot()
            .FillHeight(1.0f)
            [
                ChildSlot.GetWidget()
            ]
            +SVerticalBox::Slot()
            .AutoHeight()
            [
                HorizontalScrollBar.ToSharedRef()
            ]
        ];
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
    // Store current text
    FText CurrentText;
    if (EditableText.IsValid())
    {
        CurrentText = EditableText->GetText();
    }

    // Create new syntax highlighter
    CreateSyntaxHighlighter(NewLanguage);

    // Recreate the editable text widget with the new highlighter
    ChildSlot
    [
        SNew(SBorder)
        .BorderImage(FN2CCodeEditorStyle::Get().GetBrush(
            *FString::Printf(TEXT("N2CCodeEditor.%s.%s.Background"), 
                *FN2CCodeEditorStyle::GetLanguageString(NewLanguage),
                *CurrentTheme.ToString())))
        [
            SNew(SGridPanel)
            .FillColumn(0, 1.0f)
            .FillRow(0, 1.0f)

            // Main text area
            +SGridPanel::Slot(0, 0)
            [
                SAssignNew(EditableText, SMultiLineEditableText)
                .Text(CurrentText)
                .TextStyle(&FN2CCodeEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("N2CCodeEditor.TextEditor.NormalText"))
                .Marshaller(SyntaxHighlighter)
                .AutoWrapText(false)
                .OnTextChanged(this, &SN2CCodeEditor::OnTextChanged)
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
}

void SN2CCodeEditor::CreateSyntaxHighlighter(EN2CCodeLanguage Language)
{
    // Create base style with current font size
    FTextBlockStyle BaseStyle = FN2CCodeEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("N2CCodeEditor.TextEditor.NormalText");
    BaseStyle.SetFont(FCoreStyle::GetDefaultFontStyle("Mono", FontSize));
    
    SyntaxHighlighter = FN2CRichTextSyntaxHighlighter::Create(Language, CurrentTheme, BaseStyle);
}

void SN2CCodeEditor::SetTheme(const FName& NewTheme)
{
    if (CurrentTheme != NewTheme)
    {
        CurrentTheme = NewTheme;
        
        // Update the syntax highlighter with new theme
        CreateSyntaxHighlighter(CurrentLanguage);
        
        // Force a redraw
        if (EditableText.IsValid())
        {
            EditableText->Invalidate(EInvalidateWidget::Layout);
        }
    }
}

void SN2CCodeEditor::OnTextChanged(const FText& NewText)
{
    if (OnTextChangedHandler.IsBound())
    {
        OnTextChangedHandler.Execute(NewText);
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

void SN2CCodeEditor::SelectText(int32 StartLine, int32 StartColumn, int32 EndLine, int32 EndColumn)
{
    if (EditableText.IsValid())
    {
        EditableText->SelectText(
            FTextLocation(StartLine, StartColumn),
            FTextLocation(EndLine, EndColumn)
        );
    }
}

void SN2CCodeEditor::GetSelection(int32& OutStartIndex, int32& OutEndIndex) const
{
    if (EditableText.IsValid())
    {
        FTextSelection Selection = EditableText->GetSelection();
        const FTextLocation& Begin = Selection.GetBeginning();
        const FTextLocation& End = Selection.GetEnd();
        
        // Convert line/column positions to indices
        OutStartIndex = Begin.GetLineIndex();
        OutEndIndex = End.GetLineIndex();
    }
    else
    {
        OutStartIndex = 0;
        OutEndIndex = 0;
    }
}

void SN2CCodeEditor::SetFontSize(int32 NewSize)
{
    FontSize = NewSize;
    
    if (EditableText.IsValid())
    {
        // Store current text and cursor position
        FText CurrentText = EditableText->GetText();
        FTextLocation CursorLocation = EditableText->GetCursorLocation();
        
        // Update the text style with new font size
        TextStyle.SetFont(FCoreStyle::GetDefaultFontStyle("Mono", FontSize));
        
        // Recreate syntax highlighter with updated style
        SyntaxHighlighter = FN2CRichTextSyntaxHighlighter::Create(CurrentLanguage, CurrentTheme, TextStyle);
        
        // Create new text widget with updated style
        TSharedRef<SMultiLineEditableText> NewTextWidget = SNew(SMultiLineEditableText)
            .Text(CurrentText)
            .TextStyle(&TextStyle)
            .Marshaller(SyntaxHighlighter)
            .AutoWrapText(false)
            .OnTextChanged(this, &SN2CCodeEditor::OnTextChanged)
            .AllowMultiLine(true)
            .HScrollBar(HorizontalScrollBar)
            .VScrollBar(VerticalScrollBar);
            
        // Replace the old widget with the new one
        EditableText = NewTextWidget;
        
        // Restore cursor position
        EditableText->GoTo(CursorLocation);
        EditableText->ScrollTo(CursorLocation);
        
        // Update the child slot with the new widget
        ChildSlot
        [
            SNew(SBorder)
            .BorderImage(FN2CCodeEditorStyle::Get().GetBrush(
                *FString::Printf(TEXT("N2CCodeEditor.%s.%s.Background"), 
                    *FN2CCodeEditorStyle::GetLanguageString(CurrentLanguage),
                    *CurrentTheme.ToString())))
            [
                SNew(SGridPanel)
                .FillColumn(0, 1.0f)
                .FillRow(0, 1.0f)

                // Main text area
                +SGridPanel::Slot(0, 0)
                [
                    NewTextWidget
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
    }
}

void SN2CCodeEditor::SetWordWrap(bool bEnable)
{
    if (EditableText.IsValid())
    {
        EditableText->SetWrappingPolicy(bEnable ? ETextWrappingPolicy::DefaultWrapping : ETextWrappingPolicy::AllowPerCharacterWrapping);
    }
}

void SN2CCodeEditor::SetTabSize(int32 NewSize)
{
    // Store the new tab size
    TabSize = NewSize;

    // Store current text
    FText CurrentText;
    if (EditableText.IsValid())
    {
        CurrentText = EditableText->GetText();
    }

    // Create new syntax highlighter
    CreateSyntaxHighlighter(CurrentLanguage);

    // Recreate the editable text widget with the new highlighter
    ChildSlot
    [
        SNew(SBorder)
        .BorderImage(FN2CCodeEditorStyle::Get().GetBrush(
            *FString::Printf(TEXT("N2CCodeEditor.%s.%s.Background"), 
                *FN2CCodeEditorStyle::GetLanguageString(CurrentLanguage),
                *CurrentTheme.ToString())))
        [
            SNew(SGridPanel)
            .FillColumn(0, 1.0f)
            .FillRow(0, 1.0f)

            // Main text area
            +SGridPanel::Slot(0, 0)
            [
                SAssignNew(EditableText, SMultiLineEditableText)
                .Text(CurrentText)
                .TextStyle(&FN2CCodeEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("N2CCodeEditor.TextEditor.NormalText"))
                .Marshaller(SyntaxHighlighter)
                .AutoWrapText(false)
                .OnTextChanged(this, &SN2CCodeEditor::OnTextChanged)
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
}
