# Blueprint Component Tools

This directory contains the MCP tools in the `BlueprintComponentTools` toolset: listing, adding and deleting the
components of an Actor Blueprint. Add and delete go through the engine's `USubobjectDataSubsystem`, as the Blueprint
editor's Components panel does (card 0167); the list tool reads the hierarchy without changing anything
(`FN2CMcpComponentUtils::BuildComponentTree`).

## Status: frozen, bug fixes only (dec-5)

Every tool here (`BlueprintComponentTools`) is frozen. Decision record [0007](../../../../../../../.agents/decisions/0007-crud-tool-families-fix-then-freeze.md): owner decision card 0140, applied
provisionally as the review's option 2, with card 0089 applied provisionally as option (b).

- **Accepted:** bug fixes, meaning a change that fixes a crash or hang (a modal dialog in an unattended call
  counts), data loss, Blueprint or project corruption, or a result that misreports what happened (success for an
  edit that did not happen, wrong counts, duplicates, a wrong type in a read). Also accepted, since they add no
  capability: explicit targeting arguments on existing tools (review next-6), security hardening, building on the
  supported engines, tests, docs that match the code, and removing dead code.
- **Not accepted:** new tools in this family, and new arguments, modes or result fields that
  add capability. On UE 5.8 that work belongs to Epic's EditorToolset (`ActorTools.add_component`, `remove_component`, `get_components`); on 5.6 and 5.7 it waits for the
  owner.
- **Not frozen:** comprehension, export, translation, batch and tag tools: `BlueprintDiscoveryTools` (except the
  tools in `N2CMcpEpicOverlap::Tools`), `BlueprintTagTools`, `TranslationTools`, and also `FileSystemTools` and
  `PythonTools`.
- **Fix cards still owed here:** 0287 (list-components-in-actor duplicates, missing native components, the type filter), and 0274 (explicit
  targets, admitted as next-6).

The whole policy, and how the frozen set is read from the code, is "Frozen tool families" in
`Source/Private/MCP/Tools/AGENTS.md`.

## What the Tools See

- **Every component of the actor**, as the Components panel shows them: the native components of the C++ parent class
  (a Character's `CapsuleComponent`, `Mesh`, `CharacterMovement` and `Arrow`, read from the class default object),
  the components parent Blueprints add (their SCS), and this Blueprint's own (its `SimpleConstructionScript`).
  A native component is named by its property name (`Mesh`); its object name (`CharacterMesh0`) is also accepted.
- **Target.** `blueprintPath` is an asset path (`/Game/Blueprints/BP_Door` or `/Game/Blueprints/BP_Door.BP_Door`),
  resolved by `FN2CMcpBlueprintUtils::ResolveBlueprint`; without it, the Blueprint of the active Blueprint editor.
  A path that does not resolve: `ASSET_NOT_FOUND: Blueprint not found at path: <path>`; no path and no editor:
  `NO_ACTIVE_BLUEPRINT: ...`; a Blueprint whose parent is not an Actor (a component or widget Blueprint):
  `NOT_ACTOR_BLUEPRINT|Blueprint must derive from Actor to have components`; a level script, macro library,
  function library, interface or const Blueprint: `NO_CONSTRUCTION_SCRIPT|Blueprint '<name>' cannot have components
  ...` (`FN2CMcpComponentUtils::CheckComponentBlueprint`). No tool ever creates a `SimpleConstructionScript`.
- **Errors** are text results with `isError` true, in the form `CODE|message`. add and delete check everything
  (Blueprint, class, name, parent, socket, component) before they change anything, so a refused call leaves the
  Blueprint, its dirty flag and the undo history as they were.
- **Undo.** Each add or delete is one `FScopedTransaction` ("Add Component 'X'", "Delete Component 'X'"): Edit > Undo
  restores the components, their attachment and the graph nodes the delete removed. The Blueprint is compiled once
  after the transaction closes (card 0151); `compilationStatus` and the result's `compile` object carry the
  compiler's real counts and messages (see the Graph folder's AGENTS.md for the `compile` fields). The package is
  left dirty, unsaved.
- **Known issue (card 0274):** there is no target besides `blueprintPath` and the active editor.

## Available Tools

