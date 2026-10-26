"""Graph-type coverage matrix: capture (and, with --translate, translate) one real graph of each graph type through the
MCP tools on a headless editor, and record what NodeToCode does with it (card 0246, evidence for decision 0247).

Stdlib only; run with `python -I` (set N2C_PORT_OFFSET first when other agents run probes):

    python -I graph_type_matrix_0246.py --engine G:/UE/UE_5.8 \
        --project "D:/n2c-lanes/lane-a/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" --label 5.8 [--translate]

Fixtures are real assets that ship with every engine from 5.6 to 5.8, so the same graphs are measured on each engine,
plus a few scratch assets made in the session:
- Engine plugin content, mounted for the session with -EnablePlugins=MoverExamples,Landmass (nothing is copied or
  saved): the MoverExamples AnimBlueprint ABP_Manny (AnimGraph, two state machines, their states and transition rules,
  its event graph), the Widget Blueprint ControlsDisplay, the Editor Utility Widget EUW_Landmass_Editor (functions,
  macros, collapsed graphs, interface implementations, event graph), the Editor Utility Blueprint EUB_SpawnErosionTab,
  the Control Rig CR_Mannequin_BasicFootIK, and the level L_CharacterMovementBasics (opened, then its Level Blueprint
  read by its object path).
- Engine content: /Engine/Sequencer/DefaultBurnIn (a Widget Blueprint whose text bindings are function graphs),
  /Engine/EngineSky/BP_Sky_Sphere (a construction script with logic), /Engine/EditorBlueprintResources/StandardMacros
  (the engine's macro library), /Engine/EngineMaterials/DefaultMaterial (not a Blueprint).
- Scratch, under /Game/N2CTest/Matrix0246_<time> (deleted at the end, never saved): BPI_Matrix (a Blueprint interface
  with one function) and BP_MatrixNodes (an Actor whose BeginPlay runs Switch on Gameplay Tag, with Get Sequence
  Binding placed beside it: K2 nodes NodeToCode maps only through a base class, or not at all). An Evaluate Chooser
  node is not used: wired with no chooser asset it trips an engine assertion in its compile on 5.6 and 5.7
  (EvaluateChooserNode.cpp, `ResultPin || Chooser...`), which ends the editor.

For every fixture: list-blueprint-graphs (the kind and node count it reports), then get-blueprint-n2cjson at depth 0
(toFile, so size never matters): its graph_type, how many nodes it captured against the graph's own node count, node
types and the classes typed Unknown, nodes flagged unused or unreachable, execution and data links, or the error.
For a few graphs the focused path too: open-blueprint-asset with focus_graph, then get-focused-blueprint. Last, a map
given by its package path to list-blueprint-graphs, and whether the editor still answers.

--translate (needs ANTHROPIC_API_KEY in the repo's .env; loaded into the editor's environment only, never printed)
also runs translate-batch (anthropic, --model, cpp) on one graph per type, one call each, and keeps the code for
review. The captures and translations land in logs/graph_type_matrix_0246_<label>/, the report in
logs/graph_type_matrix_0246_<label>.json. The scratch folder and the export and translation folders it made are deleted
from the host.
"""
import argparse
import collections
import glob
import http.client
import json
import os
import re
import shutil
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from editor_session import EditorSession, free_port  # noqa: E402
from mcp_session import session_headers  # noqa: E402

PLUGINS = 'MoverExamples,Landmass'
ABP = '/MoverExamples/Characters/Mannequins/Animations/ABP_Manny'
EUW = '/Landmass/Landscape/BlueprintBrushes/Widgets/EUW_Landmass_Editor'
EUB = '/Landmass/Landscape/ErosionBrush/BP/Widgets/EUB_SpawnErosionTab'
WBP = '/MoverExamples/Widgets/ControlsDisplay'
BURNIN = '/Engine/Sequencer/DefaultBurnIn'
SKY = '/Engine/EngineSky/BP_Sky_Sphere'
MACROS = '/Engine/EditorBlueprintResources/StandardMacros'
CONTROL_RIG = '/MoverExamples/Characters/Mannequins/Rigs/CR_Mannequin_BasicFootIK'
MAP = '/MoverExamples/Maps/L_CharacterMovementBasics'
LEVEL_BP = MAP + '.L_CharacterMovementBasics:PersistentLevel.L_CharacterMovementBasics'
UNLOADED_MAP = '/MoverExamples/Maps/L_TechValidation'
MATERIAL = '/Engine/EngineMaterials/DefaultMaterial'

