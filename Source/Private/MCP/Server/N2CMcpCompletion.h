// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class FN2CMcpPromptManager;

/**
 * completion/complete (MCP server/utilities/completion, card 0214): values for a prompt argument (the argument's
 * CompletionValues, such as the language ids of generate-code) and for the path variable of the
 * nodetocode://blueprint/{+path} resource template (Blueprint asset paths from the AssetRegistry, project and plugin
 * content, never /Engine or /Script). Values start with what the client typed, case-insensitively; at most MaxValues,
 * with total and hasMore.
 */
namespace N2CMcpCompletion
{
	/** The spec's limit on values per response */
	constexpr int32 MaxValues = 100;

	/**
	 * The result of completion/complete for Params ({ref: {type: "ref/prompt", name} or {type: "ref/resource", uri},
	 * argument: {name, value}}): {completion: {values, total, hasMore}}. Null with OutError ("ref ...", "argument ...")
	 * when Params is malformed or names no prompt or resource template of this server. Game thread.
	 */
	TSharedPtr<FJsonObject> Complete(const TSharedPtr<FJsonObject>& Params, const FN2CMcpPromptManager& Prompts, FString& OutError);

	/**
	 * Blueprint asset paths as the template takes them (Game/Blueprints/BP_Door: package name without its leading '/')
	 * that start with Prefix (case-insensitive), sorted. Game thread.
	 */
	TArray<FString> BlueprintPaths(const FString& Prefix);
}
