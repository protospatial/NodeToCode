"""Prove a NodeToCode 1.2.x project's settings and saved data load in a 2.0 build (card 0079).

Stdlib only; run with `python -I` from anywhere:

    python -I Tests/Migration/migration_12x_check.py --engine G:/UE/UE_5.8 \
        --host "G:/Personal Projects/n2c/Hosts/NodeToCodeHost_5_8" --label 5.8

For each fixture (default: 1.2.3, then 1.2.x-main) it makes a scratch copy of the built host project in a temp folder
(never the host itself), puts the fixture's Config/ and Saved/ into it, starts a headless 2.0 editor twice with
migration_dump.py, and checks every row of Context/release/migration-1.2-to-2.0.md that a project's files can show.
A third start of the last fixture checks the environment-variable key path (K3) with a dummy value. It prints one
PASS / FAIL / SKIP line per row and exits non-zero on any FAIL. Report: Tests/Migration/logs/migration_<label>.json.

Machine state it touches, always restored in a finally block:
- the per-user secrets folder (%LOCALAPPDATA%/NodeToCode): moved aside before the first start and put back at the end,
  so the fixtures' dummy keys never mix with real ones;
- the engine's per-user EditorSettings.ini (%LOCALAPPDATA%/UnrealEngine/<ver>/Saved/Config/WindowsEditor): saved,
  set to bThrottleCPUWhenNotForeground=False as 1.2.x left it, and written back byte for byte.
The fixtures hold dummy keys only ("test-...-0000" / "test-...-main"); no key value is printed.
"""
import argparse
import hashlib
import json
import math
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'mcp_probe'))
from editor_session import find_editor, kill_with_parent  # noqa: E402
from env_keys import find_leaks, load_provider_keys  # noqa: E402

DUMP = os.path.join(HERE, 'migration_dump.py').replace('\\', '/')
PRERELEASE = os.path.join(HERE, 'prerelease')
TRANSLATION = 'BP_Door_2025-05-12-14.03.27'
KEY_NAMES = ('OpenAI', 'Anthropic', 'Gemini', 'DeepSeek')

# What each fixture holds (as 1.2.x wrote it) and what 2.0 must make of it. Model names are C++ enumerator names.
FIXTURES = {
    '1.2.3': {
        'provider': 'Gemini', 'anthropic': 'Claude3_5_Haiku', 'openai': 'GPT_o1', 'gemini': 'Gemini_2_5_ProExp',
        'deepseek': 'DeepSeek_V3', 'ollama_model': 'codellama:13b', 'lmstudio': None, 'language': 'Python',
        'depth': 2, 'severity': 'Debug', 'openai_kept_price': ('GPT4o_2024_08_06', 1.5, 6.0),
        'prerelease_tags': True, 'http_user': {},
    },
    '1.2.x-main': {
        'provider': 'LMStudio', 'anthropic': 'Claude4_Opus', 'openai': 'GPT_4_1', 'gemini': 'Gemini_2_5_Pro',
        'deepseek': 'DeepSeek_V3', 'ollama_model': 'qwen2.5-coder:14b',
        'lmstudio': {'lm_studio_model': 'gemma-3-27b', 'lm_studio_endpoint': 'http://127.0.0.1:1235',
                     'lm_studio_prepended_model_command': '/nothink'},
        'language': 'JavaScript', 'depth': 3, 'severity': 'Warning', 'openai_kept_price': ('GPT_4_1', 1.5, 6.0),
        'prerelease_tags': False, 'http_user': {'HttpConnectionTimeout': '120.000000', 'HttpTimeout': '180'},
    },
}
RETIRED_TARGET = {'anthropic': 'Claude5_5_Sonnet', 'openai': 'GPT_6_1_Sol', 'gemini': 'Gemini_3_8_Flash'}
KEPT_OPENAI = {'GPT4o_2024_08_06', 'GPT4o_Mini_2024_07_18', 'GPT_4_1', 'GPT_o3'}
THEME_KEYS = ('CPPThemes', 'PythonThemes', 'JavaScriptThemes', 'CSharpThemes', 'SwiftThemes', 'PseudocodeThemes')
SEVERITY = ['Debug', 'Info', 'Warning', 'Error', 'Fatal']
# Behaviour that differs from the matrix because of a defect an open card owns: reported as KNOWN (not a failure)
# while the defect is present, PASS once it is fixed, FAIL if it turns into something else
KNOWN_NOTE = {
    'S24': '; KNOWN (card 0186): the constructor computes the estimate before LoadConfig, so the saved number stays',
    'S25': '; KNOWN (card 0186): the constructor validates the paths before LoadConfig, so a missing file is kept '
           'and each translation warns "Failed to load reference source file"',
}


def known(row, matches):
    return 'KNOWN' if matches else False


def n(name):
    """Compare UE enum names across C++ (Claude5_5_Sonnet) and Python (CLAUDE5_5_SONNET) spellings"""
    return str(name).replace('_', '').upper()


def quote(arg):
    if ' ' not in arg:
        return arg
    if arg.startswith('-') and '=' in arg:
        key, value = arg.split('=', 1)
        return '%s="%s"' % (key, value)
    return '"%s"' % arg


def read(path, binary=False):
    try:
        data = open(path, 'rb').read()
    except OSError:
        return None
    return data if binary else data.replace(b'\x00', b'').decode('utf-8-sig', 'replace')


