# Asset validator

The script `scripts/asset_validator.py` reports; it renames, moves and deletes nothing. Compiling is the one thing it
can change (a Blueprint's compiled class, not its graphs or the file on disk), and it only compiles when asked. Run it
in the editor with `run-python` (or give the code to the user for the editor's Python console when run-python is not
available):

```python
import nodetocode as n2c
validator = n2c.skill_script('asset-validator')
result = validator.check_naming('/Game/Characters')
```

Every function returns `{success, data, error}`.

## Functions

| Function | What it reports |
|---|---|
| `check_naming(folder, recursive=True, conventions=None)` | `issues` for assets whose name lacks the prefix of their exact class; classes the table does not list count as `unchecked` |
| `find_assets_without_prefix(folder, prefix, asset_type='Blueprint', recursive=True)` | assets of one exact class without the prefix |
| `validate_blueprint(path, compile_check=False, naming_check=True)` | `is_valid`, `issues` (compile errors) and `warnings` (naming) for one Blueprint |
| `validate_blueprints_in_folder(folder, recursive=True, compile_check=False, naming_check=True)` | the same for every Blueprint of a folder (Actor, Widget, Anim, ...) |
| `check_for_empty_blueprints(folder, recursive=True)` | Blueprints with no logic node in any graph; any it could not read are listed apart |
| `logic_node_count(path)` | the logic nodes of one Blueprint, per graph |
| `get_default_naming_conventions()`, `expected_prefix(class_name)` | the `{exact class: prefix}` table and one lookup |

- **Naming** compares the asset's exact class (`AssetData.asset_class_path.asset_name`) with the table, so a Widget
  Blueprint wants `WBP_`, an Anim Blueprint `ABP_`, a material instance (`MaterialInstanceConstant`) `MI_` and a
  render target (`TextureRenderTarget2D`) `RT_`. A plain Blueprint also passes with a role prefix (`GM_`, `PC_`,
  `AC_`, `BPI_`, ...). Pass `conventions` for a project's own table.
- **Compiling** (`compile_check=True`) runs `compile-blueprint` through `n2c.compile_blueprint` (it works from Python
  also on UE 5.8 when the MCP server leaves that tool to Unreal MCP's `BlueprintTools.compile_blueprint`): the
  compiler's errors become `issues` and `is_valid` is false, on every engine. Ask before compiling a whole folder of
  a large project: each compile takes time.
- **Empty** means every graph holds only function entry or result nodes and the editor's greyed-out default events.

## Reporting

Show the user the counts first, then the issues by asset. Propose fixes (a rename with `rename-asset`, on UE 5.8
with Unreal MCP `AssetTools.move`; a compile error to look at with `get-blueprint-n2cjson`) and wait for the user's
go-ahead before changing anything.
