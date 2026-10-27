#!/usr/bin/env python3
"""
NodeToCode MCP Bridge

A lightweight stdio-to-HTTP bridge that lets stdio-only MCP clients (like Claude Desktop) talk to the NodeToCode
UE plugin's Streamable HTTP MCP server.

- stdin and stdout are UTF-8 whatever the console code page, so non-ASCII text (asset names, run-python code,
  prompts) reaches the editor unchanged.
- Each stdin message is POSTed to /mcp with Accept: application/json, text/event-stream. A JSON response is written
  to stdout as one line; a text/event-stream response is read as it arrives: every JSON-RPC message in it (progress
  notifications, then the response) is written to stdout at once, and the response to the request ends the call.
- stdout carries only JSON-RPC messages, one per line, written under a lock. A transport failure or a non-JSON body
  (an HTTP error page) becomes a JSON-RPC error carrying the request's id, and nothing is written in reply to a
  notification. Logs go to stderr.
- One thread reads stdin. Every request except initialize runs on its own worker thread, so a long tools/call
  (translate-focused-blueprint) does not hold up others, and ping or notifications/cancelled pass at once.
  initialize and notifications are sent in order on the reader thread.
- Cancellation: notifications/cancelled is forwarded while the call runs (the editor stops the call and sends no
  response), and the bridge writes nothing more for that request id, as MCP asks. A request cancelled before it was
  sent is never sent.
- The editor keeps one MCP session per client (Mcp-Session-Id). The bridge sends the session its client's initialize
  opened on every request, with MCP-Protocol-Version. When the editor answers 404 (the session is gone: the editor
  restarted, or the session was ended), or the bridge moves to another editor, it replays the client's initialize and
  notifications/initialized to open a new session and resends the message once. When stdin closes it ends the
  session with DELETE.
- Which editor: --url or --port names one. Otherwise each running editor's MCP server writes a discovery entry
  (<user settings dir>/NodeToCode/servers/<pid>.json, plus <Project>/Saved/NodeToCode/mcp-server.json); entries of
  editors that are gone are skipped. One editor: that one. Several: --project (or the NODETOCODE_PROJECT environment
  variable) picks by project name, .uproject path or project folder, and without it every request gets an error
  listing the running editors. The bridge starts without an editor and looks again on every request until one
  answers, so it survives an editor that is still loading or restarts.
- Bearer token (card 0183): when the editor has Require MCP Bearer Token on, /mcp needs "Authorization: Bearer
  <token>". The bridge sends the token from --token-file, else the NODETOCODE_MCP_TOKEN environment variable, else the
  per-user file the editor writes (<user settings dir>/NodeToCode/mcp-token), read again for every request so a
  regenerated token is used at once. With no token it sends no header (the editor's default accepts that).
- MCP 2026-07-28 (card 0210): a request whose params._meta names io.modelcontextprotocol/protocolVersion (a modern,
  stateless client, which probes with server/discover) is sent without a session, with the headers that revision
  requires: MCP-Protocol-Version (the _meta version), Mcp-Method and, for tools/call, prompts/get and resources/read,
  Mcp-Name (Base64 sentinel when the name is not plain ASCII). Its notifications/cancelled is not forwarded: the bridge
  closes the request's HTTP stream, which is how 2026-07-28 cancels over HTTP. A legacy client keeps its session.

Zero external dependencies - uses only the Python standard library.

Usage:
    python nodetocode_bridge.py [--project NAME_OR_PATH] [--port PORT [--host HOST]] [--url URL]
                                [--token-file PATH] [--timeout SECONDS] [--debug]
"""

import argparse
import base64
import json
import os
import socket
import sys
import threading
import urllib.error
import urllib.request
from typing import Any, Callable, Dict, List, Optional, Tuple

# Configuration
DEFAULT_HOST = "127.0.0.1"
ACCEPT = "application/json, text/event-stream"
PROJECT_ENV = "NODETOCODE_PROJECT"
DISCOVERY_DIR_ENV = "NODETOCODE_DISCOVERY_DIR"
TOKEN_ENV = "NODETOCODE_MCP_TOKEN"
NOTIFICATION_TIMEOUT = 30.0  # seconds; notifications are sent on the stdin reader thread
CANCEL_RESEND_DELAY = 1.0  # seconds; a cancel that overtook its request in the editor is sent once more

# MCP 2026-07-28 per-request metadata (basic/index, "_meta")
META_PROTOCOL_VERSION = "io.modelcontextprotocol/protocolVersion"
NAME_HEADER_FIELDS = {"tools/call": "name", "prompts/get": "name", "resources/read": "uri"}

# JSON-RPC error codes
PARSE_ERROR = -32700
INTERNAL_ERROR = -32603

# Global debug flag
debug_mode = False

# One message per stdout line, whichever thread writes it
_stdout_lock = threading.Lock()

# Plain HTTP to the loopback editor: never through a proxy from the environment or the Windows settings
_opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def log_debug(message: str) -> None:
    """Log debug message to stderr."""
    if debug_mode:
        print(f"[NodeToCode-Bridge] {message}", file=sys.stderr, flush=True)


