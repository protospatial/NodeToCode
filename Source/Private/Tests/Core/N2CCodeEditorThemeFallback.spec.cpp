// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Code Editor/Models/N2CCodeEditorStyle.h"
#include "Core/N2CSettings.h"
#include "Misc/ScopeExit.h"
#include "Tests/N2CTestHelpers.h"

/**
 * The code editor draws one theme (card 0332). UN2CSettings::GetThemeColors returned only the "Unreal Engine" entry,
 * so a config that renamed that key, or emptied the map, gave null and FN2CCodeEditorStyle::InitializeLanguageStyles
 * returned early: no syntax highlighting. Checks: the named entry when present; the first entry when the key was
 * renamed; the default colors when the map is empty; the style set picks up the renamed entry's background; and the
 * Themes property is fixed size in the details panel.
 */
BEGIN_DEFINE_SPEC(FN2CCodeEditorThemeFallbackSpec, "NodeToCode.UI.CodeEditorThemeFallback", N2C_TEST_FLAGS)
END_DEFINE_SPEC(FN2CCodeEditorThemeFallbackSpec)

void FN2CCodeEditorThemeFallbackSpec::Define()
{
	It(TEXT("returns the Unreal Engine entry, else the first entry, else the default colors"), [this]()
	{
		UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
		const TMap<FName, FN2CCodeEditorColors> Saved = Settings->CodeEditorTheme.Themes;
		ON_SCOPE_EXIT
		{
			Settings->CodeEditorTheme.Themes = Saved;
			FN2CCodeEditorStyle::Reinitialize();
		};

		const FN2CCodeEditorColors* Named = Settings->GetThemeColors(EN2CCodeLanguage::Cpp, FName(TEXT("Unreal Engine")));
		TestTrue(TEXT("the Unreal Engine entry"), Named == Settings->CodeEditorTheme.Themes.Find(TEXT("Unreal Engine")));

		// A key renamed in the ini
		FN2CCodeEditorColors Renamed = Saved.FindRef(TEXT("Unreal Engine"));
		Renamed.Background = FColor(11, 22, 33, 255);
		Settings->CodeEditorTheme.Themes.Reset();
		Settings->CodeEditorTheme.Themes.Add(TEXT("My Theme"), Renamed);
		const FN2CCodeEditorColors* First = Settings->GetThemeColors(EN2CCodeLanguage::Python, FName(TEXT("Unreal Engine")));
		if (TestNotNull(TEXT("a renamed key still gives colors"), First))
		{
			TestEqual(TEXT("the renamed entry's colors"), First->Background, Renamed.Background);
		}
		FN2CCodeEditorStyle::Reinitialize();
		const FSlateBrush* Background = FN2CCodeEditorStyle::Get().GetBrush(TEXT("N2CCodeEditor.CPP.Unreal Engine.Background"));
		if (TestNotNull(TEXT("the language background brush is registered"), Background))
		{
			TestEqual(TEXT("the style set draws the renamed entry"), Background->TintColor.GetSpecifiedColor(), FLinearColor(Renamed.Background));
		}

		// An emptied map
		Settings->CodeEditorTheme.Themes.Reset();
		const FN2CCodeEditorColors* Default = Settings->GetThemeColors(EN2CCodeLanguage::Cpp, FName(TEXT("Unreal Engine")));
		if (TestNotNull(TEXT("an empty map still gives colors"), Default))
		{
			TestEqual(TEXT("the default background"), Default->Background, FN2CCodeEditorThemes().Themes.FindRef(TEXT("Unreal Engine")).Background);
		}
	});

	It(TEXT("keeps the Themes map fixed size in Project Settings"), [this]()
	{
		const FProperty* Property = FN2CCodeEditorThemes::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FN2CCodeEditorThemes, Themes));
		TestTrue(TEXT("Themes is EditFixedSize"), Property && Property->HasAnyPropertyFlags(CPF_EditFixedSize));
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
