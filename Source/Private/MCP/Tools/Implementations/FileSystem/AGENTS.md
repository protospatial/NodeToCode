# File System Tools

This directory contains the MCP tools that read files and directories inside the Unreal Engine project, in the
`FileSystemTools` toolset: `read-path` and `read-file`. Both are read-only. Asset operations such as `move-asset` are
Content Browser tools (see `../ContentBrowser/AGENTS.md`).

read-path declares an `outputSchema` and returns its listing as `structuredContent` too (card 0207). read-file does
not: its result is up to a file part of text, which `structuredContent` would send twice.

## Available Tools

### read-path
- **Description**: Lists the files and folders directly inside a directory (not recursive), in pages
- **Parameters**:
  - `relativePath` (required): the directory, relative to the project folder (`""` or `"."` for the project folder);
    an absolute path must be inside one of the read roots (see "Read Roots")
  - `pageSize` (optional): entries per page, 1 to 1000 (default 200; `FN2CMcpReadPathTool::DefaultPageSize`, `MaxPageSize`)
  - `cursor` (optional): the `nextCursor` of the previous page; left out for the first page. It is the index of the
    page's first entry as a decimal string; a cursor past the end (the folder shrank) is an error that says to start again
  - `filter` (optional): a wildcard (`*`, `?`, `FString::MatchesWildcard`) the names must match, ignoring case, for
    folders and files alike (`"*.json"`)
- **Returns**: `path` (project-relative, see "Paths in Results"), `directories` and `files` (this page's names: the
  entries are ordered folders first, then files, each sorted by name, so a page can hold some of each), `directoryCount`,
  `fileCount` and `total` (every entry that matches the filter, not just this page), `returned`, `pageSize`,
  `truncated` (more entries follow), `nextCursor` (only when `truncated`), `filter` (when given), `success`. Every
  entry is listed, dot files included
- **Use Case**: Browsing the project structure, saved translations and exports

### read-file
- **Description**: Reads a text file, at most 500 KB of it per call, in whole lines
- **Parameters**:
  - `relativePath` (required): the file, relative to the project folder; an absolute path must be inside a read root
  - `offset` (optional): the line number to start at, 1-based (default 1; 0 also means 1)
  - `limit` (optional): the most lines to return (default: as many whole lines as fit in 500 KB)
  - `offset` and `limit` take a JSON number or a numeric string; a negative or fractional value, or a `limit` of 0, is
    an error
- **Returns**: `path` (project-relative), `content`, `size` (the file's size in bytes), `contentBytes` (UTF-8 bytes of
  `content`), `contentType` (a MIME type from the extension, `FN2CContentTypeUtils::GetContentTypeFromExtension`),
  `extension`, `encoding` (`utf-8`, `utf-8-bom`, `utf-16le` or `utf-16be`; `content` is always the decoded text),
  `lastModified` (local time, `YYYY-MM-DD HH:MM:SS`), `startLine` and `endLine` (the first and last line returned, 0
  when none), `lineCount`, `totalLines` (only when the read scanned to the end of the file, see "Bounded Reads"),
  `truncated` (the 500 KB cap stopped the read), `nextOffset` (only when lines follow the returned ones: pass it as
  `offset` to continue), `note` (when one line is longer than 500 KB), `success`
- **Use Case**: Reading configuration, source files, logs, documentation, N2C JSON exports and saved translations

## Path Handling

Both tools call `FN2CPathUtils::ResolveAgentReadPath` (`Source/Private/Utils/N2CPathUtils.cpp`) and use the absolute
path it returns. It picks the root, joins and checks the path (`ValidatePathWithinBounds`, the path jail) and applies
the credential deny-list (`IsDeniedForAgentAccess`). `NodeToCode.PathPolicy`
(`Source/Private/Tests/MCP/N2CPathPolicy.spec.cpp`) tests the jail and the deny-list; `NodeToCode.MCP.FileSystemTools`
(`Source/Private/Tests/MCP/N2CFileSystemTools.spec.cpp`) tests the tools, the roots and the read limits.

### Read Roots (`FN2CPathUtils::GetAgentReadRoots`)
- The project folder (`FPaths::ProjectDir()`), always first; relative paths are joined to it
- The translation output directory (`FN2CPathUtils::GetTranslationsBasePath`, made absolute the way
  `get-translation-output-directory` reports it) when it is outside the project. It is reached by absolute path only:
  pass the `absolutePath` that `get-translation-output-directory` returns, plus the rest of the path
