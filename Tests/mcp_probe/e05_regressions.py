"""Regression probe for the crash (p0-6) and data-loss (p0-7) fixes of epic E05.

Stdlib only; run with `python -I`. Three ways to run it:

    # MCP cases against a running editor (for example one opened with .\\build.ps1 -UEVersion 5.8 -Launch)
    python -I e05_regressions.py --url http://127.0.0.1:27000/mcp

    # MCP cases against a headless editor this script starts and stops
    python -I e05_regressions.py --engine G:/UE/UE_5.8 --project <host>.uproject

    # Everything headless: the MCP cases on a headless editor, the headless Python checks (e05_struct_check.py,
    # e05_datatable_check.py, e05_registry_check.py) and the automation tests, each through UnrealEditor-Cmd
    python -I e05_regressions.py --headless --engine G:/UE/UE_5.8 --project <host>.uproject [--all-tests]

Prints one PASS, FAIL or SKIP line per case and exits 0 only when nothing failed. The graph cases need run-python
(Allow MCP Python Execution); without it they print SKIP. Every Blueprint the probe makes lives under /Game/E05Probe
and that folder is deleted at the end of the run. See README.md for which case covers which p0-6 / p0-7 item.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from mcp_session import session_headers, url_port  # noqa: E402

PROBE_DIR = '/Game/E05Probe'

# The automation tests that cover p0-6 / p0-7 items with no MCP surface (see README.md); --all-tests runs NodeToCode
DEFAULT_TESTS = 'NodeToCode.Robustness+NodeToCode.Secrets.Store'

# Headless Python checks: (file, log tag). Each prints '<tag> PASS|FAIL <label> <detail>' lines and a final '<tag> PASS'
HEADLESS_CHECKS = [('e05_struct_check.py', 'E05_STRUCT'),
                   ('e05_datatable_check.py', 'E05_DATATABLE'),
                   ('e05_registry_check.py', 'E05_REGISTRY')]


# ---------------------------------------------------------------------------------------------------------------------
# MCP helpers
# ---------------------------------------------------------------------------------------------------------------------

def rpc(url, method, params=None, rid=1):
    """One JSON-RPC request in this script's MCP session (mcp_session.py); a 404 (the session is gone, for example
    the editor on that port restarted) opens a new session and sends the request once more"""
    body = json.dumps({'jsonrpc': '2.0', 'id': rid, 'method': method, 'params': params or {}}).encode('utf-8')
    port = url_port(url)
    for refresh in (False, True):
        headers = dict(session_headers(port, refresh=refresh), **{'Content-Type': 'application/json'})
        req = urllib.request.Request(url, data=body, headers=headers)
        try:
            with urllib.request.urlopen(req, timeout=120) as r:
                return json.loads(r.read().decode('utf-8'))
        except urllib.error.HTTPError as e:
            if e.code != 404 or refresh:
                raise


def tool(url, name, arguments):
    return rpc(url, 'tools/call', {'name': name, 'arguments': arguments})


def tool_text(resp):
    res = resp.get('result') or {}
    return ''.join(c.get('text', '') for c in res.get('content', [])), bool(res.get('isError'))


def tool_json(url, name, arguments):
    """(parsed JSON or None, isError, raw text)"""
    text, error = tool_text(tool(url, name, arguments))
    try:
        return json.loads(text), error, text
    except ValueError:
        return None, error, text


def is_tool_error(resp, needle=None):
    text, error = tool_text(resp)
    return error and (needle is None or needle.lower() in text.lower()), text[:160]


def run_python(url, code):
    """The `result` dict of a run-python call ({} when run-python is off or the code raised), and the raw text.
    run-python executes in the editor's shared __main__ namespace, where a module-level variable that holds an asset
    keeps it loaded (and undeletable) after the call; so the code runs inside a function and only `result`, plain
    data, is left behind."""
    body = '\n'.join('    ' + line for line in code.strip('\n').splitlines())
    wrapped = 'def _n2c_e05():\n%s\n    return result\nresult = _n2c_e05()\ndel _n2c_e05\n' % body
    text, _ = tool_text(tool(url, 'run-python', {'code': wrapped}))
    try:
        return (json.loads(text).get('data') or {}), text[:300]
    except ValueError:
        return {}, text[:300]


def python_available(url):
    data, _ = run_python(url, 'result = {"ok": True}')
    return data.get('ok') is True


# ---------------------------------------------------------------------------------------------------------------------
# Scratch assets (all under /Game/E05Probe)
# ---------------------------------------------------------------------------------------------------------------------

# A scratch Actor Blueprint, optionally with one extra function, compiled and saved (so its package is clean), with
# `focus` opened as the focused graph so the graph tools target it
SCRATCH_BP_PY = r'''
import json, unreal
path = "%(dir)s/%(name)s"
factory = unreal.BlueprintFactory()
factory.set_editor_property("parent_class", unreal.Actor)
bp = unreal.AssetToolsHelpers.get_asset_tools().create_asset("%(name)s", "%(dir)s", unreal.Blueprint, factory)
steps = {"created": bp is not None}
if bp:
    if "%(function)s":
        steps["function"] = unreal.BlueprintEditorLibrary.add_function_graph(bp, "%(function)s") is not None
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    steps["saved"] = unreal.EditorAssetLibrary.save_loaded_asset(bp, False)
    steps["opened"] = json.loads(unreal.N2CPythonBridge.open_blueprint(path, "%(focus)s")).get("success")
    dirty = [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
    steps["clean_before"] = path not in dirty
result = steps
'''

IS_DIRTY_PY = r'''
import unreal
dirty = [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
result = {"dirty": "%(path)s" in dirty}
'''

# Close every editor open on an asset under /Game/E05Probe, then delete the folder. Run at the start (leftovers of an
# aborted run) and at the end of every run
CLEANUP_PY = r'''
import unreal
deleted = True
if unreal.EditorAssetLibrary.does_directory_exist("%(dir)s"):
    aes = unreal.get_editor_subsystem(unreal.AssetEditorSubsystem)
    for asset_path in unreal.EditorAssetLibrary.list_assets("%(dir)s", True, False):
        asset = unreal.EditorAssetLibrary.load_asset(asset_path)
        if asset:
            aes.close_all_editors_for_asset(asset)
    unreal.EditorAssetLibrary.delete_directory("%(dir)s")
    # Judge by what is left: delete_directory also returns False when the folder keeps a file the asset registry
    # does not know (for example a package left by a crashed run), which is not ours to judge here
    deleted = not unreal.EditorAssetLibrary.list_assets("%(dir)s", True, False)
result = {"deleted": deleted}
'''


def scratch_blueprint(url, name, focus='EventGraph', function=''):
    """Create, save and open a scratch Blueprint; returns (asset path, setup steps dict, raw text)"""
    names = {'dir': PROBE_DIR, 'name': name, 'focus': focus, 'function': function}
    steps, text = run_python(url, SCRATCH_BP_PY % names)
    return '%s/%s' % (PROBE_DIR, name), steps, text


def setup_ok(steps):
    return bool(steps.get('saved') and steps.get('opened') and steps.get('clean_before'))


def is_dirty(url, path):
    data, _ = run_python(url, IS_DIRTY_PY % {'path': path})
    return data.get('dirty')


def cleanup(url):
    data, _ = run_python(url, CLEANUP_PY % {'dir': PROBE_DIR})
    return data.get('deleted') is True


def find_action(url, term, node_type, node_name):
    """(name to pass as nodeName, spawnMetadata.actionIdentifier) of the search-blueprint-nodes result for term whose
    type is node_type and whose member_name (or, without one, title) is node_name. N2C JSON 2.0.0 (card 0138) names a
    node by member_name and class; its display title is the optional "title" hint, so the term searched for is the
    name passed on (add-bp-node-to-active-graph matches it against the action's search text, which holds both)."""
    data, _, text = tool_json(url, 'search-blueprint-nodes', {'searchTerm': term, 'maxResults': 20})
    for node in (data or {}).get('nodes', []):
        if node.get('type') == node_type and node.get('member_name', node.get('title')) == node_name:
            return term, node['spawnMetadata']['actionIdentifier']
    raise RuntimeError('search-blueprint-nodes %r has no %s %s: %s' % (term, node_type, node_name, text[:120]))


def add_node(url, term, node_type, node_name, x):
    """Add a node to the focused graph; returns {'guid', 'in': {pinName: guid}, 'out': {pinName: guid}}"""
    display_name, action = find_action(url, term, node_type, node_name)
    data, error, text = tool_json(url, 'add-bp-node-to-active-graph',
                                  {'nodeName': display_name, 'actionIdentifier': action, 'location': {'x': x, 'y': 0}})
    if error or not data or not data.get('success'):
        raise RuntimeError('add-bp-node-to-active-graph %s: %s' % (node_name, text[:160]))
    return {'guid': data['nodeGuid'],
            'in': {p['pinName']: p['pinGuid'] for p in data.get('inputPins', [])},
            'out': {p['pinName']: p['pinGuid'] for p in data.get('outputPins', [])}}


def link(src, src_pin, dst, dst_pin):
    return {'from': {'nodeGuid': src['guid'], 'pinGuid': src['out'][src_pin]},
            'to': {'nodeGuid': dst['guid'], 'pinGuid': dst['in'][dst_pin]}}


def graph_nodes(url, term):
    """find-nodes-in-graph on the focused graph: ({guid without hyphens: node}, totalInGraph)"""
    data, _, text = tool_json(url, 'find-nodes-in-graph', {'searchTerms': [term], 'maxResults': 200})
    if not data:
        raise RuntimeError('find-nodes-in-graph %r: %s' % (term, text[:160]))
    nodes = {n['ids']['guid'].replace('-', '').upper(): n for n in data.get('nodes', [])}
    return nodes, data.get('metadata', {}).get('totalInGraph')


def connected(node, direction, index):
    pins = node.get('output_pins' if direction == 'out' else 'input_pins', [])
    return index < len(pins) and pins[index].get('connected') is True


def branch_and_print(url, x):
    """A Branch and a Print String in the focused graph"""
    return add_node(url, 'Branch', 'Branch', 'Branch', x), add_node(url, 'Print String', 'CallFunction', 'PrintString', x + 400)


def fan_in_state(url, branch, printer):
    """(Branch.True linked, Branch.False linked, PrintString.execute linked)"""
    nodes, _ = graph_nodes(url, 'Branch')
    b = nodes.get(branch['guid'].upper(), {})
    nodes, _ = graph_nodes(url, 'Print')
    p = nodes.get(printer['guid'].upper(), {})
    return connected(b, 'out', 0), connected(b, 'out', 1), connected(p, 'in', 0)


# ---------------------------------------------------------------------------------------------------------------------
# MCP cases. Each returns (True | False | None for skipped, detail)
# ---------------------------------------------------------------------------------------------------------------------

CASES = []


def case(fn):
    CASES.append(fn)
    return fn


@case
def prompts_get_non_string_argument(url, ctx):
    """p0-6: prompts/get with null, array and object arguments returns -32602 (card 0043)"""
    prompts = rpc(url, 'prompts/list').get('result', {}).get('prompts', [])
    if not prompts:
        return None, 'no prompts registered'
    name = prompts[0]['name']
    results = []
    for value in (None, [1, 2], {'a': 1}):
        r = rpc(url, 'prompts/get', {'name': name, 'arguments': {'x': value}})
        results.append(r.get('error', {}).get('code'))
    return all(c == -32602 for c in results), 'error codes %s' % results


@case
def create_variable_long_type(url, ctx):
    """p0-6: 1,100-character typeIdentifier (card 0043)"""
    return is_tool_error(tool(url, 'create-variable', {'variableName': 'N2CProbeVar', 'typeIdentifier': 'x' * 1100}), 'limited to')


@case
def create_local_variable_long_name(url, ctx):
    """p0-6: 1,100-character variableName (card 0043)"""
    return is_tool_error(tool(url, 'create-local-variable', {'variableName': 'v' * 1100, 'typeIdentifier': 'bool'}), 'limited to')


@case
def get_local_variable_node_long_name(url, ctx):
    """p0-6: 1,100-character variableName on a node tool (card 0043)"""
    return is_tool_error(tool(url, 'create-get-local-function-variable-node', {'variableName': 'v' * 1100}), 'limited to')


# create-variable inputs that UE 5.8 asserted on (ClassObject != nullptr in AddMemberVariable) when a bare struct or
# enum name resolved to an object pin (card 0153): (typeIdentifier, expected category, typeObject)
BARE_TYPE_NAMES = [('HitResult', 'struct', '/Script/Engine.HitResult'),
                   ('FHitResult', 'struct', '/Script/Engine.HitResult'),
                   ('Guid', 'struct', '/Script/CoreUObject.Guid'),
                   ('ECollisionChannel', 'byte', '/Script/Engine.ECollisionChannel'),
                   ('AActor', 'object', '/Script/Engine.Actor')]
# Inputs that must be refused before the Blueprint is touched: (arguments beyond the variable name, error text)
REFUSED_TYPES = [({'typeIdentifier': 'multicastdelegate'}, 'INVALID_TYPE'),
                 ({'typeIdentifier': 'wildcard'}, 'INVALID_TYPE'),
                 ({'typeIdentifier': 'bool', 'containerType': 'set'}, 'INVALID_SET_ELEMENT_TYPE'),
                 ({'typeIdentifier': 'TSubclassOf<AActor>'}, 'INVALID_TYPE')]


@case
def create_variable_bare_type_names(url, ctx):
    """p0-6: create-variable with bare struct, enum and prefixed class names (HitResult crashed UE 5.8) makes variables
    of the right type that compile; delegate, wildcard, bool-set and template inputs are refused and add nothing
    (card 0153)"""
    if not ctx['python']:
        return None, 'run-python is off'
    _, steps, text = scratch_blueprint(url, 'BP_E05Types_%d' % os.getpid())
    if not setup_ok(steps):
        return False, 'setup %s %s' % (steps, text if not steps else '')
    problems = []
    for index, (type_id, _, _) in enumerate(BARE_TYPE_NAMES):
        data, error, raw = tool_json(url, 'create-variable', {'variableName': 'N2CType%d' % index, 'typeIdentifier': type_id})
        if error or not (data or {}).get('success'):
            problems.append('%s: %s' % (type_id, raw[:120]))
    for index, (args, needle) in enumerate(REFUSED_TYPES):
        refused, raw = is_tool_error(tool(url, 'create-variable', dict(args, variableName='N2CRefused%d' % index)), needle)
        if not refused:
            problems.append('%s not refused with %s: %s' % (args, needle, raw[:100]))
    data, _, raw = tool_json(url, 'get-blueprint-member-variables', {})
    variables = {v.get('name'): v.get('type', {}) for v in (data or {}).get('variables', [])}
    for index, (type_id, category, type_object) in enumerate(BARE_TYPE_NAMES):
        got = variables.get('N2CType%d' % index, {})
        if got.get('category') != category or got.get('typeObject') != type_object:
            problems.append('%s is %s %s, expected %s %s' % (type_id, got.get('category'), got.get('typeObject'),
                                                             category, type_object))
    added = [name for name in variables if name and name.startswith('N2CRefused')]
    if added:
        problems.append('refused inputs added %s' % added)
    data, _, raw = tool_json(url, 'compile-blueprint', {})
    errors = ((data or {}).get('results') or {}).get('errorCount')
    if errors != 0:
        problems.append('compile errorCount=%s %s' % (errors, raw[:120]))
    return not problems, '; '.join(problems) or '%d types made with the right pin type, %d refused, 0 compile errors' % (
        len(BARE_TYPE_NAMES), len(REFUSED_TYPES))


@case
def read_content_browser_huge_page(url, ctx):
    """p0-6: page 2147483647 (card 0043)"""
    return is_tool_error(tool(url, 'read-content-browser-path', {'path': '/Game', 'page': 2147483647}))


@case
def add_return_pin_marks_package_dirty(url, ctx):
    """p0-7: on a saved, clean Blueprint, add-function-return-pin leaves the package dirty (card 0064)"""
    if not ctx['python']:
        return None, 'run-python is off'
    path, steps, text = scratch_blueprint(url, 'BP_E05Dirty_%d' % os.getpid(), 'N2CProbeFunction', 'N2CProbeFunction')
    if not setup_ok(steps):
        return False, 'setup %s %s' % (steps, text if not steps else '')
    added = tool(url, 'add-function-return-pin', {'pinName': 'bOk', 'typeIdentifier': 'bool'})
    add_error, add_text = is_tool_error(added)
    dirty = is_dirty(url, path)
    return (not add_error and dirty is True,
            'add error=%s dirty_after=%s %s' % (add_error, dirty, add_text[:80] if add_error else ''))


@case
def search_blueprint_nodes_read_only(url, ctx):
    """p0-7: search-blueprint-nodes leaves the focused graph and its package unchanged (card 0050)"""
    if not ctx['python']:
        return None, 'run-python is off'
    path, steps, text = scratch_blueprint(url, 'BP_E05Search_%d' % os.getpid())
    if not setup_ok(steps):
        return False, 'setup %s %s' % (steps, text if not steps else '')
    before, total_before = graph_nodes(url, 'Event')
    hits = 0
    for term in ('BeginPlay', 'Event Tick', 'Print String', 'Branch'):
        data, error, _ = tool_json(url, 'search-blueprint-nodes', {'searchTerm': term, 'maxResults': 20})
        hits += len((data or {}).get('nodes', [])) if not error else 0
    after, total_after = graph_nodes(url, 'Event')
    dirty = is_dirty(url, path)
    ok = hits > 0 and total_before == total_after and set(before) == set(after) and before and dirty is False
    return ok, 'results=%d nodes %s->%s events %d->%d dirty=%s' % (hits, total_before, total_after, len(before),
                                                                   len(after), dirty)


@case
def connect_pins_fan_in_keeps_both_links(url, ctx):
    """p0-7: connect-pins wires Branch.True and Branch.False into one exec input and both links stay (card 0046)"""
    if not ctx['python']:
        return None, 'run-python is off'
    _, steps, text = scratch_blueprint(url, 'BP_E05Connect_%d' % os.getpid())
    if not setup_ok(steps):
        return False, 'setup %s %s' % (steps, text if not steps else '')
    branch, printer = branch_and_print(url, 0)
    ctx['connect_nodes'] = (branch, printer)
    results = []
    for pin in ('then', 'else'):
        data, error, raw = tool_json(url, 'connect-pins', {'connections': [link(branch, pin, printer, 'execute')]})
        results.append(not error and (data or {}).get('summary', {}).get('succeeded') == 1)
    state = fan_in_state(url, branch, printer)
    return all(results) and all(state), 'connected=%s true/false/execute linked=%s' % (results, state)


@case
def connect_pins_refuses_silent_replace(url, ctx):
    """p0-7: connecting an already linked exec output elsewhere fails by default and keeps the old link (card 0046)"""
    if 'connect_nodes' not in ctx:
        return None, 'needs connect_pins_fan_in_keeps_both_links'
    branch, printer = ctx['connect_nodes']
    other = add_node(url, 'Print String', 'CallFunction', 'PrintString', 800)
    data, error, raw = tool_json(url, 'connect-pins', {'connections': [link(branch, 'then', other, 'execute')]})
    failed = (data or {}).get('failed') or []
    refused = bool(failed) and failed[0].get('errorCode') == 'WOULD_BREAK_EXISTING_LINK'
    nodes, _ = graph_nodes(url, 'Print')
    other_linked = connected(nodes.get(other['guid'].upper(), {}), 'in', 0)
    state = fan_in_state(url, branch, printer)
    return refused and not other_linked and all(state), 'refused=%s new pin linked=%s old links=%s' % (
        refused, other_linked, state)


BRIDGE_CONNECT_PY = r'''
import json, unreal
result = json.loads(unreal.N2CPythonBridge.connect_pins(%r))  # the default break_existing_links
'''


@case
def bridge_connect_pins_fan_in_keeps_both_links(url, ctx):
    """p0-7: the Python bridge's ConnectPins, called with its default breakExistingLinks, keeps both links of a fan-in
    (card 0046)"""
    if 'connect_nodes' not in ctx:
        return None, 'needs connect_pins_fan_in_keeps_both_links'
    branch, printer = branch_and_print(url, 0)
    results = []
    for pin in ('then', 'else'):
        data, raw = run_python(url, BRIDGE_CONNECT_PY % json.dumps([link(branch, pin, printer, 'execute')]))
        results.append(bool(data.get('success')) and (data.get('data') or {}).get('summary', {}).get('succeeded') == 1)
    state = fan_in_state(url, branch, printer)
    return all(results) and all(state), 'connected=%s true/false/execute linked=%s' % (results, state)


BRIDGE_CONNECT_BREAK_PY = r'''
import json, unreal
result = json.loads(unreal.N2CPythonBridge.connect_pins(%r, %r))
'''


def tool_connect(url, conn, break_links):
    """connect-pins with one connection: (succeeded, failure text)"""
    args = {'connections': [conn]}
    if break_links is not None:
        args['options'] = {'breakExistingLinks': break_links}
    data, error, raw = tool_json(url, 'connect-pins', args)
    failed = (data or {}).get('failed') or []
    ok = not error and (data or {}).get('summary', {}).get('succeeded') == 1
    return ok, ('%s %s' % (failed[0].get('errorCode'), failed[0].get('reason'))) if failed else raw[:160]


def bridge_connect(url, conn, break_links):
    """The Python bridge's ConnectPins with one connection (its default breakExistingLinks when break_links is None)"""
    if break_links is None:
        data, raw = run_python(url, BRIDGE_CONNECT_PY % json.dumps([conn]))
    else:
        data, raw = run_python(url, BRIDGE_CONNECT_BREAK_PY % (json.dumps([conn]), break_links))
    body = data.get('data') or {}
    failed = body.get('failed') or []
    ok = bool(data.get('success')) and body.get('summary', {}).get('succeeded') == 1
    return ok, failed[0].get('error', '') if failed else raw[:160]


def conversion_nodes(url):
    """{guid: output linked} of the To String conversion nodes in the focused graph, and the graph's node count"""
    nodes, total = graph_nodes(url, 'To String')
    return {g: connected(n, 'out', 0) for g, n in nodes.items()}, total


def pin_linked(url, term, node, direction, pin_name):
    nodes, _ = graph_nodes(url, term)
    pins = nodes.get(node['guid'].upper(), {}).get('output_pins' if direction == 'out' else 'input_pins', [])
    return any(p.get('name') == pin_name and p.get('connected') is True for p in pins)


def conversion_link_case(url, connect, x):
    """p0-7 (card 0415): a Float into an In String that is already linked needs a conversion node. Without
    breakExistingLinks it fails, names the existing link and leaves the graph as it was; with it, it replaces the link"""
    time_term, time_fn = 'Get Game Time in Seconds', 'GetGameTimeInSeconds'
    first = add_node(url, time_term, 'CallFunction', time_fn, x)
    second = add_node(url, time_term, 'CallFunction', time_fn, x + 200)
    printer = add_node(url, 'Print String', 'CallFunction', 'PrintString', x + 600)
    steps = {}
    before, _ = conversion_nodes(url)
    steps['first'], text = connect(url, link(first, 'ReturnValue', printer, 'InString'), None)
    linked, total = conversion_nodes(url)
    made = [g for g in linked if g not in before]
    if not steps['first'] or len(made) != 1 or not linked[made[0]]:
        return False, 'setup: first conversion link %s new conversion nodes %s (%s)' % (steps['first'], made, text)
    old = made[0]

    ok, text = connect(url, link(second, 'ReturnValue', printer, 'InString'), None)
    after, total_after = conversion_nodes(url)
    steps['refused'] = not ok and 'existing link' in text and 'currently linked to' in text
    steps['unchanged'] = (total_after == total and set(after) == set(linked) and after[old] is True
                          and not pin_linked(url, 'Get Game Time', second, 'out', 'ReturnValue')
                          and pin_linked(url, 'Print', printer, 'in', 'InString'))
    refusal = text

    ok, text = connect(url, link(second, 'ReturnValue', printer, 'InString'), True)
    replaced, _ = conversion_nodes(url)
    made = [g for g in replaced if g not in after]
    steps['replaced'] = (ok and len(made) == 1 and replaced[made[0]] is True and replaced[old] is False
                         and pin_linked(url, 'Get Game Time', second, 'out', 'ReturnValue')
                         and pin_linked(url, 'Print', printer, 'in', 'InString'))
    return all(steps.values()), '%s refusal=%r%s' % (steps, refusal[:140], '' if steps['replaced'] else ' replace: ' + text[:100])


@case
def connect_pins_refuses_conversion_replace(url, ctx):
    """p0-7: connect-pins refuses a conversion-node link into an already linked data input unless breakExistingLinks
    is set, and replaces the link when it is (card 0415)"""
    if 'connect_nodes' not in ctx:
        return None, 'needs connect_pins_fan_in_keeps_both_links'
    return conversion_link_case(url, tool_connect, 0)


@case
def bridge_connect_pins_refuses_conversion_replace(url, ctx):
    """p0-7: the Python bridge's ConnectPins refuses a conversion-node link into an already linked data input with its
    default breakExistingLinks, and replaces the link with break_existing_links=True (card 0415)"""
    if 'connect_nodes' not in ctx:
        return None, 'needs connect_pins_fan_in_keeps_both_links'
    return conversion_link_case(url, bridge_connect, 1200)


# Open a Control Rig with RigVM.UseNewEditor=1: its toolkit is not an FBlueprintEditor, so N2C's overlay poll, the
# editor-opened handler and the tab lambda must skip it (card 0044). Restores the cvar afterwards.
CONTROL_RIG_OPEN_PY = r'''
import unreal
cvar = unreal.SystemLibrary.get_console_variable_bool_value("RigVM.UseNewEditor")
unreal.SystemLibrary.execute_console_command(None, "RigVM.UseNewEditor 1")
factory = getattr(unreal, "ControlRigBlueprintFactory", None)
rig = factory.create_new_control_rig_asset("%(dir)s/%(name)s") if factory else None
opened = unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).open_editor_for_assets([rig]) if rig else False
unreal.SystemLibrary.execute_console_command(None, "RigVM.UseNewEditor %%d" %% (1 if cvar else 0))
result = {"factory": factory is not None, "created": rig is not None, "is_blueprint": isinstance(rig, unreal.Blueprint),
          "opened": bool(opened)}
'''


@case
def non_blueprint_asset_editor(url, ctx):
    """p0-6: a UBlueprint open in a toolkit that is not an FBlueprintEditor (Control Rig, new RigVM editor) (card 0044)"""
    if not ctx['python']:
        return None, 'run-python is off'
    name = 'CR_E05Probe_%d' % os.getpid()
    steps, text = run_python(url, CONTROL_RIG_OPEN_PY % {'dir': PROBE_DIR, 'name': name})
    if not steps.get('factory'):
        return None, 'Control Rig plugin not loaded %s' % text[:120]
    if not (steps.get('created') and steps.get('is_blueprint') and steps.get('opened')):
        return False, 'setup %s' % steps
    time.sleep(5)  # N2C's overlay poll runs every 2 s; let it visit the open Control Rig editor twice
    data, error, raw = tool_json(url, 'get-open-blueprint-editors', {})
    listed = [e.get('blueprintName') for e in (data or {}).get('editors', [])]
    alive = 'result' in rpc(url, 'ping')
    return alive and not error and name not in listed, 'editor answers=%s rig listed as Blueprint editor=%s' % (
        alive, name in listed)


def run_mcp_cases(url):
    failed = 0
    ctx = {}
    try:
        ctx['python'] = python_available(url)
    except Exception as e:
        print('FAIL editor not answering at %s: %s' % (url, e))
        return 1
    if ctx['python']:
        cleanup(url)
    else:
        print('NOTE run-python is off: the graph cases are skipped (turn on Allow MCP Python Execution)')
    try:
        for fn in CASES:
            try:
                ok, detail = fn(url, ctx)
            except Exception as e:  # a crash closes the connection
                ok, detail = False, 'exception: %s' % e
            label = 'SKIP' if ok is None else ('PASS' if ok else 'FAIL')
            failed += label == 'FAIL'
            print('%s %s  %s' % (label, fn.__name__, detail), flush=True)
    finally:
        if ctx.get('python'):
            try:
                deleted = cleanup(url)
            except Exception as e:
                deleted = False
            print('%s scratch folder %s deleted' % ('PASS' if deleted else 'FAIL', PROBE_DIR))
            failed += not deleted
    alive = True
    try:
        alive = 'result' in rpc(url, 'ping')
    except Exception:
        alive = False
    print('%s editor still answering after all cases' % ('PASS' if alive else 'FAIL'))
    failed += not alive
    return failed


# ---------------------------------------------------------------------------------------------------------------------
# Headless: Python checks and automation tests through UnrealEditor-Cmd
# ---------------------------------------------------------------------------------------------------------------------

def quote(arg):
    """Windows command-line token; '-Key=Value with spaces' becomes -Key="Value with spaces", which UE parses"""
    if ' ' not in arg:
        return arg
    if arg.startswith('-') and '=' in arg:
        key, value = arg.split('=', 1)
        return '%s="%s"' % (key, value)
    return '"%s"' % arg


def run_editor_cmd(engine, project, extra, log_path, timeout):
    from editor_session import find_editor
    args = [find_editor(engine), project, '-unattended', '-nullrhi', '-nosplash', '-nosound', '-NoLiveCoding',
            '-culture=en', '-abslog=%s' % log_path] + extra
    if os.path.exists(log_path):
        os.remove(log_path)
    command = ' '.join(quote(a) for a in args) if os.name == 'nt' else args
    try:
        code = subprocess.run(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=timeout).returncode
    except subprocess.TimeoutExpired:
        code = 'timeout'
    try:
        text = open(log_path, 'rb').read().replace(b'\x00', b'').decode('utf-8', 'replace')
    except OSError:
        text = ''
    return code, text


def run_python_checks(engine, project, logs):
    failed = 0
    for file_name, tag in HEADLESS_CHECKS:
        log_path = os.path.join(logs, file_name.replace('.py', '.log'))
        # Forward slashes: UE's Python executer reads '\n2c' in 'G:\Personal Projects\n2c' as an escape
        script = os.path.join(HERE, file_name).replace('\\', '/')
        code, text = run_editor_cmd(engine, project, ['-ExecutePythonScript=%s' % script], log_path, 900)
        lines = re.findall(r'%s (PASS|FAIL)(.*)$' % tag, text, re.M)
        checks = [(r, d.strip()) for r, d in lines if d.strip()]
        overall = [r for r, d in lines if not d.strip()]
        bad = [d for r, d in checks if r == 'FAIL']
        ok = bool(checks) and not bad and overall == ['PASS']
        failed += not ok
        print('%s %s  %d/%d checks pass%s (exit %s, log %s)' % (
            'PASS' if ok else 'FAIL', file_name, len(checks) - len(bad), len(checks),
            '; failed: %s' % '; '.join(bad[:5]) if bad else '', code, os.path.relpath(log_path, HERE)), flush=True)
    return failed


def run_automation(engine, project, tests, logs, label):
    log_path = os.path.join(logs, 'e05_automation_%s.log' % label)
    code, text = run_editor_cmd(engine, project, ['-ExecCmds=Automation RunTests %s' % tests,
                                                  '-TestExit=Automation Test Queue Empty'], log_path, 3600)
    results = re.findall(r'Test Completed\. Result=\{(\w+)\} Name=\{[^}]*\} Path=\{([^}]*)\}', text)
    failed = 0
    for result, path in results:
        ok = result == 'Success'
        failed += not ok
        print('%s test %s  Result=%s' % ('PASS' if ok else 'FAIL', path, result))
    if not results:
        print('FAIL automation %s  no test completed (exit %s, log %s)' % (tests, code, os.path.relpath(log_path, HERE)))
        return 1
    print('%s automation %s  %d/%d Success (exit %s, log %s)' % (
        'PASS' if not failed else 'FAIL', tests, len(results) - failed, len(results), code,
        os.path.relpath(log_path, HERE)), flush=True)
    return failed


def start_headless_editor(engine, project):
    from editor_session import EditorSession, free_port
    port = free_port(27160)
    # Python on for the cases that set up assets; -RenderOffScreen because on UE 5.6 under -nullrhi an opened
    # Blueprint editor's window otherwise reaches FGenericWindow::GetRestoredDimensions, which is Fatal
    session = EditorSession(engine, project, port, os.path.join(HERE, 'logs', 'e05_editor.log'),
                            allow_python=True, extra_args=['-RenderOffScreen'])
    if not session.start():
        session.stop()
        return None, None
    return session, 'http://127.0.0.1:%d/mcp' % port


def main():
    ap = argparse.ArgumentParser(description='E05 regression probe (p0-6 crash inputs, p0-7 data-loss cases)')
    ap.add_argument('--url', help='MCP endpoint of a running editor, e.g. http://127.0.0.1:27000/mcp')
    ap.add_argument('--engine', help='engine root, e.g. G:/UE/UE_5.8 (headless runs)')
    ap.add_argument('--project', help='host .uproject (headless runs)')
    ap.add_argument('--headless', action='store_true',
                    help='also run the headless Python checks and the automation tests through UnrealEditor-Cmd')
    ap.add_argument('--no-mcp', action='store_true', help='with --headless: skip the MCP cases')
    ap.add_argument('--tests', default=DEFAULT_TESTS, help='automation filter for --headless (default %(default)s)')
    ap.add_argument('--all-tests', action='store_true', help='with --headless: run every NodeToCode automation test')
    ap.add_argument('--label', default='', help='suffix for the automation log name, e.g. 5.8')
    args = ap.parse_args()
    if not args.url and not (args.engine and args.project):
        ap.error('pass --url, or --engine and --project')
    logs = os.path.join(HERE, 'logs')
    os.makedirs(logs, exist_ok=True)
    failed = 0

    if args.url or not args.no_mcp:
        print('== MCP cases', flush=True)
        session, url = None, args.url
        if not url:
            session, url = start_headless_editor(args.engine, args.project)
            if not session:
                print('FAIL headless editor did not start (log logs/e05_editor.log)')
                failed += 1
        if url:
            try:
                failed += run_mcp_cases(url)
            finally:
                if session:
                    session.stop()

    if args.headless:
        if not (args.engine and args.project):
            ap.error('--headless needs --engine and --project')
        print('== Headless Python checks', flush=True)
        failed += run_python_checks(args.engine, args.project, logs)
        tests = 'NodeToCode' if args.all_tests else args.tests
        print('== Automation tests (%s)' % tests, flush=True)
        label = args.label or os.path.basename(os.path.normpath(args.engine))
        failed += run_automation(args.engine, args.project, tests, logs, label)

    print('%s e05_regressions: %d failure(s)' % ('PASS' if not failed else 'FAIL', failed))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
