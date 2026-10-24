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
import subprocess
import sys
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

    def __init__(self):
        self.posts = []  # (headers dict, body bytes)
        self.posted = threading.Condition()
        self.release_final = threading.Event()
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

            def do_POST(self):
                body = self.rfile.read(int(self.headers.get('Content-Length', '0')))
                with editor.posted:
                    editor.posts.append(({k.lower(): v for k, v in self.headers.items()}, body))
                    editor.posted.notify_all()
                message = json.loads(body.decode('utf-8'))
                method = message.get('method')
                if 'id' not in message:
                    if method == 'notifications/http-error':
                        self._send(404, 'text/plain; charset=utf-8', b'Not Found')
                    else:
                        self._send(202, 'application/json', b'')
                elif method == 'test/echo':
                    reply = {'jsonrpc': '2.0', 'id': message['id'], 'result': {'echo': message['params']}}
                    self._send(200, 'application/json', json.dumps(reply, ensure_ascii=False).encode('utf-8'))
                elif method == 'tools/call':
                    self._stream(message)
                else:
                    self._send(404, 'text/plain; charset=utf-8', 'No route for /mcp: café'.encode('utf-8'))

            def _send(self, code, content_type, body):
                self.send_response(code)
                self.send_header('Content-Type', content_type)
                self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                self.wfile.write(body)

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

    def close(self):
        self.release_final.set()
        self.server.shutdown()
        self.server.server_close()


class BridgeProcess:
    """The bridge as a stdio child process; stdout lines are collected on a thread."""

    def __init__(self, port):
        env = {k: v for k, v in os.environ.items() if k not in ('PYTHONUTF8', 'PYTHONIOENCODING')}
        env['PYTHONUTF8'] = '0'
        self.proc = subprocess.Popen([sys.executable, '-I', BRIDGE, '--port', str(port)], stdin=subprocess.PIPE,
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

    def test_unparsable_line_gets_a_parse_error(self):
        self.bridge.proc.stdin.write(b'{"jsonrpc": "2.0", "id": \n')
        self.bridge.proc.stdin.flush()
        reply = self.bridge.read()
        self.assertIsNone(reply['id'])
        self.assertEqual(reply['error']['code'], -32700)


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
