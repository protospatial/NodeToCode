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
- Card 0235 (additive, under 2.0.0 since no release has written it): a graph may carry an optional `comments` array
  after `flows`, one `{"text", "nodes"}` object per comment box (`N2CJsonContract::CommentsField`; `nodes` lists the
  IDs of the captured nodes inside the box and is left out when there are none); a `Composite` node's `member_name` is
  its collapsed graph's name; `graph_type` values are unchanged but now come from the graph's schema (the construction
  script is `Construction`, a macro `Macro` whatever its name, an anim graph or state machine `Animation`). A reader that
  predates it keeps `comments` as an unknown field (amendment item 2 below).
- Card 0238 (additive and corrective, under 2.0.0 since no release has written it): a map pin keeps its key in `type` and
  `sub_type` and gains `value_type` and `value_sub_type` (`N2CJsonContract::PinValueTypeField`, `PinValueSubTypeField`),
  written after its flags on map pins only; user structs and enums used only as map values are listed. Pin `type` values
  change where the subcategory decides them: an enum pin is `Enum` (was `Byte`; a byte without an enum stays `Byte`), a
  Target pin `Self` (was `Object` with `sub_type` "self", which it keeps), a bitmask int or byte `Bitmask`, a Select
  index `Index`. Make Map's `member_name` is "<key> to <value>" in pin type spellings. A reader that predates it keeps
  the two fields as unknown fields; this reader keeps a map pin without them as it is and an unknown `value_type` with
  an `UNKNOWN_PIN_TYPE` warning.
