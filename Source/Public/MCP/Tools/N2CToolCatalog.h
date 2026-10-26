// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "MCP/Tools/N2CToolCallContext.h"

class IN2CMcpTool;
class FN2CMcpToolManager;
struct FN2CMcpToolset;

/** How a tool runs once Invoke has started it */
enum class EN2CToolExecutionPolicy : uint8
{
	/** Runs to completion inside Invoke: the future is already set when Invoke returns */
	Immediate,

	/**
	 * Starts inside Invoke and finishes later (an LLM request). It reports progress and watches for cancellation
	 * through the call context; a transport answers it when the future is set.
	 */
	LongRunning,
};

/**
 * One tool as any caller sees it, whatever the transport (card 0339): what it is called, what it takes and gives back,
 * how it behaves and how it runs. FN2CToolCatalog::Describe returns these; an MCP transport turns one into a tools/list
 * entry with ToMcpDefinition().
 */
struct NODETOCODE_API FN2CToolDescriptor
{
	/** Unique name (kebab-case for NodeToCode's tools) */
	FString Name;

	/** Human-readable name; empty when the tool has none (clients fall back to Name) */
	FString Title;

	/** What the tool does, for a model that sees nothing else of it */
	FString Description;

	/** The declared toolset the tool belongs to; null only for the tool-search meta-tools */
	const FN2CMcpToolset* Toolset = nullptr;

	/** JSON Schema of the arguments (an object schema) */
	TSharedPtr<FJsonObject> InputSchema;

	/** JSON Schema of a successful outcome's value, when the tool declares one */
	TSharedPtr<FJsonObject> OutputSchema;

	/** Behaviour hints: read-only, destructive, idempotent, open world (unset: unknown) */
	FMcpToolAnnotations Hints;

	/** Whether Invoke finishes the call at once or later */
	EN2CToolExecutionPolicy ExecutionPolicy = EN2CToolExecutionPolicy::Immediate;

	/** The tool's successful results carry Blueprint text, which Invoke marks as untrusted data (card 0124) */
	bool bReturnsUntrustedContent = false;

	/** The tool keeps working during Play In Editor although it is not read-only (card 0170) */
	bool bAllowedDuringPlaySession = false;

	/**
	 * The tool definition this descriptor was made from (cached when the tool was registered). It holds what only MCP
	 * sends (_meta entries, anthropic/maxResultSizeChars, anthropic/alwaysLoad); ToMcpDefinition starts from it.
	 */
	TSharedPtr<const FMcpToolDefinition> Source;

	/** The descriptor of a tool definition */
	static FN2CToolDescriptor FromDefinition(const TSharedPtr<const FMcpToolDefinition>& Definition);

	/** The MCP Tool this descriptor is served as: Source with the descriptor's fields applied */
	FMcpToolDefinition ToMcpDefinition() const;

	/** The names InputSchema's "required" lists (empty when it lists none) */
	TArray<FString> GetRequiredArguments() const;

	/** True when the tool says it only reads (Hints.IsReadOnly()) */
	bool IsReadOnly() const { return Hints.IsReadOnly(); }
};

/** Which tools Describe returns */
struct NODETOCODE_API FN2CToolFilter
{
	enum class EScope : uint8
	{
		/** What tools/list holds: every served tool, or the three meta-tools in tool-search exposure */
		Listed,

		/** Every served tool (what call_tool reaches), never the meta-tools */
		Served,
	};

	EScope Scope = EScope::Listed;

	/** Only the tools of this toolset (null: any) */
	const FN2CMcpToolset* Toolset = nullptr;

	/** Only the tools whose Hints say read-only */
	bool bReadOnlyOnly = false;

	/** What tools/list holds */
	static FN2CToolFilter Listed() { return FN2CToolFilter(); }

	/** Every served tool */
	static FN2CToolFilter Served()
	{
		FN2CToolFilter Filter;
		Filter.Scope = EScope::Served;
		return Filter;
	}
};

/** Why a call produced no value */
enum class EN2CToolErrorCode : uint8
{
	/** No tool of that name is served */
	UnknownTool,

	/** An argument the input schema requires is missing (FN2CToolError::Field names it) */
	InvalidArgument,

	/** The tool changes Blueprints, assets or levels and a Play In Editor session is running (PIE_ACTIVE) */
	PlaySessionActive,

	/** The caller cannot run the tool's execution policy (a long-running tool inside a JSON-RPC batch) */
	NotRunnableHere,

	/** The call was cancelled through its context */
	Cancelled,

	/** The tool ran and reported a failure */
	ToolFailed,

	/** The tool threw or returned no result */
	Internal,
};

/** A typed error of a tool call */
struct NODETOCODE_API FN2CToolError
{
	EN2CToolErrorCode Code = EN2CToolErrorCode::ToolFailed;

	/** The error text the caller shows (for MCP, the isError result's text) */
	FString Message;

	/** The argument the error is about (InvalidArgument), else empty */
	FString Field;

	/** The code's stable name: "UNKNOWN_TOOL", "INVALID_ARGUMENT", ... */
	static const TCHAR* CodeName(EN2CToolErrorCode Code);
};

/**
 * The outcome of one tool call: a JSON value or a typed error, never MCP content. ToValueString gives it as one plain
 * string, the form UE 5.8's ToolsetRegistry wraps with MakeTextResult.
 *
 * Today's IN2CMcpTool classes return an FMcpToolCallResult; the catalog's legacy shim reads the value or error from it
 * (FromCallToolResult) and keeps the result itself, so the MCP transport sends exactly what the tool built (the
 * untrusted-data marking, a compile item, structuredContent) while every other caller reads the value. A tool that
 * moves to the catalog natively returns an outcome without one, and ToCallToolResult builds the MCP content then.
 */
