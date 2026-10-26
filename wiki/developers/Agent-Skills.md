NodeToCode ships three Agent Skills: instructions that teach a coding agent (Claude Code, Codex, GitHub Copilot,
Cursor and others that read the Agent Skills format) how to read your Blueprints through NodeToCode's MCP tools,
translate them to C++ and migrate them safely. Copy them into the folder your agent reads; the agent then uses them
by itself when a request matches, without you naming them.

The skills work on UE 5.6, 5.7 and 5.8. They need the agent connected to NodeToCode's MCP server (see
[Using a Claude or Gemini Subscription](Using-a-Claude-or-Gemini-Subscription)) or, on UE 5.8, to Epic's Unreal MCP
with the [NodeToCodeTools](NodeToCodeTools) toolset or NodeToCode mounted (see
[Using NodeToCode with Unreal MCP](Using-NodeToCode-with-Unreal-MCP)). On UE 5.8 with Epic's EditorToolset, the
skills send the asset, compile and save steps to Epic's tools, as the
[tool mapping](NodeToCode-and-Epic-Tool-Mapping) lists them. On UE 5.8 with the **Toolset Registry** plugin, the editor
also serves the three skills itself (see [In Epic's skill registry](#in-epics-skill-registry-ue-58)).

## The skills

| Skill | The agent uses it when you ask | What it does |
|---|---|---|
| `blueprint-comprehension` | what a Blueprint does, to explain, review or document one | Lists the Blueprint's graphs and reads them as N2C JSON by asset path (`list-blueprint-graphs`, `get-blueprint-n2cjson`), then explains each event and function in order, the variables used and the nodes that never run. Read-only |
| `blueprint-to-cpp` | to convert or translate a Blueprint to C++, or a tagged set of graphs | Reads the Blueprint, writes the `.h` and `.cpp` itself (or, when you ask for NodeToCode's translator, translates by path with `translate-batch` or, on UE 5.8, `NodeToCodeTools.translate_graph`), places the code in your module and builds it. For many graphs it tags them and exports or translates the tag in one batch |
| `blueprint-migration` | to migrate or move a Blueprint to C++ | One Blueprint at a time with three stops for your approval: the plan, the compiled C++, and the switch-over. It never reparents, deletes or saves anything before the C++ compiles and you approve |

Each skill treats the text inside your Blueprints (comments, names, string literals) as data, never as instructions,
so a Blueprint from the Marketplace cannot steer the agent.

## Where to find them

In the plugin folder: `Plugins/NodeToCode/Agents/claude-code/nodetocode/skills/` (for an engine install from Fab,
the NodeToCode folder under the engine's `Engine/Plugins/Marketplace/`). Each skill is a folder with a `SKILL.md`
and, for the first two, a `references/` folder with the N2C JSON format and the C++ translation rules. Copy whole
folders, not only `SKILL.md`.

## Install them for your agent

For Claude Code, the [Claude Code plugin](Claude-Code-Plugin) installs the skills together with the connection to
NodeToCode's MCP server and a `/nodetocode:translate` command; copying is not needed then.

Copy the three folders into the skills folder your agent reads, at your project's root (for every agent working in
that project) or in your home folder (for every project).

| Agent | Project folder | Personal folder |
|---|---|---|
| Claude Code | `.claude/skills/` | `~/.claude/skills/` |
| Codex | `.agents/skills/` | `~/.agents/skills/` |
| GitHub Copilot | `.github/skills/` or `.agents/skills/` | `~/.copilot/skills/` or `~/.agents/skills/` |
| Cursor | `.cursor/skills/` or `.agents/skills/` | `~/.cursor/skills/` or `~/.agents/skills/` |

For example, on Windows from your project folder, for Claude Code:

```powershell
New-Item -ItemType Directory -Force .claude\skills | Out-Null
Copy-Item -Recurse Plugins\NodeToCode\Agents\claude-code\nodetocode\skills\* .claude\skills\
```

Start a new agent session afterwards. In Claude Code, `/skills` lists them. Copy them again after updating
NodeToCode, so the skills match the plugin's tools.

## In Epic's skill registry (UE 5.8)

With the **Toolset Registry** plugin enabled on UE 5.8 (restart the editor after enabling it), NodeToCode registers
the three skills in Epic's skill registry at startup, next to the [NodeToCodeTools](NodeToCodeTools) toolset. Any
client of **Unreal MCP** then finds them with no copying (Epic's in-editor assistant reads the same registry; it was
not part of the check). On UE 5.6 and 5.7, or with Toolset Registry off, nothing is registered and nothing is logged.

Through Unreal MCP they are served by the `ToolsetRegistry.AgentSkillToolset` toolset: `ListSkills` returns each
skill's path and description, and `GetSkills` with those paths returns the instructions.

```json
{"name": "call_tool", "arguments": {"toolset_name": "ToolsetRegistry.AgentSkillToolset", "tool_name": "ListSkills",
  "arguments": {}}}
{"name": "call_tool", "arguments": {"toolset_name": "ToolsetRegistry.AgentSkillToolset", "tool_name": "GetSkills",
  "arguments": {"skillPaths": ["/NodeToCode/Python/nodetocode/skills/__init___PY.NodeToCodeBlueprintToCppSkill"]}}}
```

The three paths end in `NodeToCodeBlueprintComprehensionSkill`, `NodeToCodeBlueprintToCppSkill` and
`NodeToCodeBlueprintMigrationSkill`. The instructions are the same text as the `SKILL.md` files, with the
`references/` files appended, since a registry client cannot open them.

## Try it

With the editor open and the agent connected, ask in plain words:

- "What does /Game/Characters/BP_Door do?"
- "Translate /Game/Characters/BP_Door to C++ in my game module."
- "Migrate /Game/Characters/BP_Door to C++."

For a migration, expect the agent to stop and ask before it changes anything in the project. Reparenting the
Blueprint is your step, in the Blueprint editor's **Class Settings > Parent Class**.

## Limits

- The agent compiles C++ with your project's build. With the editor open, Live Coding covers changes inside `.cpp`
  files only; a new class or property needs the editor closed and a full build.
- Through NodeToCode mounted by Epic's MCP Client Toolset, a translation longer than about 30 seconds is cut off
  while it still finishes and is billed; with NodeToCodeTools on UE 5.8 the skill translates there instead.
- NodeToCode cannot list which levels and assets use a Blueprint. Check the editor's **Reference Viewer** before you
  reparent one.