def ini_value(text, section, key):
    """Last value of key in [section] (UE layering: last line wins), or None"""
    value, current = None, None
    for line in (text or '').splitlines():
        s = line.strip()
        if s.startswith('[') and s.endswith(']'):
            current = s[1:-1]
        elif current == section and '=' in s and s.split('=', 1)[0].strip().lstrip('+!-.') == key:
            value = s.split('=', 1)[1]
    return value


def engine_version(engine):
    v = json.load(open(os.path.join(engine, 'Engine', 'Build', 'Build.version')))
    return '%d.%d' % (v['MajorVersion'], v['MinorVersion'])


def tree_hashes(root):
    out = {}
    for base, _, files in os.walk(root):
        for f in files:
            p = os.path.join(base, f)
            out[os.path.relpath(p, root).replace('\\', '/')] = hashlib.sha256(open(p, 'rb').read()).hexdigest()
    return out


def ports_free(*ports):
    for p in ports:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            if s.connect_ex(('127.0.0.1', p)) == 0:
                return False
    return True


class Rows:
    def __init__(self, fixture):
        self.fixture = fixture
        self.items = []

    def add(self, row, ok, detail):
        status = 'SKIP' if ok is None else ('KNOWN' if ok == 'KNOWN' else ('PASS' if ok else 'FAIL'))
        self.items.append({'fixture': self.fixture, 'row': row, 'status': status, 'detail': detail})
        print('%-5s %-11s %-4s %s' % (status, self.fixture, row, detail), flush=True)


def make_scratch(host, root, fixture):
    """Scratch copy of the built host: .uproject, Config, host Binaries, the plugin's .uplugin and Binaries, and
    copies (not junctions) of its Content, Resources and ThirdParty, so nothing in the repo or host can be written"""
    uproject = [f for f in os.listdir(host) if f.endswith('.uproject')][0]
    dst = os.path.join(root, 'N2CMig_' + re.sub(r'[^A-Za-z0-9]', '_', fixture))
    os.makedirs(dst)
    shutil.copy2(os.path.join(host, uproject), dst)
    shutil.copytree(os.path.join(host, 'Config'), os.path.join(dst, 'Config'))
    shutil.copytree(os.path.join(host, 'Binaries'), os.path.join(dst, 'Binaries'))
    plugin = os.path.join(host, 'Plugins', 'NodeToCode')
    pdst = os.path.join(dst, 'Plugins', 'NodeToCode')
    os.makedirs(pdst)
    shutil.copy2(os.path.join(plugin, 'NodeToCode.uplugin'), pdst)
    for d in ('Binaries', 'Content', 'Resources', 'ThirdParty'):
        if os.path.isdir(os.path.join(plugin, d)):
            shutil.copytree(os.path.join(plugin, d), os.path.join(pdst, d))
    return dst, os.path.join(dst, uproject)


def overlay(src, dst, project_dir):
    for base, _, files in os.walk(src):
        for f in files:
            p = os.path.join(base, f)
            rel = os.path.relpath(p, src)
            if rel.split(os.sep)[0] == 'Config':
                continue
            t = os.path.join(dst, rel)
            os.makedirs(os.path.dirname(t), exist_ok=True)
            shutil.copyfile(p, t)


def seed(fixture, project_dir, original_engine_ini):
    fdir = os.path.join(HERE, fixture)
    pdir = project_dir.replace('\\', '/').rstrip('/') + '/'
    ini = read(os.path.join(fdir, 'Config', 'DefaultNodeToCode.ini'), True).replace(b'{PROJECT_DIR}', pdir.encode())
    open(os.path.join(project_dir, 'Config', 'DefaultNodeToCode.ini'), 'wb').write(ini)
    http = read(os.path.join(fdir, 'Config', 'DefaultEngine.HTTP.ini'), True)
    base = original_engine_ini.rstrip(b'\r\n') + b'\r\n\r\n'
    open(os.path.join(project_dir, 'Config', 'DefaultEngine.ini'), 'wb').write(base + http)
    overlay(fdir, project_dir, pdir)
    if FIXTURES[fixture]['prerelease_tags']:
        overlay(PRERELEASE, project_dir, pdir)
    return ini


def run_start(engine, uproject, log_path, mcp, env_extra=None, timeout=900):
    args = [find_editor(engine), uproject, '-unattended', '-nullrhi', '-nosplash', '-nosound', '-NoLiveCoding',
            '-culture=en', '-abslog=%s' % log_path, '-ExecutePythonScript=%s' % DUMP]
    if mcp:
        args.append('-N2CMcp')
    if os.path.exists(log_path):
        os.remove(log_path)
    env = dict(os.environ)
    for k in ('ANTHROPIC_API_KEY', 'OPENAI_API_KEY', 'GEMINI_API_KEY', 'DEEPSEEK_API_KEY', 'DEEPSEEK_API'):
        env.pop(k, None)
    env.update(env_extra or {})
    command = ' '.join(quote(a) for a in args) if os.name == 'nt' else args
    proc = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)
    job = kill_with_parent(proc)
    try:
        code = proc.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        proc.kill()
        code = 'timeout'
    del job
    text = read(log_path) or ''
    dump = None
    try:
        dump = json.load(open(os.path.join(os.path.dirname(uproject), 'Saved', 'n2c_migration_dump.json')))
        os.remove(os.path.join(os.path.dirname(uproject), 'Saved', 'n2c_migration_dump.json'))
    except (OSError, ValueError):
        pass
    return {'code': code, 'log': text, 'dump': dump}


