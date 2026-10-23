"""Headless check that the struct utilities fail with a message instead of a fatal error (card 0048).

Run inside the editor:
    UnrealEditor-Cmd.exe <host>.uproject -ExecutePythonScript=<this file> -unattended -nullrhi -nosplash -nosound
Prints E05_STRUCT PASS or E05_STRUCT FAIL lines and removes everything it created.
"""
import os
import stat
import unreal

lib = unreal.N2CStructUtilsLibrary
results = []


def check(label, ok, detail=''):
    results.append(ok)
    unreal.log_warning('E05_STRUCT %s %s %s' % ('PASS' if ok else 'FAIL', label, detail))


def create(path, name):
    out = lib.create_user_defined_struct(path, name)
    # Python returns (struct, success, error) for the out parameters
    if isinstance(out, tuple):
        struct = out[0]
        rest = out[1:]
        success = next((x for x in rest if isinstance(x, bool)), struct is not None)
        error = next((x for x in rest if isinstance(x, str)), '')
        return struct, success, error
    return out, out is not None, ''


s, ok, err = create('Game/Data', 'F_Bad')
check('invalid path refused', not ok and not s, err)
s, ok, err = create('/NoSuchMount/Data', 'F_Bad')
check('unmounted path refused', not ok and not s, err)

s, ok, err = create('/Game/E05Probe', 'F_Row')
check('valid struct created', bool(ok and s), err)
if s:
    check('first save succeeds', lib.save_struct(s))
    filename = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir()), 'E05Probe', 'F_Row.uasset')
    if os.path.exists(filename):
        os.chmod(filename, stat.S_IREAD)
        check('read-only save returns False', lib.save_struct(s) is False)
        os.chmod(filename, stat.S_IREAD | stat.S_IWRITE)
    else:
        check('saved file exists', False, filename)
    unreal.EditorAssetLibrary.delete_directory('/Game/E05Probe')

unreal.log_warning('E05_STRUCT %s' % ('PASS' if results and all(results) else 'FAIL'))
