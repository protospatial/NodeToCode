"""Live probes for NodeToCode's MCP transport findings, against a headless editor on a host project.

Stdlib only; run with `python -I`:

    python -I mcp_probe.py --engine G:/UE/UE_5.8 --project G:/.../NodeToCodeHost_5_8.uproject --label 5.8

Writes a JSON report (--report) with, per probe, the finding ids, a status (reproduced, fixed or skipped) and raw
evidence. Exits 0 only when every status matches expected.json for the label. See README.md.
"""
import argparse
import http.client
import json
import os
import socket
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from editor_session import EditorSession, free_port  # noqa: E402

TOOLS_LIST = json.dumps({'jsonrpc': '2.0', 'id': 1, 'method': 'tools/list'})


def post(port, body, headers, path='/mcp'):
    conn = http.client.HTTPConnection('127.0.0.1', port, timeout=60)
    try:
        conn.request('POST', path, body=body.encode('utf-8'), headers=headers)
        r = conn.getresponse()
        data = r.read().decode('utf-8', 'replace')
        return {'status': r.status, 'headers': {k.lower(): v for k, v in r.getheaders()}, 'body': data[:400]}
    except Exception as e:
        return {'status': None, 'error': str(e)}
    finally:
        conn.close()


def probe_p1(port, saved_dir):
    """Cross-origin text/plain POST, the request a web page can send without a CORS preflight."""
    headers = {'Origin': 'https://evil.example', 'Content-Type': 'text/plain'}
    listed = post(port, TOOLS_LIST, headers)
    marker = os.path.join(saved_dir, 'n2c_probe.txt')
    if os.path.exists(marker):
        os.remove(marker)
    code = "open(r'%s','w').write('probe')" % marker.replace('\\', '/')
    call = post(port, json.dumps({'jsonrpc': '2.0', 'id': 2, 'method': 'tools/call',
                                  'params': {'name': 'run-python', 'arguments': {'code': code}}}), headers)
    time.sleep(1)
    wrote = os.path.exists(marker)
    if wrote:
        os.remove(marker)
    served = listed.get('status') == 200 and '"tools"' in listed.get('body', '')
    acao = listed.get('headers', {}).get('access-control-allow-origin')
    status = 'reproduced' if (served or wrote or acao == '*') else 'fixed'
    return {'ids': ['mcp-transport-1', 'dx-docs-release-1', 'p0-1'], 'status': status,
            'evidence': {'tools_list': listed, 'run_python': call, 'marker_written': wrote}}


def probe_p2(port):
    """DNS rebinding: a request whose Host is not a loopback name."""
    r = post(port, TOOLS_LIST, {'Host': 'attacker.example', 'Content-Type': 'application/json'})
    status = 'reproduced' if r.get('status') == 200 else 'fixed'
    return {'ids': ['mcp-transport-1'], 'status': status, 'evidence': r}


def probe_p3(port):
    """A two-element JSON-RPC batch. Fixed means both answered, or the batch rejected with -32600."""
    batch = json.dumps([{'jsonrpc': '2.0', 'id': 1, 'method': 'tools/list'},
                        {'jsonrpc': '2.0', 'id': 2, 'method': 'ping'}])
    r = post(port, batch, {'Content-Type': 'application/json'})
    fixed = False
    try:
        parsed = json.loads(r.get('body', ''))
        if isinstance(parsed, list) and len(parsed) == 2:
            fixed = True
        elif isinstance(parsed, dict) and parsed.get('error', {}).get('code') == -32600:
            fixed = True
    except ValueError:
        pass
    return {'ids': ['mcp-transport-9'], 'status': 'fixed' if fixed else 'reproduced', 'evidence': r}


