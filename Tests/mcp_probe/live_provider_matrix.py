"""Live translations through several cloud providers and models in one headless editor, with each request's payload
checked against the rules the provider cards set. Keys reach only the editor's environment.

Stdlib only; run with `python -I`:

    python -I live_provider_matrix.py --engine G:/UE/UE_5.8 \
        --project "G:/Personal Projects/n2c/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" --label 5.8 \
        [--cases anthropic/claude-haiku-5-5 deepseek/deepseek-flash:off ...] [--providers gemini openai] [--fanout]
        [--branch] [--liveness] [--issue13] [--gate] [--viewer] [--languages cpp python ...]

It reads the keys from the repo's .env (env_keys.py) and refuses to run if a key for a selected provider is saved in
the per-user secrets file (the saved key would be used, not the variable). For the session it sets Min Severity to
Debug in the host's Saved/Config layer (restored afterwards), so UN2CLLMPayloadBuilder::Build logs each payload.
Applying a case's settings saves them to the host's Config/DefaultNodeToCode.ini; EditorSession restores that file
byte for byte when the editor stops (or deletes it if it did not exist), and the report's config_restored says so.
It makes one in-memory probe Blueprint (probe_blueprint.SETUP_UNWIRED_PY: an Actor with a Print String node, never
saved; then CONNECT_PY wires BeginPlay to it unless --liveness builds its own graph), then per case: applies the case's settings (DeepSeek model ID and thinking mode, Max Output Tokens) on the settings object, runs
translate-focused-blueprint over MCP, reads the task stream, restores the settings, and takes that case's slice of
the editor log: the payload (summarised: model, sampling fields, output budget, response_format, Gemini
responseSchema, thinking), token usage and any NodeToCode errors. A case passes when the translation parses with
code and every payload rule for it holds. Key values are never printed; the editor log, Saved/Logs, the plugin's
own log file, new translation folders and the report are scanned with find_leaks, and the Saved/Logs files for
'key='. Report: logs/live_provider_matrix_<label>.json.

--fanout (card 0135) adds a data fan-out to the probe graph before the cases run: Get Actor Location's Return Value
feeds both Print String's In String (through the conversion node the schema inserts) and a Set Actor Location that
runs after the Print String. The report then has 'fanout' (the wiring and the flows.data entries of the focused
graph's N2C JSON whose value is an array) and each case's 'code' (declaration and implementation, so the
translation of the fan-out can be read).

--branch (card 0136) puts a Branch on the probe graph's exec line: BeginPlay runs a Branch on Has Authority, whose
True runs Collect Garbage and whose False runs the Print String (the opposite of the nodes' order, so a model that
guesses from node order gets it wrong). The report then has 'branch' (the wiring and the focused graph's
flows.execution, which names the True and False pins) and each case's 'code' plus 'branch_check': whether the
generated implementation runs CollectGarbage when HasAuthority() is true and the Print String when it is false
(a heuristic over the if/else blocks; read the kept code when it says unclear). A case with --branch passes only when
branch_check is ok.

--liveness (card 0137, issue #13) builds a graph with nodes Blueprint never runs instead of wiring BeginPlay: a custom
event runs the Print String, then Collect Garbage; the Blueprint's Compile Mode is set to Final Release, so the
Development Only Print String is disabled (exec passes through it); a Get Actor Location is left unwired (unused); the
default BeginPlay, ActorBeginOverlap and Tick stay ghost nodes. (A node the user disabled cannot be made from Python:
EnabledState is not editable there. NodeToCode.Capture.Golden "DisabledNodes" covers that case.) The report then has
'liveness' (the focused graph's nodes with their flags and its exec flows) and each case's 'code' plus
'liveness_check': the generated code must call CollectGarbage and must not implement BeginPlay, Tick or
ActorBeginOverlap, call PrintString or call GetActorLocation (comments are ignored). A case with --liveness passes
only when liveness_check is ok.

--languages (card 0132) runs every case once per target language (cpp, python, javascript, csharp, swift,
pseudocode; default cpp only), as the translate-focused-blueprint 'language' argument; a record's id then ends in
'@<language>'. branch_check and liveness_check read C++ (if blocks with braces, PascalCase engine names), so they gate
only the cpp runs; for the other languages they are kept in the report for reading. Every payload is also checked for
the shared-spec prompt: the system prompt holds the N2C JSON specification (Content/Prompting/Shared/N2CJsonSpec.md,
found by its heading) and the response contract (Shared/N2CResponseFormat.md) exactly once and no {{...}}
placeholder, and the user message holds the N2C JSON inside <n2c_json> tags.

--issue13 (gate 0385) translates issue #13's function graph instead of the event graph, built the way the capture
golden "Issue13" builds it: create-blueprint-function makes Issue13Label(Value: int) -> Result: Text on the probe
Actor (whose default events stay ghosts), and To String (Integer) of Value feeds both To Text (String), whose result is
returned, and the {effect} argument of a Format Text nobody reads. The report has 'issue13' (the captured nodes, flags
and flows) and each case's 'code' plus 'issue13_check', which gates every language: the code returns a value, computes
no Format Text ({effect}, FText::Format, a format call), implements no ghost default event, and the notes name the
skipped Format Text. It cannot be combined with --fanout, --branch, --gate or --liveness.

--gate (gate 0385) adds the golden "GateMultiExec" wiring to the probe event graph (three custom events into a Gate's
Enter, Open and Close, Exit into Flush Debug Strings), after --fanout and --branch when those are on, and keeps each
code for reading. The report's 'gate' lists the exec flows with node and pin names.

--viewer (gates 0384 and 0385) runs after the MCP cases, once per language: it sets Target Language, batch-translates
the tagged focused graph (UN2CBatchTranslationBlueprintLibrary.TranslateGraphsWithTag, the path that writes a
translation folder and records it in the graph state), then loads it through UN2CTagBlueprintLibrary.LoadTranslation,
the UN2CGraphStateManager::LoadTranslation call the translation viewer makes. A language passes when the batch wrote a
folder with the implementation, the request went to the case's model, and the viewer load returns that
implementation. A batch uses the provider the LLM module was initialized with, so the cases must share one provider;
the session starts with that provider and the first case's model in the Saved/Config layer, guards
Saved/NodeToCode/BlueprintGraphState.json (and .bak), and removes its tag afterwards. The report has 'viewer'.
"""
import argparse
import json
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from editor_session import BLUEPRINT_EDITOR_ARGS, EditorSession, free_port  # noqa: E402
from env_keys import find_leaks, load_provider_keys  # noqa: E402
from live_env_translation import (ASSET_DIR, PROVIDER_VARIABLE, STORE_ID, call_tool,  # noqa: E402
                                  run_tool_call, stored_key_present, tool_text)
from probe_blueprint import CONNECT_PY, SETUP_UNWIRED_PY  # noqa: E402

SETTINGS_SECTION = '[/Script/NodeToCode.N2CSettings]'

# Auto (0) sends the model's maximum output, at most 32000 while requests are not streamed, 65536 for DeepSeek
# thinking modes (N2CSettings.h tooltips, PR #41)
AUTO = 32000
AUTO_DEEPSEEK_THINKING = 65536


def case(provider, model, role, expect, settings=None, suffix=''):
    return {'id': '%s/%s%s' % (provider, model, suffix), 'provider': provider, 'model': model, 'role': role,
            'expect': expect, 'settings': settings or {}}


