"""The in-memory probe Blueprint the live scripts translate, as run-python snippets. Stdlib only.

Since capture marks nodes Blueprint never runs (cards 0135-0138, 0104), a Print String that no event reaches is
captured as unreachable and a provider rightly returns empty code for it. So the probe graph every live translation
uses is wired: Event BeginPlay -> Print String ("Hello from the provider matrix").

- SETUP_PY (format with {'dir': ..., 'name': ...}): makes /Game/<dir>/<name> in memory (an Actor Blueprint, a fresh
  name each run, never saved: a Blueprint made in this session is focused as soon as it is opened), opens its
  EventGraph, adds a Print String node, wires BeginPlay's exec output to it, sets its text and compiles. The result
  holds the steps and 'print_string_live': the captured N2C JSON (version 2.0.0) has a PrintString node with no
  unreachable, unused or disabled flag.
- SETUP_UNWIRED_PY: the same without the wiring, for a script that builds its own graph on top (the provider matrix's
  --liveness graph, where Print String is run by a custom event instead).
- CONNECT_PY (no formatting): the wiring step alone, for an existing focused probe Blueprint.

A headless editor that opens the probe's Blueprint editor needs editor_session.BLUEPRINT_EDITOR_ARGS.
"""

# Creates and opens the Blueprint and adds the Print String node; leaves `bridge` and `steps` defined
_CREATE = r'''
import json, unreal
path = "%(dir)s/%(name)s"
bridge = unreal.N2CPythonBridge
steps = {}
factory = unreal.BlueprintFactory()
factory.set_editor_property("parent_class", unreal.Actor)
steps["created"] = unreal.AssetToolsHelpers.get_asset_tools().create_asset("%(name)s", "%(dir)s", unreal.Blueprint, factory) is not None
steps["opened"] = json.loads(bridge.open_blueprint(path, "EventGraph")).get("success")
found = json.loads(bridge.search_blueprint_nodes("Print String", True, 5, "", True))
def walk(v):
    if isinstance(v, dict):
        yield v
        for x in v.values():
            yield from walk(x)
    elif isinstance(v, list):
        for x in v:
            yield from walk(x)
def key(d):
    # N2C JSON 2.0.0 (card 0138): the display title is the optional "title" hint; member_name is the function
    return str(d.get("title") or d.get("member_name") or d.get("name", d.get("displayName", ""))).replace(" ", "").lower()
action = next((d for d in walk(found) if key(d).startswith("printstring")
               and (d.get("actionIdentifier") or (d.get("spawnMetadata") or {}).get("actionIdentifier"))), None)
if action:
    ident = action.get("actionIdentifier") or action["spawnMetadata"]["actionIdentifier"]
    added = json.loads(bridge.add_node_to_graph("Print String", ident, 300.0, 0.0))
    steps["added_print_string"] = added.get("success")
    if not added.get("success"):
        steps["add_error"] = added.get("error")
else:
    steps["search_head"] = json.dumps(found)[:1200]
'''

# Wires BeginPlay's exec output (the new Blueprint's ghost BeginPlay, made real by the link) to the Print String and
# sets its text; expects `bridge` and `steps`. No % in here: it is appended to the formatted _CREATE.
_WIRE = r'''
found = json.loads(bridge.find_nodes_in_graph(json.dumps(["BeginPlay", "Print String"]), "keyword", False, 20))
nodes = (found.get("data") or {}).get("nodes") or []
def label(n):
    return (str(n.get("nodeName", "")) + " " + str(n.get("nodeClass", ""))).replace(" ", "").lower()
begin = next((n for n in nodes if "beginplay" in label(n)), None)
prints = next((n for n in nodes if "printstring" in label(n)), None)
steps["nodes"] = [label(n) for n in nodes][:10]
def pin(n, side, want_exec, name=None):
    for p in n.get(side) or []:
        is_exec = str(p.get("type", "")).lower() == "exec"
        if is_exec == want_exec and (name is None or str(p.get("pinName", "")).lower() == name):
            return p
    return None
if begin and prints:
    out_pin, in_pin = pin(begin, "outputPins", True), pin(prints, "inputPins", True)
    if out_pin and in_pin:
        conn = [{"from": {"nodeGuid": begin["nodeGuid"], "pinGuid": out_pin["pinGuid"]},
                 "to": {"nodeGuid": prints["nodeGuid"], "pinGuid": in_pin["pinGuid"]}}]
        res = json.loads(bridge.connect_pins(json.dumps(conn), False))
        steps["connected"] = res.get("success")
        if not res.get("success"):
            steps["connect_result"] = json.dumps(res)[:600]
    text_pin = pin(prints, "inputPins", False, "instring")
    if text_pin:
        steps["text_set"] = json.loads(bridge.set_pin_value(prints["nodeGuid"], text_pin["pinGuid"],
                                                           "Hello from the provider matrix", "InString")).get("success")
else:
    steps["found_head"] = json.dumps(found)[:1200]
'''

# Compiles and records whether the captured graph's Print String is live (N2C JSON 2.0.0 node flags, card 0137)
_FINISH = r'''
steps["compiled"] = json.loads(bridge.compile_focused_blueprint()).get("success")
raw = json.loads(bridge.get_focused_blueprint_json())
n2c = (raw.get("data") or {}).get("n2c_json") or {}
steps["n2c_version"] = n2c.get("version")
printed = [n for g in n2c.get("graphs") or [] for n in g.get("nodes") or []
           if str(n.get("member_name") or n.get("title") or "").replace(" ", "").lower() == "printstring"]
steps["print_string_live"] = bool(printed) and not any(n.get(f) for n in printed for f in ("unreachable", "unused", "disabled"))
result = steps
'''

SETUP_PY = _CREATE + _WIRE + _FINISH
SETUP_UNWIRED_PY = _CREATE + 'steps["compiled"] = json.loads(bridge.compile_focused_blueprint()).get("success")\nresult = steps\n'
CONNECT_PY = 'import json, unreal\nbridge = unreal.N2CPythonBridge\nsteps = {}\n' + _WIRE + _FINISH
