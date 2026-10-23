"""Load provider API keys from the repo's local .env for a child process, without ever printing a value.

Stdlib only; run with `python -I`. The editor reads ANTHROPIC_API_KEY, OPENAI_API_KEY, GEMINI_API_KEY and
DEEPSEEK_API_KEY (or DEEPSEEK_API) from its environment when no key is saved (FN2CSecretStore), so a live check can
hand keys to the editor it launches and nowhere else:

    from env_keys import load_provider_keys, find_leaks
    extra = load_provider_keys()                       # {'ANTHROPIC_API_KEY': '...', ...}, never print it
    with EditorSession(..., extra_env=extra) as s: ...
    assert not find_leaks(extra, [s.log_path])         # no key value in the editor log

Run on its own it prints only which variable names were found:

    python -I env_keys.py [--env-file <path>]
"""
import argparse
import json
import os
import sys

# The .env next to the repo root (gitignored)
DEFAULT_ENV_FILE = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '.env'))

# Variable names the plugin reads; .env names on the left that differ from them are renamed
PROVIDER_VARIABLES = ('ANTHROPIC_API_KEY', 'OPENAI_API_KEY', 'GEMINI_API_KEY', 'DEEPSEEK_API_KEY')
ALIASES = {'DEEPSEEK_API': 'DEEPSEEK_API_KEY'}


def parse_env_file(path):
    """KEY=VALUE lines into a dict: '#' comments, blank lines and an 'export ' prefix are allowed, and one pair of
    matching quotes around a value is removed. Values are returned, never printed."""
    values = {}
    with open(path, encoding='utf-8-sig') as f:
        for line in f:
            s = line.strip()
            if not s or s.startswith('#') or '=' not in s:
                continue
            if s.startswith('export '):
                s = s[len('export '):].lstrip()
            name, value = s.split('=', 1)
            name, value = name.strip(), value.strip()
            if len(value) >= 2 and value[0] == value[-1] and value[0] in '"\'':
                value = value[1:-1]
            if name:
                values[name] = value
    return values


def load_provider_keys(env_file=DEFAULT_ENV_FILE, only=None):
    """The provider keys in env_file under the names the plugin reads (DEEPSEEK_API becomes DEEPSEEK_API_KEY), for
    merging into a child process environment. `only` limits it to some of PROVIDER_VARIABLES. A missing file gives {}."""
    if not os.path.isfile(env_file):
        return {}
    wanted = set(only) if only else set(PROVIDER_VARIABLES)
    out = {}
    for name, value in parse_env_file(env_file).items():
        target = ALIASES.get(name, name)
        if target in wanted and value and (target not in out or name == target):
            out[target] = value
    return out


def find_leaks(keys, paths):
    """The paths (files, or folders searched recursively) whose bytes contain any key value, as
    [(path, variable name)]. Checks UTF-8 and UTF-16LE spellings, since editor logs can be either."""
    needles = []
    for name, value in keys.items():
        if len(value) >= 8:
            needles.append((name, value.encode('utf-8')))
            needles.append((name, value.encode('utf-16-le')))
    files = []
    for p in paths:
        if os.path.isdir(p):
            for root, _, names in os.walk(p):
                files += [os.path.join(root, n) for n in names]
        elif os.path.isfile(p):
            files.append(p)
    leaks = []
    for path in files:
        try:
            data = open(path, 'rb').read()
        except OSError:
            continue
        for name in sorted({n for n, b in needles if b in data}):
            leaks.append((path, name))
    return leaks


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--env-file', default=DEFAULT_ENV_FILE)
    ap.add_argument('--check', nargs='*', default=None, metavar='PATH',
                    help='report files or folders that contain a key value (names only)')
    args = ap.parse_args()
    keys = load_provider_keys(args.env_file)
    report = {'env_file': args.env_file, 'variables': sorted(keys)}
    if args.check is not None:
        report['leaks'] = [{'path': p, 'variable': n} for p, n in find_leaks(keys, args.check)]
    print(json.dumps(report, indent=1))
    return 1 if report.get('leaks') else 0


if __name__ == '__main__':
    sys.exit(main())