# Current models only (owner instruction 2026-10-23): the active rows of Content/Data/N2CModelCatalog.json per provider,
# with no retired, deprecated or "shuts down" IDs. Update the list when the catalog's lineup changes.
# expect: sampling (False = no temperature/top_p/top_k), budget_field and budget, response_format,
# thinking (enabled/disabled), reasoning_effort
CASES = [
    case('anthropic', 'claude-sonnet-5-5', 'default', {'sampling': False, 'budget_field': 'max_tokens', 'budget': AUTO}),
    case('anthropic', 'claude-haiku-5-5', 'cheap', {'sampling': False, 'budget_field': 'max_tokens', 'budget': AUTO}),
    case('anthropic', 'claude-haiku-5-5', 'cheap, explicit budget 4000',
         {'sampling': False, 'budget_field': 'max_tokens', 'budget': 4000},
         settings={'anthropic_max_output_tokens': 4000}, suffix=':budget4000'),
    case('anthropic', 'claude-opus-5-5', 'quality', {'sampling': False, 'budget_field': 'max_tokens', 'budget': AUTO}),
    case('openai', 'gpt-6.1-sol', 'default (reasoning)',
         {'sampling': False, 'budget_field': 'max_completion_tokens', 'budget': AUTO, 'response_format': 'strict_json_schema'}),
    case('openai', 'gpt-6-luna', 'cheap (reasoning)',
         {'sampling': False, 'budget_field': 'max_completion_tokens', 'budget': AUTO, 'response_format': 'strict_json_schema'}),
    case('openai', 'gpt-6-luna', 'cheap (reasoning), explicit budget 8000',
         {'sampling': False, 'budget_field': 'max_completion_tokens', 'budget': 8000, 'response_format': 'strict_json_schema'},
         settings={'open_ai_max_output_tokens': 8000}, suffix=':budget8000'),
    case('openai', 'gpt-6-astra', 'quality (reasoning)',
         {'sampling': False, 'budget_field': 'max_completion_tokens', 'budget': AUTO, 'response_format': 'strict_json_schema'}),
    case('gemini', 'gemini-3.8-flash', 'default',
         {'sampling': False, 'budget_field': 'maxOutputTokens', 'budget': AUTO, 'response_format': 'gemini_response_schema'}),
    case('gemini', 'gemini-3.5-flash-lite', 'cheap',
         {'sampling': False, 'budget_field': 'maxOutputTokens', 'budget': AUTO, 'response_format': 'gemini_response_schema'}),
    case('gemini', 'gemini-4-argon', 'quality',
         {'sampling': False, 'budget_field': 'maxOutputTokens', 'budget': AUTO, 'response_format': 'gemini_response_schema'}),
    case('deepseek', 'deepseek-flash', 'default (thinking high)',
         {'sampling': False, 'budget_field': 'max_tokens', 'budget': AUTO_DEEPSEEK_THINKING, 'response_format': None,
          'thinking': 'enabled', 'reasoning_effort': 'high'},
         settings={'deep_seek_model_id': 'deepseek-flash', 'deep_seek_reasoning_effort': 'high'}, suffix=':on'),
    case('deepseek', 'deepseek-flash', 'cheap (thinking none)',
         {'sampling': True, 'budget_field': 'max_tokens', 'budget': AUTO, 'response_format': 'json_object',
          'thinking': 'disabled', 'reasoning_effort': None},
         settings={'deep_seek_model_id': 'deepseek-flash', 'deep_seek_reasoning_effort': 'none'}, suffix=':off'),
    case('deepseek', 'deepseek-v4-pro', 'quality (thinking on)',
         {'sampling': False, 'budget_field': 'max_tokens', 'budget': AUTO_DEEPSEEK_THINKING, 'response_format': None,
          'thinking': 'enabled', 'reasoning_effort': 'high'},
         settings={'deep_seek_model_id': 'deepseek-v4-pro', 'deep_seek_reasoning_effort': 'high'}),
]

# Runs in the editor: sets settings on the settings object for one case and returns the previous values (as
# strings for enums). spec values: an int or a string, or [EnumName, member] matched without underscores and case.
APPLY_PY = r'''
import json, unreal
settings = unreal.get_default_object(unreal.N2CSettings)
spec = json.loads(%(spec)r)
previous, applied = {}, {}
def norm(s):
    return str(s).replace("_", "").lower()
for prop, value in spec.items():
    old = settings.get_editor_property(prop)
    previous[prop] = [type(old).__name__, old.name] if hasattr(old, "name") and not isinstance(old, (int, float, str)) else old
    if isinstance(value, list):
        enum_type = getattr(unreal, value[0])
        member = next((getattr(enum_type, n) for n in dir(enum_type)
                       if not n.startswith("_") and norm(n) == norm(value[1])), None)
        if member is None:
            applied[prop] = "no member %%s in %%s" %% (value[1], value[0])
            continue
        value = member
    settings.set_editor_property(prop, value)
    now = settings.get_editor_property(prop)
    applied[prop] = now.name if hasattr(now, "name") and not isinstance(now, (int, float, str)) else now
result = {"previous": previous, "applied": applied}
'''


# Runs in the editor after CONNECT_PY (--fanout): Get Actor Location -> Print String.In String and
# Set Actor Location.New Location; Print String -> Set Actor Location on the exec line. Reports the fan-out entries of
# the N2C JSON (a flows.data value that is an array).
FANOUT_PY = r"""
import json, unreal
bridge = unreal.N2CPythonBridge
steps = {}
def walk(v):
    if isinstance(v, dict):
        yield v
        for x in v.values():
            yield from walk(x)
    elif isinstance(v, list):
        for x in v:
            yield from walk(x)
def norm(s):
    return str(s).replace(" ", "").lower()
def add(title, x, y):
    found = json.loads(bridge.search_blueprint_nodes(title, True, 20, "", True))
    want = norm(title)
    # The name field starts with the action's title ("getactorlocation" then a line break and the function
    # name): an exact title first, then any title that starts with it
    actions = []
    for d in walk(found):
        ident = d.get("actionIdentifier") or (d.get("spawnMetadata") or {}).get("actionIdentifier")
        if ident:
            # N2C JSON 2.0.0 (card 0138): a node's display title is the optional "title" hint (left out when it is
            # only the function name spelled out), its identity "member_name"; the action identifier starts with the
            # menu title
            for label in (d.get("title"), d.get("member_name"), d.get("name"), d.get("displayName"), ident.split(">")[0]):
                if label:
                    actions.append((str(label).replace(" ", "").lower(), ident))
    exact = [i for n, i in actions if (n.splitlines() or [""])[0] == want]
    prefix = [i for n, i in actions if n.startswith(want)]
    for ident in exact + prefix:
        added = json.loads(bridge.add_node_to_graph(title, ident, x, y))
        return added.get("success") or added.get("error")
    return "not found in %s" % [n for n, _ in actions][:12]
steps["added_get"] = add("Get Actor Location", 0.0, 250.0)
steps["added_set"] = add("Set Actor Location", 700.0, 0.0)
found = json.loads(bridge.find_nodes_in_graph(json.dumps(["Get Actor Location", "Set Actor Location", "Print String"]), "keyword", False, 20))
nodes = (found.get("data") or {}).get("nodes") or []
def node(word):
    return next((n for n in nodes if word in norm(n.get("nodeName", ""))), None)
def pin(n, side, name=None, want_exec=False):
    for p in n.get(side) or []:
        is_exec = str(p.get("type", "")).lower() == "exec"
        if is_exec == want_exec and (name is None or norm(p.get("pinName", "")) == name):
            return p
    return None
get, setter, prints = node("getactorlocation"), node("setactorlocation"), node("printstring")
if get and setter and prints:
    def link(a, ap, b, bp):
        return {"from": {"nodeGuid": a["nodeGuid"], "pinGuid": ap["pinGuid"]}, "to": {"nodeGuid": b["nodeGuid"], "pinGuid": bp["pinGuid"]}}
    ret = pin(get, "outputPins", "returnvalue")
    conns = [link(prints, pin(prints, "outputPins", None, True), setter, pin(setter, "inputPins", None, True)),
             link(get, ret, setter, pin(setter, "inputPins", "newlocation")),
             link(get, ret, prints, pin(prints, "inputPins", "instring"))]
    res = json.loads(bridge.connect_pins(json.dumps(conns), True))
    steps["connected"] = res.get("success")
    steps["connect_result"] = json.dumps(res.get("data") or res.get("error"))[:600]
else:
    steps["found"] = [n.get("nodeName") for n in nodes]
steps["compiled"] = json.loads(bridge.compile_focused_blueprint()).get("success")
raw = json.loads(bridge.get_focused_blueprint_json())
n2c = (raw.get("data") or {}).get("n2c_json") or {}
steps["version"] = n2c.get("version")
steps["fanout_entries"] = {k: v for g in n2c.get("graphs") or [] for k, v in ((g.get("flows") or {}).get("data") or {}).items() if isinstance(v, list)}
steps["node_names"] = [n.get("member_name") or n.get("title") or n.get("class") for g in n2c.get("graphs") or [] for n in g.get("nodes") or []]
result = steps
"""


