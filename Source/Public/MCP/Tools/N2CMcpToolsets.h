// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "N2CMcpToolsets.generated.h"

/**
 * One group of NodeToCode MCP tools, declared once below (decision 0005, card 0165). A tool names its toolset by
 * identity in its FMcpToolDefinition (N2CMcpToolsets::BlueprintGraph, not a string), so a tool cannot name a toolset
 * that does not exist. The name is what list_toolsets, describe_toolset and call_tool use and what Project Settings
 * shows; it follows Epic's toolset class names (BlueprintTools, MaterialTools), and a UE 5.8 registry adapter
 * publishes it as "NodeToCode.<Name>". The description is all a model sees of a toolset before it describes it.
 */
struct FN2CMcpToolset
{
	/** Unique name, PascalCase, ending in "Tools" */
	const TCHAR* Name;

	/** One to three sentences: the domain and the kinds of operation, standing on their own */
	const TCHAR* Description;

	/** Enabled by the Default Toolsets profile */
	bool bDefaultEnabled;
};

/** The declared toolsets. Add a toolset here and to All; every tool's definition must name one of them. */
namespace N2CMcpToolsets
{
	inline constexpr FN2CMcpToolset BlueprintDiscovery{ TEXT("BlueprintDiscoveryTools"),
		TEXT("Find and read Blueprints: the Blueprint focused in the editor, open Blueprint editors, a Blueprint's graphs and functions, the functions it can override, and the node types a graph can use. Graphs are returned as N2C JSON."),
		true };

	inline constexpr FN2CMcpToolset BlueprintGraph{ TEXT("BlueprintGraphTools"),
		TEXT("Edit the nodes of a Blueprint graph: find, add and delete nodes, connect pins, set input pin values and add comment boxes."),
		true };

	inline constexpr FN2CMcpToolset BlueprintFunction{ TEXT("BlueprintFunctionTools"),
		TEXT("Create, open and delete Blueprint functions, and add or remove their input and return pins."),
		true };

	inline constexpr FN2CMcpToolset BlueprintVariable{ TEXT("BlueprintVariableTools"),
		TEXT("Create Blueprint member variables and function local variables, read and set their default values, add get and set nodes for them, and search the variable types a Blueprint can use."),
		true };

	inline constexpr FN2CMcpToolset BlueprintTag{ TEXT("BlueprintTagTools"),
		TEXT("Tag Blueprint graphs so they can be found and translated as a group, and list or remove those tags."),
		true };

	inline constexpr FN2CMcpToolset BlueprintCompile{ TEXT("BlueprintCompileTools"),
		TEXT("Compile Blueprints and report their errors and warnings, and save Blueprints to disk."),
		true };

	inline constexpr FN2CMcpToolset BlueprintClass{ TEXT("BlueprintClassTools"),
		TEXT("Create Blueprint assets from a parent class, and search the classes a Blueprint can derive from."),
		true };

	inline constexpr FN2CMcpToolset BlueprintComponent{ TEXT("BlueprintComponentTools"),
		TEXT("List, add and delete the components of an Actor Blueprint."),
		true };

	inline constexpr FN2CMcpToolset ContentBrowser{ TEXT("ContentBrowserTools"),
		TEXT("Search and browse the project's assets and folders, open them in the editor, and copy, move or rename them."),
		true };

	inline constexpr FN2CMcpToolset FileSystem{ TEXT("FileSystemTools"),
		TEXT("Read text files and list folders inside the project directory."),
		true };

	inline constexpr FN2CMcpToolset Python{ TEXT("PythonTools"),
		TEXT("Run Python in the editor with the unreal module (only when the user allows MCP Python execution), and keep a library of reusable Python scripts to list, search, read, save and delete."),
		true };

	inline constexpr FN2CMcpToolset Translation{ TEXT("TranslationTools"),
		TEXT("Translate the focused Blueprint graph into C++, Python, C#, JavaScript, Swift or pseudocode with the LLM provider configured in Project Settings, and read the available providers, target languages and output folder."),
		true };

	/** Every declared toolset, in the order list_toolsets shows them */
	inline constexpr const FN2CMcpToolset* All[] = {
		&BlueprintDiscovery, &BlueprintGraph, &BlueprintFunction, &BlueprintVariable, &BlueprintTag, &BlueprintCompile,
		&BlueprintClass, &BlueprintComponent, &ContentBrowser, &FileSystem, &Python, &Translation };

	/** The declared toolset with this name (case-insensitive), or null */
	NODETOCODE_API const FN2CMcpToolset* Find(const FString& Name);

	/** The names of every declared toolset, in declaration order */
	NODETOCODE_API TArray<FString> AllNames();
}

/** How the MCP server presents the tools of its enabled toolsets in tools/list (Project Settings: Tool Exposure) */
UENUM()
enum class EN2CMcpToolExposure : uint8
{
	/** tools/list holds every tool of the enabled toolsets */
	ListAllTools UMETA(DisplayName = "List every tool"),

	/** tools/list holds only list_toolsets, describe_toolset and call_tool, named and shaped like UE 5.8's tool search */
	ToolSearch UMETA(DisplayName = "Tool search (list_toolsets, describe_toolset, call_tool)"),
};

/** Which toolsets the MCP server serves (Project Settings: Tool Profile) */
UENUM()
enum class EN2CMcpToolProfile : uint8
{
	/** Every toolset declared default-enabled */
	DefaultToolsets UMETA(DisplayName = "Default toolsets"),

	/** PythonTools only: run-python and the script library (the former Python Script-Only Mode) */
	PythonScripting UMETA(DisplayName = "Python scripting only"),

	/** The toolsets named in Custom Toolsets */
	Custom UMETA(DisplayName = "Custom toolsets"),
};
