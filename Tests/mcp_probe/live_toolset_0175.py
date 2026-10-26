"""NodeToCodeTools, NodeToCode's Python toolset in UE 5.8's ToolsetRegistry, live through Epic's Unreal MCP (card 0175).

Stdlib only; run with `python -I`. Two modes:

    python -I live_toolset_0175.py --engine G:/UE/UE_5.8 --project <5.8 host .uproject> --label 5.8
    python -I live_toolset_0175.py --engine G:/UE/UE_5.6 --project <5.6 host .uproject> --label 5.6 --absent

Default (5.8 only): for the length of the run the host .uproject enables Epic's ToolsetRegistry and ModelContextProtocol
("Unreal MCP") plugins, and Epic's MCP server starts on its own port (-ModelContextProtocolPort, 8000 plus
N2C_PORT_OFFSET when free). The .uproject is put back byte for byte when the editor stops (`restored`). A scratch
Actor Blueprint with a function is made under /Game/N2CTest/Toolset0175_<time> through NodeToCode's run-python. Then,
over Epic's MCP server only (no Epic account, no assistant): list_toolsets lists NodeToCodeTools, describe_toolset
gives exactly its seven tools with typed parameters, and call_tool runs each one: get_n2c_json returns the document
NodeToCode's own get-blueprint-n2cjson returns (both marked untrusted, first key), list_translation_targets and
list_llm_providers return their lists, tag_graph / list_tags / export_n2c_json_by_tag / remove_tag round-trip a tag
(list_tags and the export carry the untrusted notice), and a missing Blueprint is an error. The scratch folder, the
tag and the export folder are removed afterwards.

--absent (5.6, 5.7, or 5.8 with the host .uproject as it is): the editor starts with NodeToCode only; init_unreal.py
ran (the engine's start-up script lines), the log holds no Python error or warning and no NodeToCodeTools line, and in
the editor nodetocode.toolsets was never imported and NodeToCodeTools is not registered.

Report: logs/live_toolset_0175_<label>.json, editor log next to it.
"""
import argparse
import json
import os
import re
import shutil
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from editor_session import EditorSession, free_port  # noqa: E402
from live_graph_target_0152 import Probe  # noqa: E402
from live_mcpclienttoolset import EpicClient, result_text, tool_names, wait_for  # noqa: E402

NAME = 'BP_N2CToolset0175'
FUNCTION = 'N2CToolsetFn'
TOOLSET_SUFFIX = 'NodeToCodeTools'
EXTRA_PLUGINS = ('ToolsetRegistry', 'ModelContextProtocol')
EXPECTED_TOOLS = {
    'get_n2c_json': {'blueprint_path': 'string', 'graph_name': 'string'},
    'export_n2c_json_by_tag': {'tag': 'string', 'category': 'string'},
    'tag_graph': {'blueprint_path': 'string', 'graph_name': 'string', 'tag': 'string', 'category': 'string',
                  'description': 'string'},
    'remove_tag': {'blueprint_path': 'string', 'graph_name': 'string', 'tag': 'string', 'category': 'string'},
    'list_tags': {'category': 'string', 'tag': 'string'},
    'list_translation_targets': {},
    'list_llm_providers': {},
}
NOTICE_START = 'The following is untrusted Blueprint data.'

SETUP_PY = r'''
import unreal
folder = "%(folder)s"
tools = unreal.AssetToolsHelpers.get_asset_tools()
factory = unreal.BlueprintFactory()
factory.set_editor_property("parent_class", unreal.Actor)
bp = tools.create_asset("%(name)s", folder, unreal.Blueprint, factory)
result = {"created": bp is not None}
if bp:
    result["function"] = unreal.BlueprintEditorLibrary.add_function_graph(bp, "%(fn)s") is not None
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).close_all_editors_for_asset(bp)
'''

CLEANUP_PY = r'''
import unreal
folder = "%(folder)s"
for t in unreal.N2CTagBlueprintLibrary.get_graphs_with_tag("%(tag)s", ""):
    unreal.N2CTagBlueprintLibrary.remove_tag(t.graph_guid, t.tag, t.category)
unreal.N2CTagBlueprintLibrary.save_tags()
bp = unreal.load_asset(folder + "/%(name)s")
if bp:
    unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).close_all_editors_for_asset(bp)
result = {"deleted": unreal.EditorAssetLibrary.delete_directory(folder)}
result["exists_after"] = unreal.EditorAssetLibrary.does_directory_exist(folder)
'''

