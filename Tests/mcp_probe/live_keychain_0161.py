"""Card 0161 live check (the headless part of gate 0389): API keys move from secrets.json into Windows Credential
Manager once, requests use them, a key change applies to the next request, and nothing reflected returns a key.

Stdlib only; Windows only; run with `python -I` under the `user-secrets` lock (it rewrites the per-user secrets folder
and the NodeToCode/* Credential Manager entries, and puts both back):

    aw lock run user-secrets --as <lane> --for "0161 live" -- python -I live_keychain_0161.py --engine G:/UE/UE_5.8 \
        --project "<Hosts>/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" [--provider anthropic --model claude-haiku-5-5]

Steps:
1. Every NodeToCode/* entry is snapshotted in memory and removed; the per-user secrets.json (with .bak and .tmp) and
   the host's Saved/NodeToCode/User/secrets.json are backed up byte for byte. A 2.0-beta per-user secrets.json is
   written with the provider's real key (from .env) and a fake key for a second provider, and a project file with a
   fake key for a third. The editor's environment has none of the provider variables.
2. First start: both files are gone, the three NodeToCode/<Provider> entries hold the keys (compared, never printed),
   the log says "Moved API keys", Project Settings shows an empty API Key field and "Stored in Windows Credential
   Manager (ends ...)", has_api_key is true, no N2CSettings attribute or function returns the key, and a translation
   with the migrated key succeeds.
3. In the same editor, set_api_key with a wrong key makes the next translation fail with an authentication error;
   entering the right key through the API Key field (set_editor_property, as Project Settings does) empties the field
   and makes the next translation succeed.
4. Second start: the entries are unchanged and no file is written; a translation still succeeds. set_api_key with an
   empty key removes the entry and the status says "Not set".
5. The entries and files are put back as they were (restored).
Key values are never printed; the editor logs, the report and new translation folders are scanned with find_leaks.
Report: logs/live_keychain_0161.json.
"""
import argparse
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import wincred  # noqa: E402
from editor_session import BLUEPRINT_EDITOR_ARGS, EditorSession, free_port  # noqa: E402
from env_keys import PROVIDER_VARIABLES, find_leaks, load_provider_keys  # noqa: E402
from live_credential_source_0160 import providers, read_bytes, restore, run_python, translate  # noqa: E402
from live_env_translation import PROVIDER_VARIABLE, STORE_ID, call_tool, tool_text  # noqa: E402
from probe_blueprint import SETUP_PY  # noqa: E402

ASSET_DIR = '/Game/N2CTest'
ASSET_NAME = 'BP_N2CKeychainProbe'
KEY_VARIABLE = 'N2C_0161_STORED_KEY'
ALL_PROVIDER_VARIABLES = set(PROVIDER_VARIABLES) | {'DEEPSEEK_API', 'GOOGLE_API_KEY'}
FAKE_KEYS = {'OpenAI': 'n2c-0161-fake-openai-key-0001', 'Gemini': 'n2c-0161-fake-gemini-key-0002',
             'DeepSeek': 'n2c-0161-fake-deepseek-key-0003', 'Anthropic': 'n2c-0161-fake-anthropic-key-0004'}
WRONG_KEY = 'n2c-0161-wrong-key-00000000000000'

# Runs in the editor: what Project Settings and the Blueprint/Python surface show for the provider
STATE_PY = r'''
import unreal
settings = unreal.get_default_object(unreal.N2CSettings)
prefix = "%(prefix)s"
want = "%(store_id)s".replace("_", "").upper()
provider = [getattr(unreal.N2CLLMProvider, m) for m in dir(unreal.N2CLLMProvider)
            if not m.startswith("_") and m.replace("_", "").upper() == want][0]
names = [n for n in dir(unreal.N2CSettings) if "key" in n.lower()]
result = {"field": settings.get_editor_property(prefix + "_api_key_ui"),
          "status": settings.get_editor_property(prefix + "_api_key_source"),
          "has_api_key": unreal.N2CSettings.has_api_key(provider),
          "key_names": names}
'''

# Runs in the editor: set_api_key (Blueprint/Python) with the value from an editor environment variable, or a literal
SET_API_KEY_PY = r'''
import os
import unreal
want = "%(store_id)s".replace("_", "").upper()
provider = [getattr(unreal.N2CLLMProvider, m) for m in dir(unreal.N2CLLMProvider)
            if not m.startswith("_") and m.replace("_", "").upper() == want][0]
result = {"saved": unreal.N2CSettings.set_api_key(provider, %(value)s)}
'''

# Runs in the editor: types the key into the API Key field the way Project Settings does (PostEditChangeProperty)
FIELD_PY = r'''
import os
import unreal
settings = unreal.get_default_object(unreal.N2CSettings)
prop = "%(prefix)s_api_key_ui"
settings.set_editor_property(prop, os.environ.get("%(variable)s", ""), unreal.PropertyAccessChangeNotifyMode.ALWAYS)
result = {"field_after": settings.get_editor_property(prop),
          "status": settings.get_editor_property("%(prefix)s_api_key_source")}
'''