- That second root is left out, with one warning in the log per setting value, when it is too broad: a drive or
  file-system root, or a folder that holds the project folder or the user's home or Documents folder. read-file and
  read-path then refuse it like any other absolute path

### Path Jail (`FN2CPathUtils::ValidatePathWithinBounds`)
- The result must stay inside its root; otherwise the call fails with `Access denied: Path traversal detected`
- `..` and `.` segments are collapsed first (`FPaths::CollapseRelativeDirectories`), so `"Saved/../Config"` reads
  `Config`, while `"../"` or `"../../Windows/win.ini"` is refused; a `..` that climbs above the drive root is refused
- The containment test is on a separator boundary (`FPaths::IsUnderDirectory`), so `<Project>Evil/x` is not inside
  `<Project>`, and the root itself (with or without a trailing slash) is inside
- Symlinks and junctions are resolved (`FN2CPathUtils::IsUnderDirectoryResolved` and `ResolveRealPath`:
  `GetFinalPathNameByHandleW` on Windows, `realpath` elsewhere). A link that leads outside its root is refused with
  the traversal error; one that stays inside is followed, and `path` reports the link's target. In a dev host
  whose `Plugins/NodeToCode` folder is a junction to a checkout, that folder is therefore not readable
- Backslashes work like slashes (`"Config\\Windows"`)
- On Windows, names match case-insensitively (`"config/defaultengine.INI"` reads `Config/DefaultEngine.ini`); elsewhere
  the file system decides

### Absolute Paths
- A path with a drive letter (`C:/...`, `C:\...`) or a UNC or device prefix (`//server/share`, `\\?\C:\...`) is
  absolute. On Mac and Linux a path from `/` is absolute when it lies inside a read root
- An absolute path inside a read root works like a relative one (an absolute path inside the project reads the same
  file as its relative form). Any other absolute path fails with `Access denied: an absolute path must be inside the
  project folder or the translation output directory; use a path relative to the project folder`, and the log says
  `Blocked MCP read of an absolute path outside the read roots`

### Credential Deny-List (`FN2CPathUtils::IsDeniedForAgentAccess`)
Checked on the resolved path and on its spelling before links are resolved, inside every root. These fail with
`Access denied: this path holds credentials and is not readable through MCP`, however the path is spelled:
- `Saved/NodeToCode/User` and everything under it
- `FPlatformProcess::UserSettingsDir()/NodeToCode` (the per-user secret store, outside the project)
- `<Project>/.git` and everything under it
- Any file named `secrets.json*` (including its `.tmp`, `.bak` and `.corrupt-<timestamp>` siblings), `.env`, or ending
  in `.env`, in either root

### Hidden Files
Nothing is filtered by name: `read-path` lists dot files and hidden files (including the names of denied entries such
as `.git`, never their contents), and `read-file` reads them when their type is allowed, unless the deny-list above
covers them.

### Root and Special Forms
- `""`, `"."`, `"./"` and `"/"` all name the project folder (`path` is then `""`)
- A leading `/` is dropped from a relative path (`"/Config"` reads `Config`)

### Paths in Results
`path` is the resolved path relative to the project folder, with forward slashes (`""` for the project folder), or the
absolute path when it is in a translation output directory outside the project. No result carries the absolute path of
a project file.

## File Restrictions (`FN2CMcpReadFileTool::Execute`)

### Text Types Only
- Extensions read (`N2CReadFile::TextExtensions` in `N2CMcpReadFileTool.cpp`): `.txt .log .md .markdown .rst .json
  .jsonl .ndjson .csv .tsv .xml .yaml .yml .toml .html .htm .css .ini .cfg .conf .config .properties .editorconfig
  .gitignore .gitattributes .clang-format .p4ignore .uproject .uplugin .uprojectdirs .usf .ush .hlsl .glsl .h .hh .hpp
  .hxx .inl .ipp .c .cc .cpp .cxx .cs .py .pyi .js .mjs .cjs .ts .tsx .jsx .swift .java .kt .lua .m .mm .bat .cmd .ps1
  .psm1 .sh .bash .cmake .mk .diff .patch`, and files with no extension (`README`, `LICENSE`)
- Any other extension (`.uasset`, `.umap`, `.uexp`, `.ubulk`, `.png`, `.dll`, `.pdb`, ...) fails with
  `Unsupported file type '.<ext>': read-file reads text files only (<the list>, or a file with no extension)`
