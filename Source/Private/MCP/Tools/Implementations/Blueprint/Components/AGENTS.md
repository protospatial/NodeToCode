# Blueprint Component Tools

This directory contains the MCP tools in the 'Blueprint Components' category: listing, adding and deleting the
components of an Actor Blueprint. All three work on the Blueprint's `SimpleConstructionScript` (SCS), the components
added in the Blueprint editor's Components panel.

## What the Tools See

- **SCS nodes only.** Native components that a C++ parent creates (a Character's `CapsuleComponent`, `Mesh`,
  `CharacterMovement` and `Arrow`) are not SCS nodes, so they are never listed and cannot be named as a parent or
  deleted: on a new Character Blueprint, list-components-in-actor returns `"components": []`, `"totalCount": 0` and
  `"rootComponent": ""`. Components a parent Blueprint adds are read from that Blueprint's SCS
  (`FN2CMcpComponentUtils::GetInheritedSCSNodes`) and only by the list and delete tools.
- **Target.** `blueprintPath` is an asset path (`/Game/Blueprints/BP_Door` or `/Game/Blueprints/BP_Door.BP_Door`),
  resolved by `FN2CMcpBlueprintUtils::ResolveBlueprint`; without it, the Blueprint of the active Blueprint editor.
  A path that does not resolve: `ASSET_NOT_FOUND: Blueprint not found at path: <path>`; no path and no editor:
  `NO_ACTIVE_BLUEPRINT: ...`; a Blueprint whose parent is not an Actor (a component or widget Blueprint):
  `NOT_ACTOR_BLUEPRINT|Blueprint must derive from Actor to have components`.
