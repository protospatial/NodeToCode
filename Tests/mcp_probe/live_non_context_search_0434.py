"""search-blueprint-nodes with contextSensitive false, live over MCP (card 0434).

Stdlib only; run with `python -I`:

    python -I live_non_context_search_0434.py --engine G:/UE/UE_5.8 \
        --project "G:/Personal Projects/n2c/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" --label 5.8

It starts a headless editor and makes an Actor Blueprint in memory under a fresh /Game/N2CTest/NonContext0434_<time>
folder. Then, over MCP:

1. With nothing focused, search-blueprint-nodes {"contextSensitive": false} for "print string" and "branch" by
   blueprintPath, and once more with no target at all.
2. open-blueprint-asset focuses its EventGraph; the same two searches with contextSensitive false and no target
   argument (the focused graph), and with contextSensitive true for comparison.
3. add-bp-node-to-active-graph with the Print String and Branch results of step 2.

Checks: every search in steps 1 and 2 finds results, marked isContextSensitive false (true for the comparison) and
naming its target; Print String and Branch are among them, with the IDs the default search gives; the search without
any target is an error that says there is no graph (not an empty list); find-nodes-in-graph counts the same nodes
before and after the searches; both adds succeed and add one node each. Afterwards the folder is deleted (nothing
was saved). Report: logs/live_non_context_search_0434_<label>.json.
"""
import argparse
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from editor_session import BLUEPRINT_EDITOR_ARGS, EditorSession, free_port  # noqa: E402
from live_env_translation import call_tool, tool_text  # noqa: E402

NAME = 'BP_N2CNonContext0434'

SETUP_PY = r'''
import unreal
folder = "%(folder)s"
tools = unreal.AssetToolsHelpers.get_asset_tools()
factory = unreal.BlueprintFactory()
factory.set_editor_property("parent_class", unreal.Actor)
bp = tools.create_asset("%(name)s", folder, unreal.Blueprint, factory)
result = {"created": bp is not None}
'''

CLEANUP_PY = r'''
import unreal
folder = "%(folder)s"
bp = unreal.load_asset(folder + "/%(name)s")
if bp:
    unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).close_all_editors_for_asset(bp)
result = {"deleted": unreal.EditorAssetLibrary.delete_directory(folder)}
'''

EXPECTED = {'print string': 'KismetSystemLibrary:PrintString', 'branch': 'K2Node_IfThenElse'}


class Probe:
    def __init__(self, port):
        self.port = port
        self.rid = 100

    def tool(self, name, arguments):
        self.rid += 1
        status, response = call_tool(self.port, name, arguments, self.rid)
        is_error = bool((response.get('result') or {}).get('isError')) if isinstance(response, dict) else True
        return status, tool_text(response), is_error

    def python(self, code):
        body = '\n'.join('    ' + line for line in code.strip('\n').splitlines())
        wrapped = 'def _n2c_0434():\n%s\n    return result\nresult = _n2c_0434()\ndel _n2c_0434\n' % body
        status, text, is_error = self.tool('run-python', {'code': wrapped})
        try:
            data = json.loads(text).get('data') or {}
        except (ValueError, AttributeError):
            data = {}
        return data, text[:600]

    def search(self, term, context_sensitive, path=None):
        args = {'searchTerm': term, 'contextSensitive': context_sensitive, 'maxResults': 20}
        if path:
            args['blueprintPath'] = path
        status, text, is_error = self.tool('search-blueprint-nodes', args)
        try:
            body = json.loads(text)
        except ValueError:
            body = {}
        nodes = body.get('nodes') or [] if isinstance(body, dict) else []
        hits = [{'id': (n.get('spawnMetadata') or {}).get('actionIdentifier'),
                 'name': (n.get('spawnMetadata') or {}).get('nodeName'),
                 'cs': (n.get('spawnMetadata') or {}).get('isContextSensitive')} for n in nodes]
        return {'error': is_error, 'text': text[:300] if is_error or not hits else '', 'count': len(hits), 'hits': hits,
                'target': body.get('target') if isinstance(body, dict) else None}

    def node_count(self, path):
        status, text, is_error = self.tool('find-nodes-in-graph', {'blueprintPath': path, 'searchTerms': ['K2Node']})
        try:
            body = json.loads(text)
        except ValueError:
            return None
        nodes = body.get('nodes') if isinstance(body, dict) else None
        return len(nodes) if isinstance(nodes, list) else body.get('count') if isinstance(body, dict) else None


