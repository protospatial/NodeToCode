"""Start a headless Unreal editor on a host project, wait for the NodeToCode MCP server, and stop it again.

Stdlib only; run with `python -I`. Used by mcp_probe.py, and usable on its own:

    python -I editor_session.py --engine G:/UE/UE_5.8 --project <host>.uproject --port 27150

Launch form: UnrealEditor-Cmd.exe <project> -unattended -nullrhi -nosplash -nosound -NoLiveCoding -culture=en
-N2CMcp, without -ExecutePythonScript or -ExecCmds=Quit, keeps the editor running until it is told to stop.
-N2CMcp is needed because the MCP server does not start in unattended runs otherwise.

Settings: the plugin's own config file does not take -ini: command-line overrides, so for the length of the session
this writes the probe's port (and, when asked, Allow MCP Python Execution) into the host's Saved/Config layer
(NodeToCode.ini and EditorPerProjectUserSettings.ini under Saved/Config/<Platform>Editor) and restores both files
when the session stops. The project's shared Config/ folder is never touched. Using a non-default port means an
editor already running on 27000 cannot answer the probe.

Lifetime: on Windows the editor is put in a job object that kills it (and its crash reporter) when this Python
process exits, so a probe that is killed or dies cannot leave an editor running. The Saved/Config patches are only
restored by stop(); after a hard kill, check the host's Saved/Config/<Platform>Editor files.
"""
import argparse
import json
import os
import socket
import subprocess
import sys
import time
import urllib.request


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
    """The preferred port if both it and port+1 are free, else the first such pair above it."""
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


class EditorSession:
    def __init__(self, engine_root, project, port, log_path, allow_python=True, extra_args=(), extra_env=None):
        """extra_env: variables added to the editor's environment only (for example provider keys from
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

    def args(self):
        a = [self.exe, self.project, '-unattended', '-nullrhi', '-nosplash', '-nosound', '-NoLiveCoding',
             '-culture=en', '-stdout', '-FullStdOutLogOutput', '-N2CMcp']
        return a + self.extra_args

    def _saved_config_dir(self):
        platform = 'WindowsEditor' if os.name == 'nt' else 'MacEditor'
        return os.path.join(os.path.dirname(os.path.abspath(self.project)), 'Saved', 'Config', platform)

    def _patch(self, file_name, section, key, value):
        path = os.path.join(self._saved_config_dir(), file_name)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        original = open(path, 'rb').read() if os.path.exists(path) else None
        self._backups.setdefault(path, original)
        text = (original or b'').decode('utf-8-sig', 'replace')
        lines = text.splitlines()
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

    def _restore(self):
        for path, original in self._backups.items():
            if original is None:
                if os.path.exists(path):
                    os.remove(path)
            else:
                open(path, 'wb').write(original)
        self._backups = {}

    def start(self, timeout=240):
        self._patch('NodeToCode.ini', '[/Script/NodeToCode.N2CSettings]', 'McpServerPort', str(self.port))
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
            if self.health():
                return True
            time.sleep(1.0)
        return False

    def health(self):
        try:
            with urllib.request.urlopen('http://127.0.0.1:%d/mcp/health' % self.port, timeout=2) as r:
                return r.status == 200
        except Exception:
            return False

    def stop(self, timeout=60):
        """Ask the editor to close (taskkill without /F sends WM_CLOSE), then force it after the timeout."""
        if not self.proc or self.proc.poll() is not None:
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
