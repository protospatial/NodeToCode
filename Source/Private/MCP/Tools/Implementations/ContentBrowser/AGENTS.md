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
- **Fix cards still owed here:** 0182 (asset-writing tools restricted to /Game and project plugin content), 0285 (search-content-browser and read-content-browser-path results), 0286 (open-blueprint-asset fails on unknown
  graphs).

The whole policy, and how the frozen set is read from the code, is "Frozen tool families" in
`Source/Private/MCP/Tools/AGENTS.md`.

## Available Tools

### open-content-browser-path
- **Description**: Opens a path in the primary content browser
- **Parameters**:
  - `path` (required): Content browser folder (e.g., '/Game/Blueprints'); see "Content Browser Paths"
  - `select_item` (optional): Asset in that folder to select after navigating, as `AssetName` or
    `AssetName.AssetName` (`FN2CMcpContentBrowserUtils::SelectAssetAtPath` finds it in the asset registry either way).
    An asset that is not there is not an error: the call succeeds with `selected_item` empty
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
    `/Game/Folder/BP_Copy`); the write policy (`FN2CMcpContentBrowserUtils::IsWritableContentPath`, see "Where tools may write") applies, and its name
    follows "Asset names". An existing asset there is an error (`Destination asset already exists`): the copy never
    replaces one. Missing folders are created right before the copy and removed again if it fails
  - There is no `overwriteExisting` (removed by card 0284: it never overwrote). An argument the schema does not list
    is ignored
- **Returns**: `message`, `sourcePath`, `destinationPath` (package paths, the copy read back from the asset
  registry), `assetName`, `assetClass`, `tips`. A failed copy gives the engine's reason
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
- **Description**: Moves a folder, its assets and its subfolders (empty ones too) to `<destinationPath>/<folder name>`
- **Parameters**:
  - `sourcePath` (required): Folder to move, under the write policy's roots (not a root itself: `/Game`,
    `/<PluginName>`)
  - `destinationPath` (required): Parent folder under the write policy's roots; created right before the move if
    missing, and removed again if nothing moves. Not the source folder or one of its subfolders, compared
    case-insensitively (`Cannot move ... into itself or one of its own subfolders`). `<destinationPath>/<folder name>`
    must not exist yet
  - `showNotification` (optional, default true): Show an editor notification
- **Returns**: `message`, `sourcePath`, `destinationPath` (the new folder path), `assetsMoved` (the number now at the
  destination), `navigated`, and the folder-move fields (see "Folder moves")
- **Use Case**: Reorganizing content structure

