# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
NodeToCode's Claude Code plugin (card 0172, Agents/claude-code): Agents/sync_skills.py's plugin check passes on the
repository and catches a version that drifts from NodeToCode.uplugin, a command naming an unregistered MCP tool and a
marketplace that does not point at the plugin; the MCP config, hook config and hook script agree with each other and
with the editor's default port; and the SessionStart hook, run in bash with no editor answering, prints one JSON line
and exits 0 in under 2 seconds.
"""

import importlib.util
import json
import os
import re
import shutil
import socket
import subprocess
import time

import pytest

from conftest import REPO_ROOT

PLUGIN = os.path.join(REPO_ROOT, 'Agents', 'claude-code', 'nodetocode')
MARKETPLACE = os.path.join(REPO_ROOT, 'Agents', 'claude-code')


@pytest.fixture(scope='module')
def sync_skills():
    spec = importlib.util.spec_from_file_location('sync_skills', os.path.join(REPO_ROOT, 'Agents', 'sync_skills.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


@pytest.fixture
def plugin_copy(tmp_path):
    """A writable copy of Agents/claude-code (the marketplace, the entry draft and the plugin)"""
    target = tmp_path / 'claude-code'
    shutil.copytree(MARKETPLACE, target)
    return target / 'nodetocode'


def _load(path):
    with open(path, encoding='utf-8') as f:
        return json.load(f)


def _save(path, data):
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(data, f, indent=2)


def test_repository_plugin_passes_check(sync_skills):
    assert sync_skills.check_claude_plugin() == []


def test_check_catches_version_drift(sync_skills, plugin_copy):
    manifest = plugin_copy / '.claude-plugin' / 'plugin.json'
    data = _load(manifest)
    data['version'] = '0.0.1'
    _save(manifest, data)
    findings = sync_skills.check_claude_plugin(plugin_dir=str(plugin_copy))
    assert any('differs from NodeToCode.uplugin VersionName' in f for f in findings), findings


def test_check_catches_unregistered_tool_in_command(sync_skills, plugin_copy):
    with open(plugin_copy / 'commands' / 'translate.md', 'a', encoding='utf-8', newline='\n') as f:
        f.write('\nThen call `mcp__plugin_nodetocode_nodetocode__translate-everything-now`.\n')
    findings = sync_skills.check_claude_plugin(plugin_dir=str(plugin_copy))
    assert any("'translate-everything-now'" in f for f in findings), findings


def test_check_catches_marketplace_pointing_elsewhere(sync_skills, plugin_copy):
    market = plugin_copy.parent / '.claude-plugin' / 'marketplace.json'
    data = _load(market)
    data['plugins'][0]['source'] = './elsewhere'
    _save(market, data)
    entry = plugin_copy.parent / 'marketplace-entry.json'
    data = _load(entry)
    data['source']['path'] = 'Agents/nodetocode'
    _save(entry, data)
    findings = sync_skills.check_claude_plugin(plugin_dir=str(plugin_copy))
    assert any('marketplace.json' in f for f in findings), findings
    assert any('marketplace-entry.json' in f for f in findings), findings


def test_mcp_config_matches_manifest_and_editor_default():
    manifest = _load(os.path.join(PLUGIN, '.claude-plugin', 'plugin.json'))
    server = _load(os.path.join(PLUGIN, '.mcp.json'))['mcpServers']['nodetocode']
    assert server['type'] == 'http'
    match = re.fullmatch(r'\$\{user_config\.(\w+)\}', server['url'])
    assert match and match.group(1) in manifest['userConfig']
    default = manifest['userConfig'][match.group(1)]['default']
    with open(os.path.join(REPO_ROOT, 'Source', 'Public', 'Core', 'N2CSettings.h'), encoding='utf-8') as f:
        port = re.search(r'int32 McpServerPort = (\d+);', f.read()).group(1)
    assert default == 'http://127.0.0.1:%s/mcp' % port


def test_hook_config_runs_the_bundled_script_quickly():
    hooks = _load(os.path.join(PLUGIN, 'hooks', 'hooks.json'))['hooks']
    [group] = hooks['SessionStart']
    [hook] = group['hooks']
    assert hook['type'] == 'command' and hook['timeout'] <= 10
    assert '${CLAUDE_PLUGIN_ROOT}/hooks/session-start.sh' in hook['command']
    with open(os.path.join(PLUGIN, 'hooks', 'session-start.sh'), 'rb') as f:
        assert b'\r\n' not in f.read(), 'the hook must keep LF line ends (.gitattributes: *.sh eol=lf)'


def _git_bash():
    if os.name != 'nt':
        return shutil.which('bash')
    for candidate in (os.environ.get('CLAUDE_CODE_GIT_BASH_PATH'), r'C:\Program Files\Git\bin\bash.exe',
                      r'C:\Program Files\Git\usr\bin\bash.exe'):
        if candidate and os.path.isfile(candidate):
            return candidate
    return None


def _dead_port():
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


@pytest.mark.skipif(_git_bash() is None or shutil.which('curl') is None, reason='needs bash and curl')
def test_hook_without_editor_prints_one_line_fast(tmp_path):
    empty = tmp_path / 'discovery'
    empty.mkdir()
    env = dict(os.environ, CLAUDE_PLUGIN_OPTION_MCP_URL='http://127.0.0.1:%d/mcp' % _dead_port(),
               NODETOCODE_UNREAL_MCP_URL='http://127.0.0.1:%d/mcp' % _dead_port(),
               NODETOCODE_DISCOVERY_DIR=str(empty), CLAUDE_PROJECT_DIR=str(tmp_path))
    t0 = time.perf_counter()
    proc = subprocess.run([_git_bash(), os.path.join(PLUGIN, 'hooks', 'session-start.sh')], capture_output=True,
                          env=env, cwd=str(tmp_path), timeout=30, stdin=subprocess.DEVNULL)
    seconds = time.perf_counter() - t0
    lines = [l for l in proc.stdout.decode('utf-8').splitlines() if l.strip()]
    assert proc.returncode == 0
    assert len(lines) == 1
    out = json.loads(lines[0])
    assert 'no editor is serving' in out['systemMessage']
    assert out['hookSpecificOutput']['hookEventName'] == 'SessionStart'
    assert seconds < 2.0, seconds


@pytest.mark.skipif(_git_bash() is None or shutil.which('curl') is None, reason='needs bash and curl')
def test_hook_finds_the_editor_on_another_port_from_its_discovery_entry(tmp_path):
    """The editor writes its discovery entry pretty-printed (tabs, "key": "value"); the hook must read it"""
    import http.server
    import threading

    class Health(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            body = b'{"status":"ok","service":"NodeToCode MCP Server"}'
            self.send_response(200 if self.path == '/mcp/health' else 404)
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *args):
            pass

    server = http.server.HTTPServer(('127.0.0.1', 0), Health)
    port = server.server_address[1]
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        project = tmp_path / 'MyGame'
        (project / 'Saved' / 'NodeToCode').mkdir(parents=True)
        entry = {'server': 'NodeToCode', 'port': port, 'url': 'http://127.0.0.1:%d/mcp' % port, 'pid': os.getpid(),
                 'projectName': 'MyGame', 'engineVersion': '5.8.3-58210709+++UE5+Release-5.8'}
        with open(project / 'Saved' / 'NodeToCode' / 'mcp-server.json', 'w', encoding='utf-8', newline='\r\n') as f:
            json.dump(entry, f, indent='\t', separators=(',', ': '))
        empty = tmp_path / 'discovery'
        empty.mkdir()
        dead = _dead_port()
        env = dict(os.environ, CLAUDE_PLUGIN_OPTION_MCP_URL='http://127.0.0.1:%d/mcp' % dead,
                   NODETOCODE_UNREAL_MCP_URL='http://127.0.0.1:%d/mcp' % _dead_port(),
                   NODETOCODE_DISCOVERY_DIR=str(empty), CLAUDE_PROJECT_DIR=str(project))
        proc = subprocess.run([_git_bash(), os.path.join(PLUGIN, 'hooks', 'session-start.sh')], capture_output=True,
                              env=env, cwd=str(project), timeout=30, stdin=subprocess.DEVNULL)
    finally:
        server.shutdown()
    assert proc.returncode == 0
    message = json.loads(proc.stdout.decode('utf-8'))['systemMessage']
    assert 'nothing at http://127.0.0.1:%d/mcp' % dead in message, message
    assert 'MyGame, UE 5.8.3 serves NodeToCode at http://127.0.0.1:%d/mcp' % port in message, message
