// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolTypes.h"

/**
 * Refuses tools that change editor content while a Play In Editor or Simulate session runs (card 0170). Compiling or
 * editing a Blueprint during PIE reinstances the running game's actors, and moving or renaming an asset it uses can
 * break the session or the asset; UEditorAssetSubsystem refuses such calls during PIE, and so does NodeToCode now.
 *
 * Which tools: every tool that is not read-only (FMcpToolAnnotations::IsReadOnly(), so an unset readOnlyHint counts
 * as changing things, as MCP's default does), unless its definition sets bAllowedDuringPlaySession. The rule is read
 * from each tool's own metadata, never from a list of names. FN2CMcpToolManager applies it to every tools/call
 * (ExecuteTool) and FN2CToolAsyncTaskManager to every long-running call, and registration appends DescriptionNote to
 * the description of each tool it refuses, so an agent reads the rule in tools/list.
 */
struct FN2CMcpPlaySessionGuard
{
	/** The error code a refused call starts with */
	static const TCHAR* const ErrorCode;

	/** The sentence added to the description of every tool the rule refuses */
	static const TCHAR* const DescriptionNote;

	/** Whether a PIE or Simulate session is running (GEditor->PlayWorld is set or a play session is in progress) */
	static bool IsPlaySessionActive();

	/** Whether the rule refuses this tool while a play session runs: not read-only and not bAllowedDuringPlaySession */
	static bool IsRefusedDuringPlaySession(const FMcpToolDefinition& Definition);

	/**
	 * The PIE_ACTIVE error result for a call of this tool when a play session runs and the rule refuses the tool;
	 * unset when the call may run
	 */
	static TOptional<FMcpToolCallResult> CheckCall(const FMcpToolDefinition& Definition);

	/** Appends DescriptionNote to the description of a tool the rule refuses (once); other definitions are unchanged */
	static void AddRuleToDescription(FMcpToolDefinition& Definition);
};