- Card 0242 (additive, under 2.0.0 since no release has written it; from PR #8 by flashpoint493): a document may carry
  the Blueprint's member variables as an optional top-level `variables` array after `enums`, and a function graph its
  local variables as an optional `local_variables` array after `comments` (`N2CJsonContract::VariablesField`,
  `LocalVariablesField`). Each entry has `name` and its type in the pin spellings (`type`, `sub_type`, `is_array`,
  `is_set`, `is_map`, `value_type`, `value_sub_type`), then the optional `default_value`, and for a member variable
  `category`, `comment`, the flags `instance_editable`, `read_only`, `expose_on_spawn`, `private`, `replicated` (each
  only when true) and `rep_notify`. They are written only by a capture that asks for them
  (`FN2CCaptureOptions::IncludeVariables`): a whole-Blueprint capture (`N2C::CaptureGraphs`, get-blueprint-n2cjson
  without a graph) does by default, a translation or one graph's capture does not, so its output is unchanged. A reader
  that predates it keeps both as unknown fields; this reader skips an entry that is not an object or has no `name`
  with a `SKIPPED_VARIABLE` warning.
- Card 0240 (agent document only; the contract and its version are unchanged): the validator reports findings as
  diagnostics, and a capture fails only on an error (a structural problem: a dangling flow, a duplicate ID, a missing
  name). The agent document of get-focused-blueprint, get-blueprint-n2cjson and find-nodes-in-graph lists the capture's
  warnings in an optional top-level `warnings` array of {`code`, `path`, `message`} (`N2CJsonContract::WarningsField`,
  codes in `N2CJsonContract::Diagnostic`); a translation's document never carries it, and the reader reads past it.
- Card 0239 (additive and corrective, under 2.0.0 since no release has written it): a node may carry `bound_to` (a
  component- or actor-bound event's component variable or actor), `net` ("Server", "Client" or "Multicast") and `reliable`
  (a replicated custom event) and, on a Timeline node, `timeline` {`length`, `loop`, `autoplay`, `tracks`: [{`name`,
  `kind`}]}, each written only when set, after `output_pins` (`N2CJsonContract::NodeBoundToField` and the others).
  `member_name` changes where it was wrong or empty: a custom event's is its name (was "None"), a Switch's the switched
  enum or type (its title stays the engine's, was "Switch"), a Get (array item)'s the element type, an async proxy
  node's the factory function with the factory class in `member_parent`; Timelines and async nodes are `latent`. A
  reader that predates it keeps the four fields as unknown fields.
- Card 0236 (additive and corrective, under 2.0.0 since no release has written it): each enum value carries its
  number as `value` (`N2CJsonContract::EnumValueNumberField`), and every value is listed except the generated `_MAX`
  entry and values marked Hidden (values named like "Max" or "MaxHealth" were dropped). A struct member of an object,
  class, soft, weak or interface type gets its `type_name` (a `Class` member's is the class it holds), a member of a user
  enum type is `Enum` with the enum in `type_name` (was `Byte` with none), sets and map keys get theirs too, and every
  user struct and enum a member uses is listed once. A reader that predates it keeps `value` as an unknown field; this
  reader keeps a value without one as it is.
- Card 0505 (reader only; the contract and its version are unchanged): the agent document of get-focused-blueprint,
  get-blueprint-n2cjson and Python `get_blueprint_n2c_json` writes each node's and pin's `id` as `ids` {`short`,
  `guid`} and adds the top-level `guids` table. The reader takes `ids.short` as the ID when there is no `id`
  (`N2CJsonContract::AgentIdsField`) and does not keep the GUIDs, so a document NodeToCode exported reads back with
  every node; before, it skipped every node with an Error log line and still read the document.
- If the owner chooses option 2 or 3, a new record supersedes this one; the data-edge fix itself stays, since it only
  adds an array where 1.0.0 dropped edges.

## Proposed amendment: reader policy for later 2.x documents (card 0445, 2026-10-25)

Proposed, not decided: it waits for the owner on card 0117 with the rest of this record. Card 0445 (epic 0442,
material graphs) implements it so the 2.0.0 reader is forward-compatible before 2.0.0 stable freezes the contract;
otherwise every later 2.x document (a material capture with `metadata.asset_kind`, a new node or pin kind) would be
rejected or lose nodes and pins in a 2.0 build and in Python, and could only ship as 3.0.0.

1. **Unknown kinds are kept.** An unknown `metadata.blueprint_type`, `graph_type`, node `type` or pin `type` is read as
   the `Unknown` value of its enum (`EN2CBlueprintType`, `EN2CGraphType`, `EN2CNodeType`, `EN2CPinType`), with its text
   kept (`UnknownBlueprintType`, `UnknownGraphType`, `UnknownType`) and written back by `ToJson` in place of "Unknown".
   The document is still read, the graph, node or pin is kept, and so are the flows that name it. Card 0234 reuses
   `EN2CNodeType::Unknown` for a captured node class with no mapping rather than adding a second value.
2. **Unknown fields are kept.** A field the reader does not know, at the top level or in `metadata`, a graph, a node
   or a pin, is kept as condensed JSON text (`UnknownFields`) and written back after the known fields of its object, in
   the order read, never replacing a field the writer sets. A document whose unknown fields come after the known ones
   (as a later writer that only appends fields produces) therefore round-trips byte for byte.
3. **Version.** Item 6 above maps 1.x to 2.0.0; a 2.x document now keeps its own version string (2.1.0 stays 2.1.0),
   because its unknown fields are kept too. A newer 2.x version gives a `NEWER_VERSION` warning. Majors outside
   1..2 are handled as before.
4. **Warnings.** Each unknown kind, unknown field, newer version and each graph, node or pin skipped as malformed (a
   missing required field) gives one structured warning, `FN2CJsonReadWarning` {code, path, value, message}, codes in
   `N2CJsonContract::ReadWarning`, from `FN2CSerializer::FromJson(Text, Blueprint, Warnings)`, plus a log warning.
   `UN2CPythonBridge::ReadN2CJson` / Python `nodetocode.read_n2c_json` return them as `warnings`; since card 0505 a
   read that skipped a graph, node or pin is not a success there (the error names them, the data keeps the rest). The
   validator accepts the Unknown kinds with a log warning.
5. **Still rejected:** text that is not JSON, and JSON without `version`, `metadata` (with its three fields) or
   `graphs`, or with no readable graph.

Checked by `NodeToCode.Serializer.ForwardCompat` (a synthetic 2.1.0 material document,
`Source/Private/Tests/Fixtures/Capture/ForwardCompat/UnknownKinds_2.1.json`, read with 16 warnings and written back
byte for byte; the 1.x fixtures and every capture golden read with none) and `NodeToCode.Python.Bridge`.
