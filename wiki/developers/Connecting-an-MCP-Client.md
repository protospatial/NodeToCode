While your project is open in the Unreal editor, NodeToCode runs an MCP server at **`http://127.0.0.1:27000/mcp`**
(Streamable HTTP, one endpoint, no API key or auth header unless you turn on
[Requiring an MCP Token](Requiring-an-MCP-Token)). Point your MCP client at that URL, or, for a client
that only starts local commands, at NodeToCode's stdio bridge. This page gives the setup for each client; which
tools the client then sees is set in Project Settings, see [Choosing a Tool Mode](Choosing-a-Tool-Mode).

## Before you connect

| Check | Where |
|---|---|
| The editor is open | The server runs only while the editor runs, and stops when it closes |
| **Enable MCP Server** is on (the default) | **Project Settings → Plugins → Node to Code (User) → MCP Server**; per user, needs an editor restart |
| The URL | **Server Status** in **Project Settings → Plugins → Node to Code → MCP Server** shows where the server listens. **Server Port** is 27000; when that port is taken (a second editor, say) the server takes the next free one in **Port Fallback Range** (10) and shows a notification |
| The client runs on this computer | The server answers only on `127.0.0.1` and `localhost`. A request whose `Host` header, or `Origin` header when it sends one, is not `localhost`, `127.0.0.1` or `[::1]` gets 403, so a web page cannot call it and a remote machine cannot reach it |

The client sends the protocol headers itself (`Accept: application/json, text/event-stream`, then the
`Mcp-Session-Id` from `initialize`); NodeToCode accepts MCP versions 2025-11-25, 2025-06-18, 2025-03-26 and 2024-11-05.

**Tool mode and what a client lists.** The tool mode is the server's, so every client below gets the same list. The
default (**Tool Profile** Default toolsets, **Tool Exposure** List every tool) lists 64 tools, such as
`get-focused-blueprint`, `get-blueprint-n2cjson`, `translate-focused-blueprint` and `compile-blueprint`; 67 with
**Allow MCP Python Execution** on; 30 on UE 5.8 with Epic's Unreal MCP and EditorToolset plugins enabled
([NodeToCode and Epic Tool Mapping](NodeToCode-and-Epic-Tool-Mapping)). To change it, set the mode in **Project
Settings → Plugins → Node to Code → MCP Server** ([Choosing a Tool Mode](Choosing-a-Tool-Mode)), restart the editor
and reconnect the client.

## Claude Code

Easiest: install NodeToCode's [Claude Code Plugin](Claude-Code-Plugin), which adds the server, the skills and a
check at session start. By hand, from your project's folder:

```
claude mcp add --transport http nodetocode http://127.0.0.1:27000/mcp
claude mcp list
```

`claude mcp list` shows `nodetocode: http://127.0.0.1:27000/mcp (HTTP) - ✔ Connected`, and `/mcp` in a session lists
its tools. Add `--scope user` to use it in every folder. The same server as a JSON file, for
`claude --mcp-config nodetocode.json` or a project's `.mcp.json`:

```json
{ "mcpServers": { "nodetocode": { "type": "http", "url": "http://127.0.0.1:27000/mcp" } } }
```

## Cursor

In `.cursor/mcp.json` in your project (or `~/.cursor/mcp.json` for every project):

```json
{ "mcpServers": { "nodetocode": { "url": "http://127.0.0.1:27000/mcp" } } }
```

Turn the server on in Cursor's MCP settings. Connection errors are in the Output panel under **MCP Logs**.

## VS Code (GitHub Copilot)

Run **MCP: Add Server** from the Command Palette, choose **HTTP**, and enter the URL; or put this in
`.vscode/mcp.json` (VS Code uses `servers`, not `mcpServers`):

```json
{ "servers": { "nodetocode": { "type": "http", "url": "http://127.0.0.1:27000/mcp" } } }
```

