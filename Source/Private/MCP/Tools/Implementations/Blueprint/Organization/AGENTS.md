# Blueprint Organization Tools

This directory contains MCP tools for organizing and tracking Blueprint graphs using a tagging system. These tools help manage complex Blueprint projects by adding metadata to graphs.

## Available Tools

### tag-blueprint-graph
- **Description**: Tags the currently focused Blueprint graph with metadata
- **Parameters**:
  - `tag` (required): The tag name to apply
  - `category` (optional): Category for organizing tags
  - `description` (optional): Additional notes about the tag
- **Returns**: Tagged graph information with timestamp
- **Use Case**: Marking graphs for organization and tracking

### list-blueprint-tags
- **Description**: Lists tags applied to Blueprint graphs with filtering
- **Parameters** (all optional):
  - `graphGuid`: Filter by specific graph GUID
  - `tag`: Filter by tag name
  - `category`: Filter by category
- **Returns**: Tag list with summary statistics
- **Use Case**: Finding tagged graphs and understanding organization

### remove-tag-from-graph
- **Description**: Removes a tag from a Blueprint graph
- **Parameters**:
  - `graphGuid` (required): GUID of the graph
  - `tag` (required): Tag name to remove
- **Returns**: Removal confirmation and remaining tag count
- **Use Case**: Cleaning up outdated tags

## Tagging System Features

### Tag Properties
- **Tag Name**: Primary identifier (e.g., "PlayerMovement", "NeedsRefactor")
- **Category**: Organizational grouping (e.g., "Gameplay", "UI", "Audio")
- **Description**: Extended notes about why the tag was applied
- **Timestamp**: ISO 8601 timestamp of when tag was applied
- **Graph Context**: GUID, name, and Blueprint path

### Persistence
- Tags are saved to disk in the project's Saved directory
- Tags survive editor restarts and project reloads
- Tags are project-specific, not shared between projects

### Idempotency
- Applying the same tag multiple times is safe
- Removing non-existent tags doesn't cause errors

## Usage Examples

### Organizing Feature Development
The curl examples assume `$SID` holds a session id from `initialize` (see "Calling the Server with curl" in `Source/Private/MCP/Tools/AGENTS.md`); every POST after `initialize` must send it.

```bash
# Tag graphs related to player movement
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
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
```

### Tracking Technical Debt
```bash
# Mark graphs that need optimization
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "tag-blueprint-graph",
      "arguments": {
        "tag": "PerformanceIssue",
        "category": "TechDebt",
        "description": "Heavy tick operations, needs refactoring"
      }
    },
    "id": 1
  }'
```

### Finding Related Graphs
```bash
# Find all graphs tagged with a specific feature
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "list-blueprint-tags",
      "arguments": {
        "tag": "PlayerMovement"
      }
    },
    "id": 1
  }'

# Find all tech debt items
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "list-blueprint-tags",
      "arguments": {
        "category": "TechDebt"
      }
    },
    "id": 1
  }'
```

## Workflow Patterns

### Feature Development Workflow
1. Tag all graphs related to a feature during development
2. Use tags to quickly navigate between related graphs
3. List all graphs for a feature before deployment
4. Remove feature tags after completion

### Code Review Workflow
1. Tag graphs with review status: "NeedsReview", "InReview", "Approved"
2. Filter by review status to track progress
3. Add reviewer notes in tag descriptions
4. Clean up review tags after merge

### Refactoring Workflow
1. Tag graphs that need refactoring with specific issues
2. Prioritize by filtering tags
3. Track refactoring progress with status tags
4. Verify all refactoring tags are resolved

## Implementation Notes

- Tags are stored in a JSON file in the Saved directory
- Graph GUIDs ensure tags remain valid even if graphs are renamed
- The system handles concurrent access from multiple editor instances
- Tag operations are not included in Unreal's undo system (persistent metadata)