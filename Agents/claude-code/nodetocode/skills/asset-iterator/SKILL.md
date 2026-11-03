---
name: asset-iterator
description: "Finds and walks an Unreal Engine project's assets from the editor's Python: every asset in a Content Browser folder, assets of one class (Blueprints, Widget Blueprints, static meshes, textures, ...) in a folder or the whole project, Blueprints with a given parent class, and a callback over a list of assets. Read-only. Use when the user wants to list, count, find or audit assets by folder, class, name pattern or parent class, or run a check over many assets. SKIP for: reading a Blueprint's graphs (use blueprint-comprehension), naming and compile checks (use asset-validator), and moving, renaming or deleting assets."
license: Apache-2.0
compatibility: "Unreal Engine 5.6 to 5.8 editor running the NodeToCode plugin, with its MCP server's run-python tool (the per-user Allow MCP Python Execution setting on) or the editor's Python console."
metadata:
  author: protospatial
---

# Asset iterator

The script `scripts/asset_iterator.py` queries the editor's AssetRegistry. It reads; it changes no asset. Run it in
the editor with `run-python` (or give the code to the user for the editor's Python console when run-python is not
available), loading it through the nodetocode module:

```python
import nodetocode as n2c
assets = n2c.skill_script('asset-iterator')
found = assets.find_assets_by_type('Blueprint', '/Game/Characters', recursive=False)
result = found
```

Every function returns `{success, data, error}`. An asset entry is `{name, path, class_name, package_name,
package_path}`: `path` is the object path (`/Game/A/BP_Door.BP_Door`), `package_path` the folder (`/Game/A`), and
`class_name` the exact asset class (`Blueprint`, `WidgetBlueprint`, `MaterialInstanceConstant`, ...).

## Functions

| Function | What it returns |
|---|---|
| `find_assets_by_path(folder, recursive=False, include_only_on_disk=False, name_pattern=None)` | the folder's assets, optionally filtered by a case-insensitive glob on the name (`'*Enemy*'`) |
| `find_assets_by_type(class_name, search_path=None, recursive=True, include_subclasses=True)` | assets of a class (`'Blueprint'`, `'StaticMesh'`, a class path such as `'/Script/Engine.Texture2D'`), in a folder or everywhere |
| `list_assets_simple(folder, recursive=True, include_folders=False)` | only the paths |
| `get_asset_info(path)` | one asset's entry with `exists` and `is_valid` |
| `get_blueprints_by_parent(parent, search_path=None)` | Blueprints whose direct parent is `parent` (`'Actor'`, `'/Script/Engine.Character'`), from the AssetRegistry with nothing loaded |
| `iterate_assets(assets, callback, load_assets=True)` | `callback` run on each loaded asset; `processed` counts the calls that returned, `failed` the assets that did not load or raised, and every asset has one entry in `results` |
| `filter_assets(assets, name_pattern=None, class_filter=None, path_prefix=None)` | the entries matching every filter (class exact, folder with subfolders) |

A folder filter matches that folder, and with `recursive` its subfolders: `/Game/Char` never matches
`/Game/Characters`.

## Working with the results

- Large projects return many entries: count first (`data['count']`), and narrow by folder or class before listing.
- To read one Blueprint's logic, pass its `path` to `get-blueprint-n2cjson`.
- Loading assets (`iterate_assets` with `load_assets=True`) takes memory and time; use `load_assets=False` when the
  entry itself is enough.