def n2c_lines(log):
    return [l for l in log.splitlines() if 'LogNodeToCode' in l]


def warnings(log):
    """NodeToCode warning and error lines"""
    return [l for l in n2c_lines(log) if re.search(r'LogNodeToCode: (Warning|Error):|\[(WARNING|ERROR|FATAL)\]', l)]


def engine_import_errors(log):
    return [l for l in log.splitlines() if ('import failed for' in l or 'invalid value of' in l) and 'N2CSettings' in l]


def secrets_user_path():
    base = os.environ.get('LOCALAPPDATA') or os.path.expanduser('~/.config')
    return os.path.join(base, 'NodeToCode')


def check_fixture(fixture, ctx, rows, state):
    exp = FIXTURES[fixture]
    project_dir, uproject = make_scratch(ctx['host'], ctx['root'], fixture)
    original_engine_ini = read(os.path.join(project_dir, 'Config', 'DefaultEngine.ini'), True)
    seeded_ini = seed(fixture, project_dir, original_engine_ini)
    saved = os.path.join(project_dir, 'Saved', 'NodeToCode')
    project_secrets = os.path.join(saved, 'User', 'secrets.json')
    fixture_keys = json.load(open(project_secrets))
    translation_dir = os.path.join(saved, 'Translations', TRANSLATION)
    before_translation = tree_hashes(translation_dir) if os.path.isdir(translation_dir) else {}
    mcp = ports_free(27000, 27001)
    logs = ctx['logs']
    label = '%s_%s' % (re.sub(r'[^A-Za-z0-9]', '_', fixture), ctx['label'])

    s1 = run_start(ctx['engine'], uproject, os.path.join(logs, 'migration_%s_start1.log' % label), mcp)
    ini1 = read(os.path.join(project_dir, 'Config', 'DefaultNodeToCode.ini'), True)
    eng1 = read(os.path.join(project_dir, 'Config', 'DefaultEngine.ini'))
    state_file = os.path.join(saved, 'BlueprintGraphState.json')
    state1 = read(state_file, True)
    s2 = run_start(ctx['engine'], uproject, os.path.join(logs, 'migration_%s_start2.log' % label), mcp)
    ini2 = read(os.path.join(project_dir, 'Config', 'DefaultNodeToCode.ini'), True)
    eng2 = read(os.path.join(project_dir, 'Config', 'DefaultEngine.ini'))
    state2 = read(state_file, True)
    after_translation = tree_hashes(translation_dir) if os.path.isdir(translation_dir) else {}

    ok_runs = s1['dump'] is not None and s2['dump'] is not None
    rows.add('run', ok_runs and not s1['dump']['missing'] and not s2['dump']['missing'] and not s1['dump']['errors'],
             'two headless starts (exit %s, %s); dump missing=%s errors=%s' % (
                 s1['code'], s2['code'], (s1['dump'] or {}).get('missing'), (s1['dump'] or {}).get('errors')))
    if not ok_runs:
        return project_dir
    d1, d2 = s1['dump']['settings'], s2['dump']['settings']
    ini1t = ini1.decode('utf-8', 'replace')
    sec = '/Script/NodeToCode.N2CSettings'
    w1, w2 = warnings(s1['log']), warnings(s2['log'])
    # The migration's Info lines are logged before Min Severity is applied; the MCP server's lines come after it
    mcp_logged = SEVERITY.index(exp['severity']) <= SEVERITY.index('Info')
    info1 = '\n'.join(n2c_lines(s1['log']))
    retired_warn1 = [l for l in w1 if 'no longer served by its provider' in l]
    retired_warn2 = [l for l in w2 if 'no longer served by its provider' in l]

    # Idempotence: the second start changes nothing and says nothing
    rows.add('idem', ini1 == ini2 and eng1 == eng2 and d1 == d2 and state1 == state2 and not w2 and
             not engine_import_errors(s2['log']),
             'start 2: DefaultNodeToCode.ini %s, DefaultEngine.ini %s, loaded values %s, graph state %s, '
             'NodeToCode warnings %d, engine import errors %d' % (
                 'unchanged' if ini1 == ini2 else 'CHANGED', 'unchanged' if eng1 == eng2 else 'CHANGED',
                 'same' if d1 == d2 else 'DIFFER', 'unchanged' if state1 == state2 else 'CHANGED', len(w2),
                 len(engine_import_errors(s2['log']))))
    if w2:
        rows.add('idem', False, 'start 2 warnings: %s' % ' | '.join(l.split('LogNodeToCode:')[-1][:160] for l in w2))

    rows.add('S1', n(d1['provider']) == n(exp['provider']), 'Provider %s (fixture %s)' % (d1['provider'], exp['provider']))

    for row, key, prop, ini_key in (('S2', 'anthropic', 'anthropic_model', 'AnthropicModel'),
                                    ('S3', 'openai', 'open_ai_model', 'OpenAI_Model'),
                                    ('S4', 'gemini', 'gemini_model', 'Gemini_Model')):
        old = exp[key]
        if key == 'openai' and old in KEPT_OPENAI:
            ok = (n(d1[prop]) == n(old) and ini_value(ini1t, sec, ini_key) == old and
                  not any(ini_key + '=' in l for l in retired_warn1))
            rows.add(row, ok, '%s kept: loads %s, ini %s=%s, no retired warning' % (
                ini_key, d1[prop], ini_key, ini_value(ini1t, sec, ini_key)))
            continue
        new = RETIRED_TARGET[key]
        named = [l for l in w1 if '%s=%s' % (ini_key, old) in l and new in l]
        ok = n(d1[prop]) == n(new) and ini_value(ini1t, sec, ini_key) == new and bool(named) and not retired_warn2
        detail = '%s=%s -> loads %s, ini %s=%s, start-1 warning names it: %s, start 2 silent: %s' % (
            ini_key, old, d1[prop], ini_key, ini_value(ini1t, sec, ini_key), bool(named), not retired_warn2)
        if key == 'gemini' and old == 'Gemini_2_5_ProExp':
            eng_err = [l for l in engine_import_errors(s1['log']) if 'Gemini_Model' in l]
            ok = ok and bool(eng_err) and any('values NodeToCode 2.0 does not have' in l for l in named)
            detail += ', engine import error on start 1: %s (none on start 2: %s)' % (
                bool(eng_err), not engine_import_errors(s2['log']))
        rows.add(row, ok, detail)

    rows.add('S5', n(d1['deep_seek_model']) == n(exp['deepseek']) and not any('DeepSeekModel=' in l for l in w1),
             'DeepSeekModel %s (fixture %s), no warning; the request model (deepseek-flash) is not checked here' % (
                 d1['deep_seek_model'], exp['deepseek']))

    oc = d1['ollama_config']
    want = {'NumCtx': '0', 'NumPredict': '0', 'bUseSystemPrompts': 'True', 'bOverride_Temperature': 'True',
            'Temperature': '0.700000', 'bOverride_TopP': 'False', 'bOverride_TopK': 'True', 'TopK': '20',
            'bOverride_MinP': 'False', 'bOverride_RepeatPenalty': 'True', 'bOverride_Mirostat': 'True',
            'bOverride_Seed': 'True', 'Seed': '42', 'KeepAlive': '600', 'OllamaEndpoint': '"http://192.168.1.50:11434"'}
    if exp['lmstudio']:
        want['PrependedModelCommand'] = '"/no_think"'
    got = dict(re.findall(r'(\w+)=("[^"]*"|[^,()]+)', oc))
    bad = {k: got.get(k) for k, v in want.items() if got.get(k) != v}
    ini_oc = ini_value(ini1t, sec, 'OllamaConfig') or ''
    info_ok = 'Settings saved before NodeToCode 2.0 updated' in info1
    rows.add('S6', not bad and 'NumCtx=0' in ini_oc and info_ok,
             'OllamaConfig migrated%s; written to the ini: %s; Info line: %s' % (
                 '' if not bad else ' WRONG %s' % bad, 'NumCtx=0' in ini_oc,
                 'present' if info_ok else 'absent'))
    rows.add('S7', d1['ollama_model'] == exp['ollama_model'], 'OllamaModel %s' % d1['ollama_model'])
    for row, prop in (('S8', 'lm_studio_model'), ('S9', 'lm_studio_endpoint'), ('S10', 'lm_studio_prepended_model_command')):
        if not exp['lmstudio']:
            rows.add(row, None, 'not in the v1.2.3 tag (main only); see the 1.2.x-main fixture')
        else:
            rows.add(row, d1[prop] == exp['lmstudio'][prop], '%s %s' % (prop, d1[prop]))

    kept_model, kin, kout = exp['openai_kept_price']
    oai = d1['open_ai_model_pricing']
    costs = s1['dump']['costs']
    entry = oai.get(next((k for k in oai if n(k) == n(kept_model)), ''), '')
    ok = (len(oai) == 1 and 'InputCost=%.6f' % kin in entry and costs.get(kept_model) == [kin, kout] and
          costs.get('GPT_o3', [15.0])[0] != 15.0)
    rows.add('S11', ok, 'OpenAIModelPricing keeps only %s (%s); cost %s=%s; GPT_o3 cost %s (1.2.x had 15/60)' % (
        list(oai), entry, kept_model, costs.get(kept_model), costs.get('GPT_o3')))
    rows.add('S12', d1['anthropic_model_pricing'] == {}, 'AnthropicModelPricing %s' % d1['anthropic_model_pricing'])
    gem_ini = ini_value(ini1t, sec, 'GeminiModelPricing')
    gem_err = [l for l in engine_import_errors(s1['log']) if 'GeminiModelPricing' in l]
    rows.add('S13', d1['gemini_model_pricing'] == {} and gem_ini == '()' and not engine_import_errors(s2['log']),
             'GeminiModelPricing %s, ini GeminiModelPricing=%s, start-1 import error %s, start 2 none' % (
                 d1['gemini_model_pricing'], gem_ini, bool(gem_err)))
    ds = d1['deep_seek_model_pricing']
    rows.add('S14', list(map(n, ds)) == [n('DeepSeek_V3')] and costs.get('DeepSeek_V3') == [0.2, 0.5] and
             costs.get('DeepSeek_R1', [0.55])[0] != 0.55,
             'DeepSeekModelPricing keeps %s; cost V3 %s, R1 %s (1.2.x 0.55/2.19)' % (
                 list(ds), costs.get('DeepSeek_V3'), costs.get('DeepSeek_R1')))
    rows.add('S15', n(d1['target_language']) == n(exp['language']), 'TargetLanguage %s' % d1['target_language'])
    rows.add('S16', d1['translation_depth'] == exp['depth'], 'TranslationDepth %s' % d1['translation_depth'])
    rows.add('S17', n(d1['min_severity']) == n(exp['severity']), 'MinSeverity %s' % d1['min_severity'])
    theme_warn = [l for l in w1 if 'Code editor themes saved by NodeToCode 1.2.x' in l]
    for i, key in enumerate(THEME_KEYS):
        in_ini = ini_value(ini2.decode('utf-8', 'replace'), sec, key) is not None
        named = any(key in l for l in theme_warn)
        rows.add('S%d' % (18 + i), in_ini and named and 'Unreal Engine' in d1.get('code_editor_theme_names', []),
                 '%s still in the ini: %s; named in the one start-1 warning: %s' % (key, in_ini, named))
    ref = os.path.join(project_dir, 'N2CReference', 'DoorHelpers.h')
    want_tokens = math.ceil(len(read(ref)) / 4)
    tokens_ok = d1['estimated_reference_tokens'] == want_tokens
    rows.add('S24', tokens_ok or known('S24', d1['estimated_reference_tokens'] == 999),
             'EstimatedReferenceTokens %s (file %d chars / 4 = %d; fixture saved 999)%s' % (
                 d1['estimated_reference_tokens'], len(read(ref)), want_tokens, '' if tokens_ok else KNOWN_NOTE['S24']))
    paths = [re.search(r'FilePath="([^"]*)"', p).group(1) for p in d1['reference_source_file_paths']]
    missing_warn = any('Reference source file not found' in l and 'Removed.h' in l for l in s1['log'].splitlines())
    paths_ok = paths == [ref.replace('\\', '/')] and missing_warn
    rows.add('S25', paths_ok or known('S25', [os.path.basename(p) for p in paths] == ['DoorHelpers.h', 'Removed.h']),
             'ReferenceSourceFilePaths %s; missing Removed.h dropped with a warning: %s%s' % (
                 [os.path.basename(p) for p in paths], missing_warn, '' if paths_ok else KNOWN_NOTE['S25']))
    cdir = re.search(r'Path="([^"]*)"', d1['custom_translation_output_directory'])
    rows.add('S26', bool(cdir) and cdir.group(1) == project_dir.replace('\\', '/') + '/CustomTranslations',
             'CustomTranslationOutputDirectory %s (a translation landing there: --live)' % (cdir.group(1) if cdir else None))

    tokens = [d1[k] for k in ('anthropic_max_output_tokens', 'open_ai_max_output_tokens', 'gemini_max_output_tokens',
                              'deep_seek_max_output_tokens', 'lm_studio_max_output_tokens')]
    rows.add('N1', tokens == [0] * 5, 'MaxOutputTokens %s' % tokens)
    rows.add('N2', ini_value(ini1t, sec, 'SettingsVersion') == '3', 'ini SettingsVersion=%s after start 1' %
             ini_value(ini1t, sec, 'SettingsVersion'))
    rows.add('N3', d1['mcp_server_port'] == 27000, 'McpServerPort %s' % d1['mcp_server_port'])
    rows.add('N4', d1['enable_dynamic_tool_discovery'] is False, 'bEnableDynamicToolDiscovery %s' % d1['enable_dynamic_tool_discovery'])
    rows.add('N5', d1['enable_python_script_only_mode'] is False, 'bEnablePythonScriptOnlyMode %s' % d1['enable_python_script_only_mode'])
    rows.add('N6', d1.get('ui_colors_is_default') is True, 'UIColors at struct defaults: %s' % d1.get('ui_colors_is_default'))
    rows.add('N7', 'Unreal Engine' in d1.get('code_editor_theme_names', []), 'CodeEditorTheme has %d themes incl. "Unreal Engine": %s' % (
        len(d1.get('code_editor_theme_names', [])), 'Unreal Engine' in d1.get('code_editor_theme_names', [])))

    # K1 / K2: the project's keys move to the per-user file once
    user_file = os.path.join(secrets_user_path(), 'secrets.json')
    user = json.load(open(user_file)) if os.path.exists(user_file) else {}
    moved_line = [l for l in s1['log'].splitlines() if 'Moved API keys from' in l]
    leaked = [v for v in fixture_keys.values() if v and (v in s1['log'] or v in s2['log'])]
    keys1 = s1['dump']['keys']
    first = state.get('first_keys')
    if first is None:
        same = all(user.get(k + '_API_Key') == fixture_keys.get(k + '_API_Key') for k in KEY_NAMES)
        ok = (not os.path.exists(project_secrets) and not os.path.isdir(os.path.dirname(project_secrets)) and
              user.get('Version') == 1 and same and not leaked and
              all(keys1[f]['length'] == len(fixture_keys[k + '_API_Key']) for f, k in zip(
                  ('open_ai_api_key_ui', 'anthropic_api_key_ui', 'gemini_api_key_ui', 'deep_seek_api_key_ui'), KEY_NAMES)) and
              all(d1[p].startswith('Saved key') for p in ('anthropic_api_key_source', 'open_ai_api_key_source',
                                                         'gemini_api_key_source', 'deep_seek_api_key_source')) and
              bool(moved_line))
        rows.add('K1', ok, 'project secrets.json gone: %s, User folder gone: %s; per-user file Version %s with the '
                 'fixture\'s four keys: %s; Project Settings fields hold them (lengths only) and say "%s"; log names '
                 'both paths: %s; no key value in either log: %s' % (
                     not os.path.exists(project_secrets), not os.path.isdir(os.path.dirname(project_secrets)),
                     user.get('Version'), same, d1['anthropic_api_key_source'],
                     bool(moved_line),
                     not leaked))
        rows.add('K2', None, 'checked with the second fixture')
        state['first_keys'] = fixture_keys
    else:
        conflicts = user.get('MigrationConflicts', {})
        entry = conflicts.get(next(iter(conflicts), ''), {}) if conflicts else {}
        kept = all(user.get(k + '_API_Key') == first.get(k + '_API_Key') for k in KEY_NAMES)
        parked = all(entry.get(k + '_API_Key') == fixture_keys.get(k + '_API_Key') for k in KEY_NAMES)
        warn = [l for l in w1 if 'already had a different key' in l]
        rows.add('K1', not os.path.exists(project_secrets) and not leaked,
                 'second project\'s secrets.json moved out too: %s; no key value in either log: %s' % (
                     not os.path.exists(project_secrets), not leaked))
        rows.add('K2', kept and parked and bool(warn) and any(project_secrets.replace('\\', '/') in k.replace('\\', '/')
                                                                for k in conflicts),
                 'per-user keys still the first project\'s: %s; this project\'s keys under MigrationConflicts.<its '
                 'file>: %s; warning "already had a different key": %s' % (kept, parked, bool(warn)))
    rows.add('K4', None, 'earlier 2.0 builds only (sign-in fields); covered by NodeToCode.Secrets automation tests')

    # Files
    listed = TRANSLATION.encode() in (state1 or b'')
    rows.add('F1', (before_translation == after_translation) if before_translation else None,
             ('1.2.3 translation folder (%d files) byte-identical after two starts: %s; listed in graph state: %s' % (
                 len(before_translation), before_translation == after_translation, listed)) if before_translation
             else 'no translation folder in this fixture')
    if before_translation and listed:
        rows.add('F1', False, 'the 1.2.x folder is listed in BlueprintGraphState.json')
    rows.add('F2', None, 'needs a new translation: --live')
    legacy = os.path.join(saved, 'Tags', 'BlueprintTags.json')
    if exp['prerelease_tags']:
        st = json.loads(state1.decode('utf-8-sig')) if state1 else {}
        tags = sorted(t.get('tag') for g in st.get('graphs', []) for t in g.get('tags', []))
        rows.add('F3', state1 is not None, 'BlueprintGraphState.json written on start 1: %s' % (state1 is not None))
        rows.add('F4', tags == ['Gameplay', 'NeedsReview'] and not os.path.exists(legacy) and
                 os.path.exists(legacy + '.migrated') and 'Migrating from legacy' not in s2['log'],
                 'pre-release BlueprintTags.json (not 1.2.x data) migrated: tags %s, renamed .migrated: %s, not '
                 'migrated again on start 2: %s' % (tags, os.path.exists(legacy + '.migrated'),
                                                   'Migrating from legacy' not in s2['log']))
    else:
        rows.add('F3', state1 is None, 'no tags or translations, so no BlueprintGraphState.json: %s' % (state1 is None))
        rows.add('F4', None, '1.2.x had no tag file; the pre-release path is checked with the 1.2.3 fixture')
    rows.add('F6', not os.path.exists(os.path.join(project_dir, 'Saved', 'NodeToCode.log')),
             'no Saved/NodeToCode.log: %s' % (not os.path.exists(os.path.join(project_dir, 'Saved', 'NodeToCode.log'))))
    http_now = {k: ini_value(eng1, 'HTTP', k) for k in ('HttpConnectionTimeout', 'HttpActivityTimeout', 'HttpTimeout')}
    want_http = {'HttpConnectionTimeout': exp['http_user'].get('HttpConnectionTimeout'), 'HttpActivityTimeout': None,
                 'HttpTimeout': exp['http_user'].get('HttpTimeout')}
    other_same = eng1.split('[HTTP]')[0].rstrip() == original_engine_ini.decode('utf-8-sig').rstrip()
    removed_logged = 'removed [HTTP]' in info1
    rows.add('F7', http_now == want_http and other_same and removed_logged,
             '[HTTP] after start 1 %s (want %s); rest of DefaultEngine.ini unchanged: %s; Info line: %s' % (
                 http_now, want_http, other_same, removed_logged))
    cpu1 = [l for l in w1 if 'Use Less CPU when in Background' in l]
    cpu2 = [l for l in w2 if 'Use Less CPU when in Background' in l]
    throttle = ini_value(read(ctx['es_path']), '/Script/UnrealEd.EditorPerformanceSettings', 'bThrottleCPUWhenNotForeground')
    rows.add('F8', bool(cpu1) and not cpu2 and throttle == 'False',
             'start-1 warning: %s, start 2 none: %s, engine EditorSettings.ini bThrottleCPUWhenNotForeground=%s after '
             'two starts (1.2.x left False)' % (bool(cpu1), not cpu2, throttle))
    user_settings = s1['dump']['user']
    mcp_lines = ('MCP endpoint available at: http://localhost:27000/mcp' in s1['log'],
                 'SSE server started on port 27001' in s1['log'])
    if not mcp:
        rows.add('F9', None, 'ports 27000/27001 in use on this machine; defaults %s' % user_settings)
    else:
        rows.add('F9', user_settings == {'enable_mcp_server': True, 'allow_mcp_python_execution': False} and
                 (all(mcp_lines) or not mcp_logged),
                 'defaults %s; server log lines (27000, 27001): %s' % (
                     user_settings, mcp_lines if mcp_logged else 'not logged at Min Severity %s' % exp['severity']))
    state['last'] = (project_dir, uproject, exp)
    return project_dir


