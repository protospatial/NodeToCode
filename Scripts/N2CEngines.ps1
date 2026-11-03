# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
<#
Where an Unreal Engine install of a given version is, found one way for every script (card 0532). Dot-source it:

    . (Join-Path <repo> 'Scripts/N2CEngines.ps1')
    $found = Find-N2CEngine '5.8' -Override $path -LocalEngines (Read-N2CLocalEngines <repo>\build.local.ps1)
    $found.Path    # the folder that holds Engine/, or $null
    $found.Source  # where it came from, or why nothing was found

build.ps1 and Scripts/package-release.ps1 use it; Tests/DocsShots/capture.py follows the same order in Python.
Per version, the first of these that names a folder wins:
  1. the caller's override (build.ps1 -Engine '<ver>=<path>');
  2. the environment variable N2C_UE_<major>_<minor>, for example N2C_UE_5_8;
  3. $Engines in build.local.ps1 next to build.ps1 (gitignored);
  4. the registry (HKLM:\SOFTWARE\EpicGames\Unreal Engine\<ver>), then the Epic launcher's LauncherInstalled.dat;
  5. source builds registered under HKCU:\Software\Epic Games\Unreal Engine\Builds;
  6. the launcher's default folder (%ProgramFiles%\Epic Games\UE_<ver>, or /Users/Shared/Epic Games/UE_<ver>).
Every candidate is checked against its Engine/Build/Build.version. An override (1-3) that is not that version is
reported as the result instead of quietly falling through to another engine.
#>

function Get-N2CEngineVersion([string]$dir) {
    $file = Join-Path $dir 'Engine/Build/Build.version'
    if (-not (Test-Path $file)) { return $null }
    try { $v = [IO.File]::ReadAllText($file) | ConvertFrom-Json } catch { return $null }
    return "$($v.MajorVersion).$($v.MinorVersion)"
}

function Test-N2CEngineDir([string]$dir, [string]$ver) {
    if (-not $dir) { return $null }
    $dir = $dir.Trim().Trim('"').TrimEnd('\', '/')
    if (-not $dir -or -not (Test-Path $dir)) { return $null }
    if ((Get-N2CEngineVersion $dir) -ne $ver) { return $null }
    return $dir
}

# The $Engines table of a build.local.ps1, read in this function's own scope so its other settings stay out.
function Read-N2CLocalEngines([string]$file) {
    $Engines = @{}
    if ($file -and (Test-Path $file)) { . $file }
    return $Engines
}

# -DefaultFolders replaces step 6's folders (tests only: Windows sets %ProgramFiles% itself for every process).
function Find-N2CEngine([string]$ver, [string]$Override, [hashtable]$LocalEngines, [string[]]$DefaultFolders) {
    $onWindows = ($PSVersionTable.PSEdition -ne 'Core') -or $IsWindows
    $onMac = (-not $onWindows) -and $IsMacOS
    $envName = 'N2C_UE_' + $ver.Replace('.', '_')
    $local = if ($LocalEngines) { $LocalEngines[$ver] } else { $null }
    $explicit = @(
        @{ Source = '-Engine';         Path = $Override },
        @{ Source = "`$env:$envName";  Path = [Environment]::GetEnvironmentVariable($envName) },
        @{ Source = 'build.local.ps1'; Path = $local }
    )
    foreach ($c in $explicit) {
        if (-not $c.Path) { continue }
        $dir = Test-N2CEngineDir $c.Path $ver
        if ($dir) { return [pscustomobject]@{ Path = $dir; Source = $c.Source } }
        # An explicit setting that is wrong is an error, not a reason to quietly use another engine
        return [pscustomobject]@{ Path = $null; Source = "$($c.Source) '$($c.Path)' is not a UE $ver install" }
    }

    if ($onWindows) {
        $key = "HKLM:\SOFTWARE\EpicGames\Unreal Engine\$ver"
        if (Test-Path $key) {
            $dir = Test-N2CEngineDir (Get-ItemProperty $key).InstalledDirectory $ver
            if ($dir) { return [pscustomobject]@{ Path = $dir; Source = 'registry' } }
        }
    }
    $programData = if ($env:ProgramData) { $env:ProgramData } elseif ($onMac) { '/Users/Shared' } else { $null }
    if ($programData) {
        $dat = Join-Path $programData 'Epic/UnrealEngineLauncher/LauncherInstalled.dat'
        if (Test-Path $dat) {
            $list = ([IO.File]::ReadAllText($dat) | ConvertFrom-Json).InstallationList
            foreach ($hit in @($list | Where-Object { $_.AppName -eq "UE_$ver" })) {
                $dir = Test-N2CEngineDir $hit.InstallLocation $ver
                if ($dir) { return [pscustomobject]@{ Path = $dir; Source = 'launcher' } }
            }
        }
    }
    if ($onWindows) {
        $builds = 'HKCU:\Software\Epic Games\Unreal Engine\Builds'
        if (Test-Path $builds) {
            $props = Get-ItemProperty $builds
            foreach ($p in $props.PSObject.Properties) {
                if ($p.Name -like 'PS*') { continue }
                $dir = Test-N2CEngineDir ([string]$p.Value) $ver
                if ($dir) { return [pscustomobject]@{ Path = $dir; Source = 'source build' } }
            }
        }
    }
    $defaults = @()
    if ($DefaultFolders) { $defaults = $DefaultFolders }
    else {
        if ($onWindows -and $env:ProgramFiles) { $defaults += Join-Path $env:ProgramFiles "Epic Games\UE_$ver" }
        if ($onMac) { $defaults += "/Users/Shared/Epic Games/UE_$ver" }
    }
    foreach ($d in $defaults) {
        $dir = Test-N2CEngineDir $d $ver
        if ($dir) { return [pscustomobject]@{ Path = $dir; Source = 'default folder' } }
    }
    return [pscustomobject]@{ Path = $null; Source = 'not found' }
}
