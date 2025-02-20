@echo off
setlocal enabledelayedexpansion

REM --------------------------------------------------------------------------------
REM build_plugins.bat
REM --------------------------------------------------------------------------------
REM This script builds a plugin for multiple UE versions and creates separate
REM zip files for each version, organizing them by version number.
REM --------------------------------------------------------------------------------

:: Base paths
set "PLUGIN_BASE_PATH=D:\Personal Projects\NodeToCodeHostProjects"
set "BASE_OUTPUT_DIR=M:\Protospatial\NodeToCode\Builds"

:: Version configuration
set "VERSION_CONFIG=%~dp0version_config.txt"
set "VERSION_FILE=%~dp0version.txt"

:: Read existing version configuration if it exists
if exist "%VERSION_CONFIG%" (
    for /f "tokens=1,2 delims==" %%a in (%VERSION_CONFIG%) do (
        if "%%a"=="MAJOR" set "CURRENT_MAJOR=%%b"
        if "%%a"=="MINOR" set "CURRENT_MINOR=%%b"
    )
)

:: Display current version and prompt for new version
echo.
echo ======================================================
if defined CURRENT_MAJOR (
    echo Current version: %CURRENT_MAJOR%.%CURRENT_MINOR%.x
) else (
    echo No existing version found
)
echo ======================================================
echo.

:GET_MAJOR
echo Enter Major version number (or press Enter to keep current^):
set /p "INPUT_MAJOR="
if "!INPUT_MAJOR!"=="" (
    if defined CURRENT_MAJOR (
        set "MAJOR=!CURRENT_MAJOR!"
    ) else (
        set "MAJOR=1"
    )
) else (
    echo !INPUT_MAJOR!| findstr /r "^[0-9][0-9]*$" >nul
    if !ERRORLEVEL! EQU 0 (
        set "MAJOR=!INPUT_MAJOR!"
    ) else (
        echo Invalid input. Please enter a positive number.
        goto GET_MAJOR
    )
)

:GET_MINOR
echo Enter Minor version number (or press Enter to keep current^):
set /p "INPUT_MINOR="
if "!INPUT_MINOR!"=="" (
    if defined CURRENT_MINOR (
        set "MINOR=!CURRENT_MINOR!"
    ) else (
        set "MINOR=0"
    )
) else (
    echo !INPUT_MINOR!| findstr /r "^[0-9][0-9]*$" >nul
    if !ERRORLEVEL! EQU 0 (
        set "MINOR=!INPUT_MINOR!"
    ) else (
        echo Invalid input. Please enter a positive number.
        goto GET_MINOR
    )
)

:: Save new version configuration
(
    echo MAJOR=!MAJOR!
    echo MINOR=!MINOR!
) > "%VERSION_CONFIG%"

:: Read build number from version file or initialize it
if exist "%VERSION_FILE%" (
    set /p BUILD_NUMBER=<"%VERSION_FILE%"
) else (
    set "BUILD_NUMBER=0"
)

:: Increment build number
set /a BUILD_NUMBER+=1

:: Save new build number
(echo !BUILD_NUMBER!)> "%VERSION_FILE%"

:: Construct version string
set "VERSION=%MAJOR%.%MINOR%.%BUILD_NUMBER%"

:: Create version-specific builds directory
set "VERSION_BUILDS_DIR=%BASE_OUTPUT_DIR%\v%VERSION%"
if not exist "%VERSION_BUILDS_DIR%" mkdir "%VERSION_BUILDS_DIR%"

:: 1) Define engine versions as an array
set "versions=5.3 5.4 5.5"

echo.
echo ======================================================
echo Building plugin version %VERSION%
echo Output directory: %VERSION_BUILDS_DIR%
echo ======================================================
echo.

:: Loop through each version and update the .uplugin version
for %%v in (%versions%) do (
    :: Convert version number to project folder format (e.g., 5.3 -> 5_3)
    set "PROJECT_VERSION=%%v"
    set "PROJECT_VERSION=!PROJECT_VERSION:.=_!"
    
    :: Set paths
    set "PLUGIN_UPLUGIN=%PLUGIN_BASE_PATH%\NodeToCodeHost_!PROJECT_VERSION!\Plugins\NodeToCode\NodeToCode.uplugin"
    
    :: Update version in .uplugin file using PowerShell
    echo Updating version in UE%%v .uplugin file...
    powershell -Command "(Get-Content '!PLUGIN_UPLUGIN!' | ConvertFrom-Json) | ForEach-Object { $_.Version = !BUILD_NUMBER!; $_.VersionName = '!VERSION!'; $_ } | ConvertTo-Json -Depth 10 | Set-Content '!PLUGIN_UPLUGIN!'"
)

:: 3) Loop through each version for building
for %%v in (%versions%) do (
    echo.
    echo ======================================================
    echo Building for UE %%v...
    echo ======================================================
    echo.
    
    :: Convert version number to project folder format (e.g., 5.3 -> 5_3)
    set "PROJECT_VERSION=%%v"
    set "PROJECT_VERSION=!PROJECT_VERSION:.=_!"
    
    :: Create version-specific paths
    set "PLUGIN_UPLUGIN=%PLUGIN_BASE_PATH%\NodeToCodeHost_!PROJECT_VERSION!\Plugins\NodeToCode\NodeToCode.uplugin"
    set "VERSION_OUTPUT_DIR=%VERSION_BUILDS_DIR%\UE%%v_build"
    
    :: Create the output directory if it doesn't exist
    if not exist "!VERSION_OUTPUT_DIR!" mkdir "!VERSION_OUTPUT_DIR!"
    
    echo Using .uplugin file: !PLUGIN_UPLUGIN!
    echo Output directory: !VERSION_OUTPUT_DIR!
    echo.
    
    :: Build the plugin for this version
    call uattool %%v BuildPlugin -Plugin="!PLUGIN_UPLUGIN!" -Package="!VERSION_OUTPUT_DIR!" -Rocket
    
    :: Add a small delay to ensure processes are complete
    timeout /t 5 /nobreak > nul
    
    :: Check if build was successful
    if !ERRORLEVEL! EQU 0 (
        echo.
        echo Build successful for UE %%v
        
        :: Create zip file using robocopy to create a clean copy first
        echo.
        echo Creating clean copy for zipping...
        set "TEMP_DIR=!VERSION_OUTPUT_DIR!_temp"
        
        :: Create temp directory if it doesn't exist
        if not exist "!TEMP_DIR!" mkdir "!TEMP_DIR!"
        
        :: Use robocopy to copy files, excluding Binaries and Intermediate
        robocopy "!VERSION_OUTPUT_DIR!" "!TEMP_DIR!" /E /XD "Binaries" "Intermediate" /NFL /NDL /NJH /NJS /nc /ns /np
        
        :: Create the zip file from the clean copy
        echo Creating zip archive for UE %%v...
        powershell Compress-Archive -Path "!TEMP_DIR!\*" -DestinationPath "!VERSION_BUILDS_DIR!\NodeToCode_%%v.zip" -Force
        
        :: Clean up
        echo Cleaning up temporary files...
        rd /s /q "!VERSION_OUTPUT_DIR!" 2>nul
        rd /s /q "!TEMP_DIR!" 2>nul
        
        echo Zip archive created successfully
    ) else (
        echo.
        echo Build failed for UE %%v
    )
    
    echo.
    echo ------------------------------------------------------
    echo.
)

echo ======================================================
echo Build process complete!
echo All files are located in: %VERSION_BUILDS_DIR%
echo ======================================================
echo.
pause