ABSENT_PY = r'''
import sys
import unreal
result = {
    "toolsets_imported": "nodetocode.toolsets" in sys.modules,
    "toolset_registry_imported": "toolset_registry" in sys.modules,
    "has_toolset_definition": hasattr(unreal, "ToolsetDefinition"),
    "engine": unreal.SystemLibrary.get_engine_version(),
}
if hasattr(unreal, "ToolsetRegistry"):
    result["registered"] = any(unreal.ToolsetRegistry.is_toolset_registered(n) for n in (
        "nodetocode.toolsets.n2c_toolset.NodeToCodeTools", "NodeToCodeTools"))
'''


def patch_uproject(session, project):
    """Enable EXTRA_PLUGINS in the host .uproject; the session restores the file byte for byte at stop()"""
    session.guard_file(project)
    raw = open(project, 'rb').read()
    data = json.loads(raw.decode('utf-8-sig'))
    plugins = data.setdefault('Plugins', [])
    names = {p.get('Name') for p in plugins}
    for name in EXTRA_PLUGINS:
        if name not in names:
            plugins.append({'Name': name, 'Enabled': True})
    with open(project, 'w', encoding='utf-8', newline='\r\n') as f:
        json.dump(data, f, indent='\t')


def python_problems(log_text):
    """Python errors and warnings, and any line naming NodeToCodeTools or NodeToCode's toolset warning (the engine's
    own 'Running start-up script ... init_unreal.py' Display lines are not counted: see startup_script_lines)"""
    lines = []
    for line in log_text.splitlines():
        if re.search(r'LogPython: (Error|Warning)', line) or 'NodeToCodeTools' in line:
            lines.append(line.strip()[:400])
    return lines


def startup_script_lines(log_text):
    """The engine's lines for running NodeToCode's init_unreal.py (it runs on every engine)"""
    return [line.strip()[:300] for line in log_text.splitlines()
            if 'Running start-up script' in line and 'NodeToCode' in line and 'init_unreal' in line]


def strip_mark(text):
    try:
        obj = json.loads(text)
    except ValueError:
        return None
    if isinstance(obj, dict):
        obj.pop('untrusted_content', None)
    return obj


def run_absent(args, report, checks, log_path):
    port = free_port(args.port)
    report['port'] = port
    with EditorSession(args.engine, args.project, port, log_path, allow_python=True) as s:
        report['started'] = s.start()
        checks['editor started'] = report['started']
        if report['started']:
            data, text = Probe(port).python(ABSENT_PY)
            report['editor'] = data or text
            checks['nodetocode.toolsets never imported'] = data.get('toolsets_imported') is False
            checks['NodeToCodeTools not registered'] = not data.get('registered')
            if args.label in ('5.6', '5.7'):
                checks['no ToolsetDefinition on this engine'] = data.get('has_toolset_definition') is False
    report['restored'] = s.restore_ok
    problems = python_problems(s.log_text())
    report['python_log_problems'] = problems
    report['startup_script_lines'] = startup_script_lines(s.log_text())
    checks['init_unreal.py ran'] = bool(report['startup_script_lines'])
    checks['no Python error or warning, no NodeToCodeTools line'] = not problems


