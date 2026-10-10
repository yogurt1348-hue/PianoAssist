<#
.SYNOPSIS
    Installs the Adaptive Follow MVP for FL Studio (Windows).

.DESCRIPTION
    1. Checks Python and its version (needed only for the helper tools; FL Studio
       runs the scripts with its own embedded Python).
    2. Creates a virtual environment for the tools (%LOCALAPPDATA%\AdaptiveFollow\venv).
    3. Installs the dependencies into that venv (requirements.txt; -WithDevTools adds
       pytest, mypy and the official FL Studio API stubs).
    4. Finds the FL Studio user data folder (...\Image-Line\FL Studio\Settings) or asks for it.
    5. Copies the MIDI device script to  <Settings>\Hardware\AdaptiveFollow\
       and the Piano Roll script to       <Settings>\Piano roll scripts\
    6. Installs the shared adaptive_follow module and the reference storage in
       %APPDATA%\AdaptiveFollow (lib, references, logs, reports, config.json).
    7. Backs up every file it would overwrite to %APPDATA%\AdaptiveFollow\backup\<timestamp>.
    8. Prints what was installed and how to undo it (uninstall_windows.ps1).

    Nothing outside these folders is changed. No administrator rights are needed.

.PARAMETER DryRun
    Show every action without changing anything.

.PARAMETER FLSettingsPath
    FL Studio user data "Settings" folder, e.g.
    "%USERPROFILE%\Documents\Image-Line\FL Studio\Settings". Detected automatically when omitted.

.PARAMETER DataPath
    Adaptive Follow data folder. Default: %APPDATA%\AdaptiveFollow.

.PARAMETER VenvPath
    Tools venv. Default: %LOCALAPPDATA%\AdaptiveFollow\venv.

.PARAMETER Python
    Python executable to use for the venv (default: py -3, then python, then python3).

.PARAMETER SkipVenv
    Do not create the venv / install dependencies (the FL Studio scripts work without it).

.PARAMETER WithDevTools
    Also install requirements-dev.txt (tests and API check) into the venv.