def log_info(message: str) -> None:
    print(f"[NodeToCode-Bridge] {message}", file=sys.stderr, flush=True)


def log_error(message: str) -> None:
    """Log error message to stderr."""
    print(f"[NodeToCode-Bridge ERROR] {message}", file=sys.stderr, flush=True)


def configure_stdio() -> None:
    """Read and write UTF-8 on stdio whatever the locale. Without this, Python on Windows decodes piped stdin with
    the ANSI code page (cp1252), so 'é' sent by the client reaches the editor as 'Ã©'.

    stdin keeps undecodable bytes as surrogates (Bridge.submit answers such a line with a parse error instead of
    stopping the loop); stdout writes '\\n' line ends and escapes a lone surrogate as a JSON '\\udxxx' escape."""
    for stream, errors in ((sys.stdin, "surrogateescape"), (sys.stdout, "backslashreplace"),
                           (sys.stderr, "backslashreplace")):
        if hasattr(stream, "reconfigure"):
            if stream is sys.stdout:
                stream.reconfigure(encoding="utf-8", errors=errors, newline="\n")
            else:
                stream.reconfigure(encoding="utf-8", errors=errors)


def write_message(message: Any) -> None:
    """Write one JSON-RPC message (or batch) to stdout as a single line."""
    line = json.dumps(message, ensure_ascii=False, separators=(",", ":"))
    with _stdout_lock:
        sys.stdout.write(line + "\n")
        sys.stdout.flush()
    log_debug(f"Sent: {line[:200]}")


def error_response(request_id: Any, code: int, message: str, data: Any = None) -> dict:
    """A JSON-RPC error response."""
    error = {"code": code, "message": message}
    if data is not None:
        error["data"] = data
    return {"jsonrpc": "2.0", "id": request_id, "error": error}


def is_request(message: Any) -> bool:
    """True for a JSON-RPC request (a method and an id member), the only kind of message that gets a response. An
    "id": null counts: the server refuses it with an Invalid Request error (-32600) the client must see."""
    return isinstance(message, dict) and "method" in message and "id" in message


def modern_version(message: Any) -> Optional[str]:
    """The 2026-07-28 protocol version a message names in params._meta, or None for a legacy message"""
    params = message.get("params") if isinstance(message, dict) else None
    meta = params.get("_meta") if isinstance(params, dict) else None
    version = meta.get(META_PROTOCOL_VERSION) if isinstance(meta, dict) else None
    return version if isinstance(version, str) else None


def header_value(value: str) -> str:
    """A value as an Mcp-Name header carries it: as is when it is plain visible ASCII without edge spaces, else the
    2026-07-28 Base64 sentinel =?base64?<Base64 of the UTF-8>?= (also for a plain value that looks like the sentinel)"""
    plain = all(0x20 <= ord(ch) <= 0x7E for ch in value) and value == value.strip()
    if plain and not (value.startswith("=?base64?") and value.endswith("?=")):
        return value
    return "=?base64?" + base64.b64encode(value.encode("utf-8")).decode("ascii") + "?="


def modern_headers(message: dict, version: str) -> dict:
    """MCP-Protocol-Version, Mcp-Method and (for tools/call, prompts/get, resources/read) Mcp-Name of a modern message"""
    headers = {"MCP-Protocol-Version": version}
    method = message.get("method")
    if isinstance(method, str):
        headers["Mcp-Method"] = method
        field = NAME_HEADER_FIELDS.get(method)
        params = message.get("params") if isinstance(message.get("params"), dict) else {}
        if field and isinstance(params.get(field), str):
            headers["Mcp-Name"] = header_value(params[field])
    return headers


def close_response(resp: Any) -> None:
    """Closes an open HTTP response from another thread: shuts its socket down, so the editor sees the stream close
    and a reader blocked on it wakes up"""
    try:
        sock = resp.fp.raw._sock  # http.client keeps the socket behind its buffered reader
        sock.shutdown(socket.SHUT_RDWR)
    except Exception:
        pass
    try:
        resp.close()
    except Exception:
        pass


def batch_has_request(message: Any) -> bool:
    return isinstance(message, list) and any(is_request(m) for m in message)


def id_key(request_id: Any) -> str:
    """A hashable key for a JSON-RPC id that keeps 1 and "1" apart."""
    return json.dumps(request_id, sort_keys=True)


# ---------------------------------------------------------------------------------------------------------------------
# Finding the editor
# ---------------------------------------------------------------------------------------------------------------------

def user_settings_dir() -> str:
    """FPlatformProcess::UserSettingsDir(): %LOCALAPPDATA% on Windows, ~/Library/Application Support/Epic on macOS and
    ~/.config/Epic on Linux."""
    home = os.path.expanduser("~")
    if os.name == "nt":
        return os.environ.get("LOCALAPPDATA") or os.path.join(home, "AppData", "Local")
    if sys.platform == "darwin":
        return os.path.join(home, "Library", "Application Support", "Epic")
    return os.path.join(home, ".config", "Epic")


