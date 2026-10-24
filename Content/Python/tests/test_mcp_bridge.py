"""
Unit tests for the stdio MCP bridge (Content/Python/mcp_bridge/nodetocode_bridge.py). No editor needed:

    python -I -m unittest discover -s Content/Python/tests -p "test_mcp_bridge*.py"

The bridge runs as a child process, as an MCP client starts it, against a small stdlib HTTP server that plays the
editor's /mcp endpoint. The child gets PYTHONUTF8=0 and no PYTHONIOENCODING, so on Windows its stdio would use the
ANSI code page unless the bridge reconfigures it.
"""

import json
import os
import queue
import shutil
import subprocess
import sys
import tempfile
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
BRIDGE_DIR = os.path.normpath(os.path.join(HERE, '..', 'mcp_bridge'))
BRIDGE = os.path.join(BRIDGE_DIR, 'nodetocode_bridge.py')
sys.path.insert(0, BRIDGE_DIR)

import nodetocode_bridge  # noqa: E402

TIMEOUT = 10


class FakeEditor:
    """Plays the editor's /mcp endpoint: records each POST and answers by method."""

    def __init__(self, name='editor'):
        self.name = name
        self.posts = []  # (headers dict, body bytes)
        self.posted = threading.Condition()
        self.sessions = 0  # initialize answers with Mcp-Session-Id sess-<n>
        self.expired = set()  # session ids answered with 404, as the editor does for an ended session
        self.deleted = []  # session ids sent with DELETE
        self.release_final = threading.Event()
        self.cancelled = []  # requestIds named by notifications/cancelled
        self.held = {}  # id -> held tools/call (name 'slow'), until cancelled or released
        self.release_slow = threading.Event()
        editor = self

        class Handler(BaseHTTPRequestHandler):
            protocol_version = 'HTTP/1.0'  # the connection closes after each response, like the 5.8 stream

            def log_message(self, *args):
                pass

            def do_GET(self):
                if self.path == '/mcp/health':
                    self._send(200, 'application/json', b'{"status":"ok","service":"NodeToCode MCP Server"}')
                else:
                    self._send(404, 'text/plain', b'Not Found')

            def do_DELETE(self):
                with editor.posted:
                    editor.deleted.append(self.headers.get('Mcp-Session-Id'))
                    editor.posted.notify_all()
                self._send(204, 'application/json', b'')

            def do_POST(self):
                body = self.rfile.read(int(self.headers.get('Content-Length', '0')))
                with editor.posted:
                    editor.posts.append(({k.lower(): v for k, v in self.headers.items()}, body))
                    editor.posted.notify_all()
                message = json.loads(body.decode('utf-8'))
                method = message.get('method')
                session = self.headers.get('Mcp-Session-Id')
                if session and session in editor.expired:
                    self._send(404, 'application/json',
                               b'{"jsonrpc":"2.0","id":null,"error":{"code":-32001,"message":"Session not found"}}')
                elif method == 'initialize':
                    editor.sessions += 1
                    reply = {'jsonrpc': '2.0', 'id': message['id'],
                             'result': {'protocolVersion': message['params']['protocolVersion'], 'capabilities': {},
                                        'serverInfo': {'name': 'fake', 'version': '1'}}}
                    self._send(200, 'application/json', json.dumps(reply).encode('utf-8'),
                               {'Mcp-Session-Id': 'sess-%d' % editor.sessions})
                elif method == 'notifications/cancelled':
                    with editor.posted:
                        editor.cancelled.append(message['params']['requestId'])
                        editor.posted.notify_all()
                    self._send(202, 'application/json', b'')
                elif 'id' not in message:
                    if method == 'notifications/http-error':
                        self._send(404, 'text/plain; charset=utf-8', b'Not Found')
                    else:
                        self._send(202, 'application/json', b'')
                elif method == 'test/echo':
                    reply = {'jsonrpc': '2.0', 'id': message['id'], 'result': {'echo': message['params']}}
                    self._send(200, 'application/json', json.dumps(reply, ensure_ascii=False).encode('utf-8'))
                elif method == 'ping':
                    self._send(200, 'application/json', json.dumps({'jsonrpc': '2.0', 'id': message['id'],
                                                                    'result': {}}).encode('utf-8'))
                elif method == 'tools/call' and message['params']['name'] == 'slow':
                    self._hold(message)
                elif method == 'tools/call' and message['params']['name'] == 'quick':
                    reply = {'jsonrpc': '2.0', 'id': message['id'],
                             'result': {'content': [{'type': 'text', 'text': 'quick from %s' % editor.name}]}}
                    self._send(200, 'application/json', json.dumps(reply).encode('utf-8'))
                elif method == 'tools/call':
                    self._stream(message)
                else:
                    self._send(404, 'text/plain; charset=utf-8', 'No route for /mcp: café'.encode('utf-8'))

            def _send(self, code, content_type, body, headers=None):
                self.send_response(code)
                self.send_header('Content-Type', content_type)
                self.send_header('Content-Length', str(len(body)))
                for name, value in (headers or {}).items():
                    self.send_header(name, value)
                self.end_headers()
                self.wfile.write(body)

            def _hold(self, message):
                """A held long-running call (UE 5.6/5.7, or no progress token): one JSON response when it finishes,
                or 202 with no body when notifications/cancelled names it, as the editor does."""
                rid = message['id']
                with editor.posted:
                    editor.held[rid] = 'running'
                    editor.posted.notify_all()
                    editor.posted.wait_for(lambda: rid in editor.cancelled or editor.release_slow.is_set(), TIMEOUT)
                    cancelled = rid in editor.cancelled
                    editor.held[rid] = 'cancelled' if cancelled else 'done'
                    editor.posted.notify_all()
                if cancelled:
                    self._send(202, 'application/json', b'')
                else:
                    reply = {'jsonrpc': '2.0', 'id': rid, 'result': {'content': [{'type': 'text', 'text': 'slow'}]}}
                    self._send(200, 'application/json', json.dumps(reply).encode('utf-8'))

            def _stream(self, message):
                """The UE 5.8 shape: a comment, two progress notifications (the second split over two data lines),
                then the response, then close. The response waits until the test has seen the first progress."""
                token = message['params']['_meta']['progressToken']
                self.send_response(200)
                self.send_header('Content-Type', 'text/event-stream')
                self.send_header('Cache-Control', 'no-cache')
                self.end_headers()

                def event(text):
                    self.wfile.write(text.encode('utf-8'))
                    self.wfile.flush()

                event(': keep-alive comment\r\n\r\n')
                first = {'jsonrpc': '2.0', 'method': 'notifications/progress',
                         'params': {'progressToken': token, 'progress': 0.1, 'message': 'Collecting the Blueprint'}}
                event('event: message\r\ndata: %s\r\n\r\n' % json.dumps(first))
                editor.release_final.wait(TIMEOUT)
                event('event: message\r\ndata: {"jsonrpc":"2.0","method":"notifications/progress",\r\n'
                      'data: "params":{"progressToken":%s,"progress":0.5,"message":"Waiting (1 s) …"}}\r\n\r\n'
                      % json.dumps(token))
                final = {'jsonrpc': '2.0', 'id': message['id'],
                         'result': {'content': [{'type': 'text', 'text': 'void BP_Café::Run() {}'}]}}
                event('id: 3\r\nevent: message\r\ndata: %s\r\n\r\n' % json.dumps(final, ensure_ascii=False))

        self.server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        self.port = self.server.server_address[1]
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def wait_for_posts(self, count):
        with self.posted:
            self.posted.wait_for(lambda: len(self.posts) >= count, TIMEOUT)
            return list(self.posts)

    def wait_for(self, predicate):
        with self.posted:
            return self.posted.wait_for(predicate, TIMEOUT)

    def methods(self):
        with self.posted:
            return [json.loads(b.decode('utf-8')).get('method') for _, b in self.posts]

    def close(self):
        self.release_final.set()
        with self.posted:
            self.release_slow.set()
            self.posted.notify_all()
        self.server.shutdown()
        self.server.server_close()


