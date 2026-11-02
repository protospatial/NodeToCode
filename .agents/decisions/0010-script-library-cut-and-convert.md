# 0010. Script library: retire the saved-script tools, keep three utilities as Agent Skills, delete the rest

## Status

decided (2026-11-02), by the owner on card 0158

## Sources

- Kanban card 0158 "Decide, per bundled utility and for the saved-script tools, whether to fix, delete or convert to
  an Agent Skill" (owner decision, decided 2026-11-02). Cards that build on it: 0298, 0302, 0303, 0305, 0306, and the
  `scripts/` half of 0297.
- NodeToCode 2.0 review, 2026-10-22: rec-agentic-landscape-14 (convert shipped scripts into skills; the chosen
  direction), arch-arch-mcp-8 (keep the tools as Python-declared tools; not chosen), next-11, and the per-utility
  findings python-bridge-6, -9, -13, -19, -27 to -35, -39 to -42. Evidence: `python -I
  Reviews/2026-10-22/kanban/lookup.py rec-agentic-landscape-14 arch-arch-mcp-8 next-11` in the workspace.
- Decision [0007](0007-crud-tool-families-fix-then-freeze.md): CRUD families frozen; on UE 5.8 Epic's EditorToolset
  does Blueprint and asset CRUD.

## Context

The plugin ships 13 utilities in `Content/Python/scripts/{core,data,ui,utilities}`, indexed by
`script_registry.json`, and six MCP tools in the PythonTools toolset that list, search, get, save and delete saved
scripts and list a script's functions. Most utilities carry open review findings. A custom registry plus save, search
and run tools duplicates what an Agent Skill (a `SKILL.md` with its own `scripts/`) gives every major agent client,
and the content-authoring utilities duplicate the CRUD tools that decision 0007 froze.

## Decision

1. **The six saved-script tools are retired:** `list-python-scripts`, `search-python-scripts`, `get-python-script`,
   `get-script-functions`, `save-python-script`, `delete-python-script`, with `script_registry.json` and the registry
   code behind them. `run-python` stays (card 0299).
2. **Each utility:**

| Utility | Folder | Outcome | Why |
|---|---|---|---|
| asset_validator | utilities | convert to an Agent Skill | read-only checks agents can use; fix -31 to -33 in the port |
| asset_iterator | core | convert to an Agent Skill | read-only asset queries; fix -34 and -39 in the port |
| project_folder_scaffold | utilities | convert to an Agent Skill | setup helper with no findings |
| blueprint_factory | core | delete | Blueprint creation is frozen CRUD (0007); -19, -35 |
| variable_batch_creator | core | delete | frozen CRUD; -27, -28 |
| function_scaffolder | core | delete | frozen CRUD; -29, -30 |
| component_adder | core | delete | frozen CRUD; -40 |
| struct_creator | data | delete | frozen CRUD; -13 |
| datatable_populator | data | delete | frozen CRUD; python-bridge-6 |
| ue5_compat | core | delete | dead export helper; -41, -28 |
| asset_duplicator | utilities | delete | little value over the editor and Epic's tools |
| asset_editor_opener | ui | delete | little value; the focus tools open assets |
| utility_widget_launcher | ui | delete | little value; -42 |

3. **Where the skills live:** with the other Agent Skills NodeToCode ships (`Agents/claude-code/nodetocode/skills/`,
   synced by `Agents/sync_skills.py`), each with its script under the skill's own `scripts/`, run through `run-python`.

## Consequences

- Smoke counts move when the retirement lands: the default count drops by six (67 to 61, or 68 to 62 after card 0314)
  and `python_only` drops from 7 to 1 (run-python only), on all three engines. The orchestrator claims
  `smoke-tool-count` at merge.
- Card 0305 and 0306 become delete-and-convert cards; 0302 (package names, importable saved scripts), 0303 (declare
  the script-library tools in Python) and 0298 (registry crash safety) are closed as superseded by this record, and
  the `scripts/` half of 0297 goes with the deletions.
- The docs, tool reference and Python-mode pages drop the six tools; Python script-only mode becomes run-python plus
  the three skills.
- Users who saved their own scripts through the tools keep the files in their project; the record does not delete
  project content, and the release notes say how to move them into a skill.