def secrets_dir():
    return os.path.join(os.environ.get('LOCALAPPDATA', ''), 'NodeToCode')


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--provider', default='anthropic', choices=sorted(PROVIDER_VARIABLE))
    ap.add_argument('--model', default='claude-haiku-5-5')
    ap.add_argument('--port', type=int, default=27176)
    ap.add_argument('--timeout', type=int, default=300)
    ap.add_argument('--log', default=os.path.join(HERE, 'logs', 'live_keychain_0161_editor%d.log'))
    ap.add_argument('--report', default=os.path.join(HERE, 'logs', 'live_keychain_0161.json'))
    args = ap.parse_args()
    if os.name != 'nt':
        print(json.dumps({'ok': False, 'reason': 'Windows only'}))
        return 2

    variable = PROVIDER_VARIABLE[args.provider]
    store_id = STORE_ID[args.provider]
    prefix = store_id.lower()
    keys = load_provider_keys(only=[variable])
    if variable not in keys:
        print(json.dumps({'ok': False, 'reason': '%s not in .env' % variable}))
        return 2
    real_key = keys[variable]
    others = [p for p in ('OpenAI', 'Gemini', 'DeepSeek', 'Anthropic') if p != store_id][:2]
    user_keys = {store_id: real_key, others[0]: FAKE_KEYS[others[0]]}
    project_keys = {others[1]: FAKE_KEYS[others[1]]}
    expected = dict(user_keys, **project_keys)
    leak_values = dict(keys, **{'FAKE_' + k: v for k, v in FAKE_KEYS.items()})

    for name in ALL_PROVIDER_VARIABLES:
        os.environ.pop(name, None)
    extra_env = {KEY_VARIABLE: real_key}

    user_file = os.path.join(secrets_dir(), 'secrets.json')
    project_dir = os.path.dirname(os.path.abspath(args.project))
    project_file = os.path.join(project_dir, 'Saved', 'NodeToCode', 'User', 'secrets.json')
    guarded = [user_file, user_file + '.bak', user_file + '.tmp', project_file]
    saved_files = {p: read_bytes(p) for p in guarded}
    saved_entries = wincred.snapshot(wincred.PREFIX)
    report = {'provider': args.provider, 'model': args.model, 'entries_before': sorted(saved_entries)}
    translations_dir = os.path.join(project_dir, 'Saved', 'NodeToCode', 'Translations')
    before_dirs = set(os.listdir(translations_dir)) if os.path.isdir(translations_dir) else set()
    logs = [args.log % 1, args.log % 2]
    os.makedirs(os.path.dirname(logs[0]), exist_ok=True)

    def entries_match(want):
        now = wincred.snapshot(wincred.PREFIX)
        return sorted(now) == sorted(wincred.PREFIX + k for k in want) and all(
            now[wincred.PREFIX + k]['blob'] == v.encode('utf-8') for k, v in want.items())

    def setup(port, rid):
        name = '%s_%s' % (ASSET_NAME, time.strftime('%H%M%S'))
        status, response = call_tool(port, 'run-python', {'code': SETUP_PY % {'dir': ASSET_DIR, 'name': name}}, rid)
        return tool_text(response)[:200]

    try:
        # Step 1: a 2.0-beta per-user file and a project file, no entries
        wincred.clear(wincred.PREFIX)
        for p in guarded:
            if os.path.exists(p):
                os.remove(p)
        os.makedirs(os.path.dirname(user_file), exist_ok=True)
        with open(user_file, 'w', encoding='utf-8') as f:
            json.dump(dict({'Version': 1}, **{k + '_API_Key': v for k, v in user_keys.items()}), f, indent=4)
        os.makedirs(os.path.dirname(project_file), exist_ok=True)
        with open(project_file, 'w', encoding='utf-8') as f:
            json.dump({k + '_API_Key': v for k, v in project_keys.items()}, f, indent=4)

        port = free_port(args.port)
        report['port'] = port
        # Step 2 and 3: first start
        with EditorSession(args.engine, args.project, port, logs[0], allow_python=True, extra_env=extra_env,
                           extra_args=BLUEPRINT_EDITOR_ARGS) as s:
            first = report['first_start'] = {'started': s.start()}
            if first['started']:
                first['files_gone'] = not os.path.exists(user_file) and not os.path.exists(user_file + '.bak') \
                    and not os.path.exists(project_file)
                first['entries'] = wincred.targets(wincred.PREFIX)
                first['entries_hold_keys'] = entries_match(expected)
                first['setup'] = setup(port, 1)
                first['state'] = run_python(port, STATE_PY % {'prefix': prefix, 'store_id': store_id}, 2)
                first['providers'] = providers(port, 3)
                is_error, text = translate(port, args.provider, args.model, 4, args.timeout)
                first['translate'] = {'is_error': is_error, 'text_head': text[:200]}

                change = report['key_change'] = {}
                change['set_wrong'] = run_python(port, SET_API_KEY_PY % {'store_id': store_id, 'value': repr(WRONG_KEY)}, 5)
                is_error, text = translate(port, args.provider, args.model, 6, args.timeout)
                change['wrong_translate'] = {'is_error': is_error, 'text_head': text[:400]}
                change['field'] = run_python(port, FIELD_PY % {'prefix': prefix, 'variable': KEY_VARIABLE}, 7)
                change['entry_restored'] = entries_match(expected)
                is_error, text = translate(port, args.provider, args.model, 8, args.timeout)
                change['restored_translate'] = {'is_error': is_error, 'text_head': text[:200]}
        log1 = s.log_text()
        after_first = wincred.snapshot(wincred.PREFIX)

        # Step 4: second start
        with EditorSession(args.engine, args.project, port, logs[1], allow_python=True, extra_env=extra_env,
                           extra_args=BLUEPRINT_EDITOR_ARGS) as s:
            second = report['second_start'] = {'started': s.start()}
            if second['started']:
                second['entries_unchanged'] = wincred.snapshot(wincred.PREFIX) == after_first
                second['no_file'] = not os.path.exists(user_file) and not os.path.exists(project_file)
                second['setup'] = setup(port, 11)
                is_error, text = translate(port, args.provider, args.model, 12, args.timeout)
                second['translate'] = {'is_error': is_error, 'text_head': text[:200]}
                second['remove'] = run_python(port, SET_API_KEY_PY % {'store_id': store_id, 'value': '""'}, 13)
                second['entry_removed'] = (wincred.PREFIX + store_id) not in wincred.targets(wincred.PREFIX)
                second['state_after_remove'] = run_python(port, STATE_PY % {'prefix': prefix, 'store_id': store_id}, 14)
        log2 = s.log_text()
    finally:
        # Step 5: everything as it was
        restored_entries = wincred.restore_snapshot(wincred.PREFIX, saved_entries)
        for p, data in saved_files.items():
            restore(p, data)
        restored_files = all(read_bytes(p) == data for p, data in saved_files.items())
    report['restored'] = {'entries': restored_entries, 'files': restored_files}

    report['log'] = {'moved_line': 'Moved API keys' in log1,
                     'saved_line': ('Saved the %s API key to Windows Credential Manager' % store_id) in log1,
                     'moved_again': 'Moved API keys' in log2}
    after_dirs = set(os.listdir(translations_dir)) if os.path.isdir(translations_dir) else set()
    new_dirs = [os.path.join(translations_dir, d) for d in sorted(after_dirs - before_dirs)]
    os.makedirs(os.path.dirname(args.report), exist_ok=True)
    with open(args.report, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1)
    leaks = find_leaks(leak_values, logs + [args.report] + new_dirs)
    report['leaks'] = [{'path': p, 'variable': n} for p, n in leaks]

    first, change, second = report.get('first_start') or {}, report.get('key_change') or {}, report.get('second_start') or {}
    state = first.get('state') or {}
    wrong_text = ((change.get('wrong_translate') or {}).get('text_head') or '').lower()
    removed_state = second.get('state_after_remove') or {}
    checks = {
        'migrated_files_deleted': bool(first.get('files_gone')),
        'migrated_entries_hold_keys': bool(first.get('entries_hold_keys')),
        'migration_logged': report['log']['moved_line'],
        'field_empty_after_migration': state.get('field') == '',
        'status_names_credential_manager': str(state.get('status', '')).startswith('Stored in Windows Credential Manager (ends ...'),
        'has_api_key': state.get('has_api_key') is True,
        'no_key_getter': not [n for n in state.get('key_names', []) if n.startswith('get') and 'api_key' in n],
        'listed_as_saved': ((first.get('providers') or {}).get(args.provider) or {}).get('keySource') == 'saved',
        'migrated_key_translates': (first.get('translate') or {}).get('is_error') is False,
        'wrong_key_fails_auth': bool((change.get('wrong_translate') or {}).get('is_error'))
                                and any(w in wrong_text for w in ('401', '403', 'auth', 'invalid', 'api key', 'permission')),
        'field_entry_saves_and_empties': (change.get('field') or {}).get('field_after') == ''
                                         and bool(change.get('entry_restored')) and report['log']['saved_line'],
        'restored_key_translates': (change.get('restored_translate') or {}).get('is_error') is False,
        'second_start_changes_nothing': bool(second.get('entries_unchanged')) and bool(second.get('no_file'))
                                        and not report['log']['moved_again'],
        'translates_after_restart': (second.get('translate') or {}).get('is_error') is False,
        'empty_set_removes_entry': bool(second.get('entry_removed')) and str(removed_state.get('status', '')).startswith('Not set')
                                   and removed_state.get('has_api_key') is False,
        'restored': restored_entries and restored_files,
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
