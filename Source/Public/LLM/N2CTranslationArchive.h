// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Code Editor/Models/N2CCodeLanguage.h"
#include "Models/N2CTranslation.h"

class FJsonObject;
struct FN2CBlueprint;

/** Who started a translation run: the manifest's "kind" */
enum class EN2CTranslationRunKind : uint8
{
	/** One graph translated from the editor's NodeToCode window */
	Single,
	/** A batch of tagged graphs (the editor's batch or translate-batch) */
	Batch,
	/** A translation an MCP tool or the Python API asked for (translate-focused-blueprint, translate_json) */
	Mcp,
};

/** One entry of a translation response as written to disk: its own folder and files */
struct NODETOCODE_API FN2CArchivedEntry
{
	/** The graph name the response gave */
	FString GraphName;
	/** The response's graph_type (EventGraph, Function, Struct, Enum, ...) */
	FString GraphType;
	/** Index of this entry in the response's Graphs */
	int32 ResponseIndex = INDEX_NONE;
	/** root (the translated graph), nested (another graph of the response), struct or enum */
	FString Role;
	/** Absolute folder holding the entry's files */
	FString Folder;
	/** Absolute paths of the files written, empty when the response had no text for them */
	FString DeclarationFile;
	FString ImplementationFile;
	FString NotesFile;
};

/** What the archive wrote for one response: a run's single translation or one batch item */
struct NODETOCODE_API FN2CArchivedResponse
{
	/** Absolute folder that holds the entry folders: the run folder, or the batch item's folder */
	FString Folder;
	TArray<FN2CArchivedEntry> Entries;
	/** Index of the root entry in Entries, INDEX_NONE when the response had no entries */
	int32 RootIndex = INDEX_NONE;

	const FN2CArchivedEntry* GetRoot() const { return Entries.IsValidIndex(RootIndex) ? &Entries[RootIndex] : nullptr; }
};

/** What a run's manifest says about where the translation came from */
struct NODETOCODE_API FN2CTranslationRunInfo
{
	EN2CTranslationRunKind Kind = EN2CTranslationRunKind::Single;
	/** The run folder's name part: the Blueprint's name, or BatchTranslation */
	FString Name;
	/** Provider and model short names (Anthropic, claude-haiku-5-5) */
	FString Provider;
	FString Model;
	EN2CCodeLanguage Language = EN2CCodeLanguage::Cpp;
};

/** One response to write: what was sent and where it belongs */
struct NODETOCODE_API FN2CArchiveSource
{
	/** The N2C JSON that was sent (saved beside the translation) */
	FString InputJson;
	/** The capture InputJson was serialized from, when the caller has it (saved pretty and minified) */
	const FN2CBlueprint* Capture = nullptr;
	/** The graph the caller asked for (a tag's graph name): the entry with this name is the root entry */
	FString GraphName;
	/** The tagged graph's Blueprint and GUID, for the manifest (batch items) */
	FString BlueprintPath;
	FString GraphGuid;
};

/**
 * The translation archive (card 0252): the only code that creates translation folders and writes translation files.
 *
 * Layout under the translation output directory (FN2CPathUtils::GetTranslationsBasePath):
 *
 *     <Name>_<yyyyMMdd-HHmmss-fff>_<run id>/        one run; two runs never share a folder
 *         manifest.json                             what the run holds (WriteManifest)
 *         N2C_BP_<run folder>.json                  single and MCP runs: the N2C JSON sent (pretty when the capture is known)
 *         N2C_BP_Minified_<run folder>.json         single runs: the capture, minified
 *         N2C_Translation_<run folder>.json         single and MCP runs: the parsed response (graphs and usage)
 *         <Entry>/                                  single and MCP runs: one folder per response entry
 *         <Item>/<Entry>/                           batch runs: one folder per item, one per entry inside it
 *         <Item>/<Entry>/<Graph>.json               batch runs: the item's N2C JSON, in its root entry's folder
 *         BatchSummary.json                         batch runs (written by the orchestrator through WriteRunFile)
 *
 * Entry folders are named after the response's graph name (GetTranslationFileStem), with _2, _3 on a repeated name.
 * An entry holds <Entry><impl ext> (graphImplementation), <Entry>_Notes.txt and the declaration: <Entry><decl ext>
 * for a language with a declaration file (C++), otherwise <Entry><impl ext> when the entry has no implementation (a
 * C# struct is S_Item.cs) or <Entry>_Declaration<impl ext> beside one. Every file is written to a temporary file
 * first and then moved into place.
 *
 * A run object is used on the game thread only; nothing here is thread-safe.
 */
