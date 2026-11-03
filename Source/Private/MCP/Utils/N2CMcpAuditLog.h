// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/**
 * The audit trail of code-execution calls (card 0424, epic 0001): every run-python call appends one JSON line to
 * <Project>/Saved/NodeToCode/Logs/mcp-audit.jsonl with the time, the MCP session and client, the full code, the
 * outcome and the start of the result, so what an agent ran after a suspected prompt injection can be reviewed.
 *
 * The file can carry secrets (a script may print an environment variable or a key it read), so read-file and
 * read-path never open it (FN2CPathUtils::IsDeniedForAgentAccess); run-python itself can, like any file. At
 * MaxBytes it is renamed to mcp-audit.1.jsonl (one older file is kept) and a new one starts.
 *
 * Game thread only: the caller scope is set by the MCP protocol core around each inbound message, and the tool that
 * writes runs inside it.
 */
namespace N2CMcpAuditLog
{
	/** Who made the call being handled: the Mcp-Session-Id and the client's initialize clientInfo */
	struct FCaller
	{
		FString SessionId;
		FString ClientName;
		FString ClientVersion;
	};

	/** The caller of the message being handled; empty outside one (an in-editor call, a spec without a session) */
	FCaller CurrentCaller();

	/** Sets the current caller for its lifetime (the protocol core opens one per inbound message) */
	class FScopedCaller
	{
	public:
		FScopedCaller(const FString& SessionId, const TSharedPtr<FJsonObject>& ClientInfo);
		~FScopedCaller();

		FScopedCaller(const FScopedCaller&) = delete;
		FScopedCaller& operator=(const FScopedCaller&) = delete;

	private:
		FCaller Previous;
	};

	/** The result text kept in a line, at most this many characters (the rest is cut and marked) */
	constexpr int32 MaxResultChars = 4000;

	/** Size at which the file is rotated */
	constexpr int64 MaxBytes = 10 * 1024 * 1024;

	/** <Project>/Saved/NodeToCode/Logs/mcp-audit.jsonl, absolute (a spec may point it elsewhere) */
	FString FilePath();

	/**
	 * Appends one line: {time (UTC ISO 8601), session, client: {name, version}, tool, then Fields as given}. Fields
	 * should hold what the call was asked (for run-python the full code and the timeout) and what it gave (success,
	 * isError, result through TruncateResult). Never fails the call: a write that fails is logged.
	 */
	void Append(const FString& Tool, const TSharedRef<FJsonObject>& Fields);

	/** Text cut to MaxResultChars, with "... [N more characters]" after the cut */
	FString TruncateResult(const FString& Text);

#if WITH_DEV_AUTOMATION_TESTS
	/** Points FilePath at Path (empty: back to the default) */
	void SetFilePathForTests(const FString& Path);
#endif
}
