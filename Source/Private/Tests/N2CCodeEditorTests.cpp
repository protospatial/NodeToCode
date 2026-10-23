// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Code Editor/Models/N2CCodeEditorStyle.h"
#include "Code Editor/Widgets/SN2CCodeEditor.h"
#include "Core/N2CSettings.h"
#include "Styling/SlateStyleRegistry.h"
#include "Framework/Application/SlateApplication.h"

/**
 * A theme change (FN2CCodeEditorStyle::Reinitialize, run by UN2CSettings::PostEditChangeProperty) must not free the
 * brushes a live SN2CCodeEditor paints with, and SelectText must clamp out-of-range lines and columns instead of
 * letting FSlateEditableTextLayout index past its line array.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FN2CCodeEditorRobustnessTest, "NodeToCode.Robustness.CodeEditor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FN2CCodeEditorRobustnessTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		AddInfo(TEXT("Slate is not initialized; skipping"));
		return true;
	}

	FN2CCodeEditorStyle::Initialize(); // no-op when the module already did it

	// 100-line document, no trailing newline, so the layout has exactly 100 lines
	TArray<FString> Lines;
	for (int32 Index = 0; Index < 100; ++Index)
	{
		Lines.Add(FString::Printf(TEXT("int32 Value%d = %d; // line"), Index, Index));
	}
	const FString Document = FString::Join(Lines, TEXT("\n"));

	TSharedRef<SN2CCodeEditor> Editor = SNew(SN2CCodeEditor)
		.Text(FText::FromString(Document))
		.Language(EN2CCodeLanguage::Cpp);
	TestEqual(TEXT("editor has 100 lines"), Editor->GetLineCount(), 100);

	// --- Theme change keeps the brush alive -----------------------------------------------------------------
	const FName BrushName(TEXT("N2CCodeEditor.CPP.Unreal Engine.Background"));
	const FSlateBrush* const BorderBrush = Editor->GetBackgroundBrush();
	TSharedPtr<const FSlateStyleSet> SetBefore = FN2CCodeEditorStyle::GetStyleSetPtr();
	if (!TestNotNull(TEXT("border brush recorded"), BorderBrush) || !TestTrue(TEXT("style set exists"), SetBefore.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("border brush comes from the style set"), SetBefore->GetBrush(BrushName) == BorderBrush);

	// Change the theme the way a Project Settings edit does, then restore it
	UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
	FN2CCodeEditorColors* Colors = Settings ? Settings->CodeEditorTheme.Themes.Find(TEXT("Unreal Engine")) : nullptr;
	const FColor TestBackground(1, 2, 3, 255);
	FColor SavedBackground = FColor::Black;
	if (Colors)
	{
		SavedBackground = Colors->Background;
		Colors->Background = TestBackground;
	}

	FN2CCodeEditorStyle::Reinitialize();

	// SetBefore holds a reference, so a destroy-and-recreate Reinitialize would show up as a different set here
	// rather than as a reused allocation
	TestTrue(TEXT("Reinitialize keeps the same style set"), FN2CCodeEditorStyle::GetStyleSetPtr() == SetBefore);
	TestTrue(TEXT("the kept set is still the registered one"),
		FSlateStyleRegistry::FindSlateStyle(SetBefore->GetStyleSetName()) == SetBefore.Get());
	TestTrue(TEXT("border brush is still owned by the registered style set"),
		FN2CCodeEditorStyle::Get().GetBrush(BrushName) == BorderBrush);
	TestTrue(TEXT("widget still points at the same brush"), Editor->GetBackgroundBrush() == BorderBrush);
	if (Colors)
	{
		TestEqual(TEXT("brush retinted in place to the new theme color"),
			BorderBrush->TintColor.GetSpecifiedColor(), FLinearColor(TestBackground));
		Colors->Background = SavedBackground;
		FN2CCodeEditorStyle::Reinitialize();
		TestEqual(TEXT("brush restored to the saved theme color"),
			BorderBrush->TintColor.GetSpecifiedColor(), FLinearColor(SavedBackground));
	}
	else
	{
		AddWarning(TEXT("'Unreal Engine' theme missing from settings; color retint not checked"));
	}

	// --- SelectText clamps -------------------------------------------------------------------------------------
	int32 StartLine = -1, StartColumn = -1, EndLine = -1, EndColumn = -1;

	Editor->SelectText(0, 0, 500, 5);
	Editor->GetSelection(StartLine, StartColumn, EndLine, EndColumn);
	TestEqual(TEXT("selection starts on line 0"), StartLine, 0);
	TestEqual(TEXT("selection ends on the last line"), EndLine, 99);
	TestEqual(TEXT("end column kept when within the line"), EndColumn, 5);

	Editor->SelectText(-7, -3, 2, 100000);
	Editor->GetSelection(StartLine, StartColumn, EndLine, EndColumn);
	TestEqual(TEXT("negative start line clamps to 0"), StartLine, 0);
	TestEqual(TEXT("negative start column clamps to 0"), StartColumn, 0);
	TestEqual(TEXT("end line in range is kept"), EndLine, 2);
	TestEqual(TEXT("end column clamps to the line length"), EndColumn, Lines[2].Len());

	int32 FirstLine = -1, LastLine = -1;
	Editor->GetSelection(FirstLine, LastLine);
	TestEqual(TEXT("two-output GetSelection returns the first line"), FirstLine, 0);
	TestEqual(TEXT("two-output GetSelection returns the last line"), LastLine, 2);

	// Empty document: nothing to select, must not crash
	TSharedRef<SN2CCodeEditor> EmptyEditor = SNew(SN2CCodeEditor).Text(FText::GetEmpty());
	EmptyEditor->SelectText(3, 3, 9, 9);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
