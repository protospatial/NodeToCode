NodeToCode ships a Claude Code plugin in its plugin folder. One install gives Claude Code the connection to
NodeToCode's MCP server in your open editor, the three [Agent Skills](Agent-Skills), a `/nodetocode:translate`
command and a check at the start of each session that tells you whether the editor answers. You no longer write the
MCP client config by hand.

It works with UE 5.6, 5.7 and 5.8 and needs NodeToCode's MCP server on (**Project Settings → Plugins → Node to Code
(User) → Enable MCP Server**, on by default).

## Install

From a terminal, with your path to the NodeToCode plugin folder (`<Project>/Plugins/NodeToCode`, or
`Engine/Plugins/Marketplace/NodeToCode` for an engine install from Fab):

```
claude plugin marketplace add "<Project>/Plugins/NodeToCode/Agents/claude-code"
claude plugin install nodetocode@nodetocode
```

Inside a Claude Code session, `/plugin marketplace add <same path>` and `/plugin install nodetocode@nodetocode` do
the same. Claude Code reads the plugin from that folder in place, so it always matches the NodeToCode version in your
project; after updating NodeToCode, start a new session.

If you added NodeToCode's server yourself before (`claude mcp add ... nodetocode ...`), remove it with
`claude mcp remove nodetocode`, so the tools are not listed twice.

## Check it

Start Claude Code in your Unreal project's folder with the editor open:

- the session starts with `NodeToCode: the editor's MCP server answers at http://127.0.0.1:27000/mcp ...`;
- `/mcp` shows `plugin:nodetocode:nodetocode` as connected (its tools are named
  `mcp__plugin_nodetocode_nodetocode__<tool>`);
- `/skills` lists `nodetocode:blueprint-comprehension`, `nodetocode:blueprint-to-cpp` and
  `nodetocode:blueprint-migration`.

Then ask, for example, "What does /Game/Characters/BP_Door do?".

| The first line says | What to do |
|---|---|
| `the editor's MCP server answers at <url>` | Nothing, the tools are ready |
| `nothing at <url>, but <project> serves NodeToCode at <other url>` | Set the plugin's MCP URL to the other one (below), or use the stdio bridge |
| `no editor is serving NodeToCode's MCP server` | Open the project with the MCP server on, then run `/mcp reconnect` |

The check takes about a second and changes nothing. It runs in Git Bash on Windows, the shell Claude Code uses for
hooks, and uses `curl`.

## Another port, or the stdio bridge

If you changed **Server Port** (**Project Settings → Plugins → Node to Code → MCP Server**), set the plugin's
**NodeToCode MCP URL** to `http://127.0.0.1:<port>/mcp`: in `/plugin`, **Installed → nodetocode → Configure**, or
install with `claude plugin install nodetocode@nodetocode --config mcp_url=http://127.0.0.1:<port>/mcp`. Then run
`/mcp reconnect`.

With several editors open, a port that can move, or Claude Code started before the editor, use NodeToCode's stdio
bridge instead: it finds the running editor through the file its server writes (see
[Using a Claude or Gemini Subscription](Using-a-Claude-or-Gemini-Subscription)).

```
claude mcp add --scope user nodetocode -- "<Project>/Plugins/NodeToCode/Content/Python/mcp_bridge/launch_bridge.bat" --project "<Project>/<Project>.uproject"
```

Then disable `plugin:nodetocode:nodetocode` in `/mcp`. The skills work over either route.

The plugin's server entry sends no token, so with **Require MCP Bearer Token** on use the bridge above, which reads
the token itself, or the `headers` snippet in [Requiring an MCP Token](Requiring-an-MCP-Token).

## Translate with `/nodetocode:translate`

```
/nodetocode:translate /Game/Characters/BP_Door
/nodetocode:translate /Game/Characters/BP_Door OpenDoor provider=anthropic model=claude-haiku-5-5
```

This runs NodeToCode's own translator, the same as the **Translate** button, with the provider set in the editor or
the one you name. With a Blueprint path it opens that Blueprint on the graph you name (`EventGraph` by default);
without one it translates the graph you have open. The files go where the editor saves translations: a
`<Blueprint>_<date-time>` folder under `Saved/NodeToCode/Translations` (or your custom output folder), with
`<Graph>/<Graph>.h`, `<Graph>/<Graph>.cpp` and the notes. Your `Source/` folder is not touched; ask
Claude to place and compile the code when you want that. Each run is one billed provider request, and Claude only runs
the command when you type it.

## UE 5.8 and Epic's Unreal MCP

If Epic's Unreal MCP also answers, the first line says so. NodeToCode's tools are reachable there too when the MCP
Client Toolset mounts NodeToCode (see [Using NodeToCode with Unreal MCP](Using-NodeToCode-with-Unreal-MCP)), but
prefer the plugin's own server: Epic's route cuts a call off after about 30 seconds.

## Other agents

Codex, Cursor and GitHub Copilot read the same skills from their own folders; copy them as
[Agent Skills](Agent-Skills) shows and connect the agent to `http://127.0.0.1:27000/mcp`. The session-start check
and `/nodetocode:translate` are Claude Code only.
