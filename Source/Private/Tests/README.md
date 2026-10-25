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
  N2CTestBlueprintFactory.h/.cpp  N2CTest::FTestBlueprint: builds Blueprint graphs in code for capture specs
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
| `NodeToCode.Names` | rename-folder accepts ordinary names that start with a device name's letters | mcp-tools-contentbrowser-fs-11 |
| `NodeToCode.Names` | create-folder refuses COM1-9 and LPT1-9 like rename-folder does | mcp-tools-contentbrowser-fs-11 |
| `NodeToCode.Registry` | advertises the category of every tool through assess-needed-tools | mcp-tools-python-translation-resources-14 |
| `NodeToCode.Registry` | gives every tool a category (find-nodes-in-graph has none) | mcp-tools-python-translation-resources-14 |
| `NodeToCode.LLM.Parse.Fixtures` | Anthropic: a sentence before the fenced answer is skipped | llm-layer-12 (card 0185) |
| `NodeToCode.LLM.Parse.Fixtures` | OpenAI: an answer in a bare fence parses | llm-layer-12 (card 0185) |
| `NodeToCode.LLM.Parse.Fixtures` | Gemini: output tokens include thoughtsTokenCount | llm-layer-27 (card 0185) |
| `NodeToCode.Capture.Golden` | <every case> survives ToJson -> FromJson -> ToJson unchanged (8 cases) | translation-core-25 (card 0135) |

`NodeToCode.LLM.Parse.Fixtures` is data-driven: its cases live in `Fixtures/LLM/Parser/cases.json`, and a case with
a `known_defect` field (finding id and owning card) runs as `xIt`. The fixing card deletes that field. Set
`N2C_RUN_KNOWN_DEFECTS=1` in the editor's environment to run the disabled cases too and see which still fail.

## LLM payload goldens and parser fixtures

- `NodeToCode.LLM.Payload.Golden` builds every provider's request (body, endpoint and headers, with a placeholder
  key) for every registry model that is not retired, plus an explicit Max Output Tokens case and an unregistered
  model ID, and compares it with `Goldens/LLM/Payload/<provider>.json`. It also checks the payload rules from the
  registry flags (the same rules `Tests/mcp_probe/live_provider_matrix.py` checks on live requests). A new registry
  model fails until it has a golden. After an intended wire-format change, run
  `.\build.ps1 -UEVersion 5.8 -Test -TestFilter NodeToCode.LLM.Payload.Golden` with `N2C_UPDATE_GOLDENS=1` in the
  environment, then review the golden diff: every change there is a change to what goes on the wire.
- `NodeToCode.LLM.Parse.Fixtures` runs each provider's parser on the bodies in `Fixtures/LLM/Parser/<provider>/`
  (success, fenced output, truncation, refusal, 400/401/429/503/529 errors, string-typed errors, malformed
  `graph_name`). Error bodies go through `FN2CTranslationError::WrapHttpErrorBody` first, as the HTTP handler sends
  them. No fixture may hold a real key; the spec fails on `sk-` or `AIza` in a fixture.

## Capture goldens

`NodeToCode.Capture.Golden` (`Capture/N2CCaptureGolden.spec.cpp`) pins today's N2C JSON v1 capture of the graph
shapes below, so the E10 capture rewrite (cards 0133-0138) and the capture fixes (0130, 0234-0236) show every output
change they make. Each case builds a graph in code with `N2CTest::FTestBlueprint` (`N2CTestBlueprintFactory.h`: a
transient Actor Blueprint named `BP_N2CGolden_<case>` in its own `/Game/N2CTest_<guid>` package, never saved or
registered; under `/Game` because capture only follows functions, macros, structs and enums whose path contains
`/Game/` or `/Content/`, translation-core-21, card 0235), runs what the editor runs
(`FN2CNodeCollector::CollectNodesFromGraph`, `FN2CNodeTranslator::GenerateN2CStruct`, `FN2CSerializer::ToJson`),
replaces the package path, GUIDs and the session-dependent `Var_<graph>_<index>` member name with `<PACKAGE>`,
`<GUID>` and `<OBJECT_INDEX>`, and compares the result with `Goldens/Capture/<case>.json`. On a mismatch it reports
the first differing line and writes the whole output to `<Project>/Saved/Automation/N2CCaptureGolden/<case>.actual.json`.

- **Core shapes (card 0126):** `FanOutData` (one output into three inputs), `Knots` (an exec knot and a two-knot data
  chain that fans out), `Branch`, `SwitchOnInt` (cases 0, 1 and Default, two of them into one node), `SwitchOnEnum`
  (`EComponentMobility`), `MakeBreakStruct` (Make and Break Vector), `GateMultiExec` (three events into Enter, Open
  and Close of the StandardMacros Gate) and `TimelineMultiExec` (Play and Reverse of an empty timeline).