class NODETOCODE_API FN2CTranslationArchive
{
public:
	/** The manifest's schemaVersion */
	static constexpr int32 ManifestSchemaVersion = 1;

	/**
	 * Starts a run: creates its folder under BaseDirectory (the translation output directory when empty) and writes
	 * an empty manifest. Null when the folder cannot be created.
	 */
	static TSharedPtr<FN2CTranslationArchive> BeginRun(const FN2CTranslationRunInfo& Info, const FString& BaseDirectory = FString());

	/**
	 * Creates a new, uniquely named folder <Prefix>_<yyyyMMdd-HHmmss-fff>_<id> under BaseDirectory (the translation
	 * output directory when empty), for output that is not a translation run (the batch JSON export). Empty on failure.
	 */
	static FString CreateUniqueFolder(const FString& Prefix, const FString& BaseDirectory = FString());

	/** Writes Text to Path through a temporary file moved into place (UTF-8). False when either step fails. */
	static bool SaveTextAtomic(const FString& Path, const FString& Text);

	/** The run folder the latest BeginRun created (UN2CLLMModule::GetLatestTranslationPath), empty before the first */
	static FString GetLatestRunFolder();

	/** The role an entry of this graph_type gets when it is not the root: struct, enum or nested */
	static FString GetEntryRole(const FString& GraphType);

	/**
	 * Writes one response. ItemFolder empty writes a single or MCP translation at the run's root (with the N2C JSON
	 * and response files); a batch item names its own folder (a safe name, unique in the run). Updates the manifest.
	 * False when no entry could be written; OutResult then lists what was.
	 */
	bool WriteResponse(const FN2CTranslationResponse& Response, const FN2CArchiveSource& Source, const FString& ItemFolder,
		FN2CArchivedResponse& OutResult);

	/** Writes another file of the run (BatchSummary.json) at RelativePath under the run folder, atomically */
	bool WriteRunFile(const FString& RelativePath, const FString& Text) const;

	/** Rewrites manifest.json from what the run has written so far */
	bool WriteManifest() const;

	const FString& GetFolder() const { return Folder; }
	const FString& GetRunId() const { return RunId; }
	const FN2CTranslationRunInfo& GetInfo() const { return Info; }

	/** The kind as the manifest writes it: single, batch or mcp */
	static FString KindToString(EN2CTranslationRunKind Kind);

private:
	FN2CTranslationArchive(const FN2CTranslationRunInfo& InInfo, const FString& InFolder, const FString& InRunId);

	/** A folder name under Parent not used yet in this run (case-insensitive): Stem, then Stem_2, Stem_3, ... */
	FString ClaimFolderName(const FString& Parent, const FString& Stem);

	/** One written response, for the manifest */
	struct FWrittenItem
	{
		FN2CArchiveSource Source;
		FN2CArchivedResponse Result;
		FN2CTranslationUsage Usage;
		FString RequestId;
		FString Timestamp;
	};

	FN2CTranslationRunInfo Info;
	FString Folder;
	FString RunId;
	FString Created;
	TArray<FWrittenItem> Items;
	/** Folder names taken in this run, lower case, as Parent|Name */
	TSet<FString> ClaimedFolders;
};
