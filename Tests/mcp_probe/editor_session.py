"""Start a headless Unreal editor on a host project, wait for the NodeToCode MCP server, and stop it again.

Stdlib only; run with `python -I`. Used by mcp_probe.py, and usable on its own:

    python -I editor_session.py --engine G:/UE/UE_5.8 --project <host>.uproject --port 27150

Launch form: UnrealEditor-Cmd.exe <project> -unattended -nullrhi -nosplash -nosound -NoLiveCoding -culture=en
-N2CMcp, without -ExecutePythonScript or -ExecCmds=Quit, keeps the editor running until it is told to stop.
-N2CMcp is needed because the MCP server does not start in unattended runs otherwise.

Settings: the plugin's own config file does not take -ini: command-line overrides, so for the length of the session
this writes the probe's port (and, when asked, Allow MCP Python Execution) into the host's Saved/Config layer
(NodeToCode.ini and EditorPerProjectUserSettings.ini under Saved/Config/<Platform>Editor) and restores both files
when the session stops. Changing a NodeToCode setting through the editor (set_editor_property, the settings UI, a
settings migration) saves it to the project's Config/DefaultNodeToCode.ini, so the session also snapshots that file
when it starts and puts it back byte for byte when it stops (or deletes it if it did not exist); restore_ok says
whether every file came back exactly. Using a non-default port means an editor already running on 27000 cannot
answer the probe.

Lifetime: on Windows the editor is put in a job object that kills it (and its crash reporter) when this Python
process exits, so a probe that is killed or dies cannot leave an editor running. The Saved/Config patches are only
restored by stop(); after a hard kill, check the host's Saved/Config/<Platform>Editor files and
Config/DefaultNodeToCode.ini.
"""
import argparse
import json
import os
import socket
import subprocess
import sys
import time
import urllib.request

# Extra arguments for a headless session that opens a Blueprint editor (every live script that makes the in-memory
# probe Blueprint, probe_blueprint.py): on UE 5.6 under -nullrhi the Blueprint editor's window otherwise reaches
# FGenericWindow::GetRestoredDimensions, which is Fatal. Pass as EditorSession(..., extra_args=BLUEPRINT_EDITOR_ARGS).
BLUEPRINT_EDITOR_ARGS = ('-RenderOffScreen',)


def find_editor(engine_root):
    exe = os.path.join(engine_root, 'Engine', 'Binaries', 'Win64', 'UnrealEditor-Cmd.exe')
    if not os.path.isfile(exe):
        exe = os.path.join(engine_root, 'Engine', 'Binaries', 'Mac', 'UnrealEditor-Cmd')
    if not os.path.isfile(exe):
        raise SystemExit('UnrealEditor-Cmd not found under %s' % engine_root)
    return exe


def kill_with_parent(proc):
    """Windows: put proc in a job object that kills it when the last handle to the job closes, which happens when this
    process exits however it exits. Returns the job handle (keep it referenced) or None where unsupported."""
    if os.name != 'nt':
        return None
    try:
        import ctypes
        from ctypes import wintypes
    except ImportError:
        return None

    class IoCounters(ctypes.Structure):
        _fields_ = [(n, ctypes.c_ulonglong) for n in ('ReadOperationCount', 'WriteOperationCount', 'OtherOperationCount',
                                                      'ReadTransferCount', 'WriteTransferCount', 'OtherTransferCount')]

    class BasicLimits(ctypes.Structure):
        _fields_ = [('PerProcessUserTimeLimit', ctypes.c_longlong), ('PerJobUserTimeLimit', ctypes.c_longlong),
                    ('LimitFlags', wintypes.DWORD), ('MinimumWorkingSetSize', ctypes.c_size_t),
                    ('MaximumWorkingSetSize', ctypes.c_size_t), ('ActiveProcessLimit', wintypes.DWORD),
                    ('Affinity', ctypes.c_size_t), ('PriorityClass', wintypes.DWORD), ('SchedulingClass', wintypes.DWORD)]

    class ExtendedLimits(ctypes.Structure):
        _fields_ = [('BasicLimitInformation', BasicLimits), ('IoInfo', IoCounters),
                    ('ProcessMemoryLimit', ctypes.c_size_t), ('JobMemoryLimit', ctypes.c_size_t),
                    ('PeakProcessMemoryUsed', ctypes.c_size_t), ('PeakJobMemoryUsed', ctypes.c_size_t)]

    kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel32.CreateJobObjectW.restype = wintypes.HANDLE
    kernel32.CreateJobObjectW.argtypes = (ctypes.c_void_p, wintypes.LPCWSTR)
    kernel32.SetInformationJobObject.argtypes = (wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD)
    kernel32.AssignProcessToJobObject.argtypes = (wintypes.HANDLE, wintypes.HANDLE)
    job = kernel32.CreateJobObjectW(None, None)
    if not job:
        return None
    info = ExtendedLimits()
    info.BasicLimitInformation.LimitFlags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
    # 9 = JobObjectExtendedLimitInformation
    if not kernel32.SetInformationJobObject(job, 9, ctypes.byref(info), ctypes.sizeof(info)):
        kernel32.CloseHandle(job)
        return None
    if not kernel32.AssignProcessToJobObject(job, wintypes.HANDLE(int(proc._handle))):
        kernel32.CloseHandle(job)
        return None
    return job


