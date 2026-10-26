# NodeToCode automation tests

The plugin's C++ tests are Unreal Automation tests compiled into the `NodeToCode` editor module. They live here,
inside `Source/`, because the build hosts junction only `Source`, `Content`, `Resources` and `ThirdParty`: a test in a
top-level folder would never be built. (A later card moves them into a separate tests module.) Live MCP probes and
the headless editor helper are Python and live in `Tests/mcp_probe/`; the smoke check is `Tests/smoke/`.

## Layout and naming

```
Source/Private/Tests/
  README.md               this file
  N2CTestHelpers.h        shared helpers (N2CTest namespace) and N2C_TEST_FLAGS
  N2CTestHttpStub.h       N2CTest::StartStubRouter: an HTTPServer router for a local stub endpoint on a port the OS
                          picked (never a random port in a range: another checkout's editor may hold it)
  N2CTestBlueprintFactory.h/.cpp  N2CTest::FTestBlueprint: builds Blueprint graphs in code for capture specs
  N2CToolSpecHarness.h/.cpp       the MCP tool spec harness: scratch Blueprints, tool calls, results, graph invariants
  N2CTestContentFolder.h  N2CTest::DeleteContentFolder: removes a saved /Game/__N2CTest... scratch folder completely
  Capture/  Core/  LLM/  MCP/     specs by area: <Area>/N2C<Thing>.spec.cpp
  N2C*Tests.cpp           older IMPLEMENT_SIMPLE_AUTOMATION_TEST regression tests (NodeToCode.Robustness.*,
                          NodeToCode.Security.PathUtils.*, NodeToCode.MCP.Transport.*); kept as they are
  Fixtures/<Area>/        checked-in inputs a spec reads (N2CTest::FixturesDir(Area))
  Goldens/<Area>/         checked-in expected outputs a spec compares against (N2CTest::GoldensDir(Area))
```

- New tests are Automation Specs (`BEGIN_DEFINE_SPEC`) named `NodeToCode.<Area>.<Thing>`, for example
  `NodeToCode.PathPolicy`, `NodeToCode.Registry`, `NodeToCode.LLM.Parse`. Describe/It texts finish the sentence, so
  a full test path reads as a requirement: `NodeToCode.Pagination.refuses a page past the end`.
- Every test name starts with `NodeToCode.`, so `Automation RunTests NodeToCode` (what `build.ps1 -Test` runs) finds
  it.
- Use `N2C_TEST_FLAGS` (`EditorContext | EngineFilter`); it compiles on UE 5.6, 5.7 and 5.8.
- Every file here, headers included, is wrapped in `#if WITH_DEV_AUTOMATION_TESTS` ... `#endif` after including
  `Misc/AutomationTest.h`, so nothing here ships in a build without automation tests.
- Do not rename a test that a gate card, `Tests/mcp_probe/e05_regressions.py` (default filter
  `NodeToCode.Robustness+NodeToCode.Secrets.Store`) or a doc cites. Grep for the name first.
- Files a test writes go in a scratch folder under `<Project>/Saved/Automation/` (`N2CTest::FScratchDir`), never in
  the real `Saved/NodeToCode` folders or the per-user secret store, and are removed after the case.
