// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "MCP/Tools/N2CToolCallContext.h"

/**
 * Interface for MCP tools
 * All MCP tools must implement this interface to be registered with the tool system.
 *
 * A quick tool overrides Execute(). A long-running tool (FMcpToolDefinition::bIsLongRunning) overrides InvokeAsync()
 * instead: it is started on the game thread, must not block, reports progress and watches for cancellation through
 * its FN2CToolCallContext, and fulfils the returned future exactly once (on the game thread) with its result or a
 * cancelled result.
 */
class NODETOCODE_API IN2CMcpTool
{
public:
	virtual ~IN2CMcpTool() = default;

	/**
	 * Get the tool definition containing metadata (name, description, schema)
	 * @return The tool definition
	 */
	virtual FMcpToolDefinition GetDefinition() const = 0;

	/**
	 * Execute the tool with given arguments and return its result. The default is an error: a tool overrides this, or
	 * InvokeAsync when it is long-running.
	 * @param Arguments The JSON arguments passed to the tool
	 * @return The result of the tool execution
	 */
	virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments)
	{
		return FMcpToolCallResult::CreateErrorResult(FString::Printf(TEXT("Tool '%s' cannot run synchronously"), *GetDefinition().Name));
	}

	/**
	 * Starts the tool for one call and returns its result as a future. The MCP server calls this, on the game thread,
	 * for a long-running tool's tools/call. The default runs Execute() and returns its result already set.
	 * @param Context The call's progress channel and cancellation signal
	 * @param Arguments The JSON arguments passed to the tool
	 * @return The call's result, set exactly once
	 */
	virtual TFuture<FMcpToolCallResult> InvokeAsync(const FN2CToolCallContextRef& Context, const TSharedPtr<FJsonObject>& Arguments)
	{
		return MakeFulfilledPromise<FMcpToolCallResult>(Execute(Arguments)).GetFuture();
	}

	/**
	 * Check if tool requires Game Thread execution
	 * Tools that interact with Unreal Editor APIs should return true
	 * @return true if the tool must run on the Game Thread
	 */
	virtual bool RequiresGameThread() const { return false; }
};