# (id, graph type, blueprint path, graph name, pick) where pick chooses among graphs of that name: 'first', 'most'
# (the most nodes) or 'any kind' filters. Scratch paths are filled in at run time.
FIXTURES = [
    ('event_graph', 'Event graph (Actor-like)', EUB, 'EventGraph', 'first'),
    ('function', 'Function', EUW, 'Draw Triangle', 'first'),
    ('macro', 'Macro (in a Blueprint)', EUW, 'Get Cursor World Ray', 'first'),
    ('macro_library', 'Macro library (engine StandardMacros)', MACROS, 'ForLoop', 'first'),
    ('collapsed', 'Collapsed graph', EUW, 'Make Distance Field', 'first'),
    ('construction', 'Construction Script', SKY, 'UserConstructionScript', 'first'),
    ('interface_impl', 'Interface function implemented in a Blueprint', EUW, 'Get Shape Mask', 'first'),
    ('interface_asset', 'Blueprint Interface asset', '{scratch}/BPI_Matrix', 'MatrixQuery', 'first'),
    ('unmapped_k2', 'Event graph with unmapped K2 nodes', '{scratch}/BP_MatrixNodes', 'EventGraph', 'first'),
    ('anim_graph', 'AnimGraph', ABP, 'AnimGraph', 'first'),
    ('state_machine', 'State machine graph', ABP, 'Locomotion', 'most'),
    ('anim_state', 'State (pose graph inside a state machine)', ABP, 'Walk / Run', 'first'),
    ('transition_rule', 'Transition rule', ABP, 'Transition', 'most'),
    ('anim_event_graph', 'AnimBlueprint event graph', ABP, 'EventGraph', 'first'),
    ('widget_event_graph', 'Widget Blueprint event graph', WBP, 'EventGraph', 'first'),
    ('widget_function', 'Widget Blueprint function', WBP, 'GatherInputInfo', 'first'),
    ('widget_binding', 'Widget property binding', BURNIN, 'Get_TopLeft_Text_0', 'first'),
    ('editor_utility_widget', 'Editor Utility Widget event graph', EUW, 'EventGraph', 'first'),
    ('editor_utility_blueprint', 'Editor Utility Blueprint', EUB, 'EventGraph', 'first'),
    ('level_blueprint', 'Level Blueprint event graph', LEVEL_BP, 'EventGraph', 'first'),
    ('control_rig', 'Control Rig graph (RigVM)', CONTROL_RIG, 'Rig Graph', 'first'),
    ('material', 'Material (not a Blueprint)', MATERIAL, None, 'first'),
]

# Graphs opened in a Blueprint editor and read with get-focused-blueprint: (id, path, focus_graph)
FOCUSED = [
    ('focused_anim_graph', ABP, 'AnimGraph'),
    ('focused_state_machine', ABP, 'Locomotion'),
    ('focused_collapsed', EUW, 'Make Distance Field'),
    ('focused_level_blueprint', LEVEL_BP, 'EventGraph'),
]

TRANSLATE = ['event_graph', 'construction', 'unmapped_k2', 'anim_graph', 'anim_state', 'transition_rule',
             'anim_event_graph', 'widget_binding', 'widget_event_graph', 'level_blueprint', 'control_rig',
             'interface_asset']

SETUP_PY = r'''
import json, unreal
folder = "%(folder)s"
bpi = unreal.load_asset(folder + "/BPI_Matrix")
result = {"interface_function": bool(bpi) and unreal.BlueprintEditorLibrary.add_function_graph(bpi, "MatrixQuery") is not None}
if bpi:
    unreal.BlueprintEditorLibrary.compile_blueprint(bpi)
result["map_loaded"] = unreal.EditorLoadingAndSavingUtils.load_map("%(map)s") is not None
level_bp = unreal.find_object(None, "%(level_bp)s")
result["level_blueprint_class"] = level_bp.get_class().get_name() if level_bp else None
'''