# Runs in the editor after CONNECT_PY (--branch): BeginPlay -> Branch(Has Authority); True -> Collect Garbage,
# False -> Print String. Reports the focused graph's exec flows with the node and pin names they point at.
BRANCH_PY = r"""
import json, unreal
bridge = unreal.N2CPythonBridge
steps = {}
def walk(v):
    if isinstance(v, dict):
        yield v
        for x in v.values():
            yield from walk(x)
    elif isinstance(v, list):
        for x in v:
            yield from walk(x)
def norm(s):
    return str(s).replace(" ", "").lower()
def add(title, x, y):
    found = json.loads(bridge.search_blueprint_nodes(title, True, 20, "", True))
    want = norm(title)
    actions = []
    for d in walk(found):
        ident = d.get("actionIdentifier") or (d.get("spawnMetadata") or {}).get("actionIdentifier")
        if ident:
            # N2C JSON 2.0.0 (card 0138): a node's display title is the optional "title" hint (left out when it is
            # only the function name spelled out), its identity "member_name"; the action identifier starts with the
            # menu title
            for label in (d.get("title"), d.get("member_name"), d.get("name"), d.get("displayName"), ident.split(">")[0]):
                if label:
                    actions.append((str(label).replace(" ", "").lower(), ident))
    exact = [i for n, i in actions if (n.splitlines() or [""])[0] == want]
    prefix = [i for n, i in actions if n.startswith(want)]
    for ident in exact + prefix:
        added = json.loads(bridge.add_node_to_graph(title, ident, x, y))
        return added.get("success") or added.get("error")
    return "not found in %s" % [n for n, _ in actions][:12]
steps["added_branch"] = add("Branch", 150.0, 0.0)
steps["added_authority"] = add("Has Authority", 0.0, 200.0)
steps["added_collect"] = add("Collect Garbage", 500.0, -200.0)
found = json.loads(bridge.find_nodes_in_graph(json.dumps(["BeginPlay", "Branch", "Has Authority", "Collect Garbage", "Print String"]), "keyword", False, 20))
nodes = (found.get("data") or {}).get("nodes") or []
def node(*words):
    return next((n for n in nodes if any(w in norm(n.get("nodeName", "")) for w in words)), None)
def pin(n, side, names=None, want_exec=False):
    for p in n.get(side) or []:
        is_exec = str(p.get("type", "")).lower() == "exec"
        if is_exec == want_exec and (names is None or norm(p.get("pinName", "")) in names or norm(p.get("displayName", "")) in names):
            return p
    return None
begin, branch, authority = node("beginplay"), node("branch"), node("hasauthority")
collect, prints = node("collectgarbage"), node("printstring")
steps["found"] = [n.get("nodeName") for n in nodes]
if begin and branch and authority and collect and prints:
    def link(a, ap, b, bp):
        return {"from": {"nodeGuid": a["nodeGuid"], "pinGuid": ap["pinGuid"]}, "to": {"nodeGuid": b["nodeGuid"], "pinGuid": bp["pinGuid"]}}
    conns = [link(begin, pin(begin, "outputPins", None, True), branch, pin(branch, "inputPins", None, True)),
             link(authority, pin(authority, "outputPins", ["returnvalue"]), branch, pin(branch, "inputPins", ["condition"])),
             link(branch, pin(branch, "outputPins", ["then", "true"], True), collect, pin(collect, "inputPins", None, True)),
             link(branch, pin(branch, "outputPins", ["else", "false"], True), prints, pin(prints, "inputPins", None, True))]
    res = json.loads(bridge.connect_pins(json.dumps(conns), True))
    steps["connected"] = res.get("success")
    steps["connect_result"] = json.dumps(res.get("data") or res.get("error"))[:600]
steps["compiled"] = json.loads(bridge.compile_focused_blueprint()).get("success")
raw = json.loads(bridge.get_focused_blueprint_json())
n2c = (raw.get("data") or {}).get("n2c_json") or {}
steps["version"] = n2c.get("version")
names = {}
for g in n2c.get("graphs") or []:
    for n in g.get("nodes") or []:
        ids = n.get("ids") or {}
        nid = ids.get("short") or n.get("id")
        for p in (n.get("input_pins") or []) + (n.get("output_pins") or []):
            pids = p.get("ids") or {}
            names["%s.%s" % (nid, pids.get("short") or p.get("id"))] = "%s.%s" % (
                n.get("member_name") or n.get("title") or n.get("class"), p.get("name") or "exec")
def named(entry):
    return " -> ".join(names.get(e.strip(), e.strip()) for e in entry.split("->"))
steps["execution"] = [[e, named(e)] for g in n2c.get("graphs") or [] for e in (g.get("flows") or {}).get("execution") or []]
result = steps
"""


# Runs in the editor after SETUP_UNWIRED_PY (--liveness, instead of CONNECT_PY): a custom event runs the Print String, then
# Collect Garbage; Compile Mode Final Release disables the Development Only Print String; Get Actor Location stays
# unwired; BeginPlay, ActorBeginOverlap and Tick stay ghosts. Reports the focused graph's nodes, flags and exec flows.
LIVENESS_PY = r"""
import json, unreal
bridge = unreal.N2CPythonBridge
steps = {}
def walk(v):
    if isinstance(v, dict):
        yield v
        for x in v.values():
            yield from walk(x)
    elif isinstance(v, list):
        for x in v:
            yield from walk(x)
def norm(s):
    return str(s).replace(" ", "").lower()
def add(title, x, y):
    found = json.loads(bridge.search_blueprint_nodes(title, True, 20, "", True))
    want = norm(title)
    actions = []
    for d in walk(found):
        ident = d.get("actionIdentifier") or (d.get("spawnMetadata") or {}).get("actionIdentifier")
        if ident:
            # N2C JSON 2.0.0 (card 0138): a node's display title is the optional "title" hint (left out when it is
            # only the function name spelled out), its identity "member_name"; the action identifier starts with the
            # menu title
            for label in (d.get("title"), d.get("member_name"), d.get("name"), d.get("displayName"), ident.split(">")[0]):
                if label:
                    actions.append((str(label).replace(" ", "").lower(), ident))
    exact = [i for n, i in actions if (n.splitlines() or [""])[0] == want]
    prefix = [i for n, i in actions if n.startswith(want)]
    for ident in exact + prefix:
        added = json.loads(bridge.add_node_to_graph(title, ident, x, y))
        return added.get("success") or added.get("error")
    return "not found in %s" % [n for n, _ in actions][:12]
steps["added_event"] = add("Add Custom Event", -300.0, 0.0)
steps["added_collect"] = add("Collect Garbage", 700.0, 0.0)
steps["added_location"] = add("Get Actor Location", 0.0, 300.0)
found = json.loads(bridge.find_nodes_in_graph(json.dumps(["CustomEvent", "Custom Event", "Print String", "Collect Garbage"]), "keyword", False, 20))
nodes = (found.get("data") or {}).get("nodes") or []
def node(*words):
    return next((n for n in nodes if any(w in norm(n.get("nodeName", "")) for w in words)), None)
def pin(n, side, names=None, want_exec=False):
    for p in n.get(side) or []:
        is_exec = str(p.get("type", "")).lower() == "exec"
        if is_exec == want_exec and (names is None or norm(p.get("pinName", "")) in names or norm(p.get("displayName", "")) in names):
            return p
    return None
event, prints, collect = node("customevent"), node("printstring"), node("collectgarbage")
steps["found"] = [n.get("nodeName") for n in nodes]
if event and prints and collect:
    def link(a, ap, b, bp):
        return {"from": {"nodeGuid": a["nodeGuid"], "pinGuid": ap["pinGuid"]}, "to": {"nodeGuid": b["nodeGuid"], "pinGuid": bp["pinGuid"]}}
    conns = [link(event, pin(event, "outputPins", None, True), prints, pin(prints, "inputPins", None, True)),
             link(prints, pin(prints, "outputPins", None, True), collect, pin(collect, "inputPins", None, True))]
    res = json.loads(bridge.connect_pins(json.dumps(conns), True))
    steps["connected"] = res.get("success")
    steps["connect_result"] = json.dumps(res.get("data") or res.get("error"))[:600]
    text_pin = pin(prints, "inputPins", ["instring"])
    if text_pin:
        steps["text_set"] = json.loads(bridge.set_pin_value(prints["nodeGuid"], text_pin["pinGuid"],
                                                           "Hello from the provider matrix", "InString")).get("success")
asset = unreal.load_asset("%(path)s")
try:
    asset.set_editor_property("compile_mode", unreal.BlueprintCompileMode.FINAL_RELEASE)
    steps["compile_mode"] = str(asset.get_editor_property("compile_mode"))
except Exception as error:
    steps["compile_mode_error"] = str(error)[:300]
steps["compiled"] = json.loads(bridge.compile_focused_blueprint()).get("success")
raw = json.loads(bridge.get_focused_blueprint_json())
n2c = (raw.get("data") or {}).get("n2c_json") or {}
steps["version"] = n2c.get("version")
steps["nodes"] = [[n.get("id"), n.get("member_name") or n.get("title"),
                   [f for f in ("unused", "unreachable", "disabled") if n.get(f)]]
                  for g in n2c.get("graphs") or [] for n in g.get("nodes") or []]
steps["execution"] = [e for g in n2c.get("graphs") or [] for e in (g.get("flows") or {}).get("execution") or []]
result = steps
"""