### list-components-in-actor
- **Description**: Lists the components of an Actor Blueprint as a tree (read-only, `readOnlyHint`): changes nothing,
  adds no undo step, leaves the package clean
- **Parameters**:
  - `blueprintPath` (optional): See "What the Tools See"
  - `includeInherited` (optional, default true): Also list the native components and the components parent
    Blueprints add. When false, only this Blueprint's own components are listed; one attached to an inherited or
    native component moves up to the top level and keeps its `parentComponent`
  - `componentTypeFilter` (optional, default `all`): `scene` (`USceneComponent`), `actor` (`UActorComponent` but not
    `USceneComponent`), `primitive` (`UPrimitiveComponent`); any other value lists everything
    (`FN2CMcpComponentUtils::PassesComponentTypeFilter`). A component that fails the filter is left out and its
    listed descendants move up to where it was
- **Returns**: See "Output Format"

### add-component-class-to-actor
- **Description**: Adds a component with `USubobjectDataSubsystem::AddNewSubobject`, names it with
  `RenameSubobject`, and compiles the Blueprint
- **Parameters**:
  - `componentClass` (required): The class (`FN2CMcpComponentUtils::ResolveComponentClass`): a class path
    (`/Script/Engine.StaticMeshComponent`), a component Blueprint's generated class
    (`/Game/Components/BP_Health.BP_Health_C`) or the Blueprint's own path (`_C` is added), or the name of a loaded
    class (`StaticMeshComponent`, `UStaticMeshComponent`, `BP_Health`; native classes first). The class part of a
    path whose module is wrong (`/Script/Engin.StaticMeshComponent`) is looked up as a name
  - `componentName` (optional): Variable name; must be a valid name not in use in the Blueprint (`NAME_EXISTS`,
    `INVALID_NAME`). Without it, the engine's: the class name minus a trailing `Component`, or a Blueprint class's
    name minus `_C`, made unique with a number (`StaticMesh`, then `StaticMesh1`)
  - `parentComponent` (optional, scene components only): The scene component to attach to: one this Blueprint adds,
    one a parent Blueprint adds, or a native one (`Mesh` on a Character). Defaults to the actor's root component
    (`DefaultSceneRoot` on a new Actor Blueprint, `CapsuleComponent` on a Character)
  - `attachSocketName` (optional, scene components only): A socket of the parent; it must exist on it
    (`DoesSocketExist` or `GetAllSocketNames`, so `SpringEndpoint` on a spring arm, a mesh's sockets once the mesh
    asset is set)
  - `relativeTransform` (optional, scene components): `{"location": {"x","y","z"}, "rotation": {"pitch","yaw","roll"},
    "scale": {"x","y","z"}}`; each part and each number is optional (location and rotation default to 0, scale to 1).
    The new component's relative transform is this (or identity), not conformed to its parent. Ignored for other
    components
  - `blueprintPath` (optional): See "What the Tools See"
- **Where the component ends up**: under `parentComponent`, or the root, as the Components panel attaches it, and it
  stays there after compiles. A parent in this Blueprint takes the new node as a child node; an inherited or native
  parent is recorded on the new root node (`USCS_Node::SetParent`). The `DefaultSceneRoot` is kept: the first
  component added to a new Actor Blueprint goes under it. A component that is not a scene component is attached to
  nothing.
- **Returns**: `componentName`, `componentClass` (short name), `componentClassPath`, `nodeGuid` (the SCS node's
  `VariableGuid`, 32 hex digits), `isSceneComponent`, `attachedTo` and `attachSocketName` (where the component is
  after the compile, read back from the hierarchy; absent for a component that is not a scene component),
  `isRootComponent`, `compilationStatus` (`success`, `errorCount`, `warningCount`, `compilationTime` from the
  compile), `compile`, `nextSteps`
