// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Code Editor/Models/N2CCodeEditorStyle.h"
#include "Code Editor/Syntax/N2CRichTextSyntaxHighlighter.h"
#include "Code Editor/Syntax/N2CWhiteSpaceRun.h"
#include "Code Editor/Widgets/N2CCodeEditorWidget.h"
#include "Code Editor/Widgets/SN2CCodeEditor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Text/SlateTextLayout.h"
#include "Tests/N2CTestHelpers.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Widgets/Text/SlateEditableTextLayout.h"
#include "Runtime/Launch/Resources/Version.h"

N2C_TEST_PRIVATE_MEMBER(FN2CCodeEditorSpecHighlighter, TSharedPtr<FN2CRichTextSyntaxHighlighter> SN2CCodeEditor::*,
	&SN2CCodeEditor::SyntaxHighlighter)
N2C_TEST_PRIVATE_MEMBER(FN2CCodeEditorSpecText, TSharedPtr<SMultiLineEditableText> SN2CCodeEditor::*,
	&SN2CCodeEditor::EditableText)
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8
N2C_TEST_PRIVATE_MEMBER(FN2CCodeEditorSpecLayout, FSlateEditableTextLayout SMultiLineEditableText::*,
	&SMultiLineEditableText::SlateEditableTextLayout)
#else
N2C_TEST_PRIVATE_MEMBER(FN2CCodeEditorSpecLayout, TUniquePtr<FSlateEditableTextLayout> SMultiLineEditableText::*,
	&SMultiLineEditableText::EditableTextLayout)
#endif
N2C_TEST_PRIVATE_MEMBER(FN2CCodeEditorSpecUmgSlate, TSharedPtr<SN2CCodeEditor> UN2CCodeEditorWidget::*,
	&UN2CCodeEditorWidget::CodeEditorWidget)
N2C_TEST_PRIVATE_MEMBER(FN2CCodeEditorSpecUmgText, FText UN2CCodeEditorWidget::*, &UN2CCodeEditorWidget::Text)
N2C_TEST_PRIVATE_MEMBER(FN2CCodeEditorSpecUmgSync, void (UN2CCodeEditorWidget::*)(), &UN2CCodeEditorWidget::SynchronizeProperties)
N2C_TEST_PRIVATE_MEMBER(FN2CCodeEditorSpecCursorDelegate, SN2CCodeEditor::FOnCursorMoved SN2CCodeEditor::*,
	&SN2CCodeEditor::OnCursorMovedDelegate)
N2C_TEST_PRIVATE_MEMBER(FN2CCodeEditorSpecSelectionDelegate, SN2CCodeEditor::FOnSelectionChanged SN2CCodeEditor::*,
	&SN2CCodeEditor::OnSelectionChangedDelegate)

/**
 * SN2CCodeEditor's setters and the UMG wrapper's events (card 0264). Before, the tab size never reached the
 * highlighter's whitespace runs (always 4 spaces), SetWordWrap changed the wrapping policy of a widget built with
 * AutoWrapText(false), nothing bound the Slate widget's text-changed callback, so the UMG OnTextChanged,
 * OnCursorMoved and OnSelectionChanged never fired, and SynchronizeProperties pushed the stale Text back over what
 * the user typed.
 */