- **Harder shapes (card 0127):** `UserStruct` (a user struct with an Actor object member, an int and a user enum
  member, through Break and Make), `UserEnum` (Switch on a user enum with the entries Min, Max and MaxHealth),
  `Recursion_Depth1` and `Recursion_Depth2` (Root calls Child and itself, Child calls Grandchild, Grandchild calls
  Root, captured from Root), `CopyPaste` (a node and a copy pasted the way the editor pastes: new node GUID, pin IDs
  kept), `NestedGraphs_Depth0` and `NestedGraphs_Depth1` (a collapsed graph and an instance of the Blueprint's own
  macro) and `EnhancedInputEvent` (the action event of a transient `UInputAction`, made by reflection so the plugin
  needs no EnhancedInput dependency; EnhancedInput is on by default in every host). User structs, enums and input
  actions are created in the test package (`AddUserStruct`, `AddUserEnum`, `AddEnhancedInputAction`), not as assets.
- **Max Translation Depth:** a case may set `UN2CSettings::TranslationDepth` for its capture; the spec puts it back
  afterwards and the golden records it as `translation_depth`. The `Hygiene` cases check that the depth is put back
  and that a test package with a user struct, enum and input action is gone after garbage collection (the factory
  makes user types outside the editor's undo history and clears the flags of the `STRUCT_REINST_` copies the struct
  compiler leaves behind; either would keep the package alive).
- **Known defects are pinned, not hidden.** Each golden's `known_defects` lists the findings its output shows and the
  card that changes them: lossy data edges and knot fan-out (translation-core-1, card 0135), node-pair exec flows
  (translation-core-2, 0136), ghost default events (translation-core-18/19, 0137), global pin IDs and traversal-order
  node IDs (translation-core-7, 0134), display-text names (gap-5, 0138), Break Struct captured as a Variable named
  `None` (translation-core-4, 0234), custom events with member_name `None` and Switch nodes named `Switch`
  (translation-core-16 and -32, 0239). From card 0127: the depth limit never applies past the first level, so depth 1
  and 2 capture the same graphs (translation-core-3, qw-12, card 0130); a function that calls itself is captured twice
  with every pin of the copy sharing one ID (translation-core-9, 0134); a pasted node's pins all share one ID because
  pin IDs are keyed by pin GUID (translation-core-7, 0134); a user struct with an object member is dropped from
  `structs` (translation-core-8, 0236); enum values whose display name contains `max` are dropped
  (translation-core-11, 0236); a struct or enum met twice logs `Missing name` (translation-core-24, 0236); collapsed
  graphs and user macros are dropped at depth 0 (imp-translation-core-5, 0235), their nodes do not name the captured
  graph (imp-translation-core-4, 0235) and a macro without `macro` in its name is an EventGraph (translation-core-20,
  0235); the Enhanced Input event is typed `CallFunction` (translation-core-15, 0234). The list lives in the case
  table in the spec and is written into the golden. Error log lines that a known defect writes (`Duplicate pin ID`,
  `Missing name`, `Member Owner requires a type name`) are the case's `ExpectedErrors`: the case fails if they stop
  appearing, so the fixing card updates the case.
- **Round trip:** `drops exec pins, structs and enums in ToJson -> FromJson -> ToJson today` pins the lossy reader
  (translation-core-25): `FromJson` needs a pin `type` that `ToJson` never writes for exec pins and never reads
  structs or enums. Card 0135 deletes that case and enables `survives ToJson -> FromJson -> ToJson unchanged`.
- **Updating:** a change to capture output regenerates the goldens in the same commit and its message explains the
  golden diff: `.\build.ps1 -All -Test -TestFilter NodeToCode.Capture -UpdateGoldens` (the editor gets
  `-N2CUpdateGoldens`; `N2C_UPDATE_GOLDENS=1` in its environment does the same), then the same command without
  `-UpdateGoldens` to check every engine agrees. Engines run 5.6, 5.7, 5.8 in that order and each writes
  `<case>.<engine>.json` when that file exists, else `<case>.json`.
- **Per-engine goldens:** `<case>.<engine>.json` replaces `<case>.json` on that engine only where the engine really
  differs. Today: `Branch`, `CopyPaste`, `EnhancedInputEvent`, `GateMultiExec`, `NestedGraphs_Depth1`,
  `SwitchOnEnum`, `SwitchOnInt`, `TimelineMultiExec` and `UserEnum` have a `.5.8.json`, because UE 5.8's
  `UK2Node_CallFunction::GetUserFacingFunctionName` gives the menu title the function's own name (`CollectGarbage`)
  where 5.6 and 5.7 give its friendly name (`Collect Garbage`). Calls to the Blueprint's own functions are the same
  on every engine. Card 0138's stable identifiers remove the difference. To add one, copy `<case>.json` to
  `<case>.<engine>.json` and regenerate on that engine.
- **Locale:** `build.ps1 -Test` runs the editor with `-culture=en`; the goldens are recorded that way.
  `-TestCulture de` runs once in another culture. With `de` (checked on 5.6, 5.7 and 5.8, the same on each) 9 of the
  16 cases differ, only in display text: the function return node's name (`Add Return Node...`), the ghost events'
  comment (`This node is disabled ...`) and the entry tunnel's name (`Inputs`); node titles such as Branch and Switch
  and all pin names stay English. All of it is card 0138's.

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
