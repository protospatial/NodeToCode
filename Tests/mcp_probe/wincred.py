"""Windows Credential Manager from the live checks (card 0161), with ctypes: list, snapshot, restore, read and delete
generic credentials. Stdlib only; Windows only (every function raises OSError elsewhere).

Values are returned as bytes for comparisons and never printed. The live checks snapshot every entry under the
plugin's prefix before they run and put the snapshot back afterwards, in memory only:

    from wincred import snapshot, restore_snapshot, targets
    saved = snapshot('NodeToCode/')          # {target: entry}; keep it in memory, never write it out
    try: ...
    finally: restore_snapshot('NodeToCode/', saved)

Run on its own it prints only the target names under a prefix (default NodeToCode/):

    python -I wincred.py [--prefix NodeToCode/]
"""
import argparse
import ctypes
import json
import os
import sys

CRED_TYPE_GENERIC = 1
CRED_PERSIST_LOCAL_MACHINE = 2
ERROR_NOT_FOUND = 1168
PREFIX = 'NodeToCode/'

if os.name == 'nt':
    from ctypes import wintypes

    class CREDENTIALW(ctypes.Structure):
        _fields_ = [('Flags', wintypes.DWORD), ('Type', wintypes.DWORD), ('TargetName', wintypes.LPWSTR),
                    ('Comment', wintypes.LPWSTR), ('LastWritten', wintypes.FILETIME),
                    ('CredentialBlobSize', wintypes.DWORD), ('CredentialBlob', ctypes.POINTER(ctypes.c_ubyte)),
                    ('Persist', wintypes.DWORD), ('AttributeCount', wintypes.DWORD), ('Attributes', ctypes.c_void_p),
                    ('TargetAlias', wintypes.LPWSTR), ('UserName', wintypes.LPWSTR)]

    PCREDENTIALW = ctypes.POINTER(CREDENTIALW)
    _advapi = ctypes.WinDLL('advapi32', use_last_error=True)
    _advapi.CredEnumerateW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD),
                                       ctypes.POINTER(ctypes.POINTER(PCREDENTIALW))]
    _advapi.CredEnumerateW.restype = wintypes.BOOL
    _advapi.CredReadW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.POINTER(PCREDENTIALW)]
    _advapi.CredReadW.restype = wintypes.BOOL
    _advapi.CredWriteW.argtypes = [PCREDENTIALW, wintypes.DWORD]
    _advapi.CredWriteW.restype = wintypes.BOOL
    _advapi.CredDeleteW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD]
    _advapi.CredDeleteW.restype = wintypes.BOOL
    _advapi.CredFree.argtypes = [ctypes.c_void_p]
    _advapi.CredFree.restype = None


def _require_windows():
    if os.name != 'nt':
        raise OSError('Windows Credential Manager exists only on Windows')


def _entry(cred):
    blob = bytes(cred.CredentialBlob[:cred.CredentialBlobSize]) if cred.CredentialBlobSize else b''
    return {'blob': blob, 'persist': cred.Persist, 'user': cred.UserName or '', 'comment': cred.Comment or ''}


def snapshot(prefix=PREFIX):
    """Every generic credential whose target starts with prefix: {target: {'blob', 'persist', 'user', 'comment'}}"""
    _require_windows()
    count = wintypes.DWORD(0)
    creds = ctypes.POINTER(PCREDENTIALW)()
    if not _advapi.CredEnumerateW(prefix + '*', 0, ctypes.byref(count), ctypes.byref(creds)):
        error = ctypes.get_last_error()
        if error == ERROR_NOT_FOUND:
            return {}
        raise OSError('CredEnumerateW failed: %d' % error)
    try:
        out = {}
        for i in range(count.value):
            cred = creds[i].contents
            if cred.Type == CRED_TYPE_GENERIC and cred.TargetName and cred.TargetName.lower().startswith(prefix.lower()):
                out[cred.TargetName] = _entry(cred)
        return out
    finally:
        _advapi.CredFree(creds)


def targets(prefix=PREFIX):
    """The target names under prefix, sorted (no values)"""
    return sorted(snapshot(prefix))


def read(target):
    """The entry for target, or None"""
    _require_windows()
    cred = PCREDENTIALW()
    if not _advapi.CredReadW(target, CRED_TYPE_GENERIC, 0, ctypes.byref(cred)):
        return None
    try:
        return _entry(cred.contents)
    finally:
        _advapi.CredFree(cred)


def write(target, blob, user='NodeToCode', comment='', persist=CRED_PERSIST_LOCAL_MACHINE):
    """Writes a generic credential (blob: bytes)"""
    _require_windows()
    buffer = (ctypes.c_ubyte * max(len(blob), 1)).from_buffer_copy(blob or b'\0')
    cred = CREDENTIALW()
    cred.Type = CRED_TYPE_GENERIC
    cred.TargetName = target
    cred.Comment = comment or None
    cred.UserName = user or None
    cred.CredentialBlobSize = len(blob)
    cred.CredentialBlob = ctypes.cast(buffer, ctypes.POINTER(ctypes.c_ubyte))
    cred.Persist = persist
    if not _advapi.CredWriteW(ctypes.byref(cred), 0):
        raise OSError('CredWriteW failed for %s: %d' % (target, ctypes.get_last_error()))


def delete(target):
    """Deletes target; True if it existed"""
    _require_windows()
    if _advapi.CredDeleteW(target, CRED_TYPE_GENERIC, 0):
        return True
    error = ctypes.get_last_error()
    if error == ERROR_NOT_FOUND:
        return False
    raise OSError('CredDeleteW failed for %s: %d' % (target, error))


def clear(prefix=PREFIX):
    """Deletes every entry under prefix; returns their target names"""
    removed = targets(prefix)
    for target in removed:
        delete(target)
    return removed


def restore_snapshot(prefix, saved):
    """Puts the entries under prefix back to saved (from snapshot): deletes the others, rewrites saved ones that
    changed. Returns True when the entries now equal saved."""
    for target in targets(prefix):
        if target not in saved:
            delete(target)
    for target, entry in saved.items():
        if read(target) != entry:
            write(target, entry['blob'], entry['user'], entry['comment'], entry['persist'])
    return snapshot(prefix) == saved


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--prefix', default=PREFIX)
    args = ap.parse_args()
    print(json.dumps(targets(args.prefix), indent=1))
    return 0


if __name__ == '__main__':
    sys.exit(main())
