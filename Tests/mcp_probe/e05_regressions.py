"""Regression cases for crash and data-loss fixes (epic E05), sent over MCP to a running editor.

Stdlib only; run with `python -I`. Either point it at a running editor:

    python -I e05_regressions.py --url http://127.0.0.1:27000/mcp

or let it start a headless one (same launch form as mcp_probe.py):

    python -I e05_regressions.py --engine G:/UE/UE_5.8 --project <host>.uproject

Prints one PASS or FAIL line per case and exits 0 only when every case passes. Later E05 cards add cases here.
"""
import argparse
import json
import os
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)


def rpc(url, method, params=None, rid=1):
    body = json.dumps({'jsonrpc': '2.0', 'id': rid, 'method': method, 'params': params or {}}).encode('utf-8')
    req = urllib.request.Request(url, data=body, headers={'Content-Type': 'application/json'})
    with urllib.request.urlopen(req, timeout=60) as r:
        return json.loads(r.read().decode('utf-8'))


def tool(url, name, arguments):
    return rpc(url, 'tools/call', {'name': name, 'arguments': arguments})


def is_tool_error(resp, needle=None):
    res = resp.get('result') or {}
    text = ''.join(c.get('text', '') for c in res.get('content', []))
    return bool(res.get('isError')) and (needle is None or needle.lower() in text.lower()), text[:160]


CASES = []


def case(fn):
    CASES.append(fn)
    return fn


@case
def prompts_get_non_string_argument(url):
    prompts = rpc(url, 'prompts/list').get('result', {}).get('prompts', [])
    if not prompts:
        return None, 'no prompts registered'
    name = prompts[0]['name']
    results = []
    for value in (None, [1, 2], {'a': 1}):
        r = rpc(url, 'prompts/get', {'name': name, 'arguments': {'x': value}})
        results.append(r.get('error', {}).get('code'))
    return all(c == -32602 for c in results), 'error codes %s' % results


@case
def create_variable_long_type(url):
    return is_tool_error(tool(url, 'create-variable', {'variableName': 'N2CProbeVar', 'typeIdentifier': 'x' * 1100}), 'limited to')


@case
def create_local_variable_long_name(url):
    return is_tool_error(tool(url, 'create-local-variable', {'variableName': 'v' * 1100, 'typeIdentifier': 'bool'}), 'limited to')


@case
def get_local_variable_node_long_name(url):
    return is_tool_error(tool(url, 'create-get-local-function-variable-node', {'variableName': 'v' * 1100}), 'limited to')


@case
def read_content_browser_huge_page(url):
    ok, text = is_tool_error(tool(url, 'read-content-browser-path', {'path': '/Game', 'page': 2147483647}))
    return ok, text


# Runs in the editor: a scratch Blueprint with one function, compiled and saved (so its package is clean), with that
# function's graph opened so add-function-return-pin targets it
DIRTY_SETUP_PY = r'''
import json, unreal
path = "%(dir)s/%(name)s"
factory = unreal.BlueprintFactory()
factory.set_editor_property("parent_class", unreal.Actor)
bp = unreal.AssetToolsHelpers.get_asset_tools().create_asset("%(name)s", "%(dir)s", unreal.Blueprint, factory)
steps = {"created": bp is not None}
if bp:
    steps["function"] = unreal.BlueprintEditorLibrary.add_function_graph(bp, "N2CProbeFunction") is not None
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    steps["saved"] = unreal.EditorAssetLibrary.save_loaded_asset(bp, False)
    steps["opened"] = json.loads(unreal.N2CPythonBridge.open_blueprint(path, "N2CProbeFunction")).get("success")
    dirty = [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
    steps["clean_before"] = path not in dirty
result = steps
'''

# Runs in the editor: is the scratch package in the dirty list now; then close its editor and delete the folder
DIRTY_CHECK_PY = r'''
import unreal
path = "%(dir)s/%(name)s"
dirty = [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
bp = unreal.load_asset(path)
if bp:
    unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).close_all_editors_for_asset(bp)
deleted = unreal.EditorAssetLibrary.delete_directory("%(dir)s")
result = {"dirty_after": path in dirty, "deleted": deleted}
'''


def run_python(url, code):
    resp = tool(url, 'run-python', {'code': code})
    text = ''.join(c.get('text', '') for c in (resp.get('result') or {}).get('content', []))
    try:
        return (json.loads(text).get('data') or {}), text[:300]
    except ValueError:
        return {}, text[:300]


@case
def add_return_pin_marks_package_dirty(url):
    """Card 0064: on a saved, clean Blueprint, add-function-return-pin leaves the package in the dirty list"""
    names = {'dir': '/Game/E05Probe', 'name': 'BP_N2CDirtyProbe_%d' % os.getpid()}
    setup, text = run_python(url, DIRTY_SETUP_PY % names)
    if not setup:
        return None, 'run-python unavailable: %s' % text
    if not (setup.get('saved') and setup.get('opened') and setup.get('clean_before')):
        run_python(url, DIRTY_CHECK_PY % names)
        return False, 'setup %s' % setup
    added = tool(url, 'add-function-return-pin', {'pinName': 'bOk', 'typeIdentifier': 'bool'})
    add_error, add_text = is_tool_error(added)
    check, text = run_python(url, DIRTY_CHECK_PY % names)
    return (not add_error and check.get('dirty_after') is True and check.get('deleted') is True,
            'add error=%s dirty_after=%s deleted=%s %s' % (add_error, check.get('dirty_after'), check.get('deleted'),
                                                        add_text[:80] if add_error else ''))


def main():
    ap = argparse.ArgumentParser(description='E05 regression cases over MCP')
    ap.add_argument('--url')
    ap.add_argument('--engine')
    ap.add_argument('--project')
    args = ap.parse_args()
    session = None
    url = args.url
    if not url:
        from editor_session import EditorSession, free_port
        port = free_port(27160)
        os.makedirs(os.path.join(HERE, 'logs'), exist_ok=True)
        # Python on for the cases that set up assets; -RenderOffScreen because on UE 5.6 under -nullrhi an opened
        # Blueprint editor's window otherwise reaches FGenericWindow::GetRestoredDimensions, which is Fatal
        session = EditorSession(args.engine, args.project, port, os.path.join(HERE, 'logs', 'e05_editor.log'),
                                allow_python=True, extra_args=['-RenderOffScreen'])
        if not session.start():
            print('FAIL editor did not start')
            session.stop()
            return 1
        url = 'http://127.0.0.1:%d/mcp' % port
    failed = 0
    try:
        for fn in CASES:
            try:
                ok, detail = fn(url)
            except Exception as e:  # a crash closes the connection
                ok, detail = False, 'exception: %s' % e
            label = 'SKIP' if ok is None else ('PASS' if ok else 'FAIL')
            failed += label == 'FAIL'
            print('%s %s  %s' % (label, fn.__name__, detail))
        alive = True
        try:
            rpc(url, 'ping')
        except Exception:
            alive = False
        print('%s editor still answering after all cases' % ('PASS' if alive else 'FAIL'))
        failed += not alive
    finally:
        if session:
            session.stop()
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