class BridgeProcess:
    """The bridge as a stdio child process; stdout lines are collected on a thread."""

    def __init__(self, port=None, args=(), env_extra=None):
        env = {k: v for k, v in os.environ.items()
               if k not in ('PYTHONUTF8', 'PYTHONIOENCODING', 'NODETOCODE_PROJECT', 'NODETOCODE_DISCOVERY_DIR')}
        env['PYTHONUTF8'] = '0'
        env.update(env_extra or {})
        command = [sys.executable, '-I', BRIDGE] + (['--port', str(port)] if port else []) + list(args)
        self.proc = subprocess.Popen(command, stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
        self.lines = queue.Queue()
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        for raw in self.proc.stdout:
            self.lines.put(raw)

    def send(self, message):
        """Write one message as the client does: raw UTF-8 JSON (JSON.stringify does not escape non-ASCII)."""
        self.proc.stdin.write((json.dumps(message, ensure_ascii=False) + '\n').encode('utf-8'))
        self.proc.stdin.flush()

    def read_raw(self):
        return self.lines.get(timeout=TIMEOUT)

    def read(self):
        raw = self.read_raw()
        return json.loads(raw.decode('utf-8'))

    def close(self):
        if self.proc.stdin.closed:
            return
        try:
            self.proc.stdin.close()
            self.proc.wait(TIMEOUT)
        finally:
            if self.proc.poll() is None:
                self.proc.kill()
            self.stderr = self.proc.stderr.read().decode('utf-8', 'replace')
            self.proc.stdout.close()
            self.proc.stderr.close()


class BridgeTest(unittest.TestCase):
    def setUp(self):
        self.editor = FakeEditor()
        self.bridge = BridgeProcess(self.editor.port)

    def tearDown(self):
        self.editor.release_final.set()
        self.bridge.close()
        self.editor.close()

    def test_non_ascii_text_reaches_the_editor_and_the_client_unchanged(self):
        params = {'blueprint': 'BP_café', 'variable': 'あ日本', 'note': 'Ł 🙂'}
        self.bridge.send({'jsonrpc': '2.0', 'id': 1, 'method': 'test/echo', 'params': params})

        raw = self.bridge.read_raw()
        headers, body = self.editor.wait_for_posts(1)[0]
        for text in ('café', 'あ', '日本', 'Ł', '🙂'):
            self.assertIn(text.encode('utf-8'), body, 'forwarded body bytes')
        self.assertEqual(json.loads(body.decode('utf-8'))['params'], params)
        self.assertTrue(headers['content-type'].startswith('application/json'))

        # One UTF-8 line back to the client, with the text intact
        self.assertTrue(raw.endswith(b'\n') and not raw.endswith(b'\r\n'), raw)
        self.assertEqual(json.loads(raw.decode('utf-8')), {'jsonrpc': '2.0', 'id': 1, 'result': {'echo': params}})

    def test_streamed_response_forwards_progress_as_it_arrives_then_the_response(self):
        self.bridge.send({'jsonrpc': '2.0', 'id': 7, 'method': 'tools/call',
                          'params': {'name': 'translate-focused-blueprint', 'arguments': {},
                                     '_meta': {'progressToken': 42}}})

        # The first notification reaches stdout while the editor still holds the rest of the stream
        first = self.bridge.read()
        self.assertEqual(first['method'], 'notifications/progress')
        self.assertEqual(first['params']['progressToken'], 42)
        self.assertFalse(self.editor.release_final.is_set())
        self.editor.release_final.set()

        second = self.bridge.read()
        self.assertEqual(second['method'], 'notifications/progress')
        self.assertEqual(second['params'], {'progressToken': 42, 'progress': 0.5, 'message': 'Waiting (1 s) …'})

        final = self.bridge.read()
        self.assertEqual(final['id'], 7)
        self.assertEqual(final['result']['content'][0]['text'], 'void BP_Café::Run() {}')

        headers, _ = self.editor.wait_for_posts(1)[0]
        self.assertIn('text/event-stream', headers['accept'])
        self.assertIn('application/json', headers['accept'])
        self.assertTrue(self.bridge.lines.empty())

    def test_http_404_text_body_becomes_a_framed_json_rpc_error_with_the_request_id(self):
        self.bridge.send({'jsonrpc': '2.0', 'id': 'req-404', 'method': 'resources/unknown'})

        raw = self.bridge.read_raw()
        reply = json.loads(raw.decode('utf-8'))
        self.assertEqual(reply['id'], 'req-404')
        self.assertEqual(reply['error']['code'], -32603)
        self.assertEqual(reply['error']['data']['httpStatus'], 404)
        self.assertEqual(reply['error']['data']['body'], 'No route for /mcp: café')

    def test_notifications_get_no_reply(self):
        self.bridge.send({'jsonrpc': '2.0', 'method': 'notifications/initialized'})
        self.bridge.send({'jsonrpc': '2.0', 'method': 'notifications/http-error'})
        self.editor.wait_for_posts(2)
        self.bridge.send({'jsonrpc': '2.0', 'id': 3, 'method': 'test/echo', 'params': {}})

        # The first thing on stdout is the request's response
        self.assertEqual(self.bridge.read(), {'jsonrpc': '2.0', 'id': 3, 'result': {'echo': {}}})
        self.bridge.close()
        self.assertTrue(self.bridge.lines.empty())

    def initialize(self, rid=1):
        self.bridge.send({'jsonrpc': '2.0', 'id': rid, 'method': 'initialize',
                          'params': {'protocolVersion': '2025-11-25', 'capabilities': {},
                                     'clientInfo': {'name': 'bridge-test', 'version': '1'}}})
        self.assertEqual(self.bridge.read()['result']['protocolVersion'], '2025-11-25')

    def test_requests_carry_the_session_and_protocol_version_initialize_settled(self):
        self.initialize()
        self.bridge.send({'jsonrpc': '2.0', 'id': 2, 'method': 'test/echo', 'params': {}})
        self.assertEqual(self.bridge.read()['id'], 2)
        posts = self.editor.wait_for_posts(2)
        self.assertNotIn('mcp-session-id', posts[0][0])
        self.assertEqual(posts[1][0].get('mcp-session-id'), 'sess-1')
        self.assertEqual(posts[1][0].get('mcp-protocol-version'), '2025-11-25')

    def test_404_for_the_session_replays_initialize_and_resends_once(self):
        self.initialize()
        self.editor.expired.add('sess-1')  # the editor restarted, or the session was ended
        self.bridge.send({'jsonrpc': '2.0', 'id': 2, 'method': 'test/echo', 'params': {'n': 1}})

        # The client sees only the answer to its request, from the new session
        self.assertEqual(self.bridge.read(), {'jsonrpc': '2.0', 'id': 2, 'result': {'echo': {'n': 1}}})
        posts = self.editor.wait_for_posts(5)
        methods = [(json.loads(b.decode('utf-8')).get('method'), h.get('mcp-session-id')) for h, b in posts]
        self.assertEqual(methods, [('initialize', None), ('test/echo', 'sess-1'), ('initialize', None),
                                   ('notifications/initialized', 'sess-2'), ('test/echo', 'sess-2')])
        self.assertTrue(self.bridge.lines.empty())

    def test_closing_stdin_ends_the_session_with_delete(self):
        self.initialize()
        self.bridge.close()
        self.assertEqual(self.editor.deleted, ['sess-1'])

    def test_unparsable_line_gets_a_parse_error(self):
        self.bridge.proc.stdin.write(b'{"jsonrpc": "2.0", "id": \n')
        self.bridge.proc.stdin.flush()
        reply = self.bridge.read()
        self.assertIsNone(reply['id'])
        self.assertEqual(reply['error']['code'], -32700)

    def test_quick_call_and_ping_answer_while_a_long_call_runs_and_a_cancel_reaches_it(self):
        self.initialize()
        self.bridge.send({'jsonrpc': '2.0', 'id': 10, 'method': 'tools/call',
                          'params': {'name': 'slow', 'arguments': {}}})
        self.assertTrue(self.editor.wait_for(lambda: self.editor.held.get(10) == 'running'))

        # Both answer while the editor still holds call 10
        self.bridge.send({'jsonrpc': '2.0', 'id': 11, 'method': 'tools/call',
                          'params': {'name': 'quick', 'arguments': {}}})
        self.assertEqual(self.bridge.read()['id'], 11)
        self.bridge.send({'jsonrpc': '2.0', 'id': 12, 'method': 'ping'})
        self.assertEqual(self.bridge.read(), {'jsonrpc': '2.0', 'id': 12, 'result': {}})
        self.assertEqual(self.editor.held[10], 'running')

        # The cancel reaches the editor in the client's session while the call runs; nothing comes back for id 10
        self.bridge.send({'jsonrpc': '2.0', 'method': 'notifications/cancelled',
                          'params': {'requestId': 10, 'reason': 'User pressed Stop'}})
        self.assertTrue(self.editor.wait_for(lambda: self.editor.held.get(10) == 'cancelled'))
        cancel_posts = [h for h, b in self.editor.posts
                        if json.loads(b.decode('utf-8')).get('method') == 'notifications/cancelled']
        self.assertEqual(cancel_posts[0].get('mcp-session-id'), 'sess-1')
        self.bridge.send({'jsonrpc': '2.0', 'id': 13, 'method': 'test/echo', 'params': {}})
        self.assertEqual(self.bridge.read()['id'], 13)
        self.bridge.close()
        self.assertTrue(self.bridge.lines.empty(), 'no response or error for the cancelled request')
        self.assertEqual(self.editor.deleted, ['sess-1'])

    def test_streamed_call_cancelled_mid_stream_gets_no_error(self):
        self.bridge.send({'jsonrpc': '2.0', 'id': 7, 'method': 'tools/call',
                          'params': {'name': 'translate-focused-blueprint', 'arguments': {},
                                     '_meta': {'progressToken': 'p'}}})
        self.assertEqual(self.bridge.read()['method'], 'notifications/progress')
        self.bridge.send({'jsonrpc': '2.0', 'method': 'notifications/cancelled', 'params': {'requestId': 7}})
        self.assertTrue(self.editor.wait_for(lambda: 7 in self.editor.cancelled))
        self.editor.release_final.set()  # the fake still sends the rest; the bridge drops it
        self.bridge.send({'jsonrpc': '2.0', 'id': 8, 'method': 'test/echo', 'params': {}})
        self.assertEqual(self.bridge.read()['id'], 8)
        self.bridge.close()
        self.assertTrue(self.bridge.lines.empty())


class StubUpstream:
    """Records what the Bridge forwards; a request can be held before it is sent."""

    def __init__(self):
        self.forwarded = []
        self.gate = threading.Event()
        self.reached = threading.Event()

    def forward(self, body, message, emit, before_send=None, timeout=None):
        if before_send is not None:
            self.reached.set()
            self.gate.wait(TIMEOUT)
            if not before_send():
                return
        self.forwarded.append(message)
        if 'id' in message:
            emit({'jsonrpc': '2.0', 'id': message['id'], 'result': {}})


class BridgeDispatchTest(unittest.TestCase):
    def test_request_cancelled_before_it_was_sent_is_never_sent(self):
        upstream = StubUpstream()
        out = []
        bridge = nodetocode_bridge.Bridge(upstream, out.append)
        bridge.submit(json.dumps({'jsonrpc': '2.0', 'id': 'a', 'method': 'tools/call', 'params': {}}))
        self.assertTrue(upstream.reached.wait(TIMEOUT))
        bridge.submit(json.dumps({'jsonrpc': '2.0', 'method': 'notifications/cancelled',
                                  'params': {'requestId': 'a'}}))
        upstream.gate.set()
        bridge.drain()
        self.assertEqual(upstream.forwarded, [], 'neither the request nor its cancel reaches the editor')
        self.assertEqual(out, [])

    def test_ids_1_and_string_1_are_different_requests(self):
        self.assertNotEqual(nodetocode_bridge.id_key(1), nodetocode_bridge.id_key('1'))


def _dead_pid():
    proc = subprocess.Popen([sys.executable, '-I', '-c', 'pass'])
    proc.wait()
    return proc.pid


class DiscoveryTest(unittest.TestCase):
    """Editor selection from discovery entries (NODETOCODE_DISCOVERY_DIR points the bridge at a temp folder)."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix='n2c-bridge-')
        self.index = os.path.join(self.tmp, 'servers')
        os.makedirs(self.index)
        self.editors = []
        self.bridges = []
        self.sleepers = []

    def tearDown(self):
        for bridge in self.bridges:
            bridge.close()
        for editor in self.editors:
            editor.close()
        for sleeper in self.sleepers:
            sleeper.kill()
            sleeper.wait()
        shutil.rmtree(self.tmp, ignore_errors=True)

    def live_pid(self):
        sleeper = subprocess.Popen([sys.executable, '-I', '-c', 'import time; time.sleep(60)'])
        self.sleepers.append(sleeper)
        return sleeper.pid

    def editor(self, name, pid=None, engine='5.8.0'):
        """A fake editor plus its discovery entries (per-user index and the project's Saved file)."""
        editor = FakeEditor(name)
        self.editors.append(editor)
        pid = pid or self.live_pid()
        project_dir = os.path.join(self.tmp, name)
        os.makedirs(os.path.join(project_dir, 'Saved', 'NodeToCode'), exist_ok=True)
        entry = {'server': 'NodeToCode', 'port': editor.port, 'requestedPort': 27000,
                 'url': 'http://127.0.0.1:%d/mcp' % editor.port, 'pid': pid,
                 'projectFile': os.path.join(project_dir, name + '.uproject').replace(os.sep, '/'),
                 'projectName': name, 'engineVersion': engine, 'pluginVersion': '2.0.0',
                 'protocolVersions': ['2025-11-25'], 'startedAt': '2026-10-24T00:00:00Z'}
        for path in (os.path.join(self.index, '%d.json' % pid),
                     os.path.join(project_dir, 'Saved', 'NodeToCode', 'mcp-server.json')):
            with open(path, 'w', encoding='utf-8') as f:
                json.dump(entry, f)
        editor.entry = entry
        return editor

    def bridge(self, args=(), env=None):
        extra = {'NODETOCODE_DISCOVERY_DIR': self.index}
        extra.update(env or {})
        bridge = BridgeProcess(args=args, env_extra=extra)
        self.bridges.append(bridge)
        return bridge

    def call(self, bridge, rid):
        bridge.send({'jsonrpc': '2.0', 'id': rid, 'method': 'tools/call', 'params': {'name': 'quick', 'arguments': {}}})
        return bridge.read()

    def initialize(self, bridge, rid=1):
        bridge.send({'jsonrpc': '2.0', 'id': rid, 'method': 'initialize',
                     'params': {'protocolVersion': '2025-11-25', 'capabilities': {},
                                'clientInfo': {'name': 'bridge-test', 'version': '1'}}})
        return bridge.read()

    def text(self, reply):
        return reply['result']['content'][0]['text']

    def test_one_editor_is_used_without_a_selection(self):
        self.editor('Solo')
        bridge = self.bridge()
        self.assertIn('result', self.initialize(bridge))
        self.assertEqual(self.text(self.call(bridge, 2)), 'quick from Solo')

    def test_starts_before_the_editor_and_connects_once_it_runs(self):
        bridge = self.bridge()
        reply = self.initialize(bridge, 1)
        self.assertEqual(reply['id'], 1)
        self.assertEqual(reply['error']['code'], -32603)
        self.assertIn('No NodeToCode editor is running', reply['error']['message'])
        self.assertEqual(self.call(bridge, 2)['id'], 2)  # each request is answered, and the bridge keeps running
        self.assertIsNone(bridge.proc.poll())

        editor = self.editor('Late')
        self.assertIn('result', self.initialize(bridge, 3))
        self.assertEqual(self.text(self.call(bridge, 4)), 'quick from Late')
        headers = [h for h, b in editor.posts if json.loads(b.decode('utf-8')).get('method') == 'tools/call']
        self.assertEqual(headers[0].get('mcp-session-id'), 'sess-1')

    def test_two_editors_without_a_selection_is_an_error_listing_both(self):
        self.editor('Alpha', engine='5.8.0')
        self.editor('Beta', engine='5.6.1')
        bridge = self.bridge()
        reply = self.initialize(bridge)
        self.assertEqual(reply['id'], 1)
        message = reply['error']['message']
        self.assertIn('2 NodeToCode editors are running', message)
        self.assertIn('--project', message)
        self.assertIn('Alpha (UE 5.8.0', message)
        self.assertIn('Beta (UE 5.6.1', message)
        self.assertEqual(sorted(e['projectName'] for e in reply['error']['data']['editors']), ['Alpha', 'Beta'])
        self.assertTrue(all(not e.posts for e in self.editors), 'nothing is sent to either editor')

    def test_project_picks_the_editor_by_name_path_folder_or_environment(self):
        alpha = self.editor('Alpha')
        beta = self.editor('Beta')
        by_name = self.bridge(['--project', 'Beta'])
        self.initialize(by_name)
        self.assertEqual(self.text(self.call(by_name, 2)), 'quick from Beta')

        by_env = self.bridge(env={'NODETOCODE_PROJECT': alpha.entry['projectFile']})
        self.initialize(by_env)
        self.assertEqual(self.text(self.call(by_env, 2)), 'quick from Alpha')

        by_folder = self.bridge(['--project', os.path.dirname(beta.entry['projectFile'])])
        self.initialize(by_folder)
        self.assertEqual(self.text(self.call(by_folder, 2)), 'quick from Beta')

        unknown = self.bridge(['--project', 'Gamma'])
        reply = self.initialize(unknown)
        self.assertIn("No running NodeToCode editor has the project 'Gamma'", reply['error']['message'])
        self.assertIn('Alpha', reply['error']['message'])

    def test_entries_of_editors_that_are_gone_are_skipped(self):
        self.editor('Gone', pid=_dead_pid())
        self.editor('Live')
        bridge = self.bridge()
        self.initialize(bridge)
        self.assertEqual(self.text(self.call(bridge, 2)), 'quick from Live')

    def test_explicit_port_wins_over_discovery(self):
        self.editor('Alpha')
        beta = self.editor('Beta')
        bridge = self.bridge(['--port', str(beta.port), '--project', 'Alpha'])
        self.initialize(bridge)
        self.assertEqual(self.text(self.call(bridge, 2)), 'quick from Beta')

    def test_editor_restarted_on_another_port_gets_the_request_in_a_new_session(self):
        first = self.editor('Proj')
        bridge = self.bridge()
        self.initialize(bridge)
        self.assertEqual(self.text(self.call(bridge, 2)), 'quick from Proj')
        first.close()
        self.editors.remove(first)
        os.remove(os.path.join(self.index, '%d.json' % first.entry['pid']))
        second = self.editor('Proj')  # a new process on a new port

        self.assertEqual(self.text(self.call(bridge, 3)), 'quick from Proj')
        self.assertEqual(second.methods(), ['initialize', 'notifications/initialized', 'tools/call'])


class SseParserTest(unittest.TestCase):
    def parse(self, chunks):
        parser = nodetocode_bridge.SseParser()
        events = []
        for chunk in chunks:
            for line in nodetocode_bridge.SseParser.split_lines(chunk):
                event = parser.feed_line(line)
                if event:
                    events.append(event)
        return events

    def test_comments_ignored_and_data_lines_joined_with_newlines(self):
        events = self.parse([b': comment\r\n', b'\r\n', b'event: message\r\n', b'data: {"a":\r\n', b'data:1}\r\n',
                             b'id: 9\r\n', b'retry: 10\r\n', b'\r\n'])
        self.assertEqual(events, [('message', '{"a":\n1}')])

    def test_lf_and_cr_line_ends_and_default_event_type(self):
        self.assertEqual(self.parse([b'data: x\n', b'\n', b'data: y\r\rdata: z\n']), [('message', 'x'), ('message', 'y')])

    def test_event_without_data_and_partial_event_at_end_are_dropped(self):
        self.assertEqual(self.parse([b'event: ping\n', b'\n', b'data: tail\n']), [])


if __name__ == '__main__':
    unittest.main()
