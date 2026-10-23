# File System Tools

This directory contains MCP tools for safely reading files and directories within the Unreal Engine project. These tools enforce security boundaries to prevent unauthorized access outside the project directory.

## Available Tools

### read-path
- **Description**: Lists files and folders in a directory within the project
- **Parameters**:
  - `relativePath` (required): Path relative to project root (use `""` for root)
- **Returns**: File and directory listings with counts
- **Security**: Prevents directory traversal attacks
- **Use Case**: Browsing project structure safely

### read-file
- **Description**: Reads the contents of a text file within the project
- **Parameters**:
  - `relativePath` (required): Path to file relative to project root
- **Returns**: File content, size, and metadata
- **Security**: Size limits and binary file restrictions
- **Use Case**: Reading configuration, source files, and documentation

### move-asset
- **Description**: Move or rename an asset to a new location in the content browser
- **Parameters**:
  - `sourcePath` (required): Path to the asset to move. Accepts both formats:
    - Package path: `/Game/Folder/Asset` (as returned by read-content-browser-path)
    - Object path: `/Game/Folder/Asset.Asset` (full UE object path)
  - `destinationPath` (required): Destination directory path (e.g., '/Game/Blueprints/Characters')
  - `newName` (optional): New name for the asset (keeps original name if not provided)
  - `showNotification` (optional, default: true): Show a notification after the move operation
- **Returns**: Move details including:
  - `oldPath`: Clean package path of source
  - `newPath`: Clean package path of destination
  - `objectPath`: Full object path of moved asset
  - `operation`: "move" or "rename"
  - `assetInfo`: Asset metadata
- **Features**: 
  - Automatic path format detection and conversion
  - Reference updating across the project
  - Source control integration
  - Clear error messages with expected formats
  - Support for both move and rename operations
- **Use Case**: Reorganizing project assets, renaming Blueprints and other content

## Security Features

### Path Jail Enforcement
- All paths are restricted to the project directory
- Relative path components (`..` and `.`) are collapsed
- Symlinks and junctions are resolved, and a path that resolves outside the project is refused (so in a dev host whose Plugins/NodeToCode folders are junctions to a checkout, those files are not readable)
- Absolute paths are rejected

### File Restrictions
- Maximum file size: 500KB
- Binary files blocked: `.uasset`, `.umap`
- Text files only
- Read-only operations

## Usage Examples

### Browsing Project Structure
```bash
# List project root
curl -X POST http://localhost:27000/mcp \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "read-path",
      "arguments": {"relativePath": ""}
    },
    "id": 1
  }'

# List Config directory
curl -X POST http://localhost:27000/mcp \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "read-path",
      "arguments": {"relativePath": "Config"}
    },
    "id": 2
  }'

# List plugin directory
curl -X POST http://localhost:27000/mcp \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "read-path",
      "arguments": {"relativePath": "Plugins/NodeToCode/Source"}
    },
    "id": 3
  }'
```

### Reading Files
```bash
# Read project configuration
curl -X POST http://localhost:27000/mcp \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "read-file",
      "arguments": {"relativePath": "Config/DefaultEngine.ini"}
    },
    "id": 1
  }'

# Read plugin descriptor
curl -X POST http://localhost:27000/mcp \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "read-file",
      "arguments": {"relativePath": "Plugins/NodeToCode/NodeToCode.uplugin"}
    },
    "id": 2
  }'
```

### Moving and Renaming Assets
```bash
# Move an asset to a different folder
curl -X POST http://localhost:27000/mcp \
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

# Move and rename in one operation
curl -X POST http://localhost:27000/mcp \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "move-asset",
      "arguments": {
        "sourcePath": "/Game/Test/BP_TestActor",
        "destinationPath": "/Game/Blueprints/Actors",
        "newName": "BP_MyActor",
        "showNotification": false
      }
    },
    "id": 3
  }'
```

## Path Format Guidelines

### Correct Path Usage
- **Project Root**: Use empty string `""`, not `"."` or `"/"`
- **Subdirectories**: Use forward slashes: `"Config"`, `"Source/Classes"`
- **Nested Paths**: `"Plugins/NodeToCode/Content"`

### Incorrect Patterns (Will Fail)
- `"."` or `"./"` for current directory
- `"../OtherProject"` for traversal attempts
- `"/absolute/path"` for absolute paths
- `"Config\Windows"` using backslashes

## Content Type Detection

The `read-file` tool automatically detects content types:

### Programming Languages
- C/C++: `.h`, `.cpp`, `.c`
- Python: `.py`
- JavaScript: `.js`, `.ts`
- C#: `.cs`
- Unreal: `.uc`, `.uci`

### Configuration Files
- INI: `.ini`
- JSON: `.json`
- XML: `.xml`
- YAML: `.yml`, `.yaml`

### Documentation
- Markdown: `.md`
- Text: `.txt`
- Logs: `.log`

## Common Workflows

### Exploring Plugin Structure
```bash
# 1. List plugin root
read-path "Plugins/NodeToCode"

# 2. List source directories
read-path "Plugins/NodeToCode/Source"

# 3. Read specific source files
read-file "Plugins/NodeToCode/Source/Public/NodeToCode.h"
```

### Reading Translation Outputs
```bash
# 1. List available translations
read-path "Saved/NodeToCode/Translations"

# 2. List specific translation
read-path "Saved/NodeToCode/Translations/BP_Player_2024-01-15-10.30.00"

# 3. Read translated code
read-file "Saved/NodeToCode/Translations/BP_Player_2024-01-15-10.30.00/EventGraph.cpp"
```

### Asset Organization Workflow
```bash
# 1. Browse current asset structure
open-content-browser-path "/Game"

# 2. Create organized folder structure
open-content-browser-path "/Game/Blueprints/Characters"
open-content-browser-path "/Game/Blueprints/Widgets"
open-content-browser-path "/Game/Blueprints/GameModes"

# 3. Move assets to organized locations
move-asset "/Game/BP_Player" "/Game/Blueprints/Characters"
move-asset "/Game/BP_Enemy" "/Game/Blueprints/Characters"
move-asset "/Game/WBP_MainMenu" "/Game/Blueprints/Widgets"
move-asset "/Game/GM_Main" "/Game/Blueprints/GameModes"

# 4. Rename assets for consistency
move-asset "/Game/Blueprints/Characters/PlayerCharacter" "/Game/Blueprints/Characters" "BP_PlayerCharacter"
move-asset "/Game/Blueprints/GameModes/MainMode" "/Game/Blueprints/GameModes" "GM_MainMode"
```

## Implementation Notes

- Uses `IPlatformFile::IterateDirectory` for reliable directory traversal
- File content is read as UTF-8 text
- Timestamps are in local system time
- Directory listings exclude hidden files by default
- All operations are synchronous and require Game Thread