BEGIN_DEFINE_SPEC(FN2CCodeEditorWidgetSpec, "NodeToCode.UI.CodeEditor", N2C_TEST_FLAGS)

	bool SlateReady()
	{
		if (!FSlateApplication::IsInitialized())
		{
			AddInfo(TEXT("Slate is not initialized; skipping"));
			return false;
		}
		FN2CCodeEditorStyle::Initialize(); // no-op when the module already did it
		return true;
	}

	/** The runs the editor's own highlighter makes for Source: name and the run itself, per line */
	static TArray<TArray<TPair<FString, TSharedRef<const IRun>>>> HighlightWith(const TSharedRef<SN2CCodeEditor>& Editor, const FString& Source)
	{
		TArray<TArray<TPair<FString, TSharedRef<const IRun>>>> Lines;
		const TSharedPtr<FN2CRichTextSyntaxHighlighter> Highlighter = (*Editor).*N2CTest::GetPrivateMember(N2CTest::FN2CCodeEditorSpecHighlighter());
		if (!Highlighter.IsValid())
		{
			return Lines;
		}
		const TSharedRef<SBox> Owner = SNew(SBox);
		const TSharedRef<FSlateTextLayout> Layout = FSlateTextLayout::Create(&Owner.Get(), FTextBlockStyle());
		Highlighter->SetText(Source, *Layout);
		for (const FTextLayout::FLineModel& Line : Layout->GetLineModels())
		{
			auto& Runs = Lines.AddDefaulted_GetRef();
			for (const FTextLayout::FRunModel& Run : Line.Runs)
			{
				Runs.Emplace(Run.GetRun()->GetRunInfo().Name, Run.GetRun());
			}
		}
		return Lines;
	}

	/** The run name of the first run whose text starts at Offset on line 0, or "" */
	static FString KindOfWord(const TSharedRef<SN2CCodeEditor>& Editor, const FString& Source, const FString& Word)
	{
		const TSharedPtr<FN2CRichTextSyntaxHighlighter> Highlighter = (*Editor).*N2CTest::GetPrivateMember(N2CTest::FN2CCodeEditorSpecHighlighter());
		const TSharedRef<SBox> Owner = SNew(SBox);
		const TSharedRef<FSlateTextLayout> Layout = FSlateTextLayout::Create(&Owner.Get(), FTextBlockStyle());
		Highlighter->SetText(Source, *Layout);
		for (const FTextLayout::FLineModel& Line : Layout->GetLineModels())
		{
			for (const FTextLayout::FRunModel& Run : Line.Runs)
			{
				const FTextRange Range = Run.GetTextRange();
				if (Line.Text->Mid(Range.BeginIndex, Range.Len()) == Word)
				{
					return Run.GetRun()->GetRunInfo().Name;
				}
			}
		}
		return FString();
	}

	/** Spaces per tab of the first whitespace run holding a tab, or -1 */
	static int32 TabSpacesOf(const TSharedRef<SN2CCodeEditor>& Editor)
	{
		for (const auto& Line : HighlightWith(Editor, TEXT("\tValue = 1;")))
		{
			for (const auto& Run : Line)
			{
				if (Run.Key == TEXT("WhiteSpace"))
				{
					return StaticCastSharedRef<const FN2CWhiteSpaceRun>(Run.Value)->GetNumSpacesPerTab();
				}
			}
		}
		return -1;
	}

	/** Whether the editor's text widget wraps at its width (its layout's AutoWrapText) */
	static bool AutoWrapOf(const TSharedRef<SN2CCodeEditor>& Editor)
	{
		const TSharedPtr<SMultiLineEditableText> Text = (*Editor).*N2CTest::GetPrivateMember(N2CTest::FN2CCodeEditorSpecText());
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8
		return ((*Text).*N2CTest::GetPrivateMember(N2CTest::FN2CCodeEditorSpecLayout())).GetAutoWrapText();
#else
		return ((*Text).*N2CTest::GetPrivateMember(N2CTest::FN2CCodeEditorSpecLayout()))->GetAutoWrapText();
#endif
	}

END_DEFINE_SPEC(FN2CCodeEditorWidgetSpec)

