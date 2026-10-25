# Content Browser Tools

This directory contains MCP tools for interacting with Unreal Engine's Content Browser. These tools enable navigation, asset discovery, and Blueprint opening through the content browser interface.

## Available Tools

### open-content-browser-path
- **Description**: Opens a specified path in the focused content browser
- **Parameters**:
  - `path` (required): Content browser path (e.g., '/Game/Blueprints')
  - `select_item` (optional): Specific item to select
  - `create_if_missing` (optional): Create folder if it doesn't exist
- **Returns**: Navigation status and selected paths
- **Use Case**: Programmatic content browser navigation

### read-content-browser-path
- **Description**: Returns assets and folders at a content browser path
- **Parameters**:
  - `path` (required): Content browser path to read
  - `page` (optional): Page number for pagination
  - `page_size` (optional): Items per page (1-200)
  - `filter_type` (optional): Filter by asset type
  - `filter_name` (optional): Filter by name contains
  - `sync_browser` (optional): Sync content browser to path
- **Returns**: Paginated list of assets and folders
- **Use Case**: Discovering and filtering content

### open-blueprint-asset
- **Description**: Opens a Blueprint asset in the Blueprint Editor
- **Parameters**:
  - `asset_path` (required): Full asset path
  - `bring_to_front` (optional): Bring editor to front
  - `focus_graph` (optional): Specific graph to focus
- **Returns**: Blueprint information and editor state
- **Use Case**: Opening Blueprints for editing

### search-content-browser
- **Description**: Search for assets across the entire content browser by name or type
- **Parameters**:
  - `query` (optional): Search query for asset names (partial match, case-insensitive)
  - `assetType` (optional): Filter by type (All, Blueprint, Material, Texture, StaticMesh, SkeletalMesh, Sound, Animation, ParticleSystem, DataAsset, DataTable)
  - `includeEngineContent` (optional, default: false): Include Engine content in search
  - `includePluginContent` (optional, default: true): Include Plugin content in search
  - `maxResults` (optional, default: 50): Maximum results to return (1-200)
- **Returns**: Ranked search results with relevance scores
- **Use Case**: Finding assets across entire project

### copy-asset
- **Description**: Copy an asset to a new location in the content browser
- **Parameters**:
  - `sourcePath` (required): Source asset path (supports both package and object path formats)
  - `destinationPath` (required): Destination asset path where the copy will be created
  - `overwriteExisting` (optional, default: false): Whether to overwrite if destination exists
- **Returns**: Copy operation result with new asset details
- **Use Case**: Duplicating assets, creating variations, organizing content

### create-folder
- **Description**: Create a new folder in the content browser
- **Parameters**:
  - `folderPath` (required): Path where the folder should be created (e.g., '/Game/NewFolder')
  - `createParents` (optional, default: false): Create missing parent folders if they don't exist
- **Returns**: Creation result with folder path and navigation status
- **Use Case**: Organizing content, creating project structure, preparing locations for assets

### move-folder
- **Description**: Move a folder and all its contents to a new location in the content browser
- **Parameters**:
  - `sourcePath` (required): Source folder path to move (e.g., '/Game/OldFolder')
  - `destinationPath` (required): Destination parent path where the folder will be moved (e.g., '/Game/NewLocation')
  - `showNotification` (optional, default: true): Show a notification after the move operation
- **Returns**: Move operation result with source/destination paths and number of assets moved
- **Use Case**: Reorganizing content structure, moving folders between projects/plugins, consolidating assets

### rename-asset
- **Description**: Rename an asset or move it to a new location
- **Parameters**:
  - `sourcePath` (required): Source asset path (supports both package and object path formats)
  - `newName` (optional): New name for the asset (keeps in same folder)
  - `destinationPath` (optional): Full destination path to move and rename
  - `showNotification` (optional, default: true): Show a notification after the rename operation
- **Returns**: Rename operation result with old/new paths and navigation status
- **Use Case**: Renaming assets, moving assets to different folders, fixing naming conventions
- **Note**: Provide either `newName` OR `destinationPath`, not both

### rename-folder
- **Description**: Rename a folder in the content browser
- **Parameters**:
  - `sourcePath` (required): Source folder path to rename (e.g., '/Game/OldFolderName')
  - `newName` (required): New name for the folder
  - `showNotification` (optional, default: true): Show a notification after the rename operation
