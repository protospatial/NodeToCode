By default any program running as you on this computer can call NodeToCode's MCP server. With **Require MCP Bearer
Token** on, the server answers every `/mcp` request that lacks `Authorization: Bearer <token>` with 401, so a program
that can reach the port but cannot read your files cannot call tools. This page covers turning it on, where the token
is, and the header setting for each client.

## Turning it on

1. **Editor Preferences → Plugins → Node to Code (User) → MCP Server → Require MCP Bearer Token**. It is a per-user
   switch: only your own value counts, and a value committed in the project's config is ignored.
2. Restart the editor. **Server Status** then ends in `, bearer token required`, and the Output Log names the token
   file.

It is off by default in 2.0-beta, and turning it off brings back the previous behaviour.

## The token

| What | Where |
|---|---|
| The token file | `%LOCALAPPDATA%\NodeToCode\mcp-token` on Windows, `~/Library/Application Support/Epic/NodeToCode/mcp-token` on macOS, `~/.config/Epic/NodeToCode/mcp-token` on Linux |
| What it holds | 64 hex characters from the operating system's secure random generator, made the first time it is needed |
| Who shares it | Every editor you run uses the same file, so one token works for all your projects |
| Copy it | Console command `N2C.Mcp.CopyToken` (**Window → Output Log**, the **Cmd** box) copies it to the clipboard |
| Replace it | `N2C.Mcp.RegenerateToken` writes a new one, and the old token is refused at once |

The file is outside every project, and NodeToCode's file tools refuse to read that folder, so an agent cannot read the
token through MCP. `/mcp/health` stays open without the token and never shows it.

## Client settings

The [stdio bridge](Connecting-an-MCP-Client#claude-desktop-stdio-bridge) (Claude Desktop and other stdio-only
clients) reads the token file itself on every request, so it needs no change and follows a regenerated token. It
also takes `--token-file <path>`, or the token in the `NODETOCODE_MCP_TOKEN` environment variable.

An HTTP client sends the header. Put the token in the `NODETOCODE_MCP_TOKEN` environment variable of the client (or
paste it in place of the variable), next to the URL from [Connecting an MCP Client](Connecting-an-MCP-Client).
With the setting on, `N2C.Mcp.WriteClientConfig` writes these settings into the project's client files for you
([Let NodeToCode write the config files](Connecting-an-MCP-Client#let-nodetocode-write-the-config-files)), always
as the variable, never the token:

| Client | Setting |
|---|---|
| Claude Code (`.mcp.json` or `--mcp-config`) | `"headers": { "Authorization": "Bearer ${NODETOCODE_MCP_TOKEN}" }` beside `"type": "http"` and `"url"` |
| Claude Code (command line) | `claude mcp add --transport http nodetocode http://127.0.0.1:27000/mcp --header "Authorization: Bearer <token>"` |
| Codex (`~/.codex/config.toml`) | `bearer_token_env_var = "NODETOCODE_MCP_TOKEN"` under `[mcp_servers.nodetocode]` |
| VS Code (`.vscode/mcp.json`) | `"headers": { "Authorization": "Bearer ${env:NODETOCODE_MCP_TOKEN}" }` beside `"type"` and `"url"` |
| Cursor (`mcp.json`) | `"headers": { "Authorization": "Bearer ${env:NODETOCODE_MCP_TOKEN}" }` beside `"url"` |
| Gemini CLI (`settings.json`) | `"httpUrl": "http://127.0.0.1:27000/mcp", "headers": { "Authorization": "Bearer $NODETOCODE_MCP_TOKEN" }` |
| Epic's MCP Client Toolset (UE 5.8) | **Auth**: **Bearer Token (API Key)**, with the token in **Api Key** |

The whole Claude Code file:

```json
{ "mcpServers": { "nodetocode": { "type": "http", "url": "http://127.0.0.1:27000/mcp",
  "headers": { "Authorization": "Bearer ${NODETOCODE_MCP_TOKEN}" } } } }
```

Checked: the Claude Code file above (Claude Code CLI on UE 5.8) and the stdio bridge. The other rows follow each
vendor's documentation and were not run against NodeToCode.

## If a client gets 401

| What you see | What to do |
|---|---|
| `Unauthorized: this NodeToCode MCP server requires 'Authorization: Bearer <token>' (no Authorization header)` | Add the header for your client (above), or turn the setting off |
| The same with `wrong or malformed bearer token` | The token was regenerated, or the variable holds an old one: copy it again with `N2C.Mcp.CopyToken` |
| The bridge logs `the bridge has none` | The token file is missing: start the editor with the setting on once, or pass `--token-file` |
