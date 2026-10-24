@echo off
REM NodeToCode UE Path Detector (Windows)
REM Finds Unreal Engine installation and updates AGENTS.md
REM Finds Python the same way as launch_bridge.bat: ue_python_finder.py (Content\Python\mcp_bridge) picks the bundled
REM Python of the newest UE install it finds (registry, LauncherInstalled.dat, common folders; any version).
REM   1. Run ue_python_finder.py with the first of these that works: py -3, python3, python
REM   2. No Python on PATH: run it with the bundled Python of a launcher install registered under
REM      HKLM\SOFTWARE\EpicGames\Unreal Engine\<version> (any version)

setlocal EnableDelayedExpansion

REM Get the directory where this script is located
set "SCRIPT_DIR=%~dp0"
set "PYTHON_SCRIPT=%SCRIPT_DIR%update_agents_ue_path.py"
set "FINDER_SCRIPT=%SCRIPT_DIR%..\Python\mcp_bridge\ue_python_finder.py"

REM Check that the scripts exist
if not exist "%PYTHON_SCRIPT%" (
    echo [detect-ue-path] ERROR: Python script not found at %PYTHON_SCRIPT%
    exit /b 1
)
if not exist "%FINDER_SCRIPT%" (
    echo [detect-ue-path] ERROR: ue_python_finder.py not found at %FINDER_SCRIPT%
    exit /b 1
)

set "PYTHON_EXE="

REM === 1. ue_python_finder.py, run with a Python on PATH ===
for %%C in ("py -3" "python3" "python") do (
    if "!PYTHON_EXE!"=="" (
        for /f "usebackq delims=" %%P in (`%%~C "%FINDER_SCRIPT%" 2^>nul`) do set "PYTHON_EXE=%%P"
        if not "!PYTHON_EXE!"=="" echo [detect-ue-path] Python from ue_python_finder.py ^(run with %%~C^): !PYTHON_EXE!
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
        echo [detect-ue-path] Python from the registered UE installs: !PYTHON_EXE!
    )
)

REM === No Python found ===
if "!PYTHON_EXE!"=="" (
    echo [detect-ue-path] ERROR: Python not found.
    echo [detect-ue-path] Please install Unreal Engine or Python 3.
    exit /b 1
)

REM Run the Python script to update AGENTS.md
echo [detect-ue-path] Running UE path detection...
"!PYTHON_EXE!" "%PYTHON_SCRIPT%" %*
