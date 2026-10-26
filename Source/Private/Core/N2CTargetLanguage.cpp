// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Core/N2CTargetLanguage.h"

namespace
{
	/** The table, in enum order. A new EN2CCodeLanguage value needs a row here (NodeToCode.Languages checks it). */
	const FN2CTargetLanguage GTargetLanguages[] = {
		{
			EN2CCodeLanguage::Cpp, TEXT("Cpp"), TEXT("C++"), TEXT(".cpp"), TEXT(".h"), TEXT(""), TEXT("CPP"), true,
			TEXT("C++ with Unreal Engine conventions and best practices"), TEXT("compiled"),
			TEXT("Header/source separation, UPROPERTY/UFUNCTION macros, full UE5 API compatibility"),
		},
		{
			EN2CCodeLanguage::Python, TEXT("Python"), TEXT("Python"), TEXT(".py"), TEXT(""), TEXT(""), TEXT("Python"), false,
			TEXT("Python 3 with type hints and PEP 8 compliance"), TEXT("scripted"),
			TEXT("Type annotations, async/await support, clean pythonic idioms"),
		},
		{
			EN2CCodeLanguage::JavaScript, TEXT("JavaScript"), TEXT("JavaScript"), TEXT(".js"), TEXT(""), TEXT(""), TEXT("JavaScript"), false,
			TEXT("Modern JavaScript (ECMAScript 2022+) with clean syntax"), TEXT("scripted"),
			TEXT("ES6+ features, arrow functions, destructuring, async/await"),
		},
		{
			EN2CCodeLanguage::CSharp, TEXT("CSharp"), TEXT("C#"), TEXT(".cs"), TEXT(""), TEXT(""), TEXT("CSharp"), false,
			TEXT("C# with Unity-compatible conventions"), TEXT("compiled"),
			TEXT("Properties, LINQ-style operations, Unity MonoBehaviour patterns"),
		},
		{
			EN2CCodeLanguage::Swift, TEXT("Swift"), TEXT("Swift"), TEXT(".swift"), TEXT(""), TEXT(""), TEXT("Swift"), false,
			TEXT("Swift 5+ for iOS/macOS development"), TEXT("compiled"),
			TEXT("Optionals, protocols, SwiftUI compatibility, modern Swift patterns"),
		},
		{
			// Older 2.0 builds' loader looked for Pseudocode as .txt, so .txt is read after .md
			EN2CCodeLanguage::Pseudocode, TEXT("Pseudocode"), TEXT("Pseudocode"), TEXT(".md"), TEXT(""), TEXT(".txt"), TEXT("Pseudocode"), false,
			TEXT("Human-readable algorithmic representation for documentation"), TEXT("pseudocode"),
			TEXT("Plain English descriptions, structured flow, ideal for documentation"),
		},
	};
}

TArray<FString> FN2CTargetLanguage::GetImplementationExtensions() const
{
	TArray<FString> Extensions;
	Extensions.Add(ImplementationExtension);
	if (*LegacyImplementationExtension)
	{
		Extensions.AddUnique(LegacyImplementationExtension);
	}
	return Extensions;
}

TArray<FString> FN2CTargetLanguage::GetFileExtensions() const
{
	TArray<FString> Extensions;
	if (bHasDeclarationFile && *DeclarationExtension)
	{
		Extensions.Add(DeclarationExtension);
	}
	for (const FString& Extension : GetImplementationExtensions())
	{
		Extensions.AddUnique(Extension);
	}
	return Extensions;
}

TConstArrayView<FN2CTargetLanguage> N2C::GetLanguages()
{
	return MakeArrayView(GTargetLanguages);
}

const FN2CTargetLanguage* N2C::FindLanguageRow(EN2CCodeLanguage Language)
{
	for (const FN2CTargetLanguage& Row : GTargetLanguages)
	{
		if (Row.Enum == Language)
		{
			return &Row;
		}
	}
	return nullptr;
}

const FN2CTargetLanguage& N2C::GetLanguage(EN2CCodeLanguage Language)
{
	const FN2CTargetLanguage* Row = FindLanguageRow(Language);
	return Row ? *Row : GTargetLanguages[0];
}

const FN2CTargetLanguage* N2C::FindLanguage(const FString& Name)
{
	FString Wanted = Name.TrimStartAndEnd();
	if (Wanted.StartsWith(TEXT("EN2CCodeLanguage::"), ESearchCase::IgnoreCase))
	{
		Wanted.RightChopInline(18); // EN2CCodeLanguage::Python -> Python
	}
	if (Wanted.IsEmpty())
	{
		return nullptr;
	}
	for (const FN2CTargetLanguage& Row : GTargetLanguages)
	{
		if (Wanted.Equals(Row.Id, ESearchCase::IgnoreCase) || Wanted.Equals(Row.DisplayName, ESearchCase::IgnoreCase)
			|| Wanted.Equals(Row.PromptKey, ESearchCase::IgnoreCase))
		{
			return &Row;
		}
	}
	return nullptr;
}

FString N2C::GetPromptKey(EN2CCodeLanguage Language, const TCHAR* Family)
{
	return FString::Printf(TEXT("%s_%s"), Family, GetLanguage(Language).PromptKey);
}
