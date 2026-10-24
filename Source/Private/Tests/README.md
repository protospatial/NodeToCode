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
  Core/  LLM/  MCP/       specs by area: <Area>/N2C<Thing>.spec.cpp
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
