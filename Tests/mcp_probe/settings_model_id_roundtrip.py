"""Project Settings model selection by string model ID (card 0129), checked in a headless host editor.

Stdlib only; run with `python -I`:

    python -I settings_model_id_roundtrip.py --engine G:/UE/UE_5.8 \
        --project "G:/Personal Projects/n2c/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" --label 5.8

Session 1 reads the Model drop-down options (Get<Provider>ModelOptions, the DeepSeek Thinking options and the Model
Price Overrides key options) from the settings object, then sets a model ID, a custom model ID, the DeepSeek thinking
mode and a price override through set_editor_property, which goes through PostEditChangeProperty and saves each value
to the host's Config/DefaultNodeToCode.ini as a Project Settings edit does. Session 2 starts on that saved file and
reads the values back. EditorSession puts the host's config files back byte for byte afterwards. No provider key is
used and nothing is sent to a provider. Report: logs/settings_model_id_roundtrip_<label>.json.
"""
import argparse
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from editor_session import EditorSession, free_port  # noqa: E402
from live_env_translation import call_tool, tool_text  # noqa: E402

SECTION = '[/Script/NodeToCode.N2CSettings]'

SET_PY = r'''
import unreal
s = unreal.get_default_object(unreal.N2CSettings)
options = {
    "anthropic": list(s.get_anthropic_model_options()),
    "openai": list(s.get_open_ai_model_options()),
    "gemini": list(s.get_gemini_model_options()),
    "deepseek": list(s.get_deep_seek_model_options()),
    "deepseek_thinking": list(s.get_deep_seek_reasoning_effort_options()),
    "pricing_keys": list(s.get_model_pricing_key_options()),
}
s.set_editor_property("provider", unreal.N2CLLMProvider.ANTHROPIC)
s.set_editor_property("anthropic_model_id", "claude-haiku-5-5")
s.set_editor_property("open_ai_custom_model_id", "gpt-roundtrip-test")
s.set_editor_property("deep_seek_reasoning_effort", "none")
prices = unreal.Map(str, unreal.N2CModelPricing)
p = unreal.N2CModelPricing()
p.set_editor_property("input_cost", 1.25)
p.set_editor_property("output_cost", 2.5)
prices["claude-haiku-5-5"] = p
s.set_editor_property("model_pricing", prices)
result = {"options": options, "cost": [s.get_current_input_cost(), s.get_current_output_cost()]}
'''

READ_PY = r'''
import unreal
s = unreal.get_default_object(unreal.N2CSettings)
pricing = {str(k): [v.get_editor_property("input_cost"), v.get_editor_property("output_cost")]
           for k, v in s.get_editor_property("model_pricing").items()}
result = {
    "provider": s.get_editor_property("provider").name,
    "anthropic_model_id": s.get_editor_property("anthropic_model_id"),
    "open_ai_model_id": s.get_editor_property("open_ai_model_id"),
    "open_ai_custom_model_id": s.get_editor_property("open_ai_custom_model_id"),
    "deep_seek_model_id": s.get_editor_property("deep_seek_model_id"),
    "deep_seek_reasoning_effort": s.get_editor_property("deep_seek_reasoning_effort"),
    "model_pricing": pricing,
    "cost": [s.get_current_input_cost(), s.get_current_output_cost()],
}
'''


def run_py(port, code, rid):
    status, response = call_tool(port, 'run-python', {'code': code}, rid)
    text = tool_text(response)
    try:
        return json.loads(text or '{}').get('data')
    except ValueError:
        return {'status': status, 'text': (text or '')[:600]}


def catalog_ids(repo):
    data = json.load(open(os.path.join(repo, 'Content', 'Data', 'N2CModelCatalog.json'), encoding='utf-8'))
    ids = {}
    for m in data['models']:
        if m['lifecycle']['status'] != 'retired':
            ids.setdefault(m['provider'], [])
            if m['id'] not in ids[m['provider']]:
                ids[m['provider']].append(m['id'])
    return ids


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--label', required=True)
    args = ap.parse_args()
    repo = os.path.abspath(os.path.join(HERE, '..', '..'))
    logs = os.path.join(HERE, 'logs')
    os.makedirs(logs, exist_ok=True)
    report = {'checks': {}}
    port = free_port(27150)

    s1 = EditorSession(args.engine, args.project, port, os.path.join(logs, 'settings_model_id_roundtrip_%s_1.log' % args.label))
    config = s1.project_config_path()
    with s1:
        report['started_1'] = s1.start(timeout=900)
        if report['started_1']:
            report['set'] = run_py(port, SET_PY, 1)
        saved = open(config, encoding='utf-8-sig').read() if os.path.isfile(config) else ''
    report['saved_ini'] = [l for l in saved.splitlines() if re.match(r'(Provider|\w+ModelId|DeepSeekReasoningEffort|ModelPricing|SettingsVersion)=', l)]

    s2 = EditorSession(args.engine, args.project, port, os.path.join(logs, 'settings_model_id_roundtrip_%s_2.log' % args.label))
    s2.guard_file(config)
    with open(config, 'w', encoding='utf-8') as f:
        f.write(saved)
    with s2:
        report['started_2'] = s2.start(timeout=900)
        if report['started_2']:
            report['read'] = run_py(port, READ_PY, 2)
    report['config_restored'] = bool(s1.restore_ok) and bool(s2.restore_ok)

    want = catalog_ids(repo)
    options = (report.get('set') or {}).get('options') or {}
    c = report['checks']
    for provider in ('anthropic', 'openai', 'gemini', 'deepseek'):
        c['options_' + provider] = options.get(provider) == want.get(provider)
    c['options_deepseek_thinking'] = set(options.get('deepseek_thinking') or []) >= {'none', 'high'}
    c['options_pricing_keys'] = set(options.get('pricing_keys') or []) >= set(sum(want.values(), []))
    c['cost_override_in_session'] = (report.get('set') or {}).get('cost') == [1.25, 2.5]
    ini = '\n'.join(report['saved_ini'])
    c['ini_written'] = all(k in ini for k in ('AnthropicModelId=', 'OpenAICustomModelId=', 'DeepSeekReasoningEffort=',
                                              'ModelPricing=', 'SettingsVersion=4'))
    r = report.get('read') or {}
    c['round_trip'] = (r.get('provider') == 'ANTHROPIC' and r.get('anthropic_model_id') == 'claude-haiku-5-5' and
                       r.get('open_ai_custom_model_id') == 'gpt-roundtrip-test' and
                       r.get('deep_seek_reasoning_effort') == 'none' and
                       r.get('model_pricing') == {'claude-haiku-5-5': [1.25, 2.5]} and r.get('cost') == [1.25, 2.5])
    c['config_restored'] = report['config_restored']
    report['ok'] = all(c.values())
    out = os.path.join(logs, 'settings_model_id_roundtrip_%s.json' % args.label)
    with open(out, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1)
    for k, v in c.items():
        print('%-28s %s' % (k, 'PASS' if v else 'FAIL'))
    print('report', out)
    return 0 if report['ok'] else 1


if __name__ == '__main__':
    sys.exit(main())