def discovery_index_dir() -> str:
    """The per-user folder of discovery entries: <user settings dir>/NodeToCode/servers. NODETOCODE_DISCOVERY_DIR
    replaces the whole path."""
    override = os.environ.get(DISCOVERY_DIR_ENV)
    if override:
        return override
    return os.path.join(user_settings_dir(), "NodeToCode", "servers")


def default_token_file() -> str:
    """The editor's bearer token file: <user settings dir>/NodeToCode/mcp-token (FN2CMcpAuthToken)"""
    return os.path.join(user_settings_dir(), "NodeToCode", "mcp-token")


class TokenSource:
    """The bearer token the bridge sends: the --token-file file, else NODETOCODE_MCP_TOKEN, else the editor's per-user
    token file. A file is read on every call, so a token the editor regenerated is used at once; None when there is
    none (then no header is sent)."""

    def __init__(self, token_file: Optional[str] = None, env: Optional[Dict[str, str]] = None) -> None:
        self.explicit_file = token_file
        self.env = os.environ if env is None else env

    def _env_token(self) -> str:
        return "" if self.explicit_file else (self.env.get(TOKEN_ENV) or "").strip()

    def describe(self) -> str:
        """Where the token comes from, for log lines (never the token itself)"""
        if self._env_token():
            return f"the {TOKEN_ENV} environment variable"
        return self.explicit_file or default_token_file()

    def get(self) -> Optional[str]:
        value = self._env_token()
        if value:
            return value
        path = self.explicit_file or default_token_file()
        try:
            with open(path, "r", encoding="utf-8-sig") as f:
                token = f.read().strip()
        except OSError:
            if self.explicit_file:
                log_error(f"Cannot read the token file {path}")
            return None
        return token or None


def pid_alive(pid: int) -> bool:
    """Whether a process with this id is running (best effort: a reused id counts as running)."""
    if not isinstance(pid, int) or pid <= 0:
        return False
    if os.name == "nt":
        import ctypes
        from ctypes import wintypes
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.OpenProcess.restype = wintypes.HANDLE
        kernel32.OpenProcess.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
        kernel32.GetExitCodeProcess.argtypes = (wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD))
        kernel32.CloseHandle.argtypes = (wintypes.HANDLE,)
        handle = kernel32.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
        if not handle:
            return ctypes.get_last_error() == 5  # ERROR_ACCESS_DENIED: it exists, under another user
        try:
            code = wintypes.DWORD()
            if not kernel32.GetExitCodeProcess(handle, ctypes.byref(code)):
                return True
            return code.value == 259  # STILL_ACTIVE
        finally:
            kernel32.CloseHandle(handle)
    try:
        os.kill(pid, 0)
    except PermissionError:
        return True
    except OSError:
        return False
    return True


def _read_entry(path: str) -> Optional[dict]:
    try:
        with open(path, "r", encoding="utf-8-sig") as f:
            entry = json.load(f)
    except (OSError, ValueError):
        return None
    if not isinstance(entry, dict):
        return None
    pid = entry.get("pid")
    if isinstance(pid, float) and pid.is_integer():
        pid = int(pid)
    url = entry.get("url")
    if not isinstance(url, str) or not url:
        port = entry.get("port")
        if not isinstance(port, (int, float)) or not port:
            return None
        url = f"http://{DEFAULT_HOST}:{int(port)}/mcp"
    entry["pid"] = pid
    entry["url"] = url
    entry["discoveryFile"] = path
    return entry


def _project_folder(selector: str) -> Optional[str]:
    """The project folder a --project value names, when it is a path (a .uproject file or a folder)."""
    if selector.lower().endswith(".uproject"):
        return os.path.dirname(os.path.abspath(selector))
    if os.path.isdir(selector):
        return os.path.abspath(selector)
    return None


def _norm(path: str) -> str:
    return os.path.normcase(os.path.normpath(os.path.abspath(path)))


def entry_matches(entry: dict, selector: str) -> bool:
    """Whether a discovery entry is the project a --project value names: its name (with or without .uproject), its
    .uproject path or its folder."""
    selector = selector.strip()
    name = str(entry.get("projectName") or "")
    project_file = str(entry.get("projectFile") or "")
    bare = selector[:-len(".uproject")] if selector.lower().endswith(".uproject") else selector
    if name and bare.lower() == name.lower():
        return True
    if project_file:
        if os.path.basename(project_file).lower() == os.path.basename(selector).lower() and (
                os.path.basename(selector) == selector):
            return True
        target = _norm(selector)
        if target in (_norm(project_file), _norm(os.path.dirname(project_file))):
            return True
    return False


def describe_entry(entry: dict) -> str:
    name = entry.get("projectName") or "?"
    parts = [f"UE {entry.get('engineVersion') or '?'}", entry.get("url") or "?", f"pid {entry.get('pid')}"]
    if entry.get("projectFile"):
        parts.append(str(entry["projectFile"]))
    return f"{name} ({', '.join(parts)})"


