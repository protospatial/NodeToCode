"""Headless check that the DataTable populator merges, reports failures and saves only when asked (card 0065).

Run inside the editor:
    UnrealEditor-Cmd.exe <host>.uproject -ExecutePythonScript=<this file> -unattended -nullrhi -nosplash -nosound
Prints E05_DATATABLE PASS or E05_DATATABLE FAIL lines and removes everything it created (/Game/E05Probe).
"""
import hashlib
import json
import os
import unreal

from scripts.data import datatable_populator as dtp

FOLDER = '/Game/E05Probe'
NAME = 'DT_E05Probe'
PATH = FOLDER + '/' + NAME
results = []


def check(label, ok, detail=''):
    results.append(bool(ok))
    unreal.log_warning('E05_DATATABLE %s %s %s' % ('PASS' if ok else 'FAIL', label, detail))


def file_hash():
    filename = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir()),
                            'E05Probe', NAME + '.uasset')
    if not os.path.exists(filename):
        return None
    with open(filename, 'rb') as f:
        return hashlib.sha256(f.read()).hexdigest()


def rows():
    dt = unreal.EditorAssetLibrary.load_asset(PATH)
    return {r['Name']: r for r in json.loads(dt.export_to_json_string())}


if unreal.EditorAssetLibrary.does_directory_exist(FOLDER):
    unreal.EditorAssetLibrary.delete_directory(FOLDER)

try:
    factory = unreal.DataTableFactory()
    factory.set_editor_property('struct', unreal.GameplayTagTableRow.static_struct())
    dt = unreal.AssetToolsHelpers.get_asset_tools().create_asset(NAME, FOLDER, unreal.DataTable, factory)
    check('table created', dt is not None)

    seed = [{'Name': 'Row%d' % i, 'Tag': 'Probe.Seed%d' % i, 'DevComment': 'seed %d' % i} for i in (1, 2, 3)]
    check('seed fill', dt.fill_from_json_string(json.dumps(seed)))
    check('seed saved', unreal.EditorAssetLibrary.save_asset(PATH, only_if_is_dirty=False))
    original = rows()
    on_disk = file_hash()
    check('seed has 3 rows', len(original) == 3, str(sorted(original)))

    # Append two rows; one uses the documented RowName alias
    add = json.dumps([{'Name': 'Row4', 'Tag': 'Probe.Four', 'DevComment': 'four'},
                      {'RowName': 'Row5', 'Tag': 'Probe.Five', 'DevComment': 'five'}])
    res = dtp.populate_from_json(PATH, add)
    now = rows()
    check('append succeeds', res['success'], str(res.get('error')))
    check('append gives 5 rows', len(now) == 5, str(sorted(now)))
    check('RowName alias kept the row', 'Row5' in now)
    check('original 3 rows unchanged', all(now.get(k) == v for k, v in original.items()))
    check('append reports added=2', (res.get('data') or {}).get('rows_added') == 2, str(res.get('data')))
    check('append not saved by default', (res.get('data') or {}).get('saved') is False and file_hash() == on_disk)

    # Invalid JSON fails and changes nothing
    res = dtp.populate_from_json(PATH, '[{"Name": "Bad", ')
    check('invalid JSON returns success False', res['success'] is False, str(res.get('error')))
    check('invalid JSON leaves 5 rows', len(rows()) == 5)
    check('invalid JSON not saved', file_hash() == on_disk)

    # A row without a name fails before touching the table
    res = dtp.populate_from_json(PATH, json.dumps([{'Tag': 'Probe.NoName'}]))
    check('nameless row returns success False', res['success'] is False, str(res.get('error')))
    check('nameless row leaves 5 rows', len(rows()) == 5)

    # Updating an existing row overlays only the given fields
    res = dtp.populate_from_json(PATH, json.dumps([{'Name': 'Row1', 'DevComment': 'changed'}]))
    now = rows()
    check('update succeeds', res['success'] and (res.get('data') or {}).get('rows_updated') == 1, str(res))
    check('update keeps other fields', now['Row1']['DevComment'] == 'changed'
          and now['Row1']['Tag'] == original['Row1']['Tag'] and len(now) == 5, str(now.get('Row1')))

    # CSV merges too
    res = dtp.populate_from_csv(PATH, 'Name,Tag,DevComment\nRow6,Probe.Six,six\n')
    now = rows()
    check('csv append succeeds', res['success'], str(res.get('error')))
    check('csv append gives 6 rows', len(now) == 6 and now.get('Row6', {}).get('Tag') == 'Probe.Six', str(sorted(now)))
    check('csv append keeps rows', now['Row2'] == original['Row2'])

    # A fill that reports failure, or yields the wrong rows, is an error and the old rows come back.
    # The engine import almost never returns False, so drive the post-fill check directly.
    before = rows()
    backup = unreal.EditorAssetLibrary.load_asset(PATH).export_to_json_string()
    res = dtp._finish_fill(PATH, False, list(before), backup, True, {})
    check('fill False returns success False', res['success'] is False, str(res.get('error')))
    check('fill False restores rows', rows() == before and file_hash() == on_disk)
    unreal.EditorAssetLibrary.load_asset(PATH).fill_from_json_string(json.dumps([{'Name': 'Ghost', 'Tag': 'Probe.Ghost'}]))
    res = dtp._finish_fill(PATH, True, ['Ghost', 'Missing'], backup, True, {})
    check('wrong rows returns success False', res['success'] is False, str(res.get('error')))
    check('wrong rows restores rows, not saved', rows() == before and file_hash() == on_disk)

    # clear_existing replaces; save=True writes the file
    res = dtp.populate_from_json(PATH, json.dumps([{'Name': 'Only', 'Tag': 'Probe.Only'}]), clear_existing=True, save=True)
    now = rows()
    check('replace succeeds', res['success'], str(res.get('error')))
    check('replace gives 1 row', list(now) == ['Only'], str(sorted(now)))
    check('save=True writes the asset', (res.get('data') or {}).get('saved') is True and file_hash() != on_disk)
except Exception as e:
    check('no exception', False, repr(e))
finally:
    unreal.EditorAssetLibrary.delete_directory(FOLDER)

unreal.log_warning('E05_DATATABLE %s' % ('PASS' if results and all(results) else 'FAIL'))
