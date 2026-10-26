"""Card 0160 live check: a key saved through Project Settings reaches the next request of a running editor, through
the secret store and the per-request credential source, with no restart.

Stdlib only; run with `python -I`, under the `user-secrets` lock (it edits the per-user secrets file and puts it back):

    python -I live_credential_source_0160.py --engine G:/UE/UE_5.8 \
        --project "<Hosts>/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" [--provider anthropic --model claude-haiku-5-5]

Steps, in one headless editor whose environment has none of the provider variables:
1. The per-user secrets file is backed up byte for byte (with its .bak), and the provider's key is removed from it.
2. translate-focused-blueprint must fail before sending: an error that names the Project Settings field and the
   variable, and no 'HTTP request sent' line for the provider in the log. get-available-llm-providers must not list it.
3. Editor Python sets the provider's API Key field on the settings object with PostEditChangeProperty, as typing it in
   Project Settings does (FN2CSecretStore::SetApiKey saves it). The value comes from a non-provider variable of the
   editor's environment (N2C_0160_STORED_KEY), so it is never in a command, a log or this report.
4. get-available-llm-providers must list the provider with keySource 'saved'; the same editor's next
   translate-focused-blueprint must succeed, with a 'Sending request ... (saved key)' log line.
5. The editor stops and the secrets file and its .bak are restored byte for byte (secrets_restored).
Key values are never printed; the editor log, the report and new translation folders are scanned with find_leaks.
Report: logs/live_credential_source_0160.json.
"""
import argparse
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from editor_session import BLUEPRINT_EDITOR_ARGS, EditorSession, free_port  # noqa: E402
from env_keys import PROVIDER_VARIABLES, find_leaks, load_provider_keys  # noqa: E402
from live_env_translation import PROVIDER_VARIABLE, STORE_ID, call_tool, run_tool_call, tool_text  # noqa: E402
from probe_blueprint import SETUP_PY  # noqa: E402

ASSET_DIR = '/Game/N2CTest'
ASSET_NAME = 'BP_N2CStoredKeyProbe'
STORED_KEY_VARIABLE = 'N2C_0160_STORED_KEY'
# Every variable the plugin reads a provider key from: none may reach the editor
ALL_PROVIDER_VARIABLES = set(PROVIDER_VARIABLES) | {'DEEPSEEK_API', 'GOOGLE_API_KEY'}

# Runs in the editor: sets the API Key field the way Project Settings does (PostEditChangeProperty saves it through
# FN2CSecretStore), reading the value from the editor's own environment; returns what the settings then show
SET_KEY_PY = r'''
import os
import unreal
settings = unreal.get_default_object(unreal.N2CSettings)
prop = "%(prefix)s_api_key_ui"
settings.set_editor_property(prop, os.environ.get("%(variable)s", ""), unreal.PropertyAccessChangeNotifyMode.ALWAYS)
result = {"field_set": settings.get_editor_property(prop) != "",
          "api_key_source": settings.get_editor_property("%(prefix)s_api_key_source")}
'''

SOURCE_PY = r'''
import unreal
settings = unreal.get_default_object(unreal.N2CSettings)
result = {"api_key_source": settings.get_editor_property("%(prefix)s_api_key_source")}
'''


def secrets_path():
    base = os.environ.get('LOCALAPPDATA') if os.name == 'nt' else os.path.expanduser('~/Library/Application Support/Epic')
    return os.path.join(base or '', 'NodeToCode', 'secrets.json')


def read_bytes(path):
    try:
        with open(path, 'rb') as f:
            return f.read()
    except OSError:
        return None


def restore(path, data):
    if data is None:
        if os.path.exists(path):
            os.remove(path)
        return
    with open(path, 'wb') as f:
        f.write(data)


def translate(port, provider, model, rid, timeout):
    """One translate-focused-blueprint call: (is_error, response text)"""
    events = run_tool_call(port, 'translate-focused-blueprint', {'provider': provider, 'model': model, 'language': 'cpp'},
                           rid, meta={'progressToken': 'live-0160-%d' % rid}, timeout=timeout)
    final = [d for e, d in events if e == 'response']
    if not final:
        return True, 'no response'
    try:
        message = json.loads(final[-1])
    except ValueError:
        return True, final[-1][:600]
    text = tool_text(message)
    is_error = bool((message.get('result') or {}).get('isError')) or 'error' in message
    if not is_error:
        try:
            translation = json.loads(text)
            is_error = not (isinstance(translation, dict) and translation.get('graphs'))
        except ValueError:
            is_error = True
    return is_error, text


def providers(port, rid):
    status, response = call_tool(port, 'get-available-llm-providers', {}, rid)
    listed = json.loads(tool_text(response) or '{}').get('providers', [])
    return {p.get('id', ''): {k: p.get(k) for k in ('keySource', 'keyVariable')} for p in listed}


