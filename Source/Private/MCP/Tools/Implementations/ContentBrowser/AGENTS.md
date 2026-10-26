# Content Browser Tools

This directory contains the MCP tools in the `ContentBrowserTools` toolset: navigation, asset discovery, opening
Blueprints, and folder and asset operations. In dynamic tool discovery, enabling 'Content Browser' gives all ten
tools below, `move-asset` included.

## Status: frozen, bug fixes only (dec-5)

Every tool here (`ContentBrowserTools`) is frozen. Decision record [0007](../../../../../../.agents/decisions/0007-crud-tool-families-fix-then-freeze.md): owner decision card 0140, applied
provisionally as the review's option 2, with card 0089 applied provisionally as option (b).

- **Accepted:** bug fixes, meaning a change that fixes a crash or hang (a modal dialog in an unattended call
  counts), data loss, Blueprint or project corruption, or a result that misreports what happened (success for an
  edit that did not happen, wrong counts, duplicates, a wrong type in a read). Also accepted, since they add no
  capability: explicit targeting arguments on existing tools (review next-6), security hardening, building on the
  supported engines, tests, docs that match the code, and removing dead code.
- **Not accepted:** new tools in this family, and new arguments, modes or result fields that
  add capability. On UE 5.8 that work belongs to Epic's EditorToolset (`AssetTools.find_assets`, `list_folders`, `create_folder`, `move`, `duplicate`, and
  `EditorAppToolset`); on 5.6 and 5.7 it waits for the
  owner.
- **Not frozen:** comprehension, export, translation, batch and tag tools: `BlueprintDiscoveryTools` (except the
  tools in `N2CMcpEpicOverlap::Tools`), `BlueprintTagTools`, `TranslationTools`, and also `FileSystemTools` and
  `PythonTools`.
- **Fix cards still owed here:** 0182 (asset-writing tools restricted to /Game and project plugin content), 0281 (folders through
  `UEditorAssetSubsystem::RenameDirectory`), 0284 (move-, rename- and copy-asset path handling), 0285
  (search-content-browser and read-content-browser-path results), 0286 (open-blueprint-asset fails on unknown
  graphs).

The whole policy, and how the frozen set is read from the code, is "Frozen tool families" in
`Source/Private/MCP/Tools/AGENTS.md`.

## Available Tools

### open-content-browser-path
- **Description**: Opens a path in the primary content browser
- **Parameters**:
  - `path` (required): Content browser folder (e.g., '/Game/Blueprints'); see "Content Browser Paths"
  - `select_item` (optional): Asset to select after navigating, given as `AssetName.AssetName` (object form). A bare
    `AssetName` is not found (`FN2CMcpContentBrowserUtils::SelectAssetAtPath` looks it up as an object path), and the
    call still succeeds with `selected_item` empty
  - `create_if_missing` (optional, default false): Create the folder if it does not exist
