"""Generate (or check) the MCP tool reference, Context/mcp-server/tools.generated.json and tools.generated.md.

Stdlib only; run with `python -I`. Starts a headless UnrealEditor-Cmd on a host project that has NodeToCode built and
runs the N2CToolReference commandlet (card 0179), which writes both files from the MCP tool registry and the declared
toolsets. Never edit the files by hand.

    python -I Scripts/generate_tool_reference.py --engine G:/UE/UE_5.8 --project <host>/NodeToCodeHost_5_8.uproject
    python -I Scripts/generate_tool_reference.py --engine ... --project ... --check

--output picks another folder (default: Context/mcp-server of this checkout). --check writes nothing and exits 1 when
either file is missing or differs from what the registry builds (CI and the docs lint use it). The editor log goes to
--log (default BuildLogs/ToolReference_<timestamp>.log). Exits with the commandlet's code: 0 done, 1 check failed,
2 nothing written; 3 when the editor could not be started or timed out.
"""
import argparse
import os
import subprocess
import sys
import time

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
DEFAULT_OUTPUT = os.path.join(REPO_ROOT, 'Context', 'mcp-server')


def editor_cmd(engine):
    """UnrealEditor-Cmd under an engine root (the folder that holds Engine/)"""
    if sys.platform == 'win32':
        rel = os.path.join('Engine', 'Binaries', 'Win64', 'UnrealEditor-Cmd.exe')
    elif sys.platform == 'darwin':
        rel = os.path.join('Engine', 'Binaries', 'Mac', 'UnrealEditor-Cmd')
    else:
        rel = os.path.join('Engine', 'Binaries', 'Linux', 'UnrealEditor-Cmd')
    return os.path.join(engine, rel)


def build_command(exe, project, output, check, log):
    command = [exe, os.path.abspath(project), '-run=NodeToCode.N2CToolReference', '-Output=%s' % os.path.abspath(output),
               '-unattended', '-nullrhi', '-nosplash', '-nosound', '-NoLiveCoding', '-stdout', '-FullStdOutLogOutput',
               '-abslog=%s' % os.path.abspath(log)]
    if check:
        command.append('-Check')
    return command


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True, help='engine root, the folder that holds Engine/ (e.g. G:/UE/UE_5.8)')
    ap.add_argument('--project', required=True, help='a host .uproject with NodeToCode built (build.ps1 makes them)')
    ap.add_argument('--output', default=DEFAULT_OUTPUT, help='folder for the two files (default: Context/mcp-server)')
    ap.add_argument('--check', action='store_true', help='write nothing; exit 1 when the committed files are stale')
    ap.add_argument('--log', help='editor log path (default: BuildLogs/ToolReference_<timestamp>.log)')
    ap.add_argument('--timeout', type=int, default=900, help='seconds before the editor is stopped (default 900)')
    args = ap.parse_args(argv)

    exe = editor_cmd(args.engine)
    if not os.path.isfile(exe):
        print('generate_tool_reference: %s not found' % exe, file=sys.stderr)
        return 3
    if not os.path.isfile(args.project):
        print('generate_tool_reference: %s not found' % args.project, file=sys.stderr)
        return 3
    log = args.log or os.path.join(REPO_ROOT, 'BuildLogs', 'ToolReference_%s.log' % time.strftime('%Y%m%d_%H%M%S'))
    os.makedirs(os.path.dirname(os.path.abspath(log)), exist_ok=True)

    command = build_command(exe, args.project, args.output, args.check, log)
    print('generate_tool_reference: %s' % ' '.join('"%s"' % c if ' ' in c else c for c in command), flush=True)
    try:
        completed = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=args.timeout)
    except subprocess.TimeoutExpired:
        print('generate_tool_reference: the editor did not finish in %d s (log: %s)' % (args.timeout, log), file=sys.stderr)
        return 3
    except OSError as error:
        print('generate_tool_reference: could not start the editor: %s' % error, file=sys.stderr)
        return 3

    lines = completed.stdout.decode('utf-8', 'replace').replace('\x00', '').splitlines()
    for line in lines:
        if 'N2CToolReference:' in line:
            print(line.strip())
    print('generate_tool_reference: exit %d (log: %s)' % (completed.returncode, log))
    return completed.returncode


if __name__ == '__main__':
    sys.exit(main())
