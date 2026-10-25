<#
.SYNOPSIS
    Build NodeToCode release packages (one zip per engine) from a clean `git archive` of a commit.

.DESCRIPTION
    Nothing from the working tree goes into a release: the script exports <Ref> with `git archive` into
    <OutDir>\staging\<sha12>\NodeToCode, so uncommitted, untracked and ignored files (.env, BuildLogs, __pycache__,
    Binaries, ...) cannot leak. For each engine (UE 5.6, 5.7, 5.8 by default) it then:

      1. runs RunUAT BuildPlugin on the staged copy -> <OutDir>\NodeToCode_<VersionName>_UE<ver>
         (-TargetPlatforms is the union of the .uplugin modules' PlatformAllowList);
      2. zips the package, without Intermediate\ (and without *.pdb unless -KeepPdb), as
         <OutDir>\NodeToCode-<VersionName>-UE<ver>.zip with one top-level NodeToCode\ folder;
      3. unzips it again and verifies what was zipped: required files present, developer files absent,
         every committed Source file present, the .uplugin fields (Version, VersionName, EngineVersion <ver>.0,
         Installed true, platform allowlist), and no secrets (no .env or secrets.json file, no provider key value
         from the repo's .env via Tests/mcp_probe/env_keys.py, and no text that looks like a provider key);
      4. rebuild from source: copies the package without Binaries\ and Intermediate\ into a scratch C++ host
         under the staging folder (outside the repo and outside ..\Hosts) and runs Build.bat on it;
      5. smoke (unless -SkipSmoke): copies the unzipped package as is, prebuilt Binaries included, into a
         Blueprint-only scratch host, starts a headless editor with -N2CMcp and checks that NodeToCode mounts from
         there and its MCP server answers tools/list (Scripts/release_smoke.py).

    Finally it writes <OutDir>\NodeToCode-<VersionName>-SHA256SUMS.txt for the zips it made, deletes the staging
    folder (unless -KeepStaging) and exits non-zero if any step failed for any engine.

    The script never tags, uploads or publishes anything. Logs go to <OutDir>\logs\<sha12>\.

.PARAMETER Ref
    Commit, branch or tag to package (default HEAD). Uncommitted changes are never included.

.PARAMETER UEVersion
    Engines to package (default 5.6, 5.7 and 5.8). Engines are found from the registry, the Epic launcher's
    LauncherInstalled.dat, or $env:N2C_UE_<major>_<minor> (for example N2C_UE_5_8=D:\UE\UE_5.8).

.PARAMETER OutDir
    Where packages, zips, checksums, logs and staging go (default ..\Packages next to the repo). Must be outside
    the repository.

.PARAMETER RemoveFromStaging
    Self-test hook: delete these paths (relative to the plugin root) from the staged copy before building, to
    prove the checks fail. For example -RemoveFromStaging LICENSE must make the script exit non-zero.

.EXAMPLE
    .\Scripts\package-release.ps1                          # HEAD, UE 5.6 + 5.7 + 5.8
    .\Scripts\package-release.ps1 -Ref v2.0.0-beta.1 -UEVersion 5.8
    .\Scripts\package-release.ps1 -UEVersion 5.8 -SkipRebuild -SkipSmoke -RemoveFromStaging LICENSE   # must fail
#>
[CmdletBinding()]
param(
    [string]$Ref = 'HEAD',
    [ValidateSet('5.6', '5.7', '5.8')]
    [string[]]$UEVersion = @('5.6', '5.7', '5.8'),
    [string]$OutDir,
    [switch]$KeepPdb,
    [switch]$SkipRebuild,
    [switch]$SkipSmoke,
    [switch]$KeepStaging,
    [string[]]$RemoveFromStaging = @()
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $OutDir) { $OutDir = Join-Path (Split-Path $RepoRoot -Parent) 'Packages' }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path

# --- Rules -------------------------------------------------------------------------------------------------------
# Files every package must contain (relative to the plugin root).
$RequiredFiles = @(
    'NodeToCode.uplugin',
    'LICENSE',
    'NOTICE',
    'Config/FilterPlugin.ini',
    'ThirdParty/cpp-httplib/httplib.h',
    'ThirdParty/cpp-httplib/LICENSE',
    'Source/NodeToCode.Build.cs',
    'Resources/Icon128.png',
    'Content/Prompting/CodeGen_CPP.md',
    'Content/Prompting/Shared/N2CJsonSpec.md',
    'Content/Prompting/Shared/N2CResponseFormat.md',
    'Content/Python/mcp_bridge/nodetocode_bridge.py',
    'Binaries/Win64/UnrealEditor-NodeToCode.dll',
    'Binaries/Win64/UnrealEditor.modules'
)
# Root-level files and folders that must not ship (prefix match on the relative path, case-sensitive).
$ForbiddenPrefixes = @(
    '.git/', '.github/', '.agents/', '.claude/', '.vs/', '.idea/', 'BuildLogs/', 'Tests/', 'Intermediate/',
    'Saved/', 'DerivedDataCache/', 'Context/', 'wiki/', 'tools/', 'assets/', 'Scripts/', 'Documentation/',
    'Content/Python/tests/', 'Content/Scripts/', 'AGENTS.md', 'README.md', 'CONTRIBUTING.md', 'SECURITY.md'
)
# File or folder names that must not appear anywhere. (Source/**/AGENTS.md, the MCP tool docs, ship with Source.)
$ForbiddenSegments = @('__pycache__', '.git', '.env', 'secrets.json', 'CLAUDE.md', '.mcp.json',
                       '.gitignore', '.gitattributes')
$ForbiddenPatterns = @('*.pyc', '*.ps1', '.env.*', '*.log')
if (-not $KeepPdb) { $ForbiddenPatterns += '*.pdb' }
# Text that looks like a provider key (checked in text files only).
$KeyRegex = 'sk-ant-[A-Za-z0-9_\-]{20,}|sk-(proj-)?[A-Za-z0-9_\-]{32,}|AIza[0-9A-Za-z_\-]{35}'
$TextExtensions = @('.h', '.hpp', '.cpp', '.cs', '.py', '.md', '.txt', '.json', '.ini', '.uplugin', '.bat', '.sh',
                    '.modules', '')

# --- Helpers -----------------------------------------------------------------------------------------------------
function Write-Step($msg) { Write-Host "==> $msg" -ForegroundColor Cyan }
function Write-Ok($msg)   { Write-Host "    OK   $msg" -ForegroundColor Green }
function Write-Bad($msg)  { Write-Host "    FAIL $msg" -ForegroundColor Red }

function Find-Engine([string]$ver) {
    $envName = 'N2C_UE_' + $ver.Replace('.', '_')
    $fromEnv = [Environment]::GetEnvironmentVariable($envName)
    $candidates = @()
    if ($fromEnv) { $candidates += $fromEnv }
    $key = "HKLM:\SOFTWARE\EpicGames\Unreal Engine\$ver"
    if (Test-Path $key) { $candidates += (Get-ItemProperty $key).InstalledDirectory }
    $dat = 'C:\ProgramData\Epic\UnrealEngineLauncher\LauncherInstalled.dat'
    if (Test-Path $dat) {
        $hit = (Get-Content $dat -Raw | ConvertFrom-Json).InstallationList |
            Where-Object { $_.AppName -eq "UE_$ver" } | Select-Object -First 1
        if ($hit) { $candidates += $hit.InstallLocation }
    }
    foreach ($c in $candidates) {
        if ($c -and (Test-Path (Join-Path $c 'Engine\Build\BatchFiles\RunUAT.bat'))) { return $c.TrimEnd('\') }
    }
    return $null
}

# Run a tool, write everything to $log, echo only the lines worth seeing. Returns the exit code.
function Invoke-Logged([string]$exe, [string[]]$argList, [string]$log) {
    $quoted = $argList | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }
    Write-Host "    $exe $($quoted -join ' ')" -ForegroundColor DarkGray
    & cmd.exe /c "`"$exe`" $($quoted -join ' ') 2>&1" | Tee-Object -FilePath $log | ForEach-Object {
        if ($_ -match '\berror\b|: fatal|BUILD (SUCCESSFUL|FAILED)|AutomationTool exiting|Result: |Total execution time') {
            $color = if ($_ -match '\berror\b|: fatal|FAILED') { 'Red' } else { 'DarkGray' }
            Write-Host "    | $_" -ForegroundColor $color
        }
    }
    return $LASTEXITCODE
}

function Get-RelativeFiles([string]$root) {
    $rootFull = (Resolve-Path $root).Path.TrimEnd('\') + '\'
    Get-ChildItem -LiteralPath $root -Recurse -Force -File | ForEach-Object {
        [pscustomobject]@{ Rel = $_.FullName.Substring($rootFull.Length).Replace('\', '/'); Full = $_.FullName; Length = $_.Length }
    }
}

function Test-ZipEntry([string]$rel) {
    if ($rel -like 'Intermediate/*') { return $false }
    if (-not $KeepPdb -and $rel -like '*.pdb') { return $false }
    return $true
}

function New-PluginZip([string]$packageDir, [string]$zipPath) {
    if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
    $files = @(Get-RelativeFiles $packageDir | Where-Object { Test-ZipEntry $_.Rel } | Sort-Object Rel)
    $zip = [IO.Compression.ZipFile]::Open($zipPath, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($f in $files) {
            [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $f.Full, "NodeToCode/$($f.Rel)",
                [IO.Compression.CompressionLevel]::Optimal)
        }
    } finally { $zip.Dispose() }
    return $files.Count
}

function Expand-PluginZip([string]$zipPath, [string]$dest) {
    if (Test-Path $dest) { Remove-Item $dest -Recurse -Force }
    $zip = [IO.Compression.ZipFile]::OpenRead($zipPath)
    try {
        $bad = @($zip.Entries | Where-Object { -not $_.FullName.StartsWith('NodeToCode/') -or $_.FullName -match '\\|\.\./' })
        if ($bad.Count) { throw "zip has entries outside NodeToCode/: $($bad[0].FullName)" }
    } finally { $zip.Dispose() }
    [IO.Compression.ZipFile]::ExtractToDirectory($zipPath, $dest)
    return (Join-Path $dest 'NodeToCode')
}

# Returns a list of failure strings (empty when the package is good).
function Test-Package([string]$pluginDir, $sourceDescriptor, [string]$ver, [string[]]$sourceFiles) {
    $fail = New-Object System.Collections.Generic.List[string]
    $files = @(Get-RelativeFiles $pluginDir)
    $rels = @{}
    foreach ($f in $files) { $rels[$f.Rel] = $true }

    foreach ($r in $RequiredFiles) { if (-not $rels.ContainsKey($r)) { $fail.Add("missing required file $r") } }
    $missingSrc = @($sourceFiles | Where-Object { -not $rels.ContainsKey($_) })
    if ($missingSrc.Count) { $fail.Add("missing $($missingSrc.Count) committed Source files, e.g. $($missingSrc[0])") }

    foreach ($f in $files) {
        $rel = $f.Rel
        $segments = $rel.Split('/')
        $name = $segments[-1]
        foreach ($p in $ForbiddenPrefixes) { if ($rel.StartsWith($p, [StringComparison]::Ordinal)) { $fail.Add("forbidden path $rel"); break } }
        foreach ($s in $ForbiddenSegments) { if ($segments -ccontains $s) { $fail.Add("forbidden name $rel"); break } }
        foreach ($p in $ForbiddenPatterns) { if ($name -like $p) { $fail.Add("forbidden file $rel"); break } }
        if ($TextExtensions -contains [IO.Path]::GetExtension($name).ToLowerInvariant() -and $f.Length -lt 20MB) {
            $text = [IO.File]::ReadAllText($f.Full)
            if ($text -match $KeyRegex) { $fail.Add("text that looks like a provider key in $rel") }
        }
    }

    # The .uplugin BuildPlugin wrote
    $upPath = Join-Path $pluginDir 'NodeToCode.uplugin'
    if (Test-Path $upPath) {
        $d = Get-Content $upPath -Raw | ConvertFrom-Json
        $checks = [ordered]@{
            'Version'       = @($d.Version, $sourceDescriptor.Version)
            'VersionName'   = @($d.VersionName, $sourceDescriptor.VersionName)
            'EngineVersion' = @($(if ($d.PSObject.Properties['EngineVersion']) { $d.EngineVersion } else { '(none)' }), "$ver.0")
            'Installed'     = @($d.Installed, $true)
            'Modules'       = @((($d.Modules | ForEach-Object { "$($_.Name):$($_.Type):$((@($_.PlatformAllowList)) -join '+')" }) -join ','),
                                (($sourceDescriptor.Modules | ForEach-Object { "$($_.Name):$($_.Type):$((@($_.PlatformAllowList)) -join '+')" }) -join ','))
        }
        foreach ($k in $checks.Keys) {
            $got, $want = $checks[$k]
            if ("$got" -ne "$want") { $fail.Add(".uplugin $k is '$got', expected '$want'") }
            else { Write-Host "    .uplugin $k = $got" -ForegroundColor DarkGray }
        }
    }

    # Provider key values from the repo's .env (names only are printed)
    $envFile = Join-Path $RepoRoot '.env'
    $envKeys = Join-Path $RepoRoot 'Tests\mcp_probe\env_keys.py'
    if (-not (Test-Path $envKeys)) { $fail.Add("secret scan: $envKeys not found") }
    else {
        $json = & python -I $envKeys --env-file $envFile --check $pluginDir
        $code = $LASTEXITCODE
        $report = ($json -join "`n") | ConvertFrom-Json
        $leaks = @($report.leaks)
        if ($code -ne 0 -or $leaks.Count) {
            foreach ($l in $leaks) { $fail.Add("key value of $($l.variable) found in $($l.path)") }
            if (-not $leaks.Count) { $fail.Add("secret scan failed (env_keys.py exit $code)") }
        } else {
            $n = @($report.variables).Count
            if ($n) { Write-Host "    secret scan: no value of $n provider key(s) from .env found" -ForegroundColor DarkGray }
            else { Write-Host "    secret scan: no .env keys to compare against (pattern scan only)" -ForegroundColor Yellow }
        }
    }
    return ,$fail
}

function New-ScratchHost([string]$dir, [string]$name, [string]$ver, [bool]$withCode) {
    New-Item -ItemType Directory -Force $dir | Out-Null
    $modules = if ($withCode) { "`"Modules`": [ { `"Name`": `"$name`", `"Type`": `"Runtime`", `"LoadingPhase`": `"Default`" } ]," } else { '' }
    $uproject = "{ `"FileVersion`": 3, `"EngineAssociation`": `"$ver`", $modules `"Plugins`": [ { `"Name`": `"NodeToCode`", `"Enabled`": true } ] }"
    $utf8 = New-Object Text.UTF8Encoding($false)
    [IO.File]::WriteAllText((Join-Path $dir "$name.uproject"), $uproject, $utf8)
    New-Item -ItemType Directory -Force (Join-Path $dir 'Config') | Out-Null
    [IO.File]::WriteAllText((Join-Path $dir 'Config\DefaultEngine.ini'), "[/Script/EngineSettings.GameMapsSettings]`r`nGameDefaultMap=/Engine/Maps/Templates/OpenWorld`r`n", $utf8)
    if ($withCode) {
        foreach ($kind in @('Game', 'Editor')) {
            $t = if ($kind -eq 'Editor') { "${name}Editor" } else { $name }
            New-Item -ItemType Directory -Force (Join-Path $dir 'Source') | Out-Null
            [IO.File]::WriteAllText((Join-Path $dir "Source\$t.Target.cs"), @"
using UnrealBuildTool;
public class ${t}Target : TargetRules
{
	public ${t}Target(TargetInfo Target) : base(Target)
	{
		Type = TargetType.$kind;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("$name");
	}
}
"@, $utf8)
        }
        New-Item -ItemType Directory -Force (Join-Path $dir "Source\$name") | Out-Null
        [IO.File]::WriteAllText((Join-Path $dir "Source\$name\$name.Build.cs"), @"
using UnrealBuildTool;
public class $name : ModuleRules
{
	public $name(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
	}
}
"@, $utf8)
        [IO.File]::WriteAllText((Join-Path $dir "Source\$name\$name.cpp"),
            "#include `"Modules/ModuleManager.h`"`r`nIMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, $name, `"$name`");`r`n", $utf8)
    }
    return (Join-Path $dir "$name.uproject")
}

function Copy-Tree([string]$from, [string]$to, [string[]]$skipTop) {
    New-Item -ItemType Directory -Force $to | Out-Null
    Get-ChildItem -LiteralPath $from -Force | Where-Object { $skipTop -notcontains $_.Name } |
        ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $to -Recurse -Force }
}

# --- Resolve the commit and stage it ---------------------------------------------------------------------------
if ($OutDir.StartsWith($RepoRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase) -or $OutDir -eq $RepoRoot) {
    throw "OutDir $OutDir is inside the repository; pick a folder outside it."
}
if ($OutDir.Length -gt 80) {
    # BuildPlugin's HostProject intermediates add ~175 characters; UBT refuses action paths over 260.
    Write-Host "OutDir is $($OutDir.Length) characters; BuildPlugin may fail on paths over 260. Prefer a shorter -OutDir." -ForegroundColor Yellow
}
$sha = (& git -C $RepoRoot rev-parse --verify --quiet "$Ref^{commit}")
if ($LASTEXITCODE -ne 0 -or -not $sha) { throw "Unknown ref '$Ref'" }
$sha = "$sha".Trim()
$short = $sha.Substring(0, 12)
$StageRoot = Join-Path $OutDir "staging\$short"
$Staged    = Join-Path $StageRoot 'NodeToCode'
$LogDir    = Join-Path $OutDir "logs\$short"
New-Item -ItemType Directory -Force $LogDir | Out-Null

Write-Step "Staging $Ref ($sha) with git archive -> $Staged"
if (Test-Path $StageRoot) { Remove-Item $StageRoot -Recurse -Force }
New-Item -ItemType Directory -Force $Staged | Out-Null
$tarFile = Join-Path $StageRoot 'source.tar'
# Fixed eol settings so the archive does not depend on the packager's core.autocrlf.
& git -C $RepoRoot -c core.autocrlf=false -c core.eol=lf archive --format=tar -o $tarFile $sha
if ($LASTEXITCODE -ne 0) { throw 'git archive failed' }
& (Join-Path $env:SystemRoot 'System32\tar.exe') -xf $tarFile -C $Staged
if ($LASTEXITCODE -ne 0) { throw 'tar extract failed' }
Remove-Item $tarFile -Force
foreach ($r in $RemoveFromStaging) {
    $p = Join-Path $Staged $r
    if (Test-Path $p) { Remove-Item $p -Recurse -Force; Write-Host "    self-test: removed $r from the staged copy" -ForegroundColor Yellow }
    else { Write-Host "    self-test: $r not in the staged copy" -ForegroundColor Yellow }
}

$upStaged = Join-Path $Staged 'NodeToCode.uplugin'
$descriptor = Get-Content $upStaged -Raw | ConvertFrom-Json
$VersionName = $descriptor.VersionName
$platforms = @($descriptor.Modules | ForEach-Object { @($_.PlatformAllowList) } | Where-Object { $_ } | Sort-Object -Unique)
if (-not $platforms.Count) { $platforms = @('Win64') }
$sourceFiles = @(& git -C $RepoRoot ls-tree -r --name-only $sha -- Source)
Write-Host "    VersionName $VersionName, Version $($descriptor.Version), target platforms $($platforms -join '+'), $($sourceFiles.Count) Source files"

# --- Per engine ------------------------------------------------------------------------------------------------
$results = [ordered]@{}
$made = New-Object System.Collections.Generic.List[string]
foreach ($ver in $UEVersion) {
    $res = [ordered]@{ Zip = '-'; Size = '-'; Sha256 = '-'; Verify = 'not run'; Rebuild = 'not run'; Smoke = 'not run'; Failed = $false }
    $results[$ver] = $res
    $tag = $ver.Replace('.', '_')
    $engine = Find-Engine $ver
    if (-not $engine) { Write-Bad "UE $ver not found"; $res.Verify = 'NO ENGINE'; $res.Failed = $true; continue }

    # 1. BuildPlugin
    $pkgDir = Join-Path $OutDir "NodeToCode_${VersionName}_UE$ver"
    $sw = [Diagnostics.Stopwatch]::StartNew()
    Write-Step "UE ${ver}: BuildPlugin -> $pkgDir"
    $code = Invoke-Logged (Join-Path $engine 'Engine\Build\BatchFiles\RunUAT.bat') @(
        'BuildPlugin', "-Plugin=$upStaged", "-Package=$pkgDir", "-TargetPlatforms=$($platforms -join '+')", '-unattended'
    ) (Join-Path $LogDir "BuildPlugin_UE$tag.log")
    if ($code -ne 0) { Write-Bad "BuildPlugin exit $code (log $LogDir\BuildPlugin_UE$tag.log)"; $res.Verify = "BUILDPLUGIN FAILED ($code)"; $res.Failed = $true; continue }
    Write-Ok "BuildPlugin ($([int]$sw.Elapsed.TotalSeconds)s)"

    # 2. Zip
    $zipName = "NodeToCode-$VersionName-UE$ver.zip"
    $zipPath = Join-Path $OutDir $zipName
    $count = New-PluginZip $pkgDir $zipPath
    $res.Zip = $zipName
    $res.Size = '{0:N1} MB' -f ((Get-Item $zipPath).Length / 1MB)
    Write-Ok "zipped $count files -> $zipName ($($res.Size))"

    # 3. Verify what is in the zip
    Write-Step "UE ${ver}: verify $zipName"
    $unzipped = Expand-PluginZip $zipPath (Join-Path $StageRoot "unzipped_UE$tag")
    $fail = Test-Package $unzipped $descriptor $ver $sourceFiles
    if ($fail.Count) {
        $fail | Select-Object -First 40 | ForEach-Object { Write-Bad $_ }
        $res.Verify = "FAIL ($($fail.Count))"; $res.Failed = $true
        Remove-Item $zipPath -Force
        $res.Zip = "$zipName (deleted)"
        continue
    }
    $res.Verify = 'PASS'
    Write-Ok 'required files present, forbidden files absent, Source complete, .uplugin fields right, no secrets'

    # 4. Rebuild from source in a scratch C++ host
    if (-not $SkipRebuild) {
        Write-Step "UE ${ver}: rebuild from source (package without Binaries and Intermediate)"
        $hostDir = Join-Path $StageRoot "RebuildHost_UE$tag"
        $hostName = 'N2CRebuildHost'
        $uproject = New-ScratchHost $hostDir $hostName $ver $true
        $plugin = Join-Path $hostDir 'Plugins\NodeToCode'
        Copy-Tree $unzipped $plugin @('Binaries', 'Intermediate')
        $sw = [Diagnostics.Stopwatch]::StartNew()
        $code = Invoke-Logged (Join-Path $engine 'Engine\Build\BatchFiles\Build.bat') @(
            "${hostName}Editor", 'Win64', 'Development', "-Project=$uproject", '-WaitMutex', '-NoHotReloadFromIDE'
        ) (Join-Path $LogDir "Rebuild_UE$tag.log")
        $dll = Join-Path $plugin 'Binaries\Win64\UnrealEditor-NodeToCode.dll'
        if ($code -eq 0 -and (Test-Path $dll)) { $res.Rebuild = "OK ($([int]$sw.Elapsed.TotalSeconds)s)"; Write-Ok "rebuilt NodeToCode from source ($([int]$sw.Elapsed.TotalSeconds)s)" }
        else { $res.Rebuild = "FAIL (exit $code, dll built: $(Test-Path $dll))"; $res.Failed = $true; Write-Bad "rebuild: $($res.Rebuild), log $LogDir\Rebuild_UE$tag.log" }
    }

    # 5. Load the unzipped package (prebuilt binaries) in a Blueprint-only host and ask its MCP server for tools
    if (-not $SkipSmoke) {
        Write-Step "UE ${ver}: smoke (installed package in a Blueprint-only host, headless editor, MCP tools/list)"
        $hostDir = Join-Path $StageRoot "SmokeHost_UE$tag"
        $uproject = New-ScratchHost $hostDir 'N2CSmokeHost' $ver $false
        $plugin = Join-Path $hostDir 'Plugins\NodeToCode'
        Copy-Tree $unzipped $plugin @()
        $out = & python -I (Join-Path $PSScriptRoot 'release_smoke.py') --engine $engine --project $uproject `
            --plugin-dir $plugin --log (Join-Path $LogDir "Smoke_UE$tag.log")
        $code = $LASTEXITCODE
        Write-Host "    $($out -join ' ')" -ForegroundColor DarkGray
        if ($code -eq 0) { $res.Smoke = 'PASS'; Write-Ok 'NodeToCode loaded from the package and the MCP server answered tools/list' }
        else { $res.Smoke = "FAIL ($code)"; $res.Failed = $true; Write-Bad "smoke failed, log $LogDir\Smoke_UE$tag.log" }
    }

    if (-not $res.Failed) {
        $res.Sha256 = (Get-FileHash -Algorithm SHA256 $zipPath).Hash.ToLowerInvariant()
        $made.Add("$($res.Sha256)  $zipName")
    }
}

# --- Checksums, cleanup, summary -------------------------------------------------------------------------------
if ($made.Count) {
    $sums = Join-Path $OutDir "NodeToCode-$VersionName-SHA256SUMS.txt"
    [IO.File]::WriteAllText($sums, (($made | Sort-Object) -join "`n") + "`n", (New-Object Text.UTF8Encoding($false)))
    Write-Step "Checksums -> $sums"
}
if (-not $KeepStaging) {
    Write-Step "Removing staging folder $StageRoot"
    Remove-Item $StageRoot -Recurse -Force -ErrorAction SilentlyContinue
    if (Test-Path $StageRoot) { Write-Host "    could not remove all of $StageRoot (a process may still hold a file)" -ForegroundColor Yellow }
}

Write-Host "`n===== NodeToCode $VersionName from $short =====" -ForegroundColor Cyan
foreach ($ver in $results.Keys) {
    $r = $results[$ver]
    $color = if ($r.Failed) { 'Red' } else { 'Green' }
    Write-Host ("  UE {0}: {1}  {2}  verify {3}, rebuild {4}, smoke {5}" -f $ver, $r.Zip, $r.Size, $r.Verify, $r.Rebuild, $r.Smoke) -ForegroundColor $color
    if ($r.Sha256 -ne '-') { Write-Host "         sha256 $($r.Sha256)" -ForegroundColor DarkGray }
}
Write-Host "  logs: $LogDir"
if (@($results.Values | Where-Object { $_.Failed }).Count) { exit 1 }
exit 0
