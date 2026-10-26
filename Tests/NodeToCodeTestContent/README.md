# NodeToCodeTestContent

A content-only plugin holding assets that NodeToCode's automation tests load. It is never shipped: `build.ps1` links
this folder into each host project's `Plugins/` (a junction on Windows, a symbolic link elsewhere), so it mounts at
`/NodeToCodeTestContent`, while RunUAT BuildPlugin packages only the NodeToCode plugin's own folders and
`Scripts/package-release.ps1` refuses any `Tests/` path in a release zip.

## Content/Perf: the perf fixtures (card 0359)

| Asset | Shape |
|---|---|
| `BP_N2CPerf_100` | 100 K2 nodes, one event graph |
| `BP_N2CPerf_1k` | 1000 K2 nodes: event graph, 10 functions, 2 macros |
| `BP_N2CPerf_5k` | 5000 K2 nodes: event graph of about 2000, 30 functions (including the call chain `F_Chain_0`..`F_Chain_5`, 5 calls deep), 5 macros, 1 collapsed graph |
| `S_N2CPerf`, `E_N2CPerf` | the user struct (Count, Mode, Weight, Ready) and enum (Idle, Walk, Run) the Blueprints use |

The node mix is exec chains, Branch, Sequence, ForEachLoop, variable Get and Set, pure math, calls to the Blueprint's
own functions and macros, casts, Make and Break of `S_N2CPerf`, and Switch on `E_N2CPerf`. The generator is C++
(`Source/Private/Tests/Perf/N2CPerfFixtures.cpp`) and uses a fixed seed per fixture, so a regeneration gives the same
graph structure. `NodeToCode.Perf.Fixtures` loads each fixture on every engine, checks its node count (within 5%) and
shape, exports it to N2C JSON v2 that validates, and logs capture time, bytes and tokens per node.

Regenerate on UE 5.6 only (a package saved by a later engine does not load in 5.6, and all three hosts load this one
checkout), then commit the `.uasset` files:

```powershell
$env:N2C_GENERATE_PERF_FIXTURES = '1'; .\build.ps1 -UEVersion 5.6 -Test -TestFilter NodeToCode.Perf; Remove-Item Env:N2C_GENERATE_PERF_FIXTURES
```
