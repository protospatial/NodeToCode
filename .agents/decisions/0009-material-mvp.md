# 0009. Material MVP: part 1 of the 0247 graph-type record (materials)

## Status

decided (2026-11-02), by the owner on card 0443 (the card's recommended answers)

## Sources

- Kanban card 0443 "Material MVP" (owner decision, decided 2026-11-02) and epic 0442, "Capture Material graphs into
  N2C JSON and explain them (read-only, UE 5.6-5.8)". Cards that build on it: 0444, 0445, 0447, 0448, 0449, 0453,
  0455, 0470 and the rest of epic 0442.
- [0008](0008-graph-types-every-k2-graph.md), the graph-type record; this record is its part 1 for materials.
- Evidence: card 0446's inventory, [material-classes.md](../../Context/translation-pipeline/material-classes.md), and
  the epic proposal under `Reviews/2026-10-22/material-epic/` in the workspace.
- It assumes card 0089 option (b), applied provisionally: on UE 5.8 Epic's MaterialTools does material CRUD.
  It names UE 5.6, 5.7 and 5.8. Decision [0003](0003-n2c-json-v2-contract.md) holds the contract version.

## Context

Capture is K2-only, so materials came back `ASSET_NOT_FOUND`. On 5.8 Epic's MaterialTools lists expressions in one
call but needs one call per expression to read the wiring and has no whole-graph read or translation; 5.6 has no Epic
MCP and 5.7 only AIAssistant. The engine already emits HLSL (`FMaterial::GetMaterialExpressionSource`), expressions
are editor-only data, and the Material Editor edits a transient preview material until Apply.

## Decision

1. **Assets.** UMaterial and UMaterialFunction, captured by exact class. Material Layers, Layer Blends,
   UMaterialFunctionInstance and Layer/Blend instances are refused with `UNSUPPORTED_ASSET_TYPE`. Material Instances
   may get a read-only summary later (card 0470).
2. **Targets.** N2C JSON capture plus a Pseudocode explanation first; HLSL Custom-node bodies after that, behind cards
   0250 and 0312. No C++ or Python rebuild, and no material CRUD on any engine.
3. **Read source.** The saved asset by path, with `workingCopy` reported in every result. Focused working-copy reads
   come later.
4. **Function calls.** Functions under `/Engine/Functions` are described by name at depth 0 and expanded only when
   depth is greater than 0.
5. **Static switches.** Both branches are kept.
6. **Contract version.** The optional `metadata.asset_kind` (absent means Blueprint) goes into 2.0.0: under decision
   0003 the contract stays 2.0.0 until card 0083 tags v2.0.0-beta.1, so it is an additive field, not 2.1.0. Reader
   tolerance (card 0445) does not wait on this.
7. **Public names,** mirroring the camelCase contract of card 0157: MCP tools `get-material-n2cjson` and
   `translate-material`; arguments `materialPath`, `depth`, `compact`, `toFile`; Python
   `get_material_n2cjson(material_path)`.
8. **Sprint.** The epic 0442 cards go into sprint s9, after s8, not a separate materials-only sprint. Placement of the
   E10 and E11 cards is not decided here.

## Consequences

- Epic 0442's cards can start in s9 with these names and scope fixed; renaming them after card 0349 freezes the tool
  names is avoided.
- Material capture adds tools (two in the MVP), so the smoke tool count moves when they land.
- Material editing stays with Epic's MaterialTools on 5.8; NodeToCode is read-and-explain only.
- If 0089 is decided otherwise, epic 0442 is re-sliced and a new record supersedes this one.
