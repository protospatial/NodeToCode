# 0006. CRUD tool families: fix crash and corruption defects, then freeze

## Status

proposed (2026-10-26), provisional pending the owner's confirmation on card 0140

**Provisional:** the owner has not decided card 0140 (review decision dec-5). The E12 cards since card 0151 followed
the review's recommendation (option 2), and card 0168 writes it down here and in the tools' `AGENTS.md` files so
agents stop growing the frozen tools. It also leans on card 0089 (dec-1), applied provisionally as option (b) by card
0174: on UE 5.8 Epic's EditorToolset does Blueprint and asset CRUD. If the owner picks option 1 or 3, a new record
supersedes this one and the "Status: frozen" sections come out of the `AGENTS.md` files listed below.

## Sources

- Kanban card 0140 "Decide how much effort goes into the graph-edit, variable and structure MCP tools" (owner
  decision, in review) and card 0168, which records it; card 0089 (dec-1) and card 0174, which leaves the
  Epic-duplicating tools to Unreal MCP on 5.8; epic 0442's note on Material graphs.
- NodeToCode 2.0 review, 2026-10-22: dec-5 (the options), next-7 (the fix list it gates), next-6 (explicit targeting
  and comprehension tools, not frozen), dec-1. Evidence: `python -I Reviews/2026-10-22/kanban/lookup.py dec-5 next-7
  next-6` in the workspace.
- Code: the toolset declarations `Source/Public/MCP/Tools/N2CMcpToolsets.h` (decision 0005) and the Epic-overlap
  table `N2CMcpEpicOverlap::Tools` in `Source/Public/MCP/Tools/N2CMcpEpicOverlap.h` (34 tools, mirrored in
  `wiki/developers/NodeToCode-and-Epic-Tool-Mapping.md`).

## Context

The review verified about 150 findings in the graph-edit, function, variable, component, class, compile and Content
Browser tools. dec-5 offered: (1) fix every finding; (2) fix only crash, data-loss and Blueprint-corrupting defects,
then freeze, and on 5.8 hide the tools in favour of Epic's EditorToolset; (3) remove them. 5.6 and 5.7 have no
Epic MCP, so their users still need these tools; on 5.8 Epic's explicitly targeted `BlueprintTools`, `AssetTools`
and `ActorTools` are the better default for CRUD. The high-severity fixes of next-7 have landed (cards 0151 to 0156,
0166, 0167), and card 0174 leaves the 34 overlapping tools to Unreal MCP on 5.8.

## Decision

Option 2, provisionally. The frozen set is read from the code, not from a hand list:

1. **Frozen toolsets** (by their declaration in `N2CMcpToolsets.h`): `BlueprintGraphTools`,
   `BlueprintFunctionTools`, `BlueprintVariableTools`, `BlueprintCompileTools`, `BlueprintClassTools`,
   `BlueprintComponentTools`, `ContentBrowserTools`. Every tool in them is frozen, read-only ones included
   (`find-nodes-in-graph`, `search-variable-types`, `list-components-in-actor`).
2. **Frozen tools in other toolsets**: any tool in `N2CMcpEpicOverlap::Tools`. Today that adds
   `list-blueprint-functions`, `list-overridable-functions` and `search-blueprint-nodes` (`BlueprintDiscoveryTools`).
3. **Frozen outside MCP**: `UN2CStructUtilsLibrary` (`Source/Private/BlueprintLibraries/N2CStructUtilsLibrary.cpp`).
4. **Not frozen**: comprehension, export, translation, batch and tag tools: `BlueprintDiscoveryTools` except the
   tools of point 2, `BlueprintTagTools`, `TranslationTools`, and the rest of the declared toolsets
   (`FileSystemTools`, `PythonTools`). Material read and translate tools (epic 0442) are comprehension or
   translation; a Material edit family would be new CRUD and is out of scope.

**Bug fixes only** admits a change that fixes one of: a crash or hang (a modal dialog in an unattended call counts);
data loss; Blueprint or project corruption (wrong pin types, lost flags, orphaned or misnamed graphs, content written
outside the project); a result that misreports what happened (success for an edit that did not happen, wrong counts,
duplicates, a wrong type in a read). Also admitted, as they add no capability: the explicit targeting arguments of
next-6 on existing tools (`blueprintPath`, `graphName`, `graphGuid`, card 0152; card 0274 for the rest), security
hardening of what a tool may write (card 0182), keeping the tools building on the supported engines, tests, docs that
match the code, and removing dead code. **Not admitted:** a new tool in a frozen toolset or a new tool that Epic's
EditorToolset also has, a new argument, mode or result that adds capability (card 0292's class, soft and interface
references waits for the owner for this reason), and moving a frozen tool into an unfrozen toolset.

## Consequences

- The policy is in `Source/Private/MCP/Tools/AGENTS.md` ("Frozen tool families"), with a short "Status: frozen, bug
  fixes only (dec-5)" section in the `AGENTS.md` of `Implementations/Blueprint/` and its `Graph/`, `Functions/`,
  `Variables/`, `Components/`, `Classes/`, `Compilation/` folders and of `Implementations/ContentBrowser/`; the root
  `AGENTS.md` points to it.
- A tool that Epic's EditorToolset gains later goes in `N2CMcpEpicOverlap::Tools` (card 0174's rule) and is frozen
  from then on.
- New CRUD needs on 5.8 go to Epic's tools; on 5.6 and 5.7 they wait for the owner. New comprehension, export or
  translation work goes in the unfrozen toolsets.
- To confirm with the owner: option 2 itself; whether the Python bridge's edit calls (`N2CPythonBridge`, which share
  the frozen tools' code and are fixed with them by cards 0276 and 0286) are frozen too; how long the frozen tools are
  maintained on 5.6 and 5.7 (card 0089's open question; until then they keep receiving fixes).
