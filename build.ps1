# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0

<#
.SYNOPSIS
    NodeToCode build, smoke and automation-test runner for UE 5.6, 5.7 and 5.8.

.DESCRIPTION
    Each UE version gets its own minimal C++ host project, <HostsRoot>\NodeToCodeHost_<ver> (default: a Hosts folder
    next to this checkout). The script creates a missing host on first use. The host's Plugins\NodeToCode folder is a
    real directory whose Source, Content, Resources and ThirdParty are junctions (symbolic links on Mac and Linux)
    back into this checkout, so:
      * there is exactly one copy of the source (this checkout), and edits anywhere land here;
      * Binaries and Intermediate stay per host, so each engine builds incrementally without clobbering
        another engine's DLLs.
    NodeToCode.uplugin is a file, so it is copied into each host on every run instead.

    Engines are found, per version, in this order:
      1. -Engine '<ver>=<path>' (repeatable; a bare path is allowed when exactly one -UEVersion is given);
      2. the environment variables N2C_UE_5_6, N2C_UE_5_7 and N2C_UE_5_8;
      3. $Engines in build.local.ps1 (see below);
      4. the registry (HKLM:\SOFTWARE\EpicGames\Unreal Engine\<ver>) and the Epic launcher's LauncherInstalled.dat;
      5. source builds registered under HKCU:\Software\Epic Games\Unreal Engine\Builds;
      6. the launcher's default folder (%ProgramFiles%\Epic Games\UE_<ver>, or /Users/Shared/Epic Games/UE_<ver>).
    Every candidate is checked against its Engine/Build/Build.version, so an override that points at the wrong
    engine is reported instead of being used.

    Machine-specific settings go in build.local.ps1 next to this script (gitignored). When it exists it is
    dot-sourced before anything runs, and may set:
        $Engines      = @{ '5.8' = '<path to UE_5.8>' }
        $HostsRoot    = '<folder for host projects>'
        $PackagesRoot = '<folder for -Package output>'
        $LogRoot      = '<folder for logs>'
    Parameters given on the command line win over build.local.ps1.

    Release packages come from Scripts\package-release.ps1, not from -Package (a quick local BuildPlugin check).

.EXAMPLE
    .\build.ps1 -UEVersion 5.7              # incremental editor build for 5.7
    .\build.ps1 -All -Smoke                 # build 5.6, 5.7, 5.8, then load the plugin in a headless editor
    .\build.ps1 -All -Test                  # build, then run the NodeToCode automation tests on each engine
    .\build.ps1 -UEVersion 5.6 -Launch      # build, then open the host editor
    .\build.ps1 -All -Package               # RunUAT BuildPlugin (clean, out-of-tree)
    .\build.ps1 -WhatIf -HostsRoot <scratch dir> # show detected engines and the hosts that would be used
    .\build.ps1 -UEVersion 5.8 -Engine '<path to UE_5.8>' -Test # one engine at an explicit path
#>
[CmdletBinding()]
param(
    [ValidateSet('5.6', '5.7', '5.8')]
    [string[]]$UEVersion,
    [switch]$All,
    [switch]$WhatIf,      # list detected engines and hosts, change nothing
    [switch]$Clean,       # wipe the host's plugin Binaries/Intermediate first
    [switch]$Smoke,       # after build: headless editor load check (Tests/smoke/smoke_check.py)
    [switch]$Test,        # after build: Automation RunTests <TestFilter>, report pass/fail from the exported report
    [switch]$Launch,      # after build: open the editor on the host project
    [switch]$Package,     # RunUAT BuildPlugin instead of the incremental host build
    [string]$Config = 'Development',
    [string[]]$Engine,    # per-version engine override: '5.8=<path>' (repeatable)
    [string]$HostsRoot,   # default: <checkout>\..\Hosts
    [string]$PackagesRoot,# default: <checkout>\..\Packages
    [string]$LogRoot,     # default: <checkout>\BuildLogs
    [string]$TestFilter = 'NodeToCode',
    [int]$TestTimeoutMinutes = 60
)

