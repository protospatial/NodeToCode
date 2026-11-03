---
name: project-folder-scaffold
description: "Creates a standard Content Browser folder layout in an Unreal Engine project from the editor's Python: one folder, a list of folders, a feature folder with chosen subfolders, or a named template (feature_standard, character, level, ui_module, gameplay_system, ai, weapon, environment). Only creates folders, under /Game or a project plugin. Use when the user wants to set up, scaffold or organize the folders for a new feature, character, level or module. SKIP for: moving, renaming or deleting folders and assets (the Content Browser tools do that, with the user's go-ahead), and creating assets."
license: Apache-2.0
compatibility: "Unreal Engine 5.6 to 5.8 editor running the NodeToCode plugin, with its MCP server's run-python tool (the per-user Allow MCP Python Execution setting on) or the editor's Python console."
metadata:
  author: protospatial
---

# Project folder scaffold

The script `scripts/project_folder_scaffold.py` creates empty Content Browser folders. It never deletes or moves a
folder or an asset, and it refuses paths under `/Engine`, `/Script`, `/Temp` and `/Memory`. Run it in the editor with
`run-python` (or give the code to the user for the editor's Python console when run-python is not available):

```python
import nodetocode as n2c
scaffold = n2c.skill_script('project-folder-scaffold')
result = scaffold.create_from_template('/Game/Features/Inventory', 'feature_standard')
```

Every function returns `{success, data, error}`.

## Functions

| Function | What it does |
|---|---|
| `get_available_templates()` | `{template: [subfolders]}` |
| `create_from_template(base, template)` | the base folder and the template's subfolders |
| `create_feature_folders(base, subfolders)` | the base folder and the given subfolders |
| `create_folders(paths)` | each folder on its own: `created`, `already_existed` and `failed` lists; `success` is false when any failed |
| `create_folder(path)` | one folder; an existing one is reported, not an error |
| `folder_exists(path)` | `True` or `False` |

Templates: `feature_standard` (Blueprints, Data, UI, Materials, Audio), `character` (Blueprints, Meshes, Animations,
Materials, Audio), `level` (Blueprints, Geometry, Lighting, Audio, Data), `ui_module` (Widgets, Styles, Icons, Data),
`gameplay_system` (Blueprints, Data, Config, Tests), `ai` (Blueprints, BehaviorTrees, Blackboards, Data, Debug),
`weapon` (Blueprints, Meshes, Materials, Audio, VFX), `environment` (Props, Foliage, Materials, Decals, Lighting).

## Before creating

Say which folders you will create and where, and let the user change the list; check what exists first with
`folder_exists` or `read-content-browser-path` (on UE 5.8 with Unreal MCP `AssetTools.list_folders`). Report what
was created and what already existed.
