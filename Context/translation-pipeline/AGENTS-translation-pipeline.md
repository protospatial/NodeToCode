# NodeToCode Translation Pipeline

Blueprint node collection, translation to structured format, and node processing strategies.

## Directory Structure

```
Source/
├── Public/
│   ├── Core/
│   │   ├── N2CNodeCollector.h        → Collects nodes from Blueprint graphs
│   │   ├── N2CCapture.h              → Capture API: N2C::CaptureNodes/CaptureGraph/DescribeNode
│   │   └── N2CSerializer.h           → JSON serialization/deserialization
│   │
│   ├── Models/
│   │   ├── N2CBlueprint.h            → FN2CBlueprint, FN2CGraph, FN2CFlows, etc.
│   │   ├── N2CJsonContract.h         → N2C JSON version ("2.0.0"), readable majors and the flows field names
│   │   ├── N2CNode.h                 → FN2CNodeDefinition, EN2CNodeType
│   │   ├── N2CPin.h                  → FN2CPinDefinition, EN2CPinType
│   │   ├── N2CTranslation.h          → LLM response structures
│   │   └── N2CBatchTranslationTypes.h → Batch translation types and delegates
│   │
│   ├── Utils/Processors/
│   │   ├── N2CNodeProcessor.h        → IN2CNodeProcessor interface
│   │   ├── N2CBaseNodeProcessor.h    → Base processor implementation
│   │   ├── N2CNodeProcessorFactory.h → Factory for node processors
│   │   └── N2C*Processor.h           → Specific processor implementations
│   │
│   ├── LLM/
│   │   └── N2CBatchTranslationOrchestrator.h → Batch translation management
│   │
│   └── BlueprintLibraries/
│       └── N2CBatchTranslationBlueprintLibrary.h → Blueprint API
│
└── Private/
    ├── Core/
    │   ├── N2CNodeCollector.cpp
    │   ├── N2CCapture.cpp            → The N2C:: capture functions
    │   ├── N2CCaptureContext.h/.cpp  → Per-call capture state and node conversion (private)
    │   └── N2CSerializer.cpp
    │
    ├── Models/
    │   ├── N2CBlueprint.cpp
    │   ├── N2CNode.cpp
    │   └── N2CPin.cpp
    │
    ├── Utils/Processors/
    │   └── N2C*Processor.cpp
    │
    ├── LLM/
    │   └── N2CBatchTranslationOrchestrator.cpp
    │
    └── BlueprintLibraries/
        └── N2CBatchTranslationBlueprintLibrary.cpp
```

## Core Translation Pipeline

Handles transforming Blueprint nodes into a structured, LLM-processable format.

### Node Collector

**`Source/Public/Core/N2CNodeCollector.h` (`FN2CNodeCollector`)**

Singleton that gathers `UK2Node` instances and pin details from `UEdGraph`.

| Method | Description |
|--------|-------------|
| `CollectNodesFromGraph()` | Collects all nodes from a specific graph |
| `CollectPinsFromNode()` | Collects input/output pins from a node |

Private helpers: `ValidatePin`, `GetK2PinMetadata`, `CollectPinDefaultValue`, `CollectPinConnections`

### Capture

**`Source/Public/Core/N2CCapture.h` (namespace `N2C`)**

Which graph types capture handles, measured on real assets on UE 5.6 to 5.8 (card 0246, input to decision 0247):
[graph-type-coverage.md](graph-type-coverage.md). Every K2 graph captures correctly. AnimGraph and state pose graphs
come out with every node "unused". State machines and Control Rig graphs fail as `EMPTY_GRAPH`, because the collector
keeps `UK2Node`s only. Every engine `UK2Node` subclass and the N2C type it gets: [k2-node-inventory.md](k2-node-inventory.md)
(`Scripts/k2_node_inventory.py`). Material capture (epic 0442) is not built yet; every material expression and
material asset class on each engine, the handling proposed for it and the ENGINE_MINOR_VERSION guards it needs:
[material-classes.md](material-classes.md) (`Context/translation-pipeline/gen_material_classes.py`, card 0446).

