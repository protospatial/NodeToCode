r"""
UE Python Finder

Finds Unreal Engine's bundled Python installation using the same methods
as UnrealVersionSelector:
1. Windows Registry (HKEY_CURRENT_USER\SOFTWARE\Epic Games\Unreal Engine\Builds)
2. Epic Games Launcher's LauncherInstalled.dat file
3. Common installation paths as fallback (every UE_<version> folder, no fixed version list)

Installs are ordered by the version in their Engine/Build/Build.version, so 5.6, 5.7, 5.8 and later are all found.

This module uses only Python standard library.
"""

import os
import sys
import json
import re
from typing import Optional, List, Tuple

# Platform-specific imports
if sys.platform == 'win32':
    import winreg


def log_debug(message: str, debug: bool = False) -> None:
    """Log debug message to stderr."""
    if debug:
        print(f"[UE-Python-Finder] {message}", file=sys.stderr, flush=True)


def get_ue_installs_from_registry(debug: bool = False) -> List[Tuple[str, str]]:
    """
    Get UE installations from Windows Registry.
    Returns list of (version_name, install_path) tuples.
    """
    installations = []

    if sys.platform != 'win32':
        return installations

    # Check HKEY_CURRENT_USER\SOFTWARE\Epic Games\Unreal Engine\Builds
    # This is where custom/source builds are registered
    try:
        key_path = r"SOFTWARE\Epic Games\Unreal Engine\Builds"
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, key_path) as key:
            i = 0
            while True:
                try:
                    name, value, _ = winreg.EnumValue(key, i)
                    if os.path.isdir(value):
                        log_debug(f"Registry build found: {name} -> {value}", debug)
                        installations.append((name, value))
                    i += 1
                except OSError:
                    break
    except FileNotFoundError:
        log_debug("Registry key for custom builds not found", debug)
    except Exception as e:
        log_debug(f"Error reading registry builds: {e}", debug)

    # Check HKEY_LOCAL_MACHINE\SOFTWARE\EpicGames\Unreal Engine
    # This is where launcher-installed versions are registered
    try:
        key_path = r"SOFTWARE\EpicGames\Unreal Engine"
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, key_path) as key:
            i = 0
            while True:
                try:
                    subkey_name = winreg.EnumKey(key, i)
                    try:
                        with winreg.OpenKey(key, subkey_name) as subkey:
                            install_dir, _ = winreg.QueryValueEx(subkey, "InstalledDirectory")
                            if os.path.isdir(install_dir):
                                log_debug(f"Registry installed found: {subkey_name} -> {install_dir}", debug)
                                installations.append((subkey_name, install_dir))
                    except (FileNotFoundError, OSError):
                        pass
                    i += 1
                except OSError:
                    break
    except FileNotFoundError:
        log_debug("Registry key for installed versions not found", debug)
    except Exception as e:
        log_debug(f"Error reading registry installations: {e}", debug)

    return installations


def get_ue_installs_from_launcher(debug: bool = False) -> List[Tuple[str, str]]:
    """
    Get UE installations from Epic Games Launcher's LauncherInstalled.dat file.
    Returns list of (version_name, install_path) tuples.
    """
    installations = []

    # LauncherInstalled.dat location
    if sys.platform == 'win32':
        launcher_file = os.path.join(
            os.environ.get('PROGRAMDATA', 'C:\\ProgramData'),
            'Epic', 'UnrealEngineLauncher', 'LauncherInstalled.dat'
        )
    elif sys.platform == 'darwin':
        launcher_file = os.path.expanduser(
            '~/Library/Application Support/Epic/UnrealEngineLauncher/LauncherInstalled.dat'
        )
    else:
        return installations

    if not os.path.exists(launcher_file):
        log_debug(f"Launcher file not found: {launcher_file}", debug)
        return installations

    try:
        with open(launcher_file, 'r', encoding='utf-8') as f:
            data = json.load(f)

        for item in data.get('InstallationList', []):
            install_path = item.get('InstallLocation', '')
            app_name = item.get('AppName', '')

            # UE installations have AppName like "UE_5.4"
            if install_path and os.path.isdir(install_path):
                # Try to determine version from path or app name
                version = app_name if app_name else os.path.basename(install_path)
                log_debug(f"Launcher found: {version} -> {install_path}", debug)
                installations.append((version, install_path))

    except json.JSONDecodeError as e:
        log_debug(f"Error parsing launcher file: {e}", debug)
    except Exception as e:
        log_debug(f"Error reading launcher file: {e}", debug)

    return installations


