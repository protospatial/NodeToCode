"""Runs inside the 2.0 editor started by migration_12x_check.py (-ExecutePythonScript). Writes what the editor loaded
to <Project>/Saved/n2c_migration_dump.json and prints one MIGDUMP line. API key values are never written: only their
length, whether they look like the fixture's dummies, and the API Key Source line Project Settings shows.
"""
import json
import os

import unreal

OUT = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), 'n2c_migration_dump.json')
NEVER = unreal.PropertyAccessChangeNotifyMode.NEVER

SETTINGS = ['provider', 'anthropic_model', 'open_ai_model', 'gemini_model', 'deep_seek_model', 'ollama_config',
            'ollama_model', 'lm_studio_model', 'lm_studio_endpoint', 'lm_studio_prepended_model_command',
            'open_ai_model_pricing', 'anthropic_model_pricing', 'gemini_model_pricing', 'deep_seek_model_pricing',
            'target_language', 'translation_depth', 'min_severity', 'estimated_reference_tokens',
            'reference_source_file_paths', 'custom_translation_output_directory', 'anthropic_max_output_tokens',
            'open_ai_max_output_tokens', 'gemini_max_output_tokens', 'deep_seek_max_output_tokens',
            'lm_studio_max_output_tokens', 'mcp_server_port', 'enable_dynamic_tool_discovery',
            'enable_python_script_only_mode', 'anthropic_api_key_source', 'open_ai_api_key_source',
            'gemini_api_key_source', 'deep_seek_api_key_source']
KEY_FIELDS = ['anthropic_api_key_ui', 'open_ai_api_key_ui', 'gemini_api_key_ui', 'deep_seek_api_key_ui']


def norm(v):
    if isinstance(v, unreal.EnumBase):
        return v.name
    if isinstance(v, unreal.StructBase):
        return v.export_text()
    if isinstance(v, unreal.Map):
        return {str(norm(k)): norm(x) for k, x in v.items()}
    if isinstance(v, (unreal.Array, list, tuple)):
        return [norm(x) for x in v]
    if isinstance(v, (bool, int, float, str)) or v is None:
        return v
    return str(v)


def enum(cls, name):
    want = name.replace('_', '').upper()
    hits = [m for m in dir(cls) if not m.startswith('_') and m.replace('_', '').upper() == want]
    return getattr(cls, hits[0]) if len(hits) == 1 else None


out = {'settings': {}, 'missing': [], 'keys': {}, 'costs': {}, 'user': {}, 'errors': []}
s = unreal.get_default_object(unreal.N2CSettings)
for name in SETTINGS:
    try:
        out['settings'][name] = norm(s.get_editor_property(name))
    except Exception as e:  # noqa: BLE001
        out['missing'].append('%s: %s' % (name, e))
for name in KEY_FIELDS:
    value = s.get_editor_property(name) or ''
    out['keys'][name] = {'length': len(value), 'dummy': value.startswith('test-') and value.endswith(('-0000', '-main'))}
    value = None
try:
    theme = s.get_editor_property('code_editor_theme').get_editor_property('themes')
    out['settings']['code_editor_theme_names'] = sorted(str(k) for k in theme.keys())
    ui = s.get_editor_property('ui_colors')
    out['settings']['ui_colors_is_default'] = ui.export_text() == unreal.N2CUIColors().export_text()
except Exception as e:  # noqa: BLE001
    out['errors'].append('theme/ui: %s' % e)

# Pricing in effect (map entry if there is one, else the model registry), read with notify Never so nothing is saved;
# the selection is put back afterwards
saved = {p: s.get_editor_property(p) for p in ('provider', 'open_ai_model', 'deep_seek_model', 'anthropic_model', 'gemini_model')}
probe = [('OpenAI', 'open_ai_model', unreal.N2COpenAIModel, m) for m in ('GPT_o3', 'GPT_4_1', 'GPT4o_2024_08_06', 'GPT4o_Mini_2024_07_18')]
probe += [('DeepSeek', 'deep_seek_model', unreal.N2CDeepSeekModel, m) for m in ('DeepSeek_R1', 'DeepSeek_V3')]
probe += [('Anthropic', 'anthropic_model', unreal.N2CAnthropicModel, m) for m in ('Claude3_5_Haiku', 'Claude5_5_Sonnet')]
probe += [('Gemini', 'gemini_model', unreal.N2CGeminiModel, m) for m in ('Gemini_Flash_2_0', 'Gemini_3_8_Flash')]
try:
    for provider, prop, cls, model in probe:
        value = enum(cls, model)
        if value is None:
            continue
        s.set_editor_property('provider', enum(unreal.N2CLLMProvider, provider), NEVER)
        s.set_editor_property(prop, value, NEVER)
        out['costs'][model] = [round(s.get_current_input_cost(), 6), round(s.get_current_output_cost(), 6)]
finally:
    for p, v in saved.items():
        s.set_editor_property(p, v, NEVER)

try:
    u = unreal.get_default_object(unreal.N2CUserSettings)
    out['user'] = {'enable_mcp_server': u.get_editor_property('enable_mcp_server'),
                   'allow_mcp_python_execution': u.get_editor_property('allow_mcp_python_execution')}
except Exception as e:  # noqa: BLE001
    out['errors'].append('user settings: %s' % e)
# F2: the saved N2C JSON of earlier translations (1.2.x writes version 1.0.0) read with the plugin's own reader
# (UN2CPythonBridge::ReadN2CJson, card 0135); read only, so the folder stays byte-identical (F1)
out['n2c_json'] = []
translations = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), 'NodeToCode', 'Translations')
for base, _, names in os.walk(translations):
    for name in sorted(names):
        if not (name.startswith('N2C_BP_') and name.endswith('.json')):
            continue
        entry = {'file': os.path.relpath(os.path.join(base, name), translations).replace('\\', '/')}
        try:
            with open(os.path.join(base, name), encoding='utf-8-sig') as f:
                result = json.loads(unreal.N2CPythonBridge.read_n2c_json(f.read()))
            data = result.get('data') or {}
            entry.update({'success': result.get('success'), 'error': result.get('error')})
            entry.update({k: data.get(k) for k in ('source_version', 'version', 'valid', 'graphs', 'nodes', 'data_edges')})
        except Exception as e:  # noqa: BLE001
            entry['error'] = str(e)
        out['n2c_json'].append(entry)

with open(OUT, 'w', encoding='utf-8') as f:
    json.dump(out, f, indent=1, sort_keys=True)
print('MIGDUMP written %s (%d settings, %d missing, %d errors)' % (OUT, len(out['settings']), len(out['missing']), len(out['errors'])))
