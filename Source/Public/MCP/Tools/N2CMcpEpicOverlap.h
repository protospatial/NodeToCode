// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolsets.h"

/**
 * NodeToCode's MCP tools that Epic's Unreal MCP also has on UE 5.8 (card 0174), and whether the server leaves them to
 * Unreal MCP. Applies owner decision 0089 provisionally as the review's option (b): on 5.8 Epic's EditorToolset does
 * Blueprint and asset CRUD and NodeToCode serves what only it has (N2C JSON, translation, tags, Python script
 * library); on 5.6 and 5.7 nothing changes.
 *
 * The switch point is bLeaveToUnrealMcpByDefault: it is what the Automatic value of Project Settings > Overlapping
 * Tools does. A user who picked Leave them to Unreal MCP or Always serve them keeps that choice whatever the default,
 * and a Custom tool profile (an explicit list of toolsets) is served as listed under Automatic.
 */
namespace N2CMcpEpicOverlap
{
	/**
	 * Decision 0089, provisionally option (b): true leaves the overlapping tools to Unreal MCP by default on UE 5.8 when
	 * Unreal MCP and EditorToolset are enabled. Set false to serve every tool by default on every engine (option a).
	 */
	inline constexpr bool bLeaveToUnrealMcpByDefault = true;

	/** One NodeToCode tool and the Epic tools that do the same (the review's overlap matrix, verdict "deprecate") */
	struct FOverlap
	{
		const TCHAR* Tool;
		const TCHAR* EpicCounterpart;
	};

	/**
	 * The overlapping tools. wiki/developers/NodeToCode-and-Epic-Tool-Mapping.md lists the same tools (pytest
	 * Tests/python/test_epic_overlap_mapping.py keeps them equal), and NodeToCode.MCP.EpicOverlap checks each names a
	 * registered tool.
	 */
	inline constexpr FOverlap Tools[] = {
		// BlueprintDiscoveryTools
		{ TEXT("list-blueprint-functions"), TEXT("BlueprintTools.list_functions, list_graphs") },
		{ TEXT("list-overridable-functions"), TEXT("BlueprintTools.list_functions, list_events") },
		{ TEXT("search-blueprint-nodes"), TEXT("BlueprintTools.find_node_types, find_node_categories, get_node_type_pins") },
		// BlueprintGraphTools
		{ TEXT("add-bp-node-to-active-graph"), TEXT("BlueprintTools.create_node, write_graph_dsl") },
		{ TEXT("set-input-pin-value"), TEXT("BlueprintTools.set_pin_value") },
		// BlueprintFunctionTools
		{ TEXT("create-blueprint-function"), TEXT("BlueprintTools.add_function_graph") },
		{ TEXT("add-function-input-pin"), TEXT("BlueprintTools.add_function_param") },
		{ TEXT("add-function-return-pin"), TEXT("BlueprintTools.add_function_param") },
		{ TEXT("remove-function-entry-pin"), TEXT("BlueprintTools.remove_function_param") },
		{ TEXT("remove-function-return-pin"), TEXT("BlueprintTools.remove_function_param") },
		// BlueprintVariableTools
		{ TEXT("create-get-member-variable-node"), TEXT("BlueprintTools.create_node") },
		{ TEXT("create-set-member-variable-node"), TEXT("BlueprintTools.create_node") },
		{ TEXT("create-get-local-function-variable-node"), TEXT("BlueprintTools.create_node") },
		{ TEXT("create-set-local-function-variable-node"), TEXT("BlueprintTools.create_node") },
		{ TEXT("get-blueprint-member-variables"), TEXT("BlueprintTools.list_variables") },
		{ TEXT("get-blueprint-function-local-variables"), TEXT("BlueprintTools.list_variables") },
		{ TEXT("set-member-variable-default-value"), TEXT("BlueprintTools.get_default_object, ObjectTools.set_properties") },
		// BlueprintCompileTools
		{ TEXT("compile-blueprint"), TEXT("BlueprintTools.compile_blueprint") },
		{ TEXT("save-blueprint"), TEXT("AssetTools.save_assets") },
		// BlueprintClassTools
		{ TEXT("create-blueprint-class"), TEXT("BlueprintTools.create") },
		{ TEXT("search-blueprint-classes"), TEXT("ObjectTools.search_subclasses") },
		// BlueprintComponentTools
		{ TEXT("add-component-class-to-actor"), TEXT("ActorTools.add_component") },
		{ TEXT("delete-component-in-actor"), TEXT("ActorTools.remove_component") },
		{ TEXT("list-components-in-actor"), TEXT("ActorTools.get_components") },
		// ContentBrowserTools
		{ TEXT("copy-asset"), TEXT("AssetTools.duplicate") },
		{ TEXT("create-folder"), TEXT("AssetTools.create_folder") },
		{ TEXT("move-asset"), TEXT("AssetTools.move") },
		{ TEXT("move-folder"), TEXT("AssetTools.move") },
		{ TEXT("rename-asset"), TEXT("AssetTools.move") },
		{ TEXT("rename-folder"), TEXT("AssetTools.move") },
		{ TEXT("open-blueprint-asset"), TEXT("EditorAppToolset.OpenEditorForAsset") },
		{ TEXT("open-content-browser-path"), TEXT("EditorAppToolset.SetContentBrowserPath") },
		{ TEXT("read-content-browser-path"), TEXT("AssetTools.list_folders, find_assets") },
		{ TEXT("search-content-browser"), TEXT("AssetTools.find_assets") },
	};

	/** The Epic tools that do what ToolName does, or null when ToolName has no Epic counterpart */
	NODETOCODE_API const TCHAR* FindEpicCounterpart(const FString& ToolName);

	/** The plugins that must both be enabled for Unreal MCP to serve the counterparts: the server and EditorToolset */
	inline constexpr const TCHAR* UnrealMcpPlugin = TEXT("ModelContextProtocol");
	inline constexpr const TCHAR* EditorToolsetPlugin = TEXT("EditorToolset");

	/** What the decision reads from the running editor */
	struct FEnvironment
	{
		/** The engine is UE 5.8 or later (Unreal MCP exists) */
		bool bEngineHasUnrealMcp = false;

		/** IPluginManager reports ModelContextProtocol enabled */
		bool bUnrealMcpEnabled = false;

		/** IPluginManager reports EditorToolset enabled */
		bool bEditorToolsetEnabled = false;

		/** This editor: the engine version it was built for and its enabled plugins */
		NODETOCODE_API static FEnvironment Current();
	};

	/** Whether the overlapping tools are left out, and why, in words for the log */
	struct FDecision
	{
		bool bLeaveToUnrealMcp = false;
		FString Reason;
	};

	/**
	 * Whether the server leaves the overlapping tools to Unreal MCP. Never when the engine has no Unreal MCP or
	 * ModelContextProtocol or EditorToolset is not enabled. Otherwise: AlwaysServe never; LeaveToUnrealMcp in every
	 * profile; Automatic follows bDefault, and only for the Default toolsets profile (Custom and Python scripting only
	 * serve the toolsets the user named).
	 * @param bDefault what Automatic does (bLeaveToUnrealMcpByDefault; specs pass both values)
	 */
	NODETOCODE_API FDecision Decide(EN2CMcpOverlappingTools Setting, EN2CMcpToolProfile Profile, const FEnvironment& Environment,
		bool bDefault = bLeaveToUnrealMcpByDefault);
}
