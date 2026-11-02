// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"

class FJsonObject;

/**
 * Runs Python for the MCP tools through nodetocode._mcp_runner (card 0299). One statement with no '.py' in it is sent
 * with EPythonCommandExecutionMode::ExecuteStatement and EPythonCommandFlags::Unattended (no modal dialog blocks the
 * call); the source travels base64-encoded and is compiled unchanged in a fresh namespace by the runner, which hands
 * its strict JSON answer to UN2CPythonBridge::SubmitMcpResult under a call id. See the runner's docstring.
 */
namespace N2CMcpPythonRunner
{
	struct FRun
	{
		/** The runner's answer {success, data, error, traceback, output, stderr}, or null when it gave none */
		TSharedPtr<FJsonObject> Answer;
		/** Why there is no answer: Python unavailable, or the statement itself failed (the command's result) */
		FString Error;
	};

	/** Runs Python source as run-python does. Game thread */
	FRun RunCode(const FString& Source);

	/** Evaluates a nodetocode call expression ("list_scripts(limit=20)"; n2c. is added) as the script tools do. Game thread */
	FRun RunCall(const FString& NodeToCodeCall);
}
