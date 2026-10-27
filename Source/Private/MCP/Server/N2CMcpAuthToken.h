// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "HttpResultCallback.h"

struct FHttpServerRequest;

/**
 * The MCP endpoint's optional bearer token (card 0183). With Require MCP Bearer Token on (UN2CUserSettings), a POST,
 * DELETE or other request to /mcp must carry "Authorization: Bearer <token>", so a local program that can reach the
 * port but cannot read the user's files cannot call tools. /mcp/health stays open and never shows the token.
 *
 * The token is 32 bytes from the operating system's CSPRNG, written as 64 lowercase hex characters to
 * FPlatformProcess::UserSettingsDir()/NodeToCode/mcp-token (%LOCALAPPDATA% on Windows, owner-only on macOS and Linux):
 * outside every project, so read-path and read-file cannot reach it (FN2CPathUtils::IsDeniedForAgentAccess), and shared
 * by every editor of the user, so the stdio bridge finds it with no configuration. It is created on first use and kept
 * until N2C.Mcp.RegenerateToken replaces it.
 */
class FN2CMcpAuthToken
{
public:
	/** Bytes of randomness in a token (64 hex characters) */
	static constexpr int32 TokenBytes = 32;

	/** Where the token lives: <UserSettingsDir>/NodeToCode/mcp-token, or the path a spec set */
	static FString GetTokenFilePath();

	/**
	 * The current token: the one in the token file when it holds a valid token, else a new one written there.
	 * Empty only when no token could be generated or saved (the error is logged; every request is then refused).
	 */
	static FString Get();

	/** Writes a new token to the token file (the old one stops working at once) and returns it; empty on failure */
	static FString Regenerate();

	/**
	 * Whether Authorization (the request's header value, or null when it has none) carries the current token as
	 * "Bearer <token>". The scheme is case-insensitive, the comparison takes the same time for every wrong token of the
	 * right length, and when it fails the token file is read again, so a token another editor regenerated counts at once.
	 */
	static bool IsAuthorized(const FString* Authorization);

	/**
	 * For the /mcp route: true when Request carries the current token; otherwise answers 401 with
	 * WWW-Authenticate: Bearer and a JSON-RPC error with a null id, and returns false.
	 */
	static bool Authorize(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	/** The token of an Authorization value "Bearer <token>" (scheme case-insensitive, spaces trimmed), else empty */
	static FString ParseBearer(const FString& Authorization);

	/** True when A and B are equal, in time that depends only on their lengths */
	static bool ConstantTimeEquals(const FString& A, const FString& B);

	/** True for 64 lowercase hex characters */
	static bool IsWellFormed(const FString& Token);

	/** Specs: keep the token in Path instead of the per-user file (empty restores it); drops the cached token */
	static void SetTokenFilePathForTesting(const FString& Path);

private:
	/** Reads the token file into Cached when it holds a valid token; call with Lock held */
	static bool LoadLocked();

	/** Writes a new token to the file and Cached; call with Lock held */
	static bool GenerateLocked();
};