- **Returns**: `success`, `navigated_path` (the normalized path), `selected_item`, `created_folder`,
  `current_selected_paths` (the browser's selected folders, as `/All/...` virtual paths)
- **Use Case**: Programmatic content browser navigation

### read-content-browser-path
- **Description**: Lists the assets of every type and the subfolders directly inside a folder (not recursive)
- **Parameters**:
  - `path` (required): Content browser folder to read
  - `page` (optional, default 1): Page number; a page past the end is an error (`Page N is out of range`)
  - `page_size` (optional, default 50): Items per page, 1-200; anything else fails with `Invalid arguments`
  - `filter_type` (optional, default `All`): `All`, `Blueprint`, `Material`, `Texture`, `StaticMesh` or `Folder`.
    Spell it exactly: `All` and `Folder` are compared case-sensitively when choosing whether folders are listed, so
    `all` returns the assets without the folders and `folder` returns nothing. Any other value returns nothing.
    `Blueprint`, `Material`, `Texture` and `StaticMesh` keep assets whose class name contains that word
    (`Material` leaves out `MaterialFunction`)
  - `filter_name` (optional): Keep items whose name contains this text (case-insensitive)
  - `sync_browser` (optional, default true): Sync the primary content browser to the path
- **Returns**: `items` (each with `path`, `name`, `is_folder`, `type`, `class`, and `native_class` for assets),
  `total_count`, `page`, `page_size`, `has_more`
- **Use Case**: Discovering and filtering content

### open-blueprint-asset
- **Description**: Opens a Blueprint asset in the Blueprint Editor
- **Parameters**:
  - `asset_path` (required): Object path (`/Game/Folder/BP_Name.BP_Name`) or package path (`/Game/Folder/BP_Name`).
    `FN2CMcpOpenBlueprintAssetTool::ValidateBlueprintAssetPath` accepts any mounted root (`/Game/`, `/Engine/`, a
    plugin's `/<PluginName>/`), as the other read tools do
  - `bring_to_front` (optional, default true): Focus the editor window
  - `focus_graph` (optional): Name of an event, function, macro or delegate graph to focus, matched
    case-insensitively (`FN2CMcpOpenBlueprintAssetTool::FocusOnGraph`). An Actor's construction script is the function
    graph `UserConstructionScript`. An unknown name is not an error: the Blueprint opens and `focused_graph` is empty
- **Returns**: `success`, `asset_path`, `editor_id`, `focused_graph`, `asset_type`, `blueprint_name`, `parent_class`,
  `graph_counts` (`event_graphs`, `function_graphs`, `macro_graphs`, `delegate_graphs`). A non-Blueprint asset fails
  with `Asset is not a blueprint`
- **Use Case**: Opening Blueprints for editing

### search-content-browser
- **Description**: Searches assets under `/Game`, plus `/Engine` when `includeEngineContent` is true, by name or type
- **Parameters**:
  - `query` (optional): Matched against asset names and package paths (partial, case-insensitive); empty lists all
    assets of the type
  - `assetType` (optional, default `All`): `All`, `Blueprint`, `Material`, `Texture`, `StaticMesh`, `SkeletalMesh`,
    `Sound`, `Animation`, `ParticleSystem`, `DataAsset`, `DataTable`, compared case-insensitively. Each maps to exact
    asset classes in an `FARFilter` (`FN2CMcpSearchContentBrowserTool::SearchAssets`), so `Blueprint` does not
    include Widget or Animation Blueprints; any other value applies no type filter
  - `includeEngineContent` (optional, default false): Also search `/Engine`
  - `includePluginContent` (optional, default true): Has no effect. `SearchAssets` only puts `/Game` (and `/Engine`)
    in the filter's package paths, so plugin content mounted at `/<PluginName>` is never returned, whatever this is
    set to. List plugin content with `read-content-browser-path` on `/<PluginName>`
  - `maxResults` (optional, default 50): Clamped to 1-200
- **Returns**: `searchInfo` (`query`, `assetType`, `includeEngineContent`, `includePluginContent`, `totalFound`,
  `resultsReturned`), `assets` sorted by `relevanceScore` (each with `name`, `path`, `type`, `class`, `relevanceScore`
  and `metadata`: `folder`, `objectPath`, and for Blueprints `parentClass` and `blueprintType` when the registry has
  them), `tips`
- **Use Case**: Finding assets across the project

### copy-asset
- **Description**: Copies an asset to a new asset path
- **Parameters**:
  - `sourcePath` (required): Package path or object path under any mounted root (copying only reads it)
  - `destinationPath` (required): Package path of the new asset including its name (e.g.,
    `/Game/Folder/BP_Copy`); the write policy (`FN2CMcpContentBrowserUtils::IsWritableContentPath`, see "Where tools may write") applies, its name
    may not contain spaces or dots, and missing folders are created
  - `overwriteExisting` (optional, default false): With false, an existing destination is reported before copying
    (`Destination asset already exists`). With true that check is skipped, but `UEditorAssetSubsystem::DuplicateAsset`
    never replaces an existing asset, so the copy fails (`Failed to copy asset ...`) and the existing asset is left
    as it was. There is no way to overwrite through this tool
- **Returns**: `message`, `sourcePath`, `destinationPath`, `assetName`, `assetClass`, `tips`
- **Use Case**: Duplicating assets, creating variations

### create-folder
- **Description**: Creates a folder in the content browser and navigates to it
- **Parameters**:
  - `folderPath` (required): Folder to create, under `/Game/` or a project plugin's `/<PluginName>/` (the write
    policy); no `..`, `~`, `*`, `?` or dots in the name, and not a Windows device name (CON, PRN, AUX, NUL, COM1-9,
    LPT1-9: `FN2CMcpContentBrowserUtils::IsReservedDeviceName`, shared with `rename-folder`)
  - `createParents` (optional, default false): Create missing parent folders; without it a missing parent is an
    error
- **Returns**: `message`, `folderPath`, `navigated`, `tips`. An existing folder is an error
- **Use Case**: Organizing content, preparing locations for assets

### move-folder
- **Description**: Moves the assets of a folder (recursively) into `<destinationPath>/<folder name>`
- **Parameters**:
  - `sourcePath` (required): Folder to move, under the write policy's roots (not a root itself: `/Game`,
    `/<PluginName>`)
  - `destinationPath` (required): Parent folder under the write policy's roots; created if missing. `<destinationPath>/<folder name>`
    must not exist yet
  - `showNotification` (optional, default true): Show an editor notification