void FN2CCodeEditorWidgetSpec::Define()
{
	Describe(TEXT("SN2CCodeEditor"), [this]()
	{
		It(TEXT("keeps the language SetLanguage set through a font size and a tab size change"), [this]()
		{
			if (!SlateReady())
			{
				return;
			}
			const TSharedRef<SN2CCodeEditor> Editor = SNew(SN2CCodeEditor).Language(EN2CCodeLanguage::Cpp);
			TestEqual(TEXT("C++: def is a plain word"), KindOfWord(Editor, TEXT("def f():"), TEXT("def")), FString(TEXT("N2CCodeEditor.Normal")));
			Editor->SetLanguage(EN2CCodeLanguage::Python);
			Editor->SetFontSize(14);
			Editor->SetTabSize(2);
			TestEqual(TEXT("the language is still Python"), Editor->GetLanguage(), EN2CCodeLanguage::Python);
			TestEqual(TEXT("Python: def is a keyword after the rebuilds"), KindOfWord(Editor, TEXT("def f():"), TEXT("def")), FString(TEXT("Keyword")));
			TestEqual(TEXT("the font size took"), Editor->GetFontSize(), 14);
		});

		It(TEXT("draws a tab as TabSize spaces"), [this]()
		{
			if (!SlateReady())
			{
				return;
			}
			const TSharedRef<SN2CCodeEditor> Editor = SNew(SN2CCodeEditor).Language(EN2CCodeLanguage::Cpp);
			TestEqual(TEXT("4 by default"), TabSpacesOf(Editor), 4);
			Editor->SetTabSize(8);
			TestEqual(TEXT("8 after SetTabSize(8)"), TabSpacesOf(Editor), 8);
			Editor->SetLanguage(EN2CCodeLanguage::Python);
			TestEqual(TEXT("and still 8 after a language change"), TabSpacesOf(Editor), 8);
			const TSharedRef<SN2CCodeEditor> Built = SNew(SN2CCodeEditor).TabSize(2);
			TestEqual(TEXT("2 from the TabSize argument"), TabSpacesOf(Built), 2);
		});

		It(TEXT("wraps long lines only while word wrap is on, through rebuilds"), [this]()
		{
			if (!SlateReady())
			{
				return;
			}
			const TSharedRef<SN2CCodeEditor> Editor = SNew(SN2CCodeEditor).Text(FText::FromString(TEXT("int32 Value = 1;")));
			TestFalse(TEXT("off by default"), AutoWrapOf(Editor));
			Editor->SetWordWrap(true);
			TestTrue(TEXT("SetWordWrap(true) turns the text widget's auto wrap on"), AutoWrapOf(Editor));
			Editor->SetFontSize(12);
			Editor->SetLanguage(EN2CCodeLanguage::Python);
			TestTrue(TEXT("and it stays on through a font size and a language change"), AutoWrapOf(Editor));
			Editor->SetWordWrap(false);
			TestFalse(TEXT("SetWordWrap(false) turns it off"), AutoWrapOf(Editor));
			const TSharedRef<SN2CCodeEditor> Built = SNew(SN2CCodeEditor).WordWrap(true);
			TestTrue(TEXT("on from the WordWrap argument"), AutoWrapOf(Built));
		});

		It(TEXT("reports text, cursor and selection changes"), [this]()
		{
			if (!SlateReady())
			{
				return;
			}
			FString LastText;
			int32 TextChanges = 0;
			int32 CursorLine = -1;
			int32 CursorColumn = -1;
			int32 SelectionStart = -1;
			int32 SelectionEnd = -1;
			const TSharedRef<SN2CCodeEditor> Editor = SNew(SN2CCodeEditor)
				.Text(FText::FromString(TEXT("one\ntwo\nthree")))
				.OnTextChanged_Lambda([&LastText, &TextChanges](const FText& Text) { LastText = Text.ToString(); ++TextChanges; })
				.OnCursorMoved_Lambda([&CursorLine, &CursorColumn](int32 Line, int32 Column) { CursorLine = Line; CursorColumn = Column; })
				.OnSelectionChanged_Lambda([&SelectionStart, &SelectionEnd](int32 Start, int32 End) { SelectionStart = Start; SelectionEnd = End; });

			Editor->SetCursorPosition(1, 2);
			TestEqual(TEXT("cursor line"), CursorLine, 1);
			TestEqual(TEXT("cursor column"), CursorColumn, 2);

			Editor->SelectText(0, 1, 2, 3);
			TestEqual(TEXT("selection first line"), SelectionStart, 0);
			TestEqual(TEXT("selection last line"), SelectionEnd, 2);

			Editor->SetCursorPosition(0, 0);
			Editor->InsertTextAtCursor(TEXT("zero "));
			TestTrue(TEXT("typing reports a change"), TextChanges > 0);
			TestEqual(TEXT("with the new text"), LastText.Replace(TEXT("\r\n"), TEXT("\n")),FString(TEXT("zero one\ntwo\nthree")));
		});
	});

	Describe(TEXT("UN2CCodeEditorWidget"), [this]()
	{
		It(TEXT("takes typed text into Text, keeps it through SynchronizeProperties, and binds the cursor and selection events"), [this]()
		{
			if (!SlateReady())
			{
				return;
			}
			TStrongObjectPtr<UN2CCodeEditorWidget> Widget(NewObject<UN2CCodeEditorWidget>(GetTransientPackage()));
			Widget->SetText(FText::FromString(TEXT("int32 A = 1;")));
			Widget->TakeWidget();
			const TSharedPtr<SN2CCodeEditor> Slate = (*Widget).*N2CTest::GetPrivateMember(N2CTest::FN2CCodeEditorSpecUmgSlate());
			if (!TestTrue(TEXT("the Slate editor was built"), Slate.IsValid()))
			{
				return;
			}
			TestTrue(TEXT("the cursor event is bound"), ((*Slate).*N2CTest::GetPrivateMember(N2CTest::FN2CCodeEditorSpecCursorDelegate())).IsBound());
			TestTrue(TEXT("the selection event is bound"), ((*Slate).*N2CTest::GetPrivateMember(N2CTest::FN2CCodeEditorSpecSelectionDelegate())).IsBound());

			// The user types at the end of the text
			Slate->SetCursorPosition(0, 12);
			Slate->InsertTextAtCursor(TEXT(" // typed"));
			const FString Typed(TEXT("int32 A = 1; // typed"));
			TestEqual(TEXT("the UMG Text follows the typing"),
				((*Widget).*N2CTest::GetPrivateMember(N2CTest::FN2CCodeEditorSpecUmgText())).ToString(), Typed);

			((*Widget).*N2CTest::GetPrivateMember(N2CTest::FN2CCodeEditorSpecUmgSync()))();
			TestEqual(TEXT("SynchronizeProperties keeps the typed text"), Slate->GetText().ToString(), Typed);
			int32 Line = -1;
			int32 Column = -1;
			Slate->GetCursorPosition(Line, Column);
			TestEqual(TEXT("and the cursor (no rebuild for an unchanged language)"), Column, Typed.Len());

			Widget->ReleaseSlateResources(true);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