- **Errors** are text results with `isError` true, in the form `CODE|message`.
- **No undo.** None of the tools opens an `FScopedTransaction`; add and delete mark the Blueprint structurally
  modified, compile it once (`FN2CMcpBlueprintUtils::MarkModified` and `Compile`, card 0151) and leave the package
  dirty, unsaved. `compilationStatus` and the result's `compile` object carry the compiler's real counts and
  messages (see the Graph folder's AGENTS.md for the `compile` fields).
- **Known issue (card 0274):** there is no target besides `blueprintPath` and the active editor.
- **Known issue (card 0167):** the tools edit SCS nodes directly instead of through the engine's
  `USubobjectDataSubsystem`, which causes the attachment and deletion behaviour described below.

## Available Tools

### list-components-in-actor
- **Description**: Lists the SCS components of an Actor Blueprint as a tree (read-only, `readOnlyHint`)
- **Parameters**:
  - `blueprintPath` (optional): See "What the Tools See"
  - `includeInherited` (optional, default true): Also list the SCS components of parent Blueprints
  - `componentTypeFilter` (optional, default `all`): `scene` (`USceneComponent`), `actor` (`UActorComponent` but not
    `USceneComponent`), `primitive` (`UPrimitiveComponent`); any other value lists everything
    (`FN2CMcpComponentUtils::PassesComponentTypeFilter`). A component that fails the filter is left out together with
    its whole subtree (`BuildNodeHierarchyRecursive`), so `primitive` returns nothing when the root is a plain
    `SceneComponent` or a `SpringArmComponent`
- **Returns**: See "Output Format"

### add-component-class-to-actor
- **Description**: Adds an SCS node of the given class and compiles the Blueprint
  (`FN2CMcpComponentUtils::CreateSCSNode`, `SetupComponentAttachment`)
- **Parameters**:
  - `componentClass` (required): Class object path, found with `FindObject` then `LoadObject`
    (`ResolveComponentClass`): `/Script/Engine.StaticMeshComponent`, or a component Blueprint's generated class
    `/Game/Components/BP_Health.BP_Health_C`. A bare class name (`StaticMeshComponent`) is not found
  - `componentName` (optional): Variable name. Without it, the class name minus a trailing `Component`, made unique
    with a number: `StaticMesh`, then `StaticMesh1` (`GenerateUniqueComponentName`); a Blueprint component class keeps
    its generated class name (`BP_Health_C`)
  - `parentComponent` (optional): Name of an SCS component of this Blueprint (not a native or inherited one), for
    scene components; ignored for other components
  - `attachSocketName` (optional): Stored as the node's `AttachToName` when `parentComponent` is given
  - `relativeTransform` (optional, scene components): `{"location": {"x","y","z"}, "rotation": {"pitch","yaw","roll"},
    "scale": {"x","y","z"}}`; each part and each number is optional (location and rotation default to 0, scale to 1)
  - `blueprintPath` (optional): See "What the Tools See"
- **Where the component ends up**: `CreateSCSNode` adds every node as a root node, and the engine's
  `USimpleConstructionScript::AddNode` then makes the first scene component the Blueprint's root in place of the
  `DefaultSceneRoot`. `SetupComponentAttachment` only sets the node's parent name, and the compile that follows moves
  the node under the scene root. So, after the call, every scene component the tool adds is a direct child of the
  root, whatever `parentComponent` names: in the doc's Spring Arm example `FollowCamera` ends up under `CameraBoom`
  only because `CameraBoom` became the root, and a component added with `parentComponent: "B"`, where `B` is itself a
  child of the root, is listed beside `B`, not under it. The socket name is kept. Components that are not scene
  components are listed as separate roots.
- **Returns**: `componentName`, `componentClass` (short name), `componentClassPath`, `nodeGuid` (the SCS node's
  `VariableGuid`, 32 hex digits), `isSceneComponent`, `attachedTo` and `attachSocketName` (echo the arguments
  whenever `parentComponent` was given, also for a non-scene component that was not attached),
  `compilationStatus` (`success`, `errorCount`, `warningCount`, `compilationTime` from the compile), `compile`,
  `nextSteps`
- **Errors**: `CLASS_NOT_FOUND|Component class '<path>' not found`, `NOT_COMPONENT|Class 'Actor' is not an
  ActorComponent`, `CANNOT_INSTANTIATE|Class 'MovementComponent' cannot be instantiated (abstract, deprecated, or has
  newer version)`, `NAME_EXISTS|Component with name 'CameraBoom' already exists`, `PARENT_NOT_FOUND|Parent component
  'Mesh' not found`, `EMPTY_CLASS_PATH|...`, plus the target errors above
- **Known issue (card 0167):** the node is created before `parentComponent` is looked up, so a `PARENT_NOT_FOUND`
  error leaves the new component in the Blueprint (as a root, uncompiled); a parent other than the root is not kept
  (above)

### delete-component-in-actor
- **Description**: Removes an SCS node of this Blueprint (`FN2CMcpComponentUtils::DeleteSCSNode`) and compiles
- **Parameters**:
  - `componentName` (required): The component's variable name
  - `deleteChildren` (optional, default false): See "Deleting Components"
  - `blueprintPath` (optional): See "What the Tools See"
- **Returns**: `deletedComponentName`, `deletedComponentClass`, `deletedComponentGuid`, `affectedChildren` (one
  entry per direct child with `action` `reparented` and `newParent` `root`; with `deleteChildren` true, one entry per
  descendant with `action` `deleted`), `childrenDeleted`, `compilationStatus` (as above), `compile`, `nextSteps`
- **Errors**: `COMPONENT_NOT_FOUND|Component 'X' not found in Blueprint`,
  `INHERITED_COMPONENT|Cannot delete components inherited from parent Blueprint` (a parent Blueprint's component),
  `NO_COMPONENTS|...` (no SCS), plus the target errors above. A native component's name gives `COMPONENT_NOT_FOUND`
- **Known issue (card 0167):** Get nodes and bound events of the deleted component are left in the graphs; there is
  no confirmation (card 0212)

## Deleting Components

What happens to the children of a deleted component, observed on UE 5.8 with the hierarchy built in the Blueprint
editor (DefaultSceneRoot > L1 > L2 > L3 > L4):

- **`deleteChildren: false`** (default): the children are kept, but they are not moved to the deleted component's
  parent. `DeleteSCSNode` reads the deleted node's `ParentComponentOrVariableName`, which is empty for a node that
  sits in another node's child list, so it calls `USCS_Node::SetParent(nullptr)` on each child (the engine logs
  `Ensure condition failed: InParentNode` the first time) and removes the node; the compile then puts each orphaned
  child under the Blueprint's scene root. Deleting `L3` leaves DefaultSceneRoot > L1 > L2 and DefaultSceneRoot > L4,
  with L4's own children still under it. The result says `"newParent": "root"`. Deleting the root itself moves its
  children under a new `DefaultSceneRoot` the engine creates.
- **`deleteChildren: true`**: the node and all its descendants are removed (`GetAllChildNodes`, deepest first).
- **DefaultSceneRoot**: deleting it is reported as a success, but the engine adds the default root back
  (`USimpleConstructionScript::ValidateSceneRootNodes`) with the same `nodeGuid`, so nothing changes. The
  `DEFAULT_ROOT_WITH_CHILDREN` check in `CanDeleteNode` compares the template name with `DefaultSceneRoot`, while
  the template is named `DefaultSceneRoot_GEN_VARIABLE`, so that error is never returned.
- **Inherited components** cannot be deleted (`INHERITED_COMPONENT`); `INHERITED_NODE` exists in `CanDeleteNode` but is
  not reached, because the tool looks the name up in this Blueprint's SCS first.
- **Known issue (card 0167):** children are not reparented to the deleted component's parent, the call fires an
  engine ensure, and the DefaultSceneRoot guard never fires.

## Output Format (list-components-in-actor)

`FN2CMcpComponentUtils::BuildComponentHierarchy` and `SCSNodeToJson`, for an Actor Blueprint with a Spring Arm root
and a camera under it:

```json
{
  "components": [
    {
      "name": "CameraBoom",
      "className": "SpringArmComponent",
      "classPath": "/Script/Engine.SpringArmComponent",
      "nodeGuid": "7A3FC83C471769D1B943DD9580B59782",
      "isSceneComponent": true,
      "isRootComponent": true,
      "parentComponent": null,
      "attachSocketName": "None",
      "transform": {
        "location": { "x": 0, "y": 0, "z": 60 },
        "rotation": { "pitch": -10.000000000000004, "yaw": 0, "roll": -0 },
        "scale": { "x": 1, "y": 1, "z": 1 }
      },
      "isInherited": false,
      "children": [
        {
          "name": "FollowCamera",
          "className": "CameraComponent",
          "classPath": "/Script/Engine.CameraComponent",
          "nodeGuid": "1AE67F734F1A0516DC4E81827C74351F",
          "isSceneComponent": true,
          "isRootComponent": true,
          "parentComponent": null,
          "attachSocketName": "None",
          "transform": { "...": "..." },
          "isInherited": false,
          "children": []
        }
      ]
    }
  ],
  "totalCount": 1,
  "rootComponent": "CameraBoom",
  "blueprintName": "BP_InteractableObject",
  "blueprintPath": "/Game/Blueprints/BP_InteractableObject.BP_InteractableObject"
}
```

- **The tree is in `children`.** `isRootComponent` and `parentComponent` come from the node's
  `ParentComponentOrVariableName`, which is only set for a root node attached to a native or inherited component, so
  nearly every node, children included, shows `isRootComponent: true` and `parentComponent: null`. Read the nesting
  from `children`.
- **`totalCount`** is the number of top-level entries in `components` (`ComponentsArray.Num()`), not the number of
  components: the tree above holds two components and has `totalCount` 1.
- **`rootComponent`** is the first SCS root node's name, or `""` when the Blueprint has none (a Character
  Blueprint, whose root is native).