- **Errors**: `CLASS_NOT_FOUND|Component class '<path>' not found. ...`, `NOT_COMPONENT|Class 'Actor' is not an
  ActorComponent`, `CANNOT_INSTANTIATE|Class 'MovementComponent' cannot be instantiated (...)`,
  `NAME_EXISTS|Component with name 'CameraBoom' already exists` (or the name validator's text for a name used by a
  variable or function), `INVALID_NAME|...`, `PARENT_IS_SELF|parentComponent 'X' is the name of the component being
  added`, `PARENT_NOT_FOUND|Parent component 'CamerBoom' not found. Scene components: DefaultSceneRoot, CameraBoom`,
  `PARENT_NOT_SCENE|...`, `CANNOT_ATTACH|...` (the parent or the class refuses the attachment),
  `SOCKET_NOT_FOUND|'CameraBoom' has no socket 'Tip'. Its sockets: SpringEndpoint`,
  `NOT_SCENE_COMPONENT|'RotatingMovementComponent' is not a SceneComponent, so it is not attached to anything: leave
  out parentComponent and attachSocketName`, `EMPTY_CLASS_PATH|...`, `ADD_FAILED|...` (the engine refused), plus the
  target errors above

### delete-component-in-actor
- **Description**: Deletes a component this Blueprint adds with `USubobjectDataSubsystem::DeleteSubobjects`, and
  compiles
- **Parameters**:
  - `componentName` (required): The component's variable name
  - `deleteChildren` (optional, default false): See "Deleting Components"
  - `blueprintPath` (optional): See "What the Tools See"
- **Returns**: `deletedComponentName`, `deletedComponentClass`, `deletedComponentGuid`, `affectedChildren` (without
  `deleteChildren`: one entry per direct child, `action` `reparented`, `newParent` the component it is now attached
  to and `isRootComponent`; with it: one entry per descendant, `action` `deleted`), `childrenDeleted`,
  `removedVariableNodes` (the Get and Set nodes of the deleted components the engine removed: `nodeGuid`, `title`,
  `graph`, `component`), `boundEvents` (component events such as On Component Hit bound to a deleted component, same
  fields), `compilationStatus` (as above), `compile`, `nextSteps`
- **Errors**: `COMPONENT_NOT_FOUND|Component 'X' not found in Blueprint`, `NATIVE_COMPONENT|'Mesh' is created by the
  C++ class Character and cannot be deleted from a Blueprint`, `INHERITED_COMPONENT|Cannot delete components inherited
  from parent Blueprint ('CameraBoom' is added by BP_Base)`, `DEFAULT_SCENE_ROOT|...` (see below), `CANNOT_DELETE`
  and `DELETE_FAILED` (the engine refused), plus the target errors above
- **Known issue (card 0212):** there is no confirmation before a delete

## Deleting Components

- **`deleteChildren: false`** (default): the children are kept and move up, as in the Components panel
  (`USimpleConstructionScript::RemoveNodeAndPromoteChildren`): deleting `L3` from DefaultSceneRoot > L1 > L2 > L3 > L4
  leaves DefaultSceneRoot > L1 > L2 > L4. Deleting the root component makes its first scene child the new root, with
  the other children under it; deleting a root with no children brings the `DefaultSceneRoot` back.
- **`deleteChildren: true`**: the component and everything under it are deleted.
- **Graph nodes**: the engine removes the Get and Set nodes of every deleted component (listed in
  `removedVariableNodes`; Undo puts them back). Events bound to a deleted component stay in the graph, unbound, and
  never fire; they are listed in `boundEvents` for the agent to delete (delete-blueprint-node) or undo. The engine's
  own delete would also open the Blueprint editor on the first such event and post a message log; the tool keeps the
  events out of that path for the call, so a delete never opens an editor.
- **DefaultSceneRoot** cannot be deleted: `DEFAULT_SCENE_ROOT|DefaultSceneRoot is the placeholder root the engine keeps
  while no other scene component is the root; it cannot be deleted (the engine would add it back). Delete or move its
  children instead`.
- **Native and inherited components** cannot be deleted from this Blueprint (`NATIVE_COMPONENT`,
  `INHERITED_COMPONENT`).

## Output Format (list-components-in-actor)

`FN2CMcpComponentUtils::BuildComponentHierarchy` and `ComponentToJson`, for an Actor Blueprint with a spring arm and a
camera under it:

