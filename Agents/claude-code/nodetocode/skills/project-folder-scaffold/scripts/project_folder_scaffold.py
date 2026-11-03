# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
Create a standard Content Browser folder layout from the editor's Python (the project-folder-scaffold Agent Skill,
decision 0010).

Load it through run-python:

    import nodetocode as n2c
    scaffold = n2c.skill_script('project-folder-scaffold')
    scaffold.create_from_template('/Game/Features/Inventory', 'feature_standard')

Every function returns {success: bool, data: {...} or None, error: str or None}. It only creates folders, under
/Game or a plugin of the project; it never deletes one (the 1.x delete_folder is gone) and never moves an asset.
Ported from the 1.x script library's project_folder_scaffold: the templates its docstring listed now exist.
"""

from typing import Any, Dict, List

import unreal

from nodetocode.utils import make_error_result, make_success_result

TEMPLATES = {
    'feature_standard': ['Blueprints', 'Data', 'UI', 'Materials', 'Audio'],
    'character': ['Blueprints', 'Meshes', 'Animations', 'Materials', 'Audio'],
    'level': ['Blueprints', 'Geometry', 'Lighting', 'Audio', 'Data'],
    'ui_module': ['Widgets', 'Styles', 'Icons', 'Data'],
    'gameplay_system': ['Blueprints', 'Data', 'Config', 'Tests'],
    'ai': ['Blueprints', 'BehaviorTrees', 'Blackboards', 'Data', 'Debug'],
    'weapon': ['Blueprints', 'Meshes', 'Materials', 'Audio', 'VFX'],
    'environment': ['Props', 'Foliage', 'Materials', 'Decals', 'Lighting'],
}

# Roots no folder is created under: engine content and script packages
REFUSED_ROOTS = ('/Engine', '/Script', '/Temp', '/Memory')


def get_available_templates() -> Dict[str, List[str]]:
    """{template name: its subfolders}"""
    return {name: list(folders) for name, folders in TEMPLATES.items()}


def _check_path(path: str) -> str:
    """The normalised content path, or raises ValueError naming what is wrong with it"""
    clean = (path or '').strip().replace('\\', '/').rstrip('/')
    if not clean.startswith('/') or clean.count('/') < 2:
        raise ValueError(f"'{path}' is not a content folder such as /Game/Features/Combat")
    if '..' in clean.split('/'):
        raise ValueError(f"'{path}' may not contain '..'")
    if clean.split('/')[1] in {r.strip('/') for r in REFUSED_ROOTS}:
        raise ValueError(f"'{path}' is under {clean.split('/')[1]}: create folders under /Game or a project plugin")
    return clean


def folder_exists(path: str) -> bool:
    """Whether the Content Browser folder exists"""
    try:
        return bool(unreal.EditorAssetLibrary.does_directory_exist(_check_path(path)))
    except Exception:
        return False


def create_folder(path: str) -> Dict[str, Any]:
    """
    One folder.

    Returns:
        {success, data: {path, created, already_existed}, error}
    """
    try:
        clean = _check_path(path)
        if unreal.EditorAssetLibrary.does_directory_exist(clean):
            return make_success_result({'path': clean, 'created': False, 'already_existed': True})
        if not unreal.EditorAssetLibrary.make_directory(clean):
            return make_error_result(f'could not create {clean}')
        return make_success_result({'path': clean, 'created': True, 'already_existed': False})
    except Exception as e:
        return make_error_result(f'create_folder failed: {e}')


def create_folders(paths: List[str]) -> Dict[str, Any]:
    """
    Several folders, each on its own: one that fails does not stop the others.

    Returns:
        {success, data: {created, created_count, already_existed, already_existed_count, failed: [{path, error}],
         failed_count, total_requested}, error}; success is False when any folder failed.
    """
    if not paths:
        return make_error_result('paths cannot be empty')
    created, existed, failed = [], [], []
    for path in paths:
        result = create_folder(path)
        if not result['success']:
            failed.append({'path': path, 'error': result['error']})
        elif result['data']['created']:
            created.append(result['data']['path'])
        else:
            existed.append(result['data']['path'])
    data = {'created': created, 'created_count': len(created), 'already_existed': existed,
            'already_existed_count': len(existed), 'failed': failed, 'failed_count': len(failed),
            'total_requested': len(paths)}
    if failed:
        return make_error_result(f'{len(failed)} of {len(paths)} folders were not created', data)
    return make_success_result(data)


def create_feature_folders(base_path: str, subfolders: List[str]) -> Dict[str, Any]:
    """
    A folder and the given subfolders in it.

    Returns:
        create_folders' result, with base_path and subfolders added to data
    """
    try:
        base = _check_path(base_path)
    except ValueError as e:
        return make_error_result(str(e))
    names = [s.strip().replace('\\', '/').strip('/') for s in subfolders or []]
    names = [n for n in names if n]
    if not names:
        return make_error_result('subfolders cannot be empty')
    result = create_folders([base] + [f'{base}/{name}' for name in names])
    if result.get('data') is not None:
        result['data'].update({'base_path': base, 'subfolders': names})
    return result


def create_from_template(base_path: str, template_name: str) -> Dict[str, Any]:
    """
    A folder with one template's subfolders (get_available_templates lists them; the name is case-insensitive,
    '-' and ' ' read as '_').

    Returns:
        create_feature_folders' result, with template_used added to data
    """
    key = (template_name or '').strip().lower().replace(' ', '_').replace('-', '_')
    if key not in TEMPLATES:
        return make_error_result(f"unknown template '{template_name}'; templates: {', '.join(sorted(TEMPLATES))}")
    result = create_feature_folders(base_path, TEMPLATES[key])
    if result.get('data') is not None:
        result['data']['template_used'] = key
    return result