- The content is checked too, and fails with `Unsupported content: <path>: <why>; read-file reads text files only`: a
  NUL byte in the first 8 KB of a UTF-8 file (`the file looks binary`), returned text that is not valid UTF-8, or a
  UTF-16 file with a NUL or an unpaired surrogate. A UTF-8 BOM is skipped; a UTF-16 file with a BOM (`FF FE` or
  `FE FF`, as PowerShell's `Tee-Object` writes) is decoded

### Bounded Reads
- The file is streamed (`IFileManager::CreateFileReader`, 64 KB at a time, opened so a log being written can be read);
  it is never loaded whole
- One call returns at most 500 KB (`FN2CMcpReadFileTool::MaxContentBytes`, 512,000 UTF-8 bytes) of whole lines: the
  line that would cross the cap is left for the next call, `truncated` is `true` and `nextOffset` names it. A file that
  grows while it is read cannot push the reply past the cap
- A single line longer than 500 KB returns its first 500 KB (cut on a character boundary) with a `note`; `nextOffset`
  is the line after it, and the rest of that line cannot be read
- To reach `offset` and count `totalLines`, the read scans at most the first 64 MB of the file
  (`N2CReadFile::MaxScanBytes`). Past that `totalLines` is left out, and an `offset` beyond it is an error
- A directory fails with `<path> is a directory; use read-path to list it`; a missing file with `File does not exist`
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
- **Project Root**: `""` (or `"."`)
- **Subdirectories**: `"Config"`, `"Source/Classes"` (backslashes also work)
- **Nested Paths**: `"Plugins/NodeToCode/Content"`
- **Translation output directory outside the project**: its absolute path, as `get-translation-output-directory`
  returns it

### Patterns That Fail
- `"../OtherProject"` and any other path that leaves its root (traversal error)
- `"C:/absolute/path"` outside the project and the translation output directory (absolute-path error)
- Credential paths (deny-list error)
- Binary and other non-text files (unsupported type or content)

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
Every translation is saved under the translation output directory (`get-translation-output-directory`; default
`Saved/NodeToCode/Translations`), one run folder each with a `manifest.json`: the editor's window, translate-batch,
translate-focused-blueprint and the Python API. When a custom output directory is outside the project, pass its
`absolutePath` (and the rest of the path) instead of a project-relative path. The folder layout is in
`../Translation/AGENTS.md`.

```bash
# 1. List saved translations
read-path "Saved/NodeToCode/Translations"

# 2. List one translation run (folder name <Blueprint>_<yyyyMMdd-HHmmss-fff>_<run id>); manifest.json lists its entries
read-path "Saved/NodeToCode/Translations/BP_Player_20260115-103000-123_1a2b3c4d"

# 3. Read a graph's translated code (one folder per graph, struct or enum of the response)
read-file "Saved/NodeToCode/Translations/BP_Player_20260115-103000-123_1a2b3c4d/EventGraph/EventGraph.cpp"

# 4. With a custom output directory outside the project, use its absolute path
read-path "D:/Translations/BP_Player_20260115-103000-123_1a2b3c4d"
```

### Reading a Large File in Parts
```bash
# 1. The first 500 KB of whole lines; the result has truncated: true and nextOffset: 5121
read-file "Saved/Logs/MyProject.log"

# 2. Continue where it stopped
read-file "Saved/Logs/MyProject.log" offset=5121

# 3. Or read a known range: lines 200 to 249
read-file "Saved/Logs/MyProject.log" offset=200 limit=50
```

### Paging a Large Folder
```bash
# 1. The first 200 entries; total says how many there are, nextCursor where the next page starts
read-path "Content/ImportedData" pageSize=200

# 2. The next page
read-path "Content/ImportedData" pageSize=200 cursor="200"

# 3. Only the JSON files
read-path "Content/ImportedData" filter="*.json"
```

## Implementation Notes

- `read-path` iterates with `IPlatformFile::IterateDirectory` (immediate children only), sorts, then slices one page
- `read-file` streams the file with `IFileManager::CreateFileReader` (`FILEREAD_AllowWrite`) through
  `N2CReadFile::ReadLines`; the UTF-8 check is `N2CReadFile::IsValidUtf8`
- Timestamps are in local system time
- Both tools run their work on the game thread (`ExecuteOnGameThread`)
- A search-in-files tool is a separate card (it was left out of card 0184)
