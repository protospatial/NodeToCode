# Material Tools

This directory holds `get-material-n2cjson` (card 0453), the first tool of the `MaterialTools` toolset and the
agent-facing half of the Material MVP (decision 0009): a whole Material or Material Function as N2C JSON in one call,
which Epic's UE 5.8 MaterialTools lacks and which is the only route on UE 5.6 and 5.7. The capture itself is lane-a's
`N2C::CaptureMaterialAsset` (`Source/Public/Core/N2CCapture.h`, cards 0449-0451).

### get-material-n2cjson
- **Description**: Any Material or Material Function as N2C JSON. By asset path it reads the saved asset with no
  editor open (decision 0009 item 3): `target.workingCopy` is false and unapplied Material Editor edits are not
  included. With no `materialPath` (card 0463) it reads the working copy of the focused Material Editor, unapplied edits
  included: `resolvedBy` focus, `workingCopy` true. Only the exact classes `UMaterial` and `UMaterialFunction` are
  captured; engine functions under `/Engine/Functions` are named at depth 0 and expanded only at depth > 0
- **Focused Material Editor**: `FN2CBlueprintEditorTracker::GetFocusedMaterialAsset` (`Core/N2CBlueprintEditorTracker.h`),
  the same events and last-activation rule as the focused Blueprint editor, kept apart from it: the Material Editor
  (FMaterialEditor, toolkit `MaterialEditor`; not the material instance editor) whose asset tab or inner tab was
  activated or that was opened last, falling back to the most recently activated one still open, then to the latest
  `GetLastActivationTime()`. `WorkingCopyOf` gives the copy it edits until Apply: a material's preview material
  (`IMaterialEditor::GetMaterialInterface`, a `UPreviewMaterial` at `/Engine/Transient.<name>`), a function's duplicate
  (`UMaterialGraph::MaterialFunction`). The copy is captured with `FN2CMaterialCaptureContext` directly (the exact-class
  entry point would refuse `UPreviewMaterial`) and named after the asset; the asset's own class decides what is refused.
  Expression GUIDs survive the duplication (`UMaterialExpression::PostDuplicate`), so node ids of unchanged expressions
  equal the saved asset's
- **Arguments** (types, required and defaults: [reference](../../../../../../Context/mcp-server/tools.generated.md#get-material-n2cjson)):
  - `materialPath` (optional): `/Engine/BasicShapes/BasicShapeMaterial` or the object path
    (`/Game/Materials/M_Rock.M_Rock`); omitted or empty reads the focused Material Editor's working copy
  - `depth` (0 to 5): how deep material function calls are expanded. Default: the project's Max Translation Depth
  - `compact` (default true): no whitespace; false indents
  - `toFile` (default false): always write the document to a file
- **Returns**: the document `get-blueprint-n2cjson` returns for a graph (`N2CMcpBlueprintExport::ToResultJson`: nodes
  and pins with `ids` {short, guid}, flows, the guids table; `metadata.asset_kind` Material), plus `target`
  {`assetPath` (the saved asset's, also for a working copy), `kind`: `material` or `material-function`, `resolvedBy`:
  `path` | `focus`, `workingCopy`, and for a focus read `workingCopyPath`, the transient copy that was read}. Over 20000
  estimated tokens, or with `toFile`, it is written to
  `<Project>/Saved/NodeToCode/Exports/<asset>__Material__<hash of path, depth and compact>.json` (a working copy:
  `__MaterialWorkingCopy__`) and the result is
  {`path`, `bytes`, `estimatedTokens`, `maxInlineTokens`, `reason`: `size` | `toFile`, `nodeCount`, `target`}. Marked as
  untrusted data (card 0124): expression descriptions, parameter names and Custom node code come from the asset's author
- **Errors**: `INVALID_ARGUMENT` (`materialPath` not a string; `depth` not a whole number from 0 to 5),
  `NO_FOCUSED_MATERIAL` (no `materialPath` and no Material Editor open), `ASSET_NOT_FOUND`,
  `UNSUPPORTED_ASSET_TYPE` (a Blueprint, naming get-blueprint-n2cjson; a function instance, material layer or layer
  blend, naming its class; a material instance whose chain ends at no material), `CAPTURE_FAILED`, `EXPORT_FAILED`.
  A Material Instance (`MaterialInstanceConstant`, card 0470) is read as its base material's graph, with the instance's
  name and `metadata.material.instance` {`parents`, `scalars`, `vectors`, `textures`, `static_switches`}
- **Related**: `get-blueprint-n2cjson` given a material path answers `UNSUPPORTED_ASSET_TYPE` naming this tool
  (`FN2CMcpGetMaterialN2CJsonTool::IsMaterialAssetPath`). Python: `nodetocode.get_material_n2cjson(material_path,
  depth=-1, compact=True, to_file=False)` runs this tool through `invoke_tool` (by path only)
- **Tests**: `NodeToCode.MCP.MaterialN2CJson` (`Source/Private/Tests/MCP/N2CMaterialN2CJson.spec.cpp`: BasicShapeMaterial,
  WorldGridMaterial, the CameraDepthFade function, toFile, the refusals, the get-blueprint-n2cjson pointer);
  `NodeToCode.MCP.MaterialWorkingCopy` (`N2CMaterialWorkingCopy.spec.cpp`, card 0463: NO_FOCUSED_MATERIAL, the last
  activated of two Material Editors, node ids equal to the saved asset's, an expression added to the working copy seen
  by a focus read and not by a path read, the fallback on close, a Material Function editor);
  `Tests/python/test_material_n2cjson.py`; live `Tests/mcp_probe/live_material_0453.py` and
  `Tests/mcp_probe/live_material_focus_0463.py`