CLEANUP_PY = r'''
import unreal
folder = "%(folder)s"
sub = unreal.get_editor_subsystem(unreal.AssetEditorSubsystem)
for name in ("BPI_Matrix", "BP_MatrixNodes"):
    a = unreal.load_asset(folder + "/" + name)
    if a:
        sub.close_all_editors_for_asset(a)
unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
result = {"deleted": unreal.EditorAssetLibrary.delete_directory(folder),
          "exists_after": unreal.EditorAssetLibrary.does_directory_exist(folder)}
'''


class Mcp:
    def __init__(self, port):
        self.port = port
        self.rid = 1000

    def call(self, name, arguments, timeout=180):
        """(text, isError); text is the tool result's text, or the transport error"""
        self.rid += 1
        payload = {'jsonrpc': '2.0', 'id': self.rid, 'method': 'tools/call',
                   'params': {'name': name, 'arguments': arguments}}
        for refresh in (False, True):
            conn = http.client.HTTPConnection('127.0.0.1', self.port, timeout=timeout)
            try:
                conn.request('POST', '/mcp', body=json.dumps(payload).encode('utf-8'),
                             headers=dict(session_headers(self.port, refresh=refresh),
                                          **{'Content-Type': 'application/json'}))
                r = conn.getresponse()
                status, body = r.status, r.read().decode('utf-8', 'replace')
            except (OSError, http.client.HTTPException) as e:
                return 'TRANSPORT: %s' % e, True
            finally:
                conn.close()
            if status != 404:
                break
        try:
            res = json.loads(body).get('result') or {}
        except ValueError:
            return body[:500], True
        return ''.join(c.get('text', '') for c in res.get('content', [])), bool(res.get('isError'))

    def json(self, name, arguments, timeout=180):
        text, err = self.call(name, arguments, timeout)
        try:
            return json.loads(text), err, text
        except ValueError:
            return None, err, text

    def python(self, code):
        body = '\n'.join('    ' + line for line in code.strip('\n').splitlines())
        wrapped = 'def _n2c_0246():\n%s\n    return result\nresult = _n2c_0246()\ndel _n2c_0246\n' % body
        data, err, text = self.json('run-python', {'code': wrapped})
        return ((data or {}).get('data') or {}), text[:600]


def find_action(mcp, path, graph, term, node_class):
    data, err, text = mcp.json('search-blueprint-nodes', {'searchTerm': term, 'maxResults': 20,
                                                          'blueprintPath': path, 'graphName': graph})
    for node in (data or {}).get('nodes') or []:
        meta = node.get('spawnMetadata') or {}
        if node_class in meta.get('actionIdentifier', ''):
            return meta.get('nodeName') or node.get('name'), meta['actionIdentifier']
    return None, None


def add_node(mcp, path, graph, term, node_class, x, y=0):
    name, action = find_action(mcp, path, graph, term, node_class)
    if not action:
        return None
    data, err, text = mcp.json('add-bp-node-to-active-graph', {'nodeName': name, 'actionIdentifier': action,
                                                               'location': {'x': x, 'y': y},
                                                               'blueprintPath': path, 'graphName': graph})
    if err or not data or not data.get('success'):
        return None
    return {'guid': data['nodeGuid'], 'class': node_class,
            'in': {p['pinName']: p['pinGuid'] for p in data.get('inputPins', [])},
            'out': {p['pinName']: p['pinGuid'] for p in data.get('outputPins', [])}}