- Fixtures are small text files. Never put API keys, tokens or a real `secrets.json` in a fixture, golden or log.
- `N2C_TEST_PRIVATE_MEMBER` reaches a private member of a product class (for example a tool's validator) without
  changing product code. Prefer a public seam when the product code changes anyway.

## Known defects

A spec case that fails because of an open finding stays in the spec, disabled: change `It` to `xIt` (or `Describe`
to `xDescribe`) and put a comment above it naming the finding id and what enables it, for example

```cpp
// Known defect mcp-tools-contentbrowser-fs-11: ValidateNewName refuses any name that starts with COM or LPT.
// Enable (xIt -> It) with the fix.
xIt(TEXT("accepts ordinary names that start with a device name's letters"), ...
```

A characterization spec (for example `NodeToCode.MCP.Protocol`) may also pin today's behaviour where it breaks a
rule, in an `It` named "... today" whose comment names the finding, the rule and the card that changes it. That
card deletes the "today" case and enables the `xIt` next to it.

List every disabled case in the PR. A test card does not fix product code; the card that fixes the finding turns the
case back on. Currently disabled:

| Spec | Case | Finding |
|------|------|---------|
| `NodeToCode.LLM.Parse.Fixtures` | Anthropic: a sentence before the fenced answer is skipped | llm-layer-12 (card 0185) |
| `NodeToCode.LLM.Parse.Fixtures` | OpenAI: an answer in a bare fence parses | llm-layer-12 (card 0185) |
| `NodeToCode.LLM.Parse.Fixtures` | Gemini: output tokens include thoughtsTokenCount | llm-layer-27 (card 0185) |
| `NodeToCode.MCP.Graph` | connect-pins reports a malformed connection entry in failed and counts it | mcp-tools-graph-functions-25 (card 0276) |
| `NodeToCode.MCP.Graph` | delete-blueprint-node with preserveConnections keeps the exec chain through a deleted Print String | mcp-tools-graph-functions-12 (card 0279) |
| `NodeToCode.MCP.Variables` | create-variable and create-local-variable round trips: class reference to Actor, soft object reference to Actor | mcp-tools-variables-15 (card 0292) |
| `NodeToCode.MCP.Variables` | get-blueprint-member-variables read-back: float, double | mcp-tools-variables-8 (card 0283) |
| `NodeToCode.MCP.Variables` | get-blueprint-member-variables read-back: the four maps | mcp-tools-variables-8 (card 0283) |
| `NodeToCode.MCP.Variables` | get-blueprint-member-variables read-back: class and soft object reference to Actor | mcp-tools-variables-15 (card 0292) |
| `NodeToCode.MCP.Variables` | search-variable-types returns Blueprint classes as identifiers the resolver accepts | mcp-tools-variables-23 (card 0291) |
| `NodeToCode.MCP.Structure` | search-blueprint-classes lists each native class once | mcp-tools-bp-structure-12 (card 0288) |
| `NodeToCode.MCP.Structure` | search-blueprint-classes lists a loaded Blueprint class once, as a Blueprint class | mcp-tools-bp-structure-12 (card 0288) |
| `NodeToCode.MCP.Content` | folders: move-folder and rename-folder move an empty folder and remove the source | mcp-tools-contentbrowser-fs-10 (card 0281) |
| `NodeToCode.MCP.Content` | folders: move-folder keeps an empty subfolder | mcp-tools-contentbrowser-fs-10 (card 0281) |
| `NodeToCode.MCP.Content` | folders: move-folder refuses to move a folder into itself or its own subfolder, changing nothing | mcp-tools-contentbrowser-fs-9 (card 0281) |

`NodeToCode.LLM.Parse.Fixtures` is data-driven: its cases live in `Fixtures/LLM/Parser/cases.json`, and a case with
a `known_defect` field (finding id and owning card) runs as `xIt`. The fixing card deletes that field. Set
`N2C_RUN_KNOWN_DEFECTS=1` in the editor's environment to run the disabled cases too and see which still fail.

## MCP tool specs and the harness

`N2CToolSpecHarness.h` (card 0147) is the one way the `NodeToCode.MCP.*` specs build a Blueprint, call a tool and
check what it did. Use it for any new tool spec instead of a local copy:

- **Scratch Blueprint:** `N2CTest::FToolSpecBlueprint` is an `FTestBlueprint` (all its graph helpers) in its own
  package under `/Temp` (or `/Game` when the code under test only treats `/Game` content as the user's), never saved
  or registered. Its package's `RF_Transient` is cleared so `MarkPackageDirty` applies and a spec can check an edit
  dirties the package; `Settle()` compiles and cleans it for a baseline. Its destructor closes the editor it opened,
  cleans the package, gives `RF_Transient` back and resets the editor's undo buffer, so `AfterEach` only resets it.
- **Calling tools:** `N2CTest::RunTool(name, args)` runs the tool through `FN2CMcpToolManager::ExecuteTool`, as
  `tools/call` does: one `FN2CMcpToolCallScope` per call (one compile, diagnostics in the result's `compile`
  object) and the untrusted-content mark for tools that declare `bReturnsUntrustedContent`. A tool missing from the
  active tool set is registered for the call. `RunToolUnmarked` skips the mark, for a spec that compares the text
  byte for byte with what the tool builds (`NodeToCode.MCP.BlueprintByPath`). `InToolCall(lambda)` runs code that a
  tool would run (a tool's static helper) inside one call scope and returns the compiles. Arguments:
  `TargetArgs(Blueprint, Graph)` (`blueprintPath` + `graphGuid`), `PinRef`, `ConnectArgs`.
- **Results:** `ResultJson`, `ResultTarget`, `ResultCompile`, `IsMarkedUntrusted`; `ExpectResultTargets` checks the
  result names the intended Blueprint and graph, `ExpectToolCompiled` that it succeeded and its compile has no
  errors.
- **Invariants:** `Snapshot(Blueprint)` writes every graph's nodes, pins (ID, type, default, flags, links) and
  `UserDefinedPins`, and the Blueprint's variables, timelines and component templates as text;
  `ExpectUndoRedoExact` undoes the last transaction and compares with the snapshot from before the edit, then redoes
  it and compares with the edited one, reporting the first differing line. `ExpectSymmetricLinks` (every `LinkedTo`
  links back, to a node still in its graph), `ExpectCompilesClean` (zero compile errors), `FCompileCounter`,
  `UndoQueueLength`, `AllLinks`, `HasUserPin`, `ExecIn`/`ExecOut`, `FindNodeByGuid`.
- **Headless limits:** the Graph, Functions and Variables tools take `blueprintPath` with `graphName` or `graphGuid`
  (card 0152), so their specs need no editor and run on every engine. A case that needs the focused graph (the
  Python bridge, the variable node tools, `get-focused-blueprint`) calls `FToolSpecBlueprint::OpenInEditor`, which
  opens and focuses the graph where `N2C_CAN_CLOSE_HEADLESS_BLUEPRINT_EDITOR` is 1 (UE 5.8) and elsewhere adds the
  reason as info and returns false: under `-nullrhi`, UE 5.6 and 5.7 hit a fatal error in `GenericWindow.cpp` when a
  Blueprint editor window is closed. Such cases are compiled under `#if N2C_CAN_CLOSE_HEADLESS_BLUEPRINT_EDITOR`
  with a skip case naming the reason on the other engines.

`NodeToCode.MCP.Graph` (`MCP/N2CMcpGraphTools.spec.cpp`) and `NodeToCode.MCP.Functions`
(`MCP/N2CMcpFunctionTools.spec.cpp`) run the Graph and Functions tools that way and check, for each edit, a dirty
package, one undo step, symmetric links, a clean compile and an exact Undo then Redo: a context-sensitive
search-blueprint-nodes on an explicit event graph and function graph changes nothing (nodes, pins, links,
timelines, components, undo buffer, dirty flag) and compiles nothing; add-bp-node-to-active-graph; connect-pins
keeps exec fan-in and refuses to replace an exec output's link without `breakExistingLinks`
(`WOULD_BREAK_EXISTING_LINK`), replacing it with it; delete-blueprint-node removes a wired node's links on both sides
and refuses an unknown GUID and a function entry; and add-function-input-pin, add-function-return-pin,
remove-function-entry-pin and remove-function-return-pin on a function with two Return nodes. What they leave to
other specs is listed in each spec's header comment (NodeActionIds, SearchIsReadOnly, GraphTarget, CompileOnce,
CompileScope, SpawnedNodes, FunctionPinEdits).

`NodeToCode.MCP.Variables` (`MCP/N2CMcpVariableTools.spec.cpp`, card 0148) is a type table: bool, int, int64, byte,
float, double, string, name, text, a native enum (by path and name), a user enum and a user struct (by name and by
path, in a `/NodeToCode`-mounted package, so plugin content), a native and a math struct, objects by name and path, a
Blueprint class, a plugin native class (EnhancedInput `InputAction`), an interface, soft object and soft class,
arrays, sets and maps. Each row runs what create-variable runs once it has the Blueprint (argument parsing,
`ResolvePinType`, `CreateMemberVariable`) in one tool call and checks the resolved type, the variable description
and `UEdGraphSchema_K2::ConvertPropertyToPinType` of the compiled property against the type the editor makes, after
the call's compile and a second one; the default value on the class defaults; one exact Undo and Redo; and Get and
Set nodes with a unique GUID, one set of pins and a value pin of that type. The same rows run as local variables
(the compiled function's property) and through get-blueprint-member-variables' type description read back into
create-variable arguments. Refused inputs (wildcard, exec, delegates, template spellings, an over-long name or type
through both tools) return an error and change nothing, and every class, struct and enum the editor's variable type
tree offers resolves by path to itself. On UE 5.8 the rows also run through the create-variable tool on a focused
Blueprint, and search-variable-types results are resolved. A row's `CreateDefect`/`ReadBackDefect` field (finding
id and card) makes it an `xIt`; `N2C_RUN_KNOWN_DEFECTS=1` runs those too. Flags, OnRep and their undo are
`NodeToCode.MCP.VariableFlags`; the node tools' wiring and undo are `NodeToCode.MCP.SpawnedNodes`; the resolver's
error codes, prefixes and ambiguity are `NodeToCode.MCP.TypeResolver`.

`NodeToCode.MCP.CreateBlueprintClass` (`MCP/N2CCreateBlueprintClass.spec.cpp`, card 0166) runs create-blueprint-class
for real: it saves Blueprints under `/Game/__N2CTest` (an existing folder, a UserWidget parent giving a
`UWidgetBlueprint`, a Blueprint interface, `ifExists` error and unique) and checks that refused calls (an AnimInstance
parent, wrong type and parent pairs, unknown types, invalid names) create neither the folder nor an asset. Each case
deletes the folder and everything in it before and after it runs.

`NodeToCode.MCP.Components` (`MCP/N2CComponentTools.spec.cpp`, card 0167) runs add-component-class-to-actor,
delete-component-in-actor and list-components-in-actor on transient Actor, Character, child and macro-library
Blueprints: a component under a parent that is not the root stays there after compiles, a refused add (misspelled
parent, its own name as parent, missing socket, taken name, non-scene component with a parent) changes nothing,
classes resolve by short name and component Blueprint path, `Mesh` on a Character and an inherited component take
children, a delete promotes children and removes Get nodes, a bound event is reported without opening an editor,
DefaultSceneRoot and native and inherited components are refused, and each edit is one undo step that Undo then Redo
restore exactly (graphs and SCS tree).

`NodeToCode.MCP.Structure` (`MCP/N2CMcpStructureTools.spec.cpp`, card 0163) runs the structure tools end to end, the
way an agent chains them: create-folder, then create-blueprint-class of an Actor, a Widget (a `UWidgetBlueprint`) and
an Interface Blueprint into that existing folder; a SpringArm and a Camera under it on a created Blueprint, in the
SpringArm's `ChildNodes` after three compiles; a delete of a mid-hierarchy component whose children are promoted with
their own children and whose SCS `AllNodes` stays equal to the nodes reachable from the roots; list-components-in-actor
on a child of a Character Blueprint (native, inherited and own components once each, under their parents, `totalCount`
counting all); and search-blueprint-classes without duplicates. Its Blueprints are saved under
`/Game/__N2CTestStructure`.

`NodeToCode.MCP.Content` (`MCP/N2CMcpContentTools.spec.cpp`, card 0163) runs move-asset and rename-asset with package
and object paths, moves into and out of a project plugin's content mount, move-folder and rename-folder of empty and
populated folders, and move-folder into itself. "asset names and paths" (card 0284) checks that names are validated as
given (`BP_New.Final` refused), failed copies leave no folder, case-only renames are refused, and results are read
back from the asset registry. The test plugin is `<Project>/Plugins/N2CTestContent` (content only,
explicitly loaded), written, mounted at `/N2CTestContent` and removed again by the case. Its assets are saved under
`/Game/__N2CTest`.

Both delete their folders (assets, the redirectors a move leaves, files and registry paths;
`N2CTest::DeleteContentFolder` in `N2CTestContentFolder.h`) before and after every case, also a failed one. Their
known-defect cases are `KnownDefectIt`: an `xIt`, or an `It` with `N2C_RUN_KNOWN_DEFECTS=1`.

## Secrets, settings migration and StructUtils

- `NodeToCode.Secrets.Keychain` (`Core/N2CSecretStoreKeychain.spec.cpp`, card 0162) runs `FN2CSecretStore` over
  Windows Credential Manager as the editor does: every provider's key through a restart, the largest key it takes,
  two stores loaded before either saves (neither undoes the other), twelve writers on several threads, entries it does
  not know (`Mistral`, `OpenAI.Conflict.1`) kept and never read as a provider's key, and the environment fallback
  (never saved). Each case uses the target prefix `NodeToCodeTest/<guid>/`, removes it, and checks that the user's
  own `NodeToCode/*` entries are unchanged. `NodeToCode.Secrets.Store` and `.Environment` are the file backend's
  versions; `.CredentialManager` covers the backend's blob format and the `secrets.json` migration's failure paths.
- `NodeToCode.Settings.Migration` (`Core/N2CSettingsMigration.spec.cpp`) upgrades `Fixtures/Settings/Legacy12x`, a
  1.2.x project: `DefaultNodeToCode.ini` with every config key of main's `UN2CSettings`, the `DefaultEngine.ini`
  1.2.x wrote, and a 1.2.x project `secrets.json`. Every key has a fate in the spec's table (kept, migrated, model ID,
  pricing, theme); each value is checked after the upgrade and again after a restart, and every key lands in the
  per-user store. A key added to the fixture without a fate fails the spec.
- `NodeToCode.StructUtils` (`Core/N2CStructUtils.spec.cpp`) calls `UN2CStructUtilsLibrary` with bad package paths and
  names (refused, nothing created), a taken name (refused, the struct there kept), the variable listing, and a save
  to a read-only file (returns false, auth-settings-module-20). Its structs live under
  `/Game/__N2CTestStructUtils_<guid>`, deleted after each case.

## LLM payload goldens and parser fixtures

- `NodeToCode.LLM.Payload.Golden` builds every provider's request (body, endpoint and headers, with a placeholder
  key) for every registry model that is not retired, plus an explicit Max Output Tokens case and an unregistered
  model ID, and compares it with `Goldens/LLM/Payload/<provider>.json`. It also checks the payload rules from the
  registry flags (the same rules `Tests/mcp_probe/live_provider_matrix.py` checks on live requests). A new registry
  model fails until it has a golden. After an intended wire-format change, run
  `.\build.ps1 -UEVersion 5.8 -Test -TestFilter NodeToCode.LLM.Payload.Golden` with `N2C_UPDATE_GOLDENS=1` in the
  environment, then review the golden diff: every change there is a change to what goes on the wire.