def summarize_entry(entry: dict) -> dict:
    return {k: entry.get(k) for k in ("projectName", "projectFile", "url", "port", "pid", "engineVersion",
                                      "pluginVersion")}


def discover_editors(project: Optional[str] = None, index_dir: Optional[str] = None,
                     alive: Callable[[int], bool] = pid_alive) -> List[dict]:
    """The running editors' discovery entries: the per-user index, plus the project's own file when project is a
    path. Entries whose process is gone are skipped; one entry per process."""
    index_dir = index_dir or discovery_index_dir()
    paths: List[str] = []
    try:
        for name in sorted(os.listdir(index_dir)):
            if name.lower().endswith(".json") and name[:-5].isdigit():
                paths.append(os.path.join(index_dir, name))
    except OSError:
        pass
    folder = _project_folder(project) if project else None
    if folder:
        paths.append(os.path.join(folder, "Saved", "NodeToCode", "mcp-server.json"))

    entries: List[dict] = []
    seen = set()
    for path in paths:
        if not os.path.isfile(path):
            continue
        entry = _read_entry(path)
        if entry is None:
            log_debug(f"Ignoring unreadable discovery entry {path}")
            continue
        pid = entry.get("pid")
        if pid in seen:
            continue
        if not alive(pid):
            log_debug(f"Ignoring the discovery entry of an editor that is gone: {path}")
            continue
        seen.add(pid)
        entries.append(entry)
    return entries


class EditorNotFound(Exception):
    """No editor to send to; the message is the JSON-RPC error's message."""

    def __init__(self, message: str, editors: Optional[List[dict]] = None) -> None:
        super().__init__(message)
        self.editors = editors or []


class EditorLocator:
    """Picks the editor's /mcp URL: the explicit --url/--port, else the discovery entries filtered by --project."""

    def __init__(self, url: Optional[str] = None, project: Optional[str] = None,
                 index_dir: Optional[str] = None) -> None:
        self.explicit_url = url
        self.project = project or None
        self.index_dir = index_dir

    @property
    def is_explicit(self) -> bool:
        return bool(self.explicit_url)

    def locate(self) -> Tuple[str, Optional[dict]]:
        """(url, discovery entry or None). Raises EditorNotFound."""
        if self.explicit_url:
            return self.explicit_url, None
        index_dir = self.index_dir or discovery_index_dir()
        editors = discover_editors(self.project, index_dir)
        hint = (f"choose one with --project <project name, .uproject path or folder> (or the {PROJECT_ENV} "
                "environment variable), or --port/--url")
        if self.project:
            matches = [e for e in editors if entry_matches(e, self.project)]
            if len(matches) == 1:
                return matches[0]["url"], matches[0]
            if not matches:
                running = "; ".join(describe_entry(e) for e in editors) or "none"
                raise EditorNotFound(f"No running NodeToCode editor has the project '{self.project}'. Running editors: "
                                     f"{running}", editors)
            raise EditorNotFound(f"{len(matches)} NodeToCode editors have the project '{self.project}'; pass --port or "
                                 f"--url to choose: " + "; ".join(describe_entry(e) for e in matches), matches)
        if len(editors) == 1:
            return editors[0]["url"], editors[0]
        if not editors:
            raise EditorNotFound("No NodeToCode editor is running: no MCP server discovery entry in "
                                 f"{index_dir}. Open the project in the Unreal Editor with NodeToCode's MCP server "
                                 "enabled (Editor Preferences > Plugins > Node to Code (User)), or pass --port/--url.")
        raise EditorNotFound(f"{len(editors)} NodeToCode editors are running; {hint}: "
                             + "; ".join(describe_entry(e) for e in editors), editors)


# ---------------------------------------------------------------------------------------------------------------------
# Talking to the editor
# ---------------------------------------------------------------------------------------------------------------------

class SseParser:
    """Incremental text/event-stream parser (WHATWG HTML, 'Parsing an event stream').

    Feed it lines without their terminators. An empty line dispatches the event: its data lines joined with '\\n'.
    Comment lines (starting with ':') are ignored, as are the id and retry fields. A partial event at the end of the
    stream is dropped."""

    def __init__(self) -> None:
        self._data: List[str] = []
        self._event = ""

    def feed_line(self, line: str) -> Optional[Tuple[str, str]]:
        """Returns (event type, data) when this line ends an event that has data, else None."""
        if line == "":
            if not self._data:
                self._event = ""
                return None
            event = (self._event or "message", "\n".join(self._data))
            self._data, self._event = [], ""
            return event
        if line.startswith(":"):
            return None
        field, colon, value = line.partition(":")
        if colon and value.startswith(" "):
            value = value[1:]
        if field == "data":
            self._data.append(value)
        elif field == "event":
            self._event = value
        return None

    @staticmethod
    def split_lines(raw: bytes) -> List[str]:
        """The SSE lines in one readline() chunk: CRLF, LF and CR all end a line; the stream is UTF-8."""
        text = raw.decode("utf-8", "replace")
        if text.endswith("\n"):
            text = text[:-1]
            if text.endswith("\r"):
                text = text[:-1]
        return text.split("\r")


