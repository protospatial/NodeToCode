# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
Scripts/N2CEngines.ps1, the engine lookup build.ps1 and Scripts/package-release.ps1 share (card 0532). Each case
dot-sources the helper in a fresh PowerShell under Set-StrictMode 2.0 (as package-release.ps1 runs) and looks up a
version no real install has (5.99), so the registry and the launcher's list never answer, and the order shown is the
helper's own: override, then N2C_UE_<ver>, then build.local.ps1's $Engines, then the default install folder. Needs
powershell.exe or pwsh on PATH; skipped without one.
"""

import json
import os
import shutil
import subprocess

import pytest

REPO_ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
HELPER = os.path.join(REPO_ROOT, 'Scripts', 'N2CEngines.ps1')
SHELL = shutil.which('pwsh') or shutil.which('powershell')
VER = '5.99'

pytestmark = pytest.mark.skipif(SHELL is None, reason='needs pwsh or powershell on PATH')


def _engine(folder, version=VER):
    """A folder that looks like an engine install of the given version (Engine/Build/Build.version)."""
    major, minor = version.split('.')
    build = folder / 'Engine' / 'Build'
    build.mkdir(parents=True)
    (build / 'Build.version').write_text(json.dumps({'MajorVersion': int(major), 'MinorVersion': int(minor)}),
                                         encoding='utf-8')
    return str(folder)


def _find(override='', env=None, local_file='', program_files=None):
    environ = {k: v for k, v in os.environ.items() if k != 'N2C_UE_5_99'}
    if env:
        environ['N2C_UE_5_99'] = env
    # Windows sets %ProgramFiles% itself for every process, so a test's default folder goes in as -DefaultFolders
    folders = f" -DefaultFolders '{os.path.join(program_files, 'Epic Games', 'UE_' + VER)}'" if program_files else ''
    script = (
        "Set-StrictMode -Version 2.0; $ErrorActionPreference = 'Stop'; "
        f". '{HELPER}'; "
        f"$local = Read-N2CLocalEngines '{local_file}'; "
        f"$r = Find-N2CEngine '{VER}' -Override '{override}' -LocalEngines $local{folders}; "
        "[pscustomobject]@{ Path = $r.Path; Source = $r.Source; "
        "Leaked = [bool](Get-Variable HostsRoot -ErrorAction SilentlyContinue) } | ConvertTo-Json -Compress"
    )
    out = subprocess.run([SHELL, '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-Command', script],
                         capture_output=True, text=True, env=environ, timeout=60)
    assert out.returncode == 0, out.stderr
    return json.loads(out.stdout.strip().splitlines()[-1])


def _local(tmp_path, engine_path):
    f = tmp_path / 'build.local.ps1'
    f.write_text(f"$HostsRoot = 'X:\\Hosts'\n$Engines = @{{ '{VER}' = '{engine_path}' }}\n", encoding='utf-8')
    return str(f)


def test_the_override_wins(tmp_path):
    over = _engine(tmp_path / 'over')
    env = _engine(tmp_path / 'env')
    r = _find(override=over, env=env, local_file=_local(tmp_path, _engine(tmp_path / 'local')))
    assert (r['Path'], r['Source']) == (over, '-Engine')


def test_the_environment_variable_comes_before_build_local(tmp_path):
    env = _engine(tmp_path / 'env')
    r = _find(env=env, local_file=_local(tmp_path, _engine(tmp_path / 'local')))
    assert (r['Path'], r['Source']) == (env, '$env:N2C_UE_5_99')


def test_build_local_engines_are_read_without_its_other_settings(tmp_path):
    local = _engine(tmp_path / 'local')
    r = _find(local_file=_local(tmp_path, local))
    assert (r['Path'], r['Source']) == (local, 'build.local.ps1')
    assert r['Leaked'] is False


def test_a_wrong_explicit_setting_is_reported_not_skipped(tmp_path):
    wrong = _engine(tmp_path / 'wrong', '5.98')
    fallback = tmp_path / 'pf'
    _engine(fallback / 'Epic Games' / f'UE_{VER}')
    r = _find(env=wrong, program_files=str(fallback))
    assert r['Path'] is None
    assert r['Source'] == f"$env:N2C_UE_5_99 '{wrong}' is not a UE {VER} install"


def test_the_launcher_default_folder_is_found_with_nothing_set(tmp_path):
    pf = tmp_path / 'pf'
    default = _engine(pf / 'Epic Games' / f'UE_{VER}')
    r = _find(program_files=str(pf))
    assert (r['Path'], r['Source']) == (default, 'default folder')


def test_nothing_found_says_so(tmp_path):
    r = _find(program_files=str(tmp_path / 'empty'))
    assert (r['Path'], r['Source']) == (None, 'not found')