def get_python_path_for_ue_install(install_path: str, debug: bool = False) -> Optional[str]:
    """
    Get the Python executable path for a given UE installation.
    """
    if sys.platform == 'win32':
        python_path = os.path.join(
            install_path, 'Engine', 'Binaries', 'ThirdParty', 'Python3', 'Win64', 'python.exe'
        )
    elif sys.platform == 'darwin':
        python_path = os.path.join(
            install_path, 'Engine', 'Binaries', 'ThirdParty', 'Python3', 'Mac', 'bin', 'python3'
        )
    else:
        python_path = os.path.join(
            install_path, 'Engine', 'Binaries', 'ThirdParty', 'Python3', 'Linux', 'bin', 'python3'
        )

    if os.path.exists(python_path):
        log_debug(f"Found Python at: {python_path}", debug)
        return python_path

    log_debug(f"Python not found at: {python_path}", debug)
    return None


def get_source_path_for_ue_install(install_path: str, debug: bool = False) -> Optional[str]:
    """
    Get the Engine/Source path for a given UE installation.
    """
    source_path = os.path.join(install_path, 'Engine', 'Source')
    if os.path.isdir(source_path):
        log_debug(f"Found Source at: {source_path}", debug)
        return source_path

    log_debug(f"Source not found at: {source_path}", debug)
    return None


def common_install_roots() -> List[str]:
    """Folders that commonly hold UE_5.x installs, for machines whose installs are not registered."""
    if sys.platform == 'win32':
        roots = []
        for drive in 'CDEFG':
            roots += [f'{drive}:\\Program Files\\Epic Games', f'{drive}:\\Epic Games', f'{drive}:\\UE']
        return roots
    home = os.path.expanduser('~')
    if sys.platform == 'darwin':
        return ['/Users/Shared/Epic Games', os.path.join(home, 'Epic Games'), '/Applications/Epic Games']
    return [os.path.join(home, 'Epic Games'), os.path.join(home, 'UnrealEngine'), os.path.join(home, 'UE'),
            '/opt/UnrealEngine', '/opt/Epic Games']


def get_ue_installs_from_common_paths(debug: bool = False) -> List[Tuple[str, str]]:
    """
    Get UE installations from common install folders: every UE_<version> folder in them (no fixed version list).
    Returns list of (version_name, install_path) tuples.
    """
    installations = []
    for root in common_install_roots():
        try:
            names = os.listdir(root)
        except OSError:
            continue
        for name in names:
            path = os.path.join(root, name)
            if name.upper().startswith('UE_') and os.path.isdir(os.path.join(path, 'Engine')):
                log_debug(f"Common path found: {name} -> {path}", debug)
                installations.append((name, path))
    return installations


def read_build_version(install_path: str) -> Optional[Tuple[int, int, int]]:
    """The engine version in Engine/Build/Build.version, or None when it cannot be read."""
    try:
        with open(os.path.join(install_path, 'Engine', 'Build', 'Build.version'), 'r', encoding='utf-8-sig') as f:
            data = json.load(f)
        return (int(data['MajorVersion']), int(data['MinorVersion']), int(data.get('PatchVersion', 0)))
    except (OSError, ValueError, KeyError, TypeError):
        return None


def parse_ue_version(version_str: str) -> Tuple[int, int, int]:
    """
    Parse a UE version string into a tuple for sorting.
    Examples: "UE_5.4" -> (5, 4, 0), "5.3.2" -> (5, 3, 2)
    """
    # Extract version numbers
    match = re.search(r'(\d+)\.(\d+)(?:\.(\d+))?', version_str)
    if match:
        major = int(match.group(1))
        minor = int(match.group(2))
        patch = int(match.group(3)) if match.group(3) else 0
        return (major, minor, patch)
    return (0, 0, 0)