def setup_scratch(mcp, folder):
    """BPI_Matrix and BP_MatrixNodes; returns what was made"""
    out = {}
    data, err, text = mcp.json('create-blueprint-class', {
        'blueprintName': 'BPI_Matrix', 'addPrefix': False, 'parentClassPath': '/Script/CoreUObject.Interface',
        'assetPath': folder, 'openInEditor': False})
    out['interface_created'] = bool(data and data.get('success'))
    data, err, text = mcp.json('create-blueprint-class', {
        'blueprintName': 'BP_MatrixNodes', 'addPrefix': False, 'parentClassPath': '/Script/Engine.Actor',
        'assetPath': folder, 'openInEditor': False})
    out['actor_created'] = bool(data and data.get('success'))
    path = folder + '/BP_MatrixNodes'
    begin = add_node(mcp, path, 'EventGraph', 'Event BeginPlay', 'K2Node_Event', 0)
    switch = add_node(mcp, path, 'EventGraph', 'Switch on Gameplay Tag', 'GameplayTagsK2Node_SwitchGameplayTag', 300)
    binding = add_node(mcp, path, 'EventGraph', 'Get Sequence Binding', 'K2Node_GetSequenceBinding', 0, 300)
    out['nodes_added'] = {k: bool(v) for k, v in (('begin', begin), ('switch', switch), ('sequence_binding', binding))}
    links = []
    if begin and switch:
        links.append({'from': {'nodeGuid': begin['guid'], 'pinGuid': begin['out'].get('then')},
                      'to': {'nodeGuid': switch['guid'], 'pinGuid': switch['in'].get('execute')}})
    if links:
        data, err, text = mcp.json('connect-pins', {'connections': links, 'blueprintPath': path,
                                                    'graphName': 'EventGraph'})
        out['linked'] = not err
        if err:
            out['link_error'] = text[:300]
    return out


def pick_graph(graphs, name, pick):
    same = [g for g in graphs if g.get('name') == name]
    if not same:
        return None
    if pick == 'most':
        return max(same, key=lambda g: g.get('nodeCount') or 0)
    return same[0]


def safe_name(text):
    return re.sub(r'[^A-Za-z0-9_.-]+', '_', text)[:80]


def load_document(text):
    """The N2C JSON document of a get-blueprint-n2cjson / get-focused-blueprint result, read from its file when spilled"""
    try:
        body = json.loads(text)
    except ValueError:
        return None, None
    if isinstance(body, dict) and 'graphs' not in body and body.get('path') and os.path.isfile(body['path']):
        with open(body['path'], encoding='utf-8') as f:
            return json.load(f), body['path']
    if isinstance(body, dict) and 'n2c_json' in body:
        return body['n2c_json'], None
    return body, None


def summarize(doc, listed=None):
    graphs = []
    for g in (doc or {}).get('graphs') or []:
        nodes = g.get('nodes') or []
        flows = g.get('flows') or {}
        types = collections.Counter(n.get('type') for n in nodes)
        unknown = collections.Counter(n.get('class') for n in nodes if n.get('type') == 'Unknown')
        classes = collections.Counter(n.get('class') for n in nodes)
        graphs.append({
            'name': g.get('name'), 'graph_type': g.get('graph_type'), 'nodes': len(nodes),
            'unused': sum(1 for n in nodes if n.get('unused')),
            'unreachable': sum(1 for n in nodes if n.get('unreachable')),
            'exec_links': len(flows.get('execution') or []),
            'data_links': len(flows.get('data') or {}),
            'comments': len(g.get('comments') or []),
            'types': dict(types.most_common()), 'unknown_classes': dict(unknown.most_common()),
            'classes': dict(classes.most_common(12)),
        })
    out = {'graphs': graphs, 'metadata': (doc or {}).get('metadata')}
    if listed is not None and graphs:
        out['listed_node_count'] = listed
        out['captured_node_count'] = graphs[0]['nodes']
    return out