Liveness (card 0137, issue #13): the capture leaves out the editor's ghost nodes (`IsAutomaticallyPlacedGhostNode()`,
unless `FN2CCaptureOptions::bIncludeGhostNodes`, which the graph-editing MCP tools and the Python bridge's
`GetFocusedBlueprintJson` set), marks a node whose
`IsNodeEnabled()` is false `bDisabled` and routes exec past it when it has one exec input and one exec output (as the
compiler's `GetPassThroughPin` does), then `N2CLiveness::MarkGraph` (`Source/Private/Core/N2CLiveness.h`) marks pure
nodes nothing live reads `bUnused` and impure nodes no entry reaches `bUnreachable` ("unused", "unreachable",
"disabled" in N2C JSON, `N2CJsonContract`). The validator warns when a connected data input has no data flow into it.

Converts collected `UK2Node`s into `FN2CBlueprint`. Every call builds its result in its own private context
(`FN2CCaptureContext`, `Source/Private/Core/N2CCaptureContext.h`) and returns it by value, so captures share no state:
two captures, a capture started during another (an overlay estimate during a translation) or one made between sending
a request and reading its result never change each other's output (card 0133 replaced the `FN2CNodeTranslator`
singleton). Game thread only.

| Function | Description |
|----------|-------------|
| `CaptureNodes(Nodes, Options)` | Nodes of one graph (and the nested graphs they use) -> `TValueOrError<FN2CCapture, FString>` |
| `CaptureGraph(Graph, Options)` | Collects the graph's nodes with `FN2CNodeCollector`, then `CaptureNodes` |
| `DescribeNode(Node, NodeID)` | One node alone (MCP add-node and search tools): fresh context, no nested graphs, no related types |

`FN2CCapture` holds the `FN2CBlueprint` and that capture's ID-to-GUID maps: `NodeGuids` (node ID -> `NodeGuid`),
`Pins` (pin ID -> `FN2CCapturedPin`: node ID, `NodeGuid`, `PinId`; a `PinId` is only unique on its node) and
`GraphGuids` (one per graph, in `Blueprint.Graphs` order), with `FindNodeID(NodeGuid)`.
`FN2CCaptureOptions` carries `Depth` (default `INDEX_NONE`: `UN2CSettings::TranslationDepth`, read when the capture
starts), `bIncludeRelatedTypes` and `bDiscoverNestedGraphs`. `FN2CEditorIntegration::TranslateNodesToN2CBlueprint`
and `TranslateNodesToN2CBlueprintWithMaps` (which returns the whole `FN2CCapture`) are thin wrappers kept for the MCP
tools and the Python bridge.

IDs (card 0134): nodes are N1, N2, ... and pins P1, P2, ..., each unique across the whole capture. They are keyed by
the `UEdGraphNode*`/`UEdGraphPin*` themselves (never by GUID alone) and given in two passes per graph: first every
node and its visible pins in graph order (collection order for the captured graph), then the nodes are described and
their flows recorded, so IDs never depend on which links are met first and an unchanged graph gets the same IDs.
Nodes outside the capture get no ID, and links to them are not recorded (card 0136). Every graph, the
captured one included, is marked visited when it starts (by pointer), so a function that calls itself and a graph
reached twice are captured once, and same-named functions of different Blueprints are both captured; a nested graph
from another Blueprint sets `FN2CGraph::OwnerBlueprint` (JSON `owner_blueprint`, optional).

Nested graphs and graph kinds (card 0235, translation-core-20 and -21, imp-translation-core-4 and -5):
`DetermineGraphType` asks the graph's schema (`UEdGraphSchema::GetGraphType`): a collapsed graph (outer
`UK2Node_Composite`) is `Composite`, `GT_Function` is `Function` or, for `UEdGraphSchema_K2::FN_UserConstructionScript`,
`Construction`, `GT_Macro` is `Macro`, `GT_Animation` and `GT_StateMachine` are `Animation`, the rest `EventGraph`;
never the graph's name or its nodes. `AddGraphToProcess` queues a collapsed graph always, at the depth of the graph
that holds it (it is part of that graph; calls inside it count from there), never a math expression's; a function or
macro graph only when its Blueprint is user content (`IsUserContent`: the package mount point is `Game`, or a plugin
that can contain content and is loaded from the project, so content and GameFeature plugins count and engine content,
engine plugins, `/Script` and `/Temp` do not) and within the depth limit. User structs and enums are emitted by class
(`UUserDefinedStruct`, `UUserDefinedEnum`), wherever they live. A `Composite` node's `MemberName` is its bound graph's
name, as a `MacroInstance` node's is its macro's. `CaptureComments` records each `UEdGraphNode_Comment` of a captured
graph as an `FN2CCommentBox` (JSON `comments`: `text` and `nodes`, the captured nodes whose top-left corner lies in
the box, in capture order; additive and optional). A null entry in `UEdGraph::Nodes` is skipped everywhere.

