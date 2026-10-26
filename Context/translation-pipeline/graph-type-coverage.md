# Graph-type coverage matrix (card 0246, input to decision 0247)

What NodeToCode does today with each kind of graph a user can open, measured on real Blueprints on UE 5.6, 5.7 and 5.8
at `origin/dev-2.0` 6476e20 (cards 0234 and 0235 merged), October 2026. Decision 0247 asks which graph types
NodeToCode supports, supports in part with a warning, or refuses. This page gives the evidence and a recommendation; the
owner decides. The node-class half is [k2-node-inventory.md](k2-node-inventory.md).

## Recommendation in one paragraph

Own every K2 graph (option B below). Every Blueprint graph built on the K2 schema already captures and translates
correctly on all three engines: event graphs, functions, macros, collapsed graphs, construction scripts, interfaces, and
the K2 graphs inside Widget, Editor Utility, Level and Animation Blueprints. Support the two cheap partial cases with a
note: widget bindings, and K2 nodes that map to no N2C type. Refuse the animation pose graphs (AnimGraph, states and
state machines) and RigVM graphs (Control Rig) with a typed error. Today they come out confidently wrong: every pose
node is flagged "unused", so the LLM writes empty code, and one transition rule became `return false;`. Supporting them
properly is a separate L-sized project with no C++ target worth generating. Transition rules are pure boolean K2 graphs,
so they can move from refused to supported for about S once the pose-result boundary fix lands. Materials stay with
decision 0443.

## How it was measured

- Fixtures are real engine assets that exist on all three engines, so the same graphs were measured everywhere. They
  come from the engine plugins MoverExamples (AnimBlueprint `ABP_Manny` with two state machines, Widget Blueprint
  `ControlsDisplay`, Control Rig `CR_Mannequin_BasicFootIK`, level `L_CharacterMovementBasics`) and Landmass (Editor
  Utility Widget `EUW_Landmass_Editor` with functions, macros, collapsed graphs and interface implementations, Editor
  Utility Blueprint `EUB_SpawnErosionTab`). Engine content added `/Engine/Sequencer/DefaultBurnIn` (a Widget Blueprint
  with text bindings), `/Engine/EngineSky/BP_Sky_Sphere` (a construction script),
  `/Engine/EditorBlueprintResources/StandardMacros` and `/Engine/EngineMaterials/DefaultMaterial`. Two scratch assets
  were made in the session: a Blueprint Interface, and an Actor whose BeginPlay runs Switch on Gameplay Tag with Get
  Sequence Binding beside it.
- Capture went through the MCP tools on a headless editor of each engine (`-RenderOffScreen`), with no editor tab open.
  `list-blueprint-graphs` gave the kind and node count, and `get-blueprint-n2cjson` at depth 0 gave the capture. For
  AnimGraph, a state machine, a collapsed graph and the Level Blueprint, the graph was also opened in a Blueprint editor
  (`open-blueprint-asset` with `focus_graph`) and read with `get-focused-blueprint`. That is the path the editor
  overlay and the NodeToCode window use.
- Translation used `translate-batch` with one graph per call: provider anthropic, `claude-sonnet-5-5` (the catalog
  default), target C++, on 5.8 only. It ran twice, with the same verdicts both times. The N2C JSON was the same on all
  three engines apart from content differences, so the translations were not repeated on 5.6 and 5.7.
- Harness: `Tests/mcp_probe/graph_type_matrix_0246.py` (stdlib; report and every capture and translation go to
  the gitignored logs folder next to it). Node inventory: `Scripts/k2_node_inventory.py`.

Verdicts: **supported** means the capture is right and the translation is right. **Partial** means the capture is
right but something the user needs is missing. **Wrong** means NodeToCode returns output that misleads. **Refused
(de facto)** means it fails today, but with a misleading message.

## The matrix

Numbers are the same on 5.6, 5.7 and 5.8 unless a cell says otherwise. "Nodes" means nodes captured out of the nodes
the graph holds; comment boxes and reroute knots account for the difference. Cost is the work to reach "supported":
S is under a day, M is 2 to 4 days, L is one to two weeks or more.