# Shared run-python prelude for --issue13 and --gate: add(title) spawns the action whose menu title is `title` and
# returns the bridge's result for it (nodeGuid and pins), pin() picks a pin from such a node, link() makes a
# connect_pins entry. Expects nothing; defines bridge, steps, walk, norm, add, pin, link, node_by_guid.
_NODE_HELPERS_PY = r"""
import json, unreal
bridge = unreal.N2CPythonBridge
steps = {}
def walk(v):
    if isinstance(v, dict):
        yield v
        for x in v.values():
            yield from walk(x)
    elif isinstance(v, list):
        for x in v:
            yield from walk(x)
def norm(s):
    return str(s).replace(" ", "").lower()
def add(title, x, y, category=""):
    found = json.loads(bridge.search_blueprint_nodes(title, True, 100, category, True))
    want = norm(title)
    actions = []
    for d in walk(found):
        ident = d.get("actionIdentifier") or (d.get("spawnMetadata") or {}).get("actionIdentifier")
        if ident:
            for label in (d.get("title"), d.get("member_name"), d.get("name"), d.get("displayName"), ident.split(">")[0]):
                if label:
                    actions.append((norm(label), ident))
    exact = [i for n, i in actions if (n.splitlines() or [""])[0] == want]
    prefix = [i for n, i in actions if n.startswith(want)]
    for ident in exact + prefix:
        added = json.loads(bridge.add_node_to_graph(title, ident, x, y))
        if added.get("success"):
            return added.get("data") or {}
        return {"error": added.get("error")}
    return {"error": "not found in %s" % [n for n, _ in actions][:12]}
def node_by_guid(guid):
    found = json.loads(bridge.find_nodes_in_graph(json.dumps([guid]), "guid", False, 5))
    nodes = (found.get("data") or {}).get("nodes") or []
    return nodes[0] if nodes else None
def pin(n, side, names=None, want_exec=False):
    for p in (n or {}).get(side) or []:
        is_exec = str(p.get("type", "")).lower() == "exec"
        if is_exec == want_exec and (names is None or norm(p.get("pinName", "")) in names or norm(p.get("displayName", "")) in names):
            return p
    return None
def link(a, ap, b, bp):
    return {"from": {"nodeGuid": a["nodeGuid"], "pinGuid": (ap or {}).get("pinGuid")},
            "to": {"nodeGuid": b["nodeGuid"], "pinGuid": (bp or {}).get("pinGuid")}}
def capture():
    raw = json.loads(bridge.get_focused_blueprint_json())
    return (raw.get("data") or {}).get("n2c_json") or {}
"""

# Runs in the editor after create-blueprint-function made and focused Issue13Label(Value: int) -> Result: Text
# (--issue13, card 0137's golden NodeToCode.Capture.Golden "Issue13", built the way N2CCaptureGolden.spec.cpp builds it):
# To String (Integer) of Value feeds both To Text (String), whose result is the Return node's Result, and the {effect}
# argument of a Format Text whose Result nothing reads. Reports the captured graph's nodes, flags and flows.
ISSUE13_PY = _NODE_HELPERS_PY + r"""
found = json.loads(bridge.find_nodes_in_graph(json.dumps(["Issue13Label", "Return"]), "keyword", False, 20))
nodes = (found.get("data") or {}).get("nodes") or []
steps["found"] = [[n.get("nodeName"), n.get("nodeClass")] for n in nodes]
entry = next((n for n in nodes if "functionentry" in norm(n.get("nodeClass", ""))), None)
ret = next((n for n in nodes if "functionresult" in norm(n.get("nodeClass", ""))), None)
to_string = add("To String (Integer)", 250.0, 150.0)
to_text = add("To Text (String)", 500.0, 100.0)
fmt = add("Format Text", 500.0, 300.0)
steps["added"] = {k: (v.get("nodeGuid") or v.get("error")) for k, v in
                  (("to_string", to_string), ("to_text", to_text), ("format_text", fmt))}
if fmt.get("nodeGuid"):
    fmt_pin = pin(fmt, "inputPins", ["format"])
    steps["format_set"] = json.loads(bridge.set_pin_value(fmt["nodeGuid"], (fmt_pin or {}).get("pinGuid", ""),
                                                          "{effect}", "Format")).get("success")
    fmt = node_by_guid(fmt["nodeGuid"]) or fmt
    steps["format_pins"] = [p.get("pinName") for p in fmt.get("inputPins") or []]
if entry and ret and to_string.get("nodeGuid") and to_text.get("nodeGuid") and fmt.get("nodeGuid"):
    s_out = pin(to_string, "outputPins", ["returnvalue"])
    # create-blueprint-function already links the entry's exec output to the Return node
    conns = [link(entry, pin(entry, "outputPins", ["value"]), to_string, pin(to_string, "inputPins", ["inint"])),
             link(to_string, s_out, to_text, pin(to_text, "inputPins", ["instring"])),
             link(to_string, s_out, fmt, pin(fmt, "inputPins", ["effect"])),
             link(to_text, pin(to_text, "outputPins", ["returnvalue"]), ret, pin(ret, "inputPins", ["result"]))]
    res = json.loads(bridge.connect_pins(json.dumps(conns), False))
    steps["connected"] = res.get("success")
    steps["connect_result"] = json.dumps(res.get("data") or res.get("error"))[:900]
steps["compiled"] = json.loads(bridge.compile_focused_blueprint()).get("success")
n2c = capture()
steps["version"] = n2c.get("version")
steps["graphs"] = [g.get("name") for g in n2c.get("graphs") or []]
steps["nodes"] = [[n.get("id"), n.get("member_name") or n.get("title") or n.get("type"),
                   [f for f in ("unused", "unreachable", "disabled") if n.get(f)]]
                  for g in n2c.get("graphs") or [] for n in g.get("nodes") or []]
steps["flows"] = [g.get("flows") for g in n2c.get("graphs") or []]
result = steps
"""

# Runs in the editor on the focused event graph (--gate, golden "GateMultiExec"): three custom events run a Gate
# (StandardMacros) through Enter, Open and Close; the Gate's Exit runs Flush Debug Strings. Added after --fanout and
# --branch when those are on, so all of them share one event graph. Reports the exec flows with node and pin names.
GATE_PY = _NODE_HELPERS_PY + r"""
events = [add("Add Custom Event", -400.0, 600.0 + 150.0 * i) for i in range(3)]
gate = add("Gate", 0.0, 750.0, "flowcontrol")
flush = add("Flush Debug Strings", 400.0, 750.0)
steps["added"] = [e.get("nodeGuid") or e.get("error") for e in events] + [gate.get("nodeGuid") or gate.get("error"),
                                                                          flush.get("nodeGuid") or flush.get("error")]
steps["event_names"] = [e.get("nodeName") for e in events]
if all(e.get("nodeGuid") for e in events) and gate.get("nodeGuid") and flush.get("nodeGuid"):
    conns = [link(events[0], pin(events[0], "outputPins", None, True), gate, pin(gate, "inputPins", ["enter"], True)),
             link(events[1], pin(events[1], "outputPins", None, True), gate, pin(gate, "inputPins", ["open"], True)),
             link(events[2], pin(events[2], "outputPins", None, True), gate, pin(gate, "inputPins", ["close"], True)),
             link(gate, pin(gate, "outputPins", ["exit"], True), flush, pin(flush, "inputPins", None, True))]
    res = json.loads(bridge.connect_pins(json.dumps(conns), False))
    steps["connected"] = res.get("success")
    steps["connect_result"] = json.dumps(res.get("data") or res.get("error"))[:900]
steps["compiled"] = json.loads(bridge.compile_focused_blueprint()).get("success")
n2c = capture()
names = {}
for g in n2c.get("graphs") or []:
    for n in g.get("nodes") or []:
        for p in (n.get("input_pins") or []) + (n.get("output_pins") or []):
            names["%s.%s" % (n.get("id"), p.get("id"))] = "%s.%s" % (
                n.get("member_name") if n.get("member_name") not in (None, "None") else (n.get("title") or n.get("type")),
                p.get("name") or "exec")
steps["execution"] = [" -> ".join(names.get(x.strip(), x.strip()) for x in e.split("->"))
                      for g in n2c.get("graphs") or [] for e in (g.get("flows") or {}).get("execution") or []]
result = steps
"""

