# Blueprint Class Tools

This directory contains MCP tools for discovering and managing Blueprint classes. These tools provide functionality for searching available parent classes and creating new Blueprint classes.

## Available Tools

### search-blueprint-classes
- **Description**: Searches for available parent classes for Blueprint creation, similar to the 'Pick Parent Class' dialog
- **Parameters**:
  - `searchTerm` (required): Text query to search for class names
  - `classType` (optional): Filter by base class type (all/actor/actorComponent/object/userWidget)
  - `includeEngineClasses` (optional): Include engine-provided classes in results
  - `includePluginClasses` (optional): Include plugin-provided classes in results
  - `includeDeprecated` (optional): Include deprecated classes in results
  - `includeAbstract` (optional): Include abstract classes in results
  - `maxResults` (optional): Maximum number of results to return (1-200)
- **Returns**: List of matching classes with metadata including path, hierarchy, and blueprint compatibility
- **Use Case**: Finding appropriate parent classes before creating new Blueprints

### create-blueprint-class
- **Description**: Creates a new Blueprint class with the specified parent class and settings
- **Parameters**:
  - `blueprintName` (required): Name for the new Blueprint (BP_ prefix added automatically)
  - `parentClassPath` (required): Path to the parent class (e.g., '/Script/Engine.Actor')
  - `assetPath` (required): Content path where the Blueprint will be created
  - `openInEditor` (optional): Open the Blueprint in the editor after creation (default: true)
  - `openInFullEditor` (optional): Open in full Blueprint editor vs. simplified (default: true)
  - `description` (optional): Description/tooltip for the Blueprint
  - `generateConstructionScript` (optional): Generate default construction script for Actors (default: true)
  - `blueprintType` (optional): Blueprint type (auto/normal/const/interface, default: auto)
- **Returns**: Created Blueprint details including path, class info, and suggested next steps
- **Use Case**: Creating new Blueprint classes programmatically

## Class Information Structure

The tool returns comprehensive metadata for each class:

```json
{
  "classes": [
    {
      "className": "AActor",
      "classPath": "/Script/Engine.Actor",
      "displayName": "Actor",
      "category": "Common|Actor",
      "description": "Base class for all actors that can be placed in a level",
      "parentClass": "UObject",
      "isAbstract": false,
      "isDeprecated": false,
      "isBlueprintable": true,
      "isNative": true,
      "module": "Engine",
      "icon": "Actor",
      "commonClass": true
    }
  ],
  "totalFound": 150,
  "hasMore": true
}
```

## Common Usage Patterns

### Complete Workflow: Search and Create

The curl examples assume `$SID` holds a session id from `initialize` (see "Calling the Server with curl" in `Source/Private/MCP/Tools/AGENTS.md`); every POST after `initialize` must send it.

```bash
# Step 1: Search for a parent class
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "search-blueprint-classes",
      "arguments": {
        "searchTerm": "Character",
        "classType": "actor",
        "maxResults": 5
      }
    },
    "id": 1
  }'

# Step 2: Create a Blueprint using the found class
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "create-blueprint-class",
      "arguments": {
        "blueprintName": "MyPlayerCharacter",
        "parentClassPath": "/Script/Engine.Character",
        "assetPath": "/Game/Blueprints/Characters",
        "description": "Player character with custom movement abilities"
      }
    },
    "id": 2
  }'
```

### Creating Different Blueprint Types

#### Actor Blueprint
```bash
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "create-blueprint-class",
      "arguments": {
        "blueprintName": "InteractableObject",
        "parentClassPath": "/Script/Engine.Actor",
        "assetPath": "/Game/Blueprints/Gameplay"
      }
    },
    "id": 1
  }'
```

#### Component Blueprint
```bash
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "create-blueprint-class",
      "arguments": {
        "blueprintName": "HealthComponent",
        "parentClassPath": "/Script/Engine.ActorComponent",
        "assetPath": "/Game/Blueprints/Components",
        "generateConstructionScript": false
      }
    },
    "id": 1
  }'
```

#### Widget Blueprint
```bash
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "create-blueprint-class",
      "arguments": {
        "blueprintName": "MainMenuWidget",
        "parentClassPath": "/Script/UMG.UserWidget",
        "assetPath": "/Game/UI/Menus",
        "openInFullEditor": true
      }
    },
    "id": 1
  }'
```

### Finding Actor-Based Classes
```bash
# Search for character-related actor classes
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "search-blueprint-classes",
      "arguments": {
        "searchTerm": "Character",
        "classType": "actor",
        "maxResults": 10
      }
    },
    "id": 1
  }'
```

### Finding Component Classes
```bash
# Search for mesh component classes
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "search-blueprint-classes",
      "arguments": {
        "searchTerm": "Mesh",
        "classType": "actorComponent",
        "includeAbstract": false
      }
    },
    "id": 1
  }'
```

### Finding Widget Classes
```bash
# Search for UI widget base classes
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "search-blueprint-classes",
      "arguments": {
        "searchTerm": "Widget",
        "classType": "userWidget",
        "includeEngineClasses": true
      }
    },
    "id": 1
  }'
```

## Class Type Filtering

The `classType` parameter supports these categories:
- **all**: No filtering, returns all matching classes
- **actor**: Classes derived from AActor (placeable in levels)
- **actorComponent**: Classes derived from UActorComponent (attachable to actors)
- **object**: General UObject classes (data assets, subsystems, etc.)
- **userWidget**: Classes derived from UUserWidget (UI elements)

## Common Parent Classes

The tool prioritizes commonly-used parent classes in search results:

### Actor Classes
- `AActor` - Base actor class
- `APawn` - Controllable actors
- `ACharacter` - Humanoid characters with movement
- `AGameModeBase` - Game rules and flow
- `AGameStateBase` - Replicated game state
- `APlayerController` - Player input handling
- `APlayerState` - Player-specific state
- `AHUD` - Heads-up display

### Component Classes
- `UActorComponent` - Non-spatial components
- `USceneComponent` - Spatial components
- `UPrimitiveComponent` - Renderable/collidable components
- `UStaticMeshComponent` - Static mesh rendering
- `USkeletalMeshComponent` - Skeletal mesh rendering

### Object Classes
- `UObject` - Base object class
- `UDataAsset` - Data-only assets
- `USaveGame` - Save game data

### Widget Classes
- `UUserWidget` - Base widget class
- Common UI elements (Button, TextBlock, Image)

## Implementation Notes

- Uses `FKismetEditorUtilities::CanCreateBlueprintOfClass` for blueprint compatibility validation
- Searches both loaded native classes and unloaded Blueprint classes via Asset Registry
- Implements relevance scoring for search results based on name matching
- Common classes are marked with `commonClass: true` for UI prioritization
- Filters respect Unreal's class flags (Blueprintable, Deprecated, Abstract)

## Integration with Other Tools

The class tools work seamlessly with other Blueprint MCP tools:

1. **Search → Create → Edit Workflow**:
   - Use `search-blueprint-classes` to find a suitable parent
   - Use `create-blueprint-class` to create the Blueprint
   - Use `create-blueprint-function` to add functions
   - Use `create-variable` to add member variables
   - Use `add-bp-node-to-active-graph` to build logic

2. **The create-blueprint-class tool returns helpful next steps** that guide users to related tools for further Blueprint development.

## Future Tools

This directory will be expanded with:
- **reparent-blueprint**: Change the parent class of an existing Blueprint
- **validate-class-hierarchy**: Check Blueprint class hierarchy for issues
- **duplicate-blueprint-class**: Create a copy of an existing Blueprint with a new name