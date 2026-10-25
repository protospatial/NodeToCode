// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Code Editor/Models/N2CCodeEditorStyle.h"
#include "Code Editor/Models/N2CCodeLanguage.h"
#include "Code Editor/Syntax/N2CRichTextSyntaxHighlighter.h"
#include "Code Editor/Widgets/SN2CCodeEditor.h"
#include "Core/N2CGraphStateManager.h"
#include "Core/N2CSettings.h"
#include "Core/Widgets/SN2CTranslationViewer.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Models/N2CTranslation.h"
#include "Tests/N2CTestHelpers.h"
#include "UObject/StrongObjectPtr.h"
#include "Utils/N2CPathUtils.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

// The viewer's state and widgets, and the code editor's language
N2C_TEST_PRIVATE_MEMBER(FN2CViewerActiveFileType, FString SN2CTranslationViewer::*, &SN2CTranslationViewer::ActiveFileType)
N2C_TEST_PRIVATE_MEMBER(FN2CViewerImplTab, TSharedPtr<SButton> SN2CTranslationViewer::*, &SN2CTranslationViewer::CppTabButton)
N2C_TEST_PRIVATE_MEMBER(FN2CViewerHeaderTab, TSharedPtr<SButton> SN2CTranslationViewer::*, &SN2CTranslationViewer::HeaderTabButton)
N2C_TEST_PRIVATE_MEMBER(FN2CViewerCodeEditor, TSharedPtr<SN2CCodeEditor> SN2CTranslationViewer::*, &SN2CTranslationViewer::CodeEditor)
N2C_TEST_PRIVATE_MEMBER(FN2CViewerTabClicked, FReply (SN2CTranslationViewer::*)(FString), &SN2CTranslationViewer::HandleFileTabClicked)
N2C_TEST_PRIVATE_MEMBER(FN2CViewerEditorLanguage, EN2CCodeLanguage SN2CCodeEditor::*, &SN2CCodeEditor::CurrentLanguage)
N2C_TEST_PRIVATE_MEMBER(FN2CViewerEditorHighlighter, TSharedPtr<FN2CRichTextSyntaxHighlighter> SN2CCodeEditor::*,
	&SN2CCodeEditor::SyntaxHighlighter)
N2C_TEST_PRIVATE_MEMBER(FN2CViewerHighlighterDefinition, TSharedPtr<FN2CSyntaxDefinition> FN2CRichTextSyntaxHighlighter::*,
	&FN2CRichTextSyntaxHighlighter::SyntaxDefinition)

// The graph state singleton, swapped for an isolated manager so the host project's BlueprintGraphState.json is untouched
N2C_TEST_PRIVATE_MEMBER(FN2CViewerStateInstance, UN2CGraphStateManager**, &UN2CGraphStateManager::Instance)

/**
 * The translation viewer shows a translation in the language it was written in (card 0440). Before, it always
 * labelled the implementation tab .cpp, opened on the .h tab and highlighted as C++, so a Python, JavaScript, C#,
 * Swift or Pseudocode translation opened on an empty .h tab with its code under a .cpp label in C++ colours.
 * Checks, on a headless SN2CTranslationViewer: the implementation tab's label is the language's extension, the .h tab
 * is hidden without a declaration, the viewer opens on the implementation and the code editor highlights in the
 * language. Paths: LoadTranslation with an explicit language, the direct LoadTranslation after a single translate
 * (the settings' Target Language), and LoadTranslation(FN2CTagInfo) from a recorded translation.
 */