def summarize(result, term):
    want = EXPECTED[term]
    hit = next((h for h in result['hits'] if h['id'] and want in h['id']), None)
    return {'error': result['error'], 'count': result['count'], 'target': result['target'],
            'expected_hit': hit, 'flags': sorted({str(h['cs']) for h in result['hits']}), 'text': result['text']}


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--label', required=True)
    ap.add_argument('--port', type=int, default=27199)
    args = ap.parse_args()

    log_dir = os.path.join(HERE, 'logs')
    os.makedirs(log_dir, exist_ok=True)
    log_path = os.path.join(log_dir, 'live_non_context_search_0434_%s_editor.log' % args.label)
    report_path = os.path.join(log_dir, 'live_non_context_search_0434_%s.json' % args.label)

    port = free_port(args.port)
    folder = '/Game/N2CTest/NonContext0434_%s' % time.strftime('%H%M%S')
    path = '%s/%s.%s' % (folder, NAME, NAME)
    report = {'label': args.label, 'port': port, 'folder': folder}
    checks = {}

    with EditorSession(args.engine, args.project, port, log_path, allow_python=True,
                       extra_args=BLUEPRINT_EDITOR_ARGS) as s:
        report['started'] = s.start()
        if not report['started']:
            checks['editor_started'] = False
        else:
            p = Probe(port)
            data, text = p.python(SETUP_PY % {'folder': folder, 'name': NAME})
            report['setup'] = data or text
            checks['setup'] = data.get('created') is True
            nodes_before = p.node_count(path)

            # 1. Nothing focused: by blueprintPath, and with no target at all
            report['by_path'] = {}
            for term in EXPECTED:
                r = summarize(p.search(term, False, path), term)
                report['by_path'][term] = r
                checks['by path "%s": results' % term] = not r['error'] and r['count'] > 0
                checks['by path "%s": finds it' % term] = r['expected_hit'] is not None
                checks['by path "%s": marked false' % term] = r['flags'] == ['False']
                checks['by path "%s": names target' % term] = isinstance(r['target'], dict)
            no_target = p.search('print string', False)
            report['no_target'] = {'error': no_target['error'], 'count': no_target['count'], 'text': no_target['text']}
            checks['no target: an error saying there is no graph'] = (
                no_target['error'] and 'No graph to search for' in no_target['text'])

            # 2. Focused EventGraph, no target argument
            status, text, err = p.tool('open-blueprint-asset', {'asset_path': path, 'focus_graph': 'EventGraph'})
            checks['open'] = not err
            report['focused'] = {}
            for term in EXPECTED:
                off = p.search(term, False)
                on = p.search(term, True)
                r = summarize(off, term)
                report['focused'][term] = dict(r, default_count=on['count'])
                checks['focused "%s": results' % term] = not r['error'] and r['count'] > 0
                checks['focused "%s": finds it' % term] = r['expected_hit'] is not None
                checks['focused "%s": marked false' % term] = r['flags'] == ['False']
                checks['focused "%s": default search marked true' % term] = (
                    sorted({str(h['cs']) for h in on['hits']}) == ['True'])
                checks['focused "%s": same IDs as the default search' % term] = (
                    [h['id'] for h in off['hits']] == [h['id'] for h in on['hits']])
            nodes_after_search = p.node_count(path)
            report['nodes'] = {'before': nodes_before, 'after_searches': nodes_after_search}
            checks['searches leave the node count'] = nodes_before is not None and nodes_before == nodes_after_search

            # 3. Add the Print String and Branch results
            report['adds'] = {}
            count = nodes_after_search
            for i, term in enumerate(EXPECTED):
                hit = (report['focused'][term] or {}).get('expected_hit')
                if not hit:
                    checks['add "%s"' % term] = False
                    continue
                status, text, err = p.tool('add-bp-node-to-active-graph', {
                    'nodeName': hit['name'], 'actionIdentifier': hit['id'], 'location': {'x': 400, 'y': 200 * (i + 1)}})
                after = p.node_count(path)
                report['adds'][term] = {'error': err, 'text': text[:300], 'nodes_after': after}
                checks['add "%s"' % term] = not err
                checks['add "%s": one node more' % term] = count is not None and after == count + 1
                count = after

            data, text = p.python(CLEANUP_PY % {'folder': folder, 'name': NAME})
            report['cleanup'] = data or text
        report['stopped'] = s.stop() if hasattr(s, 'stop') else None

    report['checks'] = checks
    report['ok'] = bool(checks) and all(checks.values())
    with open(report_path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=2)
    for name, ok in checks.items():
        print('%s %s' % ('PASS' if ok else 'FAIL', name))
    print('report: %s' % report_path)
    return 0 if report['ok'] else 1


if __name__ == '__main__':
    sys.exit(main())