def get_ue_installations(prefer_newest: bool = True, debug: bool = False) -> List[Tuple[Tuple[int, int, int], str]]:
    """
    Every UE installation found through the registry, LauncherInstalled.dat and common install folders, once each,
    as (version, install_path) sorted by version (newest first by default). The version comes from the install's
    Engine/Build/Build.version, else from its registered name, so any version (5.6, 5.7, 5.8, later) is found
    without a version list.
    """
    candidates = []
    candidates.extend(get_ue_installs_from_registry(debug))
    candidates.extend(get_ue_installs_from_launcher(debug))
    candidates.extend(get_ue_installs_from_common_paths(debug))

    seen_paths = set()
    installations = []
    for name, path in candidates:
        normalized_path = os.path.normcase(os.path.normpath(path))
        if normalized_path in seen_paths or not os.path.isdir(os.path.join(path, 'Engine')):
            continue
        seen_paths.add(normalized_path)
        version = read_build_version(path) or parse_ue_version(name)
        if version == (0, 0, 0):  # a source build registered under a GUID, without Build.version
            version = parse_ue_version(os.path.basename(os.path.normpath(path)))
        installations.append((version, os.path.normpath(path)))

    installations.sort(key=lambda x: x[0], reverse=prefer_newest)
    log_debug(f"Found {len(installations)} UE installation(s): "
              + ", ".join(f"{'.'.join(map(str, v))} {p}" for v, p in installations), debug)
    return installations


def find_ue_source(prefer_newest: bool = True, debug: bool = False) -> Optional[str]:
    """
    Find UE Engine/Source path.

    Args:
        prefer_newest: If True, prefer the newest UE version. If False, prefer oldest.
        debug: Enable debug logging.

    Returns:
        Path to Engine/Source directory, or None if not found.
    """
    for version, install_path in get_ue_installations(prefer_newest, debug):
        source_path = get_source_path_for_ue_install(install_path, debug)
        if source_path:
            log_debug(f"Selected: {'.'.join(map(str, version))} ({install_path})", debug)
            return source_path

    log_debug("No UE installation has valid Source directory", debug)
    return None


def find_ue_python(prefer_newest: bool = True, debug: bool = False) -> Optional[str]:
    """
    Find UE's bundled Python installation.

    Args:
        prefer_newest: If True, prefer the newest UE version. If False, prefer oldest.
        debug: Enable debug logging.

    Returns:
        Path to Python executable, or None if not found.
    """
    for version, install_path in get_ue_installations(prefer_newest, debug):
        python_path = get_python_path_for_ue_install(install_path, debug)
        if python_path:
            log_debug(f"Selected: {'.'.join(map(str, version))} ({install_path})", debug)
            return python_path

    log_debug("No UE installation has valid Python", debug)
    return None


def find_system_python(debug: bool = False) -> Optional[str]:
    """Find system Python installation: the interpreter running this script when it is a standalone python
    executable, else python3 or python on PATH."""
    import shutil

    executable = sys.executable or ''
    if os.path.isfile(executable) and os.path.basename(executable).lower().startswith('python'):
        log_debug(f"Using the running Python: {executable}", debug)
        return executable

    for cmd in ['python3', 'python']:
        path = shutil.which(cmd)
        if path:
            log_debug(f"Found system Python: {path}", debug)
            return path

    log_debug("System Python not found", debug)
    return None


def find_python(prefer_ue: bool = True, debug: bool = False) -> Optional[str]:
    """
    Find Python installation, preferring UE's bundled Python by default.

    Args:
        prefer_ue: If True, try UE Python first. If False, try system Python first.
        debug: Enable debug logging.

    Returns:
        Path to Python executable, or None if not found.
    """
    if prefer_ue:
        python_path = find_ue_python(debug=debug)
        if python_path:
            return python_path
        return find_system_python(debug)
    else:
        python_path = find_system_python(debug)
        if python_path:
            return python_path
        return find_ue_python(debug=debug)


if __name__ == '__main__':
    # When run directly, print the found Python path (launch_bridge.bat/.sh and detect-ue-path.bat/.sh use this)
    import argparse

    parser = argparse.ArgumentParser(description='Find UE Python installation')
    parser.add_argument('--debug', action='store_true', help='Enable debug output')
    parser.add_argument('--system-first', action='store_true', help='Prefer system Python')
    parser.add_argument('--list', action='store_true', help='List every UE installation found, newest first')
    args = parser.parse_args()

    if args.list:
        for found_version, found_path in get_ue_installations(debug=args.debug):
            print(f"{'.'.join(map(str, found_version))}\t{found_path}\t{get_python_path_for_ue_install(found_path) or '-'}")
        sys.exit(0)

    python_path = find_python(prefer_ue=not args.system_first, debug=args.debug)

    if python_path:
        print(python_path)
        sys.exit(0)
    else:
        print("Python not found", file=sys.stderr)
        sys.exit(1)
