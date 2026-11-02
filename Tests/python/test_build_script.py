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
    proc = subprocess.run([SHELL, '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', str(script_file)],
                          capture_output=True, text=True, env=env, timeout=120)
    assert proc.returncode == 0, proc.stderr
    results = json.loads(proc.stdout)
    for (cmd, path, want_win, want_linux), (got_win, got_linux) in zip(CASES, results):
        assert (got_win, got_linux) == (want_win, want_linux), f'{cmd!r} vs {path!r}'
    assert len(results) == len(CASES)


# (UBT output, retried): sharing violations UBT hit at startup on per-user files other checkouts' UBT share
UBT_LOGS = [
    ('Log file: C:/Users/u/AppData/Local/UnrealBuildTool/Log.txt\n'
     'Unhandled exception: IOException: The process cannot access the file because it is being used by another process.\n'
     '   at EpicGames.Core.Log.BackupLogFile(FileReference outputFile)\n'
     'Result: Failed (OtherCompilationError)\n', True),
    ('Unhandled 1 aggregate exceptions\n'
     "Unhandled exception: IOException: The process cannot access the file 'C:/Users/u/AppData/Local/UnrealEngine"
     "/Intermediate/Build/BuildRules/MarketplaceRules.dll' because it is being used by another process.\n"
     '   at UnrealBuildTool.DynamicCompilation.CompileAssemblyAsync(...)\n'
     'Result: Failed (OtherCompilationError)\n', True),
    ('[3/8] Compile [x64] Foo.cpp\nFoo.cpp(12): error C2065: undeclared identifier\nResult: Failed (OtherCompilationError)\n', False),
    ('c1xx: fatal error C1076: compiler limit: internal heap limit reached\nResult: Failed (OtherCompilationError)\n', False),
]

COLLISION_SCRIPT = r'''
$ErrorActionPreference = 'Stop'
$ast = [System.Management.Automation.Language.Parser]::ParseFile($env:N2C_BUILD_PS1, [ref]$null, [ref]$null)
$fn = $ast.Find({ param($a) $a -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $a.Name -eq 'Test-UbtSharedFileCollision' }, $true)
if (-not $fn) { throw 'build.ps1 has no function Test-UbtSharedFileCollision' }
. ([scriptblock]::Create($fn.Extent.Text))
$out = foreach ($log in ($env:N2C_LOGS -split ';')) { [bool](Test-UbtSharedFileCollision $log) }
ConvertTo-Json -Compress @($out)
'''


def test_ubt_shared_file_collision_is_detected(tmp_path):
    logs = []
    for i, (text, _) in enumerate(UBT_LOGS):
        path = tmp_path / f'Build_{i}.log'
        # build.ps1's Tee-Object writes UTF-16 on Windows PowerShell; the check must read either
        path.write_text(text, encoding='utf-16' if i % 2 else 'utf-8')
        logs.append(str(path))
    script_file = tmp_path / 'collision.ps1'
    script_file.write_text(COLLISION_SCRIPT, encoding='utf-8')
    env = dict(os.environ, N2C_BUILD_PS1=BUILD_PS1, N2C_LOGS=';'.join(logs))
    proc = subprocess.run([SHELL, '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', str(script_file)],
                          capture_output=True, text=True, env=env, timeout=120)
    assert proc.returncode == 0, proc.stderr
    assert json.loads(proc.stdout) == [want for _, want in UBT_LOGS]


LINK_SCRIPT = r'''
$ErrorActionPreference = 'Stop'
$ast = [System.Management.Automation.Language.Parser]::ParseFile($env:N2C_BUILD_PS1, [ref]$null, [ref]$null)
foreach ($name in @('Get-LinkTarget', 'Set-HostLink')) {
    $fn = $ast.Find({ param($a) $a -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $a.Name -eq $name }, $true)
    if (-not $fn) { throw "build.ps1 has no function $name" }
    . ([scriptblock]::Create($fn.Extent.Text))
}
$OnWindows = ($PSVersionTable.PSEdition -ne 'Core') -or $IsWindows
$root = $env:N2C_ROOT
$target = Join-Path $root 'Tests/NodeToCodeTestContent'
$link = Join-Path $root 'Host/Plugins/NodeToCodeTestContent'
New-Item -ItemType Directory -Force $target, (Split-Path $link -Parent) | Out-Null
Set-HostLink $link $target
Set-HostLink $link $target
$made = (Get-LinkTarget (Get-Item $link -Force)) -eq ([IO.Path]::GetFullPath($target)).TrimEnd('\', '/')
$plain = Join-Path $root 'Host/Plugins/Other'
New-Item -ItemType Directory -Force $plain | Out-Null
$refused = $false
try { Set-HostLink $plain $target } catch { $refused = $_.Exception.Message -like '*is not a link to*' }
ConvertTo-Json -Compress @($made, $refused)
'''


def test_host_link_is_made_once_and_refuses_a_folder(tmp_path):
    """build.ps1's Set-HostLink, which links the plugin folders and Tests/NodeToCodeTestContent (card 0359) into a host:
    makes the link, leaves it alone on the next run, and refuses to replace anything that is not that link"""
    script_file = tmp_path / 'link.ps1'
    script_file.write_text(LINK_SCRIPT, encoding='utf-8')
    env = dict(os.environ, N2C_BUILD_PS1=BUILD_PS1, N2C_ROOT=str(tmp_path / 'root'))
    proc = subprocess.run([SHELL, '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', str(script_file)],
                          capture_output=True, text=True, env=env, timeout=120)
    assert proc.returncode == 0, proc.stderr
    assert json.loads(proc.stdout) == [True, True]


