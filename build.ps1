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

    UnrealBuildTool logs to BuildLogs/UBT_<ver>_<stamp>.txt (-Log=), not the shared per-user UnrealBuildTool/Log.txt,
    so builds from several checkouts can start together. A build that still fails at once on a shared file another
    UBT holds (a sharing-violation IOException, e.g. on UE 5.8's UnrealBuildTool/Trace.uba or a BuildRules DLL) is
    retried up to 20 times after a random 5-30 s wait (an attempt that hits it fails in about a second); the failed
    attempt's log is kept as Build_<ver>_<stamp>_try<n>.log.
    A version is skipped only when an UnrealEditor is open on this run's own host .uproject (full path compared), not
    another checkout's host.

    Release packages come from Scripts\package-release.ps1, not from -Package (a quick local BuildPlugin check).

    -Smoke runs Tests/smoke/smoke_check.py with the engine's own Python. It starts the host editor headless with
    -N2CMcp once per MCP tool-registration mode (default, dynamic discovery, Python script-only) and fails on an error,
    ensure, assertion or crash line in the editor log (minus a commented allowlist), a missing MCP start line or
    /mcp/health answer from that editor, a tools/list count that differs from Tests/smoke/expected.json, any change to
    the host's Config/*.ini during a session, a failed in-editor get_focused_blueprint_json on an engine Blueprint, or
    a Tests/mcp_probe probe status that differs from Tests/mcp_probe/expected.json. In the default-mode session it
    also runs the official MCP conformance suite and the MCP Inspector CLI (tools/list), both pinned in
    Tests/conformance/baseline.json and fetched by npx (needs Node.js), and fails when a baseline scenario fails or a
    failure is not listed there; a listed failure that starts passing prints a NOW PASSES reminder. The summary shows
    the tool count per mode, the probe statuses, the conformance pass and fail counts and the config check. Editor
    logs: BuildLogs/Smoke_<ver>_<stamp>_<mode>.log; conformance and Inspector JSON:
    BuildLogs/Smoke_<ver>_<stamp>_conformance/.

    -Test first runs Tests/python (pytest, Content/Python logic with a stub `unreal` module, no editor) on the engine's
    own Python through `uv run` with the pinned Tests/python/requirements.txt (needs uv on PATH; without it the step is
    a WARN), then Automation RunTests <TestFilter> in a headless editor. Logs: BuildLogs/PyTest_<ver>_<stamp>.log and
    BuildLogs/Tests_<ver>_<stamp>.log plus its report folder. The editor runs with -culture=en (-TestCulture changes
    it, for a locale check of the capture goldens). -UpdateGoldens passes -N2CUpdateGoldens, so the golden specs
    (NodeToCode.Capture.Golden, NodeToCode.LLM.Payload.Golden) rewrite their goldens instead of comparing; review and
    commit the diff with the change that caused it. On UE 5.8 and later that editor also gets
    -EnablePlugins=ToolsetRegistry (the host .uproject is not changed), so Content/Python/init_unreal.py registers the
    NodeToCodeTools toolset and its in-editor Python tests, NodeToCode.Toolsets.PythonTests.*: they run in the same
    pass, are counted apart in the summary ('tests PASS (n, m toolset)'), and when the filter covers them and none ran
    (registration failed) the step fails.

.EXAMPLE
    .\build.ps1 -UEVersion 5.7              # incremental editor build for 5.7
    .\build.ps1 -All -Smoke                 # build 5.6, 5.7, 5.8, then the headless smoke check (see -Smoke)
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
    [switch]$Smoke,       # after build: headless smoke check per MCP mode plus the MCP probes (Tests/smoke/smoke_check.py)
    [switch]$Test,        # after build: Tests/python (pytest, engine Python via uv), then Automation RunTests <TestFilter>
    [switch]$Launch,      # after build: open the editor on the host project
    [switch]$Package,     # RunUAT BuildPlugin instead of the incremental host build
    [string]$Config = 'Development',
    [string[]]$Engine,    # per-version engine override: '5.8=<path>' (repeatable)
    [string]$HostsRoot,   # default: <checkout>\..\Hosts
    [string]$PackagesRoot,# default: <checkout>\..\Packages
    [string]$LogRoot,     # default: <checkout>\BuildLogs
    [string]$TestFilter = 'NodeToCode',
    [int]$TestTimeoutMinutes = 60,
    [string]$TestCulture = 'en', # -culture for the -Test editor (the goldens are recorded with en)
    [switch]$UpdateGoldens       # -Test: rewrite the golden files (-N2CUpdateGoldens) instead of comparing
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
        'Python' {
            if ($OnWindows) { 'Engine\Binaries\ThirdParty\Python3\Win64\python.exe' }
            else { "Engine/Binaries/ThirdParty/Python3/$Platform/bin/python3" } }
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
		{ "Name": "NodeToCode", "Enabled": true },
		{ "Name": "AndroidFileServer", "Enabled": false }
	]
}
"@
    # AndroidFileServer is off because on first start it writes a generated SecurityToken into Config/DefaultEngine.ini,
    # which the template below then resets on every run; the smoke check fails on any Config/*.ini the editor changes
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