### move-asset
- **Description**: Moves or renames an asset (`UEditorAssetSubsystem::RenameAsset`, which updates references)
- **Parameters**:
  - `sourcePath` (required): Package path `/Game/Folder/Asset` (as `read-content-browser-path` returns it) or object
    path `/Game/Folder/Asset.Asset` (as `search-content-browser`'s `metadata.objectPath`); the asset is looked up in the
    asset registry (`FN2CMcpContentBrowserUtils::ResolveAsset`) and its name taken from there
  - `destinationPath` (required): Destination folder (e.g., '/Game/Blueprints/Characters'); created right before the
    move if missing, and removed again if the move fails. The write policy applies to it and to `sourcePath`, since a
    move rewrites both packages
  - `newName` (optional): New asset name (default: the asset's name), following "Asset names"
  - `showNotification` (optional, default true): Show an editor notification
- **Returns**: `success`, `oldPath`, `newPath` (package paths, read back from the asset registry), `objectPath`,
  `operation` (`move`, or `rename` when the folder is unchanged), `leftRedirector` (whether the asset registry holds
  a redirector at the old object path after the move: `FN2CMcpContentBrowserUtils::IsRedirectorAt`), `assetInfo`,
  `nextSteps` (names the redirector when one was left, and says paths in C++, config or text files are not updated). An existing asset at the destination is an error, and so is a case-only rename (see "Asset names").
  An asset checked out by someone else in source control is refused. A failed move gives the engine's reason
- **Use Case**: Reorganizing project assets, renaming Blueprints and other content

### rename-asset
- **Description**: Renames an asset in place, or moves it to a full new asset path
- **Parameters**:
  - `sourcePath` (required): Package or object path under the write policy's roots (`/Engine/` assets are refused)
  - `newName` (optional): New name in the same folder, following "Asset names" (`BP_New.Final` is refused)
  - `destinationPath` (optional): Full new package path including the name (`/Game/Folder/BP_New`, or the object form
    `/Game/Folder/BP_New.BP_New`), under the write policy's roots; missing folders are created right before the
    rename and removed again if it fails
  - `showNotification` (optional, default true): Show an editor notification
- **Returns**: `message`, `oldPath`, `newPath` (package paths, read back from the asset registry), `oldName`,
  `newName`, `assetSelected` (whether the content browser selected the renamed asset), `wasMoved`, `tips` (whether a
  redirector was left at the old path, from the asset registry). An existing asset at the destination and a
  case-only rename are errors; a failed rename gives the engine's reason
- **Note**: Give exactly one of `newName` and `destinationPath`; both, or neither, is an error
- **Use Case**: Renaming assets, fixing naming conventions

### rename-folder
- **Description**: Renames a folder: moves it, its assets and its subfolders (empty ones too) to a sibling folder
- **Parameters**:
  - `sourcePath` (required): Folder under the write policy's roots (not a root itself: `/Game`, `/<PluginName>`)
  - `newName` (required): New folder name (no slashes, dots, spaces, `~`, `*`, `?`, and not a Windows device name:
    exactly CON, PRN, AUX, NUL, COM1-9 or LPT1-9, so `Components` or `Common` are fine). A case-only rename
    (`blueprints` to `Blueprints`) is an error: folder names are case-insensitive
  - `showNotification` (optional, default true): Show an editor notification
- **Returns**: `message`, `oldPath`, `newPath`, `oldName`, `newName`, `assetsRenamed` (the number now at the new
  path), `navigated`, and the folder-move fields (see "Folder moves")
- **Use Case**: Fixing folder naming

## Folder moves

`move-folder` and `rename-folder` both call `FN2CMcpContentBrowserUtils::MoveFolder` (card 0281), which is one
`UEditorAssetSubsystem::RenameDirectory` call: the engine checks every asset's destination before it moves any, renames
them all in one `IAssetTools::RenameAssets` batch under a progress dialog, and deletes the source directory once
nothing is left in it. `MoveFolder` adds what the engine does not do: it refuses a destination equal to or inside the
source, recreates the subfolders that held no assets at the destination (the engine moves assets, not folders), and
reads the outcome back from the asset registry. An empty folder moves too: the new folder appears and the old one goes.

Both results carry:
- `movedAssets`: the new package paths of the assets now at the destination
- `failedAssets`: package paths of assets still at the source (empty on success)
- `redirectors`: old object paths where a redirector was left, because a referencing package could not be updated or
  saved (`FN2CMcpContentBrowserUtils::IsRedirectorAt`)
- `sourceRemoved`: whether the old folder is gone. It stays when it still holds redirectors, assets that did not move,
  or files the asset registry does not list
- `tips`: the outcome in words: redirectors left or none, the source removed or what it still holds, and that paths in
  C++, config or text files are not updated

When any asset fails to move the call is an error whose text gives the engine's reason, then how many moved, which did
not (the first five) and the redirectors left. Nothing is rolled back: what moved stays moved, so move the rest with
`move-asset`. When nothing moved, the folders the call created are removed again.

## Asset names

`move-asset`'s and `rename-asset`'s `newName` and the name at the end of `copy-asset`'s and `rename-asset`'s
`destinationPath` are checked as given before any path is built or folder created
(`FN2CMcpContentBrowserUtils::ValidateNewAssetName`): not empty, none of the characters object and package names
refuse (`.`, `/`, `\`, spaces, `:`, `"`, `'`, `,`, `|`, `&`, `!`, `~`, `@`, `#`, brackets and the like), not a Windows
device name, and a valid file name. Package names are case-insensitive, so a rename that changes only the case
(`BP_player` to `BP_Player`) is refused with a `Case-only renames are not supported` error: rename to another name
first, then to the new spelling.

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

# A destination that already exists is an error: copy to a new name, or rename or delete the existing asset first
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

**Note**: `copy-asset`, `rename-asset`, `move-asset` and `open-blueprint-asset` accept either format for the source.
`open-content-browser-path`'s `select_item` accepts `AssetName` or `AssetName.AssetName`.

## Implementation Notes

- Path checks: `FN2CMcpContentBrowserUtils::ValidateContentPath` (reading: any mounted root) and
  `FN2CMcpContentBrowserUtils::IsWritableContentPath` (writing: `/Game` and project plugins), called from each asset
  tool's own `Validate*` functions
- Listing: `FN2CMcpContentBrowserUtils::EnumerateItemsAtPathAsJson` (folders from disk, assets from the asset
  registry, immediate children only); pagination from `CalculatePagination`
- Search: one `FARFilter` over `/Game` (and `/Engine`) with exact class paths, then name/path matching and scoring
- Copy uses `UEditorAssetSubsystem::DuplicateAsset`, which never replaces an existing asset
- Move and rename use `UEditorAssetSubsystem::RenameAsset`, which updates references to the asset and keeps modal
  dialogs away (`GIsRunningUnattendedScript`); there is no `IAssetTools::RenameAssets` fallback (card 0284)
- Copy, move and rename resolve the source in the asset registry (`FN2CMcpContentBrowserUtils::ResolveAsset`), run
  every check before creating anything, create a missing destination folder only right before the change
  (`CreateFolderForMutation`) and remove it if the change fails (`RemoveFolderCreatedForMutation`, never a folder
  holding an asset), report what the registry holds afterwards, and give the engine's logged reason on failure
  (`FN2CMcpEngineErrorCapture`). move-asset asks source control about the package's file
  (`USourceControlHelpers::PackageFilename`)
- Folder move and rename move each asset one by one; the old folder stays, empty, and an empty folder is not moved
- All operations run on the game thread (`ExecuteOnGameThread`)
