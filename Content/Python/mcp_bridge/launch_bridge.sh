#!/bin/bash
# NodeToCode MCP Bridge Launcher (macOS/Linux)
# The bridge runs under the Python that ue_python_finder.py picks: the bundled Python of the newest Unreal Engine
# install it finds (LauncherInstalled.dat and common install folders, any version), else the Python that ran it.
#   1. Run ue_python_finder.py with python3, falling back to python
#   2. No Python on PATH: run it with the bundled Python of a UE_* install in a common install folder
# stdin and stdout carry UTF-8 JSON-RPC, so PYTHONUTF8=1 is exported; messages from this script go to stderr.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BRIDGE_SCRIPT="$SCRIPT_DIR/nodetocode_bridge.py"
FINDER_SCRIPT="$SCRIPT_DIR/ue_python_finder.py"

# Check that the scripts exist
for script in "$BRIDGE_SCRIPT" "$FINDER_SCRIPT"; do
    if [ ! -f "$script" ]; then
        echo "[NodeToCode-Bridge] ERROR: Script not found at $script" >&2
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
            echo "[NodeToCode-Bridge] Python from ue_python_finder.py (run with $boot): $PYTHON_EXE" >&2
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
        echo "[NodeToCode-Bridge] Python from the UE installs: $PYTHON_EXE" >&2
    fi
fi

if [ -z "$PYTHON_EXE" ]; then
    echo "[NodeToCode-Bridge] ERROR: Python not found. Please install Unreal Engine or Python 3." >&2
    exit 1
fi

# Run the bridge with all passed arguments; its stdio is UTF-8 JSON-RPC
export PYTHONUTF8=1
exec "$PYTHON_EXE" "$BRIDGE_SCRIPT" "$@"