def check_env_key(ctx, rows):
    """K3: with no saved key, the editor's environment variable is used and named (dummy value)"""
    project_dir, uproject, exp = ctx['state']['last']
    user_dir = secrets_user_path()
    if os.path.isdir(user_dir):
        shutil.rmtree(user_dir)
    dummy = 'test-env-key-0000'
    s3 = run_start(ctx['engine'], uproject, os.path.join(ctx['logs'], 'migration_k3_%s.log' % ctx['label']), False,
                   {'ANTHROPIC_API_KEY': dummy})
    d = (s3['dump'] or {}).get('settings', {})
    keys = (s3['dump'] or {}).get('keys', {})
    src = d.get('anthropic_api_key_source', '')
    rows.add('K3', 'ANTHROPIC_API_KEY' in src and keys.get('anthropic_api_key_ui', {}).get('length') == 0 and
             dummy not in s3['log'] and not os.path.exists(os.path.join(user_dir, 'secrets.json')),
             'no saved key + ANTHROPIC_API_KEY in the editor environment: API Key Source "%s", field empty: %s, '
             'value not in the log: %s, nothing saved: %s' % (
                 src, keys.get('anthropic_api_key_ui', {}).get('length') == 0, dummy not in s3['log'],
                 not os.path.exists(os.path.join(user_dir, 'secrets.json'))))