def free_port(preferred):
    """The preferred port if both it and port+1 are free, else the first such pair above it. Nothing listens on port+1
    since card 0113; probe P5 and the smoke check need it free at start to show the editor does not take it."""
    port = preferred
    while port < preferred + 200:
        ok = True
        for p in (port, port + 1):
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
                if s.connect_ex(('127.0.0.1', p)) == 0:
                    ok = False
        if ok:
            return port
        port += 2
    raise SystemExit('no free port pair near %d' % preferred)


def patch_ini(path, section, key, value):
    """Set key=value in section (for example '[/Script/NodeToCode.N2CSettings]') of an ini file, creating the file or
    the section when missing and keeping every other line"""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    original = open(path, 'rb').read() if os.path.exists(path) else None
    lines = (original or b'').decode('utf-8-sig', 'replace').splitlines()
    out, in_section, written = [], False, False
    for line in lines:
        stripped = line.strip()
        if stripped.startswith('['):
            if in_section and not written:
                out.append('%s=%s' % (key, value))
                written = True
            in_section = stripped == section
        elif in_section and stripped.split('=', 1)[0].strip() == key:
            continue
        out.append(line)
    if not written:
        if not in_section:
            out += ['', section]
        out.append('%s=%s' % (key, value))
    open(path, 'w', encoding='utf-8', newline='\n').write('\n'.join(out) + '\n')


def remove_ini_key(path, section, key):
    """Delete every key= line in section of an ini file, keeping every other line; no file, no change"""
    if not os.path.exists(path):
        return
    lines = open(path, 'rb').read().decode('utf-8-sig', 'replace').splitlines()
    out, in_section = [], False
    for line in lines:
        stripped = line.strip()
        if stripped.startswith('['):
            in_section = stripped == section
        elif in_section and stripped.split('=', 1)[0].strip() == key:
            continue
        out.append(line)
    open(path, 'w', encoding='utf-8', newline='\n').write('\n'.join(out) + '\n')