# --viewer: the run-python steps of one batch translation of the tagged graph and of the translation viewer's load
# path (SN2CTranslationViewer::LoadTranslation(FN2CTagInfo) -> UN2CGraphStateManager::LoadTranslation, reached from
# Python through UN2CTagBlueprintLibrary.LoadTranslation, the same call).
TAG_PY = r"""
import json, unreal
res = json.loads(unreal.N2CPythonBridge.tag_focused_graph("%(tag)s", "N2CGate", "live_provider_matrix --viewer"))
result = res.get("data") or {"error": res.get("error")}
"""

BATCH_START_PY = r"""
import unreal
lib = unreal.N2CBatchTranslationBlueprintLibrary
out = lib.translate_graphs_with_tag("%(tag)s", "")
result = {"returned": str(out), "in_progress": lib.is_batch_translation_in_progress()}
"""

BATCH_POLL_PY = r"""
import unreal
lib = unreal.N2CBatchTranslationBlueprintLibrary
result = {"in_progress": lib.is_batch_translation_in_progress(), "progress": lib.get_batch_progress()}
"""

VIEWER_LOAD_PY = r"""
import json, os, unreal
tags = unreal.N2CTagBlueprintLibrary
guid = "%(guid)s"
steps = {"has_translation": tags.has_translation(guid)}
out = tags.load_translation(guid)
translation = out[1] if isinstance(out, tuple) else out
steps["load_returned"] = type(out).__name__ if not isinstance(out, tuple) else "tuple(%%s)" %% out[0]
if translation is not None and hasattr(translation, "code"):
    code = translation.code
    steps["loaded"] = True
    steps["graph_name"] = str(translation.graph_name)
    steps["declaration"] = str(code.graph_declaration)[:3000]
    steps["implementation"] = str(code.graph_implementation)[:6000]
    steps["notes"] = str(code.implementation_notes)[:2000]
else:
    steps["loaded"] = False
result = steps
"""

REMOVE_TAG_PY = r"""
import json, unreal
result = json.loads(unreal.N2CPythonBridge.remove_tag("%(guid)s", "%(tag)s"))
"""


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    text = re.sub(r'//[^\n]*', '', text)
    text = re.sub(r'(?m)^\s*#(?!include)[^\n]*', '', text)  # Python and Markdown comment or heading lines
    text = re.sub(r'(?<=\s)#\s[^\n]*', '', text)  # trailing Python comments
    return re.sub(r'"""[\s\S]*?"""', '', text)


def check_issue13(code):
    """Issue #13 (ISSUE13_PY's graph), any language: the function returns a value (the To Text result), no Format Text
    is computed ({effect}, FText::Format, a format call), no ghost default event is implemented, and the notes name
    the skipped Format Text. Comments are ignored; read the kept code when a language's idiom trips a pattern."""
    text = strip_comments('\n'.join((c.get('declaration') or '') + '\n' + (c.get('implementation') or '')
                                    for c in code or []))
    notes = '\n'.join(c.get('notes') or '' for c in code or [])
    found = {
        'returns': bool(re.search(r'\breturn\b', text, re.I)),
        'format_text_computed': bool(re.search(r'\{effect\}|format_?text|\bformat\s*\(|\.format\b|FText::Format',
                                               text, re.I)),
        'ghost_events': [n for n, p in (('BeginPlay', r'BeginPlay'), ('Tick', r'\bTick\s*\(|ReceiveTick|\btick\b'),
                                        ('ActorBeginOverlap', r'ActorBeginOverlap')) if re.search(p, text)],
        'notes_name_format_text': bool(re.search(r'format', notes, re.I)),
    }
    ok = found['returns'] and not found['format_text_computed'] and not found['ghost_events'] \
        and found['notes_name_format_text']
    return {'ok': ok, 'found': found, 'notes': notes[:800]}


def check_liveness(code):
    """Whether the translation leaves out what Blueprint never runs (LIVENESS_PY's graph): calls CollectGarbage, and
    implements no BeginPlay, Tick or ActorBeginOverlap, no PrintString and no GetActorLocation (comments ignored)"""
    text = '\n'.join((c.get('declaration') or '') + '\n' + (c.get('implementation') or '') for c in code or [])
    text = re.sub(r'/\*.*?\*/', '', re.sub(r'//[^\n]*', '', text), flags=re.S)
    text = re.sub(r'#[^\n]*', '', text)  # Python comments, preprocessor lines
    found = {name: bool(re.search(pattern, text)) for name, pattern in (
        ('BeginPlay', r'BeginPlay'), ('Tick', r'\bTick\s*\(|ReceiveTick'), ('ActorBeginOverlap', r'ActorBeginOverlap'),
        ('PrintString', r'PrintString|Print String|AddOnScreenDebugMessage|UE_LOG'), ('GetActorLocation', r'GetActorLocation'),
        ('CollectGarbage', r'CollectGarbage'))}
    bad = [name for name in ('BeginPlay', 'Tick', 'ActorBeginOverlap', 'PrintString', 'GetActorLocation') if found[name]]
    ok = found['CollectGarbage'] and not bad
    return {'ok': ok, 'found': found, 'implemented_but_never_runs': bad,
            'notes': [(c.get('notes') or '')[:600] for c in code or []]}


def _block(text, start):
    """The text of the brace block opening at text[start] ('{'), without the braces, and the index after it"""
    depth = 0
    for i in range(start, len(text)):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return text[start + 1:i], i + 1
    return text[start + 1:], len(text)


def check_branch(code):
    """Whether the implementation runs CollectGarbage when HasAuthority() is true and PrintString when it is false
    (BRANCH_PY's wiring): finds the if whose condition reads HasAuthority (directly or through a variable assigned
    from it) and looks at its then and else blocks"""
    impl = '\n'.join((c.get('implementation') or '') for c in code or [])
    impl = re.sub(r'//[^\n]*', '', impl)
    alias = [m.group(1) for m in re.finditer(r'(\w+)\s*=\s*[^;]*HasAuthority\s*\(', impl)]
    for m in re.finditer(r'\bif\s*\(', impl):
        depth, i = 1, m.end()
        while i < len(impl) and depth:
            depth += {'(': 1, ')': -1}.get(impl[i], 0)
            i += 1
        cond = impl[m.end():i - 1]
        if 'HasAuthority' not in cond and not any(re.search(r'\b%s\b' % re.escape(a), cond) for a in alias):
            continue
        negated = bool(re.search(r'!\s*\(?\s*\w*(HasAuthority|%s)' % '|'.join(map(re.escape, alias or ['HasAuthority'])), cond)
                       or re.search(r'==\s*false|false\s*==', cond))
        rest = impl[i:].lstrip()
        if not rest.startswith('{'):
            return {'ok': None, 'reason': 'unclear: the if has no braces', 'condition': cond.strip()}
        then_block, after = _block(impl, impl.index('{', i))
        else_match = re.match(r'\s*else\s*\{', impl[after:])
        else_block = _block(impl, after + else_match.end() - 1)[0] if else_match else impl[after:]
        true_block, false_block = (else_block, then_block) if negated else (then_block, else_block)
        ok = 'CollectGarbage' in true_block and 'PrintString' not in true_block and 'PrintString' in false_block \
            and 'CollectGarbage' not in false_block
        return {'ok': ok, 'condition': cond.strip(), 'negated': negated, 'has_else': bool(else_match),
                'true_runs': [f for f in ('CollectGarbage', 'PrintString') if f in true_block],
                'false_runs': [f for f in ('CollectGarbage', 'PrintString') if f in false_block]}
    return {'ok': None, 'reason': 'unclear: no if on HasAuthority found'}


def run_py(port, code, rid):
    status, response = call_tool(port, 'run-python', {'code': code}, rid)
    text = tool_text(response)
    try:
        return json.loads(text or '{}').get('data')
    except ValueError:
        return {'status': status, 'text': text[:600]}


def apply_settings(port, spec, rid):
    if not spec:
        return {}
    wire = {k: list(v) if isinstance(v, tuple) else v for k, v in spec.items()}
    return run_py(port, APPLY_PY % {'spec': json.dumps(wire)}, rid)


def restore_spec(previous):
    """APPLY_PY's previous values as a spec that sets them back"""
    out = {}
    for prop, value in (previous or {}).items():
        out[prop] = list(value) if isinstance(value, list) else value
    return out


def extract_payloads(text):
    """Every 'LLM Request Payload:' JSON object in a log slice"""
    payloads = []
    decoder = json.JSONDecoder()
    for m in re.finditer(r'LLM Request Payload:', text):
        start = text.find('{', m.end())
        if start < 0:
            continue
        try:
            obj, _ = decoder.raw_decode(text[start:])
            payloads.append(obj)
        except ValueError:
            payloads.append({'_unparsed_head': text[start:start + 300]})
    return payloads


