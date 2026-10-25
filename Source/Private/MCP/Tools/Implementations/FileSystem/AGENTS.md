# File System Tools

This directory contains the MCP tools that read files and directories inside the Unreal Engine project, in the
'File System' category: `read-path` and `read-file`. Both are read-only. Asset operations such as `move-asset` are
Content Browser tools (see `../ContentBrowser/AGENTS.md`).

## Available Tools

### read-path
- **Description**: Lists the files and folders directly inside a directory of the project (not recursive)
- **Parameters**:
  - `relativePath` (required): Path relative to the project root (use `""` for the root)
- **Returns**: `path` (as given), `absolutePath` (the resolved path), `fileCount`, `directoryCount`, `files` and
  `directories` (sorted names, every entry including dot files), `success`
- **Use Case**: Browsing the project structure

### read-file
- **Description**: Reads a text file inside the project
- **Parameters**:
  - `relativePath` (required): Path to the file relative to the project root
- **Returns**: `path` (as given), `absolutePath` (the resolved path), `content`, `size` (bytes), `contentType` (a MIME
  type from the extension, `FN2CContentTypeUtils::GetContentTypeFromExtension`: `text/x-c++src`, `text/markdown`,
  `application/json`, `text/plain` for `.ini`, ...), `extension`, `lastModified` (local time, `YYYY-MM-DD HH:MM:SS`),
  `success`
- **Use Case**: Reading configuration, source files, documentation and saved translations

## Path Handling

Both tools build the path the same way in their `Execute`: `FPaths::Combine(FPaths::ProjectDir(), relativePath)`,
then `FN2CPathUtils::ValidatePathWithinBounds` (the path jail), then `FN2CPathUtils::IsDeniedForAgentAccess` (the
credential deny-list). Both helpers are in `Source/Private/Utils/N2CPathUtils.cpp`; `NodeToCode.PathPolicy`
(`Source/Private/Tests/MCP/N2CPathPolicy.spec.cpp`) tests them.

### Path Jail (`FN2CPathUtils::ValidatePathWithinBounds`)
- The result must stay inside the project directory; otherwise the call fails with
  `Access denied: Path traversal detected`
- `..` and `.` segments are collapsed first (`FPaths::CollapseRelativeDirectories`), so `"Saved/../Config"` reads
  `Config`, while `"../"` or `"../../Windows/win.ini"` is refused
- Absolute paths are not recognised as such: they are joined under the project root like any other path. A leading `/`
  is dropped (`"/Config"` reads `Config`), and `"C:/Windows/win.ini"` names `<Project>/C:/Windows/win.ini`, which does
  not exist, so the call fails with `File does not exist` (`Directory does not exist` for `read-path`), even when the
  absolute path points inside the project
- Symlinks and junctions are resolved (`FN2CPathUtils::IsUnderDirectoryResolved` and `ResolveRealPath`:
  `GetFinalPathNameByHandleW` on Windows, `realpath` elsewhere). A link that leads outside the project is refused with
  the traversal error; one that stays inside is followed, and `absolutePath` reports the link's target. In a dev host
  whose `Plugins/NodeToCode` folder is a junction to a checkout, that folder is therefore not readable
- Backslashes work like slashes (`"Config\\Windows"`)
- On Windows, names match case-insensitively (`"config/defaultengine.INI"` reads `Config/DefaultEngine.ini`); elsewhere
  the file system decides

### Credential Deny-List (`FN2CPathUtils::IsDeniedForAgentAccess`)
Checked on the resolved path after the jail. These fail with
`Access denied: this path holds credentials and is not readable through MCP`, however the path is spelled:
- `Saved/NodeToCode/User` and everything under it
- `FPlatformProcess::UserSettingsDir()/NodeToCode` (the per-user secret store, outside the project)
- `<Project>/.git` and everything under it
- Any file named `secrets.json*` (including its `.tmp`, `.bak` and `.corrupt-<timestamp>` siblings), `.env`, or ending
  in `.env`

### Hidden Files
Nothing is filtered by name: `read-path` lists dot files and hidden files, and `read-file` reads them, unless the
deny-list above covers them.

### Root and Special Forms
- `""`, `"/"` and `"./"` all name the project root
- `"."` alone is refused with the traversal error (it collapses to the root without its trailing slash, which the jail's
  prefix check rejects); use `""`

## File Restrictions (`FN2CMcpReadFileTool::Execute`)
- Maximum size: 500 KB (`MaxFileSize`, 512,000 bytes, checked against the file's size on disk); a larger file fails
  with `File too large: <size> bytes (max: 512000)`
- `.uasset` and `.umap` are refused by extension; any other file is read as text (`FFileHelper::LoadFileToString`), so
  other binary files are not detected
- A directory fails with `File does not exist`; use `read-path` for directories
- Read-only: neither tool writes anything

## Usage Examples

### Browsing Project Structure
The curl examples assume `$SID` holds a session id from `initialize` (see "Calling the Server with curl" in `Source/Private/MCP/Tools/AGENTS.md`); every POST after `initialize` must send it.

```bash
# List project root
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
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
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "read-path",
      "arguments": {"relativePath": "Config"}
    },
    "id": 2
  }'

# List plugin directory (refused when Plugins/NodeToCode is a junction to a folder outside the project)
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
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
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
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
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
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

## Path Format Guidelines

### Correct Path Usage
- **Project Root**: `""`
- **Subdirectories**: `"Config"`, `"Source/Classes"` (backslashes also work)
- **Nested Paths**: `"Plugins/NodeToCode/Content"`

### Patterns That Fail
- `"."` (traversal error; `"./"` and `"/"` happen to work, `""` is the documented form)
- `"../OtherProject"` and any other path that leaves the project (traversal error)
- `"C:/absolute/path"` (joined under the project root, so the file is not found)
- Credential paths (deny-list error)

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

### Reading Saved Translations
Translations run from the editor's NodeToCode window are saved under the translation output directory
(`get-translation-output-directory`; default `Saved/NodeToCode/Translations`). `translate-focused-blueprint` saves
nothing; its translation is the tool result. A custom output directory outside the project is outside the path jail,
so these tools cannot read it. The folder layout is in `../Translation/AGENTS.md`.

```bash
# 1. List saved translations
read-path "Saved/NodeToCode/Translations"

# 2. List one translation (folder name <Blueprint>_<YYYY-MM-DD-HH.MM.SS>)
read-path "Saved/NodeToCode/Translations/BP_Player_2026-01-15-10.30.00"

# 3. Read a graph's translated code (one folder per graph)
read-file "Saved/NodeToCode/Translations/BP_Player_2026-01-15-10.30.00/EventGraph/EventGraph.cpp"
```

## Implementation Notes

- `read-path` iterates with `IPlatformFile::IterateDirectory` (immediate children only)
- File content is read with `FFileHelper::LoadFileToString`
- Timestamps are in local system time
- Both tools run their work on the game thread (`ExecuteOnGameThread`)
