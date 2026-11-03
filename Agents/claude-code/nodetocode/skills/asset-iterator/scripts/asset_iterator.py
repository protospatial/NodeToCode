# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
Find and walk a project's assets from the editor's Python (the asset-iterator Agent Skill, decision 0010).

Read-only: nothing here changes an asset. Load it through run-python:

    import nodetocode as n2c
    assets = n2c.skill_script('asset-iterator').find_assets_by_type('Blueprint', '/Game/Characters')

Every function returns {success: bool, data: {...} or None, error: str or None}. An asset entry is
{name, path (object path), class_name, package_name (/Game/A/BP_A), package_path (its folder, /Game/A)}.

Ported from the 1.x script library's asset_iterator with python-bridge-34 (package_path held the package name, so a
non-recursive search found nothing and a recursive one matched sibling folders) and python-bridge-39 (iterate_assets
counted processed as results minus failures, while a load failure never entered results) fixed.
"""

import fnmatch
from typing import Any, Callable, Dict, List, Optional

import unreal

from nodetocode.utils import log_warning, make_error_result, make_success_result

# Simple class names to their class paths; anything else is looked up as /Script/Engine.<Name>
CLASS_PATHS = {
    'blueprint': '/Script/Engine.Blueprint',
    'actor': '/Script/Engine.Actor',
    'pawn': '/Script/Engine.Pawn',
    'character': '/Script/Engine.Character',
    'staticmesh': '/Script/Engine.StaticMesh',
    'skeletalmesh': '/Script/Engine.SkeletalMesh',
    'material': '/Script/Engine.Material',
    'materialinstance': '/Script/Engine.MaterialInstanceConstant',
    'materialinstanceconstant': '/Script/Engine.MaterialInstanceConstant',
    'texture': '/Script/Engine.Texture2D',
    'texture2d': '/Script/Engine.Texture2D',
    'soundcue': '/Script/Engine.SoundCue',
    'soundwave': '/Script/Engine.SoundWave',
    'particlesystem': '/Script/Engine.ParticleSystem',
    'niagarasystem': '/Script/Niagara.NiagaraSystem',
    'datatable': '/Script/Engine.DataTable',
    'dataasset': '/Script/Engine.DataAsset',
    'level': '/Script/Engine.World',
    'world': '/Script/Engine.World',
    'widgetblueprint': '/Script/UMGEditor.WidgetBlueprint',
    'userwidget': '/Script/UMG.UserWidget',
    'animblueprint': '/Script/Engine.AnimBlueprint',
    'animsequence': '/Script/Engine.AnimSequence',
    'animmontage': '/Script/Engine.AnimMontage',
    'blendspace': '/Script/Engine.BlendSpace',
    'skeleton': '/Script/Engine.Skeleton',
    'userdefinedstruct': '/Script/Engine.UserDefinedStruct',
    'userdefinedenum': '/Script/Engine.UserDefinedEnum',
    'editorutilitywidget': '/Script/Blutility.EditorUtilityWidgetBlueprint',
    'editorutilityblueprint': '/Script/Blutility.EditorUtilityBlueprint',
}


def resolve_class_path(class_name: str) -> str:
    """'Blueprint' -> '/Script/Engine.Blueprint'; a path ('/Script/UMG.UserWidget') is returned as it is"""
    name = (class_name or '').strip()
    if name.startswith('/'):
        return name
    return CLASS_PATHS.get(name.lower().replace(' ', ''), f'/Script/Engine.{name}')


def folder_of(package_name: str) -> str:
    """'/Game/A/BP_A' -> '/Game/A'"""
    return package_name.rsplit('/', 1)[0] if '/' in package_name.strip('/') else package_name


def in_folder(package_path: str, folder: str, recursive: bool) -> bool:
    """Whether an asset whose folder is package_path lies in folder: that folder only, or with recursive its
    subfolders too. '/Game/Char' does not contain '/Game/Characters'."""
    folder = folder.rstrip('/') or '/'
    if package_path == folder:
        return True
    return recursive and package_path.startswith(folder + '/')


def asset_entry(asset_data: 'unreal.AssetData') -> Dict[str, Any]:
    """An AssetData as a plain dict"""
    package_name = str(asset_data.package_name)
    return {
        'name': str(asset_data.asset_name),
        'path': f'{package_name}.{asset_data.asset_name}',
        'class_name': str(asset_data.asset_class_path.asset_name),
        'package_name': package_name,
        'package_path': str(asset_data.package_path) or folder_of(package_name),
    }


def find_assets_by_path(package_path: str, recursive: bool = False, include_only_on_disk: bool = False,
                        name_pattern: Optional[str] = None) -> Dict[str, Any]:
    """
    Every asset in a folder.

    Args:
        package_path: Content folder ('/Game/Blueprints')
        recursive: Also its subfolders
        include_only_on_disk: Only saved assets
        name_pattern: Optional glob on the asset name, case-insensitive ('*Enemy*')

    Returns:
        {success, data: {assets: [entry, ...], count, path, recursive}, error}
    """
    if not package_path or not package_path.strip():
        return make_error_result('package_path cannot be empty')
    folder = package_path.strip().rstrip('/')
    try:
        registry = unreal.AssetRegistryHelpers.get_asset_registry()
        found = registry.get_assets_by_path(folder, recursive, include_only_on_disk) or []
        assets = []
        for asset_data in found:
            if not asset_data.is_valid():
                continue
            entry = asset_entry(asset_data)
            if name_pattern and not fnmatch.fnmatch(entry['name'].lower(), name_pattern.lower()):
                continue
            assets.append(entry)
        return make_success_result({'assets': assets, 'count': len(assets), 'path': folder, 'recursive': recursive})
    except Exception as e:
        return make_error_result(f'find_assets_by_path failed: {e}')


def find_assets_by_type(class_name: str, search_path: Optional[str] = None, recursive: bool = True,
                        include_subclasses: bool = True) -> Dict[str, Any]:
    """
    Assets of one class, optionally in one folder.

    Args:
        class_name: 'Blueprint', 'StaticMesh', 'WidgetBlueprint', ... or a class path ('/Script/Engine.Texture2D')
        search_path: Optional content folder; without it the whole registry
        recursive: With search_path, also its subfolders
        include_subclasses: Also classes derived from it

    Returns:
        {success, data: {assets: [entry, ...], count, class_name, class_path, search_path}, error}
    """
    if not class_name or not class_name.strip():
        return make_error_result('class_name cannot be empty')
    class_path = resolve_class_path(class_name)
    folder = search_path.strip().rstrip('/') if search_path and search_path.strip() else None
    try:
        registry = unreal.AssetRegistryHelpers.get_asset_registry()
        found = registry.get_assets_by_class(unreal.TopLevelAssetPath(class_path), include_subclasses) or []
        assets = []
        for asset_data in found:
            if not asset_data.is_valid():
                continue
            entry = asset_entry(asset_data)
            if folder and not in_folder(entry['package_path'], folder, recursive):
                continue
            assets.append(entry)
        return make_success_result({'assets': assets, 'count': len(assets), 'class_name': class_name,
                                    'class_path': class_path, 'search_path': folder})
    except Exception as e:
        return make_error_result(f'find_assets_by_type failed: {e}')


def list_assets_simple(directory_path: str, recursive: bool = True, include_folders: bool = False) -> Dict[str, Any]:
    """
    Only the paths of a folder's assets (EditorAssetLibrary.list_assets).

    Returns:
        {success, data: {paths: [...], count, directory, recursive}, error}
    """
    if not directory_path or not directory_path.strip():
        return make_error_result('directory_path cannot be empty')
    try:
        paths = list(unreal.EditorAssetLibrary.list_assets(directory_path.strip(), recursive, include_folders) or [])
        return make_success_result({'paths': paths, 'count': len(paths), 'directory': directory_path.strip(),
                                    'recursive': recursive})
    except Exception as e:
        return make_error_result(f'list_assets_simple failed: {e}')


def get_asset_info(asset_path: str) -> Dict[str, Any]:
    """
    One asset's entry, plus exists and is_valid.

    Returns:
        {success, data: {name, path, class_name, package_name, package_path, exists, is_valid}, error}
    """
    if not asset_path or not asset_path.strip():
        return make_error_result('asset_path cannot be empty')
    path = asset_path.strip()
    try:
        if not unreal.EditorAssetLibrary.does_asset_exist(path):
            return make_success_result({'name': path.rsplit('/', 1)[-1], 'path': path, 'exists': False,
                                        'is_valid': False})
        asset_data = unreal.EditorAssetLibrary.find_asset_data(path)
        if not asset_data or not asset_data.is_valid():
            return make_success_result({'name': path.rsplit('/', 1)[-1], 'path': path, 'exists': True,
                                        'is_valid': False})
        return make_success_result(dict(asset_entry(asset_data), exists=True, is_valid=True))
    except Exception as e:
        return make_error_result(f'get_asset_info failed: {e}')


def get_blueprints_by_parent(parent_class_path: str, search_path: Optional[str] = None) -> Dict[str, Any]:
    """
    Blueprints whose direct parent class is the given class, from the AssetRegistry's ParentClass tag (no asset is
    loaded).

    Args:
        parent_class_path: '/Script/Engine.Actor', a Blueprint class path, or a simple name ('Actor', 'Character')
        search_path: Optional content folder (with subfolders)

    Returns:
        {success, data: {blueprints: [entry + parent_class, ...], count, parent_class}, error}
    """
    if not parent_class_path or not parent_class_path.strip():
        return make_error_result('parent_class_path cannot be empty')
    wanted = resolve_class_path(parent_class_path)
    found = find_assets_by_type('Blueprint', search_path, recursive=True)
    if not found['success']:
        return found
    matches = []
    for entry in found['data']['assets']:
        try:
            asset_data = unreal.EditorAssetLibrary.find_asset_data(entry['path'])
            parent = str(asset_data.get_tag_value('ParentClass') or '') if asset_data else ''
            # The tag reads like /Script/CoreUObject.Class'/Script/Engine.Actor'
            parent = parent.split("'")[1] if "'" in parent else parent
            if parent == wanted or parent == wanted + '_C' or parent.rsplit('.', 1)[-1] == wanted.rsplit('.', 1)[-1]:
                matches.append(dict(entry, parent_class=parent))
        except Exception as e:
            log_warning(f'get_blueprints_by_parent: skipped {entry.get("path")}: {e}')
    return make_success_result({'blueprints': matches, 'count': len(matches), 'parent_class': wanted})


def iterate_assets(assets: List[Dict[str, Any]], callback: Callable[[Any], Any],
                   load_assets: bool = True) -> Dict[str, Any]:
    """
    Calls callback on each asset (the loaded UObject, or with load_assets False the entry itself).

    Returns:
        {success, data: {results: [{path, result} or {path, error}, ...], processed, failed}, error}: processed
        counts the callbacks that returned, failed the assets that did not load or whose callback raised; every
        asset has one entry in results.
    """
    results, processed, failed = [], 0, 0
    for entry in assets or []:
        path = (entry or {}).get('path', '')
        try:
            target = entry
            if load_assets:
                # Checked first: load_asset logs an error for a path the AssetRegistry does not have
                exists = bool(path) and unreal.EditorAssetLibrary.does_asset_exist(path)
                target = unreal.EditorAssetLibrary.load_asset(path) if exists else None
                if target is None:
                    failed += 1
                    results.append({'path': path, 'error': 'could not be loaded'})
                    continue
            results.append({'path': path, 'result': callback(target)})
            processed += 1
        except Exception as e:
            failed += 1
            results.append({'path': path, 'error': str(e)})
    return make_success_result({'results': results, 'processed': processed, 'failed': failed})


def filter_assets(assets: List[Dict[str, Any]], name_pattern: Optional[str] = None, class_filter: Optional[str] = None,
                  path_prefix: Optional[str] = None) -> Dict[str, Any]:
    """
    Entries that match every given filter: a name glob, a class name (exact, case-insensitive) and a folder (with its
    subfolders).

    Returns:
        {success, data: {assets: [...], count, original_count}, error}
    """
    kept = []
    for entry in assets or []:
        if name_pattern and not fnmatch.fnmatch(entry.get('name', '').lower(), name_pattern.lower()):
            continue
        if class_filter and entry.get('class_name', '').lower() != class_filter.lower():
            continue
        if path_prefix and not in_folder(entry.get('package_path', ''), path_prefix, True):
            continue
        kept.append(entry)
    return make_success_result({'assets': kept, 'count': len(kept), 'original_count': len(assets or [])})