def probe_p5(port):
    """Whether the notification server (port+1) accepts a connection on a non-loopback local address."""
    addrs = set()
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            ip = info[4][0]
            if not ip.startswith('127.'):
                addrs.add(ip)
    except socket.gaierror:
        pass
    if not addrs:
        return {'ids': ['mcp-transport-14'], 'status': 'skipped', 'evidence': {'reason': 'no non-loopback IPv4 address'}}
    results = {}
    for ip in sorted(addrs):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.settimeout(2)
            results[ip] = s.connect_ex((ip, port + 1)) == 0
    status = 'reproduced' if any(results.values()) else 'fixed'
    return {'ids': ['mcp-transport-14'], 'status': status, 'evidence': {'accepted': results}}


def probe_p4(args, port, log_dir):
    """Port conflict: another process holds the MCP port; does the editor say it started anyway?

    A plain socket listener stands in for the second editor the finding describes (same effect on the bind,
    without copying the project)."""
    holder = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    holder.bind(('127.0.0.1', port))
    holder.listen(1)
    log_path = os.path.join(log_dir, 'p4_editor.log')
    try:
        s = EditorSession(args.engine, args.project, port, log_path, allow_python=False)
        s.start(timeout=90)  # health may answer from nobody; we read the log either way
        time.sleep(5)
        s.stop()
        text = s.log_text()
    finally:
        holder.close()
    claims = [l.strip()[:200] for l in text.splitlines() if 'MCP HTTP server started' in l or 'MCP HTTP server initialized' in l]
    failures = [l.strip()[:200] for l in text.splitlines() if 'Failed to start MCP' in l or 'port' in l.lower() and 'in use' in l.lower()]
    status = 'reproduced' if claims and not failures else ('fixed' if failures else 'skipped')
    return {'ids': ['mcp-transport-10'], 'status': status, 'evidence': {'success_lines': claims, 'failure_lines': failures}}


def main():
    ap = argparse.ArgumentParser(description='NodeToCode MCP transport probes')
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--label', required=True, help='engine label in expected.json, e.g. 5.8')
    ap.add_argument('--port', type=int, default=27150)
    ap.add_argument('--report', default=None)
    ap.add_argument('--skip-p4', action='store_true', help='skip the port-conflict probe (it launches a second editor)')
    args = ap.parse_args()

    log_dir = os.path.join(HERE, 'logs')
    os.makedirs(log_dir, exist_ok=True)
    port = free_port(args.port)
    saved_dir = os.path.join(os.path.dirname(os.path.abspath(args.project)), 'Saved')
    results = {}
    session = EditorSession(args.engine, args.project, port, os.path.join(log_dir, 'editor_%s.log' % args.label))
    try:
        if not session.start():
            results['session'] = {'status': 'skipped', 'evidence': {'reason': 'editor did not answer /mcp/health'}}
        else:
            results['P1'] = probe_p1(port, saved_dir)
            results['P2'] = probe_p2(port)
            results['P3'] = probe_p3(port)
            results['P5'] = probe_p5(port)
    finally:
        session.stop()
    if args.skip_p4:
        results['P4'] = {'ids': ['mcp-transport-10'], 'status': 'skipped', 'evidence': {'reason': '--skip-p4'}}
    else:
        results['P4'] = probe_p4(args, free_port(port + 2), log_dir)

    expected = json.load(open(os.path.join(HERE, 'expected.json'), encoding='utf-8')).get(args.label, {})
    mismatches = {k: {'expected': expected.get(k), 'actual': v['status']} for k, v in results.items()
                  if expected.get(k) != v['status']}
    report = {'label': args.label, 'port': port, 'results': results, 'mismatches': mismatches}
    out = args.report or os.path.join(log_dir, 'report_%s.json' % args.label)
    with open(out, 'w', encoding='utf-8') as fh:
        json.dump(report, fh, indent=1)
    print(json.dumps({k: v['status'] for k, v in results.items()}), 'mismatches:', json.dumps(mismatches))
    return 0 if not mismatches else 1


if __name__ == '__main__':
    sys.exit(main())
