On UE 5.8, Epic's **MCP Client Toolset** plugin can mount NodeToCode's MCP server into Epic's toolset registry, so
NodeToCode's tools show up next to Epic's own toolsets for anything that reads the registry: Epic's **Unreal MCP**
server (`list_toolsets`, `describe_toolset`, `call_tool`) and Epic's in-editor assistant. It needs one settings
entry and no code. The plugins are experimental, and the limits below come from how Epic's client behaves.

Checked on UE 5.8 with NodeToCode 2.0 (dev-2.0) in a headless editor, through Unreal MCP's HTTP endpoint. Epic's
in-editor assistant and the **Editor Preferences** screen itself were not part of that check.

## 1. Enable the plugins

In **Edit → Plugins**, enable **MCP Client Toolset** (it brings **Toolset Registry**) and, to reach the registry from
an outside MCP client such as Claude Code, **Unreal MCP**. Restart the editor.

## 2. Set up NodeToCode's server

In **Project Settings → Plugins → Node to Code → MCP Server**:

| Setting | Use | Why |
|---|---|---|
| **Enable Python Script-Only Mode** | off (default) | On, only the Python tools are listed: no translation or Blueprint tools |
| **Enable Dynamic Tool Discovery** | off (default) | On, only `assess-needed-tools` is listed, and Epic's client never lists tools again |
| **Server Port** | 27000, or a port only this project uses | The entry below names one fixed URL |
| **Port Fallback Range** | 1 | With more, a taken port moves the server and the entry points at the wrong server (or at another editor's NodeToCode) |

**Enable MCP Server** (**Project Settings → Plugins → Node to Code (User)**) must stay on. `run-python` and the
script save and delete tools are listed only when **Allow MCP Python Execution** is on there.

## 3. Add the entry

In **Editor Preferences → Plugins → MCP Toolset Servers**, add one element to **MCP Servers**:

| Field | Value |
|---|---|
| **Name** | `NodeToCode` (the toolset name; tools are named `NodeToCode.<tool>`) |
| **Description** | `Blueprint translation, graph editing and Python tools for the open Unreal editor` |
| **Server Url** | `http://127.0.0.1:27000/mcp` (your **Server Port**; `localhost` also works, other host names get 403) |
| **Api Key** | empty |
| **Enabled** | on |
| **Transport** | **Streamable HTTP** (the default, **Legacy SSE**, cannot connect) |
| **Auth** | **None** (the default is **Bearer Token**) |

The setting is per user and per project. The same entry as a line in
`<Project>/Saved/Config/WindowsEditor/EditorPerProjectUserSettings.ini` (checked; write `MCPServers=`, because a
`+MCPServers=` line in that file left the list empty):

```ini
[/Script/MCPClientToolset.MCPToolsetSettings]
MCPServers=(Name="NodeToCode",Description="Blueprint translation, graph editing and Python tools for the open Unreal editor",ServerUrl="http://127.0.0.1:27000/mcp",ApiKey="",bEnabled=True,Transport=StreamableHTTP,Auth=None,OAuthClientId="",OAuthScope="")
```

Restart the editor. The Output Log shows `MCPClientToolset 'NodeToCode': registered 62 tool(s)` (59 without
**Allow MCP Python Execution**) and `MCPClientToolset: registered 'NodeToCode'`. NodeToCode's log shows a session for
client `UE-ToolsetRegistry` at protocol `2024-11-05`, which NodeToCode accepts.

## 4. Call the tools through Unreal MCP

Unreal MCP's own settings are in **Editor Preferences → Plugins → Model Context Protocol**: turn on **Auto Start
Server**, and note **Server Port Number** (8000 by default). With **Enable Tool Search** on (the default) its
`tools/list` has three tools, and NodeToCode's tools are reached through them:

```json
{"name": "list_toolsets", "arguments": {}}
{"name": "describe_toolset", "arguments": {"toolset_name": "NodeToCode"}}
{"name": "call_tool", "arguments": {"toolset_name": "NodeToCode", "tool_name": "get-available-translation-targets", "arguments": {}}}
{"name": "call_tool", "arguments": {"toolset_name": "NodeToCode", "tool_name": "translate-focused-blueprint",
  "arguments": {"provider": "anthropic", "model": "claude-haiku-5-5", "language": "cpp"}}}
```

`list_toolsets` shows `- NodeToCode: <Description>`; `describe_toolset` lists every tool as `NodeToCode.<tool>`.
With **Enable Tool Search** off, Unreal MCP's `tools/list` carries each tool as `NodeToCode.<tool>` instead; those
names contain a dot, which some clients do not accept in a tool name.

A call's text is NodeToCode's text wrapped once more: `{"text": "<NodeToCode's result>"}`. A translation needs a
focused Blueprint editor and a provider key, as with any other client
(see [Using a Claude or Gemini Subscription](Using-a-Claude-or-Gemini-Subscription) for keys).

## Limits

| Limit | What you see | What to do |
|---|---|---|
| One connection attempt, at editor start | If NodeToCode's server was off or elsewhere then, the log says `MCPClientToolset: failed to connect` and the toolset is missing until the next start. NodeToCode starts its server in the same startup phase; in our checks the connection succeeded on every start | Restart the editor |
| Tool list fixed at connect | Tools added later (settings changes, dynamic discovery) never appear | Keep dynamic discovery off; restart the editor after a NodeToCode tool setting change |
| No new session after a server restart | Changing **Server Port** restarts NodeToCode's server; every later call fails with `tools/call failed: no response received` | Restart the editor |
| Errors arrive as plain text | A NodeToCode tool error (`isError`) comes back as an ordinary result whose text holds the error, for example `"No active Blueprint Editor."`; a protocol error keeps only its message | Read the text, not only the error flag |
| Text only | Only text content is passed on; anything else in a result is dropped | |
| About 30 s per call | Epic's client sends no progress token, so NodeToCode answers once, when the call ends, and the engine's HTTP activity timeout (`[HTTP] HttpActivityTimeout`, 30 s by default) ends a quiet call with `tools/call failed: no response received`. NodeToCode still finishes that translation (the provider is billed) and logs `socket_send_failure` when it answers. A small Blueprint with `claude-haiku-5-5` took 8 to 12 s | Translate small graphs, or raise `HttpActivityTimeout` under `[HTTP]` in the project's `Config/DefaultEngine.ini` (all engine HTTP requests use it; avoid 3600, which NodeToCode removes as a 1.2.x leftover) |
