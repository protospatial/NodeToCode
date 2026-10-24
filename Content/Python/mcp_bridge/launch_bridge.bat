@echo off
REM NodeToCode MCP Bridge Launcher (Windows)
REM The bridge runs under the Python that ue_python_finder.py picks: the bundled Python of the newest Unreal Engine
REM install it finds (registry, LauncherInstalled.dat and common install folders, any version), else the Python
REM that ran it.
REM   1. Run ue_python_finder.py with the first of these that works: py -3, python3, python
REM   2. No Python on PATH: run it with the bundled Python of a launcher install registered under
REM      HKLM\SOFTWARE\EpicGames\Unreal Engine\<version> (any version)
REM stdin and stdout carry UTF-8 JSON-RPC, so PYTHONUTF8=1 is set; messages from this script go to stderr.

setlocal EnableDelayedExpansion

REM Get the directory where this script is located
set "SCRIPT_DIR=%~dp0"
set "BRIDGE_SCRIPT=%SCRIPT_DIR%nodetocode_bridge.py"
set "FINDER_SCRIPT=%SCRIPT_DIR%ue_python_finder.py"

REM Check that the scripts exist
if not exist "%BRIDGE_SCRIPT%" (
    echo [NodeToCode-Bridge] ERROR: Bridge script not found at %BRIDGE_SCRIPT% 1>&2
    exit /b 1
)
if not exist "%FINDER_SCRIPT%" (
    echo [NodeToCode-Bridge] ERROR: ue_python_finder.py not found at %FINDER_SCRIPT% 1>&2
    exit /b 1
)

set "PYTHON_EXE="

REM === 1. ue_python_finder.py, run with a Python on PATH ===
for %%C in ("py -3" "python3" "python") do (
    if "!PYTHON_EXE!"=="" (
        for /f "usebackq delims=" %%P in (`%%~C "%FINDER_SCRIPT%" 2^>nul`) do set "PYTHON_EXE=%%P"
        if not "!PYTHON_EXE!"=="" echo [NodeToCode-Bridge] Python from ue_python_finder.py ^(run with %%~C^): !PYTHON_EXE! 1>&2
    )
)

REM === 2. No Python on PATH: a registered launcher install's bundled Python runs the finder ===
if "!PYTHON_EXE!"=="" (
    set "BOOT_PYTHON="
    for /f "tokens=2,*" %%A in ('reg query "HKLM\SOFTWARE\EpicGames\Unreal Engine" /s /v InstalledDirectory 2^>nul') do (
        if "%%A"=="REG_SZ" (
            if exist "%%B\Engine\Binaries\ThirdParty\Python3\Win64\python.exe" set "BOOT_PYTHON=%%B\Engine\Binaries\ThirdParty\Python3\Win64\python.exe"
        )
    )
    if not "!BOOT_PYTHON!"=="" (
        for /f "usebackq delims=" %%P in (`call "!BOOT_PYTHON!" "%FINDER_SCRIPT%" 2^>nul`) do set "PYTHON_EXE=%%P"
        if "!PYTHON_EXE!"=="" set "PYTHON_EXE=!BOOT_PYTHON!"
        echo [NodeToCode-Bridge] Python from the registered UE installs: !PYTHON_EXE! 1>&2
    )
)

REM === No Python found ===
if "!PYTHON_EXE!"=="" (
    echo [NodeToCode-Bridge] ERROR: Python not found. 1>&2
    echo [NodeToCode-Bridge] Please install Unreal Engine or Python 3. 1>&2
    exit /b 1
)

REM Run the bridge with all passed arguments; its stdio is UTF-8 JSON-RPC
set "PYTHONUTF8=1"
"!PYTHON_EXE!" "%BRIDGE_SCRIPT%" %*
