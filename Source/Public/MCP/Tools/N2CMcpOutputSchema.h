// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/**
 * Building blocks for a tool's outputSchema (card 0207) and a small checker for them. A tool that declares an
 * OutputSchema returns FMcpToolCallResult::CreateJsonResult, so every successful result carries structuredContent that
 * matches it, plus the same JSON as text.
 *
 * Schemas are JSON Schema 2020-12 objects of the parts MCP clients check: type, properties, required, items, enum and
 * description. Objects allow fields the schema does not name (a later version may add one), so only what a client can
 * rely on is listed. A tool whose results carry Blueprint text also lists "untrusted_content" (UntrustedObject).
 */
namespace N2CMcpOutputSchema
{
	/** {"type": Type} with an optional description */
	NODETOCODE_API TSharedRef<FJsonObject> Scalar(const TCHAR* Type, const FString& Description = FString());
	NODETOCODE_API TSharedRef<FJsonObject> String(const FString& Description = FString());
	NODETOCODE_API TSharedRef<FJsonObject> Integer(const FString& Description = FString());
	NODETOCODE_API TSharedRef<FJsonObject> Number(const FString& Description = FString());
	NODETOCODE_API TSharedRef<FJsonObject> Boolean(const FString& Description = FString());

	/** A string limited to Values */
	NODETOCODE_API TSharedRef<FJsonObject> Enum(const TArray<FString>& Values, const FString& Description = FString());

	/** {"type": "array", "items": Items} */
	NODETOCODE_API TSharedRef<FJsonObject> Array(const TSharedRef<FJsonObject>& Items, const FString& Description = FString());

	/** {"type": "object", "properties": ..., "required": [...]} (fields it does not name are allowed) */
	NODETOCODE_API TSharedRef<FJsonObject> Object(const TArray<TPair<FString, TSharedRef<FJsonObject>>>& Properties,
		const TArray<FString>& Required = {}, const FString& Description = FString());

	/** Object plus the optional "untrusted_content" notice FN2CMcpUntrustedContent adds to results with Blueprint text */
	NODETOCODE_API TSharedRef<FJsonObject> UntrustedObject(TArray<TPair<FString, TSharedRef<FJsonObject>>> Properties,
		const TArray<FString>& Required = {}, const FString& Description = FString());

	/**
	 * An N2C JSON document as get-focused-blueprint and get-blueprint-n2cjson return it: version, metadata (name,
	 * blueprint_type, blueprint_class), graphs (name, graph_type, nodes, flows), structs and enums. Only the top
	 * level is described; Content/Prompting/Shared/N2CJsonSpec.md is the full contract.
	 */
	NODETOCODE_API TSharedRef<FJsonObject> N2CJsonDocument();

	/**
	 * Object with "untrusted_content" (FN2CMcpUntrustedContent's notice) as its first key, followed by Object's fields;
	 * Object itself when it has the key. The structuredContent of a result whose text FN2CMcpUntrustedContent marks.
	 */
	NODETOCODE_API TSharedRef<FJsonObject> MarkUntrusted(const TSharedRef<FJsonObject>& Object);

	/** The object a text result's JSON parses to, or null when the text is not a JSON object */
	NODETOCODE_API TSharedPtr<FJsonObject> ParseObject(const FString& Text);

	/**
	 * Checks Value against Schema (type, required, properties, items, enum; nested). Each mismatch is added to
	 * OutErrors as "<path>: <what>", path from "$".
	 * @return true when there is none
	 */
	NODETOCODE_API bool Validate(const TSharedPtr<FJsonObject>& Schema, const TSharedPtr<FJsonValue>& Value, TArray<FString>& OutErrors,
		const FString& Path = TEXT("$"));
}