- `NodeToCode.LLM.Prompts` loads every CodeGen prompt through `UN2CSystemPromptManager` and checks that each holds
  the shared `Content/Prompting/Shared/N2CJsonSpec.md` and `N2CResponseFormat.md` exactly once and no placeholder,
  that the spec names every key `FN2CSerializer` writes and the current version, that the response contract names
  every property of `GetN2CResponseSchema`, that the batch export's `Combined_Blueprints.md` holds the same spec, and
  that `WrapN2CJson` puts the user message in `<n2c_json>` tags no node comment can close. It keeps no golden of the
  assembled prompts: that would be a second copy of the spec.
- `NodeToCode.MCP.UntrustedContent` (card 0124) checks that Blueprint text reaches an MCP client marked as untrusted
  data on each channel: the `untrusted_content` mark and `</` escaping (`FN2CMcpUntrustedContent`), `ExecuteTool`
  marking the result of a tool that sets `bReturnsUntrustedContent` (and the exact list of tools that set it), the
  current-Blueprint resource, and the Blueprint prompts (notice message, then the annotated embedded resource). It
  also checks prompt titles, the language validation and generate-code's CodeGen prompt, and python-scripting's
  disabled notice.
- `NodeToCode.LLM.Parse.Fixtures` runs each provider's parser on the bodies in `Fixtures/LLM/Parser/<provider>/`
  (success, fenced output, truncation, refusal, 400/401/429/503/529 errors, string-typed errors, malformed
  `graph_name`). Error bodies go through `FN2CTranslationError::WrapHttpErrorBody` first, as the HTTP handler sends
  them. No fixture may hold a real key; the spec fails on `sk-` or `AIza` in a fixture.