- **`className`** is the class's short name (`SpringArmComponent`, no `U` prefix); `attachSocketName` is `"None"`
  when there is no socket; `transform` is relative and only present for scene components.
- **Inherited components** (`includeInherited` true) get `isInherited: true` and are appended after this Blueprint's
  own. `GetInheritedSCSNodes` returns every node of the parent's SCS, not only its roots, so an inherited child is
  listed twice, once inside its parent's `children` and once at the top level, and `totalCount` counts both.
- A Blueprint with no SCS returns `{"components": [], "totalCount": 0, "rootComponent": ""}` without the
  `blueprintName` and `blueprintPath` fields (`GetBlueprintSCS` creates one, so in practice every Actor Blueprint has
  an SCS).

The result is not marked as untrusted content (names only; card 0124).

## Examples

The curl examples assume `$SID` holds a session id from `initialize` (see "Calling the Server with curl" in
`Source/Private/MCP/Tools/AGENTS.md`); every POST after `initialize` must send it.

```bash
# List the components of a Blueprint
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "list-components-in-actor",
      "arguments": {
        "blueprintPath": "/Game/Blueprints/BP_InteractableObject"
      }
    },
    "id": 1
  }'

# Add a spring arm (on a new Actor Blueprint it replaces DefaultSceneRoot as the root)
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "add-component-class-to-actor",
      "arguments": {
        "componentClass": "/Script/Engine.SpringArmComponent",
        "componentName": "CameraBoom",
        "relativeTransform": {
          "location": { "x": 0, "y": 0, "z": 60 },
          "rotation": { "pitch": -10, "yaw": 0, "roll": 0 }
        },
        "blueprintPath": "/Game/Blueprints/BP_InteractableObject"
      }
    },
    "id": 2
  }'

# Then a camera under it (CameraBoom is the root here, so the camera stays under it)
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "add-component-class-to-actor",
      "arguments": {
        "componentClass": "/Script/Engine.CameraComponent",
        "componentName": "FollowCamera",
        "parentComponent": "CameraBoom",
        "blueprintPath": "/Game/Blueprints/BP_InteractableObject"
      }
    },
    "id": 3
  }'

# Delete a component and everything under it
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "delete-component-in-actor",
      "arguments": {
        "componentName": "FollowCamera",
        "deleteChildren": true,
        "blueprintPath": "/Game/Blueprints/BP_InteractableObject"
      }
    },
    "id": 4
  }'
```

A Character's mesh cannot be a parent: `"parentComponent": "Mesh"` on a Character Blueprint answers
`PARENT_NOT_FOUND|Parent component 'Mesh' not found` (and leaves the new component behind, card 0167).

## Workflow Integration

1. `search-blueprint-classes` with `classType: "actorComponent"` to find a component class, then
   `add-component-class-to-actor` with its `classPath`, then `list-components-in-actor` to see where it ended up.
2. A component is a Blueprint variable of its own name, but not a member variable the Variables tools know:
   `create-get-member-variable-node` with `CameraBoom` answers `Variable 'CameraBoom' not found`. To read it in a
   graph, search `search-blueprint-nodes` for `Get CameraBoom` (a `K2Node_VariableGet` with `member_name`
   `CameraBoom`) and add it with `add-bp-node-to-active-graph`. The `nextSteps` hint to make a variable with
   `create-variable` and the component's class path makes a separate, empty reference variable.
