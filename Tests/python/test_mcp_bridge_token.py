# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
The stdio bridge's bearer token (card 0183), on plain CPython: where Content/Python/mcp_bridge/nodetocode_bridge.py
finds the token, that it sends it as "Authorization: Bearer <token>" on every request (initialize replays included),
and that a token the editor regenerates is used at once because the file is read again for each request. The editor
side (401 without the token) is the NodeToCode.MCP.AuthToken spec and Tests/mcp_probe/live_bearer_token_0183.py.
"""

import http.server
import importlib.util
import json
import os
import threading

import pytest

BRIDGE_PATH = os.path.join(os.path.dirname(__file__), '..', '..', 'Content', 'Python', 'mcp_bridge', 'nodetocode_bridge.py')
TOKEN_A = 'a' * 64
TOKEN_B = 'b' * 64


@pytest.fixture(scope='module')
def bridge():
    spec = importlib.util.spec_from_file_location('n2c_stdio_bridge_under_test', BRIDGE_PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def write(path, text):
    with open(path, 'w', encoding='utf-8') as f:
        f.write(text)


def test_default_token_file_is_next_to_the_discovery_index(bridge, monkeypatch, tmp_path):
    monkeypatch.setenv('LOCALAPPDATA', str(tmp_path))
    monkeypatch.setenv('HOME', str(tmp_path))
    monkeypatch.delenv(bridge.DISCOVERY_DIR_ENV, raising=False)
    token_file = bridge.default_token_file()
    assert os.path.basename(token_file) == 'mcp-token'
    assert os.path.dirname(token_file) == os.path.dirname(bridge.discovery_index_dir())


def test_token_source_precedence(bridge, monkeypatch, tmp_path):
    monkeypatch.setattr(bridge, 'default_token_file', lambda: str(tmp_path / 'default-token'))
    explicit = tmp_path / 'explicit-token'

    assert bridge.TokenSource(env={}).get() is None, 'no file and no variable: no token'
    write(tmp_path / 'default-token', TOKEN_A + '\n')
    assert bridge.TokenSource(env={}).get() == TOKEN_A, "the editor's file, trimmed"
    assert bridge.TokenSource(env={bridge.TOKEN_ENV: ' ' + TOKEN_B + ' '}).get() == TOKEN_B, 'the variable wins over the file'
    write(explicit, TOKEN_B)
    assert bridge.TokenSource(str(explicit), env={bridge.TOKEN_ENV: TOKEN_A}).get() == TOKEN_B, '--token-file wins'
    assert bridge.TokenSource(str(tmp_path / 'missing'), env={bridge.TOKEN_ENV: TOKEN_A}).get() is None, \
        'a missing --token-file gives no token, not the variable'


def test_a_regenerated_token_is_read_again(bridge, monkeypatch, tmp_path):
    path = tmp_path / 'mcp-token'
    write(path, TOKEN_A)
    tokens = bridge.TokenSource(str(path), env={})
    assert tokens.get() == TOKEN_A
    write(path, TOKEN_B)
    assert tokens.get() == TOKEN_B


class _Editor(http.server.BaseHTTPRequestHandler):
    """A stand-in /mcp endpoint that wants Authorization: Bearer <server.token>, as the editor does with the setting on"""

    def log_message(self, *args):
        pass

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get('Content-Length') or 0))
        self.server.seen.append(self.headers.get('Authorization'))
        if self.headers.get('Authorization') != 'Bearer ' + self.server.token:
            reply = b'{"jsonrpc":"2.0","id":null,"error":{"code":-32600,"message":"Unauthorized"}}'
            self.send_response(401)
            self.send_header('WWW-Authenticate', 'Bearer')
        else:
            message = json.loads(body)
            reply = json.dumps({'jsonrpc': '2.0', 'id': message.get('id'), 'result': {}}).encode()
            self.send_response(200)
            self.send_header('Mcp-Session-Id', 'session-1')
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(reply)))
        self.end_headers()
        self.wfile.write(reply)


@pytest.fixture
def editor():
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), _Editor)
    server.token = TOKEN_A
    server.seen = []
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    yield server
    server.shutdown()
    server.server_close()


def forward(bridge, upstream, message):
    out = []
    upstream.forward(json.dumps(message).encode(), message, out.append)
    return out[0] if out else None


def test_the_bridge_sends_the_token_and_follows_a_regenerated_one(bridge, editor, tmp_path):
    path = tmp_path / 'mcp-token'
    write(path, TOKEN_A)
    url = 'http://127.0.0.1:%d/mcp' % editor.server_address[1]
    upstream = bridge.Upstream(bridge.EditorLocator(url=url), 10, bridge.TokenSource(str(path), env={}))

    init = {'jsonrpc': '2.0', 'id': 1, 'method': 'initialize', 'params': {}}
    assert 'result' in forward(bridge, upstream, init)
    assert editor.seen[-1] == 'Bearer ' + TOKEN_A

    # The editor regenerates its token: the old one is refused, and the bridge reads the new one from the file
    editor.token = TOKEN_B
    write(path, TOKEN_B)
    reply = forward(bridge, upstream, {'jsonrpc': '2.0', 'id': 2, 'method': 'tools/list'})
    assert 'result' in reply and editor.seen[-1] == 'Bearer ' + TOKEN_B


def test_a_401_is_a_json_rpc_error_with_the_request_id(bridge, editor, tmp_path):
    url = 'http://127.0.0.1:%d/mcp' % editor.server_address[1]
    upstream = bridge.Upstream(bridge.EditorLocator(url=url), 10, bridge.TokenSource(str(tmp_path / 'none'), env={}))
    reply = forward(bridge, upstream, {'jsonrpc': '2.0', 'id': 7, 'method': 'initialize', 'params': {}})
    assert editor.seen[-1] is None, 'no token: no Authorization header'
    assert reply['id'] == 7 and reply['error']['message'] == 'Unauthorized'
