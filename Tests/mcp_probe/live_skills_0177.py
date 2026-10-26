"""NodeToCode's Agent Skills in UE 5.8's skill registry, live through Epic's Unreal MCP (card 0177).

Stdlib only; run with `python -I` (set N2C_PORT_OFFSET when other agents run probes). Two modes:

    python -I live_skills_0177.py --engine G:/UE/UE_5.8 --project <5.8 host .uproject> --label 5.8
    python -I live_skills_0177.py --engine G:/UE/UE_5.6 --project <5.6 host .uproject> --label 5.6 --absent

Default (5.8 only): for the length of the run the host .uproject enables Epic's ToolsetRegistry and ModelContextProtocol
("Unreal MCP") plugins (put back byte for byte when the editor stops), and Epic's MCP server starts on its own port
(8000 plus N2C_PORT_OFFSET when free). Over Epic's server only: list_toolsets lists the AgentSkillToolset,
describe_toolset gives its list and get tools, the list tool returns NodeToCode's three skills (class paths under
/Engine/PythonTypes) with the descriptions of their SKILL.md files, and the get tool serves each one's instructions,
equal to Content/Python/nodetocode/skills/<name>.md. The log has no Python error or warning.

--absent (5.6, 5.7, or 5.8 with the host .uproject as it is): the editor starts with NodeToCode only; init_unreal.py
ran, nodetocode.skills was never imported, and the log holds no Python error or
warning and no NodeToCode registration line.

Report: logs/live_skills_0177_<label>.json, editor log next to it.
"""
import argparse
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from editor_session import EditorSession, free_port  # noqa: E402
from live_graph_target_0152 import Probe  # noqa: E402
from live_mcpclienttoolset import EpicClient, result_text, wait_for  # noqa: E402
from live_toolset_0175 import patch_uproject, startup_script_lines  # noqa: E402

PLUGIN_ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
SKILL_FILES = os.path.join(PLUGIN_ROOT, 'Content', 'Python', 'nodetocode', 'skills')
CLASSES = {'blueprint-comprehension': 'NodeToCodeBlueprintComprehensionSkill',
           'blueprint-to-cpp': 'NodeToCodeBlueprintToCppSkill',
           'blueprint-migration': 'NodeToCodeBlueprintMigrationSkill'}

ABSENT_PY = r'''
import sys
import unreal
result = {
    "skills_imported": "nodetocode.skills" in sys.modules,
    "toolset_registry_imported": "toolset_registry" in sys.modules,
    "has_agent_skill": hasattr(unreal, "AgentSkill"),
    "engine": unreal.SystemLibrary.get_engine_version(),
}
'''


def python_problems(log_text):
    """Python errors and warnings, and NodeToCode's own registration warnings"""
    return [line.strip()[:400] for line in log_text.splitlines()
            if re.search(r'LogPython: (Error|Warning)', line) or 'NodeToCode: Agent Skills' in line
            or 'NodeToCodeTools toolset not registered' in line]


def run_absent(args, report, checks, log_path):
    port = free_port(args.port)
    report['port'] = port
    with EditorSession(args.engine, args.project, port, log_path, allow_python=True) as s:
        report['started'] = s.start()
        checks['editor started'] = report['started']
        if report['started']:
            data, text = Probe(port).python(ABSENT_PY)
            report['editor'] = data or text
            checks['nodetocode.skills never imported'] = data.get('skills_imported') is False
            if args.label in ('5.6', '5.7'):
                checks['no AgentSkill on this engine'] = data.get('has_agent_skill') is False
    report['restored'] = s.restore_ok
    problems = python_problems(s.log_text())
    report['python_log_problems'] = problems
    report['startup_script_lines'] = startup_script_lines(s.log_text())
    checks['init_unreal.py ran'] = bool(report['startup_script_lines'])
    checks['no Python error or warning, no NodeToCode registration warning'] = not problems


