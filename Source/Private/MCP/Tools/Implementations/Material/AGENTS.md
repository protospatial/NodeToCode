# Material Tools

This directory holds `get-material-n2cjson` (card 0453), the first tool of the `MaterialTools` toolset and the
agent-facing half of the Material MVP (decision 0009): a whole Material or Material Function as N2C JSON in one call,
which Epic's UE 5.8 MaterialTools lacks and which is the only route on UE 5.6 and 5.7. The capture itself is lane-a's
`N2C::CaptureMaterialAsset` (`Source/Public/Core/N2CCapture.h`, cards 0449-0451).

### get-material-n2cjson
- **Description**: Any Material or Material Function as N2C JSON by asset path, with no editor open. It reads the
  saved asset (decision 0009 item 3), never a Material Editor's working copy, so `target.workingCopy` is false and
  unsaved edits are not included. `N2C::CaptureMaterialAsset` takes the exact classes `UMaterial` and
  `UMaterialFunction`; engine functions under `/Engine/Functions` are named at depth 0 and expanded only at depth > 0
- **Arguments** (types, required and defaults: [reference](../../../../../../Context/mcp-server/tools.generated.md#get-material-n2cjson)):
  - `materialPath` (required): `/Engine/BasicShapes/BasicShapeMaterial` or the object path
    (`/Game/Materials/M_Rock.M_Rock`)
  - `depth` (0 to 5): how deep material function calls are expanded. Default: the project's Max Translation Depth
  - `compact` (default true): no whitespace; false indents
  - `toFile` (default false): always write the document to a file
- **Returns**: the document `get-blueprint-n2cjson` returns for a graph (`N2CMcpBlueprintExport::ToResultJson`: nodes
  and pins with `ids` {short, guid}, flows, the guids table; `metadata.asset_kind` Material), plus `target`
  {`assetPath`, `kind`: `material` or `material-function`, `resolvedBy`: `path`, `workingCopy`: false}. Over 20000
  estimated tokens, or with `toFile`, it is written to
  `<Project>/Saved/NodeToCode/Exports/<asset>__Material__<hash of path, depth and compact>.json` and the result is
  {`path`, `bytes`, `estimatedTokens`, `maxInlineTokens`, `reason`: `size` | `toFile`, `nodeCount`, `target`}. Marked as
  untrusted data (card 0124): expression descriptions, parameter names and Custom node code come from the asset's author
- **Errors**: `INVALID_ARGUMENT` (no `materialPath`; `depth` not a whole number from 0 to 5), `ASSET_NOT_FOUND`,
  `UNSUPPORTED_ASSET_TYPE` (a Blueprint, naming get-blueprint-n2cjson; a function instance, material layer or layer
  blend, naming its class; a material instance whose chain ends at no material), `CAPTURE_FAILED`, `EXPORT_FAILED`.
  A Material Instance (`MaterialInstanceConstant`, card 0470) is read as its base material's graph, with the instance's
  name and `metadata.material.instance` {`parents`, `scalars`, `vectors`, `textures`, `static_switches`}
- **Related**: `get-blueprint-n2cjson` given a material path answers `UNSUPPORTED_ASSET_TYPE` naming this tool
  (`FN2CMcpGetMaterialN2CJsonTool::IsMaterialAssetPath`). Python: `nodetocode.get_material_n2cjson(material_path,
  depth=-1, compact=True, to_file=False)` runs this tool through `invoke_tool`
- **Tests**: `NodeToCode.MCP.MaterialN2CJson` (`Source/Private/Tests/MCP/N2CMaterialN2CJson.spec.cpp`: BasicShapeMaterial,
  WorldGridMaterial, the CameraDepthFade function, toFile, the refusals, the get-blueprint-n2cjson pointer);
  `Tests/python/test_material_n2cjson.py`; live `Tests/mcp_probe/live_material_0453.py`