## Capture goldens

`NodeToCode.Capture.Golden` (`Capture/N2CCaptureGolden.spec.cpp`) pins today's N2C JSON (2.0.0 since card 0135) capture
of the graph shapes below, so the E10 capture rewrite (cards 0133-0138) and the capture fixes (0130, 0234-0236) show
every output change they make. Each case builds a graph in code with `N2CTest::FTestBlueprint`
(`N2CTestBlueprintFactory.h`: a transient Actor Blueprint named `BP_N2CGolden_<case>` in its own `/Game/N2CTest_<guid>`
package, never saved or registered; under `/Game` because capture only follows functions, macros, structs and enums
whose path contains `/Game/` or `/Content/`, translation-core-21, card 0235), runs what the editor runs
(`FN2CNodeCollector::CollectNodesFromGraph`, `N2C::CaptureNodes`, `FN2CSerializer::ToJson`), replaces the package path,
GUIDs with `<PACKAGE>` and `<GUID>` (nothing else may change between sessions: card 0234 removed the `Var_<graph>_<index>`
member name and its `<OBJECT_INDEX>` token), and compares the result with `Goldens/Capture/<case>.json`. On a mismatch it reports the first differing line and writes the
whole output to `<Project>/Saved/Automation/N2CCaptureGolden/<case>.actual.json`.

