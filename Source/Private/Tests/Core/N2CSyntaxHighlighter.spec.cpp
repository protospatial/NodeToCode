// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Code Editor/Models/N2CCodeEditorStyle.h"
#include "Code Editor/Models/N2CCodeLanguage.h"
#include "Code Editor/Syntax/N2CRichTextSyntaxHighlighter.h"
#include "Code Editor/Syntax/N2CSyntaxDefinition.h"
#include "Code Editor/Widgets/SN2CCodeEditor.h"
#include "Core/Widgets/SN2CTranslationViewer.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Text/SlateTextLayout.h"
#include "Models/N2CTranslation.h"
#include "Tests/N2CTestHelpers.h"
#include "Widgets/Layout/SBox.h"

// The viewer's code editor and tab handler, and the editor's highlighter and its definition
N2C_TEST_PRIVATE_MEMBER(FN2CHighlightViewerCodeEditor, TSharedPtr<SN2CCodeEditor> SN2CTranslationViewer::*, &SN2CTranslationViewer::CodeEditor)
N2C_TEST_PRIVATE_MEMBER(FN2CHighlightViewerTabClicked, FReply (SN2CTranslationViewer::*)(FString), &SN2CTranslationViewer::HandleFileTabClicked)
N2C_TEST_PRIVATE_MEMBER(FN2CHighlightEditorHighlighter, TSharedPtr<FN2CRichTextSyntaxHighlighter> SN2CCodeEditor::*,
	&SN2CCodeEditor::SyntaxHighlighter)
N2C_TEST_PRIVATE_MEMBER(FN2CHighlightDefinition, TSharedPtr<FN2CSyntaxDefinition> FN2CRichTextSyntaxHighlighter::*,
	&FN2CRichTextSyntaxHighlighter::SyntaxDefinition)

/**
 * The code editor's syntax highlighter (card 0263). Before, a string ended on any quote, ignored backslash escapes and
 * never ended at the end of a line, so an escaped quote or a char literal holding a double quote coloured the rest of
 * the file as a string; C++ "->" and "==" were split by shorter operator rules listed first; keywords matched in any
 * case; Python docstrings were block comments; and the viewer's JSON tab used the colourless Pseudocode definition.
 * Checks the run each word lands in after FSyntaxHighlighterTextLayoutMarshaller::SetText on a real text layout.
 */
BEGIN_DEFINE_SPEC(FN2CSyntaxHighlighterSpec, "NodeToCode.UI.SyntaxHighlighter", N2C_TEST_FLAGS)

	struct FRunText
	{
		FString Name;
		FString Text;
	};
	using FLines = TArray<TArray<FRunText>>;

	/** The runs of each line: run name (String, Keyword, ..., WhiteSpace, N2CCodeEditor.Normal) and text */
	static FLines Highlight(const TSharedRef<FN2CRichTextSyntaxHighlighter>& Highlighter, const FString& Source)
	{
		// The layout's run children need an owner widget (asserted on 5.8)
		const TSharedRef<SBox> Owner = SNew(SBox);
		const TSharedRef<FSlateTextLayout> Layout = FSlateTextLayout::Create(&Owner.Get(), FTextBlockStyle());
		Highlighter->SetText(Source, *Layout);
		FLines Lines;
		for (const FTextLayout::FLineModel& Line : Layout->GetLineModels())
		{
			TArray<FRunText>& Runs = Lines.AddDefaulted_GetRef();
			for (const FTextLayout::FRunModel& Run : Line.Runs)
			{
				const FTextRange Range = Run.GetTextRange();
				Runs.Add({Run.GetRun()->GetRunInfo().Name, Line.Text->Mid(Range.BeginIndex, Range.Len())});
			}
		}
		return Lines;
	}

	static FLines Highlight(EN2CCodeLanguage Language, const FString& Source)
	{
		return Highlight(FN2CRichTextSyntaxHighlighter::Create(Language, TEXT("Unreal Engine")), Source);
	}

	/** The run name of the first run on a line whose text contains Word (case-sensitive) */
	static FString KindOf(const FLines& Lines, int32 LineIndex, const FString& Word)
	{
		if (!Lines.IsValidIndex(LineIndex))
		{
			return TEXT("no such line");
		}
		for (const FRunText& Run : Lines[LineIndex])
		{
			if (Run.Text.Contains(Word, ESearchCase::CaseSensitive))
			{
				return Run.Name;
			}
		}
		return TEXT("no such run");
	}

	/** Each run on a line as name:text, for failure messages */
	static FString DescribeLine(const FLines& Lines, int32 LineIndex)
	{
		TArray<FString> Parts;
		if (Lines.IsValidIndex(LineIndex))
		{
			for (const FRunText& Run : Lines[LineIndex])
			{
				Parts.Add(Run.Name + TEXT(":") + Run.Text);
			}
		}
		return FString::Join(Parts, TEXT(" | "));
	}

	void ExpectKind(const FString& What, const FLines& Lines, int32 LineIndex, const FString& Word, const TCHAR* Expected)
	{
		TestEqual(FString::Printf(TEXT("%s: '%s' on line %d [%s]"), *What, *Word, LineIndex, *DescribeLine(Lines, LineIndex)),
			KindOf(Lines, LineIndex, Word), FString(Expected));
	}

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

