# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
The runner behind the run-python MCP tool (card 0299).

C++ (FN2CMcpRunPythonTool) sends one statement with no '.py' in it:

    __import__('nodetocode._mcp_runner', fromlist=['run_code']).run_code('<call id>', '<base64 source>')

run_code decodes the source and compiles it unchanged (compile(source, '<run-python>', 'exec')), so line numbers,
multi-line strings and `from __future__` imports are the code's own. It runs in a fresh namespace
({'__name__': '__main__', '__builtins__': builtins}), never the editor console's globals, so nothing leaks between
calls. Its stdout and stderr are captured for the call. The answer is {success, data, error, traceback, output,
stderr}, serialized as strict JSON (no NaN or Infinity; UObjects as their path name, unreal.Name and unreal.Text as
str, sets as lists, structs by export_text) and handed to UN2CPythonBridge.submit_mcp_result(call_id, json). Nothing
is printed for the C++ side to scrape, so a script cannot spoof the answer.
"""

import base64
import builtins
import contextlib
import io
import json
import traceback

SOURCE_NAME = '<run-python>'


def _decode(b64_text):
    return base64.b64decode(b64_text.encode('ascii')).decode('utf-8')


def _json_default(value):
    """What json.dumps cannot write on its own: UE objects, names, texts, sets, structs; anything else is an error"""
    get_path_name = getattr(value, 'get_path_name', None)
    if callable(get_path_name):
        return get_path_name()
    type_name = type(value).__name__
    if type_name in ('Name', 'Text', 'FieldPath'):
        return str(value)
    if isinstance(value, (set, frozenset)):
        return list(value)
    if isinstance(value, (bytes, bytearray)):
        return base64.b64encode(bytes(value)).decode('ascii')
    export_text = getattr(value, 'export_text', None)
    if callable(export_text):
        return export_text()
    raise TypeError(f"Object of type {type_name} is not JSON serializable")


def _strict_dumps(payload):
    return json.dumps(payload, default=_json_default, allow_nan=False, ensure_ascii=False)


def _error_text(error):
    """The exception's message; a code that does not compile says SyntaxError (or IndentationError) first"""
    message = str(error) or type(error).__name__
    if isinstance(error, SyntaxError):
        return f"{type(error).__name__}: {message}"
    return message


def _user_traceback(error, own_files):
    """The traceback without this module's frames: the first frame shown is the code's own"""
    tb = error.__traceback__
    while tb is not None and tb.tb_frame.f_code.co_filename in own_files:
        tb = tb.tb_next
    return ''.join(traceback.format_exception(type(error), error, tb))


def _finish(payload):
    """The payload as strict JSON; a value JSON cannot hold turns the answer into a failure that says why"""
    try:
        return _strict_dumps(payload)
    except (TypeError, ValueError, OverflowError, RecursionError) as error:
        failed = {
            'success': False,
            'data': None,
            'error': f"result not JSON-serializable: {error}",
            'traceback': None,
            'output': payload.get('output', ''),
            'stderr': payload.get('stderr', ''),
        }
        return _strict_dumps(failed)


def execute_source(source):
    """Runs source in a fresh namespace; the answer dict (not yet serialized)"""
    namespace = {'__name__': '__main__', '__builtins__': builtins}
    out, err = io.StringIO(), io.StringIO()
    payload = {'success': False, 'data': None, 'error': None, 'traceback': None}
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        try:
            code = compile(source, SOURCE_NAME, 'exec', dont_inherit=True)
            exec(code, namespace)
            payload['success'] = True
            payload['data'] = namespace.get('result')
        except BaseException as error:  # SystemExit and KeyboardInterrupt too: the editor must keep running
            payload['error'] = _error_text(error)
            payload['traceback'] = _user_traceback(error, {__file__})
    payload['output'] = out.getvalue()
    payload['stderr'] = err.getvalue()
    return payload


def _submit(call_id, text):
    import unreal
    unreal.N2CPythonBridge.submit_mcp_result(call_id, text)


def run_code(call_id, b64_source):
    """run-python: decode, run, hand the strict JSON answer to C++ under call_id"""
    _submit(call_id, _finish(execute_source(_decode(b64_source))))