Data edges (card 0135, from PR #7 by Yury Rakhuba): each edge is recorded once, from its output pin, after the ID
pass. `TraceTargetsThroughKnots` follows the output's links depth first in link order, through knots and every branch
of a knot's fan-out, and returns every non-knot input it reaches; each with an ID is added with `AddUnique` to
`Flows.Data[<output ref>].Targets`. So an output feeding several inputs keeps all of them, and every connected data
input of the capture is the target of exactly one edge (`NodeToCode.Capture.Golden.DataFlows` checks this on every
golden).

Exec edges (card 0136): each exec link is recorded once, from its exec output pin, the same way:
`TraceTargetsThroughKnots` gives every exec input it reaches, and each with an ID is added to `Flows.Execution` as
`N2CJsonContract::MakeExecEdge` "N3.P5->N4.P1" (`AddUnique` per pin pair, so two outputs into one node are two
entries). The source pin tells Branch True/False, Switch cases, Sequence then_N and Loop Body/Completed apart, the
target pin Gate Enter/Open/Close and Timeline Play/Reverse. `FN2CBlueprintValidator::ValidateFlowReferences`
parses each entry with `N2CJsonContract::ParseExecFlow`: a pin-qualified entry must run from an exec output pin to
an exec input pin of the named nodes; a node-only entry ("N1->N2", or a 1.x chain) needs only its nodes.
`NodeToCode.Capture.Golden.ExecFlows` checks every golden and the Branch, Sequence, ForEachLoop, Switch, Gate and
Timeline cases by pin name.

Names (card 0138, gap-5, translation-core-22 and -28): capture writes identifiers that never change with the editor
language or preferences, and display text only as hints (`Source/Public/Utils/N2CStableNames.h`). A node gets
`NodeClass` (JSON `class`, the UK2Node class), `MemberParent` (the class that declares the member,
`N2CStableNames::ClassName`: a Blueprint class by its Blueprint's name, any other class by its own; for a variable the
property's owner class, not its type) and `MemberName` (as declared; only Blueprint class names lose `_C`). A pin's
`Name` is `UEdGraphPin::PinName`. `Title` (JSON `title`) is the node's menu title and `Label` (JSON `label`) the pin's
friendly name or the node's pin-name override, both in source text (`FText::BuildSourceString`), and each is dropped
when `N2CJsonContract::IsRedundantHint` says it is only the identifier spelled for display (so UE 5.8, which titles a
call `CollectGarbage` where 5.6 and 5.7 say `Collect Garbage`, writes the same JSON). Text pin defaults and enum value
names are also written in source text. `NodeToCode.Capture.Golden.StableNames` checks this on every golden and
captures every case again with the editor in Japanese (it must match exactly) and with friendly names flipped (only
the hints may change).

Pin types (card 0238, translation-core-13 and -31): `N2CStableNames::PinType` maps one terminal type (category,
subcategory, type object) to the pin `type`, checking the subcategories the K2 schema shares between categories first
(`self` is Self, `bitmask` Bitmask, `index` Index), and typing a byte with a `UEnum` Enum, since the schema gives every
enum pin `PC_Byte`; `PinSubType` gives `sub_type`. For a map pin the main type is the key, and `PinValueType` gives
`value_type` and `value_sub_type` (`FN2CPinDefinition::ValueType`, `ValueSubType`); `ProcessRelatedTypes` lists user
structs and enums found on either side, and Make Map is named "<key> to <value>". `NodeToCode.Capture.Golden.PinTypes`
and the MapPins and BitmaskPin goldens check it.

Key responsibilities:
- Manages ID generation (N1, N2, etc. for nodes; P1, P2, etc. for pins)
- Uses `FN2CNodeProcessorFactory` for node-specific property extraction
- Determines types via `FN2CNodeTypeRegistry` (card 0234): it walks the node's class up `GetSuperClass()` and the
  nearest mapped ancestor wins (a class mapping, then the class name without `K2Node_`, at each level), so a plugin
  node lands on its real base class. No mapped ancestor: `Event` when the class implements
  `IK2Node_EventNodeInterface`, else `Unknown` with the node's `class`, filled by the base processor. Variable nodes
  are then typed by access before scope (`LocalVariableGet`/`LocalVariableSet`, `FunctionParameter`). Plugins add
  mappings with `RegisterNodeClass` or `RegisterNodeType`; `NodeToCode.Capture.Golden.NodeTypes` covers the rules
- Handles nested graphs with depth tracking (collapsed graphs at any depth; user functions and macros, project or plugin content, within the depth)
- Processes related structs/enums used by nodes
- Traces connections through knot nodes

### Serializer

**`Source/Public/Core/N2CSerializer.h` (`FN2CSerializer`)**

Static utility for serializing `FN2CBlueprint` to JSON and deserializing.

| Method | Description |
|--------|-------------|
| `ToJson(Blueprint, Style)` | Convert FN2CBlueprint to JSON string; `EN2CJsonStyle::Pretty` or `Condensed` per call (no static state) |
| `ToJson(Capture, Style, Guids)` | A capture's Blueprint; `EN2CJsonGuids::Table` adds the top-level `guids` table for agents (get-focused-blueprint), never for LLM prompts |
| `FromJson()` | Parse N2C JSON into `FN2CBlueprint`: 1.x (1.2.x and earlier 2.0 builds) is read into the v2 model, whose version is then `N2CJsonContract::Version`, and a 2.x document keeps its version; an unknown `blueprint_type`, `graph_type`, node or pin `type` is read as `Unknown` with its text kept and an unknown field is kept, each written back unchanged and reported as an `FN2CJsonReadWarning` (reader policy in `N2CJsonContract.h`, card 0445); a pin without `type` is Exec, `structs` and `enums` are read, an enum value needs only `name`; `FromJson(ToJson(x))` equals x for every capture golden |
| `NodeToJsonObject()` | Convert single node to JSON (for MCP tools) |

## Data Models

### Blueprint Structure

**`Source/Public/Models/N2CBlueprint.h`**

| Struct | Description |
|--------|-------------|
| `FN2CBlueprint` | Top-level container (version, metadata, graphs, structs, enums) |
| `FN2CMetadata` | Blueprint name, type (`EN2CBlueprintType`), class |
| `FN2CGraph` | Single graph with nodes and `FN2CFlows` |
| `FN2CFlows` | `Execution` (exec links "N3.P5->N4.P1", one per link; node pairs "N1->N2" when read from 1.x) and `Data`: output pin ref -> `FN2CDataFlowTargets` (`Targets`, every input pin it feeds, in link order) |
| `FN2CDataFlowTargets` | The input pin refs one output feeds (PR #7's `FN2CInputConnectors`, as an ordered array) |
| `FN2CVersion` | Version string, default `N2CJsonContract::Version` ("2.0.0") |

### N2C JSON versions

`Source/Public/Models/N2CJsonContract.h` holds the version and the field names the v2 contract reshaped, as
[decision 0003](../../.agents/decisions/0003-n2c-json-v2-contract.md) (provisional, card 0117) records them.

- **2.0.0** (card 0135): `flows.data` is an object keyed by the output pin ref. Its value is the input pin ref it
  feeds, or an array of them when it feeds several, so a graph without fan-out gets the same object as 1.0.0. Every
  data edge appears once. `flows.execution` has one "N3.P5->N4.P1" entry per exec link, exec output pin to exec
  input pin (card 0136). Names are stable identifiers: node `class`, `member_parent`, `member_name`, pin `name` =
  `PinName`, with the display hints `title` (nodes; 1.x calls it `name`) and `label` (pins) (card 0138). Map pins
  carry `value_type` and `value_sub_type`, and enum pins are `Enum`, not `Byte` (card 0238). Additive
  optional fields bump the minor; a breaking change bumps the major.
- **1.0.0** (1.2.x and earlier 2.0 builds): one string per output, so a fan-out kept one edge, node-only exec
  entries ("N1->N2"), and display text for names: a node `name` (read as the title) and display pin names (read as
  they are), so a 1.x document reads into the v2 model with no `class` and is written back with `title`. 2.0.0 documents saved by development builds between cards 0135 and 0136
  also have node-only exec entries: readers accept both forms in any readable version and keep them as they are.
  `FromJson`, `UN2CPythonBridge::ReadN2CJson` (Python `nodetocode.read_n2c_json`) and the validator read it through
  the 2.x line.
- **Later 2.x documents (card 0445):** the reader keeps what it does not know instead of rejecting or dropping it
  (`N2CJsonContract.h`, reader policy): unknown kinds become `Unknown` with their text (`UnknownBlueprintType`,
  `UnknownGraphType`, `UnknownType`), unknown fields at the top level and in `metadata`, graphs, nodes and pins are kept
  in `UnknownFields` and written after the known fields, and each gives one structured warning
  (`N2CJsonContract::ReadWarning`). The validator accepts them with a log warning.
  `NodeToCode.Serializer.ForwardCompat` reads `Source/Private/Tests/Fixtures/Capture/ForwardCompat/UnknownKinds_2.1.json`
  and writes it back byte for byte. `NodeToCode.Capture.Golden.V1Reader` reads the 1.0.0 documents in
  `Source/Private/Tests/Fixtures/Capture/V1` (earlier goldens and a 1.2.3 save); `Tests/Migration` reads the 1.2.3
  fixture's saved translation (row F2).

**User-Defined Types:**

| Struct | Description |
|--------|-------------|
| `FN2CStruct` | Blueprint-defined struct with members |
| `FN2CStructMember` | Member with type, container flags, key types |
| `FN2CEnum` | Blueprint-defined enum |
| `FN2CEnumValue` | Single enum value with optional comment |

**Enums:**

| Enum | Values |
|------|--------|
| `EN2CBlueprintType` | Normal, Const, MacroLibrary, Interface, LevelScript, FunctionLibrary |
| `EN2CGraphType` | EventGraph, Function, Composite, Macro, Construction, Animation, Struct, Enum |
| `EN2CStructMemberType` | Bool, Byte, Int, Float, String, Name, Text, Vector, Vector2D, Rotator, Transform, Class, Object, Struct, Enum, Array, Set, Map, Custom |

### Node Definition

**`Source/Public/Models/N2CNode.h`**

`FN2CNodeDefinition` contains:
- `ID` - Short identifier (e.g., "N1")
- `NodeType` - `EN2CNodeType` enum value
- `NodeClass` - The UK2Node class (JSON `class`), a stable identifier
- `Title` - Hint only: the menu title in source text, empty when it says no more than `MemberName` (JSON `title`;
  1.x documents call it `name`)
- `MemberParent` - The class or struct that declares the function/variable (a Blueprint by its name)
- `MemberName` - Function/variable/event/macro name as declared
- `Comment` - Node comment if any
- `bPure`, `bLatent` - Node behavior flags
- `InputPins`, `OutputPins` - Arrays of `FN2CPinDefinition`

**`EN2CNodeType`** categories (~100+ node types):

| Category | Examples |
|----------|----------|
| Function Calls | `CallFunction`, `CallArrayFunction`, `CallDelegate`, `CallParentFunction`, `FunctionEntry`, `FunctionResult` |
| Variables | `VariableSet`, `VariableGet`, `LocalVariable`, `FunctionParameter`, `TemporaryVariable` |
| Events | `Event`, `CustomEvent`, `ActorBoundEvent`, `ComponentBoundEvent`, `InputAction`, `InputKey` |
| Flow Control | `ForEachLoop`, `WhileLoop`, `ForLoop`, `Sequence`, `Branch`, `Select`, `Gate`, `MultiGate`, `DoOnce`, `Tunnel` |
| Switches | `Switch`, `SwitchInt`, `SwitchString`, `SwitchEnum`, `SwitchName` |
| Structs/Objects | `MakeStruct`, `BreakStruct`, `SetFieldsInStruct`, `StructMemberGet`, `StructMemberSet` |
| Containers | `MakeArray`, `MakeMap`, `MakeSet`, `GetArrayItem` |
| Casting | `DynamicCast`, `ClassDynamicCast`, `CastByteToEnum`, `ConvertAsset` |
| Delegates | `AddDelegate`, `CreateDelegate`, `ClearDelegate`, `RemoveDelegate`, `CallDelegate` |
| Async/Latent | `AsyncAction`, `BaseAsyncTask` |
| Components | `AddComponent`, `AddComponentByClass` |
| Utility | `ConstructObjectFromClass`, `Timeline`, `SpawnActor`, `FormatText`, `GetSubsystem` |
| Math/Logic | `MathExpression`, `EaseFunction`, `EnumEquality`, `CommutativeAssociativeBinaryOperator` |
| Special | `Self`, `Composite`, `MacroInstance`, `Message` |

### Pin Definition

**`Source/Public/Models/N2CPin.h`**

`FN2CPinDefinition` contains:
- `ID` - Short local ID (e.g., "P1")
- `Name` - `UEdGraphPin::PinName` (`execute`, `then`, `ReturnValue`); a pin read from 1.x keeps its display name
- `Label` - Hint only: the display label in source text, when it says more than `Name` (JSON `label`)
- `Type` - `EN2CPinType` enum value
- `SubType` - Optional subtype (e.g., struct name)
- `DefaultValue` - Default value if any
- `bConnected` - Whether pin is connected
- `bIsReference`, `bIsConst` - Modifier flags
- `bIsArray`, `bIsMap`, `bIsSet` - Container flags

**`EN2CPinType`** values:

| Category | Types |
|----------|-------|
| Basic | Exec, Boolean, Byte, Integer, Integer64, Float, Double, Real, String, Name, Text |
| Math | Vector, Vector2D, Vector4D, Rotator, Transform, Quat |
| Object | Object, Class, Interface, SoftObject, SoftClass, AssetId |
| Complex | Struct, Enum, Delegate, MulticastDelegate |
| Containers | Array, Set, Map |
| Assets | Material, Texture, StaticMesh, SkeletalMesh |
| Animation | Pose, Animation, BlendSpace |
| Special | FieldPath, Bitmask, Self, Index, Wildcard |

### Translation Response

**`Source/Public/Models/N2CTranslation.h`**

| Struct | Description |
|--------|-------------|
| `FN2CTranslationResponse` | Contains `FN2CGraphTranslation[]` and `FN2CTranslationUsage` |
| `FN2CGraphTranslation` | Translated code for one graph (name, type, class, code) |
| `FN2CGeneratedCode` | Declaration, implementation, and implementation notes |
| `FN2CTranslationUsage` | Input/output token counts |

## Node Processing Strategy

Handles diverse Blueprint node types using a strategy pattern.

### Framework Components

| Class | Description |
|-------|-------------|
| `IN2CNodeProcessor` | Interface with `Process()` method |
| `FN2CBaseNodeProcessor` | Base implementation with `ExtractNodeProperties()` |
| `FN2CNodeProcessorFactory` | Singleton factory mapping `EN2CNodeType` to processors |

### Specific Node Processors

**`Source/Public/Utils/Processors/` and `Source/Private/Utils/Processors/`**

| Processor | Handles |
|-----------|---------|
| `FN2CArrayProcessor` | `MakeArray`, `MakeMap`, `MakeSet`, `MakeContainer`, `GetArrayItem` |
| `FN2CCastProcessor` | `DynamicCast`, `ClassDynamicCast`, `CastByteToEnum` |
| `FN2CDelegateProcessor` | `CreateDelegate`, `AddDelegate`, `RemoveDelegate`, `ClearDelegate`, `CallDelegate` |
| `FN2CEventProcessor` | `Event`, `CustomEvent`, `ActorBoundEvent`, `ComponentBoundEvent` |
| `FN2CFlowControlProcessor` | `ExecutionSequence`, `IfThenElse`, `Select`, `Switch`, `MultiGate`, `DoOnceMultiInput` |
| `FN2CFunctionCallProcessor` | `CallFunction` and variations |
| `FN2CFunctionEntryProcessor` | `FunctionEntry`, `FunctionResult`, `MacroInstance` |
| `FN2CStructProcessor` | `MakeStruct`, `BreakStruct`, `SetFieldsInStruct`, `StructOperation` |
| `FN2CVariableProcessor` | `Variable`, `VariableGet`, `VariableSet` |

## Batch Translation System

Batch translation of multiple Blueprint graphs from `FN2CTagInfo` arrays.

### Types and Delegates

**`Source/Public/Models/N2CBatchTranslationTypes.h`**

| Type | Description |
|------|-------------|
| `EN2CBatchItemStatus` | Pending, Processing, Completed, Failed, Skipped |
| `FN2CBatchTranslationItem` | Internal tracking struct per graph (with cached JSON payload) |
| `FN2CBatchTranslationResult` | Summary with counts, timing, token usage, output path |
| `FN2CBatchJsonExportResult` | Result for JSON-only export (no LLM translation) |

**Delegates (Blueprint-compatible):**
- `FOnBatchItemTranslationComplete(TagInfo, Response, bSuccess, ItemIndex, TotalCount)`
- `FOnBatchTranslationComplete(Result)`
- `FOnBatchTranslationProgress(CurrentIndex, TotalCount, CurrentGraphName)`
- `FOnBatchJsonExportComplete(Result)`

**Native Delegates (C++ only, for Slate widgets):**
- `FOnBatchItemTranslationCompleteNative`
- `FOnBatchTranslationCompleteNative`
- `FOnBatchTranslationProgressNative`

### Orchestrator

**`Source/Public/LLM/N2CBatchTranslationOrchestrator.h` (`UN2CBatchTranslationOrchestrator`)**

Singleton orchestrator managing batch operations:

| Method | Description |
|--------|-------------|
| `StartBatchTranslation()` | Begin batch translation from TagInfo array |
| `CancelBatch()` | Cancel current batch (marks remaining as Skipped) |
| `IsBatchInProgress()` | Check if batch is running |
| `GetBatchProgress()` | Get progress percentage (0.0 - 1.0) |
| `GetCurrentItemIndex()` / `GetTotalItemCount()` | Progress tracking |
| `GetCurrentResult()` | Get current/partial result |
| `BatchExportJson()` | Export to JSON without LLM translation |

Key features:
- Loads blueprints via `FSoftObjectPath::TryLoad()` without editor focus
- Finds graphs by GUID in `UbergraphPages`, `FunctionGraphs`, `MacroGraphs`
- Sequential translation reusing `UN2CLLMModule::ProcessN2CJson()`
- Outputs to `BatchTranslation_{timestamp}/` with per-graph subfolders and `BatchSummary.json`
- Generates combined markdown file for easy LLM chat upload
- Blueprint caching to avoid reloading

### Blueprint Library

**`Source/Public/BlueprintLibraries/N2CBatchTranslationBlueprintLibrary.h`**

| Function | Description |
|----------|-------------|
| `StartBatchTranslation(TaggedGraphs)` | Direct array input |
| `TranslateGraphsWithTag(Tag, Category)` | All graphs with specific tag |
| `TranslateGraphsInCategory(Category)` | All graphs in category |
| `CancelBatchTranslation()` | Cancel current batch |
| `GetBatchProgress()` | Get progress percentage |
| `GetBatchOrchestrator()` | Get orchestrator for delegate binding |
| `BatchExportJson(TaggedGraphs, bMinifyJson)` | Export JSON without LLM |
| `ExportGraphsWithTagToJson(Tag, Category, bMinifyJson)` | Export tagged graphs to JSON |
| `ExportGraphsInCategoryToJson(Category, bMinifyJson)` | Export category to JSON |

## Translation Workflow

1. **Collection**: `FN2CNodeCollector` gathers nodes from the selected graph(s)
2. **Capture**: `N2C::CaptureNodes` converts nodes to `FN2CBlueprint` format (a value the caller owns)
   - Assigns unique IDs to nodes (N1, N2...) and pins (P1, P2...)
   - Uses `FN2CNodeProcessorFactory` to get specialized processors
   - Builds execution and data flow connections
   - Processes related structs/enums
3. **Serialization**: `FN2CSerializer` converts to JSON string
4. **LLM Processing**: JSON is sent to the configured LLM provider
5. **Response Parsing**: LLM response is parsed into `FN2CTranslationResponse`
6. **Display**: Generated code is shown in the code editor widget