- **Core shapes (card 0126):** `FanOutData` (one output into three inputs), `Knots` (an exec knot and a two-knot data
  chain that fans out), `FanOutActorLocation` (card 0135: GetActorLocation's Return Value into To String (Vector) and
  SetActorLocation's New Location, the fan-out of the card's "Done looks like"), `Branch`, `SwitchOnInt` (cases 0, 1 and
  Default, two of them into one node), `SwitchOnEnum` (`EComponentMobility`), `MakeBreakStruct` (Make and Break Vector),
  `GateMultiExec` (three events into Enter, Open and Close of the StandardMacros Gate) and `TimelineMultiExec` (Play and
  Reverse of an empty timeline).
- **Exec pins (card 0136):** `BranchSameTarget` (True and False into one node), `Sequence` (then_0 and then_1) and
  `ForEachLoop` (the StandardMacros loop over an int array input: Loop Body and Completed).
- **Harder shapes (card 0127):** `UserStruct` (a user struct with an Actor object member, an int and a user enum
  member, through Break and Make), `UserEnum` (Switch on a user enum with the entries Min, Max and MaxHealth),
  `Recursion_Depth1` and `Recursion_Depth2` (Root calls Child and itself, Child calls Grandchild, Grandchild calls
  Root, captured from Root), `CopyPaste` (a node and a copy pasted the way the editor pastes: new node GUID, pin IDs
  kept), `NestedGraphs_Depth0` and `NestedGraphs_Depth1` (a collapsed graph and an instance of the Blueprint's own
  macro) and `EnhancedInputEvent` (the action event of a transient `UInputAction`, made by reflection so the plugin
  needs no EnhancedInput dependency; EnhancedInput is on by default in every host). User structs, enums and input
  actions are created in the test package (`AddUserStruct`, `AddUserEnum`, `AddEnhancedInputAction`), not as assets.