$ErrorActionPreference = 'Stop'
$SupportedVersions = @('5.6', '5.7', '5.8')
$PluginRoot = $PSScriptRoot
$WorkRoot   = Split-Path $PluginRoot -Parent
$LinkedDirs = @('Source', 'Content', 'Resources', 'ThirdParty')
$OnWindows  = ($PSVersionTable.PSEdition -ne 'Core') -or $IsWindows
$OnMac      = (-not $OnWindows) -and $IsMacOS
$Platform   = if ($OnWindows) { 'Win64' } elseif ($OnMac) { 'Mac' } else { 'Linux' }

# Defaults, then build.local.ps1, then whatever was passed on the command line
$Engines = @{}
$bound = @{}
foreach ($k in $PSBoundParameters.Keys) { $bound[$k] = $PSBoundParameters[$k] }
if (-not $HostsRoot)    { $HostsRoot    = Join-Path $WorkRoot 'Hosts' }
if (-not $PackagesRoot) { $PackagesRoot = Join-Path $WorkRoot 'Packages' }
if (-not $LogRoot)      { $LogRoot      = Join-Path $PluginRoot 'BuildLogs' }
$LocalSettings = Join-Path $PluginRoot 'build.local.ps1'
if (Test-Path $LocalSettings) {
    . $LocalSettings
    foreach ($name in @('HostsRoot', 'PackagesRoot', 'LogRoot')) {
        if ($bound.ContainsKey($name)) { Set-Variable -Name $name -Value $bound[$name] }
    }
}
$HostsRoot    = [IO.Path]::GetFullPath($HostsRoot)
$PackagesRoot = [IO.Path]::GetFullPath($PackagesRoot)
$LogRoot      = [IO.Path]::GetFullPath($LogRoot)

function Write-Step($msg) { Write-Host "==> $msg" -ForegroundColor Cyan }

# ---------------------------------------------------------------------------
# Engine discovery

function Get-EngineVersion([string]$dir) {
    $file = Join-Path $dir 'Engine/Build/Build.version'
    if (-not (Test-Path $file)) { return $null }
    try { $v = [IO.File]::ReadAllText($file) | ConvertFrom-Json } catch { return $null }
    return "$($v.MajorVersion).$($v.MinorVersion)"
}

