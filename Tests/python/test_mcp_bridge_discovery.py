# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
The stdio bridge uses a discovery entry only when its url is plain http to this machine (card 0426, threat model
0090). Content/Python/mcp_bridge/nodetocode_bridge.py reads editor entries from the per-user index and, with
--project <folder>, from <Project>/Saved/NodeToCode/mcp-server.json, which a downloaded project can ship. An entry
naming any live pid and another host must be logged and skipped, never used. Host names other than localhost are
refused without being resolved. An explicit --url/--port stays the user's choice.
"""

import importlib.util
import json
import os
import socket

import pytest

BRIDGE_PATH = os.path.join(os.path.dirname(__file__), '..', '..', 'Content', 'Python', 'mcp_bridge', 'nodetocode_bridge.py')


@pytest.fixture(scope='module')
def bridge():
    spec = importlib.util.spec_from_file_location('n2c_stdio_bridge_discovery_under_test', BRIDGE_PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


@pytest.fixture
def no_dns(monkeypatch):
    """Fails the test if the check resolves a host name (it must decide from the text alone)."""
    def refuse(*args, **kwargs):
        raise AssertionError('the loopback check resolved a host name: %r' % (args,))
    for name in ('getaddrinfo', 'gethostbyname', 'gethostbyname_ex', 'gethostbyaddr'):
        monkeypatch.setattr(socket, name, refuse)


# url as written in an entry -> url the bridge sends to (rebuilt from the checked parts)
ACCEPTED = [
    ('http://127.0.0.1:27000/mcp', 'http://127.0.0.1:27000/mcp'),
    ('HTTP://127.0.0.1:27000/mcp', 'http://127.0.0.1:27000/mcp'),
    ('http://127.0.0.1/mcp', 'http://127.0.0.1/mcp'),
    ('http://127.0.0.1:27000', 'http://127.0.0.1:27000/'),
    ('http://127.1.2.3:27000/mcp', 'http://127.1.2.3:27000/mcp'),
    ('http://127.255.255.254:27000/mcp', 'http://127.255.255.254:27000/mcp'),
    ('http://localhost:27000/mcp', 'http://localhost:27000/mcp'),
    ('http://LocalHost:27000/mcp', 'http://localhost:27000/mcp'),
    ('http://[::1]:27000/mcp', 'http://[::1]:27000/mcp'),
    ('http://[0:0:0:0:0:0:0:1]:27000/mcp', 'http://[::1]:27000/mcp'),
    ('http://[0000:0000:0000:0000:0000:0000:0000:0001]:27000/mcp', 'http://[::1]:27000/mcp'),
    ('http://[::ffff:127.0.0.1]:27000/mcp', 'http://127.0.0.1:27000/mcp'),
    ('http://[::ffff:7f00:2]:27000/mcp', 'http://127.0.0.2:27000/mcp'),
    ('http://127.0.0.1:27000/mcp?x=1#fragment', 'http://127.0.0.1:27000/mcp?x=1'),
]

REFUSED = [
    # other hosts, by address
    'http://10.0.0.5:27000/mcp',
    'http://192.168.1.20:27000/mcp',
    'http://203.0.113.7:27000/mcp',
    'http://0.0.0.0:27000/mcp',
    'http://128.0.0.1:27000/mcp',
    'http://126.255.255.255:27000/mcp',
    'http://[::]:27000/mcp',
    'http://[::2]:27000/mcp',
    'http://[fe80::1]:27000/mcp',
    'http://[fe80::1%25eth0]:27000/mcp',
    'http://[::1%25eth0]:27000/mcp',
    'http://[2001:db8::1]:27000/mcp',
    'http://[::ffff:10.0.0.5]:27000/mcp',
    'http://[::ffff:a00:5]:27000/mcp',
    # loopback spellings that resolvers accept but that are not the plain forms
    'http://127.1:27000/mcp',
    'http://2130706433:27000/mcp',
    'http://0x7f000001:27000/mcp',
    'http://0x7f.0.0.1:27000/mcp',
    'http://0177.0.0.1:27000/mcp',
    'http://127.000.000.001:27000/mcp',
    'http://127.0.0.1.:27000/mcp',
    'http://localhost.:27000/mcp',
    'http://[127.0.0.1]:27000/mcp',
    # host names, including ones that resolve to 127.0.0.1 (never resolved)
    'http://evil.example:27000/mcp',
    'http://localhost.evil.example:27000/mcp',
    'http://127.0.0.1.nip.io:27000/mcp',
    'http://localtest.me:27000/mcp',
    'http://ｌｏｃａｌｈｏｓｔ:27000/mcp',
    'http://localhost%2Eevil.example:27000/mcp',
    # the loopback text in the wrong part of the url
    'http://127.0.0.1@evil.example:27000/mcp',
    'http://user:pass@127.0.0.1:27000/mcp',
    'http://user@localhost:27000/mcp',
    'http://evil.example:27000/mcp#@127.0.0.1',
    'http://evil.example:27000/127.0.0.1/mcp',
    'http://127.0.0.1\\@evil.example:27000/mcp',
    'http://127.0.0.1:27000\\@evil.example/mcp',
    'http://evil.example\\.127.0.0.1:27000/mcp',
    # schemes and malformed urls
    'https://127.0.0.1:27000/mcp',
    'ws://127.0.0.1:27000/mcp',
    'file:///C:/mcp',
    '//127.0.0.1:27000/mcp',
    '127.0.0.1:27000/mcp',
    'http:///mcp',
    'http://:27000/mcp',
    'http://127.0.0.1:0/mcp',
    'http://127.0.0.1:65536/mcp',
    'http://127.0.0.1:port/mcp',
    'http://127.0.0.1:-1/mcp',
    ' http://127.0.0.1:27000/mcp',
    'http://127.0.0.1:27000/mcp ',
    'http://127.0.0.1:27000/m cp',
    'http://127.0.0.1\t:27000/mcp',
    'http://127.0.0.1:27000/mcp\r\nHost: evil.example',
    'http://evil.example\n127.0.0.1:27000/mcp',
    '',
    None,
    27000,
    ['http://127.0.0.1:27000/mcp'],
]


@pytest.mark.parametrize('url,expected', ACCEPTED)
def test_plain_http_loopback_urls_are_accepted_and_rebuilt(bridge, no_dns, url, expected):
    assert bridge.loopback_url(url) == expected


@pytest.mark.parametrize('url', REFUSED)
def test_other_urls_are_refused_without_resolving_them(bridge, no_dns, url):
    assert bridge.loopback_url(url) is None


def entry(port=27000, url=None, pid=4242, name='Proj', folder=None):
    data = {'server': 'NodeToCode', 'port': port, 'pid': pid, 'projectName': name, 'engineVersion': '5.8.0'}
    if url is not None:
        data['url'] = url
    if folder:
        data['projectFile'] = os.path.join(folder, name + '.uproject').replace(os.sep, '/')
    return data


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(data, f)


def alive(pid):
    return True


def test_planted_project_file_with_another_host_is_logged_and_skipped(bridge, no_dns, tmp_path, capsys):
    """The card's case: a downloaded project ships Saved/NodeToCode/mcp-server.json naming a live pid and another
    host, and the editor has not started yet. The bridge must find no editor, not that host."""
    index = tmp_path / 'servers'
    index.mkdir()
    project = tmp_path / 'Downloaded'
    planted = project / 'Saved' / 'NodeToCode' / 'mcp-server.json'
    write(str(planted), entry(url='http://evil.example:80/mcp', pid=os.getpid(), name='Downloaded', folder=str(project)))

    assert bridge.discover_editors(project=str(project), index_dir=str(index), alive=alive) == []
    err = capsys.readouterr().err
    assert 'Ignoring the discovery entry' in err
    assert str(planted) in err
    assert "'http://evil.example:80/mcp'" in err

    locator = bridge.EditorLocator(project=str(project), index_dir=str(index))  # the planted pid is this process: alive
    with pytest.raises(bridge.EditorNotFound):
        locator.locate()


def test_a_refused_entry_does_not_hide_the_real_editor(bridge, no_dns, tmp_path):
    """Refused entries (another host in the index, another host in the project's planted file, both with live pids)
    leave the one real editor, which is picked; a refused entry never claims its pid ahead of a good one."""
    index = tmp_path / 'servers'
    project = tmp_path / 'Proj'
    real = os.getpid()
    write(str(index / '1000.json'), entry(url='http://[2001:db8::1]:27000/mcp', pid=real, name='Other'))
    write(str(index / ('%d.json' % real)), entry(url='http://127.0.0.1:27001/mcp', pid=real, folder=str(project)))
    write(str(project / 'Saved' / 'NodeToCode' / 'mcp-server.json'),
          entry(url='http://10.0.0.5:27000/mcp', pid=real, folder=str(project)))

    editors = bridge.discover_editors(project=str(project), index_dir=str(index), alive=alive)
    assert [e['url'] for e in editors] == ['http://127.0.0.1:27001/mcp']
    url, chosen = bridge.EditorLocator(project=str(project), index_dir=str(index)).locate()
    assert url == 'http://127.0.0.1:27001/mcp'
    assert chosen['discoveryFile'] == str(index / ('%d.json' % real))


def test_index_entry_with_another_host_is_skipped_too(bridge, no_dns, tmp_path, capsys):
    index = tmp_path / 'servers'
    write(str(index / '1000.json'), entry(url='http://127.0.0.1@evil.example:27000/mcp', pid=1000))
    write(str(index / '2000.json'), entry(url='https://localhost:27000/mcp', pid=2000))
    assert bridge.discover_editors(index_dir=str(index), alive=alive) == []
    assert capsys.readouterr().err.count('Ignoring the discovery entry') == 2


def test_entry_url_is_replaced_by_the_rebuilt_one(bridge, no_dns, tmp_path):
    index = tmp_path / 'servers'
    write(str(index / '1000.json'), entry(url='HTTP://LOCALHOST:27000/mcp#x', pid=1000))
    write(str(index / '2000.json'), entry(url='http://[0:0:0:0:0:0:0:1]:27002/mcp', pid=2000))
    urls = sorted(e['url'] for e in bridge.discover_editors(index_dir=str(index), alive=alive))
    assert urls == ['http://[::1]:27002/mcp', 'http://localhost:27000/mcp']


@pytest.mark.parametrize('port,usable', [(27000, True), (27000.0, True), (0, False), (-1, False), (65536, False),
                                         (27000.5, False), (True, False), ('27000', False), (float('inf'), False),
                                         (float('nan'), False), (None, False)])
def test_entry_without_url_uses_its_port_on_127_0_0_1(bridge, no_dns, tmp_path, port, usable):
    index = tmp_path / 'servers'
    write(str(index / '1000.json'), entry(port=port, pid=1000))
    editors = bridge.discover_editors(index_dir=str(index), alive=alive)
    assert [e['url'] for e in editors] == (['http://127.0.0.1:27000/mcp'] if usable else [])


def test_explicit_url_and_port_stay_the_users_choice(bridge, no_dns, tmp_path):
    index = tmp_path / 'servers'
    write(str(index / '1000.json'), entry(url='http://127.0.0.1:27001/mcp', pid=1000))
    explicit = bridge.EditorLocator(url='http://192.168.1.20:27000/mcp', index_dir=str(index))
    assert explicit.locate() == ('http://192.168.1.20:27000/mcp', None)
