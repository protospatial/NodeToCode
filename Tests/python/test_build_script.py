# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
build.ps1 helpers that decide whether an open UnrealEditor blocks this run's build.

The functions are taken out of build.ps1 with the PowerShell parser (the script itself is never run) and called in
Windows and Linux mode. Needs powershell.exe or pwsh on PATH; skipped without one.
"""

import json
import os
import shutil
import subprocess

import pytest

REPO_ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
BUILD_PS1 = os.path.join(REPO_ROOT, 'build.ps1')
SHELL = shutil.which('pwsh') or shutil.which('powershell')

pytestmark = pytest.mark.skipif(SHELL is None, reason='needs pwsh or powershell on PATH')

# (command line, host .uproject, Windows result, Linux result)
CASES = [
    # Another lane's host of the same version: same file name, other folder (the 0473 defect)
    ('"C:/UE/UnrealEditor-Cmd.exe" D:/n2c-lanes/lane-d/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject -unattended',
     'D:/n2c-lanes/lane-b/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject', False, False),
    # This run's host, backslashes and other case on the command line
    ('"C:\\UE\\UnrealEditor.exe" "d:\\N2C-Lanes\\lane-b\\Hosts\\NodeToCodeHost_5_8\\NodeToCodeHost_5_8.uproject" -log',
     'D:/n2c-lanes/lane-b/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject', True, False),
    # Exact path, quoted, with spaces in it
    ('UnrealEditor "G:/Personal Projects/n2c/Hosts/NodeToCodeHost_5_7/NodeToCodeHost_5_7.uproject" -game',
     'G:/Personal Projects/n2c/Hosts/NodeToCodeHost_5_7/NodeToCodeHost_5_7.uproject', True, True),
    # Linux /proc/<pid>/cmdline: NUL-separated argv
    ('/opt/UE/UnrealEditor\0/home/u/Hosts/NodeToCodeHost_5_6/NodeToCodeHost_5_6.uproject\0-nullrhi\0',
     '/home/u/Hosts/NodeToCodeHost_5_6/NodeToCodeHost_5_6.uproject', True, True),
    # The host path as a suffix of a longer path is another project
    ('UnrealEditor /mnt/old/home/u/Hosts/NodeToCodeHost_5_6/NodeToCodeHost_5_6.uproject',
     '/home/u/Hosts/NodeToCodeHost_5_6/NodeToCodeHost_5_6.uproject', False, False),
    # Another version's host in the same folder
    ('UnrealEditor D:/Hosts/NodeToCodeHost_5_7/NodeToCodeHost_5_7.uproject',
     'D:/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject', False, False),
    # -Project= form, repeated slashes
    ('Build.bat X Win64 Development -Project=D:\\\\Hosts\\NodeToCodeHost_5_8\\NodeToCodeHost_5_8.uproject',
     'D:/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject', True, True),
    # Linux is case-sensitive: another case is another file there
    ('UnrealEditor /home/u/hosts/NodeToCodeHost_5_6/NodeToCodeHost_5_6.uproject',
     '/home/u/Hosts/NodeToCodeHost_5_6/NodeToCodeHost_5_6.uproject', True, False),
    ('', 'D:/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject', False, False),
    # No host path (it could not be resolved) matches nothing, not every editor
    ('UnrealEditor D:/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject', '', False, False),
]

SCRIPT = r'''
$ErrorActionPreference = 'Stop'
$ast = [System.Management.Automation.Language.Parser]::ParseFile($env:N2C_BUILD_PS1, [ref]$null, [ref]$null)
foreach ($name in @('ConvertTo-PathKey', 'Test-CommandLineHasPath')) {
    $fn = $ast.Find({ param($a) $a -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $a.Name -eq $name }, $true)
    if (-not $fn) { throw "build.ps1 has no function $name" }
    . ([scriptblock]::Create($fn.Extent.Text))
}
$cases = [IO.File]::ReadAllText($env:N2C_CASES) | ConvertFrom-Json
$out = foreach ($c in $cases) {
    $OnWindows = $true; $OnMac = $false
    $win = Test-CommandLineHasPath $c.cmd $c.path
    $OnWindows = $false; $OnMac = $false
    $linux = Test-CommandLineHasPath $c.cmd $c.path
    ,@($win, $linux)
}
ConvertTo-Json -Compress @($out)
'''


def test_editor_check_matches_full_host_path(tmp_path):
    cases_file = tmp_path / 'cases.json'
    cases_file.write_text(json.dumps([{'cmd': c, 'path': p} for c, p, _, _ in CASES]), encoding='utf-8')
    script_file = tmp_path / 'check.ps1'
    script_file.write_text(SCRIPT, encoding='utf-8')
    env = dict(os.environ, N2C_BUILD_PS1=BUILD_PS1, N2C_CASES=str(cases_file))
    proc = subprocess.run([SHELL, '-NoProfile', '-NonInteractive', '-File', str(script_file)],
                          capture_output=True, text=True, env=env, timeout=120)
    assert proc.returncode == 0, proc.stderr
    results = json.loads(proc.stdout)
    for (cmd, path, want_win, want_linux), (got_win, got_linux) in zip(CASES, results):
        assert (got_win, got_linux) == (want_win, want_linux), f'{cmd!r} vs {path!r}'
    assert len(results) == len(CASES)