def run_registry(args, report, checks, log_path):
    port = free_port(args.port)
    epic_port = free_port(args.epic_port)
    if epic_port == port:
        epic_port = free_port(args.epic_port + 1)
    report.update({'port': port, 'epic_port': epic_port})
    extra = ['-ModelContextProtocolPort=%d' % epic_port, '-ModelContextProtocolStartServer']
    s = EditorSession(args.engine, args.project, port, log_path, allow_python=True, extra_args=extra)
    try:
        patch_uproject(s, args.project)
        report['started'] = s.start()
        checks['editor started'] = report['started']
        if not report['started']:
            return
        m = wait_for(r'Starting MCP server on port \d+|MCP server.*\b%d\b' % epic_port, s, 120)
        report['epic_server_log'] = m.group(0) if m else None

        ec = EpicClient(epic_port)
        status, init_msg, _ = ec.request('initialize', {'protocolVersion': '2025-11-25', 'capabilities': {},
                                                         'clientInfo': {'name': 'n2c-0177', 'version': '1.0'}})
        checks['epic initialize'] = status == 200 and 'result' in init_msg

        status, catalog, _ = ec.call('list_toolsets', {})
        catalog_text = result_text(catalog)
        line = next((l for l in catalog_text.splitlines() if 'AgentSkillToolset' in l), None)
        report['list_toolsets_line'] = line
        match = re.search(r'(\S*AgentSkillToolset)', line or '')
        toolset = match.group(1).strip('"\',`*:') if match else 'AgentSkillToolset'
        report['toolset_name'] = toolset
        checks['list_toolsets lists the AgentSkillToolset'] = line is not None

        status, described, _ = ec.call('describe_toolset', {'toolset_name': toolset})
        try:
            schema = json.loads(result_text(described))
        except ValueError:
            schema = {}
        report['describe_head'] = result_text(described)[:600]
        tools = {}
        for t in schema.get('tools', []):
            short = t['name'].rsplit('.', 1)[-1]
            tools[short.replace('_', '').lower()] = (short, list((t.get('inputSchema') or {}).get('properties', {})))
        report['described_tools'] = {v[0]: v[1] for v in tools.values()}
        checks['describe_toolset: list and get tools'] = 'listskills' in tools and 'getskills' in tools
        if not ('listskills' in tools and 'getskills' in tools):
            return
        if schema.get('name'):
            toolset = schema['name']

        def call(tool, arguments):
            status, msg, secs = ec.call('call_tool', {'toolset_name': toolset, 'tool_name': tool,
                                                      'arguments': arguments})
            result = msg.get('result') or {}
            text = result_text(msg)
            try:
                value = json.loads(text)
                if isinstance(value, dict) and set(value) == {'returnValue'}:
                    value = value['returnValue']
            except ValueError:
                value = text
            return {'status': status, 'isError': result.get('isError'), 'error': msg.get('error'),
                    'seconds': round(secs, 2)}, value

        with open(os.path.join(SKILL_FILES, 'skills.json'), encoding='utf-8') as f:
            descriptions = json.load(f)
        r, listed = call(tools['listskills'][0], {})
        report['list_call'] = r
        listed = listed if isinstance(listed, dict) else {}
        report['listed_paths'] = sorted(listed)
        paths = {}
        for name, cls in CLASSES.items():
            path = next((p for p in listed if p.rsplit('.', 1)[-1] == cls), None)
            paths[name] = path
            checks['list_skills: %s with its description' % name] = path is not None and \
                listed[path] == descriptions[name]['description'] and bool(listed[path].strip())
        report['skill_paths'] = paths

        found = [p for p in paths.values() if p]
        r, served = call(tools['getskills'][0], {tools['getskills'][1][0]: found})
        report['get_call'] = r
        served = served if isinstance(served, dict) else {}
        served_text = {}
        for name, path in paths.items():
            details = served.get(path) or {}
            served_text[name] = next((v for k, v in details.items() if k.lower() == 'instructions'), '')
            with open(os.path.join(SKILL_FILES, name + '.md'), encoding='utf-8') as f:
                expected = f.read()
            report.setdefault('served_heads', {})[name] = served_text[name][:200]
            checks['get_skills: %s instructions as generated' % name] = bool(served_text[name].strip()) and \
                served_text[name] == expected
        checks['instructions name NodeToCodeTools and Epic tools'] = \
            'NodeToCodeTools.get_n2c_json' in served_text.get('blueprint-comprehension', '') and \
            'BlueprintTools.compile_blueprint' in served_text.get('blueprint-migration', '')
    finally:
        s.stop()
        report['restored'] = s.restore_ok
        checks['host .uproject and config restored byte for byte'] = s.restore_ok is True
        problems = python_problems(s.log_text())
        report['python_log_lines'] = problems
        checks['no Python error or warning in the log'] = not problems


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--label', required=True)
    ap.add_argument('--absent', action='store_true', help='check that nothing is registered or logged')
    ap.add_argument('--port', type=int, default=27240, help='first NodeToCode port to try')
    ap.add_argument('--epic-port', type=int, default=8000, help="first port to try for Epic's MCP server")
    args = ap.parse_args()

    log_dir = os.path.join(HERE, 'logs')
    os.makedirs(log_dir, exist_ok=True)
    suffix = args.label + ('_absent' if args.absent else '')
    log_path = os.path.join(log_dir, 'live_skills_0177_%s_editor.log' % suffix)
    report_path = os.path.join(log_dir, 'live_skills_0177_%s.json' % suffix)
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
