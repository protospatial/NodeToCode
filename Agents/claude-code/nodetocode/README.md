# NodeToCode plugin for Claude Code

This Claude Code plugin connects Claude Code to NodeToCode in your open Unreal Editor, so you can ask about,
translate and migrate Blueprints in plain words. It needs the NodeToCode Unreal plugin (UE 5.6 to 5.8) in your
project, with its MCP server on (**Project Settings > Plugins > Node to Code (User) > Enable MCP Server**, on by
default).

| Part | What it does |
|---|---|
| MCP server `nodetocode` | Connects to the editor at `http://127.0.0.1:27000/mcp`. Claude sees NodeToCode's tools as `mcp__plugin_nodetocode_nodetocode__<tool>` |
| Skills `blueprint-comprehension`, `blueprint-to-cpp`, `blueprint-migration` | Claude uses them by itself when you ask what a Blueprint does, ask to translate one to C++, or ask to migrate one to C++ |
| Command `/nodetocode:translate` | Translates a Blueprint graph with NodeToCode's own translator and saves the files |
| Session-start check | Prints one line at the start of each session saying whether the editor answers, and what to do if it doesn't |

## Install in Claude Code

The plugin ships inside the NodeToCode plugin folder, so the version you install matches the NodeToCode version in
your project. Run these from a terminal, using your own path to the NodeToCode plugin folder
(`<Project>/Plugins/NodeToCode`, or `Engine/Plugins/Marketplace/NodeToCode` for an engine install from Fab):

```
claude plugin marketplace add "<Project>/Plugins/NodeToCode/Agents/claude-code"
claude plugin install nodetocode@nodetocode
```

In a Claude Code session, `/plugin marketplace add <same path>` and `/plugin install nodetocode@nodetocode` do the
same. To try the plugin for one session without installing it, start Claude Code with
`claude --plugin-dir "<Project>/Plugins/NodeToCode/Agents/claude-code/nodetocode"`.

Then start a new session in your Unreal project's folder, with the editor open:

- the first line says `NodeToCode: the editor's MCP server answers at http://127.0.0.1:27000/mcp ...`;
- `/mcp` lists `plugin:nodetocode:nodetocode` as connected;
- `/skills` lists the three `nodetocode:` skills, and `/nodetocode:translate` is a command.

Claude Code reads a plugin installed this way in place, from the NodeToCode folder, so after you update NodeToCode a
new session picks up the new version. If you move or delete that folder, run the two commands again with the new
path.

If you already added NodeToCode's server yourself (`claude mcp add ... nodetocode ...`), remove it
(`claude mcp remove nodetocode`) so the tools are not listed twice.

### Another port

If you changed **Server Port** (**Project Settings > Plugins > Node to Code > MCP Server**), set the plugin's
`mcp_url` option to `http://127.0.0.1:<port>/mcp`: in `/plugin`, open **Installed > nodetocode > Configure**, or
install with `claude plugin install nodetocode@nodetocode --config mcp_url=http://127.0.0.1:<port>/mcp`. Then run
`/mcp reconnect` (or start a new session).

### The stdio bridge instead of HTTP

Use NodeToCode's stdio bridge when several editors are open, when the server's port can change (a **Port Fallback
Range** above 1), or when you start Claude Code before the editor: the bridge finds the running editor through the
file its MCP server writes in `%LOCALAPPDATA%\NodeToCode\servers\` (on macOS `~/Library/Application
Support/Epic/NodeToCode/servers/`, on Linux `~/.config/Epic/NodeToCode/servers/`) and waits for an editor that is
still starting. It needs Python 3, which every Unreal Engine install has:

```
claude mcp add --scope user nodetocode -- "<Project>/Plugins/NodeToCode/Content/Python/mcp_bridge/launch_bridge.bat" --project "<Project>/<Project>.uproject"
```

(`launch_bridge.sh` on macOS and Linux, or `python <...>/nodetocode_bridge.py` with the same arguments.) Then, in
`/mcp`, disable the plugin's own `plugin:nodetocode:nodetocode` server so the tools are not listed twice. The
skills work the same over either route; through the bridge the tools are named `mcp__nodetocode__<tool>`.

## The session-start line

| The line says | What to do |
|---|---|
| `the editor's MCP server answers at <url>` | Nothing: the tools are ready |
| `nothing at <url>, but <project> serves NodeToCode at <other url>` | Set `mcp_url` to the other URL (above) or use the stdio bridge |
| `no editor is serving NodeToCode's MCP server` | Open the project in Unreal Editor with the MCP server on, then `/mcp reconnect` |
| `Epic's Unreal MCP also answers at ...` | On UE 5.8, NodeToCode's tools are also reachable through Epic's Unreal MCP when the MCP Client Toolset mounts NodeToCode. Prefer this plugin's server: Epic's route cuts a call off after about 30 seconds |