# Runs in the editor over MCP run-python after the migration checks: picks the live model (Project Settings writes it
# to the scratch project's ini only), tags the focused probe graph and starts a batch translation, which saves to the
# translation output directory (translate-focused-blueprint returns the code without saving it)
LIVE_START_PY = r'''
import unreal
s = unreal.get_default_object(unreal.N2CSettings)
def pick(cls, name):
    want = name.replace("_", "").upper()
    return getattr(cls, [m for m in dir(cls) if not m.startswith("_") and m.replace("_", "").upper() == want][0])
s.set_editor_property("provider", pick(unreal.N2CLLMProvider, "%(provider)s"))
s.set_editor_property("%(model_prop)s", pick(%(enum)s, "%(model_enum)s"))
tag = unreal.N2CTagBlueprintLibrary.tag_focused_blueprint_graph("N2CMigrationLive", "Default", "")
started = unreal.N2CBatchTranslationBlueprintLibrary.translate_graphs_with_tag("N2CMigrationLive", "")
result = {"tagged": str(tag), "started": str(started)}
'''
LIVE_POLL_PY = r'''
import unreal
result = {"running": unreal.N2CBatchTranslationBlueprintLibrary.is_batch_translation_in_progress()}
'''
LIVE_MODELS = {'anthropic': ('Anthropic', 'anthropic_model', 'unreal.N2CAnthropicModel', 'Claude5_5_Haiku',
                             'ANTHROPIC_API_KEY')}