class Upstream:
    """The editor's /mcp endpoint plus the session state shared by all requests."""

    def __init__(self, locator: EditorLocator, timeout: Optional[float] = None,
                 tokens: Optional[TokenSource] = None) -> None:
        self.locator = locator
        self.tokens = tokens if tokens is not None else TokenSource()
        # None: wait as long as the editor takes. A held translate-focused-blueprint call (UE 5.6/5.7, or no
        # progress token) sends nothing until the translation is done.
        self.timeout = timeout
        self._lock = threading.Lock()
        self._locate_lock = threading.Lock()
        self.url: Optional[str] = None
        self.session_id: Optional[str] = None
        self.protocol_version: Optional[str] = None
        # The client's initialize request, replayed to open a new session after a 404 or a move to another editor
        self._initialize_body: Optional[bytes] = None
        self._reinitialize_lock = threading.Lock()

    def _target(self) -> str:
        """The editor's URL, found again after a connection failure. Raises EditorNotFound."""
        with self._locate_lock:
            with self._lock:
                if self.url:
                    return self.url
            url, entry = self.locator.locate()
            with self._lock:
                self.url = url
            if entry is not None:
                log_info(f"Using the NodeToCode editor {describe_entry(entry)}")
            else:
                log_debug(f"Using the NodeToCode editor at {url}")
            return url

    def _lost(self, url: str) -> None:
        """The editor at url did not answer: look for an editor again on the next request (a restarted editor may
        have another port), and open a new session there."""
        if self.locator.is_explicit:
            return
        with self._lock:
            if self.url == url:
                self.url = None
                self.session_id = None

    def _base_headers(self) -> dict:
        """Content-Type, Accept and, when there is a token, Authorization"""
        headers = {"Content-Type": "application/json", "Accept": ACCEPT}
        token = self.tokens.get()
        if token:
            headers["Authorization"] = f"Bearer {token}"
        return headers

    def _headers(self) -> dict:
        headers = self._base_headers()
        with self._lock:
            if self.session_id:
                headers["Mcp-Session-Id"] = self.session_id
            if self.protocol_version:
                headers["MCP-Protocol-Version"] = self.protocol_version
        return headers

    def _note_headers(self, headers: Any) -> None:
        session_id = headers.get("Mcp-Session-Id") if headers is not None else None
        if session_id:
            with self._lock:
                self.session_id = session_id

    def _note_message(self, message: Any, method: Optional[str]) -> None:
        """Remember the protocol version an initialize response settled on, for the MCP-Protocol-Version header."""
        if method == "initialize" and isinstance(message, dict) and isinstance(message.get("result"), dict):
            version = message["result"].get("protocolVersion")
            if isinstance(version, str) and version:
                with self._lock:
                    self.protocol_version = version

    def _reinitialize(self, url: str, stale_session: Optional[str]) -> bool:
        """Opens a new session by replaying the client's initialize (and notifications/initialized), after the
        editor answered 404 for stale_session or when there is no session at url yet. True when a session other
        than stale_session is now in use."""
        with self._reinitialize_lock:
            with self._lock:
                if self.session_id and self.session_id != stale_session:
                    return True  # another thread already opened a new one
                init_body = self._initialize_body
                self.session_id = None
            if init_body is None:
                return False
            log_debug("Replaying initialize to open a new session")
            headers = self._base_headers()
            try:
                req = urllib.request.Request(url, data=init_body, headers=headers, method="POST")
                with _opener.open(req, timeout=30) as resp:
                    self._note_headers(resp.headers)
                    content_type = (resp.headers.get("Content-Type") or "").lower()
                    text = resp.read().decode("utf-8", "replace")
                if content_type.startswith("text/event-stream"):
                    text = "\n".join(line[5:].strip() for line in text.splitlines() if line.startswith("data:"))
                try:
                    self._note_message(json.loads(text), "initialize")
                except ValueError:
                    pass
                notify = urllib.request.Request(url, data=b'{"jsonrpc":"2.0","method":"notifications/initialized"}',
                                                headers=self._headers(), method="POST")
                with _opener.open(notify, timeout=30) as resp:
                    resp.read()
            except Exception as e:  # HTTPError, URLError, timeouts
                log_error(f"Opening an MCP session at {url} failed: {e}")
                return False
            with self._lock:
                return bool(self.session_id) and self.session_id != stale_session

    def end_session(self) -> None:
        """Ends the session with DELETE (best effort), when the client is done."""
        headers = self._headers()
        with self._lock:
            url = self.url
        if "Mcp-Session-Id" not in headers or not url:
            return
        try:
            req = urllib.request.Request(url, headers={k: v for k, v in headers.items() if k != "Content-Type"},
                                         method="DELETE")
            with _opener.open(req, timeout=5) as resp:
                resp.read()
            log_debug("Ended the MCP session")
        except Exception as e:
            log_debug(f"Ending the MCP session failed: {e}")

    def forward(self, body: bytes, message: Any, emit: Callable[[Any], None],
                before_send: Optional[Callable[[], bool]] = None, timeout: Optional[float] = None,
                _moved: bool = False, on_open: Optional[Callable[[Any], None]] = None) -> None:
        """POST one client message and emit every JSON-RPC message meant for the client. before_send runs once,
        just before the message is first sent; when it returns False the message is not sent. on_open gets the HTTP
        response once it is open (so a 2026-07-28 cancel can close it)."""
        wants_reply = is_request(message) or batch_has_request(message)
        request_id = message.get("id") if isinstance(message, dict) else None
        method = message.get("method") if isinstance(message, dict) else None
        version = modern_version(message)

        try:
            url = self._target()
        except EditorNotFound as e:
            log_error(str(e))
            if wants_reply:
                data = {"editors": [summarize_entry(x) for x in e.editors]} if e.editors else None
                emit(error_response(request_id, INTERNAL_ERROR, str(e), data))
            return

        if version is not None:
            pass  # 2026-07-28: stateless, no session to open or replay
        elif method == "initialize" and is_request(message):
            with self._lock:
                self._initialize_body = body
                self.session_id = None  # an initialize opens a new session; it never carries an old one
        else:
            with self._lock:
                needs_session = self.session_id is None and self._initialize_body is not None
            if needs_session:
                # A new editor (or this one restarted on another port): open the client's session there first
                self._reinitialize(url, None)

        if before_send is not None and not before_send():
            log_debug(f"Not sending {method} {request_id!r}: cancelled before it was sent")
            return

        for attempt in (0, 1):
            if version is not None:
                headers = self._base_headers()
                headers.update(modern_headers(message, version))
            else:
                headers = self._headers()
            req = urllib.request.Request(url, data=body, headers=headers, method="POST")
            try:
                resp = _opener.open(req, timeout=timeout if timeout is not None else self.timeout)
                break
            except urllib.error.HTTPError as e:
                try:
                    self._note_headers(e.headers)
                    error_body = e.read() if e.fp else b""
                finally:
                    e.close()
                stale = headers.get("Mcp-Session-Id")
                if (e.code == 404 and attempt == 0 and stale and method != "initialize"
                        and self._reinitialize(url, stale)):
                    continue
                self._emit_body(error_body, e.code, request_id, wants_reply, method, emit)
                return
            except Exception as e:  # URLError, timeouts, connection resets
                reason = getattr(e, "reason", e)
                log_error(f"Request to {url} failed: {reason}")
                if isinstance(e, urllib.error.URLError) and not isinstance(reason, TimeoutError):
                    # Refused or unreachable, so the editor never saw the message: when the discovery entries now
                    # name another editor (this one restarted on another port), send it there instead
                    self._lost(url)
                    if not self.locator.is_explicit and not _moved:
                        try:
                            moved_to = self._target()
                        except EditorNotFound:
                            moved_to = url
                        if moved_to != url:
                            self.forward(body, message, emit, None, timeout, _moved=True, on_open=on_open)
                            return
                if wants_reply:
                    emit(error_response(request_id, INTERNAL_ERROR, f"NodeToCode editor not reachable at {url}: {reason}"))
                return

        if on_open is not None:
            on_open(resp)
        with resp:
            if version is None:
                self._note_headers(resp.headers)
            content_type = (resp.headers.get("Content-Type") or "").lower()
            log_debug(f"Response status: {resp.status}, Content-Type: {content_type}")
            try:
                if content_type.startswith("text/event-stream"):
                    self._read_stream(resp, request_id, wants_reply, method, emit)
                else:
                    self._emit_body(resp.read(), resp.status, request_id, wants_reply, method, emit)
            except Exception as e:  # the connection broke or timed out mid-response
                log_error(f"Reading the response failed: {e}")
                if wants_reply:
                    emit(error_response(request_id, INTERNAL_ERROR, f"Reading the editor's response failed: {e}"))

    def _emit_body(self, body: bytes, status: int, request_id: Any, wants_reply: bool, method: Optional[str],
                   emit: Callable[[Any], None]) -> None:
        """Emit a complete application/json body. Anything that is not a JSON-RPC message becomes an error with
        the request's id, and a notification never gets a reply."""
        text = body.decode("utf-8", "replace").strip()
        if status == 401:
            source = self.tokens.describe()
            if self.tokens.get():
                log_error("The editor requires a bearer token (Require MCP Bearer Token is on) and refused the one from "
                          f"{source}")
            else:
                log_error("The editor requires a bearer token (Require MCP Bearer Token is on) and the bridge has none: "
                          f"nothing in {source}")
        if not wants_reply:
            if text or status >= 300:
                log_debug(f"No reply to a notification or response (HTTP {status}): {text[:200]}")
            return

        parsed = None
        if text:
            try:
                parsed = json.loads(text)
            except ValueError:
                parsed = None

        if isinstance(parsed, dict) and ("result" in parsed or "error" in parsed):
            if parsed.get("id") is None and request_id is not None:
                # The server rejected the request before reading its id (Origin, Content-Type, parse error)
                parsed["id"] = request_id
            self._note_message(parsed, method)
            emit(parsed)
            return
        if isinstance(parsed, list) and parsed:
            emit(parsed)
            return

        if 200 <= status < 300:
            message = f"The editor answered HTTP {status} without a JSON-RPC response"
        else:
            message = f"The editor answered HTTP {status}"
        log_error(f"{message}: {text[:200]}")
        emit(error_response(request_id, INTERNAL_ERROR, message, {"httpStatus": status, "body": text[:2000]}))

    def _read_stream(self, resp: Any, request_id: Any, wants_reply: bool, method: Optional[str],
                     emit: Callable[[Any], None]) -> None:
        """Emit each JSON-RPC message of a text/event-stream response as it arrives; the response to the request
        ends the call."""
        parser = SseParser()
        answered = False
        while not answered:
            raw = resp.readline()
            if not raw:
                break
            for line in SseParser.split_lines(raw):
                event = parser.feed_line(line)
                if event is None:
                    continue
                event_type, data = event
                if event_type != "message":
                    log_debug(f"Ignoring SSE event '{event_type}'")
                    continue
                try:
                    message = json.loads(data)
                except ValueError:
                    log_error(f"Ignoring a non-JSON SSE event: {data[:200]}")
                    continue
                is_response = isinstance(message, dict) and "method" not in message and (
                    "result" in message or "error" in message)
                if is_response and message.get("id") == request_id:
                    self._note_message(message, method)
                    answered = True
                emit(message)
                if answered:
                    break

        if wants_reply and not answered:
            log_error("The event stream ended without a response to the request")
            emit(error_response(request_id, INTERNAL_ERROR, "The editor's event stream ended without a response"))


