// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Code Editor/Models/N2CCodeLanguage.h"

/**
 * One translation target language: the single place that knows its id, display name, file extensions and prompt key.
 * The LLM module, the batch orchestrator, the translation record (LoadTranslation), the prompt manager and the MCP
 * targets tool all read this table, so adding a language is one row here, a CodeGen_<PromptKey>.md prompt, and the
 * code editor's own syntax definition and style (which stay in the code editor: this table must not depend on Slate).
 */
struct NODETOCODE_API FN2CTargetLanguage
{
	/** The enum value this row describes */
	EN2CCodeLanguage Enum;

	/** The id translation records store: the enum's short name (Cpp, Python, ..., Pseudocode) */
	const TCHAR* Id;

	/** What people read: C++, C#, Python */
	const TCHAR* DisplayName;

	/** The extension, with its dot, of the implementation file a translation saves (.cpp, .py, .md for Pseudocode) */
	const TCHAR* ImplementationExtension;

	/** The extension of the declaration (header) file, or empty when the language has none */
	const TCHAR* DeclarationExtension;

	/** An extension older builds saved the implementation with, read after ImplementationExtension; empty for none */
	const TCHAR* LegacyImplementationExtension;

	/** The language part of a prompt key: <Family>_<PromptKey>, the file Content/Prompting/<Family>_<PromptKey>.md */
	const TCHAR* PromptKey;

	/** Whether a translation writes a declaration file (GraphDeclaration) next to the implementation */
	bool bHasDeclarationFile;

	/** Shown by get-available-translation-targets */
	const TCHAR* Description;
	const TCHAR* Category;
	const TCHAR* Features;

	/** The id MCP tools and prompts take and report: Id in lower case (cpp, csharp, pseudocode) */
	FString GetMcpId() const { return FString(Id).ToLower(); }

	/** Implementation extensions a saved translation may have, ImplementationExtension first */
	TArray<FString> GetImplementationExtensions() const;

	/** Every extension a translation of this language writes: the declaration's (if any), then the implementation's */
	TArray<FString> GetFileExtensions() const;
};

namespace N2C
{
	/**
	 * The prompt family of a translation source kind. Only Blueprint graphs exist today ("CodeGen"); another source
	 * kind (a material graph, for example) would add its own family and the languages it serves, without touching
	 * the per-language prompt keys.
	 */
	inline constexpr const TCHAR* BlueprintPromptFamily = TEXT("CodeGen");

	/** Every target language, in enum order */
	NODETOCODE_API TConstArrayView<FN2CTargetLanguage> GetLanguages();

	/** The row for Language. An out-of-range value gets the C++ row (what older builds read unknown values as). */
	NODETOCODE_API const FN2CTargetLanguage& GetLanguage(EN2CCodeLanguage Language);

	/** The row for Language, or null when no row describes it (the table does not cover the enum) */
	NODETOCODE_API const FN2CTargetLanguage* FindLanguageRow(EN2CCodeLanguage Language);

	/**
	 * Read a language name, ignoring case and surrounding spaces: the id (Python, python), the enum-qualified form
	 * older batch translations stored (EN2CCodeLanguage::Python), the display name (C++, C#) or the prompt key (CPP,
	 * which the main window stored for every language). Null for an empty or unknown name.
	 */
	NODETOCODE_API const FN2CTargetLanguage* FindLanguage(const FString& Name);

	/** The prompt key of Language in a family: CodeGen_CPP, CodeGen_Python, ... */
	NODETOCODE_API FString GetPromptKey(EN2CCodeLanguage Language, const TCHAR* Family = BlueprintPromptFamily);
}
