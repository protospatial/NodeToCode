# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
The in-editor half of the Agent Skill scripts' tests (card 0306): NodeToCode.Python.SkillScripts
(Source/Private/Tests/MCP/N2CPythonSkillScripts.spec.cpp) runs this file with N2C_CTX set and reads the report it writes
to N2C_CTX["out"]: {"checks": [{"check", "ok", "detail"}], "problem"}.

It makes real assets in a scratch folder under /Game (N2C_CTX["folder"]) and a sibling folder named like it plus
"Old", loads each skill's script with nodetocode.skill_script and checks the review findings fixed in the port on
them: asset-iterator's folder filter and processed count (python-bridge-34, -39), asset-validator's naming table, its
compile check and its empty-Blueprint check (python-bridge-32, -31, -33), and project-folder-scaffold. Both folders
are deleted at the end, whatever happened.
"""

import json
import traceback

import unreal

import nodetocode as n2c


def run(ctx, check):
    folder = ctx['folder']
    sibling = folder + 'Old'
    tools = unreal.AssetToolsHelpers.get_asset_tools()

    def blueprint(name, where=folder):
        factory = unreal.BlueprintFactory()
        factory.set_editor_property('parent_class', unreal.Actor)
        return tools.create_asset(name, where, unreal.Blueprint, factory)

    bp_empty = blueprint('BP_N2CEmpty')
    bp_logic = blueprint('BP_N2CLogic')
    bp_broken = blueprint('BP_N2CBroken')
    blueprint('BP_N2CSibling', sibling)
    blueprint('BP_N2CNested', folder + '/Sub')
    tools.create_asset('WBP_N2CMenu', folder, unreal.WidgetBlueprint, unreal.WidgetBlueprintFactory())
    tools.create_asset('N2CBadMenu', folder, unreal.WidgetBlueprint, unreal.WidgetBlueprintFactory())
    tools.create_asset('MI_N2CRock', folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    check('the scratch Blueprints exist', all([bp_empty, bp_logic, bp_broken]), '')

    # A Print String in a function of BP_N2CLogic; an unwired-target Set Static Mesh after BP_N2CBroken's entry, which
    # does not compile (an Actor Blueprint is not a StaticMeshComponent)
    for bp, name, search in ((bp_logic, 'BP_N2CLogic', 'Print String'), (bp_broken, 'BP_N2CBroken', 'Set Static Mesh')):
        unreal.BlueprintEditorLibrary.add_function_graph(bp, 'N2CFn')
        target = {'blueprint_path': f'{folder}/{name}', 'graph': 'N2CFn'}
        found = n2c.search_blueprint_nodes(search, max_results=10, **target)
        meta = next((node['spawnMetadata'] for node in (found.get('data') or {}).get('nodes') or []
                     if node['spawnMetadata'].get('nodeName') == search), None)
        added = n2c.add_node_to_graph(meta['nodeName'], meta['actionIdentifier'], 300, 0, **target) if meta else {}
        check(f'{name}: {search} added', bool(added.get('success')), str(added.get('error')))
        if name == 'BP_N2CBroken' and added.get('success'):
            entry = n2c.find_nodes_in_graph(['N2CFn'], **target)['data']['nodes'][0]
            then = next(p['ids']['guid'] for p in entry['output_pins'] if p['ids']['name'] == 'then')
            execute = next(p['pinGuid'] for p in added['data']['inputPins'] if p['pinName'] == 'execute')
            connected = n2c.connect_pins([{'from': {'nodeGuid': entry['ids']['guid'], 'pinGuid': then},
                                           'to': {'nodeGuid': added['data']['nodeGuid'], 'pinGuid': execute}}], **target)
            check('BP_N2CBroken: Set Static Mesh wired after the entry', bool(connected.get('success')),
                  str(connected.get('error')))

    # ---- asset-iterator ----
    iterator = n2c.skill_script('asset-iterator')
    flat = iterator.find_assets_by_type('Blueprint', folder, recursive=False, include_subclasses=False)
    names = sorted(a['name'] for a in (flat.get('data') or {}).get('assets') or [])
    check('find_assets_by_type, not recursive: the folder\'s own Blueprints (python-bridge-34)',
          names == ['BP_N2CBroken', 'BP_N2CEmpty', 'BP_N2CLogic'], str(names))
    deep = iterator.find_assets_by_type('Blueprint', folder, recursive=True, include_subclasses=False)
    names = sorted(a['name'] for a in (deep.get('data') or {}).get('assets') or [])
    check('find_assets_by_type, recursive: the subfolder, not the sibling folder named like it (python-bridge-34)',
          names == ['BP_N2CBroken', 'BP_N2CEmpty', 'BP_N2CLogic', 'BP_N2CNested'], str(names))
    entries = [{'path': f'{folder}/BP_N2CEmpty.BP_N2CEmpty'}, {'path': f'{folder}/BP_N2CMissing.BP_N2CMissing'},
               {'path': f'{folder}/BP_N2CLogic.BP_N2CLogic'}]
    walked = iterator.iterate_assets(entries, lambda asset: asset.get_name()).get('data') or {}
    check('iterate_assets with one missing asset: processed 2, failed 1 (python-bridge-39)',
          (walked.get('processed'), walked.get('failed'), len(walked.get('results') or [])) == (2, 1, 3),
          json.dumps(walked)[:300])
    parents = iterator.get_blueprints_by_parent('Actor', folder)
    names = sorted(b['name'] for b in (parents.get('data') or {}).get('blueprints') or [])
    check('get_blueprints_by_parent(Actor) finds the Actor Blueprints', names[:3] == ['BP_N2CBroken', 'BP_N2CEmpty', 'BP_N2CLogic'],
          str(names))

    # ---- asset-validator ----
    validator = n2c.skill_script('asset-validator')
    naming = validator.check_naming(folder, recursive=False).get('data') or {}
    flagged = sorted(i['name'] for i in naming.get('issues') or [])
    check('check_naming accepts WBP_, MI_ and BP_ and flags a Widget Blueprint without WBP_ (python-bridge-32)',
          flagged == ['N2CBadMenu'] and naming.get('valid') == 5, json.dumps(naming)[:400])
    broken = validator.validate_blueprint(f'{folder}/BP_N2CBroken', compile_check=True).get('data') or {}
    check('validate_blueprint with compile_check reports the compile error (python-bridge-31)',
          broken.get('is_valid') is False and (broken.get('compile') or {}).get('error_count', 0) >= 1,
          json.dumps(broken)[:400])
    clean = validator.validate_blueprint(f'{folder}/BP_N2CLogic', compile_check=True).get('data') or {}
    check('validate_blueprint with compile_check: a Blueprint that compiles is valid', clean.get('is_valid') is True,
          json.dumps(clean)[:400])
    empty = validator.check_for_empty_blueprints(folder, recursive=False).get('data') or {}
    names = sorted(b['name'] for b in empty.get('empty_blueprints') or [])
    # The two new Widget Blueprints have no logic either
    check('check_for_empty_blueprints lists only the Blueprints with no logic (python-bridge-33)',
          names == ['BP_N2CEmpty', 'N2CBadMenu', 'WBP_N2CMenu'] and empty.get('checked') == 5
          and not empty.get('unreadable'), json.dumps(empty)[:400])

    # ---- project-folder-scaffold ----
    scaffold = n2c.skill_script('project-folder-scaffold')
    made = scaffold.create_from_template(folder + '/Feature', 'character')
    check('create_from_template makes the template\'s folders',
          made.get('success') and all(unreal.EditorAssetLibrary.does_directory_exist(f'{folder}/Feature/{name}')
                                      for name in ('Blueprints', 'Meshes', 'Animations', 'Materials', 'Audio')),
          json.dumps(made)[:300])
    again = scaffold.create_folder(folder + '/Feature/Audio').get('data') or {}
    check('create_folder reports an existing folder', again.get('already_existed') is True, json.dumps(again))
    refused = scaffold.create_folder('/Engine/N2CNever')
    check('create_folder refuses /Engine', refused.get('success') is False
          and not unreal.EditorAssetLibrary.does_directory_exist('/Engine/N2CNever'), str(refused.get('error')))


def main(ctx_text):
    ctx = json.loads(ctx_text)
    report = {'checks': [], 'problem': ''}

    def check(name, ok, detail):
        report['checks'].append({'check': name, 'ok': bool(ok), 'detail': detail if not ok else ''})
    try:
        run(ctx, check)
    except Exception as error:  # the spec reports it
        report['problem'] = f'{type(error).__name__}: {error}\n{traceback.format_exc()}'
    finally:
        for folder in (ctx['folder'], ctx['folder'] + 'Old'):
            try:
                if unreal.EditorAssetLibrary.does_directory_exist(folder):
                    unreal.EditorAssetLibrary.delete_directory(folder)
            except Exception as error:
                report['problem'] += f'\ncleanup of {folder}: {error}'
    with open(ctx['out'], 'w', encoding='utf-8') as f:
        json.dump(report, f)


main(N2C_CTX)  # noqa: F821  (set by the spec)