- **Returns**: Rename operation result with old/new paths and number of assets renamed
- **Use Case**: Fixing folder naming, reorganizing project structure, standardizing naming conventions

## Content Browser Paths

### Valid Path Roots
- `/Game` - Project content
- `/Engine` - Engine content
- `/EnginePresets` - Engine preset content
- `/Paper2D` - Paper2D plugin content
- `/NodeToCode` - NodeToCode plugin content
- `/Plugins` - Other plugin content

### Path Format
- Must start with `/`
- Use forward slashes
- No `..` traversal allowed
- Case-sensitive

## Asset Types for Filtering

- `"All"` - All asset types
- `"Blueprint"` - Blueprint classes
- `"Material"` - Materials and instances
- `"Texture"` - Texture assets
- `"StaticMesh"` - Static mesh assets
- `"Folder"` - Folders only

## Usage Examples

### Navigating Content Browser
The curl examples assume `$SID` holds a session id from `initialize` (see "Calling the Server with curl" in `Source/Private/MCP/Tools/AGENTS.md`); every POST after `initialize` must send it.

```bash
# Navigate to Blueprints folder
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "open-content-browser-path",
      "arguments": {
        "path": "/Game/Blueprints"
      }
    },
    "id": 1
  }'

# Create and navigate to new folder
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "open-content-browser-path",
      "arguments": {
        "path": "/Game/AI/BehaviorTrees",
        "create_if_missing": true
      }
    },
    "id": 2
  }'
```

### Browsing Assets
```bash
# List all Blueprints in a folder
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "read-content-browser-path",
      "arguments": {
        "path": "/Game/Blueprints",
        "filter_type": "Blueprint"
      }
    },
    "id": 1
  }'

# Search for player-related assets
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "read-content-browser-path",
      "arguments": {
        "path": "/Game",
        "filter_name": "Player",
        "page_size": 20
      }
    },
    "id": 2
  }'
```

### Opening Blueprints
```bash
# Open a Blueprint
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "open-blueprint-asset",
      "arguments": {
        "asset_path": "/Game/Blueprints/BP_PlayerCharacter.BP_PlayerCharacter"
      }
    },
    "id": 1
  }'

# Open and focus specific graph
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "open-blueprint-asset",
      "arguments": {
        "asset_path": "/Game/Blueprints/BP_GameMode.BP_GameMode",
        "focus_graph": "ConstructionScript"
      }
    },
    "id": 2
  }'
```

## Pagination for Large Directories

When dealing with large asset folders (e.g., Megascans):

```bash
# Page 1 of results
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "read-content-browser-path",
      "arguments": {
        "path": "/Game/Megascans/Surfaces",
        "page": 1,
        "page_size": 50
      }
    },
    "id": 1
  }'

# Check response for has_more: true, then get page 2
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "read-content-browser-path",
      "arguments": {
        "path": "/Game/Megascans/Surfaces",
        "page": 2,
        "page_size": 50
      }
    },
    "id": 2
  }'
```

## Complete Workflow Example

### Finding and Opening a Blueprint
```bash
# 1. Navigate to Blueprints folder
open-content-browser-path "/Game/Blueprints"

# 2. List available Blueprints
read-content-browser-path "/Game/Blueprints" filter_type="Blueprint"

# 3. Search for specific Blueprint
read-content-browser-path "/Game/Blueprints" filter_name="Player"

# 4. Open the found Blueprint
open-blueprint-asset "/Game/Blueprints/BP_PlayerCharacter.BP_PlayerCharacter"

# 5. Focus on specific functionality
open-blueprint-asset "/Game/Blueprints/BP_PlayerCharacter.BP_PlayerCharacter" focus_graph="HandleInput"
```

### Global Asset Search
```bash
# Search for all player-related assets
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "search-content-browser",
      "arguments": {
        "query": "Player"
      }
    },
    "id": 1
  }'

# Find all Blueprints in the project
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "search-content-browser",
      "arguments": {
        "assetType": "Blueprint",
        "maxResults": 100
      }
    },
    "id": 2
  }'

# Search for materials including engine content
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "search-content-browser",
      "arguments": {
        "query": "Metal",
        "assetType": "Material",
        "includeEngineContent": true
      }
    },
    "id": 3
  }'
```

