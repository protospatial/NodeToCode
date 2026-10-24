"""Write a 1.2.x NodeToCode project's settings and keys with 1.2.x itself. Not run by the check; kept so the fixtures
can be regenerated (see README.md).

Run inside an editor built from the v1.2.3 tag or from origin/main (the 1.2.x line):
    UnrealEditor-Cmd <host>.uproject -unattended -nullrhi -ExecutePythonScript=<this file>

Every value is set the way the Project Settings panel sets it: set_editor_property with notify Always calls 1.2.x's
UN2CSettings::PostEditChangeProperty, which writes that key to Config/DefaultNodeToCode.ini with
UpdateSinglePropertyInConfigFile, or, for the four API key fields, saves Saved/NodeToCode/User/secrets.json through
UN2CUserSecrets. 1.2.x's module startup adds the [HTTP] keys to Config/DefaultEngine.ini by itself. The key values
are obvious dummies. The variant is picked by whether the build has LM Studio (main) or not (the tag).
"""
import unreal

ALWAYS = unreal.PropertyAccessChangeNotifyMode.ALWAYS
s = unreal.get_default_object(unreal.N2CSettings)
MAIN = hasattr(s, 'lm_studio_model')
TAG = 'FIXMAIN' if MAIN else 'FIX123'


def enum(cls, name):
    want = name.replace('_', '').upper()
    hits = [m for m in dir(cls) if not m.startswith('_') and m.replace('_', '').upper() == want]
    if len(hits) != 1:
        raise RuntimeError('enum %s.%s: %s' % (cls.__name__, name, hits))
    return getattr(cls, hits[0])


def put(prop, value):
    try:
        s.set_editor_property(prop, value, ALWAYS)
        print('%s set %s' % (TAG, prop))
    except Exception as e:  # noqa: BLE001
        print('%s FAIL %s %s' % (TAG, prop, e))


if MAIN:
    put('provider', enum(unreal.N2CLLMProvider, 'LMStudio'))
    put('anthropic_model', enum(unreal.N2CAnthropicModel, 'Claude4_Opus'))      # retired
    put('open_ai_model', enum(unreal.N2COpenAIModel, 'GPT_4_1'))                # kept in 2.0 (deprecated)
    put('gemini_model', enum(unreal.N2CGeminiModel, 'Gemini_2_5_Pro'))          # retired
    put('deep_seek_model', enum(unreal.N2CDeepSeekModel, 'DeepSeek_V3'))
else:
    put('provider', enum(unreal.N2CLLMProvider, 'Gemini'))
    put('anthropic_model', enum(unreal.N2CAnthropicModel, 'Claude3_5_Haiku'))   # retired
    put('open_ai_model', enum(unreal.N2COpenAIModel, 'GPT_o1'))                 # retired
    # Gemini_2_5_ProExp is the tag's default; it is the one 1.2.x value 2.0 no longer has, so it is written explicitly.
    put('gemini_model', enum(unreal.N2CGeminiModel, 'Gemini_2_5_ProExp'))
    put('deep_seek_model', enum(unreal.N2CDeepSeekModel, 'DeepSeek_V3'))

oc = s.get_editor_property('ollama_config')
oc.set_editor_property('ollama_endpoint', 'http://192.168.1.50:11434')
oc.set_editor_property('use_system_prompts', False)   # 1.2.x default, migrated to true by 2.0
oc.set_editor_property('keep_alive', 600)
oc.set_editor_property('temperature', 0.7)
oc.set_editor_property('num_predict', 8192)            # 1.2.x default, migrated to 0 (server default)
oc.set_editor_property('top_p', 0.5)                   # 1.2.x default: its override stays off
oc.set_editor_property('top_k', 20)
oc.set_editor_property('min_p', 0.05)                  # 1.2.x default
oc.set_editor_property('repeat_penalty', 1.2)
oc.set_editor_property('num_ctx', 8192)                # 1.2.x default, migrated to 0
oc.set_editor_property('mirostat', 1)
oc.set_editor_property('mirostat_eta', 0.2)
oc.set_editor_property('mirostat_tau', 4.0)
oc.set_editor_property('seed', 42)
if MAIN:
    oc.set_editor_property('prepended_model_command', '/no_think')
put('ollama_config', oc)
put('ollama_model', 'qwen2.5-coder:14b' if MAIN else 'codellama:13b')

if MAIN:
    put('lm_studio_model', 'gemma-3-27b')
    put('lm_studio_endpoint', 'http://127.0.0.1:1235')
    put('lm_studio_prepended_model_command', '/nothink')


def reprice(prop, struct_cls, changes):
    m = s.get_editor_property(prop)
    for key, (i, o) in changes.items():
        p = struct_cls()
        p.set_editor_property('input_cost', i)
        p.set_editor_property('output_cost', o)
        m[key] = p
    put(prop, m)


# One user price per map; every other entry stays at 1.2.x's built-in price (InitializePricing).
oai = 'GPT_4_1' if MAIN else 'GPT4o_2024_08_06'
reprice('open_ai_model_pricing', unreal.N2COpenAIPricing, {enum(unreal.N2COpenAIModel, oai): (1.5, 6.0)})
reprice('anthropic_model_pricing', unreal.N2CAnthropicPricing, {enum(unreal.N2CAnthropicModel, 'Claude3_5_Haiku'): (1.0, 5.0)})
reprice('gemini_model_pricing', unreal.N2CGeminiPricing, {enum(unreal.N2CGeminiModel, 'Gemini_Flash_2_0'): (0.1, 0.4)})
reprice('deep_seek_model_pricing', unreal.N2CDeepSeekPricing, {enum(unreal.N2CDeepSeekModel, 'DeepSeek_V3'): (0.2, 0.5)})

put('target_language', enum(unreal.N2CCodeLanguage, 'JavaScript' if MAIN else 'Python'))
put('translation_depth', 3 if MAIN else 2)
put('min_severity', enum(unreal.N2CLogSeverity, 'Warning' if MAIN else 'Debug'))

for prop in ('cpp_themes', 'python_themes', 'java_script_themes', 'c_sharp_themes', 'swift_themes', 'pseudocode_themes'):
    t = s.get_editor_property(prop)
    if prop in ('cpp_themes', 'python_themes'):
        themes = t.get_editor_property('themes')
        ue = themes['Unreal Engine']
        ue.set_editor_property('keywords', unreal.Color(r=0x11, g=0x22, b=0x33, a=0xff))
        themes['Unreal Engine'] = ue
        t.set_editor_property('themes', themes)
    put(prop, t)

project = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
d = unreal.DirectoryPath()
d.set_editor_property('path', project + 'CustomTranslations')
put('custom_translation_output_directory', d)

# 1.2.x returns before saving when the array itself changes, so this only lands in memory (see README.md).
fp = unreal.FilePath()
fp.set_editor_property('file_path', project + 'Source/H123/H123.cpp')
put('reference_source_file_paths', [fp])

suffix = 'main' if MAIN else '0000'
put('anthropic_api_key_ui', 'test-anthropic-key-%s' % suffix)
put('open_ai_api_key_ui', 'test-openai-key-%s' % suffix)
put('gemini_api_key_ui', 'test-gemini-key-%s' % suffix)
put('deep_seek_api_key_ui', 'test-deepseek-key-%s' % suffix)
print('%s DONE' % TAG)
