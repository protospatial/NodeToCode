"""Live check of card 0216: each provider's model list is fetched in an editor someone uses, and never unattended.

Stdlib only; run with `python -I`:

    python -I live_model_discovery_0216.py --engine <UE_5.8> --project <host>.uproject [--providers anthropic gemini deepseek]

Per provider: starts the host editor WITHOUT -unattended (live model lists are never fetched in -unattended runs, which
every other probe uses), with that provider as the user's Provider, Live Model Lists naming it (the host's
Config/DefaultNodeToCode.ini, restored afterwards) and its key from the repo's .env in the editor's environment only.
Expects in the editor log "Fetching the model list of <id>: GET <url>" and "Model list of <name>: N models", and a cache
file Saved/NodeToCode/ModelCache/<id>.json with N models and no key in it. With --warn-model, the provider's selected
model is set to that ID and the log must carry the selected-model warning. Then one more editor WITH -unattended and
Live Model Lists naming the provider must log no fetch. Key values are never printed; logs and caches are scanned with
find_leaks. Report: logs/live_model_discovery_0216.json.
"""
import argparse
import json
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from editor_session import EditorSession, free_port  # noqa: E402
from env_keys import find_leaks, load_provider_keys  # noqa: E402

PROVIDERS = {
    # id: (EN2CLLMProvider value, key variable, model setting, a deprecated model to select for the warning or None)
    'anthropic': ('Anthropic', 'ANTHROPIC_API_KEY', 'AnthropicModelId', 'claude-sonnet-4-5-20250929'),
    'gemini': ('Gemini', 'GEMINI_API_KEY', 'GeminiModelId', None),
    'deepseek': ('DeepSeek', 'DEEPSEEK_API_KEY', 'DeepSeekModelId', None),
    'openai': ('OpenAI', 'OPENAI_API_KEY', 'OpenAIModelId', 'gpt-5-mini'),
}
SETTINGS = '[/Script/NodeToCode.N2CSettings]'
USER = '[/Script/NodeToCode.N2CUserSettings]'


class AttendedSession(EditorSession):
    """An EditorSession without -unattended: the editor behaves as for someone at the desk (still -nullrhi)"""

    def __init__(self, *a, attended=True, **kw):
        super().__init__(*a, **kw)
        self.attended = attended

    def args(self):
        a = super().args()
        return [x for x in a if x != '-unattended'] if self.attended else a


def wait_for(session, pattern, timeout):
    deadline = time.time() + timeout
    while time.time() < deadline:
        m = re.search(pattern, session.log_text())
        if m:
            return m
        if session.proc.poll() is not None:
            return None
        time.sleep(1.0)
    return None


def run_one(args, provider_id, keys, attended):
    enum_value, variable, model_setting, warn_model = PROVIDERS[provider_id]
    host_dir = os.path.dirname(os.path.abspath(args.project))
    cache = os.path.join(host_dir, 'Saved', 'NodeToCode', 'ModelCache', provider_id + '.json')
    tag = provider_id + ('' if attended else '_unattended')
    log_path = os.path.join(HERE, 'logs', 'live_model_discovery_0216_%s.log' % tag)
    result = {'provider': provider_id, 'attended': attended}
    session = AttendedSession(args.engine, args.project, free_port(args.port), log_path, allow_python=False,
                              extra_env={variable: keys[variable]} if variable in keys else {}, attended=attended)
    session.guard_file(cache)
    if os.path.exists(cache):
        os.remove(cache)
    session.patch_file(session.project_config_path(), SETTINGS, '+LiveModelListProviders', provider_id)
    if warn_model and attended and args.warn_model:
        session.patch_file(session.project_config_path(), SETTINGS, model_setting, warn_model)
    session._patch('EditorPerProjectUserSettings.ini', USER, 'Provider', enum_value)
    try:
        started = session.start(timeout=args.timeout)
        result['started'] = started
        if not started:
            result['error'] = 'the editor did not start'
            return result
        if attended:
            fetch = wait_for(session, r'Fetching the model list of %s: GET (\S+)' % re.escape(provider_id), 60)
            done = wait_for(session, r'Model list of [^:]+: (\d+) models \((\d+) marked deprecated\)', 90)
            result['url'] = fetch.group(1) if fetch else None
            result['models'] = int(done.group(1)) if done else None
            result['deprecated'] = int(done.group(2)) if done else None
            failed = re.search(r'Could not fetch the model list of [^\r\n]+', session.log_text())
            result['fetch_error'] = failed.group(0) if failed else None
            if warn_model and args.warn_model:
                warning = wait_for(session, r'%s is deprecated[^\r\n]*' % re.escape(warn_model), 15)
                result['warning'] = warning.group(0) if warning else None
            if os.path.exists(cache):
                data = json.load(open(cache, encoding='utf-8'))
                result['cache_models'] = len(data.get('models', []))
                result['cache_sample'] = [m['id'] for m in data.get('models', [])[:5]]
            result['ok'] = bool(result['url'] and result['models'] and result.get('cache_models') == result['models']
                                and (not (warn_model and args.warn_model) or result.get('warning')))
        else:
            time.sleep(20)  # past the 3 s startup refresh
            text = session.log_text()
            result['fetch_logged'] = 'Fetching the model list' in text
            result['cache_written'] = os.path.exists(cache)
            result['ok'] = not result['fetch_logged'] and not result['cache_written']
        result['leaks'] = [list(x) for x in find_leaks(keys, [log_path] + ([cache] if os.path.exists(cache) else []))]
        result['ok'] = result['ok'] and not result['leaks']
        return result
    finally:
        session.stop()
        result['config_restored'] = session.restore_ok


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--providers', nargs='*', default=['anthropic', 'gemini', 'deepseek'], choices=sorted(PROVIDERS))
    ap.add_argument('--warn-model', action='store_true', help='select a deprecated model and expect the warning')
    ap.add_argument('--port', type=int, default=27216 + int(os.environ.get('N2C_PORT_OFFSET', '0')))
    ap.add_argument('--timeout', type=int, default=300)
    ap.add_argument('--report', default=os.path.join(HERE, 'logs', 'live_model_discovery_0216.json'))
    args = ap.parse_args()
    os.makedirs(os.path.join(HERE, 'logs'), exist_ok=True)
    keys = load_provider_keys()
    results = [run_one(args, p, keys, attended=True) for p in args.providers]
    results.append(run_one(args, args.providers[0], keys, attended=False))
    with open(args.report, 'w', encoding='utf-8') as f:
        json.dump(results, f, indent=1)
    for r in results:
        print(json.dumps({k: v for k, v in r.items() if k != 'cache_sample'}))
    ok = all(r.get('ok') for r in results)
    print('PASS' if ok else 'FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