- **Depth limit (card 0130):** `CallChain_Depth1` and `CallChain_Depth2` (A calls B calls C calls D, captured from A:
  depth 1 adds B, depth 2 adds B and C, D never) and `ShortestPath_Depth3` (a graph reachable 2 and 3 calls away is
  taken at depth 2, so its callee at depth 3 is captured and the one at depth 4 is not). Max Translation Depth counts
  calls from the captured graph, and nested graphs are processed shallowest first (translation-core-3, qw-12).
- **Identity (card 0134):** `SameNameOwners` (Run calls its own Helper and Init; Helper calls Init of a second
  Blueprint, `BP_N2CGolden_SameNameOwners_Other`, made in the same package with `AddSiblingBlueprint`; depth 2) keeps
  both Init graphs, the other Blueprint's with `owner_blueprint`. The `Identity` tests check that a pasted node's
  pins get IDs of their own and the capture validates (`CopyPaste`), that Root is captured once (`Recursion_Depth1`
  and `_Depth2`), the owners in `SameNameOwners`, that capturing the unchanged `ShortestPath_Depth3` graph twice gives
  the same JSON and `guids` table, and that `FN2CCapture::NodeGuids`/`Pins` and get-focused-blueprint's result
  (`FN2CMcpGetFocusedBlueprintTool::BuildResultJson`) have one entry per node and per pin.
- **Isolation (card 0133):** captures share no state. `gives each of two interleaved captures its single-run golden`
  starts `CallChain_Depth1` in the middle of `ShortestPath_Depth3` (after its first node, through the test-only
  `FN2CCaptureContext::SetNodeCapturedHookForTests`) and compares both with their goldens; `keeps a capture's result
  when another graph is captured after it` captures `UserStruct`, then `NestedGraphs_Depth1`, then serializes the
  first result. `NodeToCode.Robustness.DescribeNode` (`Capture/N2CDescribeNodeTests.cpp`) checks that
  `N2C::DescribeNode` never queues nested graphs or leaks into the next capture.
- **Max Translation Depth:** a case may set `UN2CSettings::TranslationDepth` for its capture; the spec puts it back
  afterwards and the golden records it as `translation_depth`. The `Hygiene` cases check that the depth is put back
  and that a test package with a user struct, enum and input action is gone after garbage collection (the factory
  makes user types outside the editor's undo history and clears the flags of the `STRUCT_REINST_` copies the struct
  compiler leaves behind; either would keep the package alive).
- **Known defects are pinned, not hidden.** Each golden's `known_defects` lists the findings its output shows and the
  card that changes them: custom events with member_name `None` and Switch nodes named `Switch` (translation-core-16 and -32, 0239). Card 0134
  fixed and removed the ID defects (translation-core-7: pin IDs keyed by pin GUID, so a pasted node's pins shared one
  ID, and node IDs that depended on traversal order; translation-core-9: a function that calls itself was captured
  twice). Card 0135 fixed and removed the lossy data edges (translation-core-1: `flows.data` kept one edge per output,
  and a knot's fan-out only its first link). Card 0136 fixed and removed the node-pair exec flows (translation-core-2:
  `flows.execution` held "N1->N2" node pairs, so which pin a link used was lost and two outputs into one node were one
  entry). Card 0137 fixed and removed the ghost default events (translation-core-18/19: the greyed-out BeginPlay,
  ActorBeginOverlap and Tick of every Actor Blueprint's event graph were captured as live events N1-N3, so every
  event-graph golden lost those three nodes and its IDs moved down by three). Card 0138 fixed and removed the
  display-text names (gap-5: node names were menu titles and pin names display names, blank for exec pins, so they
  changed with the editor language, the friendly-names preference and the engine; translation-core-22: a variable's
  member_parent was its type, `real/double`; translation-core-28: `GetCleanClassName` cut `_C` off member names).
  From card 0127: a user struct with an object member is dropped from
  `structs` (translation-core-8, 0236); enum values whose display name contains `max` are dropped (translation-core-11,
  0236); a struct or enum met twice logs `Missing name` (translation-core-24, 0236). Card 0235 fixed and removed the
  nested-graph defects (imp-translation-core-5: collapsed graphs were dropped at depth 0; imp-translation-core-4: the
  Composite node did not name its graph; translation-core-20: a macro without `macro` in its name was an EventGraph).
  Card 0234 fixed and removed Break Struct captured as a Variable titled `None` with a session-dependent member_name
  (translation-core-4) and the Enhanced Input event typed `CallFunction` (translation-core-15). The list lives in the case table in the
  spec and is written into the golden. Error log lines that a known defect writes (`Missing name`, `Member Owner
  requires a type name`) are the case's `ExpectedErrors`: the case fails if they stop appearing, so the fixing card
  updates the case.