def check_live(ctx, rows, provider):
    """F2 / S26: one real batch translation (key from the repo's .env, in the editor's environment only) lands in the
    migrated CustomTranslationOutputDirectory, and its N2C JSON says version 1.0.0"""
    from editor_session import EditorSession, free_port
    from live_env_translation import SETUP_PY, ASSET_DIR, ASSET_NAME, call_tool, tool_text
    project_dir, uproject, exp = ctx['state']['last']
    store, model_prop, enum, model_enum, variable = LIVE_MODELS[provider]
    keys = load_provider_keys(only=[variable])
    if variable not in keys:
        rows.add('S26', None, '%s not in the repo .env' % variable)
        return
    custom = os.path.join(project_dir, 'CustomTranslations')
    before = set(os.listdir(custom)) if os.path.isdir(custom) else set()
    port = free_port(27180)
    log = os.path.join(ctx['logs'], 'migration_live_%s.log' % ctx['label'])
    steps, poll = {}, ''
    with EditorSession(ctx['engine'], uproject, port, log, allow_python=True, extra_env=keys) as s:
        if s.start():
            name = '%s_%s' % (ASSET_NAME, time.strftime('%H%M%S'))
            steps['setup'] = tool_text(call_tool(port, 'run-python', {'code': SETUP_PY % {'dir': ASSET_DIR, 'name': name}}, 1)[1])[:300]
            steps['start'] = tool_text(call_tool(port, 'run-python', {'code': LIVE_START_PY % {
                'provider': store, 'model_prop': model_prop, 'enum': enum, 'model_enum': model_enum}}, 2)[1])[:300]
            deadline = time.time() + 300
            while time.time() < deadline:
                time.sleep(2)
                poll = tool_text(call_tool(port, 'run-python', {'code': LIVE_POLL_PY}, 3)[1])
                try:
                    if json.loads(poll).get('data', {}).get('running') is False:
                        break
                except ValueError:
                    pass
            steps['poll'] = poll[:200]
        log_text = s.log_text()
    new = sorted(set(os.listdir(custom)) - before) if os.path.isdir(custom) else []
    versions, files = [], 0
    for folder in new:
        for base, _, names in os.walk(os.path.join(custom, folder)):
            for f in names:
                files += 1
                if f.endswith('.json'):
                    try:
                        data = json.load(open(os.path.join(base, f), encoding='utf-8-sig'))
                    except ValueError:
                        continue
                    if isinstance(data, dict) and 'version' in data and 'graphs' in data:
                        versions.append(data['version'])
    leaks = find_leaks(keys, [log] + [os.path.join(custom, d) for d in new])
    rows.add('S26', bool(new) and files > 1 and not leaks,
             'live batch translation (%s %s) wrote %s (%d files) under the migrated CustomTranslationOutputDirectory; '
             'no key in those files or the log: %s; steps %s' % (provider, model_enum, new, files, not leaks, steps))
    rows.add('F2', bool(versions) and set(versions) == {'1.0.0'}, 'new translation\'s N2C JSON version: %s' % versions)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--engine', required=True)
    ap.add_argument('--host', required=True, help='built host project folder (copied, never changed)')
    ap.add_argument('--label', required=True)
    ap.add_argument('--fixtures', default='1.2.3,1.2.x-main')
    ap.add_argument('--keep', action='store_true', help='keep the scratch projects (printed) for inspection')
    ap.add_argument('--live', action='store_true', help='also run one real translation (F2, S26); key from the .env')
    ap.add_argument('--provider', default='anthropic', help='live provider (its latest small model)')
    args = ap.parse_args()

    logs = os.path.join(HERE, 'logs')
    os.makedirs(logs, exist_ok=True)
    version = engine_version(args.engine)
    es_path = os.path.join(os.environ.get('LOCALAPPDATA', ''), 'UnrealEngine', version, 'Saved', 'Config',
                           'WindowsEditor', 'EditorSettings.ini')
    es_backup = read(es_path, True)
    user_dir = secrets_user_path()
    user_backup = None
    if os.path.exists(user_dir):
        user_backup = user_dir + '.migration-check-%d' % int(time.time())
        os.rename(user_dir, user_backup)
        print('per-user secrets folder moved aside to %s for the run' % user_backup)
    root = tempfile.mkdtemp(prefix='n2c_mig_')
    all_rows = []
    ctx = {'engine': args.engine, 'host': args.host, 'root': root, 'logs': logs, 'label': args.label, 'state': {},
           'es_path': es_path}
    try:
        # 1.2.x turned "Use Less CPU when in Background" off on every start (F8)
        text = (es_backup or b'').decode('utf-8-sig', 'replace')
        section = '[/Script/UnrealEd.EditorPerformanceSettings]'
        if section in text:
            head, tail = text.split(section, 1)
            body, rest = (tail.split('\n[', 1) + [''])[:2]
            body = re.sub(r'(?m)^bThrottleCPUWhenNotForeground=.*$', '', body).rstrip('\r\n')
            text = head + section + body + '\r\nbThrottleCPUWhenNotForeground=False\r\n' + ('\n[' + rest if rest else '')
        else:
            text = text.rstrip('\r\n') + '\r\n\r\n' + section + '\r\nbThrottleCPUWhenNotForeground=False\r\n'
        os.makedirs(os.path.dirname(es_path), exist_ok=True)
        open(es_path, 'wb').write(text.encode('utf-8'))
        for fixture in [f.strip() for f in args.fixtures.split(',') if f.strip()]:
            rows = Rows(fixture)
            project_dir = check_fixture(fixture, ctx, rows, ctx['state'])
            all_rows += rows.items
            if args.keep:
                print('kept scratch project %s' % project_dir)
        if ctx['state'].get('last'):
            rows = Rows('env')
            check_env_key(ctx, rows)
            if args.live:
                check_live(ctx, rows, args.provider)
            all_rows += rows.items
    finally:
        if es_backup is not None:
            open(es_path, 'wb').write(es_backup)
        if os.path.isdir(user_dir):
            shutil.rmtree(user_dir)
        if user_backup:
            os.rename(user_backup, user_dir)
        if not args.keep:
            shutil.rmtree(root, ignore_errors=True)
    report = os.path.join(logs, 'migration_%s.json' % args.label)
    json.dump({'engine': args.engine, 'label': args.label, 'rows': all_rows}, open(report, 'w'), indent=1)
    failed = [r for r in all_rows if r['status'] == 'FAIL']
    print('%s: %d PASS, %d FAIL, %d KNOWN, %d SKIP (report %s)' % (
        'FAIL' if failed else 'PASS', sum(r['status'] == 'PASS' for r in all_rows), len(failed),
        sum(r['status'] == 'KNOWN' for r in all_rows), sum(r['status'] == 'SKIP' for r in all_rows),
        os.path.relpath(report, HERE)))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