def run_python(port, code, rid):
    status, response = call_tool(port, 'run-python', {'code': code}, rid)
    try:
        return json.loads(tool_text(response) or '{}').get('data')
    except ValueError:
        return {'raw': tool_text(response)[:400]}


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--provider', default='anthropic', choices=sorted(PROVIDER_VARIABLE))
    ap.add_argument('--model', default='claude-haiku-5-5')
    ap.add_argument('--port', type=int, default=27175)
    ap.add_argument('--timeout', type=int, default=300)
    ap.add_argument('--log', default=os.path.join(HERE, 'logs', 'live_credential_source_0160_editor.log'))
    ap.add_argument('--report', default=os.path.join(HERE, 'logs', 'live_credential_source_0160.json'))
    args = ap.parse_args()

    variable = PROVIDER_VARIABLE[args.provider]
    store_id = STORE_ID[args.provider]
    keys = load_provider_keys(only=[variable])
    if variable not in keys:
        print(json.dumps({'ok': False, 'reason': '%s not in .env' % variable}))
        return 2

    # The editor gets the key only under a name the plugin does not read, and none of the provider variables
    for name in ALL_PROVIDER_VARIABLES:
        os.environ.pop(name, None)
    extra_env = {STORED_KEY_VARIABLE: keys[variable]}

    path = secrets_path()
    saved_file, saved_bak = read_bytes(path), read_bytes(path + '.bak')
    report = {'provider': args.provider, 'model': args.model, 'variable': variable}
    os.makedirs(os.path.dirname(args.log), exist_ok=True)
    translations_dir = os.path.join(os.path.dirname(os.path.abspath(args.project)), 'Saved', 'NodeToCode', 'Translations')
    before = set(os.listdir(translations_dir)) if os.path.isdir(translations_dir) else set()
    try:
        # Step 1: no saved key for the provider (every other field kept)
        if saved_file is not None:
            data = json.loads(saved_file.decode('utf-8-sig'))
            data.pop('%s_API_Key' % store_id, None)
            with open(path, 'w', encoding='utf-8') as f:
                json.dump(data, f, indent=4)

        port = free_port(args.port)
        report['port'] = port
        prefix = store_id.lower()
        with EditorSession(args.engine, args.project, port, args.log, allow_python=True, extra_env=extra_env,
                           extra_args=BLUEPRINT_EDITOR_ARGS) as s:
            report['started'] = s.start()
            if report['started']:
                name = '%s_%s' % (ASSET_NAME, time.strftime('%H%M%S'))
                status, setup = call_tool(port, 'run-python', {'code': SETUP_PY % {'dir': ASSET_DIR, 'name': name}}, 1)
                report['setup'] = tool_text(setup)[:300]

                # Step 2: no key anywhere
                report['before'] = {'settings': run_python(port, SOURCE_PY % {'prefix': prefix}, 2),
                                    'providers': providers(port, 3)}
                is_error, text = translate(port, args.provider, args.model, 4, args.timeout)
                report['before']['translate'] = {'is_error': is_error, 'text_head': text[:600]}

                # Step 3: the key typed into Project Settings
                report['set_key'] = run_python(port, SET_KEY_PY % {'prefix': prefix, 'variable': STORED_KEY_VARIABLE}, 5)

                # Step 4: the same editor's next request uses it
                report['after'] = {'providers': providers(port, 6)}
                is_error, text = translate(port, args.provider, args.model, 7, args.timeout)
                report['after']['translate'] = {'is_error': is_error, 'text_head': text[:300]}
        log_text = s.log_text()
    finally:
        # Step 5: the secrets file as it was
        restore(path, saved_file)
        restore(path + '.bak', saved_bak)
    report['secrets_restored'] = read_bytes(path) == saved_file and read_bytes(path + '.bak') == saved_bak

    provider_name = {'anthropic': 'Anthropic', 'openai': 'OpenAI', 'gemini': 'Gemini', 'deepseek': 'DeepSeek'}[args.provider]
    sends = [l for l in log_text.splitlines() if 'Sending request to' in l and provider_name in l]
    report['log'] = {
        'missing_key_error': ('No %s API key is set' % provider_name) in log_text,
        'sending_lines': [l[l.find('Sending request'):][:160] for l in sends],
        'http_sent_lines': sum(1 for l in log_text.splitlines() if 'HTTP request sent to %s' % provider_name in l),
        'saved_line': ('Saved the %s API key to' % store_id) in log_text,
    }
    after = set(os.listdir(translations_dir)) if os.path.isdir(translations_dir) else set()
    new_dirs = [os.path.join(translations_dir, d) for d in sorted(after - before)]

    os.makedirs(os.path.dirname(args.report), exist_ok=True)
    with open(args.report, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1)
    leaks = find_leaks(keys, [args.log, args.report] + new_dirs)
    report['leaks'] = [{'path': p, 'variable': n} for p, n in leaks]

    before_state, after_state = report.get('before') or {}, report.get('after') or {}
    before_text = (before_state.get('translate') or {}).get('text_head', '')
    checks = {
        'fails_before_send': bool((before_state.get('translate') or {}).get('is_error'))
                             and 'API Key' in before_text and variable in before_text,
        'nothing_sent_without_key': report['log']['http_sent_lines'] == 1,
        'not_listed_without_key': args.provider not in (before_state.get('providers') or {}),
        'key_saved_through_settings': bool((report.get('set_key') or {}).get('field_set')) and report['log']['saved_line'],
        'listed_as_saved': ((after_state.get('providers') or {}).get(args.provider) or {}).get('keySource') == 'saved',
        'next_request_succeeds': (after_state.get('translate') or {}).get('is_error') is False,
        'request_used_saved_key': any('(saved key)' in l for l in report['log']['sending_lines']),
        'secrets_restored': report['secrets_restored'],
        'no_leaks': not leaks,
    }
    report['checks'] = checks
    report['ok'] = all(checks.values())
    with open(args.report, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1)
    print(json.dumps(report, indent=1))
    return 0 if report['ok'] else 1


if __name__ == '__main__':
    sys.exit(main())
