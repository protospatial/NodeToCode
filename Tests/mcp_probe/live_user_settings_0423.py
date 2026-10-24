"""Check that a committed project config cannot switch on MCP Python execution (card 0423).

Stdlib only; run with `python -I`:

    python -I live_user_settings_0423.py --engine G:/UE/UE_5.8 \
        --project "G:/Personal Projects/n2c/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject"

Three headless -N2CMcp editor sessions on the host:
  committed  the user's Saved/Config layer has no bAllowMcpPythonExecution, and the project's
             Config/DefaultEditorPerProjectUserSettings.ini (the file a teammate could commit) sets
             bAllowMcpPythonExecution=True and bEnableMcpServer=False. Expected: the server still starts,
             tools/list has none of run-python, save-python-script, delete-python-script, and the log warns once
             for each ignored value. Before the fix this listed the three tools (card 0090's live finding).
  user       the user's Saved/Config layer sets bAllowMcpPythonExecution=True, no project value. Expected: the three
             tools are listed.
  both       the project sets True and the user's Saved/Config layer sets False. Expected: none listed, no warning.
EditorSession snapshots the host's Saved/Config files and its Config/DefaultEditorPerProjectUserSettings.ini and puts
them back byte for byte after each session; the report's restore_ok says so. Report: logs/live_user_settings_0423.json.
"""
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from editor_session import EditorSession, free_port  # noqa: E402
from e05_regressions import rpc  # noqa: E402

SECTION = '[/Script/NodeToCode.N2CUserSettings]'
PYTHON_TOOLS = ('run-python', 'save-python-script', 'delete-python-script')
WARN_PYTHON = 'Ignored bAllowMcpPythonExecution=True from a project-shared config layer'
WARN_SERVER = 'Ignored bEnableMcpServer=False from a project-shared config layer'

MODES = {
    # name: (allow_python for the user's Saved layer, project Default values, python tools expected, warnings expected)
    'committed': (None, {'bAllowMcpPythonExecution': 'True', 'bEnableMcpServer': 'False'}, False,
                  [WARN_PYTHON, WARN_SERVER]),
    'user': (True, {}, True, []),
    'both': (False, {'bAllowMcpPythonExecution': 'True'}, False, []),
}


def run_mode(args, name, port):
    allow_python, project_values, expect_python, expect_warnings = MODES[name]
    log_path = os.path.join(args.log_dir, 'live_user_settings_0423_%s.log' % name)
    result = {'mode': name}
    session = EditorSession(args.engine, args.project, port, log_path, allow_python=allow_python)
    project_ini = session.project_config_path('DefaultEditorPerProjectUserSettings.ini')
    session.guard_file(project_ini)
    for key, value in project_values.items():
        session.patch_file(project_ini, SECTION, key, value)
    try:
        result['started'] = session.start()
        if result['started']:
            tools = [t['name'] for t in rpc('http://127.0.0.1:%d/mcp' % port, 'tools/list')['result']['tools']]
            result['tool_count'] = len(tools)
            result['python_tools'] = [t for t in PYTHON_TOOLS if t in tools]
    finally:
        session.stop()
    log = session.log_text()
    result['warnings'] = {w: log.count(w) for w in (WARN_PYTHON, WARN_SERVER)}
    result['restore_ok'] = session.restore_ok
    checks = [
        result.get('started') is True,
        (len(result.get('python_tools', [])) == len(PYTHON_TOOLS)) if expect_python else result.get('python_tools') == [],
        all(result['warnings'][w] == (1 if w in expect_warnings else 0) for w in result['warnings']),
        session.restore_ok is True,
    ]
    result['pass'] = all(checks)
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--port', type=int, default=27230)
    ap.add_argument('--modes', default='committed,user,both')
    ap.add_argument('--log-dir', default=os.path.join(HERE, 'logs'))
    args = ap.parse_args()
    os.makedirs(args.log_dir, exist_ok=True)

    results = []
    for name in args.modes.split(','):
        results.append(run_mode(args, name, free_port(args.port)))
        print(json.dumps(results[-1]))
    report = {'engine': args.engine, 'results': results, 'pass': all(r['pass'] for r in results)}
    with open(os.path.join(args.log_dir, 'live_user_settings_0423.json'), 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=2)
    print('PASS' if report['pass'] else 'FAIL')
    return 0 if report['pass'] else 1


if __name__ == '__main__':
    sys.exit(main())