function Test-EngineDir([string]$dir, [string]$ver) {
    if (-not $dir) { return $null }
    $dir = $dir.Trim().Trim('"').TrimEnd('\', '/')
    if (-not $dir -or -not (Test-Path $dir)) { return $null }
    if ((Get-EngineVersion $dir) -ne $ver) { return $null }
    return $dir
}

function Get-EngineOverrides {
    $map = @{}
    foreach ($entry in @($Engine)) {
        if (-not $entry) { continue }
        if ($entry -match '^\s*(5\.\d+)\s*=\s*(.+)$') { $map[$Matches[1]] = $Matches[2] }
        elseif (@($UEVersion).Count -eq 1) { $map[$UEVersion[0]] = $entry }
        else { throw "-Engine '$entry': use '<version>=<path>' (a bare path needs exactly one -UEVersion)." }
    }
    return $map
}

function Find-Engine([string]$ver, $overrides) {
    $envName = 'N2C_UE_' + $ver.Replace('.', '_')
    $explicit = @(
        @{ Source = '-Engine';           Path = $overrides[$ver] },
        @{ Source = "`$env:$envName";    Path = [Environment]::GetEnvironmentVariable($envName) },
        @{ Source = 'build.local.ps1';   Path = $Engines[$ver] }
    )
    foreach ($c in $explicit) {
        if (-not $c.Path) { continue }
        $dir = Test-EngineDir $c.Path $ver
        if ($dir) { return [pscustomobject]@{ Path = $dir; Source = $c.Source } }
        # An explicit setting that is wrong is an error, not a reason to quietly use another engine
        return [pscustomobject]@{ Path = $null; Source = "$($c.Source) '$($c.Path)' is not a UE $ver install" }
    }

    if ($OnWindows) {
        $key = "HKLM:\SOFTWARE\EpicGames\Unreal Engine\$ver"
        if (Test-Path $key) {
            $dir = Test-EngineDir (Get-ItemProperty $key).InstalledDirectory $ver
            if ($dir) { return [pscustomobject]@{ Path = $dir; Source = 'registry' } }
        }
    }
    $programData = if ($env:ProgramData) { $env:ProgramData } elseif ($OnMac) { '/Users/Shared' } else { $null }
    if ($programData) {
        $dat = Join-Path $programData 'Epic/UnrealEngineLauncher/LauncherInstalled.dat'
        if (Test-Path $dat) {
            $list = ([IO.File]::ReadAllText($dat) | ConvertFrom-Json).InstallationList
            foreach ($hit in @($list | Where-Object { $_.AppName -eq "UE_$ver" })) {
                $dir = Test-EngineDir $hit.InstallLocation $ver
                if ($dir) { return [pscustomobject]@{ Path = $dir; Source = 'launcher' } }
            }
        }
    }
    if ($OnWindows) {
        $builds = 'HKCU:\Software\Epic Games\Unreal Engine\Builds'
        if (Test-Path $builds) {
            $props = Get-ItemProperty $builds
            foreach ($p in $props.PSObject.Properties) {
                if ($p.Name -like 'PS*') { continue }
                $dir = Test-EngineDir ([string]$p.Value) $ver
                if ($dir) { return [pscustomobject]@{ Path = $dir; Source = 'source build' } }
            }
        }
    }
    $defaults = @()
    if ($OnWindows -and $env:ProgramFiles) { $defaults += Join-Path $env:ProgramFiles "Epic Games\UE_$ver" }
    if ($OnMac) { $defaults += "/Users/Shared/Epic Games/UE_$ver" }
    foreach ($d in $defaults) {
        $dir = Test-EngineDir $d $ver
        if ($dir) { return [pscustomobject]@{ Path = $dir; Source = 'default folder' } }
    }
    return [pscustomobject]@{ Path = $null; Source = 'not found' }
}

function Get-EngineTool([string]$engine, [string]$tool) {
    $rel = switch ($tool) {
        'Build'  { if ($OnWindows) { 'Engine\Build\BatchFiles\Build.bat' } else { "Engine/Build/BatchFiles/$Platform/Build.sh" } }
        'RunUAT' { if ($OnWindows) { 'Engine\Build\BatchFiles\RunUAT.bat' } else { 'Engine/Build/BatchFiles/RunUAT.sh' } }
        'EditorCmd' {
            if ($OnWindows) { 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe' } else { "Engine/Binaries/$Platform/UnrealEditor-Cmd" } }
        'Editor' {
            if ($OnWindows) { 'Engine\Binaries\Win64\UnrealEditor.exe' }
            elseif ($OnMac) { 'Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor' }
            else { 'Engine/Binaries/Linux/UnrealEditor' } }
    }
    return Join-Path $engine $rel
}

# ---------------------------------------------------------------------------
# Host projects

function Get-HostInfo([string]$ver) {
    $name = 'NodeToCodeHost_' + $ver.Replace('.', '_')
    $dir  = Join-Path $HostsRoot $name
    [pscustomobject]@{
        Name      = $name
        Dir       = $dir
        UProject  = Join-Path $dir "$name.uproject"
        Target    = "${name}Editor"
        PluginDir = Join-Path $dir (Join-Path 'Plugins' 'NodeToCode')
    }
}

# Content is compared and written with LF endings, so a CRLF checkout of this script does not rewrite every host file
function Write-IfChanged([string]$path, [string]$content) {
    $content = $content -replace "`r`n", "`n"
    if ((Test-Path $path) -and (([IO.File]::ReadAllText($path) -replace "`r`n", "`n") -eq $content)) { return }
    New-Item -ItemType Directory -Force (Split-Path $path -Parent) | Out-Null
    [IO.File]::WriteAllText($path, $content, (New-Object Text.UTF8Encoding($false)))
}

function Get-LinkTarget($item) {
    if (-not $item -or -not $item.LinkType) { return $null }
    $t = @($item.Target)[0]
    if (-not $t) { return $null }
    if (-not [IO.Path]::IsPathRooted($t)) { $t = Join-Path (Split-Path $item.FullName -Parent) $t }
    return ([IO.Path]::GetFullPath($t)).TrimEnd('\', '/')
}

function Initialize-HostProject($h, [string]$ver) {
    $n = $h.Name
    if (-not (Test-Path $h.UProject)) { Write-Step "Creating host project $($h.Dir)" }
    $uproject = @"
{
	"FileVersion": 3,
	"EngineAssociation": "$ver",
	"Category": "",
	"Description": "NodeToCode development host for UE $ver",
	"Modules": [
		{ "Name": "$n", "Type": "Runtime", "LoadingPhase": "Default" }
	],
	"Plugins": [
		{ "Name": "NodeToCode", "Enabled": true }
	]
}
"@
    Write-IfChanged $h.UProject $uproject

    foreach ($kind in @('Game', 'Editor')) {
        $tname = if ($kind -eq 'Editor') { "${n}Editor" } else { $n }
        Write-IfChanged (Join-Path $h.Dir "Source/$tname.Target.cs") @"
using UnrealBuildTool;

public class ${tname}Target : TargetRules
{
	public ${tname}Target(TargetInfo Target) : base(Target)
	{
		Type = TargetType.$kind;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("$n");
	}
}
"@
    }
    Write-IfChanged (Join-Path $h.Dir "Source/$n/$n.Build.cs") @"
using UnrealBuildTool;

public class $n : ModuleRules
{
	public $n(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
	}
}
"@
    Write-IfChanged (Join-Path $h.Dir "Source/$n/$n.cpp") @"
#include "Modules/ModuleManager.h"

IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, $n, "$n");
"@
    Write-IfChanged (Join-Path $h.Dir 'Config/DefaultEngine.ini') @"
[/Script/EngineSettings.GameMapsSettings]
GameDefaultMap=/Engine/Maps/Templates/OpenWorld

[/Script/UnrealEd.UnrealEdOptions]
bShowStartupMessages=False
"@
    Write-IfChanged (Join-Path $h.Dir 'Config/DefaultEditorPerProjectUserSettings.ini') @"
[/Script/LiveCoding.LiveCodingSettings]
bEnabled=True
Startup=AutomaticButHidden
"@

    # Plugin: real dir, linked source folders, per-host Binaries/Intermediate
    New-Item -ItemType Directory -Force $h.PluginDir | Out-Null
    foreach ($d in $LinkedDirs) {
        $link   = Join-Path $h.PluginDir $d
        $target = ([IO.Path]::GetFullPath((Join-Path $PluginRoot $d))).TrimEnd('\', '/')
        $item   = Get-Item $link -Force -ErrorAction SilentlyContinue
        if ($item) {
            if ((Get-LinkTarget $item) -eq $target) { continue }
            throw "$link exists but is not a link to $target (it may belong to another checkout): remove it, or use -HostsRoot."
        }
        $type = if ($OnWindows) { 'Junction' } else { 'SymbolicLink' }
        New-Item -ItemType $type -Path $link -Target $target | Out-Null
    }
    Copy-Item (Join-Path $PluginRoot 'NodeToCode.uplugin') (Join-Path $h.PluginDir 'NodeToCode.uplugin') -Force
}

function Test-EditorRunning($h) {
    if ($OnWindows) {
        $procs = Get-CimInstance Win32_Process -Filter "Name LIKE 'UnrealEditor%'" -ErrorAction SilentlyContinue
        return @($procs | Where-Object { $_.CommandLine -and $_.CommandLine -like "*$($h.Name).uproject*" })
    }
    return @(Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Name -like 'UnrealEditor*' -and $_.Path } |
        Where-Object { (Get-Content "/proc/$($_.Id)/cmdline" -Raw -ErrorAction SilentlyContinue) -like "*$($h.Name).uproject*" })
}

# ---------------------------------------------------------------------------
# Process helpers

function Format-Arg([string]$a) {
    if ($a -match '\s' -and $a -notmatch '"') { return '"' + $a + '"' }
    return $a
}

function Invoke-Logged([string]$exe, [string[]]$argList, [string]$log) {
    Write-Host "    $exe $($argList -join ' ')" -ForegroundColor DarkGray
    $show = {
        if ($_ -match '\berror\b|: fatal') { Write-Host $_ -ForegroundColor Red }
        elseif ($_ -match '\bwarning\b') { Write-Host $_ -ForegroundColor Yellow }
        else { Write-Host $_ }
    }
    if ($OnWindows) {
        $quoted = $argList | ForEach-Object { Format-Arg $_ }
        & cmd.exe /c "`"$exe`" $($quoted -join ' ') 2>&1" | Tee-Object -FilePath $log | ForEach-Object $show
    } else {
        & $exe @argList 2>&1 | ForEach-Object { "$_" } | Tee-Object -FilePath $log | ForEach-Object $show
    }
    return $LASTEXITCODE
}

# Runs an editor without echoing its output (it logs through -abslog) and kills it after the timeout
function Invoke-Editor([string]$exe, [string[]]$argList, [int]$timeoutMinutes) {
    $line = ($argList | ForEach-Object { Format-Arg $_ }) -join ' '
    Write-Host "    $exe $line" -ForegroundColor DarkGray
    $psi = New-Object Diagnostics.ProcessStartInfo
    $psi.FileName = $exe
    $psi.Arguments = $line
    $psi.UseShellExecute = $false
    $proc = [Diagnostics.Process]::Start($psi)
    if (-not $proc.WaitForExit($timeoutMinutes * 60 * 1000)) {
        Write-Host "    timed out after $timeoutMinutes min, stopping the editor (pid $($proc.Id))" -ForegroundColor Red
        if ($OnWindows) { & taskkill.exe /PID $proc.Id /T /F | Out-Null } else { $proc.Kill() }
        $proc.WaitForExit(30000) | Out-Null
        return $null
    }
    return $proc.ExitCode
}

function Show-Failure([string]$log) {
    Write-Host "`n--- errors (from $log) ---" -ForegroundColor Red
    Select-String -Path $log -Pattern '\berror\b|: fatal' | Select-Object -First 25 | ForEach-Object { Write-Host $_.Line -ForegroundColor Red }
}

# ---------------------------------------------------------------------------
# Smoke and tests

function Invoke-Smoke($h, [string]$engine, [string]$ver, [string]$stamp) {
    $script = Join-Path $PluginRoot 'Tests/smoke/smoke_check.py'
    $log = Join-Path $LogRoot "Smoke_${ver}_$stamp.log"
    $exe = Get-EngineTool $engine 'EditorCmd'
    Write-Step "Smoke test UE $ver (headless editor, -nullrhi)"
    $code = Invoke-Logged $exe @($h.UProject, "-ExecutePythonScript=$($script -replace '\\', '/')", '-unattended', '-nullrhi',
        '-nosplash', '-nosound', '-stdout', '-FullStdOutLogOutput', '-NoLiveCoding') $log
    $pass = Select-String -Path $log -Pattern 'N2C_SMOKE PASS' -Quiet
    $loaded = Select-String -Path $log -Pattern 'LogNodeToCode' -Quiet
    if ($pass) { Write-Host "SMOKE PASS (UE $ver)" -ForegroundColor Green; return $true }
    Write-Host "SMOKE FAIL (UE $ver, exit $code, LogNodeToCode seen: $loaded) - see $log" -ForegroundColor Red
    return $false
}

# Returns a short result string: 'tests PASS (n)', 'tests WARN (0 run)' or 'tests FAIL (...)'
function Invoke-Tests($h, [string]$engine, [string]$ver, [string]$stamp) {
    $reportDir = Join-Path $LogRoot "Tests_${ver}_$stamp"
    $log = Join-Path $LogRoot "Tests_${ver}_$stamp.log"
    New-Item -ItemType Directory -Force $reportDir | Out-Null
    Write-Step "Automation tests UE $ver ($TestFilter)"
    # -ExecCmds holds one command; never chain with ';' (it hangs the editor), use ',' if more are ever needed
    $code = Invoke-Editor (Get-EngineTool $engine 'EditorCmd') @(
        $h.UProject, "-ExecCmds=`"Automation RunTests $TestFilter`"", '-TestExit="Automation Test Queue Empty"',
        "-ReportExportPath=`"$reportDir`"", "-abslog=`"$log`"",
        '-unattended', '-nullrhi', '-nosplash', '-nosound', '-NoLiveCoding', '-culture=en') $TestTimeoutMinutes
    if ($null -eq $code) { return 'tests FAIL (timed out)' }

    $index = Join-Path $reportDir 'index.json'
    $logText = if (Test-Path $log) { [IO.File]::ReadAllText($log).Replace("`0", '') } else { '' }
    if (-not (Test-Path $index) -and $logText -match 'No automation tests matched|Automation Test Queue Empty 0 tests performed') {
        # The editor writes no report when the filter matches nothing
        Write-Host "TESTS WARN (UE $ver): zero tests ran for '$TestFilter' - see $log" -ForegroundColor Yellow
        return 'tests WARN (0 run)'
    }
    if (-not (Test-Path $index)) {
        Write-Host "TESTS FAIL (UE $ver): no report at $index (editor exit $code) - see $log" -ForegroundColor Red
        return "tests FAIL (no report, exit $code)"
    }
    $report = [IO.File]::ReadAllText($index) | ConvertFrom-Json
    $tests = @($report.tests)
    $failed = @($tests | Where-Object { $_.state -eq 'Fail' })
    $passed = @($tests | Where-Object { $_.state -eq 'Success' })
    $other  = @($tests | Where-Object { $_.state -ne 'Fail' -and $_.state -ne 'Success' })
    foreach ($t in $failed) {
        Write-Host "  FAIL $($t.fullTestPath)" -ForegroundColor Red
        foreach ($e in @($t.entries | Where-Object { $_.event.type -eq 'Error' } | Select-Object -First 5)) {
            Write-Host "       $($e.event.message)" -ForegroundColor Red
        }
    }
    foreach ($t in $other) { Write-Host "  $($t.state) $($t.fullTestPath)" -ForegroundColor Yellow }
    if ($tests.Count -eq 0) {
        Write-Host "TESTS WARN (UE $ver): zero tests ran for '$TestFilter' - see $log" -ForegroundColor Yellow
        return 'tests WARN (0 run)'
    }
    if ($failed.Count -gt 0 -or $other.Count -gt 0) {
        Write-Host "TESTS FAIL (UE $ver): $($failed.Count) failed, $($other.Count) not completed, $($passed.Count) passed - report $reportDir" -ForegroundColor Red
        return "tests FAIL ($($failed.Count) failed, $($other.Count) not completed of $($tests.Count))"
    }
    Write-Host "TESTS PASS (UE $ver): $($passed.Count) passed - report $reportDir" -ForegroundColor Green
    return "tests PASS ($($passed.Count))"
}

# ---------------------------------------------------------------------------
$overrides = Get-EngineOverrides
$detected = [ordered]@{}
foreach ($v in $SupportedVersions) { $detected[$v] = Find-Engine $v $overrides }

if (-not $UEVersion -and -not $All -and -not $WhatIf) {
    Get-Help $PSCommandPath -Examples
    $WhatIf = $true
}

if ($WhatIf) {
    Write-Host "`nHosts root:    $HostsRoot" -ForegroundColor Cyan
    Write-Host "Packages root: $PackagesRoot" -ForegroundColor Cyan
    Write-Host "Log root:      $LogRoot" -ForegroundColor Cyan
    if (Test-Path $LocalSettings) { Write-Host "Local settings: $LocalSettings" -ForegroundColor Cyan }
    Write-Host "Detected engines / hosts:" -ForegroundColor Cyan
    foreach ($v in $SupportedVersions) {
        $h = Get-HostInfo $v
        $e = $detected[$v]
        $eng = if ($e.Path) { "$($e.Path) ($($e.Source))" } else { "($($e.Source))" }
        $hs  = if (Test-Path $h.UProject) { 'host ready' } else { 'host will be created on first build' }
        Write-Host ("  UE {0}  {1}" -f $v, $eng)
        Write-Host ("          {0}  [{1}]" -f $h.UProject, $hs)
    }
    return
}

$targets = if ($All) { @($SupportedVersions | Where-Object { $detected[$_].Path }) } else { @($UEVersion) }
if ($All -and $targets.Count -eq 0) { Write-Host 'No supported engine found (run with -WhatIf).' -ForegroundColor Red; exit 1 }
New-Item -ItemType Directory -Force $LogRoot | Out-Null
$results = [ordered]@{}

foreach ($ver in $targets) {
    $engine = $detected[$ver].Path
    if (-not $engine) { Write-Host "UE $ver not found: $($detected[$ver].Source)" -ForegroundColor Red; $results[$ver] = 'NO ENGINE'; continue }
    $stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
    $sw = [Diagnostics.Stopwatch]::StartNew()

    if ($Package) {
        $out = Join-Path $PackagesRoot "NodeToCode_$ver"
        $log = Join-Path $LogRoot "Package_${ver}_$stamp.log"
        Write-Step "BuildPlugin UE $ver -> $out"
        $code = Invoke-Logged (Get-EngineTool $engine 'RunUAT') @(
            'BuildPlugin', "-Plugin=$(Join-Path $PluginRoot 'NodeToCode.uplugin')", "-Package=$out",
            "-TargetPlatforms=$Platform", '-Rocket') $log
        $results[$ver] = if ($code -eq 0) { "PACKAGED ($([int]$sw.Elapsed.TotalSeconds)s)" } else { Show-Failure $log; "PACKAGE FAILED ($code)" }
        continue
    }

    $h = Get-HostInfo $ver
    Write-Step "UE $ver  engine=$engine  host=$($h.Dir)"
    Initialize-HostProject $h $ver

    if (Test-EditorRunning $h) {
        Write-Host "Editor is open on $($h.Name). Live Coding blocks external builds - use Ctrl+Alt+F11 in the editor, or close it." -ForegroundColor Yellow
        $results[$ver] = 'SKIPPED (editor open)'; continue
    }
    if ($Clean) {
        foreach ($d in @('Binaries', 'Intermediate')) { Remove-Item (Join-Path $h.PluginDir $d) -Recurse -Force -ErrorAction SilentlyContinue }
    }

    $log = Join-Path $LogRoot "Build_${ver}_$stamp.log"
    $code = Invoke-Logged (Get-EngineTool $engine 'Build') @(
        $h.Target, $Platform, $Config, "-Project=$($h.UProject)", '-WaitMutex', '-FromMsBuild', '-NoHotReloadFromIDE') $log
    $secs = [int]$sw.Elapsed.TotalSeconds
    if ($code -ne 0) { Show-Failure $log; $results[$ver] = "FAILED ($code, ${secs}s)"; continue }
    $warn = @(Select-String -Path $log -Pattern 'NodeToCode.*\bwarning\b').Count
    $results[$ver] = "OK (${secs}s, $warn plugin warnings)"

    if ($Smoke) {
        $ok = Invoke-Smoke $h $engine $ver $stamp
        $results[$ver] += if ($ok) { ', smoke PASS' } else { ', smoke FAIL' }
    }
    if ($Test) {
        $results[$ver] += ', ' + (Invoke-Tests $h $engine $ver $stamp)
    }
    if ($Launch) {
        Write-Step "Launching UE $ver editor"
        Start-Process (Get-EngineTool $engine 'Editor') -ArgumentList "`"$($h.UProject)`""
    }
}

Write-Host "`n===== Summary =====" -ForegroundColor Cyan
foreach ($k in $results.Keys) {
    $r = $results[$k]
    $color = if ($r -notmatch '^OK|^PACKAGED' -or $r -match 'FAIL') { 'Red' } elseif ($r -match 'WARN') { 'Yellow' } else { 'Green' }
    Write-Host ("  UE {0}: {1}" -f $k, $r) -ForegroundColor $color
}
if (@($results.Values | Where-Object { $_ -notmatch '^OK|^PACKAGED' -or $_ -match 'FAIL' }).Count -gt 0) { exit 1 }
exit 0