def capture_fixtures(mcp, scratch, out_dir):
    results = {}
    lists = {}
    for fid, label, path, graph, pick in FIXTURES:
        path = path.replace('{scratch}', scratch)
        entry = {'label': label, 'path': path, 'graph': graph}
        if path not in lists:
            data, err, text = mcp.json('list-blueprint-graphs', {'blueprintPath': path})
            lists[path] = (data, err, text)
        data, err, text = lists[path]
        if err or not data:
            entry['list_error'] = text[:300]
        else:
            entry['blueprint_graph_kinds'] = dict(collections.Counter(g.get('kind') for g in data.get('graphs') or []))
            entry['blueprint_graph_count'] = data.get('graphCount')
        args = {'blueprintPath': path, 'depth': 0, 'toFile': True}
        target = pick_graph((data or {}).get('graphs') or [], graph, pick) if graph else None
        if target:
            entry['listed'] = {k: target.get(k) for k in ('name', 'kind', 'nodeCount', 'graphGuid', 'parentGraph')}
            args['graphGuid'] = target['graphGuid']
        elif graph:
            entry['listed'] = None
            args['graphName'] = graph
        text, err = mcp.call('get-blueprint-n2cjson', args)
        if err:
            entry['capture_error'] = text[:300]
        else:
            doc, spilled = load_document(text)
            fn = os.path.join(out_dir, 'capture_%s.json' % fid)
            with open(fn, 'w', encoding='utf-8') as f:
                json.dump(doc, f, indent=1)
            if spilled:
                os.remove(spilled)
            entry['capture'] = summarize(doc, (target or {}).get('nodeCount'))
            entry['capture_file'] = os.path.basename(fn)
        results[fid] = entry
        print('%-26s %s' % (fid, entry.get('capture_error') or entry.get('list_error') or
                            json.dumps({k: entry['capture']['graphs'][0][k] for k in ('graph_type', 'nodes', 'unused')}
                                       if entry.get('capture', {}).get('graphs') else 'no graph')), flush=True)
    return results


def capture_focused(mcp, out_dir):
    results = {}
    for fid, path, focus in FOCUSED:
        entry = {'path': path, 'focus_graph': focus}
        data, err, text = mcp.json('open-blueprint-asset', {'asset_path': path, 'focus_graph': focus,
                                                            'bring_to_front': False})
        entry['open'] = text[:400] if err or not data else {k: data.get(k) for k in ('success', 'focused_graph')}
        text, err = mcp.call('get-focused-blueprint', {})
        if err:
            entry['capture_error'] = text[:300]
        else:
            doc, spilled = load_document(text)
            with open(os.path.join(out_dir, '%s.json' % fid), 'w', encoding='utf-8') as f:
                json.dump(doc, f, indent=1)
            if spilled:
                os.remove(spilled)
            entry['capture'] = summarize(doc)
        results[fid] = entry
        print('%-26s %s' % (fid, json.dumps(entry.get('open'))[:160]), flush=True)
    return results