# Path key for comparing paths: forward slashes, repeated slashes collapsed (a leading '//' UNC prefix kept), and
# lower case where the file system is case-insensitive (Windows, and macOS by default)
function ConvertTo-PathKey([string]$path) {
    $p = $path.Replace('\', '/')
    $unc = $p.StartsWith('//')
    $p = [regex]::Replace($p, '/{2,}', '/')
    if ($unc) { $p = '/' + $p }
    if ($OnWindows -or $OnMac) { $p = $p.ToLowerInvariant() }
    return $p
}

# True when a process command line names this exact .uproject path: the full, normalized path, bounded by the
# start or end of the line, a quote, whitespace, '=' or a NUL (Linux argv separator). Matching only the file name
# would make every lane's host of one version (same NodeToCodeHost_5_x.uproject name) look open.
function Test-CommandLineHasPath([string]$commandLine, [string]$path) {
    if (-not $commandLine -or -not $path) { return $false }
    $pattern = '(?:^|["''\s=\x00])' + [regex]::Escape((ConvertTo-PathKey $path)) + '(?:$|["''\s\x00])'
    return [regex]::IsMatch((ConvertTo-PathKey $commandLine), $pattern)
}

# UnrealEditor processes opened on this run's host (Live Coding in them blocks an external build). Other lanes' or
# checkouts' hosts of the same version have the same file name in another folder and do not count.
function Test-EditorRunning($h) {
    $uproject = [IO.Path]::GetFullPath($h.UProject)
    if ($OnWindows) {
        $procs = Get-CimInstance Win32_Process -Filter "Name LIKE 'UnrealEditor%'" -ErrorAction SilentlyContinue
        return @($procs | Where-Object { Test-CommandLineHasPath $_.CommandLine $uproject })
    }
    return @(Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Name -like 'UnrealEditor*' } | Where-Object {
        $cmd = if ($OnMac) { (& ps -o 'args=' -p $_.Id 2>$null) -join ' ' }
               else { Get-Content "/proc/$($_.Id)/cmdline" -Raw -ErrorAction SilentlyContinue }
        Test-CommandLineHasPath $cmd $uproject })
}