def count_key(value, name):
    if isinstance(value, dict):
        return (1 if name in value else 0) + sum(count_key(v, name) for v in value.values())
    if isinstance(value, list):
        return sum(count_key(v, name) for v in value)
    return 0


LANGUAGES = ['cpp', 'python', 'javascript', 'csharp', 'swift', 'pseudocode']
# First lines of the shared prompt fragments (Content/Prompting/Shared): each must appear once in the system prompt
SPEC_HEADING = '### N2C JSON\n'
RESPONSE_FORMAT_HEAD = 'Your whole response is one JSON object, and nothing else'


def all_strings(value):
    if isinstance(value, str):
        return [value]
    if isinstance(value, dict):
        return [s for v in value.values() for s in all_strings(v)]
    if isinstance(value, list):
        return [s for v in value for s in all_strings(v)]
    return []


def prompt_summary(payload):
    """How the payload's text carries the shared-spec prompt (card 0132): counts over every string in the body"""
    texts = all_strings(payload)
    joined = '\n'.join(texts)
    return {'spec_heading': joined.count(SPEC_HEADING), 'response_format': joined.count(RESPONSE_FORMAT_HEAD),
            'placeholders': len(re.findall(r'\{\{N2C_[A-Z_]+\}\}', joined)),
            'n2c_json_tag': any(t.lstrip().startswith('<n2c_json>') or '\n<n2c_json>\n' in t for t in texts)
                            and '</n2c_json>' in joined}


def summarise(payload):
    gen = payload.get('generationConfig') or {}
    rf = payload.get('response_format') or {}
    js = rf.get('json_schema') or {}
    sampling = {k: payload[k] for k in ('temperature', 'top_p', 'top_k') if k in payload}
    sampling.update({'generationConfig.' + k: gen[k] for k in ('temperature', 'topP', 'topK') if k in gen})
    return {
        'model': payload.get('model'),
        'keys': sorted(payload),
        'sampling': sampling,
        'max_tokens': payload.get('max_tokens'),
        'max_completion_tokens': payload.get('max_completion_tokens'),
        'maxOutputTokens': gen.get('maxOutputTokens'),
        'response_format': {'type': rf.get('type'), 'strict': js.get('strict'), 'name': js.get('name')} if rf else None,
        'gemini': {'responseMimeType': gen.get('responseMimeType'), 'responseSchema': 'responseSchema' in gen,
                   'responseFormat': 'responseFormat' in gen} if gen else None,
        'additionalProperties_count': count_key(payload, 'additionalProperties'),
        'thinking': payload.get('thinking'),
        'reasoning_effort': payload.get('reasoning_effort'),
        'chars': len(json.dumps(payload)),
        'prompt': prompt_summary(payload),
    }


def check_payload(expect, summary):
    """[(rule, ok, detail)] for one case's payload summary"""
    rules = []
    if expect.get('sampling') is False:
        rules.append(('no sampling fields', not summary['sampling'], summary['sampling']))
    elif expect.get('sampling') is True:
        rules.append(('temperature sent', bool(summary['sampling']), summary['sampling']))
    field = expect.get('budget_field')
    if field:
        rules.append(('%s == %s' % (field, expect.get('budget')), summary.get(field) == expect.get('budget'),
                      {k: summary.get(k) for k in ('max_tokens', 'max_completion_tokens', 'maxOutputTokens')}))
        other = [k for k in ('max_tokens', 'max_completion_tokens', 'maxOutputTokens') if k != field and summary.get(k) is not None]
        rules.append(('only %s sent' % field, not other, other))
    rf = expect.get('response_format', 'unset')
    if rf == 'strict_json_schema':
        got = summary['response_format'] or {}
        rules.append(('response_format json_schema strict:true', got.get('type') == 'json_schema' and got.get('strict') is True, got))
    elif rf == 'gemini_response_schema':
        g = summary['gemini'] or {}
        rules.append(('responseSchema + application/json', g.get('responseSchema') and g.get('responseMimeType') == 'application/json', g))
        rules.append(('no additionalProperties', summary['additionalProperties_count'] == 0, summary['additionalProperties_count']))
    elif rf == 'json_object':
        got = summary['response_format'] or {}
        rules.append(('response_format json_object', got.get('type') == 'json_object', got))
    elif rf is None:
        rules.append(('no response_format', summary['response_format'] is None, summary['response_format']))
    if 'thinking' in expect:
        got = (summary['thinking'] or {}).get('type')
        rules.append(('thinking.type %s' % expect['thinking'], got == expect['thinking'], summary['thinking']))
    if 'reasoning_effort' in expect:
        rules.append(('reasoning_effort %s' % expect['reasoning_effort'], summary['reasoning_effort'] == expect['reasoning_effort'],
                      summary['reasoning_effort']))
    p = summary.get('prompt') or {}
    rules.append(('shared spec and response contract once, no placeholder',
                  p.get('spec_heading') == 1 and p.get('response_format') == 1 and p.get('placeholders') == 0, p))
    rules.append(('N2C JSON in <n2c_json> tags', bool(p.get('n2c_json_tag')), p.get('n2c_json_tag')))
    return rules


def nodetocode_errors(text):
    out = []
    for line in text.splitlines():
        # Errors, and warnings other than the graph overlay's construction trace
        if 'LogNodeToCode: Error' in line or ('LogNodeToCode: Warning' in line and 'GraphOverlay' not in line):
            out.append(line.strip()[:700])
    return out[:8]


def run_case(port, c, rid, timeout, log_reader, keep_code=False, language='cpp', tag_language=False):
    rec = {'id': c['id'] + ('@' + language if tag_language else ''), 'provider': c['provider'], 'model': c['model'],
           'role': c['role'], 'language': language}
    rec['settings_applied'] = apply_settings(port, c['settings'], rid)
    offset = log_reader.size()
    started_at = time.time()
    events = run_tool_call(port, 'translate-focused-blueprint',
                           {'provider': c['provider'], 'model': c['model'], 'language': language}, rid + 1,
                           meta={'progressToken': 'matrix-%d' % rid}, timeout=timeout)
    if events:
        final = [d for e, d in events if e == 'response']
        # A response read in the grace after the bound still counts; only a missing response is a timeout
        rec['timed_out'] = not final and any(e == 'timeout' for e, _ in events)
        rec['seconds'] = round(time.time() - started_at, 1)
        message = {}
        if final:
            try:
                message = json.loads(final[-1])
            except ValueError:
                message = {'raw': final[-1][:600]}
        text = tool_text(message)
        rec['is_error'] = bool((message.get('result') or {}).get('isError')) or 'error' in message or not final
        try:
            translation = json.loads(text)
        except ValueError:
            translation = None
        if isinstance(translation, dict):
            graphs = translation.get('graphs') or []
            rec['graphs'] = len(graphs)
            rec['code_chars'] = sum(len((g.get('code') or {}).get('graphDeclaration', '') or '')
                                    + len((g.get('code') or {}).get('graphImplementation', '') or '')
                                    for g in graphs if isinstance(g, dict))
            if keep_code:
                rec['code'] = [{'graph': g.get('graph_name'),
                                'declaration': ((g.get('code') or {}).get('graphDeclaration') or '')[:3000],
                                'implementation': ((g.get('code') or {}).get('graphImplementation') or '')[:6000],
                                'notes': ((g.get('code') or {}).get('implementationNotes') or '')[:2000]}
                               for g in graphs if isinstance(g, dict)]
        else:
            rec['response_head'] = (text or final[-1] if final else '')[:900]
    else:
        rec['is_error'] = True
        rec['task_start'] = 'no response'
    time.sleep(1.0)  # let the log flush
    if c['settings'] and isinstance(rec['settings_applied'], dict):
        rec['settings_restored'] = apply_settings(port, restore_spec(rec['settings_applied'].get('previous')), rid + 2)
    slice_text = log_reader.read_from(offset)
    payloads = extract_payloads(slice_text)
    rec['payload_count'] = len(payloads)
    rec['payloads'] = [summarise(p) for p in payloads if '_unparsed_head' not in p]
    usage = re.findall(r'LLM Token Usage - Input: (\d+) Output: (\d+)', slice_text)
    rec['usage'] = [{'input': int(a), 'output': int(b)} for a, b in usage]
    rec['log_errors'] = nodetocode_errors(slice_text)
    rec['rules'] = []
    if rec['payloads']:
        rec['rules'] = [{'rule': r, 'ok': bool(ok), 'detail': d} for r, ok, d in check_payload(c['expect'], rec['payloads'][0])]
    rec['translated'] = bool(not rec['is_error'] and rec.get('code_chars'))
    rec['ok'] = bool(rec['translated'] and rec['payloads'] and all(r['ok'] for r in rec['rules']))
    return rec


