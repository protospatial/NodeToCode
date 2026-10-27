# Agent Skills and the Claude Code plugin

The Agent Skills NodeToCode ships for coding agents, how they are generated and checked, their UE 5.8 skill-registry
copies, and the Claude Code plugin around them. User pages: `wiki/developers/Agent-Skills.md`,
`wiki/developers/Claude-Code-Plugin.md`.

- The user-facing Agent Skills (blueprint-comprehension, blueprint-to-cpp, blueprint-migration) are written once in
  `Agents/claude-code/nodetocode/skills/<name>/` and ship in the plugin package (`Config/FilterPlugin.ini`). After
  editing one, or `Content/Prompting/Shared/N2CJsonSpec.md` (copied into their `references/`), run
  `python -I Agents/sync_skills.py`: it regenerates the copies and the `.agents/skills/` mirror. `--check` (run by
  `Tests/python/test_agent_skills.py`, so by `build.ps1 -Test`) fails on a stale copy or mirror, invalid frontmatter,
  a `SKILL.md` over 500 lines, or a skill naming an MCP tool the plugin does not register: renaming or removing a
  tool means updating the skills in the same change. User page: `wiki/developers/Agent-Skills.md`.
- On UE 5.8 the skills are also registered in Epic's skill registry (card 0177): the script writes
  `Content/Python/nodetocode/skills/<name>.md` (body without frontmatter, `references/` appended) and `skills.json`
  (descriptions), and `Content/Python/nodetocode/skills/__init__.py` has one `@agent_skill` class per skill; a new skill
  needs its class there. `--check` also fails when a skill names a `NodeToCodeTools.<tool>` that toolset lacks, an Epic
  tool missing from `wiki/developers/NodeToCode-and-Epic-Tool-Mapping.md`, or a NodeToCode tool UE 5.8 leaves to
  Unreal MCP without naming its Epic counterpart in the same file.
- The Claude Code plugin around them (`Agents/claude-code/nodetocode/`: `.claude-plugin/plugin.json`, `.mcp.json`,
  `hooks/`, `commands/translate.md`; the local marketplace `Agents/claude-code/.claude-plugin/marketplace.json`; the
  draft entry for other marketplaces `Agents/claude-code/marketplace-entry.json`) is checked by the same script and
  `Tests/python/test_claude_plugin.py`: its `version` must equal `NodeToCode.uplugin`'s `VersionName` (bump both), and
  its command may name registered tools only. Run `claude plugin validate --strict` on both folders after a change;
  live check: `Tests/mcp_probe/live_claude_plugin_0172.py`. User page: `wiki/developers/Claude-Code-Plugin.md`.
