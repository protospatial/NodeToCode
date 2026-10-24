"""An MCP session for the probe scripts' raw HTTP requests to a NodeToCode editor.

Since card 0109 the server keeps one session per client: every POST to /mcp other than initialize must carry the
Mcp-Session-Id an initialize returned (400 without it, 404 for an unknown or ended one). The probes post raw JSON-RPC,
so they get their session here: session_headers(port) initializes once per port (protocol 2025-11-25, then
notifications/initialized) and returns the headers to add; after a 404 (the editor on that port restarted, or the
session was ended) call it again with refresh=True and resend. Stdlib only.
"""
import http.client
import json
import threading

PROTOCOL_VERSION = '2025-11-25'
ACCEPT = 'application/json, text/event-stream'

_lock = threading.Lock()
_sessions = {}  # port -> session id


def _post(port, payload, headers, timeout):
    conn = http.client.HTTPConnection('127.0.0.1', port, timeout=timeout)
    try:
        conn.request('POST', '/mcp', body=json.dumps(payload).encode('utf-8'), headers=headers)
        r = conn.getresponse()
        r.read()
        return r.status, r.getheader('Mcp-Session-Id')
    finally:
        conn.close()


def open_session(port, client_name='n2c-mcp-probe', timeout=60, version=PROTOCOL_VERSION):
    """Initializes a new session at `version` on the editor at port and returns its Mcp-Session-Id (raises when
    there is none)"""
    status, session_id = _post(port, {'jsonrpc': '2.0', 'id': 0, 'method': 'initialize', 'params': {
        'protocolVersion': version, 'capabilities': {},
        'clientInfo': {'name': client_name, 'version': '1.0'}}},
        {'Content-Type': 'application/json', 'Accept': ACCEPT}, timeout)
    if status != 200 or not session_id:
        raise RuntimeError('initialize on port %d answered HTTP %s without an Mcp-Session-Id' % (port, status))
    _post(port, {'jsonrpc': '2.0', 'method': 'notifications/initialized'},
          {'Content-Type': 'application/json', 'Accept': ACCEPT, 'Mcp-Session-Id': session_id,
           'MCP-Protocol-Version': version}, timeout)
    return session_id


def session_headers(port, refresh=False):
    """{'Mcp-Session-Id', 'MCP-Protocol-Version'} for the editor at port, initializing a session when there is none
    (or refresh is set)"""
    with _lock:
        session_id = None if refresh else _sessions.get(port)
        if not session_id:
            session_id = open_session(port)
            _sessions[port] = session_id
    return {'Mcp-Session-Id': session_id, 'MCP-Protocol-Version': PROTOCOL_VERSION}


def end_session(port, timeout=10):
    """Ends the cached session on port with DELETE (best effort); returns the HTTP status or None"""
    with _lock:
        session_id = _sessions.pop(port, None)
    if not session_id:
        return None
    conn = http.client.HTTPConnection('127.0.0.1', port, timeout=timeout)
    try:
        conn.request('DELETE', '/mcp', headers={'Mcp-Session-Id': session_id, 'MCP-Protocol-Version': PROTOCOL_VERSION})
        r = conn.getresponse()
        r.read()
        return r.status
    except (OSError, http.client.HTTPException):
        return None
    finally:
        conn.close()


def url_port(url):
    """The port of an http://127.0.0.1:<port>/mcp URL"""
    from urllib.parse import urlsplit
    return urlsplit(url).port or 80