The check takes about a second, exits successfully either way and changes nothing. It runs in Git Bash on Windows
(which Claude Code uses for hooks) and needs `curl`, which Windows 10 and later include. It reads the plugin's
`mcp_url`, `NODETOCODE_DISCOVERY_DIR` (another discovery folder) and `NODETOCODE_UNREAL_MCP_URL` (Epic's Unreal MCP,
default `http://127.0.0.1:8000/mcp`); set `NODETOCODE_HOOK_DEBUG=1` to see what it found.

## `/nodetocode:translate`

```
/nodetocode:translate /Game/Characters/BP_Door
/nodetocode:translate /Game/Characters/BP_Door OpenDoor provider=anthropic model=claude-haiku-5-5
/nodetocode:translate language=python
```

It translates one graph with the LLM provider configured in the editor (or the `provider` and `model` you name),
using NodeToCode's own prompts, the same as the **Translate** button. With a Blueprint path it opens that Blueprint in
the editor on the graph you name (`EventGraph` by default); without one it translates the graph you have open. It
saves the result where the editor saves its translations, in a `<Blueprint>_<date-time>` folder under
`Saved/NodeToCode/Translations` (or your custom output folder), and lists the files. It does not touch your
project's `Source/` folder: ask Claude to put the code in your module and compile it (the `blueprint-to-cpp` skill)
when you want that. Each run is one billed provider request. Claude only runs this command when you type it.

## Codex, Cursor, GitHub Copilot and other agents

The three skills follow the open Agent Skills format, so other agents can use them without this plugin. Copy the
skill folders (whole folders, with their `references/`) into the skills folder the agent reads:

| Agent | Project folder | Personal folder |
|---|---|---|
| Codex | `.agents/skills/` | `~/.agents/skills/` |
| GitHub Copilot | `.github/skills/` or `.agents/skills/` | `~/.copilot/skills/` or `~/.agents/skills/` |
| Cursor | `.cursor/skills/` or `.agents/skills/` | `~/.cursor/skills/` or `~/.agents/skills/` |

From your project's folder on Windows:

```powershell
New-Item -ItemType Directory -Force .agents\skills | Out-Null
Copy-Item -Recurse -Force Plugins\NodeToCode\Agents\claude-code\nodetocode\skills\* .agents\skills\
```

On macOS or Linux:

```
mkdir -p .agents/skills && cp -R Plugins/NodeToCode/Agents/claude-code/nodetocode/skills/. .agents/skills/
```

Then connect the agent to NodeToCode's MCP server at `http://127.0.0.1:27000/mcp` (Streamable HTTP), or, for an
agent that only runs stdio servers, to the bridge command above. Copy the skills again after you update NodeToCode.
The session-start check and `/nodetocode:translate` are Claude Code only.

More: [Agent Skills](https://github.com/protospatial/NodeToCode/wiki/Agent-Skills),
[the Claude Code plugin](https://github.com/protospatial/NodeToCode/wiki/Claude-Code-Plugin) and
[Using NodeToCode with Unreal MCP](https://github.com/protospatial/NodeToCode/wiki/Using-NodeToCode-with-Unreal-MCP).
