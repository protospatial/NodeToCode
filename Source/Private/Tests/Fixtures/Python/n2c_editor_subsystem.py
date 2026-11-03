# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
The Python half of NodeToCode.Editor.Subsystem (card 0301, Source/Private/Tests/Editor/N2CEditorSubsystem.spec.cpp),
which runs this file with N2C_CTX set and reads the report it writes to N2C_CTX["out"]:
{"checks": [{"check", "ok", "detail"}], "problem"}.

It makes an Actor Blueprint with a function in a scratch folder under /Game (N2C_CTX["folder"]) and calls each
function of unreal.get_editor_subsystem(unreal.N2CEditorSubsystem) on it: each returns its struct, and a bad path
raises a RuntimeError whose message names the path. It also checks the nodetocode wrappers on top of the subsystem and
that dir(unreal.N2CPythonBridge) still lists the string functions it replaces. The folder is deleted at the end,
whatever happened.
"""

import json
import traceback
import uuid
import warnings

import unreal

import nodetocode as n2c


def run(ctx, check):
    folder = ctx['folder']
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    factory = unreal.BlueprintFactory()
    factory.set_editor_property('parent_class', unreal.Actor)
    bp = tools.create_asset('BP_N2CSubsystem', folder, unreal.Blueprint, factory)
    check('the scratch Blueprint exists', bp is not None, '')
    unreal.BlueprintEditorLibrary.add_function_graph(bp, 'N2CFn')
    path = f'{folder}/BP_N2CSubsystem'

    editor = unreal.get_editor_subsystem(unreal.N2CEditorSubsystem)
    check('get_editor_subsystem(unreal.N2CEditorSubsystem)', editor is not None, '')
    names = ('compile_blueprint', 'save_blueprint', 'list_blueprint_graphs', 'get_blueprint_parent_class',
             'get_blueprint_n2c_json', 'tag_graph', 'list_tags', 'remove_tag')
    check('it exposes every function', all(hasattr(editor, name) for name in names),
          str([name for name in names if not hasattr(editor, name)]))

    compiled = editor.compile_blueprint(path)
    check('compile_blueprint returns N2CCompileResult', isinstance(compiled, unreal.N2CCompileResult)
          and compiled.status == 'UpToDate' and compiled.error_count == 0, f'{compiled}')

    saved = editor.save_blueprint(path, True)
    check('save_blueprint writes the new package', isinstance(saved, unreal.N2CSaveResult) and saved.was_dirty
          and saved.was_saved and saved.file_name.endswith('.uasset'), f'{saved}')
    again = editor.save_blueprint(path, True)
    check('and does not write it again when only_if_dirty finds nothing to save',
          not again.was_dirty and not again.was_saved, f'{again}')

    graphs = editor.list_blueprint_graphs(path)
    fn = next((g for g in graphs.graphs if g.name == 'N2CFn'), None)
    check('list_blueprint_graphs lists the function', fn is not None and fn.kind == 'function' and fn.node_count >= 1,
          f'{[(g.name, g.kind) for g in graphs.graphs]}')

    parent = editor.get_blueprint_parent_class(path)
    check('get_blueprint_parent_class is Actor', parent.parent_class == unreal.Actor.static_class()
          and parent.parent_class_path == '/Script/Engine.Actor', f'{parent.parent_class_path}')

    read = editor.get_blueprint_n2c_json(path, 'N2CFn', 0, False, True)
    document = json.loads(read.n2c_json) if read.n2c_json else {}
    check('get_blueprint_n2c_json reads one graph', read.graph_count == 1 and read.graph_name == 'N2CFn'
          and isinstance(document.get('graphs'), list), f'{read.graph_count} {read.graph_name}')

    tag = 'N2CSubsystemPy' + uuid.uuid4().hex
    tagged = editor.tag_graph(path, 'N2CFn', tag, 'N2CSpec', 'card 0301')
    listed = editor.list_tags('N2CSpec', tag, path)
    removed = editor.remove_tag(path, 'N2CFn', tag)
    check('tag_graph, list_tags and remove_tag round-trip a tag', tagged.graph_name == 'N2CFn' and len(listed) == 1
          and removed.removed_count == 1 and not editor.list_tags('', tag, ''), f'{len(listed)} {removed.removed_count}')

    # Every function raises a RuntimeError that names a bad path
    missing = f'{folder}/BP_N2CMissing'
    calls = (('compile_blueprint', (missing,)), ('save_blueprint', (missing, True)),
             ('list_blueprint_graphs', (missing,)), ('get_blueprint_parent_class', (missing,)),
             ('get_blueprint_n2c_json', (missing, '', -1, False, True)), ('tag_graph', (missing, '', 'N2CNever')),
             ('remove_tag', (missing, 'EventGraph', 'N2CNever')))
    for name, args in calls:
        try:
            getattr(editor, name)(*args)
            check(f'{name} raises for a missing Blueprint', False, 'returned normally')
        except RuntimeError as error:
            check(f'{name} raises for a missing Blueprint, naming its path', missing in str(error), str(error))
    try:
        editor.compile_blueprint('')
        check('an empty path raises', False, 'returned normally')
    except RuntimeError as error:
        check('an empty path raises INVALID_ARGUMENT', 'INVALID_ARGUMENT' in str(error), str(error))

    # The nodetocode wrappers keep their dicts on top of the subsystem
    wrapped = n2c.list_blueprint_graphs(path)
    check('nodetocode.list_blueprint_graphs', wrapped.get('success') and any(
        g['name'] == 'N2CFn' and g['kind'] == 'function' for g in wrapped['data']['graphs']), str(wrapped.get('error')))
    doc = n2c.get_blueprint_n2cjson(path, graph_name='N2CFn')
    check('nodetocode.get_blueprint_n2cjson', doc.get('success') and isinstance(doc['data'].get('n2c_json'), dict)
          and doc['data'].get('target', {}).get('graphName') == 'N2CFn', str(doc.get('error')))
    bad = n2c.list_blueprint_graphs(missing)
    check('nodetocode.list_blueprint_graphs reports a missing Blueprint by its code',
          not bad.get('success') and str(bad.get('error')).startswith('ASSET_NOT_FOUND'), str(bad.get('error')))

    # The bridge functions the subsystem replaces are still there, deprecated
    replaced = ('get_blueprint_n2c_json', 'list_blueprint_graphs', 'compile_focused_blueprint', 'save_focused_blueprint',
                'tag_focused_graph')
    listed_names = dir(unreal.N2CPythonBridge)
    check('dir(unreal.N2CPythonBridge) still lists the replaced functions', all(n in listed_names for n in replaced),
          str([n for n in replaced if n not in listed_names]))
    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter('always')
        old = json.loads(unreal.N2CPythonBridge.list_blueprint_graphs(path))
    check('the deprecated list_blueprint_graphs still works', old.get('success') is True, str(old.get('error')))
    check('and warns that it is deprecated', any(issubclass(w.category, DeprecationWarning) for w in caught),
          str([str(w.message) for w in caught]))


def main():
    ctx = json.loads(N2C_CTX)  # noqa: F821 (set by the spec, as JSON text)
    report = {'checks': [], 'problem': ''}

    def check(name, ok, detail):
        report['checks'].append({'check': name, 'ok': bool(ok), 'detail': '' if ok else str(detail)[:500]})

    try:
        run(ctx, check)
    except Exception:
        report['problem'] = traceback.format_exc()[-1500:]
    finally:
        try:
            assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
            if assets.does_directory_exist(ctx['folder']):
                assets.delete_directory(ctx['folder'])
        except Exception:
            report['problem'] = report['problem'] or traceback.format_exc()[-1500:]
        with open(ctx['out'], 'w', encoding='utf-8') as handle:
            json.dump(report, handle, indent=1)


main()