### Copying Assets
```bash
# Copy a Blueprint to a new location
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "copy-asset",
      "arguments": {
        "sourcePath": "/Game/Blueprints/BP_Enemy",
        "destinationPath": "/Game/Blueprints/Variants/BP_Enemy_Fast"
      }
    },
    "id": 1
  }'

# Copy with overwrite enabled
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "copy-asset",
      "arguments": {
        "sourcePath": "/Game/Materials/M_Base.M_Base",
        "destinationPath": "/Game/Materials/Backup/M_Base_Backup",
        "overwriteExisting": true
      }
    },
    "id": 2
  }'
```

### Creating Folders
```bash
# Create a single folder
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "create-folder",
      "arguments": {
        "folderPath": "/Game/AI/BehaviorTrees"
      }
    },
    "id": 1
  }'

# Create folder with parent directories
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "create-folder",
      "arguments": {
        "folderPath": "/Game/Characters/Player/Animations/Montages",
        "createParents": true
      }
    },
    "id": 2
  }'

# Create a folder structure for organizing Blueprints
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "create-folder",
      "arguments": {
        "folderPath": "/Game/Blueprints/UI/HUD",
        "createParents": true
      }
    },
    "id": 3
  }'
```

### Moving Folders
```bash
# Move a folder to a new location
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "move-folder",
      "arguments": {
        "sourcePath": "/Game/OldAssets",
        "destinationPath": "/Game/Archive"
      }
    },
    "id": 1
  }'

# Move without showing notification
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "move-folder",
      "arguments": {
        "sourcePath": "/Game/Temp/TestAssets",
        "destinationPath": "/Game/Production",
        "showNotification": false
      }
    },
    "id": 2
  }'

# Reorganize Blueprint folders
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "move-folder",
      "arguments": {
        "sourcePath": "/Game/Blueprints/Misc",
        "destinationPath": "/Game/Blueprints/Characters"
      }
    },
    "id": 3
  }'
```

### Renaming Assets
```bash
# Simple rename (keep in same folder)
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "rename-asset",
      "arguments": {
        "sourcePath": "/Game/Blueprints/BP_OldName",
        "newName": "BP_NewName"
      }
    },
    "id": 1
  }'

# Move and rename asset
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "rename-asset",
      "arguments": {
        "sourcePath": "/Game/Materials/M_Test.M_Test",
        "destinationPath": "/Game/Materials/Production/M_Ground"
      }
    },
    "id": 2
  }'

# Rename without notification
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "rename-asset",
      "arguments": {
        "sourcePath": "/Game/Textures/T_Temp",
        "newName": "T_Concrete_D",
        "showNotification": false
      }
    },
    "id": 3
  }'
```

### Renaming Folders
```bash
# Rename a folder
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "rename-folder",
      "arguments": {
        "sourcePath": "/Game/OldFolderName",
        "newName": "NewFolderName"
      }
    },
    "id": 1
  }'

# Rename Blueprint subfolder
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "rename-folder",
      "arguments": {
        "sourcePath": "/Game/Blueprints/Test",
        "newName": "Characters"
      }
    },
    "id": 2
  }'

# Rename without notification
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "rename-folder",
      "arguments": {
        "sourcePath": "/Game/Materials/TempMaterials",
        "newName": "EnvironmentMaterials",
        "showNotification": false
      }
    },
    "id": 3
  }'
```

## Asset Path Formats

### Package Path Format
- Format: `/Game/Folder/AssetName`
- Used by: Content browser navigation, most editor operations
- Example: `/Game/Blueprints/BP_Player`

### Object Path Format
- Format: `/Game/Folder/AssetName.AssetName`
- Used by: Asset registry, object references
- Example: `/Game/Blueprints/BP_Player.BP_Player`

**Note**: The copy-asset and move-asset tools accept both formats and will handle conversion automatically.

## Implementation Notes

- Content browser operations use `IContentBrowserSingleton`
- Asset filtering uses `FARFilter` for efficient queries
- Pagination prevents timeouts with large directories
- Asset paths must include the asset name twice (UE convention) for object paths
- All operations require Game Thread execution
- Copy operations use `UEditorAssetSubsystem::DuplicateAsset`
- Rename operations use `UEditorAssetSubsystem::RenameAsset`
- Folder rename is implemented by moving all assets to a new folder path
- Destination folders are created automatically if they don't exist
- All asset references are automatically updated when renaming/moving