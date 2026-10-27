// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"

/** The MCP clients whose config file N2C.Mcp.WriteClientConfig writes or merges */
enum class EN2CMcpClient : uint8
{
	ClaudeCode,
	Cursor,
	VSCode,
	Gemini,
	Codex,
	Num
};

/** What writing one client's config did */
struct FN2CMcpClientConfigResult
{
	EN2CMcpClient Client = EN2CMcpClient::ClaudeCode;
	bool bOk = false;
	/** True when the file already held exactly this content (nothing was written) */
	bool bUnchanged = false;
	FString FilePath;
	/** Why it failed, empty on success */
	FString Error;
};

/**
 * Writes MCP client configs that point at NodeToCode's server (card 0206), as Epic's ModelContextProtocol plugin does
 * on UE 5.8 (ModelContextProtocolClientConfig.cpp), but merging instead of overwriting:
 *
 *   Claude Code  .mcp.json              mcpServers.nodetocode  { type: http, url, headers? }
 *   Cursor       .cursor/mcp.json       mcpServers.nodetocode  { url, headers? }
 *   VS Code      .vscode/mcp.json       servers.nodetocode     { type: http, url, headers? }
 *   Gemini CLI   .gemini/settings.json  mcpServers.nodetocode  { httpUrl, headers? }
 *   Codex        .codex/config.toml     [mcp_servers.nodetocode] url, bearer_token_env_var?
 *
 * Every other key and server in the file is kept, the nodetocode entry is replaced, and running it again changes
 * nothing. A file that is not valid JSON (or a Codex file that defines nodetocode in a form this cannot edit safely)
 * is left alone and reported. With the bearer token on, the entry sends it through the NODETOCODE_MCP_TOKEN
 * environment variable in each client's own reference syntax: the token itself is never written to a config file,
 * so a file that git tracks never holds it. Claude Desktop (stdio only) gets a snippet that runs launch_bridge.
 */
class FN2CMcpClientConfig
{
public:
	/** The server entry's name in every client file */
	static const TCHAR* const ServerName;

	/** The environment variable the generated headers read the bearer token from */
	static const TCHAR* const TokenEnvVar;

	/** The client's short name for the console command (claude, cursor, vscode, gemini, codex) */
	static const TCHAR* GetClientName(EN2CMcpClient Client);

	/** The client a console argument names (case-insensitive; also claudecode, code, gemini-cli), else unset */
	static TOptional<EN2CMcpClient> ParseClient(const FString& Name);

	/** The client's config file, relative to the folder it is written in (".mcp.json", ".codex/config.toml", ...) */
	static FString GetRelativePath(EN2CMcpClient Client);

	/** http://127.0.0.1:<Port>/mcp */
	static FString MakeServerUrl(int32 Port);

	/** The port the running server is bound to, else the Server Port setting */
	static int32 GetCurrentPort();

	/** The project folder, where the command writes by default */
	static FString GetDefaultBaseDir();

	/** Writes or merges Client's file under BaseDir; bWithToken adds the header that reads TokenEnvVar */
	static FN2CMcpClientConfigResult Write(EN2CMcpClient Client, const FString& BaseDir, const FString& ServerUrl, bool bWithToken);

	/**
	 * Merges the nodetocode entry into a JSON client file's text (empty Existing: a new file). Returns false, with
	 * OutError, when Existing is not a JSON object or its servers key is not an object; Out is then unset.
	 */
	static bool MergeJson(EN2CMcpClient Client, const FString& Existing, const FString& ServerUrl, bool bWithToken, FString& Out, FString& OutError);

	/**
	 * Merges [mcp_servers.nodetocode] into a Codex config.toml's text: the table and its sub-tables are replaced where
	 * they stand (or the table is appended), every other line is kept as it is. Returns false, with OutError, when the
	 * file defines nodetocode as an inline table or dotted keys, which this cannot replace without a TOML parser.
	 */
	static bool MergeToml(const FString& Existing, const FString& ServerUrl, bool bWithToken, FString& Out, FString& OutError);

	/** A claude_desktop_config.json snippet that runs LauncherPath --project UProjectPath through the stdio bridge */
	static FString MakeClaudeDesktopSnippet(const FString& LauncherPath, const FString& UProjectPath);

	/** This plugin's launch_bridge.bat (Windows) or launch_bridge.sh, as an absolute path */
	static FString GetBridgeLauncherPath();

	/**
	 * The console command: Args are client names (or "all", the default; "desktop" prints only the snippet),
	 * "dir=<folder>" (default: the project folder; it takes the rest of the line, so it comes last and may hold spaces), and "token" or "notoken" (default: Require MCP Bearer Token).
	 * Logs each file written and returns the results.
	 */
	static TArray<FN2CMcpClientConfigResult> RunCommand(const TArray<FString>& Args);
};