# --viewer: the Saved/Config value of Provider and the model setting for each provider
VIEWER_PROVIDER = {'anthropic': ('Anthropic', 'AnthropicModelId'), 'openai': ('OpenAI', 'OpenAIModelId'),
                   'gemini': ('Gemini', 'GeminiModelId'), 'deepseek': ('DeepSeek', 'DeepSeekModelId')}
VIEWER_LANGUAGE = {'cpp': 'Cpp', 'python': 'Python', 'javascript': 'JavaScript', 'csharp': 'CSharp',
                   'swift': 'Swift', 'pseudocode': 'Pseudocode'}
VIEWER_TAG = 'N2CViewerCheck'


def run_viewer(port, c, languages, log_reader, translations_dir, timeout):
    """Per language: set Target Language, batch-translate the tagged focused graph (the path that writes a
    translation folder and records it in the graph state), then load it the way the translation viewer does. A
    language passes when the batch wrote a folder with an implementation file, the payload went to the expected
    model, and the viewer load returned the same implementation."""
    out = {'provider': c['provider'], 'model': c['model'], 'runs': []}
    tag = run_py(port, TAG_PY % {'tag': VIEWER_TAG}, 900) or {}
    out['tag'] = tag
    guid = tag.get('graph_guid')
    for i, language in enumerate(languages):
        rid = 1000 + 20 * i
        run = {'language': language}
        run['settings'] = apply_settings(port, {'target_language': ['N2CCodeLanguage', VIEWER_LANGUAGE[language]]}, rid)
        before = set(os.listdir(translations_dir)) if os.path.isdir(translations_dir) else set()
        offset = log_reader.size()
        run['start'] = run_py(port, BATCH_START_PY % {'tag': VIEWER_TAG}, rid + 1)
        started = time.time()
        poll = {}
        while time.time() - started < timeout:
            time.sleep(3.0)
            poll = run_py(port, BATCH_POLL_PY, rid + 2) or {}
            if not poll.get('in_progress'):
                break
        run['seconds'] = round(time.time() - started, 1)
        run['finished'] = poll.get('in_progress') is False
        time.sleep(1.0)
        slice_text = log_reader.read_from(offset)
        payloads = [p for p in extract_payloads(slice_text) if '_unparsed_head' not in p]
        run['payload_models'] = [summarise(p).get('model') for p in payloads]
        run['log_errors'] = nodetocode_errors(slice_text)
        new = sorted((set(os.listdir(translations_dir)) if os.path.isdir(translations_dir) else set()) - before)
        files = []
        for d in new:
            for root, _, names in os.walk(os.path.join(translations_dir, d)):
                files += [os.path.relpath(os.path.join(root, n), translations_dir).replace('\\', '/') for n in names]
        run['folders'], run['files'] = new, files
        # The batch folder's file (the LLM module also saves its own timestamped folder for each request)
        impl_files = [f for f in sorted(files, key=lambda f: not f.startswith('BatchTranslation_')) if os.path.splitext(f)[1] in ('.cpp', '.py', '.js', '.cs', '.swift', '.md', '.txt')
                      and not f.endswith('_Notes.txt')]
        saved_impl = ''
        if impl_files:
            with open(os.path.join(translations_dir, impl_files[0]), encoding='utf-8', errors='replace') as f:
                saved_impl = f.read()
        load = run_py(port, VIEWER_LOAD_PY % {'guid': guid}, rid + 3) if guid else {'error': 'no tag guid'}
        run['viewer_load'] = {k: v for k, v in (load or {}).items() if k not in ('declaration', 'implementation', 'notes')}
        run['code'] = [{'graph': (load or {}).get('graph_name'), 'declaration': (load or {}).get('declaration') or '',
                        'implementation': (load or {}).get('implementation') or '', 'notes': (load or {}).get('notes') or ''}]
        run['saved_implementation_head'] = saved_impl[:400]
        loaded_impl = ((load or {}).get('implementation') or '').strip()
        run['viewer_matches_file'] = bool(loaded_impl) and loaded_impl == saved_impl[:6000].strip()
        run['ok'] = bool(run['finished'] and impl_files and run['payload_models']
                         and all(m == c['model'] for m in run['payload_models'])
                         and (load or {}).get('loaded') and run['viewer_matches_file'])
        out['runs'].append(run)
        print('viewer %-11s %s  folders=%s files=%s loaded=%s matches=%s models=%s' % (
            language, 'PASS' if run['ok'] else 'FAIL', new, len(files), (load or {}).get('loaded'),
            run['viewer_matches_file'], run['payload_models']), flush=True)
    if guid:
        out['tag_removed'] = run_py(port, REMOVE_TAG_PY % {'guid': guid, 'tag': VIEWER_TAG}, 990)
    return out