- **Returns**: `message`, `sourcePath`, `destinationPath` (the new folder path), `assetsMoved`, `navigated`, `tips`
- **Behaviour** (`FN2CMcpMoveFolderTool::MoveFolderContents`): each asset is renamed into the new folder; the source
  folder is left behind, empty, and a folder with no assets moves nothing (the call still succeeds, with
  `assetsMoved` 0, and no new folder appears)
- **Use Case**: Reorganizing content structure

### move-asset
- **Description**: Moves or renames an asset (`UEditorAssetSubsystem::RenameAsset`, which updates references)
- **Parameters**:
  - `sourcePath` (required): Package path `/Game/Folder/Asset` (as `read-content-browser-path` returns it) or object
    path `/Game/Folder/Asset.Asset`. With an object path, also pass `newName`: without it
    `FN2CMcpMoveAssetTool::Execute` takes the name from the path including `.Asset`, and the move fails
  - `destinationPath` (required): Destination folder (e.g., '/Game/Blueprints/Characters'); created if missing.
    The write policy applies to it and to `sourcePath`, since a move rewrites both packages
  - `newName` (optional): New asset name (default: the source's name)
  - `showNotification` (optional, default true): Show an editor notification
- **Returns**: `success`, `oldPath`, `newPath` (package paths), `objectPath`, `operation` (`move`, or `rename` when
  the folder is unchanged), `leftRedirector` (always false), `assetInfo`, `nextSteps`. An existing asset at the
  destination is an error
- **Use Case**: Reorganizing project assets, renaming Blueprints and other content

### rename-asset
- **Description**: Renames an asset in place, or moves it to a full new asset path
- **Parameters**:
  - `sourcePath` (required): Package or object path under the write policy's roots (`/Engine/` assets are refused)
  - `newName` (optional): New name in the same folder
  - `destinationPath` (optional): Full new package path including the name, under the write policy's roots; missing
    folders are created
  - `showNotification` (optional, default true): Show an editor notification
- **Returns**: `message`, `oldPath`, `newPath`, `oldName`, `newName`, `assetSelected`, `wasMoved`, `tips`
- **Note**: Give exactly one of `newName` and `destinationPath`; both, or neither, is an error
- **Use Case**: Renaming assets, fixing naming conventions

### rename-folder
- **Description**: Renames a folder by moving its assets (recursively) to a sibling folder
- **Parameters**:
  - `sourcePath` (required): Folder under the write policy's roots (not a root itself: `/Game`, `/<PluginName>`)
  - `newName` (required): New folder name (no slashes, dots, spaces, `~`, `*`, `?`, and not a Windows device name:
    exactly CON, PRN, AUX, NUL, COM1-9 or LPT1-9, so `Components` or `Common` are fine)
  - `showNotification` (optional, default true): Show an editor notification
- **Returns**: `message`, `oldPath`, `newPath`, `oldName`, `newName`, `assetsRenamed`, `navigated`, `tips`
- **Behaviour** (`FN2CMcpRenameFolderTool::MoveFolderContents`): as for `move-folder`, the old folder is left behind,
  empty, and a folder with no assets is not renamed although the call succeeds
- **Use Case**: Fixing folder naming

## Content Browser Paths

### Valid Path Roots
`open-content-browser-path` and `read-content-browser-path` accept any mounted content root:
`FN2CMcpContentBrowserUtils::ValidateContentPath` normalizes the path, and `FPackageName::IsValidLongPackageName`
refuses roots that are not mounted (the error lists the mounted ones). That covers:
- `/Game` - Project content
- `/Engine` - Engine content
- `/<PluginName>` - Content of an enabled plugin with content, e.g. `/NodeToCode`, `/Paper2D`, `/Niagara`

There is no `/Plugins` root: `/Plugins` is refused (plugin content mounts at `/<PluginName>`; the `/All/Plugins/...`
paths in `current_selected_paths` are the browser's virtual view, not package paths).

`open-blueprint-asset` and the source of `copy-asset` accept any mounted root too.

### Where tools may write (card 0182)
Every tool that creates, saves, copies, moves or renames content checks
`FN2CMcpContentBrowserUtils::IsWritableContentPath` on each destination, and on the source of a move or rename
(`create-blueprint-class`, `save-blueprint`, `copy-asset`, `move-asset`, `rename-asset`, `create-folder`, `move-folder`,
`rename-folder`, `open-content-browser-path` with `create_if_missing`; `CreateContentFolder` checks it too). It
resolves the mount with `FPackageName::GetPackageMountPoint` and allows:
- `/Game`
- the content mount of a project plugin (`IPluginManager::FindPlugin(<mount>)`, `GetLoadedFrom() ==
  EPluginLoadedFrom::Project`, `CanContainContent()`), except `/NodeToCode`, whose content ships with the plugin

Everything else (`/Engine`, engine and Fab plugins, `/NodeToCode`, `/Script`, `/Temp`, `/Memory`, unmounted roots)
is refused with a message naming the per-user setting **Allow MCP Writes Outside Project**
(`UN2CUserSettings::AreMcpWritesOutsideProjectAllowed`, off by default, read only from the user's own config layers,
needs a restart). With it on, any mounted root `FPackageName::IsValidLongPackageName(Path, false)` accepts is allowed,
so `/Script`, `/Temp` and `/Memory` stay refused. Mount roots themselves (`IsContentMountRoot`) are never moved or
renamed. Python (`run-python`, `UN2CStructUtilsLibrary`) is not covered: with Python execution on, an agent can write
anywhere the editor can. Spec: `NodeToCode.MCP.Content` ("the write path policy", "a project plugin's content mount").

### Path Format
- Package paths start with a root; `NormalizeContentPath` turns backslashes into slashes, drops a trailing slash and
  adds a missing leading `/` for the navigation and listing tools
- `..` (and any other dot) is refused by `IsValidLongPackageName` for folder paths
- Not case-sensitive: package names are matched case-insensitively (`/game/n2cdocprobe` opens `/Game/N2CDocProbe`);
  `read-content-browser-path` echoes the spelling you sent in each item's `path`

## Asset Types for Filtering

`read-content-browser-path` (`filter_type`, exact spelling):
- `"All"` - Assets and folders
- `"Blueprint"`, `"Material"`, `"Texture"`, `"StaticMesh"` - Assets whose class name contains the word
- `"Folder"` - Folders only

`search-content-browser` (`assetType`, any case) has its own list; see the tool above.

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

# Open and focus the construction script (an Actor's function graph UserConstructionScript)
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
        "focus_graph": "UserConstructionScript"
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
open-blueprint-asset "/Game/Blueprints/BP_PlayerCharacter.BP_PlayerCharacter" focus_graph="HandleInput"   # a function graph
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

# A destination that already exists is an error, with or without "overwriteExisting": true;
# copy to a new name, or rename or delete the existing asset first
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

### Moving Assets
```bash
# Move an asset to a different folder
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "move-asset",
      "arguments": {
        "sourcePath": "/Game/Characters/BP_Player",
        "destinationPath": "/Game/Blueprints/Characters"
      }
    },
    "id": 1
  }'

# Rename an asset (same directory)
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "move-asset",
      "arguments": {
        "sourcePath": "/Game/Blueprints/BP_OldName",
        "destinationPath": "/Game/Blueprints",
        "newName": "BP_NewName"
      }
    },
    "id": 2
  }'

# Move and rename in one operation (an object path as source needs newName)
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "move-asset",
      "arguments": {
        "sourcePath": "/Game/Test/BP_TestActor.BP_TestActor",
        "destinationPath": "/Game/Blueprints/Actors",
        "newName": "BP_MyActor",
        "showNotification": false
      }
    },
    "id": 3
  }'
```

### Asset Organization Workflow
```bash
# 1. Browse the current asset structure
read-content-browser-path "/Game"

# 2. Create an organized folder structure
create-folder "/Game/Blueprints/Characters" createParents=true
create-folder "/Game/Blueprints/Widgets"
create-folder "/Game/Blueprints/GameModes"

# 3. Move assets to organized locations
move-asset "/Game/BP_Player" "/Game/Blueprints/Characters"
move-asset "/Game/BP_Enemy" "/Game/Blueprints/Characters"
move-asset "/Game/WBP_MainMenu" "/Game/Blueprints/Widgets"
move-asset "/Game/GM_Main" "/Game/Blueprints/GameModes"

# 4. Rename assets for consistency
move-asset "/Game/Blueprints/Characters/PlayerCharacter" "/Game/Blueprints/Characters" "BP_PlayerCharacter"
move-asset "/Game/Blueprints/GameModes/MainMode" "/Game/Blueprints/GameModes" "GM_MainMode"
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

**Note**: `copy-asset`, `rename-asset` and `open-blueprint-asset` accept either format for the source. `move-asset`
accepts either, but needs `newName` with an object path. `open-content-browser-path`'s `select_item` needs the
object form `AssetName.AssetName`.

## Implementation Notes

- Path checks: `FN2CMcpContentBrowserUtils::ValidateContentPath` (reading: any mounted root) and
  `FN2CMcpContentBrowserUtils::IsWritableContentPath` (writing: `/Game` and project plugins), called from each asset
  tool's own `Validate*` functions
- Listing: `FN2CMcpContentBrowserUtils::EnumerateItemsAtPathAsJson` (folders from disk, assets from the asset
  registry, immediate children only); pagination from `CalculatePagination`
- Search: one `FARFilter` over `/Game` (and `/Engine`) with exact class paths, then name/path matching and scoring
- Copy uses `UEditorAssetSubsystem::DuplicateAsset`, which never replaces an existing asset
- Move and rename use `UEditorAssetSubsystem::RenameAsset` (`move-asset` falls back to `IAssetTools::RenameAssets`),
  which updates references to the asset
- Folder move and rename move each asset one by one; the old folder stays, empty, and an empty folder is not moved
- All operations run on the game thread (`ExecuteOnGameThread`)
