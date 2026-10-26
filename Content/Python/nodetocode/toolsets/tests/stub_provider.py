# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""A local Ollama endpoint for the translation tests (card 0176): NodeToCode sends its requests to 127.0.0.1, never to
a real provider. While a StubProvider is entered, NodeToCode's settings point Ollama at it and translations go to a
scratch folder; everything is put back on exit, with no settings change notified or saved."""

import json
import os
import shutil
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import unreal

# What the model writes: one translated graph, as NodeToCode's response JSON
_TRANSLATION = {'graphs': [{
    'graph_name': 'N2CToolsetFunction', 'graph_type': 'Function', 'graph_class': 'BP_N2CToolset',
    'code': {'graphDeclaration': 'void N2CToolsetFunction();', 'graphImplementation': '# translated by the stub',
             'implementationNotes': ''}}]}
REPLY = json.dumps({
    'model': 'n2c-stub', 'created_at': '2026-10-25T00:00:00Z',
    'message': {'role': 'assistant', 'content': json.dumps(_TRANSLATION)},
    'done_reason': 'stop', 'done': True, 'prompt_eval_count': 10, 'eval_count': 7}).encode('utf-8')


class StubProvider:
    """Answers POST /api/chat. mode: 'ok' (after delay seconds), 'error' (HTTP 500 with an Ollama error) or 'stall'
    (no answer until the stub stops). hits counts the requests received."""

    def __init__(self, mode='ok', delay=0.0):
        self.mode = mode
        self.delay = delay
        self.hits = 0
        self._release = threading.Event()
        self._server = None
        self._thread = None
        self._saved = None
        self.output_dir = None

    @property
    def endpoint(self):
        return 'http://127.0.0.1:%d' % self._server.server_address[1]

    def __enter__(self):
        stub = self

        class Handler(BaseHTTPRequestHandler):
            def do_POST(self):  # noqa: N802
                length = int(self.headers.get('Content-Length') or 0)
                self.rfile.read(length)
                stub.hits += 1
                try:
                    if stub.mode == 'stall':
                        stub._release.wait(120)
                        self._send(503, b'{"error":"stub stopped"}')
                    elif stub.mode == 'error':
                        self._send(500, b'{"error":"stub provider failure"}')
                    else:
                        if stub.delay:
                            stub._release.wait(stub.delay)
                        self._send(200, REPLY)
                except OSError:
                    pass  # the client went away (a cancelled request)

            def _send(self, code, body):
                self.send_response(code)
                self.send_header('Content-Type', 'application/json')
                self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def log_message(self, *args):
                pass

        self._server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        self._server.daemon_threads = True
        self._thread = threading.Thread(target=self._server.serve_forever, name='N2CStubProvider', daemon=True)
        self._thread.start()

        self.output_dir = tempfile.mkdtemp(prefix='N2CToolsetTranslate_')
        settings = unreal.get_default_object(unreal.N2CSettings)
        ollama = settings.get_editor_property('ollama_config')
        output = settings.get_editor_property('custom_translation_output_directory')
        self._saved = {
            'provider': settings.get_editor_property('provider'),
            'endpoint': ollama.get_editor_property('ollama_endpoint'),
            'output': output.get_editor_property('path'),
            'timeout': settings.get_editor_property('http_activity_timeout_seconds'),
        }
        self._apply(unreal.N2CLLMProvider.OLLAMA, self.endpoint, self.output_dir, 600.0)
        return self

    def __exit__(self, *exc):
        self._release.set()
        if self._saved:
            self._apply(self._saved['provider'], self._saved['endpoint'], self._saved['output'], self._saved['timeout'])
        self._server.shutdown()
        self._server.server_close()
        shutil.rmtree(self.output_dir, ignore_errors=True)
        return False

    @staticmethod
    def _apply(provider, endpoint, output_dir, timeout):
        never = unreal.PropertyAccessChangeNotifyMode.NEVER
        settings = unreal.get_default_object(unreal.N2CSettings)
        settings.set_editor_property('provider', provider, never)
        ollama = settings.get_editor_property('ollama_config')
        ollama.set_editor_property('ollama_endpoint', endpoint, never)
        settings.set_editor_property('ollama_config', ollama, never)
        output = settings.get_editor_property('custom_translation_output_directory')
        output.set_editor_property('path', output_dir, never)
        settings.set_editor_property('custom_translation_output_directory', output, never)
        settings.set_editor_property('http_activity_timeout_seconds', timeout, never)


def inside(path, folder):
    """True when path is inside folder"""
    path, folder = os.path.normcase(os.path.abspath(path)), os.path.normcase(os.path.abspath(folder))
    return os.path.commonpath([path, folder]) == folder