- **Graph kinds and nested graphs (card 0235):** `ConstructionScript` (the UserConstructionScript is `Construction`),
  `MacroGraph_Depth1` (a macro named DoDamage is `Macro`), `NestedGraphs_Depth0` (the collapsed graph is captured at
  depth 0 and its Composite node names it in `member_name`), `PluginContent_Depth1` (a function of a Blueprint under
  the plugin's own `/NodeToCode` mount, made with `AddBlueprintAt`, is followed and its user struct emitted) and
  `CommentBoxes` (`comments`: a box around two nodes lists their IDs, an empty box only its text). `Discovery` checks
  a null entry in `UEdGraph::Nodes` (main, nested and listed graph), an anim graph typed `Animation` by its schema, and
  that a StandardMacros ForEachLoop (engine content) adds no graph at depth 2.
- **Node types (card 0234):** `NodeTypes` (Set members in Vector, For Each (Map), For Each (Set), a local
  variable's Get and Set, an unwired To Text (`K2Node_GenericToText`), and two test-only classes from
  `Capture/N2CTestNodeClasses.h`: `K2Node_N2CTestUnmapped`, a UK2Node subclass with no mapping, and
  `K2Node_N2CTestCallFunction`, a CallFunction subclass with none of its own). The `NodeTypes` tests check the
  registry's nearest-ancestor rule class by class (Enhanced Input's classes found by name) and, in the captures, that
  Break Struct is `BreakStruct` named by its struct, Set members is `SetFieldsInStruct` and not titled Make, the
  Enhanced Input event is `InputAction` named by its action, both map and set loops are `ForEachLoop`, a local
  variable's Get and Set are `LocalVariableGet` and `LocalVariableSet`, and the unmapped class is `Unknown` with its
  `class`.
- **Liveness (card 0137, issue #13):** `Issue13` (the issue's graph: one output into To Text (String) and a Format
  Text whose result nothing reads), `GhostEvents` (the default BeginPlay wired, Overlap and Tick left as ghosts),
  `DisabledNodes` (a disabled Print String between two calls, a Development Only call, a disabled Branch and the call
  after it) and `UnreachableChain` (an impure chain with no event and the pure node only it reads). The `Liveness`
  tests check that ghost nodes are not captured (an untouched event graph captures nothing), disabled nodes carry
  `"disabled": true` and exec passes through one with one exec input and one exec output (and stops at any other),
  pure nodes nothing live reads carry `"unused": true`, impure nodes no entry reaches `"unreachable": true`, that no
  other case marks a node, that the flags round-trip, and that the validator warns about a connected data input with
  no data flow into it.