# ---------------------------------------------------------------------------------------------------------------------
# The client side: one stdin reader, a worker per request
# ---------------------------------------------------------------------------------------------------------------------

class _Call:
    """A request in flight on its worker thread."""

    def __init__(self, request_id: Any) -> None:
        self.request_id = request_id
        self.sent = False
        self.cancelled = False
        self.thread: Optional[threading.Thread] = None
        self.modern = False
        self.response: Any = None


class Bridge:
    """Dispatches stdin messages: requests to worker threads, initialize and notifications in order on the caller's
    (reader) thread, and notifications/cancelled to the editor at once."""

    def __init__(self, upstream: Upstream, emit: Callable[[Any], None] = write_message) -> None:
        self.upstream = upstream
        self._emit = emit
        self._lock = threading.Lock()
        self._calls: Dict[str, _Call] = {}
        self._threads: List[threading.Thread] = []

    def submit(self, line: str) -> None:
        """Handle one stdin line."""
        try:
            message = json.loads(line)
        except ValueError as e:
            log_error(f"JSON parse error: {e}")
            self._emit(error_response(None, PARSE_ERROR, f"Parse error: {e}"))
            return

        try:
            body = line.encode("utf-8")
        except UnicodeEncodeError:
            # Bytes that were not UTF-8 survived stdin as surrogates
            log_error("Message is not valid UTF-8")
            if is_request(message) or batch_has_request(message):
                request_id = message.get("id") if isinstance(message, dict) else None
                self._emit(error_response(request_id, PARSE_ERROR, "Parse error: the message is not valid UTF-8"))
            return

        method = message.get("method") if isinstance(message, dict) else None
        if is_request(message):
            if method == "initialize":
                # In order: the session it opens must exist before the client's next message is sent
                self._guarded(body, message, self._emit)
                return
            self._start_call(body, message)
        elif isinstance(message, dict) and method == "notifications/cancelled":
            self._cancel(body, message)
        elif isinstance(message, dict):
            # Other notifications (and responses) go in order, with a bound so a stuck editor cannot hold stdin
            self._guarded(body, message, self._emit, timeout=NOTIFICATION_TIMEOUT)
        else:
            self._start_thread(lambda: self._guarded(body, message, self._emit))  # a batch

    def _guarded(self, body: bytes, message: Any, emit: Callable[[Any], None],
                 before_send: Optional[Callable[[], bool]] = None, timeout: Optional[float] = None,
                 on_open: Optional[Callable[[Any], None]] = None) -> None:
        try:
            self.upstream.forward(body, message, emit, before_send, timeout, on_open=on_open)
        except Exception as e:
            log_error(f"Error processing message: {e}")
            if is_request(message):
                emit(error_response(message.get("id"), INTERNAL_ERROR, str(e)))

    def _start_thread(self, target: Callable[[], None]) -> threading.Thread:
        thread = threading.Thread(target=target, daemon=True)
        with self._lock:
            self._threads = [t for t in self._threads if t.is_alive()] + [thread]
        thread.start()
        return thread

    def _start_call(self, body: bytes, message: dict) -> None:
        request_id = message.get("id")
        key = id_key(request_id)
        call = _Call(request_id)
        call.modern = modern_version(message) is not None
        with self._lock:
            self._calls[key] = call

        def emit(out: Any) -> None:
            if call.cancelled:
                log_debug(f"Dropping a message for cancelled request {request_id!r}")
                return
            self._emit(out)

        def before_send() -> bool:
            with self._lock:
                if call.cancelled:
                    return False
                call.sent = True
                return True

        def on_open(resp: Any) -> None:
            with self._lock:
                call.response = resp
                cancelled = call.cancelled
            if cancelled and call.modern:
                close_response(resp)

        def run() -> None:
            try:
                self._guarded(body, message, emit, before_send, on_open=on_open)
            finally:
                with self._lock:
                    if self._calls.get(key) is call:
                        del self._calls[key]

        call.thread = self._start_thread(run)

    def _cancel(self, body: bytes, message: dict) -> None:
        """notifications/cancelled: from now on nothing is written for that request. A request still waiting to be
        sent is dropped; one already sent gets the cancel forwarded, and once more a moment later should it have
        reached the editor before its request did."""
        params = message.get("params") if isinstance(message.get("params"), dict) else {}
        request_id = params.get("requestId")
        with self._lock:
            call = self._calls.get(id_key(request_id))
            if call is not None:
                call.cancelled = True
            sent = call is not None and call.sent
        if call is not None and not sent:
            log_debug(f"Request {request_id!r} cancelled before it was sent")
            return
        if call is not None and call.modern:
            # 2026-07-28 over HTTP: closing the request's stream is the cancellation; nothing is forwarded
            with self._lock:
                resp = call.response
            log_debug(f"Closing the stream of request {request_id!r} to cancel it")
            if resp is not None:
                close_response(resp)
            return
        log_debug(f"Forwarding the cancel of request {request_id!r}")
        self._guarded(body, message, self._emit, timeout=NOTIFICATION_TIMEOUT)
        if call is None:
            return

        def resend() -> None:
            with self._lock:
                running = self._calls.get(id_key(request_id)) is call
            if running:
                log_debug(f"Request {request_id!r} still running; sending its cancel again")
                self._guarded(body, message, self._emit, timeout=NOTIFICATION_TIMEOUT)

        timer = threading.Timer(CANCEL_RESEND_DELAY, resend)
        timer.daemon = True
        timer.start()

    def drain(self) -> None:
        """Wait for the requests in flight (a cancelled one only briefly: the editor sends nothing more for it)."""
        with self._lock:
            threads = list(self._threads)
            cancelled = {c.thread for c in self._calls.values() if c.cancelled}
        for thread in threads:
            thread.join(5 if thread in cancelled else None)


