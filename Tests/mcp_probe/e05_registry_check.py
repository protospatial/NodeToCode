"""Headless check that the Python script registry is written atomically and never written over after a parse failure
(card 0067).

Run inside the editor:
    UnrealEditor-Cmd.exe <host>.uproject -ExecutePythonScript=<this file> -unattended -nullrhi -nosplash -nosound
Prints E05_REGISTRY PASS or E05_REGISTRY FAIL lines. It points the module's scripts folders at a temporary folder
under Saved/Automation (never the project's Content/Python/scripts) and removes that folder at the end.
"""
import glob
import json
import os
import shutil
import uuid

import unreal

from nodetocode import scripts as s

results = []


def check(label, ok, detail=''):
    results.append(bool(ok))
    unreal.log_warning('E05_REGISTRY %s %s %s' % ('PASS' if ok else 'FAIL', label, detail))


def read_bytes(path):
    if not os.path.exists(path):
        return None
    with open(path, 'rb') as f:
        return f.read()


saved = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())
base = os.path.join(saved, 'Automation', 'N2CRegistry_%s' % uuid.uuid4().hex)
project_dir = os.path.join(base, 'project')
plugin_dir = os.path.join(base, 'plugin')
os.makedirs(plugin_dir)
registry = os.path.join(project_dir, 'script_registry.json')

original_project, original_plugin = s._get_project_scripts_dir, s._get_plugin_scripts_dir
s._get_project_scripts_dir = lambda: project_dir
s._get_plugin_scripts_dir = lambda: plugin_dir

try:
    # Normal saves go through a temporary file, keep a .bak and keep unknown top-level fields
    r1 = s.save_script('probe_one', 'result = 1', 'first probe script', tags=['probe'])
    check('first save succeeds', r1['success'], str(r1.get('error')))
    with open(registry, 'r', encoding='utf-8') as f:
        data = json.load(f)
    data['future_field'] = {'kept': True}
    with open(registry, 'w', encoding='utf-8') as f:
        json.dump(data, f, indent=2)
    before_second = read_bytes(registry)
    r2 = s.save_script('probe_two', 'result = 2', 'second probe script')
    check('second save succeeds', r2['success'], str(r2.get('error')))
    with open(registry, 'r', encoding='utf-8') as f:
        data = json.load(f)
    check('both entries saved', sorted(data.get('scripts', {})) == ['probe_one', 'probe_two'], str(sorted(data.get('scripts', {}))))
    check('unknown top-level field kept', data.get('future_field') == {'kept': True}, str(data.get('future_field')))
    check('previous file kept as .bak', read_bytes(registry + '.bak') == before_second)
    check('no temporary file left', not os.path.exists(registry + '.tmp'))

    # A truncated registry: every call returns an error and the file is never written over
    good = read_bytes(registry)
    truncated = good[:len(good) // 2]
    with open(registry, 'wb') as f:
        f.write(truncated)

    r3 = s.save_script('probe_three', 'result = 3', 'third probe script')
    check('save on a corrupt registry returns an error', r3['success'] is False and 'does not parse' in str(r3.get('error')),
          str(r3.get('error'))[:200])
    check('corrupt registry unchanged after save', read_bytes(registry) == truncated)
    check('no script file written for the refused save',
          not os.path.exists(os.path.join(project_dir, 'general', 'probe_three.py')))
    copies = glob.glob(registry + '.corrupt-*')
    check('one .corrupt copy holds the original bytes', len(copies) == 1 and read_bytes(copies[0]) == truncated, str(copies))

    r4 = s.delete_script('probe_one')
    check('delete on a corrupt registry returns an error', r4['success'] is False, str(r4.get('error'))[:200])
    check('delete left the script file', os.path.exists(os.path.join(project_dir, 'general', 'probe_one.py')))
    check('corrupt registry unchanged after delete', read_bytes(registry) == truncated)

    r5 = s.list_scripts()
    check('list on a corrupt registry returns an error', r5['success'] is False, str(r5.get('error'))[:200])
    s._update_usage_stats('probe_one')
    check('usage-stat update did not write', read_bytes(registry) == truncated)
    check('repeated failures keep one .corrupt copy', len(glob.glob(registry + '.corrupt-*')) == 1)

    # Valid JSON that is not a registry is refused the same way
    with open(registry, 'w', encoding='utf-8') as f:
        f.write('[1, 2, 3]')
    shaped = read_bytes(registry)
    r6 = s.save_script('probe_four', 'result = 4', 'fourth probe script')
    check('save on a non-registry JSON returns an error', r6['success'] is False, str(r6.get('error'))[:200])
    check('non-registry JSON unchanged', read_bytes(registry) == shaped)

    # A save that stopped after the temporary file was complete is finished on the next load
    os.remove(registry)
    with open(registry + '.tmp', 'wb') as f:
        f.write(good)
    r7 = s.list_scripts()
    check('interrupted save finished on load', r7['success'] and read_bytes(registry) == good
          and not os.path.exists(registry + '.tmp'), str(r7.get('error')))
    check('entries back after the interrupted save', r7['success'] and r7['data']['total'] == 2, str(r7.get('data')))

    # Removing the bad file starts a new registry
    os.remove(registry)
    r8 = s.save_script('probe_five', 'result = 5', 'fifth probe script')
    check('save after removing the registry succeeds', r8['success'], str(r8.get('error')))
except Exception as e:
    check('no exception', False, repr(e))
finally:
    s._get_project_scripts_dir, s._get_plugin_scripts_dir = original_project, original_plugin
    shutil.rmtree(base, ignore_errors=True)

unreal.log_warning('E05_REGISTRY %s' % ('PASS' if results and all(results) else 'FAIL'))
