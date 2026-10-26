// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "MCP/Tools/N2CMcpToolsets.h"
#include "N2CMcpToolTypes.generated.h"

/**
 * The behaviour hints of one MCP tool (ToolAnnotations, MCP 2025-11-25). Clients use them for permission prompts and
 * to decide which calls may run in parallel; they are hints, never a security boundary. An unset hint is not sent,
 * and the client then assumes the spec default (readOnly false, destructive true, idempotent false, openWorld true).
 * destructiveHint and idempotentHint only mean something when readOnlyHint is false.
 */
struct NODETOCODE_API FMcpToolAnnotations
{
	/** The tool does not change its environment */
	TOptional<bool> ReadOnlyHint;

	/** A change the tool makes may destroy or overwrite data (only when not read-only) */
	TOptional<bool> DestructiveHint;

	/** Calling the tool again with the same arguments changes nothing more (only when not read-only) */
	TOptional<bool> IdempotentHint;

	/** The tool reaches outside the editor and project (the network, an LLM, arbitrary code) */
	TOptional<bool> OpenWorldHint;

	FMcpToolAnnotations() = default;

	FMcpToolAnnotations(bool bReadOnly, bool bDestructive, bool bIdempotent, bool bOpenWorld)
		: ReadOnlyHint(bReadOnly)
		, DestructiveHint(bDestructive)
		, IdempotentHint(bIdempotent)
		, OpenWorldHint(bOpenWorld)
	{
	}

	/** A tool that only reads editor or project state: read-only, not destructive, idempotent, closed world */
	static FMcpToolAnnotations ReadOnly() { return FMcpToolAnnotations(true, false, true, false); }

	/** True when any hint is set */
	bool IsSet() const
	{
		return ReadOnlyHint.IsSet() || DestructiveHint.IsSet() || IdempotentHint.IsSet() || OpenWorldHint.IsSet();
	}

	/** True only when the tool says it is read-only; an unset hint counts as not read-only, as MCP's default does */
	bool IsReadOnly() const { return ReadOnlyHint.Get(false); }

	/** The annotations object of tools/list (set hints only); null when no hint is set */
	TSharedPtr<FJsonObject> ToJson() const;

	/** Reads the four hints of an annotations object; a missing or non-boolean hint stays unset */
	static FMcpToolAnnotations FromJson(const TSharedPtr<FJsonObject>& JsonObject);
};

/** The _meta keys FMcpToolDefinition sends */
namespace N2CMcpToolMetaKeys
{
	/** Toolset: the name of the declared toolset the tool belongs to (not a Tool field in MCP) */
	inline const TCHAR* const Toolset = TEXT("nodetocode/toolset");

	/** MaxResultSizeChars (Claude Code: the largest text result it keeps before cutting it) */
	inline const TCHAR* const MaxResultSizeChars = TEXT("anthropic/maxResultSizeChars");

	/** bAlwaysLoad (Claude Code: keep the tool loaded when tool search defers the others) */
	inline const TCHAR* const AlwaysLoad = TEXT("anthropic/alwaysLoad");
}

/**
 * MCP Tool Definition
 * Represents metadata for a single MCP tool: one entry of tools/list (Tool, MCP 2025-11-25) plus what the server keeps
 * to itself (bIsLongRunning, bReturnsUntrustedContent). FN2CMcpToolManager builds it once, when the tool is
 * registered, and serves that copy to tools/list and every tools/call; a tool's GetDefinition() must not depend on
 * state that changes while the server runs.
 */
USTRUCT()
struct NODETOCODE_API FMcpToolDefinition
{
	GENERATED_BODY()

	/** The unique name of the tool */
	UPROPERTY()
	FString Name;

	/** Optional human-readable name for display (Tool.title); clients fall back to Name */
	UPROPERTY()
	FString Title;

	/** Optional description of what the tool does */
	UPROPERTY()
	FString Description;

	/**
	 * The declared toolset this tool belongs to (N2CMcpToolsets): the tool profile serves a tool only when its toolset
	 * is enabled, and list_toolsets / describe_toolset / call_tool group tools by it. Sent to clients as
	 * _meta["nodetocode/toolset"]. Every tool of the registry must have one (NodeToCode.MCP.Toolsets checks it); only
	 * the tool-search meta-tools and spec probes have none.
	 */
	const FN2CMcpToolset* Toolset = nullptr;

