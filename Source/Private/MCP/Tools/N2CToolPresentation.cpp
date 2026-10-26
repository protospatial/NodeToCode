// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "MCP/Tools/N2CToolCatalog.h"
#include "MCP/Tools/N2CMcpToolManager.h"

/*
 * The presentation table (card 0205): every tool's title and its four behaviour hints, in one place.
 *
 * Hint rules (review rec-mcp-spec-clients-7; FMcpToolAnnotations says what each hint means):
 * - readOnly: the tool changes no Blueprint, asset, folder, level, setting, file the user keeps or editor focus. The
 *   N2C JSON exports (get-blueprint-n2cjson toFile, export-n2cjson-batch) write only scratch files under
 *   Saved/NodeToCode/Exports and stay read-only. search-blueprint-nodes (card 0050) and list-components-in-actor
 *   (card 0167) no longer change anything and are read-only. The Play In Editor rule reads this hint (card 0170).
 * - destructive (only when not read-only): the tool removes, moves, renames or overwrites something: delete-*,
 *   remove-*, move-*, rename-*, set-* (the old value is replaced), save-* (the file on disk is replaced), connect-pins
 *   (breakExistingLinks replaces a link), run-python and call_tool (anything). create-*, add-*, copy-asset (never
 *   replaces), tag-blueprint-graph, compile-blueprint and the editor navigation tools are additive.
 * - idempotent (only when not read-only): a second call with the same arguments changes nothing more: set-*, delete-*
 *   and remove-* (the target is gone), connect-pins (the link exists), create-folder, tag-blueprint-graph (a tag is
 *   added once), compile-blueprint, save-*, the open-* tools. A tool that makes a new thing each call (create-* of
 *   nodes, variables, functions and classes, add-*) or acts relative to where things are now (move-*, rename-*,
 *   copy-asset) is not.
 * - openWorld: the tool reaches past the editor and the project: the LLM translation tools, run-python and call_tool.
 *
 * NodeToCode.MCP.ToolPresentation checks that every registered tool has an entry and the entries against these rules.
 */
namespace N2CToolPresentationPrivate
{
	/** maxResultSizeChars for tools whose results can be whole Blueprint documents, files or translations */
	constexpr int32 LargeResultChars = 200000;

	FN2CToolPresentation Make(const TCHAR* Name, const TCHAR* Title, const FMcpToolAnnotations& Hints)
	{
		FN2CToolPresentation Entry;
		Entry.Name = Name;
		Entry.Title = Title;
		Entry.Hints = Hints;
		return Entry;
	}

	/** A tool that only reads */
	FN2CToolPresentation Read(const TCHAR* Name, const TCHAR* Title)
	{
		return Make(Name, Title, FMcpToolAnnotations::ReadOnly());
	}

	/** A tool that changes something, inside the editor and project unless bOpenWorld */
	FN2CToolPresentation Write(const TCHAR* Name, const TCHAR* Title, bool bDestructive, bool bIdempotent, bool bOpenWorld = false)
	{
		return Make(Name, Title, FMcpToolAnnotations(false, bDestructive, bIdempotent, bOpenWorld));
	}

	FN2CToolPresentation Large(FN2CToolPresentation Entry)
	{
		Entry.MaxResultSizeChars = LargeResultChars;
		return Entry;
	}

	FN2CToolPresentation AlwaysLoad(FN2CToolPresentation Entry)
	{
		Entry.bAlwaysLoad = true;
		return Entry;
	}