- **Stable names (card 0138):** `StableNames` (function UseNames calls the Blueprint's own `Convert_C` and returns
  `Health` of a `BP_N2CGolden_Enemy` reference (a sibling Blueprint) plus the Blueprint's own `Temperature_C`). The
  `StableNames` tests check that `Health` has member_parent `BP_N2CGolden_Enemy` and no type string, that `Convert_C`
  and `Temperature_C` keep their names, that exec pins are named `execute` and `then`, that every node of every case
  has a `K2Node_` `class` and no `name`, every pin a non-empty `name`, and every `title` and `label` says more than its
  identifier; and that every case captures the same JSON with the editor switched to Japanese
  (`FInternationalization::SetCurrentCulture("ja")`, after checking the engine's Japanese editor text really loads)
  and the same JSON apart from `title` and `label` with `UEditorStyleSettings::bShowFriendlyNames` flipped. Both are
  put back afterwards.
- **Round trip:** every case `survives ToJson -> FromJson -> ToJson unchanged` (card 0135, translation-core-25:
  `FromJson` reads a pin without `type` as Exec, reads `structs` and `enums`, and a fan-out's array).
- **Data flows (card 0135):** `DataFlows` checks on every case that each connected data input is the target of
  exactly one `flows.data` edge, every key is an output pin and an array value has several targets, and that
  `FanOutActorLocation` and `Knots` list both targets of their fan-out.
- **Exec flows (card 0136):** `ExecFlows` checks on every case that each `flows.execution` entry is one link from an
  exec output pin to an exec input pin ("N3.P5->N4.P1"), recorded once, and that every connected exec pin is in one;
  then that `Branch`, `BranchSameTarget`, `GateMultiExec`, `Sequence`, `ForEachLoop`, `SwitchOnInt` and
  `TimelineMultiExec` have exactly the links their graphs make, named by node and pin; and that the validator
  accepts a pin-qualified link and the node-only form and refuses a data pin, a reversed link, a missing pin or node,
  a mixed entry and a pin-qualified chain.
- **v1 reader (card 0135):** `V1Reader` reads the N2C JSON 1.0.0 documents in `Fixtures/Capture/V1`: the goldens of
  `FanOutData`, `Knots` and `UserEnum` from before card 0135 and `StructsAndEnums` (FromJson then ToJson gives the
  same document with version 2.0.0), and `BP_Door_1.2.3.json` (the 1.2.3 save of `Tests/Migration`); plus an array
  fan-out written back unchanged and the readable versions (1.x and 2.x). Card 0136: a 2.0.0 document with node-only
  exec flows (development builds between cards 0135 and 0136) reads, validates and is written back unchanged, and so
  is a pin-qualified one.
- **Forward compatibility (card 0445):** `NodeToCode.Serializer.ForwardCompat` (`Core/N2CSerializerForwardCompat.spec.cpp`)
  reads `Fixtures/Capture/ForwardCompat/UnknownKinds_2.1.json`, a synthetic 2.1.0 material document with unknown
  blueprint, graph, node and pin kinds and unknown fields at every level: one structured warning each, nothing
  dropped, the validator accepts it, and condensed `ToJson` gives the file back byte for byte. The 1.x fixtures and
  every capture golden give no warning; non-JSON text and a document without version or metadata are still rejected,
  and a malformed node or graph is skipped with a `SKIPPED_*` warning. `NodeToCode.Python.Bridge` reads the same file
  through `ReadN2CJson`, and `Tests/python/test_read_n2c_json.py` checks the Python wrapper returns its warnings.
- **Updating:** a change to capture output regenerates the goldens in the same commit and its message explains the
  golden diff: `.\build.ps1 -All -Test -TestFilter NodeToCode.Capture -UpdateGoldens` (the editor gets
  `-N2CUpdateGoldens`; `N2C_UPDATE_GOLDENS=1` in its environment does the same), then the same command without
  `-UpdateGoldens` to check every engine agrees. Engines run 5.6, 5.7, 5.8 in that order and each writes
  `<case>.<engine>.json` when that file exists, else `<case>.json`.
- **Per-engine goldens:** `<case>.<engine>.json` replaces `<case>.json` on that engine only where the engine really
  differs. Today there are none: every case gives the same JSON on 5.6, 5.7 and 5.8. Until card 0138, 17 cases had a
  `.5.8.json` only because UE 5.8 titles a function call by the function's own name (`CollectGarbage`) where 5.6 and
  5.7 use its display spelling (`Collect Garbage`); the title is now a hint, left out when it is only the member name
  spelled for display, so those files went. To add one, copy `<case>.json` to `<case>.<engine>.json` and regenerate on
  that engine.
- **Locale:** `build.ps1 -Test` runs the editor with `-culture=en`; the goldens are recorded that way. Capture
  output does not depend on the editor language (card 0138): `.\build.ps1 -UEVersion <ver> -Test -TestFilter
  NodeToCode.Capture -TestCulture de` (and `fr`, and `ja`) must pass against the same goldens, and did on 5.6, 5.7 and
  5.8 (de and fr on each, ja on 5.8) when card 0138 landed. Identifiers never are display text, and the `title` and
  `label` hints and text defaults are written in source (English) text. Before card 0138, `de` changed the function
  return node's name (`Add Return Node...`) and the entry tunnel's name (`Inputs`/`Eingaben`) in 9 of 16 cases.

## Running the tests

- **All engines, headless:** `.\build.ps1 -All -Test` (or `-UEVersion 5.8 -Test`; `-TestFilter NodeToCode.PathPolicy`
  narrows it). It builds, runs `Automation RunTests <filter>` in `UnrealEditor-Cmd` on the host project, prints
  each failure and the pass count, and writes the report to `BuildLogs/Tests_<ver>_<stamp>/index.json`.
- **By hand, one engine:**
  `UnrealEditor-Cmd.exe <host>.uproject -ExecCmds="Automation RunTests NodeToCode" -TestExit="Automation Test Queue Empty" -unattended -nullrhi -nosplash -nosound -abslog=<path>.log`
  (separate several `-ExecCmds` commands with commas, never semicolons).
- **In the editor:** Tools > Session Frontend > Automation, filter `NodeToCode`, tick the tests and press Start
  Tests.
- **From an agent on UE 5.8:** enable the engine's experimental AutomationTestToolset plugin in the host project
  (never in this plugin) and call its `DiscoverTests`, then `RunTestsByFilter("StartsWith:NodeToCode")` (or `RunTests`
  with full names from `ListTests("NodeToCode")`), `GetTestStatus` and `GetTestResults` through the editor's Unreal MCP connection.
  The tool specs alone: `RunTestsByFilter("StartsWith:NodeToCode.MCP.Graph")` and
  `RunTestsByFilter("StartsWith:NodeToCode.MCP.Functions")` (the first also runs `NodeToCode.MCP.GraphTarget`).