```json
{
  "components": [
    {
      "name": "DefaultSceneRoot",
      "className": "SceneComponent",
      "classPath": "/Script/Engine.SceneComponent",
      "nodeGuid": "1D3C9E0F4B2A8C5D6E7F801122334455",
      "isSceneComponent": true,
      "isRootComponent": true,
      "parentComponent": null,
      "attachSocketName": "None",
      "transform": { "...": "..." },
      "isInherited": false,
      "isNative": false,
      "children": [
        {
          "name": "CameraBoom",
          "className": "SpringArmComponent",
          "classPath": "/Script/Engine.SpringArmComponent",
          "nodeGuid": "7A3FC83C471769D1B943DD9580B59782",
          "isSceneComponent": true,
          "isRootComponent": false,
          "parentComponent": "DefaultSceneRoot",
          "attachSocketName": "None",
          "transform": {
            "location": { "x": 0, "y": 0, "z": 60 },
            "rotation": { "pitch": -10.000000000000004, "yaw": 0, "roll": -0 },
            "scale": { "x": 1, "y": 1, "z": 1 }
          },
          "isInherited": false,
          "isNative": false,
          "children": [
            {
              "name": "FollowCamera",
              "className": "CameraComponent",
              "classPath": "/Script/Engine.CameraComponent",
              "nodeGuid": "1AE67F734F1A0516DC4E81827C74351F",
              "isSceneComponent": true,
              "isRootComponent": false,
              "parentComponent": "CameraBoom",
              "attachSocketName": "SpringEndpoint",
              "transform": { "...": "..." },
              "isInherited": false,
              "isNative": false,
              "children": []
            }
          ]
        }
      ]
    }
  ],
  "totalCount": 3,
  "rootComponent": "DefaultSceneRoot",
  "blueprintName": "BP_InteractableObject",
  "blueprintPath": "/Game/Blueprints/BP_InteractableObject.BP_InteractableObject"
}
```

- **The tree is in `children`**, and `parentComponent` names each component's parent (null at the top level).
  `isRootComponent` is true for the actor's root component only.
- **Top level**: the root component, and components that are not scene components (attached to nothing).
- **`totalCount`** is the number of components listed (the tree above holds three).
- **`rootComponent`** is the actor's root component's name: a native root (`CapsuleComponent` on a Character), else
  the first Blueprint's scene root; `""` when there is none.
- **Native components** have `isNative` and `isInherited` true, an empty `nodeGuid`, and `objectName` when their
  object name differs from their property name (`"name": "Mesh", "objectName": "CharacterMesh0"`). **Components a
  parent Blueprint adds** have `isInherited` true and `inheritedFrom` (that Blueprint's name); their `transform` is
  this Blueprint's override when it has one. Each component is listed once.
- **`className`** is the class's short name (`SpringArmComponent`, no `U` prefix); `attachSocketName` is `"None"`
  when there is no socket; `transform` is relative and only present for scene components.

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

# Add a spring arm (under the root: DefaultSceneRoot on a new Actor Blueprint)
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

# Then a camera on the spring arm's end
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
        "attachSocketName": "SpringEndpoint",
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

On a Character Blueprint, `"parentComponent": "Mesh"` attaches to the inherited skeletal mesh (with
`attachSocketName` one of its mesh's sockets).

## Workflow Integration

1. `search-blueprint-classes` with `classType: "actorComponent"` to find a component class, then
   `add-component-class-to-actor` with its `classPath`, then `list-components-in-actor` to see the tree.
2. A component is a Blueprint variable of its own name, but not a member variable the Variables tools know:
   `create-get-member-variable-node` with `CameraBoom` answers `Variable 'CameraBoom' not found`. To read it in a
   graph, search `search-blueprint-nodes` for `Get CameraBoom` (a `K2Node_VariableGet` with `member_name`
   `CameraBoom`) and add it with `add-bp-node-to-active-graph`.

## Tests

`NodeToCode.MCP.Components` (`Source/Private/Tests/MCP/N2CComponentTools.spec.cpp`) runs the three tools on transient
Actor, Character, child and macro-library Blueprints: attachment kept after compiles at three levels, refusals that
change nothing, class lookup, native and inherited parents, deletes with and without children, Get-node removal,
bound events, DefaultSceneRoot, and exact Undo then Redo of each edit. Live: `Tests/mcp_probe/live_components_0167.py`
(claude -p).