	TArray<FN2CToolPresentation> BuildTable()
	{
		constexpr bool Destructive = true;
		constexpr bool Additive = false;
		constexpr bool Idempotent = true;
		constexpr bool Repeats = false;
		constexpr bool OpenWorld = true;

		TArray<FN2CToolPresentation> Table = {
			// Blueprint discovery (Blueprint/Analysis)
			Read(TEXT("export-n2cjson-batch"), TEXT("Export N2C JSON for many graphs")),
			AlwaysLoad(Read(TEXT("get-blueprint-n2cjson"), TEXT("Read a Blueprint as N2C JSON"))),
			AlwaysLoad(Large(Read(TEXT("get-focused-blueprint"), TEXT("Read the focused Blueprint graph")))),
			Read(TEXT("get-open-blueprint-editors"), TEXT("List open Blueprint editors")),
			Read(TEXT("list-blueprint-functions"), TEXT("List Blueprint functions")),
			Read(TEXT("list-blueprint-graphs"), TEXT("List Blueprint graphs")),
			Read(TEXT("list-overridable-functions"), TEXT("List overridable functions")),
			Read(TEXT("search-blueprint-nodes"), TEXT("Search Blueprint node actions")),

			// Blueprint classes
			Write(TEXT("create-blueprint-class"), TEXT("Create a Blueprint class"), Additive, Repeats),
			Read(TEXT("search-blueprint-classes"), TEXT("Search parent classes")),

			// Compilation
			Write(TEXT("compile-blueprint"), TEXT("Compile a Blueprint"), Additive, Idempotent),
			Write(TEXT("save-blueprint"), TEXT("Save a Blueprint"), Destructive, Idempotent),

			// Components
			Write(TEXT("add-component-class-to-actor"), TEXT("Add a component to an Actor Blueprint"), Additive, Repeats),
			Write(TEXT("delete-component-in-actor"), TEXT("Delete a component from an Actor Blueprint"), Destructive, Idempotent),
			Read(TEXT("list-components-in-actor"), TEXT("List an Actor Blueprint's components")),

			// Functions
			Write(TEXT("add-function-input-pin"), TEXT("Add a function input"), Additive, Repeats),
			Write(TEXT("add-function-return-pin"), TEXT("Add a function return value"), Additive, Repeats),
			Write(TEXT("create-blueprint-function"), TEXT("Create a Blueprint function"), Additive, Repeats),
			Write(TEXT("delete-blueprint-function"), TEXT("Delete a Blueprint function"), Destructive, Idempotent),
			Write(TEXT("open-blueprint-function"), TEXT("Open a Blueprint function"), Additive, Idempotent),
			Write(TEXT("remove-function-entry-pin"), TEXT("Remove a function input"), Destructive, Idempotent),
			Write(TEXT("remove-function-return-pin"), TEXT("Remove a function return value"), Destructive, Idempotent),

			// Graph
			Write(TEXT("add-bp-node-to-active-graph"), TEXT("Add a Blueprint node"), Additive, Repeats),
			Write(TEXT("connect-pins"), TEXT("Connect pins"), Destructive, Idempotent),
			Write(TEXT("create-comment-node"), TEXT("Create a comment around nodes"), Additive, Repeats),
			Write(TEXT("delete-blueprint-node"), TEXT("Delete Blueprint nodes"), Destructive, Idempotent),
			Read(TEXT("find-nodes-in-graph"), TEXT("Find nodes in a graph")),
			Write(TEXT("set-input-pin-value"), TEXT("Set an input pin's default value"), Destructive, Idempotent),

			// Organization (tags)
			Read(TEXT("list-blueprint-tags"), TEXT("List graph tags")),
			Write(TEXT("remove-tag-from-graph"), TEXT("Remove a tag from a graph"), Destructive, Idempotent),
			Write(TEXT("tag-blueprint-graph"), TEXT("Tag the focused graph"), Additive, Idempotent),

			// Variables
			Write(TEXT("create-get-local-function-variable-node"), TEXT("Add a Get node for a local variable"), Additive, Repeats),
			Write(TEXT("create-get-member-variable-node"), TEXT("Add a Get node for a member variable"), Additive, Repeats),
			Write(TEXT("create-local-variable"), TEXT("Create a local variable"), Additive, Repeats),
			Write(TEXT("create-set-local-function-variable-node"), TEXT("Add a Set node for a local variable"), Additive, Repeats),
			Write(TEXT("create-set-member-variable-node"), TEXT("Add a Set node for a member variable"), Additive, Repeats),
			Write(TEXT("create-variable"), TEXT("Create a member variable"), Additive, Repeats),
			Read(TEXT("get-blueprint-function-local-variables"), TEXT("List a function's local variables")),
			Read(TEXT("get-blueprint-member-variables"), TEXT("List a Blueprint's member variables")),
			Read(TEXT("search-variable-types"), TEXT("Search variable types")),
			Write(TEXT("set-local-function-variable-default-value"), TEXT("Set a local variable's default value"), Destructive, Idempotent),
			Write(TEXT("set-member-variable-default-value"), TEXT("Set a member variable's default value"), Destructive, Idempotent),

			// Content Browser
			Write(TEXT("copy-asset"), TEXT("Copy an asset"), Additive, Repeats),
			Write(TEXT("create-folder"), TEXT("Create a content folder"), Additive, Idempotent),
			Write(TEXT("move-asset"), TEXT("Move an asset"), Destructive, Repeats),
			Write(TEXT("move-folder"), TEXT("Move a content folder"), Destructive, Repeats),
			Write(TEXT("open-blueprint-asset"), TEXT("Open a Blueprint in its editor"), Additive, Idempotent),
			Write(TEXT("open-content-browser-path"), TEXT("Show a folder in the Content Browser"), Additive, Idempotent),
			Read(TEXT("read-content-browser-path"), TEXT("List a content folder")),
			Write(TEXT("rename-asset"), TEXT("Rename an asset"), Destructive, Repeats),
			Write(TEXT("rename-folder"), TEXT("Rename a content folder"), Destructive, Repeats),
			Read(TEXT("search-content-browser"), TEXT("Search project assets")),

			// File system
			Large(Read(TEXT("read-file"), TEXT("Read a project text file"))),
			Read(TEXT("read-path"), TEXT("List a project directory")),

			// Python
			Write(TEXT("delete-python-script"), TEXT("Delete a library script"), Destructive, Idempotent),
			Read(TEXT("get-python-script"), TEXT("Read a library script")),
			Read(TEXT("get-script-functions"), TEXT("List a library script's functions")),
			Read(TEXT("list-python-scripts"), TEXT("List library scripts")),
			Write(TEXT("run-python"), TEXT("Run Python in the editor"), Destructive, Repeats, OpenWorld),
			Write(TEXT("save-python-script"), TEXT("Save a library script"), Destructive, Idempotent),
			Read(TEXT("search-python-scripts"), TEXT("Search library scripts")),

			// Translation
			Read(TEXT("get-available-llm-providers"), TEXT("List LLM providers")),
			Read(TEXT("get-available-translation-targets"), TEXT("List translation languages")),
			Read(TEXT("get-translation-output-directory"), TEXT("Show the translation output folder")),
			Write(TEXT("translate-batch"), TEXT("Translate many Blueprint graphs"), Additive, Repeats, OpenWorld),
			Large(Write(TEXT("translate-focused-blueprint"), TEXT("Translate the focused Blueprint graph"), Additive, Repeats, OpenWorld)),

			// Tool search meta-tools (FN2CMcpToolManager): the only way in when tools/list holds them, so kept loaded
			AlwaysLoad(Read(FN2CMcpToolManager::ListToolsetsName, TEXT("List toolsets"))),
			AlwaysLoad(Read(FN2CMcpToolManager::DescribeToolsetName, TEXT("Describe a toolset"))),
			// The called tool decides what happens: the hints are the MCP defaults, said out loud
			AlwaysLoad(Write(FN2CMcpToolManager::CallToolName, TEXT("Call a tool"), Destructive, Repeats, OpenWorld)),
		};
		Table.Sort([](const FN2CToolPresentation& A, const FN2CToolPresentation& B) { return A.Name.Compare(B.Name, ESearchCase::CaseSensitive) < 0; });
		return Table;
	}
}

const TArray<FN2CToolPresentation>& FN2CToolCatalog::GetPresentations()
{
	static const TArray<FN2CToolPresentation> Table = N2CToolPresentationPrivate::BuildTable();
	return Table;
}

const FN2CToolPresentation* FN2CToolCatalog::FindPresentation(const FString& Name)
{
	static const TMap<FString, int32> Index = []()
	{
		TMap<FString, int32> Map;
		const TArray<FN2CToolPresentation>& Table = GetPresentations();
		for (int32 Position = 0; Position < Table.Num(); ++Position)
		{
			Map.Add(Table[Position].Name, Position);
		}
		return Map;
	}();
	const int32* Position = Index.Find(Name);
	return Position ? &GetPresentations()[*Position] : nullptr;
}

bool FN2CToolCatalog::ApplyPresentation(FMcpToolDefinition& Definition)
{
	const FN2CToolPresentation* Entry = FindPresentation(Definition.Name);
	if (!Entry)
	{
		return false;
	}
	Definition.Title = Entry->Title;
	Definition.Annotations = Entry->Hints;
	Definition.MaxResultSizeChars = Entry->MaxResultSizeChars;
	Definition.bAlwaysLoad = Entry->bAlwaysLoad;
	return true;
}
