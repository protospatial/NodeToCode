// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

struct FMcpToolCallResult;

/**
 * Marks text that NodeToCode's MCP server takes from a project's Blueprints (node comments, titles and names,
 * variable and function tooltips, descriptions, categories, string literals) as untrusted data before an agent reads
 * it (card 0124, threat model Context/mcp-server/threat-model.md). That text can come from a Fab asset, a third-party
 * pack or a teammate, and an agent connected to the server may also hold mutating tools or run-python.
 *
 * The mark is framing, not a filter: nothing is removed, so the agent still reads every comment.
 * - Tool results and the nodetocode://blueprint/current resource keep their JSON and gain a first key,
 *   "untrusted_content", holding the notice (MarkJson).
 * - The prompts put the notice, as its own message, right before the embedded Blueprint resource, and set
 *   annotations.audience on it.
 * - Every "</" in the text is written "<\/" (EscapeClosingTags): the same JSON string, but a comment can no longer
 *   close a tag a client or prompt wraps the result in (<n2c_json>, <function_results>, ...).
 */
struct FN2CMcpUntrustedContent
{
	/** The lead-in an agent reads before any Blueprint text (the wording card 0124 asks for) */
	static const TCHAR* const Notice;

	/** The key MarkJson adds as the first key of a JSON object */
	static const TCHAR* const MarkerField;

	/** The value of MarkerField: Notice plus which strings it covers */
	static FString MarkerText();

	/**
	 * "</" becomes "<\/". Inside JSON text "</" can only occur in a string value, where "\/" is JSON's escape for
	 * "/", so the JSON decodes to the same strings.
	 */
	static FString EscapeClosingTags(const FString& Text);

	/**
	 * The result text of a tool or resource that carries Blueprint text: a JSON object gets MarkerField as its first
	 * key (the rest is kept byte for byte, apart from EscapeClosingTags); any other text is prefixed with the notice
	 * paragraph. Text that already carries the mark is returned unchanged, so marking twice adds one mark.
	 */
	static FString MarkJson(const FString& Text);

	/** Whether Text starts with the mark MarkJson adds (a JSON object's first key, or the notice paragraph) */
	static bool IsMarked(const FString& Text);

	/** Marks every text item of a successful tool result (MarkJson); error results are left as they are */
	static void MarkToolResult(FMcpToolCallResult& Result);

	/** MCP annotations for content meant for the model to read as data: {"audience": ["assistant"]} */
	static TSharedPtr<FJsonObject> AssistantAudience();
};
