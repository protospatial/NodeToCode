# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
Check a project's assets against naming conventions, compile Blueprints and find empty ones, from the editor's
Python (the asset-validator Agent Skill, decision 0010).

Load it through run-python:

    import nodetocode as n2c
    validator = n2c.skill_script('asset-validator')
    result = validator.check_naming('/Game/Characters')

Every function returns {success: bool, data: {...} or None, error: str or None}. Only compiling changes anything (a
Blueprint's compiled class, never its graphs or the file on disk), and it is opt-in.

Ported from the 1.x script library's asset_validator with its review findings fixed:
- python-bridge-31: the compile check could never see an error (compile_blueprint returns nothing on 5.6/5.7); it
  now runs the compile-blueprint MCP tool through nodetocode.compile_blueprint, which reports the compiler's errors
  on every engine;
- python-bridge-32: check_naming matched class names by substring in dict order, so every WidgetBlueprint,
  AnimBlueprint, material instance and render target got the wrong prefix; it now looks the asset's exact class up;
- python-bridge-33: check_for_empty_blueprints reported every Blueprint as empty; it now counts the nodes of the
  Blueprint's N2C JSON (get-blueprint-n2cjson), leaving out function entry and result nodes and the editor's ghost
  default events.
"""

from typing import Any, Dict, List, Optional

import unreal

import nodetocode as n2c
from nodetocode.utils import log_warning, make_error_result, make_success_result

# Exact asset class (AssetData.asset_class_path.asset_name) -> expected name prefix
DEFAULT_CONVENTIONS = {
    'Blueprint': 'BP_',
    'BlueprintGeneratedClass': 'BP_',
    'WidgetBlueprint': 'WBP_',
    'EditorUtilityWidgetBlueprint': 'EUW_',
    'EditorUtilityBlueprint': 'EUB_',
    'AnimBlueprint': 'ABP_',
    'BlueprintFunctionLibrary': 'BPFL_',
    'Material': 'M_',
    'MaterialFunction': 'MF_',
    'MaterialInstanceConstant': 'MI_',
    'MaterialParameterCollection': 'MPC_',
    'StaticMesh': 'SM_',
    'SkeletalMesh': 'SK_',
    'Skeleton': 'SKEL_',
    'PhysicsAsset': 'PHYS_',
    'Texture2D': 'T_',
    'TextureCube': 'T_',
    'TextureRenderTarget2D': 'RT_',
    'ParticleSystem': 'PS_',
    'NiagaraSystem': 'NS_',
    'NiagaraEmitter': 'NE_',
    'SoundCue': 'SC_',
    'SoundWave': 'SW_',
    'AnimMontage': 'AM_',
    'AnimSequence': 'AS_',
    'BlendSpace': 'BS_',
    'BlendSpace1D': 'BS_',
    'DataTable': 'DT_',
    'DataAsset': 'DA_',
    'PrimaryDataAsset': 'DA_',
    'UserDefinedEnum': 'E_',
    'UserDefinedStruct': 'F_',
    'CurveFloat': 'Curve_',
    'CurveVector': 'Curve_',
    'CurveLinearColor': 'Curve_',
    'BehaviorTree': 'BT_',
    'BlackboardData': 'BB_',
}

# Blueprint classes whose prefix can also mark what the Blueprint is (a game mode, a player controller, ...)
BLUEPRINT_PREFIXES = ('BP_', 'WBP_', 'ABP_', 'BPI_', 'BPFL_', 'GM_', 'GS_', 'PC_', 'PS_', 'AC_', 'EUW_', 'EUB_')

# N2C JSON node types that hold no logic of their own
STRUCTURAL_NODE_TYPES = {'FunctionEntry', 'FunctionResult'}


def get_default_naming_conventions() -> Dict[str, str]:
    """A copy of the default {exact asset class: prefix} table"""
    return dict(DEFAULT_CONVENTIONS)


def expected_prefix(class_name: str, conventions: Optional[Dict[str, str]] = None) -> Optional[str]:
    """The prefix an asset of this exact class should have, or None when the table has no entry for it"""
    table = conventions if conventions is not None else DEFAULT_CONVENTIONS
    if class_name in table:
        return table[class_name]
    lowered = {k.lower(): v for k, v in table.items()}
    return lowered.get((class_name or '').lower())


def _folder_assets(folder_path: str, recursive: bool):
    """[(object path, asset name, exact class name)] of the assets in a folder, from the AssetRegistry"""
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    found = registry.get_assets_by_path(folder_path.strip().rstrip('/'), recursive, False) or []
    return [(f'{a.package_name}.{a.asset_name}', str(a.asset_name), str(a.asset_class_path.asset_name))
            for a in found if a.is_valid()]


def check_naming(folder_path: str, recursive: bool = True,
                 conventions: Optional[Dict[str, str]] = None) -> Dict[str, Any]:
    """
    Assets whose name lacks the prefix their exact class has in the conventions table.

    Args:
        folder_path: Content folder ('/Game/Characters')
        recursive: Also its subfolders
        conventions: {exact class name: prefix}; default get_default_naming_conventions()

    Returns:
        {success, data: {total, valid, invalid, unchecked, issues: [{path, name, class, expected_prefix, message}],
         folder_path, conventions_used}, error}. Assets of a class the table does not list count as unchecked.
    """
    if not folder_path or not folder_path.strip():
        return make_error_result('folder_path cannot be empty')
    table = conventions if conventions is not None else get_default_naming_conventions()
    try:
        valid = unchecked = 0
        issues = []
        for path, name, class_name in _folder_assets(folder_path, recursive):
            prefix = expected_prefix(class_name, table)
            if prefix is None:
                unchecked += 1
            elif name.startswith(prefix) or (class_name == 'Blueprint' and conventions is None
                                              and name.startswith(BLUEPRINT_PREFIXES)):
                valid += 1
            else:
                issues.append({'path': path, 'name': name, 'class': class_name, 'expected_prefix': prefix,
                               'message': f"{class_name} '{name}' should start with '{prefix}'"})
        return make_success_result({'total': valid + len(issues), 'valid': valid, 'invalid': len(issues),
                                    'unchecked': unchecked, 'issues': issues, 'folder_path': folder_path.strip(),
                                    'conventions_used': table})
    except Exception as e:
        return make_error_result(f'check_naming failed: {e}')


def find_assets_without_prefix(folder_path: str, prefix: str, asset_type: str = 'Blueprint',
                               recursive: bool = True) -> Dict[str, Any]:
    """
    Assets of one exact class ('Blueprint', 'StaticMesh', ...) whose name does not start with prefix.

    Returns:
        {success, data: {assets_without_prefix: [{path, name, class}], count, folder_path, expected_prefix,
         asset_type}, error}
    """
    if not folder_path or not folder_path.strip():
        return make_error_result('folder_path cannot be empty')
    if not prefix:
        return make_error_result('prefix cannot be empty')
    try:
        missing = [{'path': path, 'name': name, 'class': class_name}
                   for path, name, class_name in _folder_assets(folder_path, recursive)
                   if class_name.lower() == asset_type.lower() and not name.startswith(prefix)]
        return make_success_result({'assets_without_prefix': missing, 'count': len(missing),
                                    'folder_path': folder_path.strip(), 'expected_prefix': prefix,
                                    'asset_type': asset_type})
    except Exception as e:
        return make_error_result(f'find_assets_without_prefix failed: {e}')


def logic_node_count(blueprint_path: str) -> Dict[str, Any]:
    """
    The nodes of a Blueprint that hold logic: every node of its N2C JSON except function entry and result nodes and
    the editor's ghost default events ("disabled": true).

    Returns:
        {success, data: {blueprint_path, logic_nodes, graphs: {graph name: logic nodes}}, error}
    """
    read = n2c.get_blueprint_n2cjson(blueprint_path)
    if not read.get('success'):
        return make_error_result(read.get('error') or f'could not read {blueprint_path}')
    graphs = {}
    for graph in ((read.get('data') or {}).get('n2c_json') or {}).get('graphs') or []:
        graphs[graph.get('name', '')] = sum(1 for node in graph.get('nodes') or []
                                            if node.get('type') not in STRUCTURAL_NODE_TYPES and not node.get('disabled'))
    return make_success_result({'blueprint_path': blueprint_path, 'logic_nodes': sum(graphs.values()),
                                'graphs': graphs})


def validate_blueprint(blueprint_path: str, compile_check: bool = False, naming_check: bool = True) -> Dict[str, Any]:
    """
    One Blueprint: its name prefix and, when asked, whether it compiles.

    Args:
        blueprint_path: Asset path ('/Game/BP_Door' or '/Game/BP_Door.BP_Door')
        compile_check: Compile it (compile-blueprint) and report the compiler's errors as issues
        naming_check: Warn when the name lacks the prefix of its class

    Returns:
        {success, data: {is_valid, blueprint_path, asset_name, class, issues, issue_count, warnings, warning_count,
         compile_checked, naming_checked, compile: {status, error_count, warning_count} when compiled}, error}.
        is_valid is False when the Blueprint does not compile.
    """
    if not blueprint_path or not blueprint_path.strip():
        return make_error_result('blueprint_path cannot be empty')
    path = blueprint_path.strip()
    try:
        asset_data = unreal.EditorAssetLibrary.find_asset_data(path)
        if not asset_data or not asset_data.is_valid():
            return make_error_result(f'ASSET_NOT_FOUND: no asset at {path}')
        name = str(asset_data.asset_name)
        class_name = str(asset_data.asset_class_path.asset_name)
        issues, warnings, compiled = [], [], None
        if naming_check:
            prefix = expected_prefix(class_name)
            ok = prefix is None or name.startswith(prefix) or (class_name == 'Blueprint' and name.startswith(BLUEPRINT_PREFIXES))
            if not ok:
                warnings.append(f"{class_name} '{name}' should start with '{prefix}'")
        if compile_check:
            result = n2c.compile_blueprint(path)
            data = result.get('data') or {}
            counts = data.get('results') or {}
            compiled = {'status': (data.get('compilationStatus') or {}).get('currentStatus', ''),
                        'error_count': counts.get('errorCount', 0), 'warning_count': counts.get('warningCount', 0)}
            for message in counts.get('messages') or []:
                if message.get('severity') == 'Error':
                    issues.append(f"Compile error: {message.get('message', '')}")
            if not result.get('success') and not issues:
                issues.append(f"Compile failed: {result.get('error')}")
        out = {'is_valid': not issues, 'blueprint_path': path, 'asset_name': name, 'class': class_name,
               'issues': issues, 'issue_count': len(issues), 'warnings': warnings, 'warning_count': len(warnings),
               'compile_checked': compile_check, 'naming_checked': naming_check}
        if compiled is not None:
            out['compile'] = compiled
        return make_success_result(out)
    except Exception as e:
        return make_error_result(f'validate_blueprint failed: {e}')


def validate_blueprints_in_folder(folder_path: str, recursive: bool = True, compile_check: bool = False,
                                  naming_check: bool = True) -> Dict[str, Any]:
    """
    validate_blueprint for every Blueprint (any Blueprint class: Actor, Widget, Anim, ...) in a folder.

    Returns:
        {success, data: {total, valid, invalid, issues_by_asset: {path: {issues, warnings}}, folder_path, recursive},
         error}
    """
    if not folder_path or not folder_path.strip():
        return make_error_result('folder_path cannot be empty')
    try:
        valid, invalid, by_asset = 0, 0, {}
        for path, _name, class_name in _folder_assets(folder_path, recursive):
            if not class_name.endswith('Blueprint'):
                continue
            result = validate_blueprint(path, compile_check, naming_check)
            data = result.get('data') or {}
            if result.get('success') and data.get('is_valid'):
                valid += 1
                if data.get('warnings'):
                    by_asset[path] = {'issues': [], 'warnings': data['warnings']}
            else:
                invalid += 1
                by_asset[path] = {'issues': data.get('issues') or [result.get('error')],
                                  'warnings': data.get('warnings') or []}
        return make_success_result({'total': valid + invalid, 'valid': valid, 'invalid': invalid,
                                    'issues_by_asset': by_asset, 'folder_path': folder_path.strip(),
                                    'recursive': recursive})
    except Exception as e:
        return make_error_result(f'validate_blueprints_in_folder failed: {e}')


def check_for_empty_blueprints(folder_path: str, recursive: bool = True) -> Dict[str, Any]:
    """
    Blueprints with no logic node in any graph (logic_node_count is 0): only function entries and the editor's
    ghost default events. A Blueprint that cannot be read is listed under unreadable, not as empty.

    Returns:
        {success, data: {empty_blueprints: [{path, name}], count, checked, unreadable: [{path, error}],
         folder_path}, error}
    """
    if not folder_path or not folder_path.strip():
        return make_error_result('folder_path cannot be empty')
    try:
        empty: List[Dict[str, Any]] = []
        unreadable: List[Dict[str, Any]] = []
        checked = 0
        for path, name, class_name in _folder_assets(folder_path, recursive):
            if not class_name.endswith('Blueprint'):
                continue
            checked += 1
            counted = logic_node_count(path)
            if not counted.get('success'):
                log_warning(f'check_for_empty_blueprints: {path}: {counted.get("error")}')
                unreadable.append({'path': path, 'error': counted.get('error')})
            elif counted['data']['logic_nodes'] == 0:
                empty.append({'path': path, 'name': name})
        return make_success_result({'empty_blueprints': empty, 'count': len(empty), 'checked': checked,
                                    'unreadable': unreadable, 'folder_path': folder_path.strip()})
    except Exception as e:
        return make_error_result(f'check_for_empty_blueprints failed: {e}')