.PARAMETER NonInteractive
    Never prompt; fail instead of asking for missing information.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\install_windows.ps1 -DryRun

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\install_windows.ps1
#>
[CmdletBinding()]
param(
    [switch]$DryRun,
    [string]$FLSettingsPath = "",
    [string]$DataPath = "",
    [string]$VenvPath = "",
    [string]$Python = "",
    [switch]$SkipVenv,
    [switch]$WithDevTools,
    [switch]$NonInteractive
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"

$Version = "0.1.1"
$MinPython = [Version]"3.9"
$Root = Split-Path -Parent $PSScriptRoot
$DeviceFolderName = "AdaptiveFollow"
$PianoRollFolderName = "Piano roll scripts"
$Stamp = Get-Date -Format "yyyyMMdd-HHmmss"

$script:Installed = New-Object System.Collections.ArrayList     # files written
$script:CreatedDirs = New-Object System.Collections.ArrayList   # directories created
$script:Backups = New-Object System.Collections.ArrayList       # @{original; backup}
$script:Warnings = New-Object System.Collections.ArrayList

function Say([string]$text) { Write-Host $text }
function Step([string]$text) { Write-Host ""; Write-Host "==> $text" -ForegroundColor Cyan }
function Warn([string]$text) { [void]$script:Warnings.Add($text); Write-Host "WARNING: $text" -ForegroundColor Yellow }
function Fail([string]$text) { Write-Host "ERROR: $text" -ForegroundColor Red; exit 1 }
function DryNote([string]$text) { Write-Host "  [dry-run] would $text" -ForegroundColor DarkGray }

function Ensure-Dir([string]$path) {
    if (Test-Path -LiteralPath $path -PathType Container) { return }
    if ($DryRun) { DryNote "create folder $path"; return }
    New-Item -ItemType Directory -Path $path -Force | Out-Null
    [void]$script:CreatedDirs.Add($path)
}

function Same-File([string]$a, [string]$b) {
    if (-not (Test-Path -LiteralPath $b -PathType Leaf)) { return $false }
    return (Get-FileHash -LiteralPath $a -Algorithm SHA256).Hash -eq (Get-FileHash -LiteralPath $b -Algorithm SHA256).Hash
}

function Backup-IfExists([string]$dest) {
    if (-not (Test-Path -LiteralPath $dest -PathType Leaf)) { return }
    # Short name (Windows MAX_PATH): <parent folder>__<file>
    $safe = (Split-Path -Leaf (Split-Path -Parent $dest)) + "__" + (Split-Path -Leaf $dest)
    $backupDir = Join-Path (Join-Path $DataPath "backup") $Stamp
    $backup = Join-Path $backupDir $safe
    if ($DryRun) { DryNote "back up $dest -> $backup"; return }
    Ensure-Dir $backupDir
    Copy-Item -LiteralPath $dest -Destination $backup -Force
    [void]$script:Backups.Add([ordered]@{ original = $dest; backup = $backup })
    Say "  backup: $dest -> $backup"
}

function Install-File([string]$src, [string]$dest) {
    if (-not (Test-Path -LiteralPath $src -PathType Leaf)) { Fail "missing source file $src (incomplete download?)" }
    if (Same-File $src $dest) {
        Say "  unchanged: $dest"
        [void]$script:Installed.Add($dest)
        return
    }
    Backup-IfExists $dest
    if ($DryRun) { DryNote "copy $src -> $dest"; [void]$script:Installed.Add($dest); return }
    Copy-Item -LiteralPath $src -Destination $dest -Force
    [void]$script:Installed.Add($dest)
    Say "  installed: $dest"
}

function Write-Utf8File([string]$dest, [string]$text) {
    if ($DryRun) {
        if (Test-Path -LiteralPath $dest -PathType Leaf) { Backup-IfExists $dest }
        DryNote "write $dest (generated)"; [void]$script:Installed.Add($dest); return
    }
    $tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("af_" + [Guid]::NewGuid().ToString("N") + ".tmp")
    [System.IO.File]::WriteAllText($tmp, $text, (New-Object System.Text.UTF8Encoding($false)))
    try { Install-File $tmp $dest } finally { Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue }
}

function Py-Literal([string]$path) {
    # Python string literal for a Windows path (escaped, works with any user name).
    return '"' + ($path -replace '\\', '\\' -replace '"', '\"') + '"'
}

function Test-PythonCommand([string[]]$cmd) {
    try {
        $exe = $cmd[0]
        $rest = @()
        if ($cmd.Length -gt 1) { $rest = $cmd[1..($cmd.Length - 1)] }
        $out = & $exe @rest -c "import sys; print('%d.%d.%d' % sys.version_info[:3])" 2>$null
        if ($LASTEXITCODE -ne 0 -or -not $out) { return $null }
        return [Version](($out | Select-Object -First 1).Trim())
    } catch { return $null }
}

function Find-Python {
    $candidates = @()
    if ($Python) { $candidates += , @($Python) }
    $candidates += , @("py", "-3")
    $candidates += , @("python")
    $candidates += , @("python3")
    foreach ($c in $candidates) {
        if (-not (Get-Command $c[0] -ErrorAction SilentlyContinue)) { continue }
        $v = Test-PythonCommand $c
        if ($v) { return @{ cmd = $c; version = $v } }   # the Microsoft Store stub fails here
    }
    return $null
}

function Venv-Python([string]$venv) {
    $win = Join-Path (Join-Path $venv "Scripts") "python.exe"
    if (Test-Path -LiteralPath $win) { return $win }
    return (Join-Path (Join-Path $venv "bin") "python")
}

function Find-FLSettings {
    $docs = [Environment]::GetFolderPath("MyDocuments")
    $bases = @()
    if ($docs) { $bases += $docs }
    if ($env:USERPROFILE) { $bases += (Join-Path $env:USERPROFILE "Documents") }
    if ($env:OneDrive) {
        $bases += (Join-Path $env:OneDrive "Documents")
        # Russian-localised OneDrive "Documents" folder, written with char codes to keep this file ASCII
        $bases += (Join-Path $env:OneDrive (-join [char[]](0x0414, 0x043E, 0x043A, 0x0443, 0x043C, 0x0435, 0x043D, 0x0442, 0x044B)))
    }
    if ($env:PUBLIC) { $bases += (Join-Path $env:PUBLIC "Documents") }
    $found = @()
    foreach ($b in $bases) {
        $p = Join-Path (Join-Path (Join-Path $b "Image-Line") "FL Studio") "Settings"
        if ((Test-Path -LiteralPath $p -PathType Container) -and ($found -notcontains $p)) { $found += $p }
    }
    return , $found
}

# ---------------------------------------------------------------- start
Say "Adaptive Follow $Version installer$(if ($DryRun) { ' (DRY-RUN: nothing will be changed)' })"
Say "Source: $Root"

$isWin = ($env:OS -eq "Windows_NT")
if (-not $isWin) { Warn "not running on Windows: this is only useful for testing the installer" }
if (-not $DataPath) {
    if (-not $env:APPDATA) { Fail "APPDATA is not set; pass -DataPath" }
    $DataPath = Join-Path $env:APPDATA "AdaptiveFollow"
}
if (-not $VenvPath) {
    $local = $env:LOCALAPPDATA
    if (-not $local) { $local = $DataPath }
    $VenvPath = Join-Path (Join-Path $local "AdaptiveFollow") "venv"
}

$required = @(
    "midi_device_script\device_AdaptiveFollow\device_AdaptiveFollow.py",
    "piano_roll_script\Adaptive Follow - Export Reference.pyscript",
    "shared\adaptive_follow\__init__.py",
    "config\config.example.json",
    "requirements.txt"
)
foreach ($r in $required) {
    $p = $Root
    foreach ($part in ($r -split '\\')) { $p = Join-Path $p $part }
    if (-not (Test-Path -LiteralPath $p)) { Fail "source file not found: $p. Run the installer from the flstudio-adaptive-follow folder." }
}

$flRunning = Get-Process -Name "FL64", "FL", "FL Studio*" -ErrorAction SilentlyContinue
if ($flRunning) {
    Warn "FL Studio is running. Installing is safe, but restart FL Studio (or reload the script) afterwards."
}

# ---------------------------------------------------------------- 1-3 Python / venv / deps
Step "1-3. Python, venv and dependencies (only for the helper tools)"
$venvPython = $null
if ($SkipVenv) {
    Say "  skipped (-SkipVenv). FL Studio uses its own Python for the scripts."
} else {
    $py = Find-Python
    if (-not $py) {
        Warn "Python $MinPython+ not found (python.org installer, 'Add to PATH'). The FL Studio scripts still work; the tools (simulate_session.py, validate_install.py) need Python."
    } elseif ($py.version -lt $MinPython) {
        Warn ("Python {0} is too old for the tools (need {1}+); venv skipped." -f $py.version, $MinPython)
    } else {
        Say ("  Python {0}: {1}" -f $py.version, ($py.cmd -join " "))
        $venvPython = Venv-Python $VenvPath
        $exe = $py.cmd[0]
        $rest = @()
        if ($py.cmd.Length -gt 1) { $rest = $py.cmd[1..($py.cmd.Length - 1)] }
        if (Test-Path -LiteralPath $venvPython) {
            Say "  venv exists: $VenvPath"
        } elseif ($DryRun) {
            DryNote "create venv $VenvPath"
        } else {
            Ensure-Dir (Split-Path -Parent $VenvPath)
            & $exe @rest -m venv $VenvPath
            if ($LASTEXITCODE -ne 0) { Fail "could not create the venv at $VenvPath" }
            [void]$script:CreatedDirs.Add($VenvPath)
            $venvPython = Venv-Python $VenvPath
            Say "  venv created: $VenvPath"
        }
        $reqs = @(Join-Path $Root "requirements.txt")
        if ($WithDevTools) { $reqs += (Join-Path $Root "requirements-dev.txt") }
        foreach ($req in $reqs) {
            if ($DryRun) { DryNote "pip install -r $req (into the venv)"; continue }
            & $venvPython -m pip install --disable-pip-version-check -q -r $req
            if ($LASTEXITCODE -ne 0) { Fail "pip install -r $req failed" }
            Say "  dependencies installed: $(Split-Path -Leaf $req)"
        }
    }
}

# ---------------------------------------------------------------- 4 FL user data folder
Step "4. FL Studio user data folder"
if ($FLSettingsPath) {
    if (-not (Test-Path -LiteralPath $FLSettingsPath -PathType Container)) { Fail "folder not found: $FLSettingsPath" }
} else {
    $found = Find-FLSettings
    if ($found.Count -ge 1) {
        $FLSettingsPath = $found[0]
        if ($found.Count -gt 1) { Warn ("several FL Studio Settings folders found, using {0}. Others: {1}. Pass -FLSettingsPath to choose." -f $found[0], (($found | Select-Object -Skip 1) -join "; ")) }
    } elseif ($NonInteractive) {
        Fail "FL Studio user data folder not found; pass -FLSettingsPath '<...>\Image-Line\FL Studio\Settings'"
    } else {
        Say "  FL Studio user data folder not found automatically."
        Say "  In FL Studio: OPTIONS > File settings > 'User data folder'. Enter that folder + '\FL Studio\Settings'."
        $FLSettingsPath = (Read-Host "  Path to ...\Image-Line\FL Studio\Settings").Trim('"', ' ')
        if (-not (Test-Path -LiteralPath $FLSettingsPath -PathType Container)) { Fail "folder not found: $FLSettingsPath" }
    }
}
if ((Split-Path -Leaf $FLSettingsPath) -ne "Settings") {
    Warn "the folder is not called 'Settings': $FLSettingsPath. FL Studio looks for scripts in <User data>\FL Studio\Settings."
}
Say "  using: $FLSettingsPath"
$HardwareDir = Join-Path (Join-Path $FLSettingsPath "Hardware") $DeviceFolderName
$PianoRollDir = Join-Path $FLSettingsPath $PianoRollFolderName
$LibDir = Join-Path $DataPath "lib"

# ---------------------------------------------------------------- 6 shared module + storage
Step "6. Shared module and reference storage: $DataPath"
foreach ($d in @($DataPath, $LibDir, (Join-Path $LibDir "adaptive_follow"), (Join-Path $DataPath "references"),
                 (Join-Path $DataPath "logs"), (Join-Path $DataPath "reports"))) { Ensure-Dir $d }
$srcPkg = Join-Path (Join-Path $Root "shared") "adaptive_follow"
foreach ($f in (Get-ChildItem -LiteralPath $srcPkg -Filter "*.py" -File | Sort-Object Name)) {
    Install-File $f.FullName (Join-Path (Join-Path $LibDir "adaptive_follow") $f.Name)
}
$configPath = Join-Path $DataPath "config.json"
if (Test-Path -LiteralPath $configPath) {
    Say "  config.json kept (your settings are never overwritten): $configPath"
} else {
    Install-File (Join-Path (Join-Path $Root "config") "config.example.json") $configPath
}

# ---------------------------------------------------------------- 5 FL scripts
Step "5. FL Studio scripts"
Ensure-Dir $HardwareDir
Ensure-Dir $PianoRollDir
$pathsPy = @"
# Written by the Adaptive Follow installer $Version - where the shared module and data live.
# Delete this file (or run uninstall_windows.ps1) to uninstall; the scripts then fall back to %APPDATA%\AdaptiveFollow.
LIB_DIR = $(Py-Literal $LibDir)
DATA_DIR = $(Py-Literal $DataPath)
"@
Install-File (Join-Path (Join-Path (Join-Path $Root "midi_device_script") "device_AdaptiveFollow") "device_AdaptiveFollow.py") (Join-Path $HardwareDir "device_AdaptiveFollow.py")
Write-Utf8File (Join-Path $HardwareDir "adaptive_follow_paths.py") $pathsPy
Install-File (Join-Path (Join-Path $Root "piano_roll_script") "Adaptive Follow - Export Reference.pyscript") (Join-Path $PianoRollDir "Adaptive Follow - Export Reference.pyscript")
Write-Utf8File (Join-Path $PianoRollDir "adaptive_follow_paths.py") $pathsPy
# FL Studio adds each script's own folder to its module search path (official docs:
# "This folder should contain all resources required to run the script"), so a copy of
# the shared package goes next to both scripts. %APPDATA%\AdaptiveFollow\lib stays for the tools.
foreach ($scriptDir in @($HardwareDir, $PianoRollDir)) {
    $pkgDir = Join-Path $scriptDir "adaptive_follow"
    Ensure-Dir $pkgDir
    foreach ($f in (Get-ChildItem -LiteralPath $srcPkg -Filter "*.py" -File | Sort-Object Name)) {
        Install-File $f.FullName (Join-Path $pkgDir $f.Name)
    }
}

# ---------------------------------------------------------------- 7-8 manifest + summary
Step "7. Manifest"
$manifestPath = Join-Path $DataPath "install-manifest.json"
$previous = $null
if (Test-Path -LiteralPath $manifestPath) {
    try { $previous = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json } catch { $previous = $null }
}
$createdAll = @($script:CreatedDirs)
$backupsAll = @($script:Backups)
if ($previous) {
    # Re-install: keep what the first installation created, so uninstall removes it too.
    foreach ($d in @($previous.created_dirs)) { if ($d -and ($createdAll -notcontains $d)) { $createdAll += $d } }
    foreach ($b in @($previous.backups)) { if ($b) { $backupsAll += $b } }
}
$manifest = [ordered]@{
    product = "Adaptive Follow"
    version = $Version
    installed_at = (Get-Date).ToString("s")
    source = $Root
    fl_settings = $FLSettingsPath
    data_dir = $DataPath
    lib_dir = $LibDir
    venv = $(if ($venvPython) { $VenvPath } elseif ($previous -and $previous.venv) { $previous.venv } else { "" })
    files = @($script:Installed)
    created_dirs = $createdAll
    backups = $backupsAll
    keep_on_uninstall = @("references", "config.json", "logs", "reports", "backup")
}
if ($DryRun) {
    DryNote "write $manifestPath"
} else {
    $json = $manifest | ConvertTo-Json -Depth 6
    [System.IO.File]::WriteAllText($manifestPath, $json, (New-Object System.Text.UTF8Encoding($false)))
    Say "  $manifestPath"
}

if ($venvPython -and -not $DryRun -and (Test-Path -LiteralPath $venvPython)) {
    Step "Validation"
    & $venvPython (Join-Path (Join-Path $Root "tools") "validate_install.py") --fl-settings $FLSettingsPath --data-dir $DataPath
    if ($LASTEXITCODE -ne 0) { Warn "validate_install.py reported problems (see above)" }
}

Step "8. Summary$(if ($DryRun) { ' (DRY-RUN - nothing was changed)' })"
Say "  MIDI device script : $(Join-Path $HardwareDir 'device_AdaptiveFollow.py')"
Say "  Piano Roll script  : $(Join-Path $PianoRollDir 'Adaptive Follow - Export Reference.pyscript')"
Say "  Shared module      : $(Join-Path $LibDir 'adaptive_follow')"
Say "  Data (references, config.json, logs, reports): $DataPath"
if ($venvPython) { Say "  Tools venv         : $VenvPath" }
Say ("  Files: {0}, folders created: {1}, backups: {2}" -f $script:Installed.Count, $script:CreatedDirs.Count, $script:Backups.Count)
foreach ($w in $script:Warnings) { Say "  warning: $w" }
Say ""
Say "Next steps (details: INSTALL_WINDOWS.md):"
Say "  1. Restart FL Studio. OPTIONS > MIDI settings: enable your keyboard input, Controller type 'AdaptiveFollow'."
Say "  2. Piano Roll of the reference part: scripts menu (Tools > Scripting in FL 21+) > 'Adaptive Follow - Export Reference'."
Say "  3. VIEW > Script output: check 'Adaptive Follow ... ready'. dry_run is ON by default."
Say ""
Say "Undo:"
$uninstaller = Join-Path $PSScriptRoot "uninstall_windows.ps1"
Say "  powershell -ExecutionPolicy Bypass -File `"$uninstaller`"              (keeps references/config/logs)"
Say "  powershell -ExecutionPolicy Bypass -File `"$uninstaller`" -RemoveData  (removes everything)"
if ($script:Backups.Count -gt 0) { Say "  Overwritten files were backed up to $(Join-Path (Join-Path $DataPath 'backup') $Stamp) (-RestoreBackups restores them)." }
exit 0
