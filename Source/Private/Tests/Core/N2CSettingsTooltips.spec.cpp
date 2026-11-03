// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CSettings.h"
#include "Core/N2CUserSettings.h"
#include "Internationalization/Regex.h"
#include "Tests/N2CTestHelpers.h"
#include "UObject/EnumProperty.h"
#include "UObject/UnrealType.h"

/**
 * What Project Settings and Editor Preferences show for NodeToCode (card 0487, gate 0481): the section descriptions, the
 * tooltip of every editable setting (and of the settings inside its structs), and the tooltip of every option of its
 * drop-downs. A tooltip comes from a ToolTip meta or, without one, from the C++ doc comment, which is how function
 * names, card numbers and identifiers such as N2CMcpEpicOverlap::bLeaveToUnrealMcpByDefault reached users. None of
 * these texts may carry a C++ identifier, a function call, a scope operator or a card number.
 */
BEGIN_DEFINE_SPEC(FN2CSettingsTooltipsSpec, "NodeToCode.Settings.Tooltips", N2C_TEST_FLAGS)

	TArray<FString> Offenders;

	/** Why Text reads like source code, or empty */
	static FString CodeIn(const FString& Text)
	{
		static const TCHAR* const Patterns[] = {
			TEXT("::"),                                  // scope operator
			TEXT("\\b[A-Za-z_][A-Za-z0-9_]*\\(\\)"),     // a call: Name()
			TEXT("\\bcards? [0-9]{3,4}\\b"),             // card 0423, cards 0423, 0182
			TEXT("\\b[UFAES]N2C[A-Z][A-Za-z0-9]*"),      // UN2CSettings, FN2CSecretStore, EN2CLLMProvider
			TEXT("\\bN2C[A-Z][A-Za-z0-9]+\\b(?!\\.)"),  // N2CMcpEpicOverlap (a file name such as N2CModelCatalog.json is fine)
			TEXT("\\bb[A-Z][a-z]+[A-Z][A-Za-z0-9]*\\b"), // bLeaveToUnrealMcpByDefault, bEnableMcpServer
		};
		for (const TCHAR* Pattern : Patterns)
		{
			FRegexMatcher Matcher(FRegexPattern(Pattern), Text);
			if (Matcher.FindNext())
			{
				return Matcher.GetCaptureGroup(0);
			}
		}
		return FString();
	}

	void Check(const FString& Where, const FString& Text)
	{
		const FString Hit = CodeIn(Text);
		if (!Hit.IsEmpty())
		{
			Offenders.Add(FString::Printf(TEXT("%s: \"%s\" in \"%s\""), *Where, *Hit, *Text.Left(160)));
		}
	}

	void CheckEnum(const FString& Where, const UEnum* Enum)
	{
		if (!Enum)
		{
			return;
		}
		for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
		{
			if (Enum->HasMetaData(TEXT("Hidden"), Index))
			{
				continue;
			}
			Check(FString::Printf(TEXT("%s option %s"), *Where, *Enum->GetNameStringByIndex(Index)), Enum->GetToolTipTextByIndex(Index).ToString());
		}
	}

	void CheckProperties(const FString& Where, const UStruct* Struct, int32 Depth = 0)
	{
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			const FProperty* Property = *It;
			if (!Property->HasAnyPropertyFlags(CPF_Edit))
			{
				continue;
			}
			const FString Name = FString::Printf(TEXT("%s.%s"), *Where, *Property->GetName());
			Check(Name, Property->GetToolTipText().ToString());
			if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
			{
				CheckEnum(Name, EnumProperty->GetEnum());
			}
			else if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
			{
				CheckEnum(Name, ByteProperty->Enum);
			}
			const FProperty* Inner = Property;
			if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
			{
				Inner = ArrayProperty->Inner;
			}
			else if (const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
			{
				Inner = MapProperty->ValueProp;
			}
			const FStructProperty* StructProperty = CastField<FStructProperty>(Inner);
			// The engine's own structs (FFilePath, FColor, ...) are not NodeToCode's text
			if (StructProperty && Depth < 3 && StructProperty->Struct->GetName().StartsWith(TEXT("N2C")))
			{
				CheckProperties(Name, StructProperty->Struct, Depth + 1);
			}
		}
	}

END_DEFINE_SPEC(FN2CSettingsTooltipsSpec)

void FN2CSettingsTooltipsSpec::Define()
{
	It(TEXT("shows users no C++ identifiers, function names or card numbers"), [this]()
	{
		Offenders.Reset();
		Check(TEXT("Node to Code section"), GetDefault<UN2CSettings>()->GetSectionDescription().ToString());
		Check(TEXT("Node to Code (User) section"), GetDefault<UN2CUserSettings>()->GetSectionDescription().ToString());
		CheckProperties(TEXT("UN2CSettings"), UN2CSettings::StaticClass());
		CheckProperties(TEXT("UN2CUserSettings"), UN2CUserSettings::StaticClass());
		for (const FString& Offender : Offenders)
		{
			AddError(Offender);
		}
		TestEqual(TEXT("texts that read like source code"), Offenders.Num(), 0);
	});

	It(TEXT("catches the texts gate 0481 saw"), [this]()
	{
		TestFalse(TEXT("a function name"), CodeIn(TEXT("Read through IsMcpServerEnabled(), never directly")).IsEmpty());
		TestFalse(TEXT("a card number"), CodeIn(TEXT("ignoring any value from a project-shared layer (cards 0423, 0182)")).IsEmpty());
		TestFalse(TEXT("a C++ identifier"), CodeIn(TEXT("NodeToCode's default (N2CMcpEpicOverlap::bLeaveToUnrealMcpByDefault)")).IsEmpty());
		TestTrue(TEXT("plain text passes"), CodeIn(TEXT("Port of the MCP server (http://127.0.0.1:<port>/mcp). Requires an editor restart.")).IsEmpty());
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