def test_host_links_the_test_content_plugin():
    """Initialize-HostProject links Tests/NodeToCodeTestContent into the host's Plugins folder (card 0359), and the
    plugin it links is content only, so the perf fixtures load in every host and never ship"""
    with open(BUILD_PS1, encoding='utf-8') as f:
        text = f.read()
    assert "$TestContentPlugin = 'NodeToCodeTestContent'" in text
    assert "Set-HostLink (Join-Path $h.Dir (Join-Path 'Plugins' $TestContentPlugin))" in text
    with open(os.path.join(REPO_ROOT, 'Tests', 'NodeToCodeTestContent', 'NodeToCodeTestContent.uplugin'), encoding='utf-8') as f:
        descriptor = json.load(f)
    assert descriptor['CanContainContent'] is True
    assert not descriptor.get('Modules')


WARM_SCRIPT = r'''
$ErrorActionPreference = 'Stop'
$ast = [System.Management.Automation.Language.Parser]::ParseFile($env:N2C_BUILD_PS1, [ref]$null, [ref]$null)
$fn = $ast.Find({ param($a) $a -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $a.Name -eq 'Initialize-HostWarm' }, $true)
if (-not $fn) { throw 'build.ps1 has no function Initialize-HostWarm' }
. ([scriptblock]::Create($fn.Extent.Text))
$HostWarmStamp = 'Saved/N2CHostWarm.txt'
$LogRoot = $env:N2C_ROOT
$script:calls = @()
$script:exit = [int]$env:N2C_EXIT
$script:writes = $env:N2C_WRITES -eq '1'
function Write-Step($m) { }
function Get-EngineTool($engine, $tool) { "$engine/$tool" }
function Invoke-Editor($exe, $argList, $timeoutMinutes) {
    $script:calls += ,(@($exe) + $argList)
    if ($script:writes) { Set-Content (Join-Path $env:N2C_HOST 'Config/DefaultInput.ini') '[/Script/Engine.InputSettings]' }
    return $script:exit
}
$h = [pscustomobject]@{ Dir = $env:N2C_HOST; UProject = (Join-Path $env:N2C_HOST 'Host.uproject') }
$warm = Initialize-HostWarm $h 'E' '5.6' 'stamp'
ConvertTo-Json -Compress -Depth 4 @{ warm = $warm; calls = @($script:calls); stamped = (Test-Path (Join-Path $env:N2C_HOST $HostWarmStamp)) }
'''


def _warm(tmp_path, input_ini=False, stamp=False, exit_code=0):
    host = tmp_path / 'host'
    (host / 'Config').mkdir(parents=True)
    if input_ini:
        (host / 'Config' / 'DefaultInput.ini').write_text('[/Script/Engine.InputSettings]\n', encoding='utf-8')
    if stamp:
        (host / 'Saved').mkdir()
        (host / 'Saved' / 'N2CHostWarm.txt').write_text('warm\n', encoding='utf-8')
    script_file = tmp_path / 'warm.ps1'
    script_file.write_text(WARM_SCRIPT, encoding='utf-8')
    env = dict(os.environ, N2C_BUILD_PS1=BUILD_PS1, N2C_ROOT=str(tmp_path), N2C_HOST=str(host), N2C_EXIT=str(exit_code),
               N2C_WRITES='1')
    proc = subprocess.run([SHELL, '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', str(script_file)],
                          capture_output=True, text=True, env=env, timeout=120)
    assert proc.returncode == 0, proc.stderr
    result = json.loads(proc.stdout.strip().splitlines()[-1])
    result['calls'] = result['calls'] or []
    return result


def test_a_new_host_gets_one_quitting_start_before_its_first_smoke(tmp_path):
    """Card 0513: a host the engine never opened has no Config/DefaultInput.ini; the engine writes it on the first start,
    which failed the first smoke's 'host Config/*.ini unchanged' check. Initialize-HostWarm starts it once, headless,
    quitting at once, and stamps it"""
    result = _warm(tmp_path)
    assert result['warm'] is True
    assert len(result['calls']) == 1
    call = result['calls'][0]
    assert call[0] == 'E/EditorCmd'
    assert any(str(a).endswith('Host.uproject') for a in call)
    assert '-ExecCmds="QUIT_EDITOR"' in call and '-nullrhi' in call and '-unattended' in call
    assert result['stamped'] is True
    # The next run does not start it again
    again = _warm(tmp_path / 'again', stamp=True)
    assert again['calls'] == [] and again['warm'] is True


def test_a_host_opened_before_is_only_stamped(tmp_path):
    result = _warm(tmp_path, input_ini=True)
    assert result == {'warm': True, 'calls': [], 'stamped': True}


def test_a_failed_warm_up_is_reported_and_not_stamped(tmp_path):
    result = _warm(tmp_path, exit_code=3)
    assert result['warm'] is False and result['stamped'] is False and len(result['calls']) == 1


def test_the_test_editor_renders_off_screen():
    """Card 0509: under -nullrhi without -RenderOffScreen, opening an asset editor (Material Editor, Blueprint editor)
    is fatal on UE 5.6 and 5.7 (GenericWindow.cpp, GetRestoredDimensions); the -Test editor passes -RenderOffScreen"""
    with open(BUILD_PS1, encoding='utf-8') as f:
        text = f.read()
    start = text.index('function Invoke-Tests')
    body = text[start:text.index('$editorArgs $TestTimeoutMinutes', start)]
    assert "'-nullrhi', '-RenderOffScreen'" in body