# UBT writes its log to -Log=<file> (UE 5.6-5.8) instead of the shared %LOCALAPPDATA%/UnrealBuildTool/Log.txt, which
# two builds started together (parallel checkouts) both try to back up and open. Other per-user UBT files stay
# shared whatever -Log says (UE 5.8 backs up UnrealBuildTool/Trace.uba at startup; every version compiles
# UnrealEngine/Intermediate/Build/BuildRules/*Rules.dll there), so a build that UBT aborts because another process
# holds a file (an unhandled sharing-violation IOException, not a compile error) is retried.
# Installed UE 5.6-5.8 all compile the one <LocalAppData>/UnrealEngine/Intermediate/Build/BuildRules/MarketplaceRules.dll,
# each recompiling the other versions' copy, and a running UBT keeps it loaded for its whole build: the wait can be as
# long as another checkout's build of another version, so the window is minutes (about 6 on average), not seconds.
$UbtSharedFileRetries = 20
function Test-UbtSharedFileCollision([string]$log) {
    if (-not (Test-Path $log)) { return $false }
    $text = [IO.File]::ReadAllText($log).Replace("`0", '')
    return ($text -match 'Unhandled exception: IOException: The process cannot access the file .*being used by another process')
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

# Returns 'smoke PASS (<summary>)' or 'smoke FAIL (<summary or reason>)'
function Invoke-Smoke($h, [string]$engine, [string]$ver, [string]$stamp) {
    $script = Join-Path $PluginRoot 'Tests/smoke/smoke_check.py'
    $log = Join-Path $LogRoot "Smoke_${ver}_$stamp.log"
    $py = Get-EngineTool $engine 'Python'
    Write-Step "Smoke test UE $ver (headless editor per MCP registration mode, MCP probes, MCP conformance, log and config checks)"
    if (-not (Test-Path $py)) { Write-Host "SMOKE FAIL (UE $ver): no engine Python at $py" -ForegroundColor Red; return 'smoke FAIL (no engine Python)' }
    $code = Invoke-Logged $py @('-I', '-u', $script, '--engine', $engine, '--project', $h.UProject, '--label', $ver,
        '--log-dir', $LogRoot, '--log-prefix', "Smoke_${ver}_$stamp") $log
    $text = if (Test-Path $log) { [IO.File]::ReadAllText($log) } else { '' }
    $summary = if ($text -match 'N2C_SMOKE SUMMARY UE [\d.]+: ([^\r\n]*)') { $Matches[1] } else { 'no summary' }
    # A known conformance failure (or an unlisted scenario) that now passes: raise the baseline, not a failure
    foreach ($m in [regex]::Matches($text, '(?m)^(NOW PASSES|NEW) .*$')) { Write-Host "  $($m.Value.Trim())" -ForegroundColor Yellow }
    if ($code -eq 0 -and $text -match 'N2C_SMOKE PASS') {
        Write-Host "SMOKE PASS (UE $ver): $summary" -ForegroundColor Green
        return "smoke PASS ($summary)"
    }
    $failed = @([regex]::Matches($text, '(?m)^FAIL .*$') | ForEach-Object { $_.Value.Trim() })
    Write-Host "SMOKE FAIL (UE $ver, exit $code): $($failed.Count) failed check(s) - see $log" -ForegroundColor Red
    foreach ($f in $failed) { Write-Host "  $f" -ForegroundColor Red }
    return "smoke FAIL ($($failed.Count) failed: $(($failed | ForEach-Object { ($_ -replace '^FAIL ', '') -replace '\s{2}.*$', '' }) -join '; '))"
}

# Returns 'pytest PASS (<counts>)', 'pytest FAIL (<counts or reason>)' or 'pytest WARN (uv not found)'.
# Tests/python runs on plain CPython with a stub `unreal` module (no editor): here on the engine's own Python, through
# a throwaway uv environment that gets the pinned Tests/python/requirements.txt, so the engine install is not changed.
function Invoke-PythonTests([string]$engine, [string]$ver, [string]$stamp) {
    $log = Join-Path $LogRoot "PyTest_${ver}_$stamp.log"
    $py = Get-EngineTool $engine 'Python'
    Write-Step "Python tests UE $ver (Tests/python on the engine's Python, unreal stubbed)"
    $uv = Get-Command uv -ErrorAction SilentlyContinue
    if (-not $uv) { Write-Host "PYTEST WARN (UE $ver): uv is not on PATH, Tests/python not run (https://docs.astral.sh/uv/)" -ForegroundColor Yellow; return 'pytest WARN (uv not found)' }
    if (-not (Test-Path $py)) { Write-Host "PYTEST FAIL (UE $ver): no engine Python at $py" -ForegroundColor Red; return 'pytest FAIL (no engine Python)' }
    $code = Invoke-Logged $uv.Source @('run', '--no-project', '--python', $py,
        '--with-requirements', (Join-Path $PluginRoot 'Tests/python/requirements.txt'), '--',
        'python', '-m', 'pytest', '-q', (Join-Path $PluginRoot 'Tests/python')) $log
    $text = if (Test-Path $log) { [IO.File]::ReadAllText($log) } else { '' }
    # pytest -q ends with e.g. '109 passed, 10 xfailed in 1.21s' (or 'no tests ran in 0.01s')
    $counts = [regex]::Matches($text, '(?m)^=*\s*(\d+ \w+.*?|no tests ran) in [\d.]+s\b')
    $summary = if ($counts.Count -gt 0) { $counts[$counts.Count - 1].Groups[1].Value.Trim() } else { 'no summary' }
    if ($code -eq 0) {
        Write-Host "PYTEST PASS (UE $ver): $summary" -ForegroundColor Green
        return "pytest PASS ($summary)"
    }
    Write-Host "PYTEST FAIL (UE $ver, exit $code): $summary - see $log" -ForegroundColor Red
    return "pytest FAIL ($summary)"
}

# The NodeToCodeTools Python tests: Content/Python/init_unreal.py registers them under this prefix through Epic's
# PythonTestRunner, on UE 5.8+ when the ToolsetRegistry plugin is loaded
$ToolsetTestPrefix = 'NodeToCode.Toolsets.PythonTests'
$ToolsetTestMinEngine = [version]'5.8'

# Returns a short result string: 'tests PASS (n)' ('tests PASS (n, m toolset)' when the toolset tests ran),
# 'tests WARN (0 run)' or 'tests FAIL (...)'
function Invoke-Tests($h, [string]$engine, [string]$ver, [string]$stamp) {
    $reportDir = Join-Path $LogRoot "Tests_${ver}_$stamp"
    $log = Join-Path $LogRoot "Tests_${ver}_$stamp.log"
    New-Item -ItemType Directory -Force $reportDir | Out-Null
    # On 5.8+ the toolset tests run in the same editor: ToolsetRegistry is enabled for this run on the command line
    # only (never in the shared host .uproject). They are expected when the filter is a prefix of their names
    # (RunTests matches on a name substring; 'NodeToCode' and 'NodeToCode.Toolsets' both cover them).
    $withToolset = [version]$ver -ge $ToolsetTestMinEngine
    $expectToolset = $withToolset -and $ToolsetTestPrefix.StartsWith($TestFilter, [StringComparison]::OrdinalIgnoreCase)
    Write-Step ("Automation tests UE $ver ($TestFilter)" + $(if ($withToolset) { ', ToolsetRegistry enabled for the NodeToCodeTools Python tests' } else { '' }))
    # -ExecCmds holds one command; never chain with ';' (it hangs the editor), use ',' if more are ever needed
    $editorArgs = @(
        $h.UProject, "-ExecCmds=`"Automation RunTests $TestFilter`"", '-TestExit="Automation Test Queue Empty"',
        "-ReportExportPath=`"$reportDir`"", "-abslog=`"$log`"",
        '-unattended', '-nullrhi', '-nosplash', '-nosound', '-NoLiveCoding', "-culture=$TestCulture")
    if ($withToolset) { $editorArgs += '-EnablePlugins=ToolsetRegistry' }
    if ($UpdateGoldens) { $editorArgs += '-N2CUpdateGoldens' }
    $code = Invoke-Editor (Get-EngineTool $engine 'EditorCmd') $editorArgs $TestTimeoutMinutes
    if ($null -eq $code) { return 'tests FAIL (timed out)' }

    $index = Join-Path $reportDir 'index.json'
    $logText = if (Test-Path $log) { [IO.File]::ReadAllText($log).Replace("`0", '') } else { '' }
    $noToolsetTests = "tests FAIL (0 toolset tests ran)"
    $noToolsetHint = "no $ToolsetTestPrefix.* test ran: the toolset or its test runner did not register (see 'NodeToCode:' warnings in $log)"
    if (-not (Test-Path $index) -and $logText -match 'No automation tests matched|Automation Test Queue Empty 0 tests performed') {
        # The editor writes no report when the filter matches nothing
        if ($expectToolset) { Write-Host "TESTS FAIL (UE $ver): $noToolsetHint" -ForegroundColor Red; return $noToolsetTests }
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
    $toolset = @($tests | Where-Object { $_.fullTestPath -like "$ToolsetTestPrefix.*" })
    foreach ($t in $failed) {
        Write-Host "  FAIL $($t.fullTestPath)" -ForegroundColor Red
        foreach ($e in @($t.entries | Where-Object { $_.event.type -eq 'Error' } | Select-Object -First 5)) {
            Write-Host "       $($e.event.message)" -ForegroundColor Red
        }
    }
    foreach ($t in $other) { Write-Host "  $($t.state) $($t.fullTestPath)" -ForegroundColor Yellow }
    if ($tests.Count -eq 0) {
        if ($expectToolset) { Write-Host "TESTS FAIL (UE $ver): $noToolsetHint" -ForegroundColor Red; return $noToolsetTests }
        Write-Host "TESTS WARN (UE $ver): zero tests ran for '$TestFilter' - see $log" -ForegroundColor Yellow
        return 'tests WARN (0 run)'
    }
    $toolsetNote = if ($withToolset) { ", $($toolset.Count) toolset" } else { '' }
    if ($failed.Count -gt 0 -or $other.Count -gt 0) {
        Write-Host "TESTS FAIL (UE $ver): $($failed.Count) failed, $($other.Count) not completed, $($passed.Count) passed$toolsetNote - report $reportDir" -ForegroundColor Red
        return "tests FAIL ($($failed.Count) failed, $($other.Count) not completed of $($tests.Count)$toolsetNote)"
    }
    if ($expectToolset -and $toolset.Count -eq 0) {
        # init_unreal.py logs one warning and registers nothing when the toolset or its test runner fails to load
        Write-Host "TESTS FAIL (UE $ver): $noToolsetHint" -ForegroundColor Red
        return "tests FAIL (0 toolset tests ran, $($passed.Count) other passed)"
    }
    Write-Host "TESTS PASS (UE $ver): $($passed.Count) passed$toolsetNote - report $reportDir" -ForegroundColor Green
    return "tests PASS ($($passed.Count)$toolsetNote)"
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
    for ($attempt = 1; ; $attempt++) {
        $ubtLog = Join-Path $LogRoot ("UBT_${ver}_$stamp" + $(if ($attempt -gt 1) { "_$attempt" } else { '' }) + '.txt')
        $code = Invoke-Logged (Get-EngineTool $engine 'Build') @(
            $h.Target, $Platform, $Config, "-Project=$($h.UProject)", '-WaitMutex', '-FromMsBuild', '-NoHotReloadFromIDE',
            "-Log=$ubtLog") $log
        if ($code -eq 0 -or $attempt -gt $UbtSharedFileRetries -or -not (Test-UbtSharedFileCollision $log)) { break }
        $wait = Get-Random -Minimum 5 -Maximum 31
        Write-Host "UBT stopped on a shared file another build holds (IOException: being used by another process); retry $attempt of $UbtSharedFileRetries in ${wait}s" -ForegroundColor Yellow
        Move-Item -Force $log ($log -replace '\.log$', "_try$attempt.log")
        Start-Sleep -Seconds $wait
    }
    $secs = [int]$sw.Elapsed.TotalSeconds
    if ($code -ne 0) { Show-Failure $log; $results[$ver] = "FAILED ($code, ${secs}s)"; continue }
    $warn = @(Select-String -Path $log -Pattern 'NodeToCode.*\bwarning\b').Count
    $results[$ver] = "OK (${secs}s, $warn plugin warnings)"

    if ($Smoke) {
        $smokeWatch = [Diagnostics.Stopwatch]::StartNew()
        $results[$ver] += ', ' + (Invoke-Smoke $h $engine $ver $stamp) + " [$([int]$smokeWatch.Elapsed.TotalSeconds)s]"
    }
    if ($Test) {
        $results[$ver] += ', ' + (Invoke-PythonTests $engine $ver $stamp)
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
    # Case-sensitive: results say FAIL/FAILED/WARN in capitals, while summaries may say '24 fail' or '10 xfailed'
    $color = if ($r -notmatch '^OK|^PACKAGED' -or $r -cmatch 'FAIL') { 'Red' } elseif ($r -cmatch 'WARN') { 'Yellow' } else { 'Green' }
    Write-Host ("  UE {0}: {1}" -f $k, $r) -ForegroundColor $color
}
if (@($results.Values | Where-Object { $_ -notmatch '^OK|^PACKAGED' -or $_ -cmatch 'FAIL' }).Count -gt 0) { exit 1 }
exit 0
