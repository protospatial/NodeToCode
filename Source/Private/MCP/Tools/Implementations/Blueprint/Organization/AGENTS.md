# Blueprint Organization Tools

This directory contains the MCP tools in the `BlueprintTagTools` toolset: tagging Blueprint graphs, listing the
tags and removing them. The tools are thin wrappers over `UN2CTagManager`, which stores every tag in
`UN2CGraphStateManager` (`Source/Private/Core/N2CGraphStateManager.cpp`), the per-graph state that also records each
graph's translation and JSON export.

## Available Tools

### tag-blueprint-graph
- **Description**: Tags a graph under its `GraphGuid` (`FN2CMcpTagBlueprintGraphTool::Execute`): the one
  `blueprintPath` plus `graphName` or `graphGuid` name, else the focused graph of the active Blueprint editor
  (`FN2CGraphTarget::Resolve`, card 0274; a read-only graph can be tagged). The result has `target`
- **Parameters**:
  - `tag` (required, not empty): The tag name
  - `category` (optional, default `Default`): Grouping. An empty or blank category is stored as `Default`
    (`FN2CTagEntry::NormalizeCategory`, also applied by `UN2CGraphStateManager::AddTag` for every caller)
  - `description` (optional): Free text
  - `blueprintPath`, `graphName`, `graphGuid` (optional): The graph to tag, as for the Graph tools
- **Returns**: `success`, `message` (`Successfully tagged EventGraph with 'PlayerMovement'`) and `taggedGraph` (`tag`,
  `category`, `description`, `graphGuid` (hyphenated), `graphName`, `blueprintPath`, `timestamp` (UTC, ISO 8601))
- **Duplicates**: a tag with the same name and category, compared case-insensitively with no wildcard
  (`FN2CTagEntry::MatchesTagExactly`), is not added again and the call still succeeds. The `taggedGraph` it returns
  echoes the request (`playermovement` / `feature`, a new timestamp), not the stored tag, which keeps its first
  spelling and timestamp. The same name in another category is a separate tag
- **Errors**: `Missing required parameter: tag` (missing or empty), the resolver's `NO_FOCUSED_GRAPH`, `ASSET_NOT_FOUND`,
  `GRAPH_NOT_FOUND` and `AMBIGUOUS_GRAPH_NAME`,
  `Current graph is not valid for tagging` (a graph without a `GraphGuid`), `Failed to persist tag`

### list-blueprint-tags
- **Description**: Lists stored tags (read-only, `readOnlyHint`; results marked as untrusted content, card 0124:
  `untrusted_content` is the first key)
- **Parameters** (all optional; every filter given applies, together, `FN2CMcpListBlueprintTagsTool::Execute`,
  card 0294):
  - `graphGuid`: Tags of that graph, in every Blueprint that has it. With hyphens or without (`FGuid::Parse`)
  - `tag`: Tags with that name, case-insensitive (`UN2CTagManager::GetGraphsWithTag`)
  - `category`: Tags in that category, case-insensitive (`GetTagsInCategory`)
  - none: every tag (`GetAllTags`)
- **Returns**: `untrusted_content`, `tags` (each as in `taggedGraph` above), `count`, `summary` (`uniqueTagNames`
  and `uniqueCategories` across all stored tags, not only the listed ones) and `appliedFilters` (the filters given,
  each of which narrowed the list)
- **Errors**: `Invalid graphGuid format: Invalid GUID format`

### remove-tag-from-graph
- **Description**: Removes every tag with that name from a graph, whatever its category
  (`UN2CGraphStateManager::RemoveTagByName`, case-insensitive)
- **Parameters**:
  - `graphGuid` (required): The graph's GUID, as `list-blueprint-tags` returns it (hyphens optional)
  - `tag` (required, not empty): Tag name
- **Returns**: `success` (true also when nothing matched), `message` (`Tag 'X' removed from graph`, `Removed 2
  instances of tag 'X' from graph`, or `Tag 'X' was not found on graph <guid> (no action taken)`), `removedTag`
  (`tag`, `category` and `graphGuid` of the first match, only when something was removed) and `remainingTags`
- **Errors**: `Invalid graph GUID format: Invalid GUID format`, `Missing required parameter: graphGuid` or `tag`

## Storage

- **File.** Tags live in `<Project>/Saved/NodeToCode/BlueprintGraphState.json` (`GetStateFilePath`), one entry per
  graph keyed by `GraphGuid`, together with that graph's translation and JSON-export state. A graph left with no tags,
  translation or export is dropped from the file.
- **Writes.** Every change saves the whole in-memory state at once (`SaveState`, no undo). Since card 0066 the write
  is atomic (`FN2CFileUtils::SaveStringAtomically`: a temporary file, then a replace, with the previous file kept as
  `BlueprintGraphState.json.bak`); a corrupt file is set aside as `.corrupt-*` and saving stops until the tags are
  reloaded (`LoadStateInternal`), so a bad file is never overwritten.
- **Reads.** The file is read once, when the editor starts (`UN2CGraphStateManager::Initialize`), and again only when
  the user reloads the tags (`UN2CTagManager::LoadTags`). Tags survive editor restarts and belong to one project.
- **Several editors on one project: the last writer wins.** There is no lock and no merge: each editor keeps its own
  copy in memory and writes all of it on every change. On UE 5.8, a tag written into the file from outside while the
  editor ran was not seen by `list-blueprint-tags`, and the editor's next `tag-blueprint-graph` rewrote the file
  without it. Tag from one editor at a time, or reload the tags in the other editor before tagging there.
- **Known issue (suggested card 0436):** no lock or merge between editors.
- **Keys.** Tags follow a graph's `GraphGuid`, which survives a rename; the stored `graphName` and `blueprintPath`
  are updated the next time that graph is tagged.
- **Known issue (card 0192):** a Blueprint duplicated in the Content Browser keeps its graphs' GUIDs, so the copy
  shares the original's tags; renamed or deleted Blueprints keep stale paths.

## Examples

The curl examples assume `$SID` holds a session id from `initialize` (see "Calling the Server with curl" in
`Source/Private/MCP/Tools/AGENTS.md`); every POST after `initialize` must send it.

```bash
# Tag the focused graph
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "tag-blueprint-graph",
      "arguments": {
        "tag": "PlayerMovement",
        "category": "Feature",
        "description": "Core movement logic including walk, run, jump"
      }
    },
    "id": 1
  }'

# Every graph tagged PlayerMovement (any category)
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "list-blueprint-tags",
      "arguments": { "tag": "PlayerMovement" }
    },
    "id": 2
  }'

# Remove it again, by the graphGuid from the listing
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "remove-tag-from-graph",
      "arguments": { "graphGuid": "DFDB5585-48EE-D4EF-AC46-AF8239259FC2", "tag": "PlayerMovement" }
    },
    "id": 3
  }'
```

Typical uses: review status (`NeedsReview`, `Approved` in a `Review` category), technical debt (`PerformanceIssue`
in `TechDebt`), or feature membership, then `list-blueprint-tags` by tag or category to find the graphs again.
