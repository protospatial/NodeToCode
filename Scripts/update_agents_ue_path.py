"""
Write the Unreal Engine source path for AI coding agents into AGENTS.local.md

Finds the newest Unreal Engine install (Content/Python/mcp_bridge/ue_python_finder.py: registry, LauncherInstalled.dat
and common install folders) and writes its Engine/Source folder as the UE_SOURCE_PATH line of AGENTS.local.md at the
repository root. That file is gitignored and machine-specific; the tracked AGENTS.md is never changed (card 0195).
Run through Scripts/detect-ue-path.bat or Scripts/detect-ue-path.sh, or directly with any Python 3.
"""

import argparse
import os
import re
import sys

script_dir = os.path.dirname(os.path.abspath(__file__))
repo_root = os.path.dirname(script_dir)
sys.path.insert(0, os.path.join(repo_root, 'Content', 'Python', 'mcp_bridge'))

from ue_python_finder import find_ue_source  # noqa: E402

LOCAL_FILE = 'AGENTS.local.md'
HEADER = (
    '# AGENTS.local.md\n'
    '\n'
    'Machine-specific notes for AI coding agents working in this checkout. This file is gitignored: never commit it.\n'
    'Scripts/detect-ue-path.bat (Windows) or Scripts/detect-ue-path.sh (macOS, Linux) writes the line below.\n'
    '\n'
    'UE_SOURCE_PATH is where the Unreal Engine sources can be read for research. Never modify files there.\n'
    '\n'
)


def update_local_file(source_path: str, local_path: str) -> str:
    """Writes UE_SOURCE_PATH: <source_path> into local_path: replaces the line when the file has one, appends it
    otherwise, and creates the file with a short header when it does not exist. Returns the new text."""
    line = f'UE_SOURCE_PATH: {source_path}'
    if os.path.exists(local_path):
        with open(local_path, 'r', encoding='utf-8') as f:
            content = f.read()
        new_content, count = re.subn(r'^UE_SOURCE_PATH:.*$', lambda _: line, content, flags=re.MULTILINE)
        if count == 0:
            new_content = content + ('' if content.endswith('\n') or not content else '\n') + line + '\n'
    else:
        new_content = HEADER + line + '\n'
    with open(local_path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(new_content)
    return new_content


def main() -> int:
    parser = argparse.ArgumentParser(description='Write the UE source path to AGENTS.local.md (gitignored)')
    parser.add_argument('--debug', action='store_true', help='Enable debug output')
    parser.add_argument('--output', type=str, default=os.path.join(repo_root, LOCAL_FILE),
                        help=f'The file to write (default: {LOCAL_FILE} at the repository root)')
    parser.add_argument('--source', type=str, default=None,
                        help='Use this Engine/Source folder instead of detecting one')
    args = parser.parse_args()

    source_path = args.source or find_ue_source(prefer_newest=True, debug=args.debug)
    if not source_path:
        print('Error: could not find an Unreal Engine installation', file=sys.stderr)
        print(f'Install Unreal Engine, or pass --source <UE>/Engine/Source to write {LOCAL_FILE}', file=sys.stderr)
        return 1

    update_local_file(source_path, args.output)
    print(f'Wrote UE_SOURCE_PATH: {source_path} to {args.output}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