struct NODETOCODE_API FN2CToolOutcome
{
	/** The value of a successful call (a string, an object, ...); null on an error */
	TSharedPtr<FJsonValue> Value;

	/** Set when the call failed */
	TOptional<FN2CToolError> Error;

	bool IsError() const { return Error.IsSet(); }

	/** The outcome as one plain string: a string value as it is, any other value as compact JSON, an error's message */
	FString ToValueString() const;

	static FN2CToolOutcome MakeValue(const TSharedPtr<FJsonValue>& InValue);
	static FN2CToolOutcome MakeError(EN2CToolErrorCode Code, const FString& Message, const FString& Field = FString());

	/**
	 * The legacy shim: the outcome of a tool's FMcpToolCallResult. Its structuredContent, else its text (parsed when it
	 * is one JSON object or array) is the value; an isError result is an error with ErrorCode and its text.
	 */
	static FN2CToolOutcome FromCallToolResult(const FMcpToolCallResult& Result, EN2CToolErrorCode ErrorCode = EN2CToolErrorCode::ToolFailed);

	/**
	 * The MCP adapter: the CallToolResult a transport sends. The legacy shim's result unchanged when there is one; else
	 * an error's message as an isError text result, or the value as text (an object also as structuredContent).
	 */
	FMcpToolCallResult ToCallToolResult() const;

private:
	/** The legacy tool's own result (FromCallToolResult), sent as it is by ToCallToolResult */
	TSharedPtr<const FMcpToolCallResult> LegacyResult;
};

/**
 * The tool catalog (card 0339): the one transport-neutral seam every caller of NodeToCode's tools goes through.
 * Describe says what is served, Invoke runs a call. Behind Invoke, in one place: the Play In Editor rule (card 0170),
 * the check of required arguments against the input schema, the untrusted-data marking (card 0124), the execution
 * policy and error normalization. The MCP transport (tools/list, tools/call, the long-running call) and call_tool use
 * it; a 5.8 ToolsetRegistry or Python adapter would use the same two calls.
 *
 * Every registered IN2CMcpTool is reached through the legacy shim (FN2CToolOutcome::FromCallToolResult), with no
 * per-tool change. Which tools are served (the toolsets of the tool profile, tool-search exposure, the tools left to
 * Unreal MCP) is FN2CMcpToolManager's single Configure filter; the catalog reads that active set and never holds the
 * manager's lock while a tool runs. Tool-list changes go out through FN2CMcpToolManager::OnToolListChanged, which the
 * protocol core subscribes to; neither the catalog nor the manager knows a transport.
 */
class NODETOCODE_API FN2CToolCatalog
{
public:
	/** A catalog over the active set of Tools (specs build their own manager) */
	explicit FN2CToolCatalog(FN2CMcpToolManager& InTools);

	/** The catalog over the server's tool manager (FN2CMcpToolManager::Get) */
	static FN2CToolCatalog& Get();

	/** The tools Filter selects, sorted by name (ordinal), so the order is the same on every call */
	TArray<FN2CToolDescriptor> Describe(const FN2CToolFilter& Filter = FN2CToolFilter()) const;

	/** The descriptor of a served tool or meta-tool, found by name in O(1); unset when no such tool is served */
	TOptional<FN2CToolDescriptor> Find(const FString& Name) const;

	/** True when tools/list holds a tool of that name: the only names a client may pass to tools/call */
	bool IsListed(const FString& Name) const;

	/**
	 * Runs a served tool or meta-tool. An Immediate tool runs before this returns and the future is already set; a
	 * LongRunning one is started (on the game thread, where it must be called) and sets the future later.
	 * @param Context The call's progress sink and cancellation token
	 * @return The outcome: a value, or a typed error (UnknownTool, InvalidArgument naming the missing argument, ...)
	 */
	TFuture<FN2CToolOutcome> Invoke(const FString& Name, const TSharedPtr<FJsonObject>& Arguments, const FN2CToolCallContextRef& Context) const;

	/**
	 * Invoke for a caller that must have the outcome now (tools/call inside a JSON-RPC batch, call_tool, specs): an
	 * Immediate tool runs; a LongRunning one is refused with NotRunnableHere instead of being started.
	 */
	FN2CToolOutcome InvokeNow(const FString& Name, const TSharedPtr<FJsonObject>& Arguments) const;

	/**
	 * The call pipeline Invoke runs once it has found the tool: the Play In Editor rule, the argument check, then the
	 * tool by Descriptor's execution policy. FN2CToolAsyncTaskManager runs a call's tool instance through it.
	 */
	static TFuture<FN2CToolOutcome> InvokeTool(const TSharedRef<IN2CMcpTool>& Tool, const FN2CToolDescriptor& Descriptor,
		const TSharedPtr<FJsonObject>& Arguments, const FN2CToolCallContextRef& Context);

	/**
	 * Checks Arguments against the input schema's "required" list: an argument that is absent or null is missing.
	 * @return the InvalidArgument error naming the first missing argument, or unset when none is missing
	 */
	static TOptional<FN2CToolError> CheckArguments(const FN2CToolDescriptor& Descriptor, const TSharedPtr<FJsonObject>& Arguments);

private:
	FN2CMcpToolManager& Tools;
};