class LogReader:
    def __init__(self, path):
        self.path = path

    def size(self):
        try:
            return os.path.getsize(self.path)
        except OSError:
            return 0

    def read_from(self, offset):
        try:
            with open(self.path, 'rb') as f:
                f.seek(offset)
                return f.read().decode('utf-8', 'replace').replace('\x00', '')
        except OSError:
            return ''


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--label', default='5.8')
    ap.add_argument('--cases', nargs='*', help='case ids to run (default: all, or all of --providers)')
    ap.add_argument('--providers', nargs='*', choices=sorted(PROVIDER_VARIABLE))
    ap.add_argument('--list', action='store_true', help='list the case ids and exit')
    ap.add_argument('--port', type=int, default=27190)
    ap.add_argument('--fanout', action='store_true',
                    help='add a data fan-out (Get Actor Location into two inputs) to the probe graph and keep each code')
    ap.add_argument('--branch', action='store_true',
                    help='put a Branch (Has Authority: True -> Collect Garbage, False -> Print String) on the probe '
                         'graph, keep each code and check the generated if/else takes the right branch')
    ap.add_argument('--liveness', action='store_true',
                    help='translate a graph with ghost default events, a disabled Print String and an unused node '
                         '(instead of the wired BeginPlay), keep each code and check none of them is implemented')
    ap.add_argument('--issue13', action='store_true',
                    help="translate issue #13's function graph (golden Issue13) instead of the event graph, keep each "
                         'code and check every language returns To Text and skips the unused Format Text')
    ap.add_argument('--gate', action='store_true',
                    help='add a Gate run by three custom events (Enter, Open, Close; golden GateMultiExec) to the '
                         'probe event graph and keep each code')
    ap.add_argument('--viewer', action='store_true',
                    help='after the MCP cases, translate the focused graph once per language through a batch '
                         'translation (which writes a translation folder) and load it through the translation '
                         "viewer's load path; needs cases of one provider (its first case's model is used)")
    ap.add_argument('--languages', nargs='+', choices=LANGUAGES, default=['cpp'],
                    help='target languages to translate each case into (default: cpp); "all" is not accepted, list them')
    ap.add_argument('--timeout', type=int, default=300,
                    help='seconds per translation; a case that hits it fails as timed out and the rest are skipped, '
                         'since the editor still holds that request')
    args = ap.parse_args()

    if args.list:
        for c in CASES:
            print(c['id'], '-', c['role'])
        return 0
    # --cases runs in the order given (put a slow or flaky model last); otherwise the table's order
    by_id = {c['id']: c for c in CASES}
    cases = [by_id[i] for i in args.cases if i in by_id] if args.cases else list(CASES)
    cases = [c for c in cases if not args.providers or c['provider'] in args.providers]
    unknown = set(args.cases or []) - {c['id'] for c in CASES}
    if unknown or not cases:
        print(json.dumps({'ok': False, 'reason': 'unknown or no cases', 'unknown': sorted(unknown)}))
        return 2
    providers = sorted({c['provider'] for c in cases})
    keys = load_provider_keys(only=[PROVIDER_VARIABLE[p] for p in providers])
    missing = [p for p in providers if PROVIDER_VARIABLE[p] not in keys]
    saved = [p for p in providers if stored_key_present(STORE_ID[p])]
    if saved:
        print(json.dumps({'ok': False, 'reason': 'keys saved in the per-user secrets file for %s' % saved}))
        return 2
    cases = [c for c in cases if c['provider'] not in missing]
    if args.issue13 and (args.fanout or args.branch or args.gate or args.liveness):
        print(json.dumps({'ok': False, 'reason': '--issue13 translates its own function graph; run --fanout, '
                                                 '--branch, --gate and --liveness without it'}))
        return 2
    if args.viewer and len({c['provider'] for c in cases}) != 1:
        print(json.dumps({'ok': False, 'reason': '--viewer needs cases of exactly one provider'}))
        return 2

    logs = os.path.join(HERE, 'logs')
    os.makedirs(logs, exist_ok=True)
    log_path = os.path.join(logs, 'live_provider_matrix_%s_editor.log' % args.label)
    report_path = os.path.join(logs, 'live_provider_matrix_%s.json' % args.label)
    project_dir = os.path.dirname(os.path.abspath(args.project))
    saved_logs = os.path.join(project_dir, 'Saved', 'Logs')
    plugin_log = os.path.join(project_dir, 'Saved', 'NodeToCode.log')
    translations_dir = os.path.join(project_dir, 'Saved', 'NodeToCode', 'Translations')
    before = set(os.listdir(translations_dir)) if os.path.isdir(translations_dir) else set()
    port = free_port(args.port)
    report = {'label': args.label, 'engine': args.engine, 'date': time.strftime('%Y-%m-%d'), 'port': port,
              'not_verified': [{'provider': p, 'reason': '%s not in .env' % PROVIDER_VARIABLE[p]} for p in missing],
              'cases': []}

    session = EditorSession(args.engine, args.project, port, log_path, allow_python=True, extra_env=keys,
                            # -RenderOffScreen: on UE 5.6 with -nullrhi the opened Blueprint editor's window hits
                            # FGenericWindow::GetRestoredDimensions, which is Fatal unless this flag is on
                            extra_args=['-LogCmds=LogNodeToCode VeryVerbose', *BLUEPRINT_EDITOR_ARGS])
    with session as s:
        # Payloads are logged at Debug; restored with the other Saved/Config patches when the session stops
        s._patch('NodeToCode.ini', SETTINGS_SECTION, 'MinSeverity', 'Debug')
        if args.viewer:
            # A batch translation uses the provider and model the LLM module was initialized with (from the settings
            # at its first translation), so the session starts with the viewer provider's model selected
            s._patch('NodeToCode.ini', SETTINGS_SECTION, 'Provider', VIEWER_PROVIDER[cases[0]['provider']][0])
            s._patch('NodeToCode.ini', SETTINGS_SECTION, VIEWER_PROVIDER[cases[0]['provider']][1], cases[0]['model'])
            # The batch records graph state and the tag in Saved/NodeToCode: put both back when the session stops
            for state_file in ('BlueprintGraphState.json', 'BlueprintGraphState.json.bak'):
                s.guard_file(os.path.join(project_dir, 'Saved', 'NodeToCode', state_file))
        report['started'] = s.start()
        if report['started']:
            name = 'BP_N2CProviderMatrix_%s' % time.strftime('%H%M%S')
            report['setup'] = run_py(port, SETUP_UNWIRED_PY % {'dir': ASSET_DIR, 'name': name}, 1)
            if args.liveness:
                report['liveness'] = run_py(port, LIVENESS_PY.replace('%(path)s', '%s/%s' % (ASSET_DIR, name)), 2)
                print('liveness', json.dumps(report['liveness'])[:1600], flush=True)
            elif args.issue13:
                # The event graph keeps its ghost default events (and the unwired Print String the setup added);
                # create-blueprint-function makes and focuses Issue13Label(Value: int) -> Result: Text
                status, response = call_tool(port, 'create-blueprint-function', {
                    'functionName': 'Issue13Label', 'blueprintPath': '%s/%s' % (ASSET_DIR, name),
                    'parameters': [{'name': 'Value', 'direction': 'input', 'type': 'int'},
                                   {'name': 'Result', 'direction': 'output', 'type': 'text'}]}, 2)
                report['issue13_function'] = {'status': status, 'text': tool_text(response)[:600]}
                report['issue13'] = run_py(port, ISSUE13_PY, 3)
                print('issue13', json.dumps(report['issue13'])[:2400], flush=True)
            else:
                report['wiring'] = run_py(port, CONNECT_PY, 2)
            if args.fanout:
                report['fanout'] = run_py(port, FANOUT_PY, 3)
                print('fanout', json.dumps(report['fanout'])[:800], flush=True)
            if args.branch:
                report['branch'] = run_py(port, BRANCH_PY, 4)
                print('branch', json.dumps(report['branch'])[:1200], flush=True)
            if args.gate:
                report['gate'] = run_py(port, GATE_PY, 5)
                print('gate', json.dumps(report['gate'])[:1600], flush=True)
            print('setup', json.dumps(report['setup']), 'wiring', json.dumps(report.get('wiring'))[:400], flush=True)
            reader = LogReader(log_path)
            tag_language = args.languages != ['cpp']
            runs = [(c, language) for c in cases for language in args.languages]
            for i, (c, language) in enumerate(runs):
                run_id = c['id'] + ('@' + language if tag_language else '')
                if any(r.get('timed_out') for r in report['cases']):
                    report['cases'].append({'id': run_id, 'provider': c['provider'], 'model': c['model'],
                                            'role': c['role'], 'language': language, 'ok': False,
                                            'skipped': 'an earlier case timed out'})
                    continue
                rec = run_case(port, c, 100 + 10 * i, args.timeout, reader,
                               keep_code=args.fanout or args.branch or args.liveness or args.issue13 or args.gate
                               or tag_language,
                               language=language, tag_language=tag_language)
                # The branch and liveness heuristics read C++; other languages keep them for reading only
                gates = language == 'cpp'
                if args.branch:
                    rec['branch_check'] = check_branch(rec.get('code'))
                    if gates:
                        rec['ok'] = bool(rec['ok'] and rec['branch_check'].get('ok'))
                    print('  branch_check', json.dumps(rec['branch_check']), flush=True)
                if args.liveness:
                    rec['liveness_check'] = check_liveness(rec.get('code'))
                    if gates:
                        rec['ok'] = bool(rec['ok'] and rec['liveness_check'].get('ok'))
                    print('  liveness_check', json.dumps(rec['liveness_check'])[:1500], flush=True)
                if args.issue13:
                    # Language-neutral patterns: it gates every language
                    rec['issue13_check'] = check_issue13(rec.get('code'))
                    rec['ok'] = bool(rec['ok'] and rec['issue13_check'].get('ok'))
                    print('  issue13_check', json.dumps(rec['issue13_check'])[:1500], flush=True)
                report['cases'].append(rec)
                print('%-48s %s  %ss  usage=%s  prompt=%s' % (run_id, 'PASS' if rec['ok'] else 'FAIL', rec.get('seconds'),
                                                              rec.get('usage'),
                                                              (rec['payloads'][0].get('prompt') if rec.get('payloads') else None)),
                      flush=True)

            if args.viewer:
                report['viewer'] = run_viewer(port, cases[0], args.languages, reader, translations_dir,
                                              args.timeout)

    report['config_restored'] = session.restore_ok
    log_text = session.log_text()
    report['log_names_variables'] = {v: ('the %s environment variable is used' % v) in log_text for v in sorted(keys)}
    new_dirs = [os.path.join(translations_dir, d) for d in sorted((set(os.listdir(translations_dir)) if os.path.isdir(translations_dir) else set()) - before)]
    report['translation_dirs'] = len(new_dirs)
    key_eq = []
    for root, _, names in os.walk(saved_logs) if os.path.isdir(saved_logs) else []:
        for n in names:
            p = os.path.join(root, n)
            try:
                data = open(p, 'rb').read()
            except OSError:
                continue
            if b'key=' in data or 'key='.encode('utf-16-le') in data:
                key_eq.append(p)
    for p in (log_path, plugin_log):
        try:
            data = open(p, 'rb').read()
        except OSError:
            continue
        if b'key=' in data or 'key='.encode('utf-16-le') in data:
            key_eq.append(p)
    report['key_eq_in_logs'] = key_eq
    with open(report_path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1)
    leaks = find_leaks(keys, [log_path, report_path, saved_logs, plugin_log] + new_dirs)
    report['leaks'] = [{'path': p, 'variable': n} for p, n in leaks]
    report['ok'] = bool(report['started'] and report['cases'] and all(c['ok'] for c in report['cases'])
                        and all(v.get('ok') for v in (report.get('viewer') or {}).get('runs', [{'ok': True}]))
                        and not leaks and not key_eq and report['config_restored'])
    with open(report_path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1)
    print(json.dumps({'ok': report['ok'], 'report': report_path, 'leaks': report['leaks'], 'key_eq_in_logs': key_eq,
                      'not_verified': report['not_verified'],
                      'failed': [c['id'] for c in report['cases'] if not c['ok']],
                      'viewer_failed': [v.get('language') for v in (report.get('viewer') or {}).get('runs', [])
                                        if not v.get('ok')]}, indent=1))
    return 0 if report['ok'] else 1


if __name__ == '__main__':
    sys.exit(main())
