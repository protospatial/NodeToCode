"""place-cpp-translation end to end (card 0314): translate, place into the host's module, and compile it.

Stdlib only; run with `python -I` (set N2C_PORT_OFFSET first when other agents run probes):

    python -I live_place_cpp_0314.py --engine G:/UE/UE_5.8 \
        --project "D:/n2c-lanes/lane-b/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" \
        --provider anthropic --model claude-haiku-5-5 [--compile build.ps1]

One headless editor with only the provider's key (from the repo's .env) in its environment and Python execution on:
  1. the in-memory probe Blueprint (probe_blueprint.py: BeginPlay -> Print String) is translated to C++ with
     translate-focused-blueprint, which saves an MCP run;
  2. place-cpp-translation in a dry run (module = the host project's module, class N2CPlacedProbe): the plan names
     AActor as the parent (the probe's native parent), includes Kismet/KismetSystemLibrary.h, lists the header and .cpp
     under the module's Source folder, and writes nothing;
  3. module "Engine" and run_folder "../../Config" are refused;
  4. dry_run false writes both files.
Then, with --compile <path to build.ps1>, the host is built (`build.ps1 -UEVersion <ver>`): the placed class must
compile. The written files (and any .bak) are deleted at the end and the host's Build.cs is put back byte for byte.
No key value is printed or written. Report: logs/live_place_cpp_0314.json.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from editor_session import BLUEPRINT_EDITOR_ARGS, EditorSession, free_port  # noqa: E402
from env_keys import find_leaks, load_provider_keys  # noqa: E402
from live_env_translation import ASSET_DIR, PROVIDER_VARIABLE, SETUP_PY, call_tool, run_tool_call, tool_text  # noqa: E402

CLASS = 'N2CPlacedProbe'


def result_json(response):
    try:
        return json.loads(tool_text(response))
    except (ValueError, TypeError):
        return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--provider', default='anthropic', choices=sorted(PROVIDER_VARIABLE))
    ap.add_argument('--model', default='claude-haiku-5-5')
    ap.add_argument('--port', type=int, default=27290)
    ap.add_argument('--compile', default=None, help='build.ps1 to compile the host with the placed class')
    ap.add_argument('--log-dir', default=os.path.join(HERE, 'logs'))
    args = ap.parse_args()
    os.makedirs(args.log_dir, exist_ok=True)

    variable = PROVIDER_VARIABLE[args.provider]
    keys = load_provider_keys(only=[variable])
    if variable not in keys:
        print(json.dumps({'ok': False, 'reason': '%s not in .env' % variable}))
        return 2

    module = os.path.splitext(os.path.basename(args.project))[0]
    module_dir = os.path.join(os.path.dirname(args.project), 'Source', module)
    build_cs = os.path.join(module_dir, module + '.Build.cs')
    build_cs_before = open(build_cs, 'rb').read()
    port = free_port(args.port)
    log = os.path.join(args.log_dir, 'live_place_cpp_0314_editor.log')
    r = {'module': module, 'port': port}
    written = []
    try:
        with EditorSession(args.engine, args.project, port, log, allow_python=True, extra_env=keys,
                           extra_args=BLUEPRINT_EDITOR_ARGS) as s:
            r['started'] = s.start()
            if r['started']:
                name = 'BP_N2CPlaceProbe_%s' % time.strftime('%H%M%S')
                _, setup = call_tool(port, 'run-python', {'code': SETUP_PY % {'dir': ASSET_DIR, 'name': name}}, 1)
                r['setup'] = tool_text(setup)[:300]
                events = run_tool_call(port, 'translate-focused-blueprint',
                                       {'provider': args.provider, 'model': args.model, 'language': 'cpp'}, 2)
                responses = [json.loads(d) for e, d in events if e == 'response']
                r['translated'] = bool(responses) and not (responses[-1].get('result') or {}).get('isError')

                _, dry = call_tool(port, 'place-cpp-translation', {'module': module, 'class_name': CLASS}, 3)
                plan = result_json(dry) or {}
                header = (plan.get('header') or {})
                source = (plan.get('source') or {})
                r['dry_run'] = {
                    'parent': plan.get('parentClass'), 'class': plan.get('className'),
                    'includes': plan.get('includes'), 'required': plan.get('requiredModules'),
                    'missing': plan.get('missingModules'), 'warnings': plan.get('warnings'),
                    'header_path': header.get('path'), 'source_path': source.get('path'),
                    'wrote_nothing': not os.path.exists(header.get('path') or 'x') and not os.path.exists(source.get('path') or 'x'),
                }
                r['header_has_class'] = 'class %s_API A%s : public AActor' % (module.upper(), CLASS) in (header.get('content') or '')
                r['source_has_include'] = 'Kismet/KismetSystemLibrary.h' in (source.get('content') or '')

                _, engine = call_tool(port, 'place-cpp-translation', {'module': 'Engine', 'class_name': CLASS}, 4)
                r['engine_refused'] = bool((engine.get('result') or {}).get('isError'))
                _, escape = call_tool(port, 'place-cpp-translation',
                                      {'module': module, 'class_name': CLASS, 'run_folder': '../../Config'}, 5)
                r['escape_refused'] = bool((escape.get('result') or {}).get('isError'))

                _, wet = call_tool(port, 'place-cpp-translation', {'module': module, 'class_name': CLASS, 'dry_run': False}, 6)
                applied = result_json(wet) or {}
                written = applied.get('written') or []
                r['written'] = [os.path.relpath(p, module_dir) for p in written]
                r['all_inside_module'] = bool(written) and all(
                    os.path.normcase(os.path.abspath(p)).startswith(os.path.normcase(os.path.abspath(module_dir)) + os.sep) for p in written)
        r['restore_ok'] = s.restore_ok
        r['key_leaked'] = bool(find_leaks(keys, [log]))

        if args.compile and written:
            ver = re.search(r'UE_(\d+\.\d+)', args.engine.replace('\\', '/'))
            proc = subprocess.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', args.compile, '-UEVersion', ver.group(1) if ver else '5.8'],
                                  capture_output=True, text=True, encoding='utf-8', errors='replace')
            out = proc.stdout + proc.stderr
            r['compile'] = {'exit': proc.returncode, 'summary': out[out.rfind('Summary'):][:300]}
            r['compile']['saw_class'] = CLASS in out or ('Module.' + module) in out
            r['compiled_placed_class'] = proc.returncode == 0 and 'OK' in r['compile']['summary']
    finally:
        for path in list(written):
            for p in (path, path + '.bak', path + '.tmp'):
                if os.path.exists(p):
                    os.remove(p)
        if open(build_cs, 'rb').read() != build_cs_before:
            open(build_cs, 'wb').write(build_cs_before)
        for p in (build_cs + '.bak',):
            if os.path.exists(p):
                os.remove(p)
        r['cleaned'] = not any(os.path.exists(p) for p in written) and open(build_cs, 'rb').read() == build_cs_before

    d = r.get('dry_run') or {}
    r['pass'] = bool(r.get('started') and r.get('translated') and d.get('parent') == 'AActor' and d.get('wrote_nothing')
                     and r.get('header_has_class') and r.get('source_has_include') and r.get('engine_refused')
                     and r.get('escape_refused') and r.get('all_inside_module') and not r.get('key_leaked')
                     and r.get('cleaned') and r.get('restore_ok')
                     and (not args.compile or r.get('compiled_placed_class')))
    with open(os.path.join(args.log_dir, 'live_place_cpp_0314.json'), 'w', encoding='utf-8') as f:
        json.dump(r, f, indent=2)
    print(json.dumps(r, indent=2))
    print('PASS' if r['pass'] else 'FAIL')
    return 0 if r['pass'] else 1


if __name__ == '__main__':
    sys.exit(main())