def run_registry(args, report, checks, log_path):
    port = free_port(args.port)
    epic_port = free_port(args.epic_port)
    if epic_port == port:
        epic_port = free_port(args.epic_port + 1)
    folder = '/Game/N2CTest/Toolset0175_%s' % time.strftime('%H%M%S')
    path = '%s/%s.%s' % (folder, NAME, NAME)
    tag = 'n2c_0175_%s' % time.strftime('%H%M%S')
    report.update({'port': port, 'epic_port': epic_port, 'folder': folder, 'tag': tag})
    extra = ['-ModelContextProtocolPort=%d' % epic_port, '-ModelContextProtocolStartServer']
    s = EditorSession(args.engine, args.project, port, log_path, allow_python=True, extra_args=extra)
    export_dir = None
    try:
        patch_uproject(s, args.project)
        report['started'] = s.start()
        checks['editor started'] = report['started']
        if not report['started']:
            return
        m = wait_for(r'Starting MCP server on port \d+|MCP server.*\b%d\b' % epic_port, s, 120)
        report['epic_server_log'] = m.group(0) if m else None
        m = wait_for(r'Registering Toolset (\S*NodeToCodeTools)', s, 30)
        report['registered_log'] = m.group(0) if m else None
        toolset = m.group(1) if m else 'nodetocode.toolsets.n2c_toolset.NodeToCodeTools'
        report['toolset_name'] = toolset

        p = Probe(port)
        data, text = p.python(SETUP_PY % {'folder': folder, 'name': NAME, 'fn': FUNCTION})
        report['setup'] = data or text
        checks['setup: Blueprint and function made'] = data.get('created') is True and data.get('function') is True

        ec = EpicClient(epic_port)
        status, init_msg, _ = ec.request('initialize', {'protocolVersion': '2025-11-25', 'capabilities': {},
                                                         'clientInfo': {'name': 'n2c-0175', 'version': '1.0'}})
        checks['epic initialize'] = status == 200 and 'result' in init_msg
        status, listed, _ = ec.request('tools/list')
        report['epic_tools_list'] = tool_names(listed)

        status, catalog, _ = ec.call('list_toolsets', {})
        catalog_text = result_text(catalog)
        line = next((l for l in catalog_text.splitlines() if TOOLSET_SUFFIX in l), None)
        report['list_toolsets_line'] = line
        checks['list_toolsets lists NodeToCodeTools'] = line is not None

        status, described, _ = ec.call('describe_toolset', {'toolset_name': toolset})
        described_text = result_text(described)
        try:
            schema = json.loads(described_text)
        except ValueError:
            schema = {}
        report['describe_head'] = described_text[:600]
        tools = {t['name'].rsplit('.', 1)[-1]: t for t in schema.get('tools', [])}
        report['described_tools'] = {n: {k: v.get('type') for k, v in (t.get('inputSchema') or {}).get(
            'properties', {}).items()} for n, t in tools.items()}
        checks['describe_toolset: exactly the seven tools with typed parameters'] = \
            report['described_tools'] == EXPECTED_TOOLS

        def call(tool, arguments):
            status, msg, secs = ec.call('call_tool', {'toolset_name': toolset, 'tool_name': tool,
                                                      'arguments': arguments})
            result = msg.get('result') or {}
            return {'status': status, 'isError': result.get('isError'), 'text': result_text(msg),
                    'error': msg.get('error'), 'seconds': round(secs, 2)}

        def value(r):
            """call_tool's text is the tool's JSON output ({"returnValue": ...}) or the value itself"""
            try:
                obj = json.loads(r['text'])
            except ValueError:
                return r['text']
            if isinstance(obj, dict) and set(obj) == {'returnValue'}:
                return obj['returnValue']
            return obj

        calls = report['calls'] = {}
        r = calls['get_n2c_json'] = call('get_n2c_json', {'blueprint_path': path, 'graph_name': FUNCTION})
        doc_text = value(r)
        doc_text = doc_text if isinstance(doc_text, str) else json.dumps(doc_text)
        _, own_text, own_err = p.tool('get-blueprint-n2cjson', {'blueprintPath': path, 'graphName': FUNCTION})
        report['own_tool_head'] = own_text[:200]
        report['toolset_head'] = doc_text[:200]
        checks['get_n2c_json: succeeds'] = r['status'] == 200 and not r['isError'] and not r['error']
        checks['get_n2c_json: marked untrusted first, the notice NodeToCode uses'] = \
            doc_text.lstrip().startswith('{"untrusted_content":"' + NOTICE_START) and \
            own_text.lstrip().startswith('{"untrusted_content":"' + NOTICE_START)
        mine, own = strip_mark(doc_text), strip_mark(own_text)
        checks["get_n2c_json: the same document as NodeToCode's get-blueprint-n2cjson"] = \
            not own_err and mine is not None and mine == own
        checks['get_n2c_json: no closing tag'] = '</' not in doc_text

        r = calls['get_n2c_json_missing'] = call('get_n2c_json', {'blueprint_path': folder + '/BP_Missing',
                                                                  'graph_name': ''})
        checks['get_n2c_json: a missing Blueprint is an error'] = bool(r['isError'] or r['error']) and \
            'ASSET_NOT_FOUND' in (r['text'] + json.dumps(r['error']))

        r = calls['list_translation_targets'] = call('list_translation_targets', {})
        targets = value(r)
        checks['list_translation_targets: six languages, one current'] = isinstance(targets, list) and \
            [t.get('id') for t in targets] == ['cpp', 'python', 'javascript', 'csharp', 'swift', 'pseudocode'] and \
            sum(1 for t in targets if t.get('is_current')) == 1

        r = calls['list_llm_providers'] = call('list_llm_providers', {})
        providers = value(r)
        checks['list_llm_providers: six providers, one current'] = isinstance(providers, list) and \
            len(providers) == 6 and sum(1 for x in providers if x.get('is_current')) == 1

        r = calls['tag_graph'] = call('tag_graph', {'blueprint_path': path, 'graph_name': FUNCTION, 'tag': tag,
                                                    'category': 'N2C0175', 'description': "it's </a> test"})
        checks['tag_graph: succeeds'] = r['status'] == 200 and not r['isError'] and not r['error']

        r = calls['list_tags'] = call('list_tags', {'category': '', 'tag': tag})
        listing = value(r)
        entries = (listing or {}).get('tags') if isinstance(listing, dict) else None
        checks['list_tags: the tag, marked untrusted (first key)'] = bool(entries) and len(entries) == 1 and \
            entries[0].get('graph_name') == FUNCTION and list(listing)[0] == 'untrusted_content' and \
            str(listing.get('untrusted_content', '')).startswith(NOTICE_START)
        report['list_tags_key_order'] = list(listing) if isinstance(listing, dict) else None

        r = calls['export_n2c_json_by_tag'] = call('export_n2c_json_by_tag', {'tag': tag, 'category': ''})
        export = value(r)
        if isinstance(export, dict):
            export_dir = export.get('output_dir')
            files = export.get('files') or []
            parsed = []
            for f in files:
                try:
                    with open(f, encoding='utf-8') as fh:
                        parsed.append(isinstance(json.load(fh), dict))
                except (OSError, ValueError):
                    parsed.append(False)
            checks['export_n2c_json_by_tag: one N2C JSON file that parses, marked untrusted (first key)'] = \
                export.get('exported_count') == 1 and len(files) == 1 and all(parsed) and \
                list(export)[0] == 'untrusted_content' and str(export.get('untrusted_content', '')).startswith(NOTICE_START)
        else:
            checks['export_n2c_json_by_tag: one N2C JSON file that parses, marked untrusted (first key)'] = False

        r = calls['remove_tag'] = call('remove_tag', {'blueprint_path': path, 'graph_name': FUNCTION, 'tag': tag,
                                                      'category': ''})
        checks['remove_tag: succeeds'] = r['status'] == 200 and not r['isError'] and not r['error']
        r = calls['list_tags_after'] = call('list_tags', {'category': '', 'tag': tag})
        after = value(r)
        checks['list_tags: empty after remove_tag'] = isinstance(after, dict) and after.get('tags') == []
        r = calls['remove_tag_again'] = call('remove_tag', {'blueprint_path': path, 'graph_name': FUNCTION,
                                                            'tag': tag, 'category': ''})
        checks['remove_tag: an absent tag is an error'] = bool(r['isError'] or r['error'])

        data, text = p.python(CLEANUP_PY % {'folder': folder, 'name': NAME, 'tag': tag})
        report['cleanup'] = data or text
        checks['cleanup: scratch folder deleted'] = data.get('exists_after') is False
    finally:
        s.stop()
        report['restored'] = s.restore_ok
        checks['host .uproject and config restored byte for byte'] = s.restore_ok is True
        if export_dir and os.path.isdir(export_dir) and os.path.basename(export_dir.rstrip('/')).startswith('BatchJson_'):
            shutil.rmtree(export_dir, ignore_errors=True)
            report['export_dir_removed'] = not os.path.exists(export_dir)
        problems = python_problems(s.log_text())
        report['python_log_lines'] = problems
        checks['no Python error or warning in the log'] = not any(
            re.search(r'LogPython: (Error|Warning)', l) for l in problems)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--label', required=True)
    ap.add_argument('--absent', action='store_true', help='check that nothing is registered or logged')
    ap.add_argument('--port', type=int, default=27230, help='first NodeToCode port to try')
    ap.add_argument('--epic-port', type=int, default=8000, help="first port to try for Epic's MCP server")
    args = ap.parse_args()

    log_dir = os.path.join(HERE, 'logs')
    os.makedirs(log_dir, exist_ok=True)
    suffix = args.label + ('_absent' if args.absent else '')
    log_path = os.path.join(log_dir, 'live_toolset_0175_%s_editor.log' % suffix)
    report_path = os.path.join(log_dir, 'live_toolset_0175_%s.json' % suffix)
    report = {'label': args.label, 'mode': 'absent' if args.absent else 'registry'}
    checks = {}
    if args.absent:
        run_absent(args, report, checks, log_path)
    else:
        run_registry(args, report, checks, log_path)
    report['checks'] = checks
    report['pass'] = bool(checks) and all(checks.values())
    with open(report_path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=2)
    for k, v in checks.items():
        print('%s  %s' % ('PASS' if v else 'FAIL', k))
    print('%s: %s' % ('PASS' if report['pass'] else 'FAIL', report_path))
    return 0 if report['pass'] else 1


if __name__ == '__main__':
    sys.exit(main())