END_DEFINE_SPEC(FN2CSyntaxHighlighterSpec)

void FN2CSyntaxHighlighterSpec::Define()
{
	Describe(TEXT("strings"), [this]()
	{
		It(TEXT("end at the closing quote after an escaped quote, not on the lines below"), [this]()
		{
			if (!SlateReady())
			{
				return;
			}
			const FLines Lines = Highlight(EN2CCodeLanguage::Cpp,
				TEXT("UE_LOG(LogTemp, Log, TEXT(\"Quote: \\\"done\"));\nint32 Next = 1;"));
			ExpectKind(TEXT("escaped quote"), Lines, 0, TEXT("done"), TEXT("String"));
			ExpectKind(TEXT("escaped quote"), Lines, 0, TEXT(";"), TEXT("Operator"));
			ExpectKind(TEXT("escaped quote"), Lines, 1, TEXT("int32"), TEXT("Keyword"));
			ExpectKind(TEXT("escaped quote"), Lines, 1, TEXT("1"), TEXT("Number"));
		});

		It(TEXT("end after an even run of backslashes"), [this]()
		{
			if (!SlateReady())
			{
				return;
			}
			const FLines Lines = Highlight(EN2CCodeLanguage::Cpp, TEXT("FString P = \"C:\\\\\" + Name;\nint32 Next = 1;"));
			ExpectKind(TEXT("escaped backslash"), Lines, 0, TEXT("Name"), TEXT("N2CCodeEditor.Normal"));
			ExpectKind(TEXT("escaped backslash"), Lines, 0, TEXT("+"), TEXT("Operator"));
			ExpectKind(TEXT("escaped backslash"), Lines, 1, TEXT("int32"), TEXT("Keyword"));
		});

		It(TEXT("close a char literal holding a double quote at its own delimiter"), [this]()
		{
			if (!SlateReady())
			{
				return;
			}
			const FLines Lines = Highlight(EN2CCodeLanguage::Cpp, TEXT("const TCHAR Quote = '\"';\nint32 After = 2;"));
			ExpectKind(TEXT("char literal"), Lines, 0, TEXT(";"), TEXT("Operator"));
			ExpectKind(TEXT("char literal"), Lines, 1, TEXT("int32"), TEXT("Keyword"));
			ExpectKind(TEXT("char literal"), Lines, 1, TEXT("After"), TEXT("N2CCodeEditor.Normal"));
		});

		It(TEXT("close mixed quotes at the delimiter that opened them"), [this]()
		{
			if (!SlateReady())
			{
				return;
			}
			const FLines Lines = Highlight(EN2CCodeLanguage::Cpp, TEXT("FString S = \"'\"; int32 X = 7;\nreturn X;"));
			ExpectKind(TEXT("mixed quotes"), Lines, 0, TEXT("int32"), TEXT("Keyword"));
			ExpectKind(TEXT("mixed quotes"), Lines, 0, TEXT("7"), TEXT("Number"));
			ExpectKind(TEXT("mixed quotes"), Lines, 1, TEXT("return"), TEXT("Keyword"));

			const FLines Python = Highlight(EN2CCodeLanguage::Python, TEXT("s = '\"' + \"'\"\nreturn s"));
			ExpectKind(TEXT("Python mixed quotes"), Python, 0, TEXT("+"), TEXT("Operator"));
			ExpectKind(TEXT("Python mixed quotes"), Python, 1, TEXT("return"), TEXT("Keyword"));
		});

		It(TEXT("end at the end of the line when left open"), [this]()
		{
			if (!SlateReady())
			{
				return;
			}
			const FLines Lines = Highlight(EN2CCodeLanguage::Cpp, TEXT("FString Bad = \"oops;\nint32 W = 6;"));
			ExpectKind(TEXT("unclosed"), Lines, 0, TEXT("oops"), TEXT("String"));
			ExpectKind(TEXT("unclosed"), Lines, 1, TEXT("int32"), TEXT("Keyword"));
		});

		It(TEXT("span lines inside a Python triple-quoted string, quotes included"), [this]()
		{
			if (!SlateReady())
			{
				return;
			}
			const FLines Lines = Highlight(EN2CCodeLanguage::Python,
				TEXT("def f():\n    \"\"\"Doc with \"quotes\" and 'apostrophes'\n    second line return\"\"\"\n    return 1"));
			ExpectKind(TEXT("triple quotes"), Lines, 0, TEXT("def"), TEXT("Keyword"));
			ExpectKind(TEXT("triple quotes"), Lines, 1, TEXT("Doc"), TEXT("String"));
			ExpectKind(TEXT("triple quotes"), Lines, 1, TEXT("apostrophes"), TEXT("String"));
			ExpectKind(TEXT("triple quotes"), Lines, 2, TEXT("second"), TEXT("String"));
			ExpectKind(TEXT("triple quotes"), Lines, 2, TEXT("return"), TEXT("String"));
			ExpectKind(TEXT("triple quotes"), Lines, 3, TEXT("return"), TEXT("Keyword"));
			ExpectKind(TEXT("triple quotes"), Lines, 3, TEXT("1"), TEXT("Number"));
		});

		It(TEXT("span lines inside a C++ raw string, which has no escapes"), [this]()
		{
			if (!SlateReady())
			{
				return;
			}
			const FLines Lines = Highlight(EN2CCodeLanguage::Cpp,
				TEXT("FString J = R\"json({\"a\": \"b\\\"})json\";\nint32 Y = 4;\nFString M = R\"(first\nsecond \"unbalanced\n)\";\nint32 Z = 5;"));
			ExpectKind(TEXT("raw string"), Lines, 0, TEXT("R"), TEXT("String"));
			ExpectKind(TEXT("raw string"), Lines, 0, TEXT("a"), TEXT("String"));
			ExpectKind(TEXT("raw string"), Lines, 0, TEXT(";"), TEXT("Operator"));
			ExpectKind(TEXT("raw string"), Lines, 1, TEXT("int32"), TEXT("Keyword"));
			ExpectKind(TEXT("raw string"), Lines, 2, TEXT("first"), TEXT("String"));
			ExpectKind(TEXT("raw string"), Lines, 3, TEXT("second"), TEXT("String"));
			ExpectKind(TEXT("raw string"), Lines, 3, TEXT("unbalanced"), TEXT("String"));
			ExpectKind(TEXT("raw string"), Lines, 4, TEXT(";"), TEXT("Operator"));
			ExpectKind(TEXT("raw string"), Lines, 5, TEXT("int32"), TEXT("Keyword"));
		});

		It(TEXT("span lines inside a JavaScript template literal"), [this]()
		{
			if (!SlateReady())
			{
				return;
			}
			const FLines Lines = Highlight(EN2CCodeLanguage::JavaScript, TEXT("const s = `a\nb ${x}`;\nlet y = 1;"));
			ExpectKind(TEXT("template literal"), Lines, 1, TEXT("b"), TEXT("String"));
			ExpectKind(TEXT("template literal"), Lines, 1, TEXT(";"), TEXT("Operator"));
			ExpectKind(TEXT("template literal"), Lines, 2, TEXT("let"), TEXT("Keyword"));
		});

		It(TEXT("read a C# verbatim string's doubled quote as a quote"), [this]()
		{
			if (!SlateReady())
			{
				return;
			}
			const FLines Lines = Highlight(EN2CCodeLanguage::CSharp, TEXT("var p = @\"C:\\dir\\\"\"quoted\"\"\";\nint n = 1;"));
			ExpectKind(TEXT("verbatim"), Lines, 0, TEXT("quoted"), TEXT("String"));
			// C# lists no ';' operator: the ';' after the string is plain text, not string
			ExpectKind(TEXT("verbatim"), Lines, 0, TEXT(";"), TEXT("N2CCodeEditor.Normal"));
			ExpectKind(TEXT("verbatim"), Lines, 1, TEXT("int"), TEXT("Keyword"));
		});
	});

	Describe(TEXT("words"), [this]()
	{
		It(TEXT("take the longest operator and keep comments out of strings"), [this]()
		{
			if (!SlateReady())
			{
				return;
			}
			const FLines Lines = Highlight(EN2CCodeLanguage::Cpp,
				TEXT("if (Ptr->Value == 1) {} // done\nFString U = \"http://x\"; /* a\nb */ int32 C = 2;"));
			ExpectKind(TEXT("operators"), Lines, 0, TEXT("->"), TEXT("Operator"));
			ExpectKind(TEXT("operators"), Lines, 0, TEXT("=="), TEXT("Operator"));
			TestTrue(TEXT("-> is one run"), Lines[0].ContainsByPredicate([](const FRunText& Run) { return Run.Text == TEXT("->"); }));
			TestTrue(TEXT("== is one run"), Lines[0].ContainsByPredicate([](const FRunText& Run) { return Run.Text == TEXT("=="); }));
			ExpectKind(TEXT("comments"), Lines, 0, TEXT("done"), TEXT("Comment"));
			ExpectKind(TEXT("comments"), Lines, 1, TEXT("http"), TEXT("String"));
			ExpectKind(TEXT("comments"), Lines, 1, TEXT("a"), TEXT("Comment"));
			ExpectKind(TEXT("comments"), Lines, 2, TEXT("b"), TEXT("Comment"));
			ExpectKind(TEXT("comments"), Lines, 2, TEXT("int32"), TEXT("Keyword"));
		});

		It(TEXT("match keywords case-sensitively and colour C++ preprocessor lines"), [this]()
		{
			if (!SlateReady())
			{
				return;
			}
			const FLines Lines = Highlight(EN2CCodeLanguage::Cpp, TEXT("#include \"Foo.h\"\nIf Class return;"));
			ExpectKind(TEXT("preprocessor"), Lines, 0, TEXT("include"), TEXT("Preprocessor"));
			ExpectKind(TEXT("preprocessor"), Lines, 0, TEXT("Foo"), TEXT("String"));
			ExpectKind(TEXT("case"), Lines, 1, TEXT("If"), TEXT("N2CCodeEditor.Normal"));
			ExpectKind(TEXT("case"), Lines, 1, TEXT("Class"), TEXT("N2CCodeEditor.Normal"));
			ExpectKind(TEXT("case"), Lines, 1, TEXT("return"), TEXT("Keyword"));
		});

		It(TEXT("render a line over 2,000 characters plain, and highlight the next line"), [this]()
		{
			if (!SlateReady())
			{
				return;
			}
			const FString Long = TEXT("int32 A = \"") + FString::ChrN(FN2CRichTextSyntaxHighlighter::MaxHighlightedLineLength, TEXT('x'));
			const FLines Lines = Highlight(EN2CCodeLanguage::Cpp, Long + TEXT("\nint32 B = 1;"));
			if (TestEqual(TEXT("two lines"), Lines.Num(), 2))
			{
				TestEqual(TEXT("the long line is one run"), Lines[0].Num(), 1);
				TestEqual(TEXT("the long line is plain"), KindOf(Lines, 0, TEXT("int32")), FString(TEXT("N2CCodeEditor.Normal")));
				ExpectKind(TEXT("after the long line"), Lines, 1, TEXT("int32"), TEXT("Keyword"));
			}
		});
	});

	Describe(TEXT("JSON"), [this]()
	{
		It(TEXT("colours strings, numbers, literals and braces"), [this]()
		{
			if (!SlateReady())
			{
				return;
			}
			const FLines Lines = Highlight(FN2CRichTextSyntaxHighlighter::CreateJson(TEXT("Unreal Engine")),
				TEXT("{\n  \"key\": \"va\\\"lue\",\n  \"n\": -1.5,\n  \"t\": [true, null]\n}"));
			ExpectKind(TEXT("JSON"), Lines, 0, TEXT("{"), TEXT("CurlyBraces"));
			ExpectKind(TEXT("JSON"), Lines, 1, TEXT("key"), TEXT("String"));
			ExpectKind(TEXT("JSON"), Lines, 1, TEXT("lue"), TEXT("String"));
			ExpectKind(TEXT("JSON"), Lines, 1, TEXT(","), TEXT("Operator"));
			ExpectKind(TEXT("JSON"), Lines, 2, TEXT("1.5"), TEXT("Number"));
			ExpectKind(TEXT("JSON"), Lines, 3, TEXT("["), TEXT("SquareBrackets"));
			ExpectKind(TEXT("JSON"), Lines, 3, TEXT("true"), TEXT("Keyword"));
			ExpectKind(TEXT("JSON"), Lines, 3, TEXT("null"), TEXT("Keyword"));
		});

		It(TEXT("is what the translation viewer's JSON tab highlights with, and the other tabs leave it"), [this]()
		{
			if (!SlateReady())
			{
				return;
			}
			auto SyntaxName = [](const SN2CCodeEditor& Editor) -> FString
			{
				const TSharedPtr<FN2CRichTextSyntaxHighlighter>& Highlighter = Editor.*N2CTest::GetPrivateMember(N2CTest::FN2CHighlightEditorHighlighter());
				if (!Highlighter.IsValid())
				{
					return TEXT("none");
				}
				const TSharedPtr<FN2CSyntaxDefinition>& Definition = (*Highlighter).*N2CTest::GetPrivateMember(N2CTest::FN2CHighlightDefinition());
				return Definition.IsValid() ? Definition->GetSyntaxName() : TEXT("none");
			};

			FN2CGraphTranslation Translation;
			Translation.GraphName = TEXT("ViewerGraph");
			Translation.Code.GraphImplementation = TEXT("void Run() {}\n");
			Translation.Code.GraphDeclaration = TEXT("void Run();");
			const TSharedRef<SN2CTranslationViewer> Viewer = SNew(SN2CTranslationViewer);
			Viewer->LoadTranslation(Translation, TEXT("ViewerGraph"), TEXT("{\"graph\": 1}"), EN2CCodeLanguage::Cpp);
			const TSharedPtr<SN2CCodeEditor>& Editor = Viewer.Get().*N2CTest::GetPrivateMember(N2CTest::FN2CHighlightViewerCodeEditor());
			if (!TestTrue(TEXT("code editor exists"), Editor.IsValid()))
			{
				return;
			}
			TestEqual(TEXT("implementation tab"), SyntaxName(*Editor), FString(TEXT("Cpp")));

			(Viewer.Get().*N2CTest::GetPrivateMember(N2CTest::FN2CHighlightViewerTabClicked()))(TEXT("json"));
			TestEqual(TEXT("JSON tab highlights as JSON"), SyntaxName(*Editor), FString(TEXT("JSON")));
			// The viewer pretty-prints the JSON
			TestTrue(TEXT("JSON tab shows the JSON"), Editor->GetText().ToString().Contains(TEXT("\"graph\"")));
			Editor->SetFontSize(12);
			TestEqual(TEXT("still JSON after a font size change"), SyntaxName(*Editor), FString(TEXT("JSON")));

			(Viewer.Get().*N2CTest::GetPrivateMember(N2CTest::FN2CHighlightViewerTabClicked()))(TEXT("h"));
			TestEqual(TEXT(".h tab goes back to C++"), SyntaxName(*Editor), FString(TEXT("Cpp")));
			TestFalse(TEXT(".h tab leaves JSON"), Editor->IsJsonSyntax());
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
