# An offline Blueprint reader (card 0329, exploration)

Can NodeToCode produce N2C JSON without its editor plugin? The question comes from modding devkits whose editor
cannot load our binaries (issue #17: ARK's DevKit, a vendor build of UE 5.5 with a custom `ShooterGameEditor`
target), and from CI or agents that would rather not start an editor. This page compares three routes, records a
spike, and recommends one. It changes no plugin code. Review items: rec-agentic-landscape-15, issue-17 (closed as out
of scope by qw-26).

**Recommendation: build the T3D clipboard converter.** The spike recovers every node, pin and wire of a 99-node event
graph, matched by GUID against the editor's own capture. Leave CUE4Parse as a later batch or CI option, and do not
build on the Rust forks. Details under [Recommendation](#recommendation).

## What survives in a package

The graph that N2C JSON describes exists only in editor data:
- **Graphs are editor-only.** A Blueprint's graphs are `UbergraphPages` and `FunctionGraphs` inside
  `#if WITH_EDITORONLY_DATA` (UE 5.8 `Runtime/Engine/Classes/Engine/Blueprint.h:540-568`). A cooked package drops
  editor-only data, so **a cooked .uasset or .pak holds no EdGraph, no nodes and no pins**. What remains is the
  compiled class: Kismet bytecode for each function and the ubergraph. Decompiling that bytecode gives compiled
  statements: macros expanded, temporaries, jumps, no comments, no node GUIDs. It is not the node graph N2C JSON
  describes. Reading it would be a separate project with low fidelity.
- **Pins are custom-serialized.** In an uncooked package each node writes its pins with
  `UEdGraphPin::SerializeAsOwningNode`, only in editor builds and only past
  `FBlueprintsObjectVersion::EdGraphPinOptimized` (`Runtime/Engine/Private/EdGraph/EdGraphNode.cpp:232-262`). They
  are not tagged properties, so a generic property reader sees the nodes but not their pins and wires.
- **Clipboard text has everything a node holds.** Ctrl+C runs `FEdGraphUtilities::ExportNodesToText`
  (`Editor/UnrealEd/Private/EdGraphUtilities.cpp:458-480`): `UExporter::ExportToOutputDevice(..., TEXT("copy"),
  ..., PPF_ExportsNotFullyQualified|PPF_Copy|PPF_Delimited)` per node. Its properties come out as `Key=Value` lines
  and each pin as a `CustomProperties Pin (...)` line (`UEdGraphNode::ExportCustomProperties`, `EdGraphNode.cpp:751-760`):
  pin GUID, name, direction, the whole `FEdGraphPinType`, the default value, and `LinkedTo` with the other end's
  node name and pin GUID. Any editor that can open the Blueprint, a locked devkit included, can produce it.

## The three routes

| | T3D clipboard parser | CUE4Parse | Rust uasset forks (and UAssetAPI) |
|---|---|---|---|
| Input | Text from Ctrl+C in any editor that opens the Blueprint (or an asset's T3D export) | Uncooked .uasset files; cooked .pak / IoStore containers | Uncooked or cooked .uasset files |
| Nodes and pins from **uncooked** data | All, as text (spike below) | Nodes and pins: `UEdGraphNode` reads `Pins` through its own `SerializeAsOwningNode` when the archive is not editor-filtered ([UEdGraphNode.cs](https://github.com/FabianFG/CUE4Parse/blob/master/CUE4Parse/UE4/Assets/Exports/EdGraph/UEdGraphNode.cs), [UEdGraphPin.cs](https://github.com/FabianFG/CUE4Parse/blob/master/CUE4Parse/UE4/Assets/Exports/EdGraph/UEdGraphPin.cs)). Node classes beyond `UK2Node` are read as generic objects; the mapping to N2C would be ours | Tagged properties only: a code search of [AstroTechies/unreal_asset](https://github.com/AstroTechies/unreal_asset) and [atenfyr/UAssetAPI](https://github.com/atenfyr/UAssetAPI) finds `EdGraphPin` only in version enums, so pin and wire reading would have to be written |
| From **cooked** data | Not applicable: a devkit user copies from the editor | No graph (editor-only); Kismet bytecode only | No graph; Kismet bytecode (`unreal_asset_kismet`) only |
| Engine versions | Every UE4/UE5 version with the `CustomProperties Pin` text (the optimized pin format), 5.6 to 5.8 checked here | Up to UE 5.8 (`GAME_UE5_6` ... `GAME_UE5_8` in [EGame.cs](https://github.com/FabianFG/CUE4Parse/blob/master/CUE4Parse/UE4/Versions/EGame.cs) and version checks) | unreal_asset: up to `VER_UE5_3` (`engine_version.rs`), so 5.4 to 5.8 are missing. UAssetAPI: up to `VER_UE5_8` |
| Language and runtime | Python, stdlib only (or anything that reads text) | C#, .NET runtime | Rust (unreal_asset); C# (UAssetAPI) |
| Licence | Ours (Apache-2.0, like the plugin) | Apache-2.0 | MIT (unreal_asset, UAssetAPI) |
| Maintenance (2026-11-02) | Ours | Active: last push 2026-11-02, 678 stars | unreal_asset upstream last push 2025-01-17. The owner's fork [NCMcClure/unreal_asset](https://github.com/NCMcClure/unreal_asset) is 4 commits ahead (file paths in error messages, an engine_version tweak), last push 2025-04-27. The owner's [UAssetAPI fork](https://github.com/NCMcClure/UAssetAPI) is 155 commits behind upstream |
| Effort to N2C JSON | Small: the spike is about 300 lines; titles, a CLI and tests remain (about a week) | Medium to large: a .NET CLI, the same node mapping from binary exports, version handling, packaging a .NET tool | Large: pin deserialization, UE 5.4 to 5.8 support (unreal_asset), then the mapping |
| Who it serves | Devkit and locked-editor users (issue #17), chat and agent workflows that paste nodes | CI and agents reading many uncooked assets without an editor | Same as CUE4Parse, at higher cost |

## Spike: T3D to N2C JSON

[offline-reader/t3d_to_n2c.py](offline-reader/t3d_to_n2c.py) (stdlib Python, run with `python -I`) reads the node
blocks of clipboard text. It takes node types from the plugin's own class table, parsing
`Source/Private/Utils/N2CNodeTypeRegistry.cpp` at start. Pin types come from the K2 pin category, mapped the way
`Source/Private/Utils/N2CStableNames.cpp:65-95` maps them. Wires come from `LinkedTo` and are written as
`flows.execution` and `flows.data`. With `--guids` it adds each node's and pin's GUID, so the result can be compared
with `get-blueprint-n2cjson`, which reports the same GUIDs.

**Input.** `BP_N2CPerf_100` is the checked-in uncooked perf fixture
(`Tests/NodeToCodeTestContent/Content/Perf/BP_N2CPerf_100.uasset`, 99 nodes in its event graph). A headless UE 5.8
editor exported it with `UObjectExporterT3D` (an `AssetExportTask` over run-python), which uses the same per-object T3D
export as the clipboard. The asset export writes each object twice, once as a declaration and once with its
properties. Joining the two passes for the event graph's nodes gives clipboard-shaped text: one
`Begin Object Class=... Name=...` block per node with its properties and pins. The same session read the editor's own
capture with `get-blueprint-n2cjson` as ground truth. A 12-node excerpt and its converted output are committed:
[sample_BP_N2CPerf_100_EventGraph_12.t3d](offline-reader/sample_BP_N2CPerf_100_EventGraph_12.t3d) and
[sample_BP_N2CPerf_100_EventGraph_12.n2c.json](offline-reader/sample_BP_N2CPerf_100_EventGraph_12.n2c.json).

**Result on the whole event graph** (99 nodes, 367 pins, 98 wires), matched by GUID with the editor's capture:

| What | Agreement |
|---|---|
| Nodes, and their `type` and `class` | 99 of 99 |
| Pins, and their `name`, `type`, `sub_type`, `default_value`, `connected`, `is_reference`, `is_const`, `is_array` / `is_set` / `is_map` | 367 of 367, every field |
| Pin `label` (from `PinFriendlyName`) | 367 of 367 |
| Wires (37 exec, 61 data) | 98 of 98 |
| `member_name`; `member_parent` (with `--name BP_N2CPerf_100` for the Blueprint's own members and custom events) | 99 of 99; 97 of 99 |
| `pure` (derived as "no exec pin", the spec's definition) | 99 of 99 |
| `disabled`, `comment` | 99 of 99 |
| `title` | 64 of 99: the converter writes none, and the editor writes 35 |

**Gaps,** all of them things clipboard text does not hold:
- **Node titles.** `GetNodeTitle` is computed by each node class; the spike writes none. The spec makes titles an
  optional hint, so a translation does not depend on them.
- **Owners of inherited members.** `GetOwner` called on self has `bSelfContext=True` and no class. The editor
  resolves it to `Actor` through the class hierarchy; text cannot. The spike writes the Blueprint's name, which is
  wrong for 2 nodes.
- **What only the class knows.** Latent flags for async nodes (the function's metadata), and the unused and
  unreachable analysis (computable from the wires, not written yet).
- **Context outside the copied nodes.** The graph name and type, the Blueprint's name (passed with `--name`), member
  variables, the definitions of user structs and enums, the bodies of macro and collapsed graphs, and timeline
  templates. Wires that leave the copied selection are reported as `LINK_OUTSIDE` warnings.

**Uncooked .uasset spike: not done.** It needs CUE4Parse from NuGet (or a Rust toolchain for unreal_asset, which
is not installed). Downloading the package needed the owner's approval, which this run did not have. CUE4Parse's
`UEdGraphNode` export already reads pins, so the expected result is the same data as the T3D route: the same mapping,
read from binary.

## Recommendation

1. **Build the T3D converter** as an editor-free tool: the spike's parser, plus titles for common node classes, the
   unused and unreachable analysis, and a CLI. Ship it as a script next to the Claude Code plugin's skills, or under
   `Scripts/`. Test it against the capture goldens: an automation test can export each golden graph with
   `ExportNodesToText` (as `Source/Private/Tests/N2CTestBlueprintFactory.cpp:626` already does) and compare the
   converter's output with the capture. It serves issue #17's users today (copy in the devkit, convert anywhere), and
   any chat or agent that receives pasted nodes. Effort: about a week.
2. **CUE4Parse later, on demand,** for CI or batch reading of uncooked projects without an editor. It is maintained,
   supports UE 5.8, reads pins, and has a compatible licence. The cost is a .NET tool and the same mapping again.
3. **Not the Rust forks.** unreal_asset stops at UE 5.3 and reads no pins, and the owner's forks add little over
   their upstreams.
4. **Not cooked packages.** Their graphs are gone. Kismet bytecode is a different and much weaker source.

Revisit the CUE4Parse route if someone asks for N2C JSON in CI from a project's uncooked content, or if a devkit
vendor's editor cannot even copy nodes.
