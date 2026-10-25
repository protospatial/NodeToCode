# Blueprint Component Tools

This directory contains MCP tools for managing components in Blueprint actors. These tools provide functionality for listing, adding, deleting, and manipulating the component hierarchy within actor Blueprints.

## Available Tools

### list-components-in-actor
- **Description**: Lists all components in a Blueprint actor with their hierarchy, types, and properties
- **Parameters**:
  - `blueprintPath` (optional): Asset path of the Blueprint. Uses focused Blueprint if not provided
  - `includeInherited` (optional): Include components from parent classes (default: true)
  - `componentTypeFilter` (optional): Filter by type - "all", "scene", "actor", "primitive" (default: "all")
- **Returns**: Hierarchical JSON structure showing all components with their relationships, transforms, and metadata
- **Use Case**: Understanding the component structure of a Blueprint before modifications

### add-component-class-to-actor
- **Description**: Adds a component of a specified class to a Blueprint actor
- **Parameters**:
  - `componentClass` (required): Class path of the component (e.g., '/Script/Engine.StaticMeshComponent')
  - `componentName` (optional): Custom name for the component. Auto-generated if not provided
  - `parentComponent` (optional): Name of parent component for attachment (scene components only)
  - `attachSocketName` (optional): Socket name on parent for attachment
  - `relativeTransform` (optional): Transform object with location, rotation, and scale
  - `blueprintPath` (optional): Asset path of the Blueprint. Uses focused Blueprint if not provided
- **Returns**: Details about the created component including name, class, GUID, and compilation status
- **Use Case**: Programmatically building component hierarchies in Blueprint actors

### delete-component-in-actor
- **Description**: Deletes a component from a Blueprint actor
- **Parameters**:
  - `componentName` (required): Name of the component to delete
  - `deleteChildren` (optional): If true, deletes all child components. If false, reparents children to deleted component's parent (default: false)
  - `blueprintPath` (optional): Asset path of the Blueprint. Uses focused Blueprint if not provided
- **Returns**: Information about the deleted component and affected children, including reparenting details
- **Use Case**: Removing components from Blueprint actors while managing child component relationships

## Component Type Filters

The `componentTypeFilter` parameter supports these categories:
- **all**: No filtering, returns all components
- **scene**: Only components derived from USceneComponent (have transforms)
- **actor**: Only components derived from UActorComponent but not USceneComponent
- **primitive**: Only components derived from UPrimitiveComponent (renderable/collidable)

## Common Component Classes

When using `add-component-class-to-actor`, common component classes include:

### Scene Components
- `/Script/Engine.SceneComponent` - Basic scene component with transform
- `/Script/Engine.StaticMeshComponent` - For static mesh rendering
- `/Script/Engine.SkeletalMeshComponent` - For skeletal mesh rendering
- `/Script/Engine.CameraComponent` - Camera component
- `/Script/Engine.SpringArmComponent` - Spring arm for cameras
- `/Script/Engine.ArrowComponent` - Debug arrow visualization

### Collision Components
- `/Script/Engine.BoxComponent` - Box collision shape
- `/Script/Engine.SphereComponent` - Sphere collision shape
- `/Script/Engine.CapsuleComponent` - Capsule collision shape

### Actor Components (Non-Scene)
- `/Script/Engine.ActorComponent` - Base non-spatial component
- `/Script/Engine.MovementComponent` - Base movement component
- `/Script/Engine.CharacterMovementComponent` - Character movement

### Light Components
- `/Script/Engine.PointLightComponent` - Point light
- `/Script/Engine.SpotLightComponent` - Spot light
- `/Script/Engine.DirectionalLightComponent` - Directional light

### Audio Components
- `/Script/Engine.AudioComponent` - Audio playback

### Particle Components
- `/Script/Engine.ParticleSystemComponent` - Legacy particle systems
- `/Script/Niagara.NiagaraComponent` - Niagara particle systems

## Usage Examples

### List All Components in a Blueprint
The curl examples assume `$SID` holds a session id from `initialize` (see "Calling the Server with curl" in `Source/Private/MCP/Tools/AGENTS.md`); every POST after `initialize` must send it.

```bash
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "list-components-in-actor",
      "arguments": {
        "blueprintPath": "/Game/Blueprints/BP_PlayerCharacter"
      }
    },
    "id": 1
  }'
```

### Add a Static Mesh Component
```bash
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "add-component-class-to-actor",
      "arguments": {
        "componentClass": "/Script/Engine.StaticMeshComponent",
        "componentName": "WeaponMesh",
        "parentComponent": "Mesh",
        "attachSocketName": "WeaponSocket"
      }
    },
    "id": 1
  }'
```

### Add a Camera with Spring Arm
```bash
# First, add a spring arm component
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
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
        }
      }
    },
    "id": 1
  }'

# Then, add a camera to the spring arm
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "add-component-class-to-actor",
      "arguments": {
        "componentClass": "/Script/Engine.CameraComponent",
        "componentName": "FollowCamera",
        "parentComponent": "CameraBoom"
      }
    },
    "id": 2
  }'
```

### Filter Components by Type
```bash
# List only scene components
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "list-components-in-actor",
      "arguments": {
        "componentTypeFilter": "scene"
      }
    },
    "id": 1
  }'
```