class EditorSession:
    def __init__(self, engine_root, project, port, log_path, allow_python=True, extra_args=(), extra_env=None):
        """allow_python: True or False writes bAllowMcpPythonExecution into the host's Saved/Config layer (the user's
        own setting) for the session; None removes it from that layer, so the user has not set it (card 0423's
        "committed" check: only a project Config/Default*.ini value is left, and the plugin must ignore it).
        extra_env: variables added to the editor's environment only (for example provider keys from
        env_keys.load_provider_keys()); they are never written to a file or printed."""
        self.exe = find_editor(engine_root)
        self.project = project
        self.port = port
        self.log_path = log_path
        self.allow_python = allow_python
        self.extra_args = list(extra_args)
        self.extra_env = dict(extra_env or {})
        self.proc = None
        self._job = None
        self._backups = {}
        self.restore_ok = None

    def args(self):
        a = [self.exe, self.project, '-unattended', '-nullrhi', '-nosplash', '-nosound', '-NoLiveCoding',
             '-culture=en', '-stdout', '-FullStdOutLogOutput', '-N2CMcp']
        return a + self.extra_args

    def _saved_config_dir(self):
        platform = 'WindowsEditor' if os.name == 'nt' else 'MacEditor'
        return os.path.join(os.path.dirname(os.path.abspath(self.project)), 'Saved', 'Config', platform)

    def _patch(self, file_name, section, key, value):
        self.patch_file(os.path.join(self._saved_config_dir(), file_name), section, key, value)

    def patch_file(self, path, section, key, value):
        """Set key=value in section of an ini file; stop() puts the file back as it was before the first change"""
        self.guard_file(path)
        patch_ini(path, section, key, value)

    def guard_file(self, path):
        """Snapshot a file (or its absence) now; stop() puts it back byte for byte"""
        path = os.path.abspath(path)
        if path not in self._backups:
            self._backups[path] = open(path, 'rb').read() if os.path.isfile(path) else None

    def project_config_path(self, file_name='DefaultNodeToCode.ini'):
        return os.path.join(os.path.dirname(os.path.abspath(self.project)), 'Config', file_name)

    def _restore(self):
        ok = True
        for path, original in self._backups.items():
            if original is None:
                if os.path.exists(path):
                    os.remove(path)
            else:
                with open(path, 'wb') as f:
                    f.write(original)
            now = open(path, 'rb').read() if os.path.isfile(path) else None
            ok = ok and now == original
        self._backups = {}
        self.restore_ok = ok if self.restore_ok is None else (self.restore_ok and ok)

    def start(self, timeout=240):
        self.guard_file(self.project_config_path())
        self._patch('NodeToCode.ini', '[/Script/NodeToCode.N2CSettings]', 'McpServerPort', str(self.port))
        if self.allow_python is None:
            user_ini = os.path.join(self._saved_config_dir(), 'EditorPerProjectUserSettings.ini')
            self.guard_file(user_ini)
            remove_ini_key(user_ini, '[/Script/NodeToCode.N2CUserSettings]', 'bAllowMcpPythonExecution')
        else:
            self._patch('EditorPerProjectUserSettings.ini', '[/Script/NodeToCode.N2CUserSettings]',
                        'bAllowMcpPythonExecution', 'True' if self.allow_python else 'False')
        self.log = open(self.log_path, 'wb')
        env = None
        if self.extra_env:
            env = dict(os.environ)
            env.update(self.extra_env)
        self.proc = subprocess.Popen(self.args(), stdout=self.log, stderr=subprocess.STDOUT, env=env)
        self._job = kill_with_parent(self.proc)
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self.proc.poll() is not None:
                return False
            # The listener opens in frame 0, but the HTTP server only reads and answers on game-thread ticks, and frame 0
            # can block for several seconds (asset registry, startup scripts). A request that times out closes its
            # socket, and the server later writes its reply to the closed socket, which logs
            # LogHttpConnection: Error: socket_send_failure (card 0422). So each poll waits for the whole remaining
            # deadline instead of a short timeout: before the listener exists the connect is refused at once, and if
            # the editor dies the socket is reset, so the wait only lasts while the editor is alive and busy.
            if self.health(timeout=max(1.0, deadline - time.time())):
                return True
            time.sleep(1.0)
        return False

    def health(self, timeout=2):
        try:
            with urllib.request.urlopen('http://127.0.0.1:%d/mcp/health' % self.port, timeout=timeout) as r:
                return r.status == 200
        except Exception:
            return False

    def stop(self, timeout=60, before_restore=None):
        """Ask the editor to close (taskkill without /F sends WM_CLOSE), then force it after the timeout.

        before_restore: called once the editor has exited and before the guarded files are put back, so a caller can
        see what the session itself did to them (smoke_check.py hashes the host's Config/*.ini there)."""
        if not self.proc or self.proc.poll() is not None:
            if getattr(self, 'log', None):
                self.log.close()
            if before_restore:
                before_restore()
            self._restore()
            return
        if os.name == 'nt':
            subprocess.run(['taskkill', '/PID', str(self.proc.pid), '/T'], capture_output=True)
        else:
            self.proc.terminate()
        try:
            self.proc.wait(timeout)
        except subprocess.TimeoutExpired:
            if os.name == 'nt':
                subprocess.run(['taskkill', '/PID', str(self.proc.pid), '/T', '/F'], capture_output=True)
            else:
                self.proc.kill()
            self.proc.wait(30)
        self.log.close()
        if before_restore:
            before_restore()
        self._restore()

    def log_text(self):
        try:
            raw = open(self.log_path, 'rb').read()
        except OSError:
            return ''
        return raw.decode('utf-8', 'replace')

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.stop()


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--port', type=int, default=27150)
    ap.add_argument('--log', default='editor_session.log')
    args = ap.parse_args()
    port = free_port(args.port)
    with EditorSession(args.engine, args.project, port, args.log) as s:
        ok = s.start()
        print(json.dumps({'started': ok, 'port': port, 'pid': s.proc.pid}))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
