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
- stdout carries only JSON-RPC messages, one per line. A transport failure or a non-JSON body (an HTTP error page)
  becomes a JSON-RPC error carrying the request's id, and nothing is written in reply to a notification. Logs go
  to stderr.
- Requests run on their own threads, so a long tools/call (translate-focused-blueprint) does not hold up others.
- The editor keeps one MCP session per client (Mcp-Session-Id). The bridge sends the session its client's initialize
  opened on every request, with MCP-Protocol-Version. When the editor answers 404 (the session is gone: the editor
  restarted, or the session was ended) the bridge replays the client's initialize and notifications/initialized to
  open a new session and resends the message once. When stdin closes it ends the session with DELETE.

Zero external dependencies - uses only the Python standard library.

Usage:
    python nodetocode_bridge.py [--port PORT] [--host HOST] [--timeout SECONDS] [--debug]
"""

import argparse
import json
import sys
import threading
import urllib.error
import urllib.request
from typing import Any, Callable, List, Optional, Tuple

# Configuration
DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 27000
ACCEPT = "application/json, text/event-stream"

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


def log_error(message: str) -> None:
    """Log error message to stderr."""
    print(f"[NodeToCode-Bridge ERROR] {message}", file=sys.stderr, flush=True)


def configure_stdio() -> None:
    """Read and write UTF-8 on stdio whatever the locale. Without this, Python on Windows decodes piped stdin with
    the ANSI code page (cp1252), so 'é' sent by the client reaches the editor as 'Ã©'.

    stdin keeps undecodable bytes as surrogates (handle_line answers such a line with a parse error instead of
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


def batch_has_request(message: Any) -> bool:
    return isinstance(message, list) and any(is_request(m) for m in message)


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


def check_ue5_health(host: str, port: int) -> bool:
    """Check if UE5 MCP server is running."""
    url = f"http://{host}:{port}/mcp/health"
    log_debug(f"Probing health at {url}")

    try:
        req = urllib.request.Request(url, method="GET")
        with _opener.open(req, timeout=2) as resp:
            if resp.status == 200:
                log_debug(f"Health check passed: {resp.read().decode('utf-8', 'replace')}")
                return True
    except urllib.error.URLError as e:
        log_debug(f"Health check failed: {e}")
    except Exception as e:
        log_debug(f"Health check exception: {e}")

    return False


def find_ue5_server(host: str, port: int) -> Optional[tuple]:
    """Find running UE5 MCP server, trying multiple hosts/ports."""
    # Try 127.0.0.1 first to avoid IPv6 issues
    hosts_to_try = ["127.0.0.1", "localhost"] if host == "localhost" else [host]

    for h in hosts_to_try:
        if check_ue5_health(h, port):
            return (h, port)

    # Try port range
    for h in hosts_to_try:
        for p in range(27000, 27011):
            if p == port and h == host:
                continue  # Already tried
            if check_ue5_health(h, p):
                return (h, p)

    return None


class Upstream:
    """The editor's /mcp endpoint plus the session state shared by all requests."""

    def __init__(self, host: str, port: int, timeout: Optional[float] = None) -> None:
        self.url = f"http://{host}:{port}/mcp"
        # None: wait as long as the editor takes. A held translate-focused-blueprint call (UE 5.6/5.7, or no
        # progress token) sends nothing until the translation is done.
        self.timeout = timeout
        self._lock = threading.Lock()
        self.session_id: Optional[str] = None
        self.protocol_version: Optional[str] = None
        # The client's initialize request, replayed to open a new session after a 404
        self._initialize_body: Optional[bytes] = None
        self._reinitialize_lock = threading.Lock()

    def _headers(self) -> dict:
        headers = {"Content-Type": "application/json", "Accept": ACCEPT}
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

    def _reinitialize(self, stale_session: Optional[str]) -> bool:
        """Opens a new session by replaying the client's initialize (and notifications/initialized) after the editor
        answered 404 for stale_session. True when a session other than stale_session is now in use."""
        with self._reinitialize_lock:
            with self._lock:
                if self.session_id != stale_session:
                    return True  # another thread already opened a new one
                init_body = self._initialize_body
                self.session_id = None
            if init_body is None:
                return False
            log_debug("Session not found (404); replaying initialize to open a new session")
            headers = {"Content-Type": "application/json", "Accept": ACCEPT}
            try:
                req = urllib.request.Request(self.url, data=init_body, headers=headers, method="POST")
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
                notify = urllib.request.Request(self.url, data=b'{"jsonrpc":"2.0","method":"notifications/initialized"}',
                                                headers=self._headers(), method="POST")
                with _opener.open(notify, timeout=30) as resp:
                    resp.read()
            except Exception as e:  # HTTPError, URLError, timeouts
                log_error(f"Re-initializing the MCP session failed: {e}")
                return False
            with self._lock:
                return bool(self.session_id) and self.session_id != stale_session

    def end_session(self) -> None:
        """Ends the session with DELETE (best effort), when the client is done."""
        headers = self._headers()
        if "Mcp-Session-Id" not in headers:
            return
        try:
            req = urllib.request.Request(self.url, headers={k: v for k, v in headers.items() if k != "Content-Type"},
                                         method="DELETE")
            with _opener.open(req, timeout=5) as resp:
                resp.read()
            log_debug("Ended the MCP session")
        except Exception as e:
            log_debug(f"Ending the MCP session failed: {e}")

    def forward(self, body: bytes, message: Any, emit: Callable[[Any], None]) -> None:
        """POST one client message and emit every JSON-RPC message meant for the client."""
        wants_reply = is_request(message) or batch_has_request(message)
        request_id = message.get("id") if isinstance(message, dict) else None
        method = message.get("method") if isinstance(message, dict) else None
        if method == "initialize" and is_request(message):
            with self._lock:
                self._initialize_body = body
                self.session_id = None  # an initialize opens a new session; it never carries an old one

        for attempt in (0, 1):
            headers = self._headers()
            req = urllib.request.Request(self.url, data=body, headers=headers, method="POST")
            try:
                resp = _opener.open(req, timeout=self.timeout)
                break
            except urllib.error.HTTPError as e:
                try:
                    self._note_headers(e.headers)
                    error_body = e.read() if e.fp else b""
                finally:
                    e.close()
                stale = headers.get("Mcp-Session-Id")
                if e.code == 404 and attempt == 0 and stale and method != "initialize" and self._reinitialize(stale):
                    continue
                self._emit_body(error_body, e.code, request_id, wants_reply, method, emit)
                return
            except Exception as e:  # URLError, timeouts, connection resets
                reason = getattr(e, "reason", e)
                log_error(f"Request to {self.url} failed: {reason}")
                if wants_reply:
                    emit(error_response(request_id, INTERNAL_ERROR, f"NodeToCode editor not reachable at {self.url}: {reason}"))
                return

        with resp:
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


def handle_line(upstream: Upstream, line: str, emit: Callable[[Any], None] = write_message) -> None:
    """Forward one stdin line and emit what the client should see."""
    try:
        message = json.loads(line)
    except ValueError as e:
        log_error(f"JSON parse error: {e}")
        emit(error_response(None, PARSE_ERROR, f"Parse error: {e}"))
        return

    try:
        body = line.encode("utf-8")
    except UnicodeEncodeError:
        # Bytes that were not UTF-8 survived stdin as surrogates
        log_error("Message is not valid UTF-8")
        if is_request(message) or batch_has_request(message):
            request_id = message.get("id") if isinstance(message, dict) else None
            emit(error_response(request_id, PARSE_ERROR, "Parse error: the message is not valid UTF-8"))
        return

    try:
        upstream.forward(body, message, emit)
    except Exception as e:
        log_error(f"Error processing message: {e}")
        if is_request(message):
            emit(error_response(message.get("id"), INTERNAL_ERROR, str(e)))


def main():
    global debug_mode

    configure_stdio()

    # Parse arguments
    parser = argparse.ArgumentParser(description="NodeToCode MCP Bridge")
    parser.add_argument("--host", default=DEFAULT_HOST, help="UE5 server host")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help="UE5 server port")
    parser.add_argument("--timeout", type=float, default=0,
                        help="Seconds to wait for the editor to send data (0, the default: no limit)")
    parser.add_argument("--debug", action="store_true", help="Enable debug logging")
    args = parser.parse_args()

    debug_mode = args.debug

    log_debug("NodeToCode MCP Bridge starting...")
    log_debug(f"Configuration: host={args.host}, port={args.port}")

    # Find UE5 server
    server = find_ue5_server(args.host, args.port)

    if not server:
        log_error("Failed to connect to NodeToCode UE5 plugin. Is the Unreal Editor running with NodeToCode?")
        sys.exit(1)

    host, port = server
    log_debug(f"Connected to UE5 MCP server at {host}:{port}")
    upstream = Upstream(host, port, args.timeout if args.timeout and args.timeout > 0 else None)

    # Process stdin: each message on its own thread so a long call does not block the others
    workers: List[threading.Thread] = []
    try:
        for line in sys.stdin:
            line = line.strip()
            if not line:
                continue

            log_debug(f"Received: {line[:200]}")
            worker = threading.Thread(target=handle_line, args=(upstream, line), daemon=True)
            worker.start()
            workers = [w for w in workers if w.is_alive()] + [worker]

        # stdin closed: let the calls in flight answer before exiting, then end the session
        for worker in workers:
            worker.join()
        upstream.end_session()

    except KeyboardInterrupt:
        log_debug("Interrupted, shutting down...")
    except Exception as e:
        log_error(f"Fatal error: {e}")
        sys.exit(1)

    log_debug("Bridge shutting down")


if __name__ == "__main__":
    main()