	/** JSON Schema defining the tool's input parameters */
	TSharedPtr<FJsonObject> InputSchema;

	/**
	 * Optional JSON Schema of the result's structuredContent (Tool.outputSchema). A tool that sets it must return
	 * structuredContent matching it in every successful result.
	 */
	TSharedPtr<FJsonObject> OutputSchema;

	/** Behaviour hints (set them with FN2CMcpToolBase::SetAnnotations) */
	FMcpToolAnnotations Annotations;

	/** Optional _meta["anthropic/maxResultSizeChars"]: a client may keep text results up to this size */
	TOptional<int32> MaxResultSizeChars;

	/** Sends _meta["anthropic/alwaysLoad"] = true: an entry-point tool a client keeps loaded when it defers tools */
	bool bAlwaysLoad = false;

	/** Further _meta entries, sent as given (a key set here is replaced by the fields above) */
	TSharedPtr<FJsonObject> Meta;

	/** Whether this tool is long-running and should use async execution */
	UPROPERTY()
	bool bIsLongRunning = false;

	/**
	 * Whether the tool's results carry text taken from the project's Blueprints (comments, titles, names, tooltips,
	 * descriptions, string literals). FN2CMcpToolManager::ExecuteTool marks such a successful result as untrusted
	 * data (FN2CMcpUntrustedContent, card 0124); a long-running tool marks its own result. Not sent to clients.
	 */
	bool bReturnsUntrustedContent = false;

	FMcpToolDefinition()
	{
	}

	/** A definition with no toolset: the tool-search meta-tools and spec probes; a registered tool uses the other */
	explicit FMcpToolDefinition(const FString& InName, const FString& InDescription = TEXT(""))
		: Name(InName)
		, Description(InDescription)
	{
	}

	FMcpToolDefinition(const FString& InName, const FString& InDescription, const FN2CMcpToolset& InToolset)
		: Name(InName)
		, Description(InDescription)
		, Toolset(&InToolset)
	{
	}

	/** The _meta object tools/list sends: Meta plus the toolset and the anthropic/* entries; null when empty */
	TSharedPtr<FJsonObject> BuildMeta() const;

	/** Convert to JSON object for MCP protocol (one tools/list entry) */
	TSharedPtr<FJsonObject> ToJson() const;

	/** Create from JSON object (a tools/list entry; _meta["nodetocode/toolset"] is resolved to the declared toolset) */
	static FMcpToolDefinition FromJson(const TSharedPtr<FJsonObject>& JsonObject);
};

/**
 * MCP Tool Call Result
 * Represents the result of invoking an MCP tool
 */
USTRUCT()
struct NODETOCODE_API FMcpToolCallResult
{
	GENERATED_BODY()

	/** Array of content items (for now, just text content) */
	TArray<TSharedPtr<FJsonObject>> Content;

	/**
	 * Optional structuredContent (CallToolResult): the result as a JSON object, matching the tool's OutputSchema when
	 * it has one. Clients that read it see the same data as the text content.
	 */
	TSharedPtr<FJsonObject> StructuredContent;

	/** Whether this result indicates an error */
	UPROPERTY()
	bool bIsError = false;

	FMcpToolCallResult()
	{
	}

	/**
	 * Create a result with text content. bInIsError marks a tool execution failure whose body is still structured
	 * (for example JSON with success:false and the error), so clients that branch on isError see the failure.
	 */
	static FMcpToolCallResult CreateTextResult(const FString& Text, bool bInIsError = false);

	/** Create an error result with error message */
	static FMcpToolCallResult CreateErrorResult(const FString& ErrorMessage);

	/** Convert to JSON object for MCP protocol */
	TSharedPtr<FJsonObject> ToJson() const;
};

/**
 * Delegate type for MCP tool handlers
 */
DECLARE_DELEGATE_RetVal_OneParam(FMcpToolCallResult, FMcpToolHandlerDelegate, const TSharedPtr<FJsonObject>& /* Args */);
