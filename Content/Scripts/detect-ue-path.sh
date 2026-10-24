#!/bin/bash
# NodeToCode UE Path Detector (macOS/Linux)
# Finds Unreal Engine installation and updates AGENTS.md
# Finds Python the same way as launch_bridge.sh: ue_python_finder.py (Content/Python/mcp_bridge) picks the bundled
# Python of the newest UE install it finds (LauncherInstalled.dat, common folders; any version).
#   1. Run ue_python_finder.py with python3, falling back to python
#   2. No Python on PATH: run it with the bundled Python of a UE_* install in a common install folder

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PYTHON_SCRIPT="$SCRIPT_DIR/update_agents_ue_path.py"
FINDER_SCRIPT="$SCRIPT_DIR/../Python/mcp_bridge/ue_python_finder.py"

# Check that the scripts exist
for script in "$PYTHON_SCRIPT" "$FINDER_SCRIPT"; do
    if [ ! -f "$script" ]; then
        echo "[detect-ue-path] ERROR: Script not found at $script"
        exit 1
    fi
done

PYTHON_EXE=""

# === 1. ue_python_finder.py, run with a Python on PATH ===
for boot in python3 python; do
    if command -v "$boot" >/dev/null 2>&1; then
        found="$("$boot" "$FINDER_SCRIPT" 2>/dev/null)"
        if [ -n "$found" ]; then
            PYTHON_EXE="$found"
            echo "[detect-ue-path] Python from ue_python_finder.py (run with $boot): $PYTHON_EXE"
            break
        fi
    fi
done

# === 2. No Python on PATH: a UE install's bundled Python runs the finder ===
if [ -z "$PYTHON_EXE" ]; then
    BOOT_PYTHON=""
    for root in "/Users/Shared/Epic Games" "$HOME/Epic Games" "/Applications/Epic Games" "$HOME/UnrealEngine"; do
        for candidate in "$root"/UE_*/Engine/Binaries/ThirdParty/Python3/Mac/bin/python3 \
                         "$root"/UE_*/Engine/Binaries/ThirdParty/Python3/Linux/bin/python3; do
            if [ -x "$candidate" ]; then
                BOOT_PYTHON="$candidate"
            fi
        done
    done
    if [ -n "$BOOT_PYTHON" ]; then
        PYTHON_EXE="$("$BOOT_PYTHON" "$FINDER_SCRIPT" 2>/dev/null)"
        [ -n "$PYTHON_EXE" ] || PYTHON_EXE="$BOOT_PYTHON"
        echo "[detect-ue-path] Python from the UE installs: $PYTHON_EXE"
    fi
fi

if [ -z "$PYTHON_EXE" ]; then
    echo "[detect-ue-path] ERROR: Python not found. Please install Python 3 or Unreal Engine."
    exit 1
fi

# Run the Python script to update AGENTS.md
echo "[detect-ue-path] Running UE path detection..."
exec "$PYTHON_EXE" "$PYTHON_SCRIPT" "$@"
