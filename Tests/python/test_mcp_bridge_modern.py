# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
The stdio bridge and MCP 2026-07-28 (card 0210), on plain CPython. A request whose params._meta names
io.modelcontextprotocol/protocolVersion is a modern, stateless one: Content/Python/mcp_bridge/nodetocode_bridge.py sends
it with MCP-Protocol-Version, Mcp-Method and Mcp-Name (Base64 sentinel when needed) and no session, and its
notifications/cancelled closes the request's HTTP stream instead of being forwarded. Legacy traffic keeps its session.
The editor side is the NodeToCode.MCP.Modern spec.
"""

import base64
import http.server
import importlib.util
import json
import os
import threading
import time

import pytest

BRIDGE_PATH = os.path.join(os.path.dirname(__file__), '..', '..', 'Content', 'Python', 'mcp_bridge', 'nodetocode_bridge.py')
META = {'io.modelcontextprotocol/protocolVersion': '2026-07-28', 'io.modelcontextprotocol/clientCapabilities': {}}


@pytest.fixture(scope='module')
def bridge():
    spec = importlib.util.spec_from_file_location('n2c_stdio_bridge_modern_under_test', BRIDGE_PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def modern(request_id, method, **params):
    return {'jsonrpc': '2.0', 'id': request_id, 'method': method, 'params': dict(params, _meta=META)}


def test_modern_version_and_headers(bridge):
    assert bridge.modern_version(modern(1, 'tools/list')) == '2026-07-28'
    assert bridge.modern_version({'jsonrpc': '2.0', 'id': 1, 'method': 'tools/list', 'params': {'_meta': {'progressToken': 1}}}) is None
    assert bridge.modern_version({'jsonrpc': '2.0', 'id': 1, 'method': 'initialize'}) is None

    assert bridge.modern_headers(modern(1, 'tools/list'), '2026-07-28') == {
        'MCP-Protocol-Version': '2026-07-28', 'Mcp-Method': 'tools/list'}
    assert bridge.modern_headers(modern(1, 'tools/call', name='get-focused-blueprint'), '2026-07-28')['Mcp-Name'] == 'get-focused-blueprint'
    assert bridge.modern_headers(modern(1, 'resources/read', uri='nodetocode://x'), '2026-07-28')['Mcp-Name'] == 'nodetocode://x'

    # Not plain ASCII, edge spaces, or the sentinel itself: Base64 of the UTF-8
    for value in ('Hello, 世界', ' padded ', 'line1\nline2', '=?base64?literal?='):
        encoded = bridge.header_value(value)
        assert encoded.startswith('=?base64?') and encoded.endswith('?=')
        assert base64.b64decode(encoded[9:-2]).decode('utf-8') == value


class _Editor(http.server.BaseHTTPRequestHandler):
    """Records each POST's headers. A modern tools/call of 'slow' answers with a stream it keeps open, writing a
    keep-alive every 0.2 s, and records when the client closed it."""
    protocol_version = 'HTTP/1.1'

    def log_message(self, *args):
        pass

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers.get('Content-Length') or 0)))
        self.server.seen.append((body.get('method'), {k.lower(): v for k, v in self.headers.items()}))
        params = body.get('params') or {}
        if body.get('method') == 'tools/call' and params.get('name') == 'slow':
            self.send_response(200)
            self.send_header('Content-Type', 'text/event-stream')
            self.send_header('Connection', 'close')
            self.end_headers()
            self.server.stream_open.set()
            try:
                for _ in range(100):
                    self.wfile.write(b': keep-alive\r\n\r\n')
                    self.wfile.flush()
                    time.sleep(0.2)
            except OSError:
                self.server.stream_closed.set()
            return
        if 'id' not in body:
            self.send_response(202)
            self.send_header('Content-Length', '0')
            self.end_headers()
            return
        reply = json.dumps({'jsonrpc': '2.0', 'id': body['id'], 'result': {'resultType': 'complete'}}).encode()
        self.send_response(200)
        if body.get('method') == 'initialize':
            self.send_header('Mcp-Session-Id', 'session-1')
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(reply)))
        self.end_headers()
        self.wfile.write(reply)


@pytest.fixture
def editor():
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), _Editor)
    server.seen = []
    server.stream_open = threading.Event()
    server.stream_closed = threading.Event()
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    yield server
    server.shutdown()
    server.server_close()


def upstream_for(bridge, editor, tmp_path):
    url = 'http://127.0.0.1:%d/mcp' % editor.server_address[1]
    return bridge.Upstream(bridge.EditorLocator(url=url), 10, bridge.TokenSource(str(tmp_path / 'none'), env={}))


def test_a_modern_request_carries_its_headers_and_no_session(bridge, editor, tmp_path):
    upstream = upstream_for(bridge, editor, tmp_path)
    out = []

    # A legacy client opens a session first; the modern request after it still goes without one
    init = {'jsonrpc': '2.0', 'id': 1, 'method': 'initialize', 'params': {'protocolVersion': '2025-11-25'}}
    upstream.forward(json.dumps(init).encode(), init, out.append)
    for message in (modern(2, 'server/discover'), modern(3, 'tools/call', name='Tést', arguments={})):
        upstream.forward(json.dumps(message).encode(), message, out.append)

    (_, discover), (_, call) = editor.seen[1], editor.seen[2]
    assert discover['mcp-protocol-version'] == '2026-07-28' and discover['mcp-method'] == 'server/discover'
    assert 'mcp-session-id' not in discover and 'mcp-name' not in discover
    assert call['mcp-method'] == 'tools/call'
    assert base64.b64decode(call['mcp-name'][9:-2]).decode('utf-8') == 'Tést'
    assert [m['id'] for m in out] == [1, 2, 3]

    # The legacy session is still sent on legacy requests
    legacy = {'jsonrpc': '2.0', 'id': 4, 'method': 'tools/list'}
    upstream.forward(json.dumps(legacy).encode(), legacy, out.append)
    assert editor.seen[3][1].get('mcp-session-id') == 'session-1'


def test_a_modern_cancel_closes_the_stream_instead_of_being_forwarded(bridge, editor, tmp_path):
    out = []
    lane = bridge.Bridge(upstream_for(bridge, editor, tmp_path), emit=out.append)
    lane.submit(json.dumps(modern(9, 'tools/call', name='slow', arguments={})))
    assert editor.stream_open.wait(10), 'the editor opened the stream'

    lane.submit(json.dumps({'jsonrpc': '2.0', 'method': 'notifications/cancelled', 'params': {'requestId': 9}}))
    assert editor.stream_closed.wait(10), 'the editor saw the stream close'
    lane.drain()
    assert [m for m, _ in editor.seen] == ['tools/call'], 'notifications/cancelled was not forwarded'
    assert out == [], 'nothing was written for the cancelled request'