def main():
    global debug_mode

    configure_stdio()

    # Parse arguments
    parser = argparse.ArgumentParser(description="NodeToCode MCP Bridge")
    parser.add_argument("--project", default=os.environ.get(PROJECT_ENV) or None,
                        help="The editor's project: its name, .uproject path or folder (default: the "
                             f"{PROJECT_ENV} environment variable; needed only when several editors run)")
    parser.add_argument("--port", type=int, default=None,
                        help="Connect to the editor on this port instead of finding it through its discovery entry")
    parser.add_argument("--host", default=DEFAULT_HOST, help="Host for --port (default 127.0.0.1)")
    parser.add_argument("--url", default=None, help="The editor's MCP endpoint, for example http://127.0.0.1:27000/mcp")
    parser.add_argument("--token-file", default=None,
                        help="File holding the editor's MCP bearer token (default: the NODETOCODE_MCP_TOKEN environment "
                             "variable, else the editor's per-user mcp-token file)")
    parser.add_argument("--timeout", type=float, default=0,
                        help="Seconds to wait for the editor to send data (0, the default: no limit)")
    parser.add_argument("--debug", action="store_true", help="Enable debug logging")
    args = parser.parse_args()

    debug_mode = args.debug

    url = args.url or (f"http://{args.host}:{args.port}/mcp" if args.port else None)
    if url and args.project:
        log_info(f"--project '{args.project}' is ignored: --port/--url names the editor")
    locator = EditorLocator(url=url, project=None if url else args.project)
    log_debug("NodeToCode MCP Bridge starting...")
    log_debug(f"Editor: {url or 'from the discovery entries in ' + discovery_index_dir()}"
              + (f", project {args.project}" if args.project and not url else ""))

    upstream = Upstream(locator, args.timeout if args.timeout and args.timeout > 0 else None,
                        TokenSource(args.token_file))
    bridge = Bridge(upstream)

    try:
        for line in sys.stdin:
            line = line.strip()
            if not line:
                continue
            log_debug(f"Received: {line[:200]}")
            bridge.submit(line)

        # stdin closed: let the calls in flight answer before exiting, then end the session
        bridge.drain()
        upstream.end_session()

    except KeyboardInterrupt:
        log_debug("Interrupted, shutting down...")
    except Exception as e:
        log_error(f"Fatal error: {e}")
        sys.exit(1)

    log_debug("Bridge shutting down")


if __name__ == "__main__":
    main()
