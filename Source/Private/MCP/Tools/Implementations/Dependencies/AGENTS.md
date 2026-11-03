# Dependency Tools

This directory holds `get-blueprint-dependencies` (card 0319), a read-only tool in the `BlueprintDiscoveryTools`
toolset that gives agents the dependency index of card 0318 (`FN2CDependencyIndex`, `Source/Public/Core/N2CDependencyIndex.h`),
and `N2CMcpDependencies`, which turns the index into JSON for the tool and for the Python bridge
(`UN2CPythonBridge::GetBlueprintDependencies`, `nodetocode.get_blueprint_dependencies`).

## Files

- `N2CMcpDependencies.h/.cpp`: `ToJson` (the index as JSON, assets named by object path), `Run` (builds the index on
  the game thread, adds the export file when asked or when the result is over the inline limit), `ExportDirectory`
  (`<Project>/Saved/NodeToCode/Dependencies`) and `ExportFileName`
- `N2CMcpGetBlueprintDependenciesTool.h/.cpp`: the tool, its input and output schemas and argument checks

### get-blueprint-dependencies
- **Description**: The dependency index of the project's Blueprints, Blueprint interfaces, macro libraries, user
  structs and user enums under asset paths or content folders, by path, with no editor open and no focus.
  `FN2CDependencyIndex::Build` reads the AssetRegistry (hard package dependencies, parent class and implemented
  interface tags) and, with `includeCallGraph`, loads the Blueprints for the edges their graphs and variables hold.
  Engine and engine-plugin content is never indexed. The order is dependencies first: the order to convert or
  translate the assets in. A cycle group is a set of assets that depend on each other and must be handled together
- **Arguments** (types, required and defaults: [reference](../../../../../../Context/mcp-server/tools.generated.md#get-blueprint-dependencies)):
  - `paths` (required, 1 to 100): asset paths (`/Game/Blueprints/BP_Door` or `/Game/Blueprints/BP_Door.BP_Door`) and
    content folders (`/Game/Blueprints`, with its subfolders): the roots of the index
  - `includeCallGraph` (default true): load the Blueprints for `calls`, `macro`, `cast` and `variable-type` edges;
    false reads the AssetRegistry alone (`hard-package`, `parent`, `interface`) and loads nothing
  - `includeReferenced` (default true): also index the project's own assets the roots depend on outside `paths`
    (`root` false), so the order still puts them first
  - `toFile` (default false): also write the full index to the export file
- **Returns** (structured content, declared by the tool's `outputSchema`; marked as untrusted data, card 0124):
  `roots` (the paths as given), `nodeCount`, `edgeCount`, `nodes` (`path`, `name`, `kind`: `enum`, `struct`,
  `interface`, `macro-library`, `blueprint`; `root`), `edges` (`from` depends on `to`, both object paths; `kind`:
  `hard-package`, `parent`, `interface`, `calls`, `macro`, `cast`, `variable-type`; `graph` for node-level edges and a
  local variable; `node`: the node's GUID, or the variable's name for `variable-type`), `order` (every node's path
  once, dependencies first: enums, then structs, interfaces, macro libraries and Blueprints among the ready ones, each
  by path), `cycleGroups` (lists of paths, each sorted by path), `warnings` (paths that named nothing, assets that
  did not load) and `inline`. With `toFile`, or when the result would exceed 20000 estimated tokens
  (`N2CMcpBlueprintExport::MaxInlineTokens`), the index is written, indented, to
  `<Project>/Saved/NodeToCode/Dependencies/<first root's name>__<hash of the paths and flags>.json` (a temporary file
  moved into place; the same call replaces the same file) and the result adds `savedTo` and `bytes`. Over the limit
  `nodes`, `edges`, `order` and `cycleGroups` are left out, `inline` is false, and `cycleGroupCount` and `reason` are
  added: read the file with read-file
- **Errors**: `INVALID_ARGUMENT` (no `paths`, not an array of strings, more than 100 entries, an entry that does not
  start with `/`, a flag that is not a JSON boolean), `NOT_FOUND` (nothing to index under the paths; the message
  carries the warnings), `EXPORT_FAILED` (the file could not be written)
- **Use Case**: Before converting or translating many assets: the order to work in, what must move together, and
  which call or variable makes one asset depend on another
- **Tests**: `NodeToCode.MCP.BlueprintDependencies` (`Source/Private/Tests/MCP/N2CBlueprintDependencies.spec.cpp`)
  over the fixtures in `Tests/NodeToCodeTestContent/Content/Dependencies` (exact order, the cycle group {B, C}, typed
  edges, the export, the inline limit, every argument error); `Tests/python/test_blueprint_dependencies.py` for the
  Python wrapper

The "Tag all dependencies" and "Open Reference Viewer" actions of card 0319 are not built yet: they go into the tag
manager window (`SN2CTagManager`) once card 0258 has reworked it.