| # | Graph type | Example capture | Capture today | Translation (5.8) | What fails | Verdict | Cost to support |
|---|---|---|---|---|---|---|---|
| 1 | Event graph (Actor and other UObject Blueprints) | `EUB_SpawnErosionTab` EventGraph | `EventGraph`, 16 of 18 nodes, 9 exec / 9 data links | Logic correct. Base class guessed as `UEditorUtilityObject`, though BeginPlay, Tick and overlap events mean an actor (F4) | the parent class is not in the capture | supported | 0 (F4: S) |
| 2 | Function | `EUW_Landmass_Editor` Draw Triangle | `Function`, 26 of 26 | not run; same path as row 6 | nothing | supported | 0 |
| 3 | Macro in a Blueprint | `EUW_Landmass_Editor` Get Cursor World Ray | `Macro`, 20 of 20; Tunnel nodes as boundaries | not run | nothing | supported | 0 |
| 4 | Macro library | `StandardMacros` ForLoop | `Macro`, 9 of 9; metadata `blueprint_type: MacroLibrary` | not run | nothing | supported | 0 |
| 5 | Collapsed graph | `EUW_Landmass_Editor` Make Distance Field | `Composite`, 66 of 67, Tunnel boundaries (card 0235) | not run | By path it works. In the editor, `open-blueprint-asset` cannot focus a collapsed graph by name (focused graph empty, then `NO_FOCUSED_GRAPH`). editor-integration-16 (overlay on graphs the window cannot resolve) is still open | supported (by path), partial (editor focus) | S, editor-integration-16 / card 0251 |
| 6 | Construction Script | `BP_Sky_Sphere` UserConstructionScript | `Construction`, 8 of 8 (card 0235) | Correct `OnConstruction` | nothing | supported | 0 |
| 7 | Interface function implemented in a Blueprint | `EUW_Landmass_Editor` Get Shape Mask | `Function`, 2 of 2 (list kind `interface`) | not run | graph_type does not say it implements an interface; the function name does | supported | 0 |
| 8 | Blueprint Interface asset | scratch `BPI_Matrix` MatrixQuery | `Function`, 1 of 1, `blueprint_type: Interface` | Correct `BlueprintNativeEvent` declaration plus the `UINTERFACE` shell in the notes | nothing | supported | 0 |
| 9 | Event graph with unmapped K2 nodes | scratch `BP_MatrixNodes` | `EventGraph`, 5 of 5. Switch on Gameplay Tag typed `Switch` (by its base class); Get Sequence Binding typed `Unknown` with its class | Plausible code. The Unknown node is named and stubbed, or left out as unused, with a note saying why | Unknown nodes get no N2C type; translation leans on title and pins. A first run with Evaluate Chooser gave a TODO stub that said why | partial | S per node family (map the family), or nothing (Unknown plus class already tells the LLM) |
| 10 | Level Blueprint | `L_CharacterMovementBasics` level script, EventGraph | `EventGraph`, 205 of 271 nodes (11 comments), `blueprint_type: LevelScript`; focused path gives the same | Correct `ALevelScriptActor` subclass: level actor references as `EditInstanceOnly` pointers, bound events wired in BeginPlay (51k in, 29k out tokens, 157 s) | Read by path only with the object path `<map>.<map>:PersistentLevel.<map>`, and only once the map is loaded. The package path answers `ASSET_NOT_FOUND`. One exploratory run lost the editor (no crash report) while that call loaded an unopened map; 3 later runs on 3 engines did not reproduce it | supported (focused), partial (by path) | S: resolve a map package path to its Level Blueprint |
| 11 | Widget Blueprint event graph | `ControlsDisplay` EventGraph | `EventGraph`, 29 of 37 | Correct; `UUserWidget` guessed from the node set | Metadata has no parent class or asset class, so the model guesses the base class | supported | S for the metadata (see F4) |
| 12 | Widget Blueprint function | `ControlsDisplay` GatherInputInfo | `Function`, 58 of 62 | not run | as row 11 | supported | 0 |
| 13 | Widget property binding | `DefaultBurnIn` Get_TopLeft_Text_0 | `Function`, 6 of 6 | Correct function body, but as a member of a guessed `UDefaultBurnIn : UObject` that binds to nothing | The binding itself (widget TopLeft, property Text, this function) lives in `UWidgetBlueprint::Bindings`, outside every graph, and is not captured. The capture cannot tell a binding from any other getter | partial | S: one optional field from `UWidgetBlueprint::Bindings` (UMGEditor is already a dependency), N2C JSON contract change |
| 14 | Editor Utility Widget | `EUW_Landmass_Editor` (25 graphs: 9 function, 6 macro, 7 collapsed, 3 interface, event graph) | every graph captured; EventGraph 99 of 118 | not run; its graph kinds are rows 2 to 7 | as row 11 | supported | 0 |
| 15 | Editor Utility Blueprint | `EUB_SpawnErosionTab` | as row 1 (`Get EditorUtilitySubsystem` typed `GetSubsystem`) | as row 1 | as row 1 | supported | 0 (F4: S) |
| 16 | AnimBlueprint event graph | `ABP_Manny` EventGraph | `EventGraph`, 44 of 57 (5.6: 42) | Correct `NativeUpdateAnimation`-style code | as row 11 (`UAnimInstance` guessed correctly) | supported | 0 |
| 17 | AnimGraph | `ABP_Manny` AnimGraph | `Animation`, 10 of 10 (5.6: 8). 6 `Unknown` pose nodes (Root, 2 StateMachine, SaveCachedPose, Slot, ControlRig), 0 exec links, pose links as data links. **All 10 flagged `unused`** | Empty. The model follows the spec ("unused nodes are not translated") and writes `// Nothing is implemented for AnimGraph` | Liveness treats only FunctionEntry/Result and Tunnel nodes as graph boundaries, so a pose graph's Output Pose never counts as live. Pose nodes have no N2C types. A C++ AnimGraph is `FAnimNode_*` members, not statements, so even a correct capture has no good C++ to become | **wrong** | Refuse: S. Support (pose node types, a pose-result boundary, a prompt for "explain" rather than "translate"): L |
| 18 | State machine graph | `ABP_Manny` Locomotion (9 nodes: entry, states, transitions) | **Not captured**: `EMPTY_GRAPH: graph Locomotion has no nodes`. Its nodes are `UAnimStateNodeBase`, not `UK2Node`, and the collector keeps only `UK2Node`. `list-blueprint-graphs` lists it as kind `graph`, unclassified. In the editor, `open-blueprint-asset` cannot focus it, and `get-focused-blueprint` then silently returns the AnimGraph | `NO_VALID_ITEMS` / `EMPTY_GRAPH` (batch); nothing sent | Misleading error ("has no nodes" for a graph of 9). Silent wrong graph on the focused path. States and transitions lose their parent and their source and target. Every transition graph is named "Transition", so 11 graphs in one ABP share one name | refused (de facto) | Refuse with a typed error: S. Support (a states-and-transitions model, a new contract section): L |
| 19 | State (pose graph inside a state) | `ABP_Manny` Walk / Run | `Animation`, 8 of 8; StateResult, 2 BlendSpacePlayer, BlendListByBool `Unknown`; **all 8 `unused`** | No code: "Every node ... is marked unused" | as row 17 | **wrong** | as row 17 |
| 20 | Transition rule | `ABP_Manny` Transition (6 nodes) | `Function` (the K2 schema reports GT_Function), 6 of 6; TransitionResult `Unknown`; **all 6 `unused`** | **Wrong code.** Run 1: `bool Transition() const { return false; }`. Run 2: an empty `void Transition()`. The rule is `!ShouldMove \|\| !IsAirborne`; the notes say so but the code does not | Liveness as row 17. TransitionResult is not a boundary. The graph does not say which transition it belongs to (from-state, to-state) | **wrong** | Pure boolean K2 graph: S to make it supported (TransitionResult, StateResult and Root as liveness boundaries, plus the transition's from and to states in the graph name). The structure around it is row 18 |
| 21 | Control Rig graph (RigVM) | `CR_Mannequin_BasicFootIK` Rig Graph (41 nodes) | **Not captured**: `EMPTY_GRAPH ... has no nodes`. The asset is a `UBlueprint` subclass, so it gets past the asset check; its RigVM nodes are not K2 | `NO_VALID_ITEMS` / `EMPTY_GRAPH` | Misleading error. `list-blueprint-graphs` lists its graphs as event, function and graph | refused (de facto) | Refuse with a typed error: S. Support: L (RigVM is its own graph model; Epic's 5.8 toolsets cover it) |
| 22 | Material and other non-Blueprint assets | `/Engine/EngineMaterials/DefaultMaterial` | `ASSET_NOT_FOUND: no Blueprint at ...` (by-path tools); the batch tools' descriptions promise `UNSUPPORTED_ASSET_TYPE` (not measured) | n/a | Two different errors for the same case. Materials: decision 0443, inventory 0446 | refused | Materials: see 0443. Niagara and PCG: refuse (Epic's toolsets cover them) |

Not measured as their own rows: Widget animations and the widget designer tree (no graph), StateTree, Niagara and PCG
assets (none in the mounted plugins; none are `UBlueprint` subclasses, so they get the refusal of row 22). Delegate
signature graphs are a K2 kind that goldens cover (cards 0234, 0235).

## Cross-cutting findings

- **F1 Liveness marks pose graphs dead.** `N2CLiveness::MarkGraph` makes FunctionEntry, FunctionResult, Tunnel and
  TunnelBoundary the only boundaries, so in any graph whose result node is not one of these, every node is "unused". The
  prompt tells the model not to translate unused nodes. This is the cause of rows 17, 19 and 20, on all three engines.
  It is the one place today's output is confidently wrong rather than refused.
- **F2 Non-K2 graphs fail as "has no nodes".** The collector keeps `UK2Node`s only, so a state machine (row 18) or a
  RigVM graph (row 21) reaches the empty-graph check. The user and agents get `EMPTY_GRAPH` for a graph full of nodes.
  The batch tools then fail the whole call with `NO_VALID_ITEMS`.
- **F3 Graph kinds outside the K2 lists are unclassified.** `list-blueprint-graphs` reports AnimGraph as `function` and
  state machines, states and transitions as `graph`. Transition rules come out as graph_type `Function` (from their
  schema). An agent cannot tell from the listing which graphs NodeToCode can read.
- **F4 Metadata has no parent class and no asset class.** `metadata` is `{name, blueprint_type, blueprint_class}`, and
  `blueprint_type` is `Normal` for Actor, Widget, AnimBlueprint and Editor Utility Blueprints alike. Translations guess
  the base class: `UObject` for the burn-in widget (row 13), and `UEditorUtilityObject` for an Editor Utility Blueprint
  whose graph has actor events (row 1). One field each, S, contract change.
- **F5 The focused path can read the wrong graph.** When `open-blueprint-asset` cannot focus the requested graph (state
  machine, collapsed graph), it reports `focused_graph: ""` and success. The next `get-focused-blueprint` reads whatever
  that editor last focused (here the AnimGraph). This is the editor-side face of editor-integration-16.
- **F6 Level Blueprints need an object path.** The map's package path answers `ASSET_NOT_FOUND`, and the object path
  works only once the map is loaded. (Row 10.)
- **F7 Engine assertion through a frozen CRUD tool (not capture).** Wiring an Evaluate Chooser node with no chooser asset
  and compiling it (as `connect-pins` does) hits an engine `check` on 5.6 and 5.7 (`EvaluateChooserNode.cpp`,
  `ResultPin || Chooser...`) and ends the editor. It is Epic's assertion, so no capture work follows from it. It is
  noted for the CRUD freeze (decision 0007): a frozen tool can still take the editor down through engine node code.

## Node classes (summary of [k2-node-inventory.md](k2-node-inventory.md))

`UK2Node` subclasses declared in the engine sources, leaving out abstract and deprecated ones: 314 on 5.6, 332 on 5.7,
338 on 5.8. The registry has 110 class-name mappings.

| Engine | own mapping | via a base class | Event (interface) | Unknown | of which AnimGraph pose nodes |
|---|---|---|---|---|---|
| 5.6 | 100 | 68 | 3 | 143 | 102 |
| 5.7 | 100 | 81 | 3 | 148 | 102 |
| 5.8 | 100 | 83 | 3 | 152 | 106 |

- About 70% of the Unknown classes are AnimGraph pose nodes (`UAnimGraphNode_*`, which subclass `UK2Node`), so they
  share rows 17 and 19's fate. Refusing the pose graphs takes them out of scope.
- The other 46 Unknown classes on 5.8 are plugin K2 nodes that appear in ordinary event graphs: GameplayTags literals
  and compares, Chooser and ProxyTable evaluate, LiveLink, StateTree property references, GameplayCameras rig
  parameters, PropertyAccess, HTTP and WebAPI requests, DMX, SceneState, MVVM, Movie Scene's Get Sequence Binding. They
  capture as `Unknown` with class, title and pins (row 9). Mapping a family is S each.
- The review named K2 classes it believed new in 5.8. The inventory corrects that list. K2Node_GetCameraRigParameter(s)
  and the DataChannel `_WithContext` nodes arrived in 5.7, not 5.8. The only K2 classes new in 5.8 are
  K2Node_UAFComponentGetVariable and K2Node_UAFComponentSetInputBinding (typed CallFunction through their base class)
  and five AnimGraph nodes. K2Node_AnimNextComponentSetVariable, K2Node_DataChannel*_WithContext and
  K2Node_InjectionCallbackProxySetVariable are CallFunction subclasses, so they get the CallFunction type with their
  function's name.

## Options for decision 0247

| Option | Supported | Partial, with a note | Refused with a typed error | Work it creates | Risk |
|---|---|---|---|---|---|
| A. Only Actor-style Blueprints | rows 1 to 8, 10, 15 | 9 | everything inside Widget, Anim, Editor Utility and Level-only graphs, 11 to 14, 16 to 22 | S (a refusal by asset class) | Refuses graphs that work today (rows 11, 12, 14, 16 are fine). Users lose function |
| **B. Every K2 graph; refuse pose and RigVM graphs (recommended)** | rows 1 to 8, 10 to 12, 14 to 16; 20 once F1 is fixed | 9 (Unknown nodes), 13 (bindings), 5 and 10 (focus and path gaps) | 17, 18, 19, 21 (and 20 until fixed), 22 | 0248's refusal by schema and node base (S). F1 boundary fix for transition rules (S). F2 and F3 error and kind (S). F4 metadata (S). Binding field (S) | Low. AnimBP users get a clear "not supported" on pose graphs instead of empty or wrong code |
| C. B plus AnimGraph and state machines | B plus 17 to 20 | B | 21, 22 | B plus a states-and-transitions model, pose node types, a prompt for explaining animation rather than generating C++. L, and a contract change | High. No clear C++ target (AnimGraphs compile to `FAnimNode_*` data, and 5.8's UAF is moving the target). Epic's 5.8 toolsets cover AnimBP editing |
| D. Capture everything, warn on the rest | all K2 graphs, and 17 to 20 with a warning | 17 to 20 | 21, 22 | S | Keeps the confidently wrong output of F1 behind a warning. Not recommended |

What the recommendation commits the 27 waiting cards to: 0248 implements the refusal list of option B, keyed on the
graph's schema (`UAnimationGraphSchema`, `UAnimationStateMachineSchema` and other non-K2 schemas). It does not key on
names. The overlay shows n/a on those graphs, and the tools and batch return a typed `UNSUPPORTED_GRAPH_TYPE` naming
the type instead of `EMPTY_GRAPH`. The capture and N2C JSON contract cards behind 0247 can then assume K2 graphs
only. The material cards follow 0443.

## Reproduce

```bash
export N2C_PORT_OFFSET=1000   # your lane's offset
python -I Tests/mcp_probe/graph_type_matrix_0246.py --engine G:/UE/UE_5.8 \
    --project "<Hosts>/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" --label 5.8 [--translate]
python -I Scripts/k2_node_inventory.py --engine 5.6=G:/UE/UE_5.6 --engine 5.7=G:/UE/UE_5.7 --engine 5.8=G:/UE/UE_5.8 \
    --markdown Context/translation-pipeline/k2-node-inventory.md
```

The harness takes about 5 minutes per engine, plus about 6 minutes with `--translate`. The inventory reads every engine
header and takes about 9 minutes for three engines.