Check the tools under **Configure Tools** in the chat input. If the chat warns that a request has too many tools,
switch NodeToCode to **Tool search** ([Choosing a Tool Mode](Choosing-a-Tool-Mode)), which lists 3.

## Codex

`codex mcp add nodetocode --url http://127.0.0.1:27000/mcp`, or in `~/.codex/config.toml`:

```toml
[mcp_servers.nodetocode]
url = "http://127.0.0.1:27000/mcp"
```

`codex mcp list` lists `nodetocode`, and `/mcp` in the Codex TUI shows it among the active servers.

## Claude Desktop (stdio bridge)

Claude Desktop starts local servers as commands, so use the bridge in the plugin's `Content/Python/mcp_bridge`
folder. In **Settings → Developer → Edit Config** (`%APPDATA%\Claude\claude_desktop_config.json`), with your paths:

```json
{ "mcpServers": { "nodetocode": {
  "command": "C:\\MyProject\\Plugins\\NodeToCode\\Content\\Python\\mcp_bridge\\launch_bridge.bat",
  "args": ["--project", "C:\\MyProject\\MyProject.uproject"] } } }
```

Restart Claude Desktop; the tools appear under the tools menu in a chat. The launcher runs the bridge with the
Python of your newest Unreal Engine install, or Python 3 from your PATH (`launch_bridge.sh` on macOS and Linux). The
bridge finds the editor through the file the server writes, so it follows a port change and can start before the
editor; `--project` picks one editor when several are open, and `--url http://127.0.0.1:27000/mcp` names one
directly. Any other stdio-only client takes the same command and arguments.

## Epic's MCP Client Toolset (UE 5.8)

On UE 5.8, Epic's **MCP Client Toolset** plugin can mount NodeToCode's server into Epic's toolset registry, so Unreal
MCP and Epic's in-editor assistant see NodeToCode's tools as `NodeToCode.<tool>`. The entry goes in **Editor
Preferences → Plugins → MCP Toolset Servers** with **Transport** set to **Streamable HTTP** and **Auth** to **None**;
keep **Tool Exposure** at **List every tool** and **Port Fallback Range** at 1. After a restart the Output Log
shows `MCPClientToolset 'NodeToCode': registered 64 tool(s)` (30 with EditorToolset). Steps and limits:
[Using NodeToCode with Unreal MCP](Using-NodeToCode-with-Unreal-MCP).

## Which of these were checked

Claude Code 2.1.300 on UE 5.8: the `claude mcp add` form, the JSON file and the bridge (both the launcher and
`python nodetocode_bridge.py --project ...`) connected and listed the counts above. The Cursor, VS Code, Codex and
Claude Desktop snippets follow each vendor's documentation and were not run against NodeToCode when this page was
written.

## If the client does not connect

| What you see | What to do |
|---|---|
| Failed to connect, or the client sees another project | Open the editor first, check **Enable MCP Server**, and use the URL in **Server Status** |
| 403 | Use `127.0.0.1` or `localhost` in the URL; other host names and web page origins are refused |
| 401 `Unauthorized` | **Require MCP Bearer Token** is on: send the token ([Requiring an MCP Token](Requiring-an-MCP-Token)) |
| `Request body too large` (413) | The server takes requests up to 8 MiB; split the work into smaller tool calls |
| `Session not found` (404) after the editor restarted | A restarted editor forgets its sessions: reconnect the client (Claude Code: `/mcp`, **Reconnect**) |
| Fewer tools than expected | See [Choosing a Tool Mode](Choosing-a-Tool-Mode); a tool mode change needs an editor restart and a reconnect |
| A tool answers `PIE_ACTIVE` | Stop Play In Editor; editing tools wait until it ends |
| You need to see what the client sent | The Output Log (`LogNodeToCode`) has one `MCP request:` line per request with its method, tool, status, `isError` and duration; see [Logging Configuration](Logging-Configuration#mcp-requests-in-the-log) |
