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
        session = EditorSession(args.engine, args.project, port, os.path.join(HERE, 'logs', 'e05_editor.log'), allow_python=False)
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
