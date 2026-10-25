# 0003. N2C JSON v2 contract: version 2.0.0, lossless data edges and a kept v1 reader

## Status

proposed (2026-10-24), provisional pending the owner's confirmation on card 0117

**Provisional:** the owner has not decided card 0117 (review decision dec-7). Card 0135 applies the review's
recommendation so the lossless data edges can land; cards 0136-0138 and 0132 follow it. The parts the owner is most
likely to change, the version string and the field names, live in one header,
`Source/Public/Models/N2CJsonContract.h`, and in prose in the prompts; changing them there and regenerating the
capture goldens (`build.ps1 -Test -UpdateGoldens`) is all a different choice needs.

## Sources

- Kanban card 0117 "Decide and record the N2C JSON v2 contract" (owner decision, open) and card 0135, which applies
  it first.
- NodeToCode 2.0 review, 2026-10-22: dec-7 (options and recommendation), next-2 (scope of v2), pr-7 (fan-out model),
  issue-13 (liveness), gap-5 (stable keys), gap-9 (version and migration). Evidence:
  `python -I Reviews/2026-10-22/kanban/lookup.py dec-7 next-2 pr-7 issue-13 gap-5 gap-9` in the workspace.
- PR #7 "Support for situation when one output pin connected with few input" (Yury Rakhuba), against main.

## Context

