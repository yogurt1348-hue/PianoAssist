<#
.SYNOPSIS
    Removes the Adaptive Follow MVP from FL Studio (Windows).

.DESCRIPTION
    Removes only the files the installer recorded in
    %APPDATA%\AdaptiveFollow\install-manifest.json: the MIDI device script, the
    Piano Roll script, their adaptive_follow_paths.py, the shared module (lib)
    and the tools venv. Folders are removed only if they are empty afterwards.

    Your data (references, config.json, logs, reports, backups) is KEPT unless
    -RemoveData is given (or you answer "y" when asked).

.PARAMETER DryRun
    Show what would be removed without changing anything.

.PARAMETER DataPath
    Adaptive Follow data folder. Default: %APPDATA%\AdaptiveFollow.

.PARAMETER RemoveData
    Also delete references, config.json, logs, reports and backups.

.PARAMETER KeepData
    Never ask; keep the data (default when -NonInteractive).

.PARAMETER RestoreBackups
    Put back the files the installer had overwritten (from backup\<timestamp>).

.PARAMETER NonInteractive
    Never prompt.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\uninstall_windows.ps1 -DryRun
#>
[CmdletBinding()]
param(
    [switch]$DryRun,
    [string]$DataPath = "",
    [switch]$RemoveData,
    [switch]$KeepData,
    [switch]$RestoreBackups,
    [switch]$NonInteractive
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"

function Say([string]$text) { Write-Host $text }
function Step([string]$text) { Write-Host ""; Write-Host "==> $text" -ForegroundColor Cyan }
function Warn([string]$text) { Write-Host "WARNING: $text" -ForegroundColor Yellow }
function Fail([string]$text) { Write-Host "ERROR: $text" -ForegroundColor Red; exit 1 }
function DryNote([string]$text) { Write-Host "  [dry-run] would $text" -ForegroundColor DarkGray }

function Get-Prop($obj, [string]$name) {
    if ($null -eq $obj) { return $null }
    $p = $obj.PSObject.Properties[$name]
    if ($p) { return $p.Value }
    return $null
}

function Remove-FileSafe([string]$path) {
    if (-not $path -or -not (Test-Path -LiteralPath $path -PathType Leaf)) { return }
    if ($DryRun) { DryNote "delete $path"; return }
    Remove-Item -LiteralPath $path -Force
    Say "  deleted: $path"
}

function Remove-DirIfEmpty([string]$path) {
    if (-not $path -or -not (Test-Path -LiteralPath $path -PathType Container)) { return }
    if ($DryRun) { DryNote "remove folder $path if it is empty by then"; return }
    $left = @(Get-ChildItem -LiteralPath $path -Force | Where-Object { $_.Name -ne "__pycache__" })
    if ($left.Count -gt 0) { Say "  kept (not empty): $path"; return }
    Remove-Item -LiteralPath $path -Recurse -Force
    Say "  removed folder: $path"
}

function Remove-Tree([string]$path, [string]$mustEndWith) {
    # Recursive delete, guarded: only folders that belong to Adaptive Follow.
    if (-not $path -or -not (Test-Path -LiteralPath $path -PathType Container)) { return }
    $full = (Resolve-Path -LiteralPath $path).Path.TrimEnd('\', '/')
    if (-not $full.EndsWith($mustEndWith)) { Warn "refusing to delete $full (unexpected location)"; return }
    if ($DryRun) { DryNote "delete folder $full (recursive)"; return }
    Remove-Item -LiteralPath $full -Recurse -Force
    Say "  deleted folder: $full"
}

if (-not $DataPath) {
    if (-not $env:APPDATA) { Fail "APPDATA is not set; pass -DataPath" }
    $DataPath = Join-Path $env:APPDATA "AdaptiveFollow"
}
Say "Adaptive Follow uninstaller$(if ($DryRun) { ' (DRY-RUN: nothing will be changed)' })"
$manifestPath = Join-Path $DataPath "install-manifest.json"
if (-not (Test-Path -LiteralPath $manifestPath)) {
    Fail "no installation manifest at $manifestPath. Nothing was removed. (Installed elsewhere? pass -DataPath.)"
}
$m = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ((Get-Prop $m "product") -ne "Adaptive Follow") { Fail "$manifestPath is not an Adaptive Follow manifest" }

if (Get-Process -Name "FL64", "FL", "FL Studio*" -ErrorAction SilentlyContinue) {
    Warn "FL Studio is running. Close it first, or the device script stays loaded until restart."
}

$dataNames = @("references", "config.json", "logs", "reports", "backup", "command.txt", "last_reference.txt")
$removeAllData = [bool]$RemoveData
if (-not $RemoveData -and -not $KeepData -and -not $NonInteractive) {
    $answer = Read-Host "Also delete your references, config.json, logs and reports in $DataPath ? [y/N]"
    $removeAllData = ($answer -match '^(y|yes|d|da)$')
}

# ---------------------------------------------------------------- restore backups
if ($RestoreBackups) {
    Step "Restore overwritten files"
    foreach ($b in @(Get-Prop $m "backups")) {
        if (-not $b) { continue }
        $orig = Get-Prop $b "original"; $bak = Get-Prop $b "backup"
        if (-not (Test-Path -LiteralPath $bak -PathType Leaf)) { Warn "backup missing: $bak"; continue }
        if ($DryRun) { DryNote "restore $bak -> $orig"; continue }
        Copy-Item -LiteralPath $bak -Destination $orig -Force
        Say "  restored: $orig"
    }
}

# ---------------------------------------------------------------- files
Step "Remove installed files"
# The package copies next to the FL scripts (only if they are really ours).
foreach ($fl in @(Get-Prop $m "fl_settings")) {
    if (-not $fl) { continue }
    foreach ($pkg in @((Join-Path (Join-Path (Join-Path $fl "Hardware") "AdaptiveFollow") "adaptive_follow"),
                       (Join-Path (Join-Path $fl "Piano roll scripts") "adaptive_follow"))) {
        $init = Join-Path $pkg "__init__.py"
        if ((Test-Path -LiteralPath $init) -and ((Get-Content -LiteralPath $init -Raw) -match "Adaptive Follow")) {
            Remove-Tree $pkg "adaptive_follow"
        }
    }
}
$restored = @()
if ($RestoreBackups) { $restored = @(@(Get-Prop $m "backups") | ForEach-Object { Get-Prop $_ "original" }) }
foreach ($f in @(Get-Prop $m "files")) {
    if (-not $f) { continue }
    if ($restored -contains $f) { Say "  kept (restored from backup): $f"; continue }
    if ((Split-Path -Leaf $f) -eq "config.json") { continue }           # user data, handled below
    Remove-FileSafe $f
}
$lib = Get-Prop $m "lib_dir"
if ($lib) {
    Remove-Tree (Join-Path $lib "adaptive_follow") (Join-Path "lib" "adaptive_follow")
    Remove-DirIfEmpty $lib
}
$venv = Get-Prop $m "venv"
if ($venv) { Remove-Tree $venv "venv" }
foreach ($fl in @(Get-Prop $m "fl_settings")) {
    if (-not $fl) { continue }
    foreach ($cache in @((Join-Path (Join-Path (Join-Path $fl "Hardware") "AdaptiveFollow") "__pycache__"),
                         (Join-Path (Join-Path $fl "Piano roll scripts") "__pycache__"))) {
        # Only our own compiled files; other scripts' caches stay.
        if (-not (Test-Path -LiteralPath $cache -PathType Container)) { continue }
        foreach ($pyc in @(Get-ChildItem -LiteralPath $cache -File | Where-Object { $_.Name -like "adaptive_follow_paths.*.pyc" -or $_.Name -like "device_AdaptiveFollow.*.pyc" -or $_.Name -like "Adaptive Follow - Export Reference.*.pyc" })) {
            Remove-FileSafe $pyc.FullName
        }
        Remove-DirIfEmpty $cache
    }
}

# ---------------------------------------------------------------- data
Step "Data in $DataPath"
if ($removeAllData) {
    foreach ($n in $dataNames) {
        $p = Join-Path $DataPath $n
        if (Test-Path -LiteralPath $p -PathType Container) { Remove-Tree $p $n }
        else { Remove-FileSafe $p }
    }
    # Fallback data folders the FL scripts use when FL Studio blocks file access elsewhere.
    foreach ($fl in @(Get-Prop $m "fl_settings")) {
        if (-not $fl) { continue }
        Remove-Tree (Join-Path (Join-Path (Join-Path $fl "Hardware") "AdaptiveFollow") "data") "data"
        Remove-Tree (Join-Path (Join-Path $fl "Piano roll scripts") "Adaptive Follow data") "Adaptive Follow data"
        $imageLine = Split-Path -Parent (Split-Path -Parent $fl)
        if ($imageLine) { Remove-Tree (Join-Path $imageLine "AdaptiveFollow") "AdaptiveFollow" }
    }
} else {
    Say "  kept: references, config.json, logs, reports, backup (use -RemoveData to delete them)"
}

# ---------------------------------------------------------------- folders
Step "Remove empty folders created by the installer"
$dirs = @(Get-Prop $m "created_dirs") | Where-Object { $_ } | Sort-Object { $_.Length } -Descending
foreach ($d in $dirs) {
    if ($venv -and $d -eq $venv) { continue }
    if (-not $removeAllData -and $d -eq $DataPath) { continue }
    Remove-DirIfEmpty $d
}
# Our folder in FL's Hardware folder (and its fallback data folder) go away once empty,
# even when they existed before this installation.
foreach ($fl in @(Get-Prop $m "fl_settings")) {
    if (-not $fl) { continue }
    $hw = Join-Path (Join-Path $fl "Hardware") "AdaptiveFollow"
    foreach ($sub in @("references", "logs", "reports")) { Remove-DirIfEmpty (Join-Path (Join-Path $hw "data") $sub) }
    Remove-DirIfEmpty (Join-Path $hw "data")
    Remove-DirIfEmpty $hw
}
if ($removeAllData) {
    Remove-FileSafe $manifestPath
    foreach ($old in @(Get-ChildItem -LiteralPath $DataPath -Filter "install-manifest.uninstalled-*.json" -File -ErrorAction SilentlyContinue)) {
        Remove-FileSafe $old.FullName
    }
    Remove-DirIfEmpty $DataPath
} elseif (-not $DryRun) {
    Rename-Item -LiteralPath $manifestPath -NewName ("install-manifest.uninstalled-" + (Get-Date -Format "yyyyMMdd-HHmmss") + ".json")
    Say "  manifest archived (data kept)"
} else {
    DryNote "archive $manifestPath"
}

Say ""
Say "Adaptive Follow removed$(if ($DryRun) { ' (DRY-RUN - nothing was changed)' })."
Say "In FL Studio: OPTIONS > MIDI settings, set your keyboard's Controller type back to '(generic controller)'."
exit 0
