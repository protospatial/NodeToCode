// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Code Editor/Widgets/SN2CCodeEditor.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Code Editor/Syntax/N2CRichTextSyntaxHighlighter.h"
#include "Code Editor/Models/N2CCodeEditorStyle.h"
#include "Models/N2CStyle.h"
#include "Runtime/Launch/Resources/Version.h"

const FName SN2CCodeEditor::CurrentTheme = FName(TEXT("Unreal Engine"));

void SN2CCodeEditor::Construct(const FArguments& InArgs)
{
    CurrentLanguage = InArgs._Language;
    TabSize = 4; // Default tab size
    FontSize = 9; // Default font size
    CreateSyntaxHighlighter(CurrentLanguage);

    // Create scrollbars
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
        .BorderImage(UpdateBackgroundBrush(CurrentLanguage))
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
        .BorderImage(UpdateBackgroundBrush(NewLanguage))
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
            .BorderImage(UpdateBackgroundBrush(CurrentLanguage))
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