def translate(mcp, fixtures, out_dir, model):
    results = {}
    for fid in TRANSLATE:
        fx = fixtures.get(fid) or {}
        listed = fx.get('listed') or {}
        if not listed.get('graphGuid'):
            results[fid] = {'skipped': 'no graph listed'}
            continue
        t0 = time.time()
        data, err, text = mcp.json('translate-batch', {
            'targets': [{'blueprintPath': fx['path'], 'graphGuid': listed['graphGuid']}],
            'provider': 'anthropic', 'model': model, 'language': 'cpp'}, timeout=900)
        entry = {'seconds': round(time.time() - t0, 1)}
        if err or not data:
            entry['error'] = text[:400]
        else:
            items = data.get('items') or []
            item = items[0] if items else {}
            entry.update({k: item.get(k) for k in ('status', 'error', 'reason', 'outputDirectory', 'usage')})
            entry['summary'] = {k: data.get(k) for k in ('succeeded', 'failed', 'skipped', 'failures', 'skippedGraphs')}
            entry['batchOutputPath'] = data.get('batchOutputPath')
            with open(os.path.join(out_dir, 'translation_%s.json' % fid), 'w', encoding='utf-8') as f:
                json.dump(data, f, indent=1)
            folder = item.get('outputDirectory')
            files = {}
            if folder and os.path.isdir(folder):
                for p in glob.glob(os.path.join(folder, '**', '*'), recursive=True):
                    if os.path.isfile(p):
                        dst = os.path.join(out_dir, 'translation_%s__%s' % (fid, safe_name(os.path.basename(p))))
                        shutil.copy2(p, dst)
                        files[os.path.basename(p)] = os.path.getsize(p)
            entry['files'] = files
            if not files:
                entry['raw'] = json.dumps(item)[:1500]
        results[fid] = entry
        print('translate %-20s %s' % (fid, json.dumps({k: entry.get(k) for k in ('status', 'error', 'seconds')})),
              flush=True)
    return results


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--label', required=True)
    ap.add_argument('--translate', action='store_true')
    ap.add_argument('--model', default='claude-sonnet-5-5')
    args = ap.parse_args()

    log_dir = os.path.join(HERE, 'logs')
    out_dir = os.path.join(log_dir, 'graph_type_matrix_0246_%s' % args.label)
    shutil.rmtree(out_dir, ignore_errors=True)
    os.makedirs(out_dir)
    extra_env = None
    if args.translate:
        from env_keys import load_provider_keys
        extra_env = {k: v for k, v in load_provider_keys().items() if k == 'ANTHROPIC_API_KEY'}
        if not extra_env:
            raise SystemExit('--translate needs ANTHROPIC_API_KEY in the repo .env')
    host = os.path.dirname(os.path.abspath(args.project))
    exports_before = set(glob.glob(os.path.join(host, 'Saved', 'NodeToCode', 'Exports', '*')))
    port = free_port(27150)
    session = EditorSession(args.engine, args.project, port, os.path.join(log_dir, 'graph_type_matrix_0246_%s.log'
                                                                          % args.label),
                            allow_python=True, extra_args=['-RenderOffScreen', '-EnablePlugins=' + PLUGINS],
                            extra_env=extra_env)
    report = {'label': args.label, 'engine': args.engine, 'plugins': PLUGINS}
    scratch = '/Game/N2CTest/Matrix0246_%d' % int(time.time())
    translated_dirs = []
    try:
        if not session.start(timeout=900):
            report['error'] = 'editor did not start'
        else:
            mcp = Mcp(port)
            report['scratch'] = setup_scratch(mcp, scratch)
            data, text = mcp.python(SETUP_PY % {'folder': scratch, 'map': MAP, 'level_bp': LEVEL_BP})
            report['setup'] = data or text
            report['fixtures'] = capture_fixtures(mcp, scratch, out_dir)
            report['focused'] = capture_focused(mcp, out_dir)
            if args.translate:
                report['translations'] = translate(mcp, report['fixtures'], out_dir, args.model)
                translated_dirs = [t.get('batchOutputPath') for t in report['translations'].values()
                                   if isinstance(t, dict) and t.get('batchOutputPath')]
            # A map by its package path, not yet loaded: the resolver loads the package to look for a Blueprint
            text, err = mcp.call('list-blueprint-graphs', {'blueprintPath': UNLOADED_MAP}, timeout=300)
            report['map_package_path'] = {'error': err, 'text': text[:300], 'editor_alive': session.health(timeout=10)}
            data, text = mcp.python(CLEANUP_PY % {'folder': scratch})
            report['cleanup'] = data or text
            report['editor_alive_at_end'] = session.health(timeout=10)
    finally:
        session.stop()
    for d in translated_dirs:
        # Only the BatchTranslation_<time> folders this run made
        if os.path.basename(os.path.normpath(d)).startswith('BatchTranslation_'):
            shutil.rmtree(d, ignore_errors=True)
    for p in set(glob.glob(os.path.join(host, 'Saved', 'NodeToCode', 'Exports', '*'))) - exports_before:
        shutil.rmtree(p, ignore_errors=True) if os.path.isdir(p) else os.remove(p)
    if extra_env:
        from env_keys import find_leaks
        report['key_leaks'] = find_leaks(extra_env, [session.log_path] + glob.glob(os.path.join(out_dir, '*')))
    report['restore_ok'] = session.restore_ok
    report_path = os.path.join(log_dir, 'graph_type_matrix_0246_%s.json' % args.label)
    with open(report_path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1)
    print('report:', report_path)
    transport = [k for k, e in (report.get('fixtures') or {}).items()
                 if str(e.get('capture_error') or e.get('list_error') or '').startswith('TRANSPORT')]
    report['transport_failures'] = transport
    return 0 if report.get('fixtures') and report.get('restore_ok') and not transport else 1


if __name__ == '__main__':
    sys.exit(main())