N2C JSON is what every prompt, MCP client (get-focused-blueprint), Python caller and saved translation
(`N2C_BP_*.json`) reads. Version 1.0.0 was lossy: `flows.data` kept one edge per output pin, so an output feeding
several inputs lost all but one (translation-core-1, the main cause of issue #13); exec flows are node pairs that
lose which pin a link uses; names are display text that change with editor language. `FN2CVersion` was hard-wired to
"1.0.0" and the validator rejected any other value, so no consumer could tell a fixed document from an old one.

Options (dec-7):

1. N2C JSON v2: stable identifiers, lossless edges, pin-qualified exec, a version bump, the v1 reader kept.
2. Incremental 1.x patches, for example merging PR #7's shape (an array of "N1.P2 -> N2.P1" strings for every graph).
3. Replace N2C JSON with Epic's graph DSL.

The review recommends option 1, as a versioned contract with one spec shared by all prompts, and a benchmark against
Epic's DSL in Phase 2 (E18) before any switch.

## Decision

Option 1, provisionally. The sub-choices of card 0117:

1. **Version string:** `"version": "2.0.0"` (`N2CJsonContract::Version`). After the 2.0.0 plugin release an additive
   optional field bumps the minor (2.1.0) and a breaking change the major (3.0.0). Until that release the remaining
   v2 cards (0136-0138) land under 2.0.0, since no released build has written it.
2. **Data edges:** `flows.data` stays an object keyed by the output pin ref ("N1.P2"). The value is the one input pin
   ref it feeds, or an array of input pin refs when it feeds several (PR #7's model, `FN2CDataFlowTargets`). A graph
   without fan-out gets exactly the 1.0.0 object. Every edge appears once, recorded from its output side through
   every branch of a knot. Not a `{from, to}` list: that would change every graph's JSON.
3. **Exec edges (card 0136):** pin-qualified pairs such as `"N1.P2->N3.P1"`, one per link, no chain compression:
   from the exec output pin to the exec input pin it runs, each link once (two outputs into one node are two
   entries), recorded from the output side through knots. Writers emit only this form
   (`N2CJsonContract::MakeExecEdge`). Readers of every readable version accept it and the node-only form
   (`"N1->N2"`, or a chain `"N1->N2->N3"`) of 1.x documents and of 2.0.0 documents saved by development builds
   between cards 0135 and 0136, and keep node-only entries as they are (they never said which pins a link uses).
   No version gate: 2.0.0 is not released, so the two 2.0.0 forms differ only in the entry text. The validator
   checks that a pin-qualified entry runs from an exec output pin to an exec input pin of the named nodes.
4. **Stable keys (card 0138):** pin `name` = `UEdGraphPin::PinName`, a node `class` field, `member_parent` and
   `member_name` from `FMemberReference`; display text only in named hint fields (node `title`, pin `label`).
   Card 0138 applied it: `class` is the UK2Node class; `member_parent` is the declaring class (for a variable the
   property's owner class, not its type; a Blueprint class by its Blueprint's name; "self" only when nothing
   resolves) and `member_name` the name as declared (no `_C` stripping). The node `name` field is gone: `title` (the
   menu title) replaces it. Hints are written in source (English) text (`FText::BuildSourceString`), so the whole
   document, hints included, is the same in every editor language, and each hint only when it says more than its
   identifier (`N2CJsonContract::IsRedundantHint`: not the identifier or its `FName::NameToDisplayString` spelling),
   which also makes UE 5.8's function-call titles (`CollectGarbage`, not `Collect Garbage`) irrelevant. The reader
   takes a 1.x node `name` as the title and keeps 1.x display pin names as they are.
5. **Liveness (card 0137):** `"unused": true` on pure nodes whose outputs are never consumed, `"unreachable": true`
   on impure nodes no entry reaches, `"disabled": true` on disabled nodes; ghost (default, unconnected) events
   dropped. Each flag is written only when true. Card 0137 applied it: "disabled" means
   `UEdGraphNode::IsNodeEnabled()` is false (Disabled, or DevelopmentOnly when the Blueprint does not compile in
   development mode); `flows.execution` routes exec past a disabled node with one exec input and one exec output,
   as the compiler does (`UK2Node::GetPassThroughPin`), and records no link into any other disabled node; ghost
   nodes are `IsAutomaticallyPlacedGhostNode()` (the same on 5.6, 5.7 and 5.8). Entries for "unreachable" are
   impure nodes with exec outputs and no exec input.
   The graph-editing MCP tools (get-focused-blueprint, find-nodes-in-graph) keep ghost nodes, as `"disabled": true`,
   so an agent can wire a default event instead of adding a duplicate; translation never sees them.
6. **v1 reader:** `FN2CSerializer::FromJson` (and `UN2CPythonBridge::ReadN2CJson`, Python
   `nodetocode.read_n2c_json`) reads 1.x and 2.x into the v2 model, whose version is then 2.0.0; the validator accepts
   both majors. It is kept for the whole 2.x line and may go with 3.0.0. Saved translations are never rewritten.
7. **Shared spec fragment (card 0132):** `Content/Prompting/Shared/N2CJsonSpec.md`, included by the six CodeGen
   prompts (in place of their `{{N2C_JSON_SPEC}}` placeholder, by `UN2CSystemPromptManager::LoadPrompts`) and the
   batch export's `Combined_Blueprints.md`. Card 0132 created it, with the output contract beside it in
   `Shared/N2CResponseFormat.md`; `NodeToCode.LLM.Prompts` checks that it names every key the serializer writes and
   the current version, so a field change that skips the spec fails a test.

## Consequences

- Consumers tell old from new by `version`. A 1.x consumer reading 2.0.0 sees an array only where an output fans
  out; the prompts, the batch spec and the MCP tool docs describe both value forms.
- `FN2CFlows::Data` is `TMap<FString, FN2CDataFlowTargets>`; Blueprint or Python code that read the old
  `TMap<FString, FString>` must read `Targets` (none in the plugin did).
- The capture goldens record 2.0.0, and `NodeToCode.Capture.Golden.V1Reader` keeps 1.0.0 documents readable.
- A 2.x consumer must accept both exec entry forms; `NodeToCode.Capture.Golden.ExecFlows` checks the pin form on
  every golden, and V1Reader reads a node-only 2.0.0 document.
- If the owner chooses option 2 or 3, a new record supersedes this one; the data-edge fix itself stays, since it only
  adds an array where 1.0.0 dropped edges.