BEGIN_DEFINE_SPEC(FN2CTranslationViewerSpec, "NodeToCode.UI.TranslationViewer", N2C_TEST_FLAGS)

	static FN2CGraphTranslation MakeTranslation(EN2CCodeLanguage Language)
	{
		FN2CGraphTranslation Translation;
		Translation.GraphName = TEXT("ViewerGraph");
		Translation.GraphType = TEXT("Function");
		Translation.Code.GraphImplementation = Language == EN2CCodeLanguage::Cpp
			? TEXT("void AViewer::Run()\n{\n}\n")
			: FString::Printf(TEXT("code in %s\n"), *FN2CPathUtils::GetLanguageId(Language));
		if (Language == EN2CCodeLanguage::Cpp)
		{
			Translation.Code.GraphDeclaration = TEXT("void Run();");
		}
		Translation.Code.ImplementationNotes = TEXT("notes");
		return Translation;
	}

	/** The text of a tab button (an SButton holding one STextBlock) */
	static FString TabLabel(const TSharedPtr<SButton>& Button)
	{
		if (!Button.IsValid())
		{
			return FString();
		}
		const TSharedRef<SWidget> Content = Button->GetContent();
		return Content->GetType() == FName(TEXT("STextBlock"))
			? StaticCastSharedRef<STextBlock>(Content)->GetText().ToString()
			: FString();
	}

	/** The language the code editor colours its text in (its highlighter's syntax definition); "none" without one */
	static FString HighlightLanguage(const SN2CCodeEditor& Editor)
	{
		const TSharedPtr<FN2CRichTextSyntaxHighlighter>& Highlighter = Editor.*N2CTest::GetPrivateMember(N2CTest::FN2CViewerEditorHighlighter());
		if (!Highlighter.IsValid())
		{
			return TEXT("none");
		}
		const TSharedPtr<FN2CSyntaxDefinition>& Definition = (*Highlighter).*N2CTest::GetPrivateMember(N2CTest::FN2CViewerHighlighterDefinition());
		return Definition.IsValid() ? FN2CPathUtils::GetLanguageId(Definition->GetLanguage()) : TEXT("none");
	}

	/** Checks the viewer shows a translation written in Language: label, .h tab, active tab, highlighting, text */
	void ExpectShows(const FString& What, SN2CTranslationViewer& Viewer, EN2CCodeLanguage Language, const FN2CGraphTranslation& Translation)
	{
		const bool bHasDeclaration = !Translation.Code.GraphDeclaration.IsEmpty();
		// Bound attributes (the .h tab's visibility) are evaluated in the prepass Slate runs every frame
		Viewer.SlatePrepass(1.0f);
		TestEqual(What + TEXT(": implementation tab label"), TabLabel(Viewer.*N2CTest::GetPrivateMember(N2CTest::FN2CViewerImplTab())),
			FN2CPathUtils::GetFileExtensionForLanguage(Language));
		const TSharedPtr<SButton>& HeaderTab = Viewer.*N2CTest::GetPrivateMember(N2CTest::FN2CViewerHeaderTab());
		if (TestTrue(What + TEXT(": .h tab exists"), HeaderTab.IsValid()))
		{
			TestEqual(What + TEXT(": .h tab label"), TabLabel(HeaderTab), FString(TEXT(".h")));
			TestEqual(What + TEXT(": .h tab shows only with a declaration"), HeaderTab->GetVisibility() == EVisibility::Visible, bHasDeclaration);
		}
		TestEqual(What + TEXT(": opens on the implementation"), Viewer.*N2CTest::GetPrivateMember(N2CTest::FN2CViewerActiveFileType()),
			FString(TEXT("cpp")));
		const TSharedPtr<SN2CCodeEditor>& Editor = Viewer.*N2CTest::GetPrivateMember(N2CTest::FN2CViewerCodeEditor());
		if (TestTrue(What + TEXT(": code editor exists"), Editor.IsValid()))
		{
			TestEqual(What + TEXT(": highlighted as ") + FN2CPathUtils::GetLanguageId(Language), HighlightLanguage(*Editor),
				FN2CPathUtils::GetLanguageId(Language));
			// The language a font size, tab size or theme change rebuilds the highlighter in
			TestEqual(What + TEXT(": code editor language"),
				FN2CPathUtils::GetLanguageId((*Editor).*N2CTest::GetPrivateMember(N2CTest::FN2CViewerEditorLanguage())),
				FN2CPathUtils::GetLanguageId(Language));
			TestEqual(What + TEXT(": shows the implementation"), Editor->GetText().ToString(), Translation.Code.GraphImplementation);
		}
	}

	static TArray<EN2CCodeLanguage> AllLanguages()
	{
		return {EN2CCodeLanguage::Cpp, EN2CCodeLanguage::Python, EN2CCodeLanguage::JavaScript,
			EN2CCodeLanguage::CSharp, EN2CCodeLanguage::Swift, EN2CCodeLanguage::Pseudocode};
	}

	/** The settings' Target Language for the life of the scope */
	struct FScopedTargetLanguage
	{
		EN2CCodeLanguage Saved;
		explicit FScopedTargetLanguage(EN2CCodeLanguage Language)
		{
			Saved = GetDefault<UN2CSettings>()->TargetLanguage;
			GetMutableDefault<UN2CSettings>()->TargetLanguage = Language;
		}
		~FScopedTargetLanguage() { GetMutableDefault<UN2CSettings>()->TargetLanguage = Saved; }
	};

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

END_DEFINE_SPEC(FN2CTranslationViewerSpec)

