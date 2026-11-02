# 0008. Graph types: own every K2 graph, refuse pose and RigVM graphs with a typed error

## Status

decided (2026-11-02), by the owner on card 0247 (option B)

## Sources

- Kanban card 0247 "Decide which graph types NodeToCode captures" (owner decision, decided 2026-11-02). Cards that
  build on it: 0248 (the refusal list), the E10 capture and N2C JSON contract cards behind it, and 0443 (Material MVP),
  which also waited on this record.
- Evidence: card 0246's matrix, [graph-type-coverage.md](../../Context/translation-pipeline/graph-type-coverage.md)
  (22 graph types measured on real assets on UE 5.6, 5.7 and 5.8, options A to D), and the node-class half,
  [k2-node-inventory.md](../../Context/translation-pipeline/k2-node-inventory.md).
- NodeToCode 2.0 review, 2026-10-22: later-6, gap-11, dec-1. Evidence: `python -I
  Reviews/2026-10-22/kanban/lookup.py later-6 gap-11 dec-1` in the workspace.

## Context

Capture is K2-only, but nothing refused the graphs it cannot read. Pose graphs came out confidently wrong: every pose
node was marked unused, so the LLM wrote empty code, and one transition rule became `return false;`. State machines
and Control Rig graphs failed as `EMPTY_GRAPH`. Every K2 graph, including the K2 graphs inside Widget, Editor Utility,
Level and Animation Blueprints, already captured and translated correctly on all three engines.

Options (graph-type-coverage.md, "Options for decision 0247"): A, only Actor-style Blueprints; **B, every K2 graph,
refuse pose and RigVM graphs**; C, B plus AnimGraph and state machines (L, no clear C++ target); D, capture
everything and warn on the rest.

## Decision

Option B. Row numbers are the matrix's.

1. **Supported:** event graphs (1), functions (2), macros in a Blueprint (3), macro libraries (4), collapsed graphs
   (5), construction scripts (6), interface functions implemented in a Blueprint (7), Blueprint Interface assets (8),
   Level Blueprints (10), Widget Blueprint event graphs and functions (11, 12), Editor Utility Widgets and Blueprints
   (14, 15), and AnimBlueprint event graphs (16).
2. **Supported in part, with a note:** K2 nodes that map to no N2C type (9, typed `Unknown`), widget property
   bindings (13, a binding field), and the focus and path gaps of collapsed graphs and Level Blueprints (5, 10).
3. **Refused with a typed error:** AnimGraph (17), state machine graphs (18), states (19), Control Rig and other RigVM
   graphs (21), and non-Blueprint assets (22, materials follow decision 0443's card). Transition rules (20) are
   refused until the pose-result boundary fix (F1) lands, then move to supported: they are pure boolean K2 graphs.
4. **How refusal works:** keyed on the graph's schema (`UAnimationGraphSchema`, `UAnimationStateMachineSchema` and
   any other non-K2 schema) and node base, never on graph names. Tools and batch translation return
   `UNSUPPORTED_GRAPH_TYPE` naming the type instead of `EMPTY_GRAPH`; the graph overlay shows the graph as not
   supported.

## Consequences

- Card 0248 implements the refusal list (S), plus the F1 boundary fix, the F2/F3 error and graph-kind fixes and the F4
  metadata (each S) from the matrix.
- The capture and N2C JSON contract cards behind 0247 assume K2 graphs only; no states-and-transitions model is added
  to the contract.
- AnimBP users get a clear "not supported" on pose graphs instead of empty or wrong code. Supporting pose graphs
  (option C) would be a new record that supersedes this one.
- Materials are not covered here: they follow card 0443.
