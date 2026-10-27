# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
The stdio bridge's MCP Bundle and MCP Registry entry (card 0310), on plain CPython: Content/Python/mcp_bridge/
manifest.json and server.json carry the plugin's VersionName, run the bridge script that exists, pass the two optional
settings as the bridge's own arguments, and leave out everything but the manifest and the bridge. The schema checks
themselves need the mcpb CLI and the registry schema (see the folder's README.md).
"""

import json
import os
import re

BRIDGE_DIR = os.path.join(os.path.dirname(__file__), '..', '..', 'Content', 'Python', 'mcp_bridge')
UPLUGIN = os.path.join(os.path.dirname(__file__), '..', '..', 'NodeToCode.uplugin')


def load(name):
    with open(os.path.join(BRIDGE_DIR, name), encoding='utf-8') as f:
        return json.load(f)


def plugin_version():
    with open(UPLUGIN, encoding='utf-8-sig') as f:
        return json.load(f)['VersionName']


def test_manifest_matches_the_plugin_and_runs_the_bridge():
    manifest = load('manifest.json')
    assert manifest['version'] == plugin_version()
    server = manifest['server']
    assert server['type'] == 'python'
    assert os.path.isfile(os.path.join(BRIDGE_DIR, server['entry_point']))
    args = server['mcp_config']['args']
    assert args[0] == '${__dirname}/' + server['entry_point']
    # Each setting is passed to the bridge's own option, and both default to empty (the bridge then finds the editor)
    for key, option in (('project', '--project'), ('mcp_url', '--url')):
        assert args[args.index(option) + 1] == '${user_config.%s}' % key
        assert manifest['user_config'][key]['default'] == '' and not manifest['user_config'][key]['required']


def test_server_json_names_the_bundle_of_this_version():
    server = load('server.json')
    version = plugin_version()
    assert server['name'] == 'io.github.protospatial/nodetocode'
    assert server['version'] == version and len(server['description']) <= 100
    package = server['packages'][0]
    assert package['registryType'] == 'mcpb' and package['transport'] == {'type': 'stdio'}
    assert package['identifier'].endswith('/v%s/nodetocode-%s.mcpb' % (version, version))
    assert re.fullmatch(r'[a-f0-9]{64}', package['fileSha256'])


def test_the_bundle_leaves_out_everything_but_the_bridge():
    with open(os.path.join(BRIDGE_DIR, '.mcpbignore'), encoding='utf-8') as f:
        ignored = {line.strip() for line in f if line.strip() and not line.startswith('#')}
    for name in os.listdir(BRIDGE_DIR):
        if name in ('manifest.json', 'nodetocode_bridge.py', '.mcpbignore'):
            continue
        assert name in ignored or name + '/' in ignored, f'{name} would be packed into the bundle'
