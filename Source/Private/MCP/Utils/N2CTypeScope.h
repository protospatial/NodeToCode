// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"

/** Where a type or asset comes from */
enum class EN2CTypeScope : uint8
{
	/** The engine's own modules and /Engine content (and a module the manager does not know, compiled into the executable) */
	Engine,
	/** A plugin loaded from the engine's Plugins folder (marketplace plugins installed into the engine included) */
	EnginePlugin,
	/** A plugin loaded from the project's Plugins folder */
	ProjectPlugin,
	/** The project's own modules, /Game content, and any other mount point that is not a plugin's (/Temp, /Memory) */
	Project
};

/**
 * One classifier of type and asset paths for the search tools (card 0348, review arch-arch-mcp-7). Before it,
 * search-variable-types read where a module's binary lives, while search-blueprint-classes knew four engine roots
 * (/Script/Engine, /Script/CoreUObject, /Script/UMG, /Engine/), so /Script/AIModule was a plugin class, and tested
 * Contains("/Plugins/"), which no object path contains, so a plugin's content Blueprints were the project's.
 *
 * A native path (/Script/<Module>.<Type>) is classified by the module's owner: the plugin whose descriptor lists the
 * module, the current project's descriptor, or else the engine. A content path (/<Mount>/...) by its mount point:
 * /Engine, /Game, or the plugin mounted there. Export text (Class'/Game/BP.BP_C') is read as its object path.
 *
 * Construct one per tool call: it reads the enabled plugins' module lists once (no file access), so a plugin enabled
 * later is seen by the next call. Game thread or any thread that may call IPluginManager.
 */
class NODETOCODE_API FN2CTypeScope
{
public:
	FN2CTypeScope();

	/** The scope of a type path, object path, package path or export text */
	EN2CTypeScope Classify(const FString& Path) const;

	/** Engine and engine plugins: what the includeEngine* filters leave out */
	static bool IsEngine(EN2CTypeScope Scope) { return Scope == EN2CTypeScope::Engine || Scope == EN2CTypeScope::EnginePlugin; }

	/** Engine plugins and project plugins: what includePluginClasses leaves out */
	static bool IsPlugin(EN2CTypeScope Scope) { return Scope == EN2CTypeScope::EnginePlugin || Scope == EN2CTypeScope::ProjectPlugin; }

	/** engine, engine_plugin, project_plugin or project */
	static const TCHAR* ToString(EN2CTypeScope Scope);

private:
	/** Native module name -> its owner's scope, for plugin and project modules; any other module is the engine's */
	TMap<FName, EN2CTypeScope> ModuleScopes;
};
