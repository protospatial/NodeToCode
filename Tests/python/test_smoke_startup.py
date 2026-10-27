# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
Card 0495: the smoke check's answers to three startup failures that are not NodeToCode code.

- The engine's FDataflowToolNodeSnapshot::Date LogClass error is allowed on UE 5.8 only, as that exact line.
- Every EditorSession editor uses the file-system DDC graph (not the local Zen server that UE 5.6-5.8 share and shut
  down for each other) and skips UE 5.8's startup Build.bat call (which waits on the machine-wide Build.bat lock).
- build.ps1's automation-test editor gets the same two settings.
"""

import os
import re
import sys

import pytest

REPO_ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
for sub in ('Tests/smoke', 'Tests/mcp_probe'):
    path = os.path.join(REPO_ROOT, sub)
    if path not in sys.path:
        sys.path.insert(0, path)

import editor_session  # noqa: E402
import smoke_check  # noqa: E402

PREFIX = '[2026.10.26-19.48.57:364][  0]'
TAIL = ' Module:DataflowNodes File:Public/Dataflow/DataflowToolNode.h'
# The two forms FindUninitializedScriptStructMembers logs (Class.cpp), as lane-b's 5.8 smoke logged the first
ZEROED = ('LogClass: Error: StructProperty FDataflowToolNodeSnapshot::Date is not initialized properly even though its '
          'struct probably has a custom default constructor. Non deterministic fields should use '
          'UPROPERTY(Meta = (IgnoreForMemberInitializationTest)) to avoid errors from this test.' + TAIL)
NO_INIT = 'LogClass: Error: StructProperty FDataflowToolNodeSnapshot::Date is not initialized properly.' + TAIL
ECHO = ZEROED.replace('LogClass: Error: ', 'LogAutomationTest: Error: LogClass: ', 1)


@pytest.mark.parametrize('line', [ZEROED, NO_INIT, ECHO])
@pytest.mark.parametrize('session', smoke_check.SESSIONS)
def test_engine_dataflow_line_is_allowed_on_58(line, session):
    failures, allowed = smoke_check.scan_log(PREFIX + line + '\n', session, '5.8')
    assert failures == [] and len(allowed) == 1


@pytest.mark.parametrize('label', ['5.6', '5.7'])
def test_engine_dataflow_line_fails_on_engines_without_it(label):
    failures, _ = smoke_check.scan_log(PREFIX + ZEROED + '\n', 'default', label)
    assert len(failures) == 1


@pytest.mark.parametrize('line', [
    ZEROED.replace('::Date', '::Name'),                                    # another member of the struct
    ZEROED.replace('FDataflowToolNodeSnapshot', 'FN2CToolSnapshot'),       # another struct
    ZEROED.replace('DataflowNodes', 'NodeToCode'),                         # another module
    ZEROED + ' and more',                                                  # not the whole line
    'LogClass: Error: ObjectProperty FDataflowToolNodeSnapshot::Date is not initialized properly.' + TAIL,
    'LogNodeToCode: Error: StructProperty FDataflowToolNodeSnapshot::Date is not initialized properly.' + TAIL,
])
def test_other_lines_still_fail_on_58(line):
    failures, allowed = smoke_check.scan_log(PREFIX + line + '\n', 'default', '5.8')
    assert len(failures) == 1 and allowed == []


def test_every_allowlist_entry_names_engines_and_a_reason():
    for rx, sessions, engines, why in smoke_check.ALLOWLIST:
        assert rx.pattern.startswith('^') and rx.pattern.endswith('$'), rx.pattern
        assert set(sessions) <= set(smoke_check.SESSIONS) and engines and why


def test_last_log_line_shows_where_a_start_stopped():
    text = ("[2026.10.26-19.52.26:658][  0]LogInit: Initializing FReadOnlyCVARCache\r\n"
            "[2026.10.26-19.52.26:658][  0]LogModuleManager: InternalLoadLibrary: 'TurnkeySupport'\r\n\0\r\n")
    assert smoke_check.last_log_line(text) == "LogModuleManager: InternalLoadLibrary: 'TurnkeySupport'"
    assert smoke_check.last_log_line('') == '(empty log)'


@pytest.fixture
def session(tmp_path):
    exe = tmp_path / 'Engine' / 'Binaries' / 'Win64' / 'UnrealEditor-Cmd.exe'
    exe.parent.mkdir(parents=True)
    exe.write_bytes(b'')
    return editor_session.EditorSession(str(tmp_path), str(tmp_path / 'Host.uproject'), 27170,
                                        str(tmp_path / 'editor.log'), extra_env={'N2C_TEST_KEY': 'x'})


def test_session_uses_the_file_system_ddc(session):
    assert '-ddc=InstalledNoZenLocalFallback' in session.args()


def test_session_skips_the_58_startup_build_bat(session):
    env = session.env()
    assert env['UE_SKIP_UBT_SDK_SETUP'] == '1'
    assert env['N2C_TEST_KEY'] == 'x'          # extra_env still reaches the editor
    assert 'PATH' in env or 'Path' in env      # on top of this process's environment


def test_build_ps1_test_editor_gets_the_same_settings():
    text = open(os.path.join(REPO_ROOT, 'build.ps1'), encoding='utf-8').read()
    args = re.search(r"^\$EditorDdcArgs = @\(([^)]*)\)", text, re.M)
    env = re.search(r"^\$EditorStartupEnv = @\{([^}]*)\}", text, re.M)
    assert args and env
    assert re.findall(r"'([^']*)'", args.group(1)) == list(editor_session.DDC_ARGS)
    pairs = re.findall(r"'([^']*)'\s*=\s*'([^']*)'", env.group(1))
    assert dict(pairs) == editor_session.STARTUP_ENV
    invoke = re.search(r'function Invoke-Editor.*?\n\}', text, re.S).group(0)
    assert '$EditorDdcArgs' in invoke and '$EditorStartupEnv' in invoke
