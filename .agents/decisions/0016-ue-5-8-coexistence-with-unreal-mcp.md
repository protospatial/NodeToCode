# 0016. UE 5.8 and Epic's Unreal MCP: NodeToCode keeps its unique tools and leaves 34 duplicates to Epic

## Status

decided (2026-11-02), by the owner on card 0089 (option b; applied provisionally since 2026-10-26 by #151)

## Sources

- Kanban card 0089 "Decide how NodeToCode positions itself against UE 5.8's Unreal MCP" (owner decision, decided
  2026-11-02). Cards that built on it: 0174 (#151), the E15 cards, and epic 0442 (record 0009 assumes option b).
- `wiki/developers/NodeToCode-and-Epic-Tool-Mapping.md` (the 34 tools and their Epic counterparts).
- [0007](0007-crud-tool-families-fix-then-freeze.md): the CRUD families are frozen.
- NodeToCode 2.0 review, 2026-10-22: dec-1 and the overlap matrix.

## Context

UE 5.8 ships Epic's experimental Unreal MCP (`ModelContextProtocol`) and `EditorToolset`, whose explicitly targeted
CRUD tools overlap 34 of NodeToCode's tools. Options (dec-1): (a) serve every tool on every engine; (b) keep
NodeToCode's unique tools and leave the duplicates to Epic on 5.8; (c) become only a toolset inside Epic's registry.

## Decision

Option (b).

1. **On UE 5.8 with both `ModelContextProtocol` and `EditorToolset` enabled,** NodeToCode's own MCP server leaves the
   34 overlapping tools to Epic's EditorToolset. Its unique tools (N2C JSON, translation, batch, tags, connect-pins, comments, run-python and the other keep/differentiate tools) stay.
2. **NodeToCode's own server stays on by default on 5.8.** The switch point is
   `N2CMcpEpicOverlap::bLeaveToUnrealMcpByDefault`; Project Settings > Overlapping Tools (Automatic / Leave them to
   Unreal MCP / Always serve them) lets a user choose, and a saved choice wins.
3. **On 5.6, 5.7, and 5.8 without those plugins,** every tool is served.
4. **Support horizon for the duplicated tools:** they stay on 5.6 and 5.7 as long as those engines are in the
   window (record 0011), under the freeze of record 0007.

## Consequences

- NodeToCode's direction on 5.8 is comprehension, translation and agent-native features, not CRUD.
- When 5.6 leaves the window, the duplicated tools are reviewed again; a change needs a new record.