void FN2CTranslationViewerSpec::Define()
{
	It(TEXT("shows a Python translation from a single translate as .py, without .h, highlighted as Python"), [this]()
	{
		if (!SlateReady())
		{
			return;
		}
		// The single translate's path: the direct LoadTranslation, in the settings' Target Language
		FScopedTargetLanguage Target(EN2CCodeLanguage::Python);
		const TSharedRef<SN2CTranslationViewer> Viewer = SNew(SN2CTranslationViewer);
		const FN2CGraphTranslation Translation = MakeTranslation(EN2CCodeLanguage::Python);
		Viewer->LoadTranslation(Translation, TEXT("ViewerGraph"), TEXT("{\"graph\":1}"));
		ExpectShows(TEXT("single translate"), *Viewer, EN2CCodeLanguage::Python, Translation);

		// The styling menu's font size change rebuilds the highlighter: it stays Python
		const TSharedPtr<SN2CCodeEditor>& Editor = Viewer.Get().*N2CTest::GetPrivateMember(N2CTest::FN2CViewerCodeEditor());
		Editor->SetFontSize(13);
		TestEqual(TEXT("still highlighted as Python after a font size change"), HighlightLanguage(*Editor), FString(TEXT("Python")));
	});

	It(TEXT("labels and highlights each language, and C++ keeps its .h tab"), [this]()
	{
		if (!SlateReady())
		{
			return;
		}
		const TSharedRef<SN2CTranslationViewer> Viewer = SNew(SN2CTranslationViewer);
		for (const EN2CCodeLanguage Language : AllLanguages())
		{
			const FN2CGraphTranslation Translation = MakeTranslation(Language);
			// Reused viewer: a previous C++ translation's .h tab and the JSON tab must not stick
			(Viewer.Get().*N2CTest::GetPrivateMember(N2CTest::FN2CViewerTabClicked()))(TEXT("json"));
			Viewer->LoadTranslation(Translation, TEXT("ViewerGraph"), TEXT("{}"), Language);
			ExpectShows(FN2CPathUtils::GetLanguageId(Language), *Viewer, Language, Translation);
		}

		// C++: the .h tab shows the declaration, highlighted as C++, and the implementation tab goes back
		const FN2CGraphTranslation Cpp = MakeTranslation(EN2CCodeLanguage::Cpp);
		Viewer->LoadTranslation(Cpp, TEXT("ViewerGraph"), TEXT("{}"), EN2CCodeLanguage::Cpp);
		(Viewer.Get().*N2CTest::GetPrivateMember(N2CTest::FN2CViewerTabClicked()))(TEXT("h"));
		const TSharedPtr<SN2CCodeEditor>& Editor = Viewer.Get().*N2CTest::GetPrivateMember(N2CTest::FN2CViewerCodeEditor());
		TestEqual(TEXT("C++ .h shows the declaration"), Editor->GetText().ToString(), Cpp.Code.GraphDeclaration);
		TestEqual(TEXT("C++ .h highlighted as C++"), HighlightLanguage(*Editor), FString(TEXT("Cpp")));

		// Clear goes back to an empty viewer with no .h tab
		Viewer->Clear();
		Viewer->SlatePrepass(1.0f);
		TestTrue(TEXT("cleared: .h tab hidden"),
			(Viewer.Get().*N2CTest::GetPrivateMember(N2CTest::FN2CViewerHeaderTab()))->GetVisibility() == EVisibility::Collapsed);
		TestEqual(TEXT("cleared: implementation tab"), Viewer.Get().*N2CTest::GetPrivateMember(N2CTest::FN2CViewerActiveFileType()),
			FString(TEXT("cpp")));
	});

	It(TEXT("shows a recorded Python translation in Python whatever the Target Language"), [this]()
	{
		if (!SlateReady())
		{
			return;
		}
		N2CTest::FScratchDir StateDir(TEXT("N2CViewerState"));
		N2CTest::FScratchDir Output(TEXT("N2CViewerOutput"));
		UN2CGraphStateManager*& Instance = *N2CTest::GetPrivateMember(N2CTest::FN2CViewerStateInstance());
		UN2CGraphStateManager* const SavedInstance = Instance;
		TStrongObjectPtr<UN2CGraphStateManager> Manager(UN2CGraphStateManager::CreateIsolated(StateDir.Path));
		Instance = Manager.Get();
		FScopedTargetLanguage Target(EN2CCodeLanguage::Cpp); // the recorded language wins over the setting

		const FN2CGraphTranslation Translation = MakeTranslation(EN2CCodeLanguage::Python);
		FN2CTagInfo Tag;
		Tag.Tag = TEXT("N2CViewerTest");
		Tag.GraphGuid = FGuid::NewGuid().ToString();
		Tag.GraphName = TEXT("ViewerGraph");
		Tag.BlueprintPath = TEXT("/Game/N2CTest/BP_N2CViewer.BP_N2CViewer");
		const FString GraphDir = Output / Tag.GraphName;
		FFileHelper::SaveStringToFile(Translation.Code.GraphImplementation, *(GraphDir / (Tag.GraphName + TEXT(".py"))));
		FGuid Guid;
		FGuid::Parse(Tag.GraphGuid, Guid);
		Manager->RecordSavedTranslation(Guid, Tag.GraphName, FSoftObjectPath(Tag.BlueprintPath), GraphDir,
			TEXT("Test"), TEXT("Test"), EN2CCodeLanguage::Python, FN2CTranslationSummary());

		FN2CGraphTranslation Loaded;
		EN2CCodeLanguage LoadedLanguage = EN2CCodeLanguage::Cpp;
		TestTrue(TEXT("the state manager loads it with its language"), Manager->LoadTranslation(Guid, Loaded, LoadedLanguage)
			&& LoadedLanguage == EN2CCodeLanguage::Python);

		const TSharedRef<SN2CTranslationViewer> Viewer = SNew(SN2CTranslationViewer);
		if (TestTrue(TEXT("the viewer loads the recorded translation"), Viewer->LoadTranslation(Tag)))
		{
			ExpectShows(TEXT("recorded"), *Viewer, EN2CCodeLanguage::Python, Translation);
		}
		Instance = SavedInstance;
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