### Delete a Component
```bash
# Delete a single component (reparent children)
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "delete-component-in-actor",
      "arguments": {
        "componentName": "WeaponMesh",
        "deleteChildren": false
      }
    },
    "id": 1
  }'

# Delete a component and all its children
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "delete-component-in-actor",
      "arguments": {
        "componentName": "CameraBoom",
        "deleteChildren": true
      }
    },
    "id": 2
  }'
```

## Component Hierarchy Output Format

The `list-components-in-actor` tool returns a hierarchical structure:

```json
{
  "components": [
    {
      "name": "DefaultSceneRoot",
      "className": "USceneComponent",
      "classPath": "/Script/Engine.SceneComponent",
      "nodeGuid": "12345678-1234-1234-1234-123456789012",
      "isSceneComponent": true,
      "isRootComponent": true,
      "parentComponent": null,
      "attachSocketName": "",
      "children": [
        {
          "name": "Mesh",
          "className": "USkeletalMeshComponent",
          "classPath": "/Script/Engine.SkeletalMeshComponent",
          "nodeGuid": "87654321-4321-4321-4321-210987654321",
          "isSceneComponent": true,
          "isRootComponent": false,
          "parentComponent": "DefaultSceneRoot",
          "attachSocketName": "",
          "children": [],
          "transform": {
            "location": { "x": 0, "y": 0, "z": -90 },
            "rotation": { "pitch": 0, "yaw": -90, "roll": 0 },
            "scale": { "x": 1, "y": 1, "z": 1 }
          },
          "isInherited": false
        }
      ],
      "transform": {
        "location": { "x": 0, "y": 0, "z": 0 },
        "rotation": { "pitch": 0, "yaw": 0, "roll": 0 },
        "scale": { "x": 1, "y": 1, "z": 1 }
      },
      "isInherited": false
    }
  ],
  "totalCount": 2,
  "rootComponent": "DefaultSceneRoot",
  "blueprintName": "BP_PlayerCharacter",
  "blueprintPath": "/Game/Blueprints/BP_PlayerCharacter"
}
```

## Workflow Integration

These component tools work seamlessly with other Blueprint MCP tools:

1. **Component Discovery Workflow**:
   - Use `search-blueprint-classes` with `classType: "actorComponent"` to find component classes
   - Use `add-component-class-to-actor` to add the component
   - Use `list-components-in-actor` to verify the hierarchy

2. **Component Reference Workflow**:
   - After adding a component, use `create-variable` to create a component reference variable
   - Use `create-get-member-variable-node` to access the component in graphs
   - Use `add-bp-node-to-active-graph` to add component-specific function calls

3. **Complex Hierarchy Building**:
   - Add parent components first (e.g., SpringArmComponent)
   - Then add child components with proper parent references
   - Use transforms to position components relative to parents

## Best Practices

1. **Component Naming**: Use descriptive names that indicate the component's purpose
2. **Hierarchy Planning**: Plan your component hierarchy before adding components
3. **Socket Usage**: Define sockets on mesh components for predictable attachment points
4. **Transform Application**: Apply transforms after attachment for proper relative positioning
5. **Type Checking**: Always verify component classes exist before attempting to add them

## Component Deletion Behavior

### Child Component Handling
When deleting a component that has children:
- **`deleteChildren: false`** (default): Children are reparented to the deleted component's parent
  - If the deleted component was a root component, children become new root components
  - Preserves the component hierarchy as much as possible
- **`deleteChildren: true`**: All child components are recursively deleted
  - Deletes from deepest to shallowest to maintain integrity
  - Use with caution as this can remove large portions of the hierarchy

### Deletion Restrictions
- **Inherited Components**: Cannot delete components from parent Blueprints
- **DefaultSceneRoot**: Can only be deleted if it has no children
- **Non-existent Components**: Returns error if component name doesn't match any in the Blueprint

## Error Handling

Common errors and their meanings:

### General Errors
- `NOT_ACTOR_BLUEPRINT`: The Blueprint doesn't derive from Actor
- `NO_COMPONENTS`: Blueprint has no components (for list/delete operations)

### Add Component Errors
- `CLASS_NOT_FOUND`: The specified component class doesn't exist
- `PARENT_NOT_FOUND`: The specified parent component doesn't exist in the Blueprint
- `CIRCULAR_ATTACHMENT`: Attempting to create a circular attachment hierarchy
- `NOT_COMPONENT`: The specified class isn't derived from UActorComponent
- `NAME_EXISTS`: Component with the specified name already exists

### Delete Component Errors
- `COMPONENT_NOT_FOUND`: The specified component doesn't exist in the Blueprint
- `INHERITED_COMPONENT`: Cannot delete components inherited from parent Blueprint
- `DEFAULT_ROOT_WITH_CHILDREN`: Cannot delete DefaultSceneRoot when it has child components
- `INHERITED_NODE`: Attempting to delete an inherited component

## Implementation Notes

- Uses the Simple Construction Script (SCS) system for component management
- Components are added as SCS nodes which generate instance components at runtime
- All modifications trigger Blueprint compilation to ensure validity
- Component GUIDs are returned for future reference and operations
- Supports both native and Blueprint component classes