# Builds the two Windows deliverables of one version from one Release exe:
#   PianoAssist-<version>-windows-x64-setup.exe     (Inno Setup, per-user)
#   PianoAssist-<version>-windows-x64-portable.zip  (unzip and run; data in Data\)
# plus SHA256SUMS.txt and windows-checks.txt (Defender scan and signature
# status of each file, as seen on the build machine).
#
# Signing is optional: it runs only when SIGN_CERT_PFX_BASE64 and
# SIGN_CERT_PASSWORD are set (CI secrets). Nothing secret is stored here.
param(
    [Parameter(Mandatory = $true)] [string] $Exe,
    [Parameter(Mandatory = $true)] [string] $Version,
    [string] $BuildDir = "build/windows-x64",
    [string] $OutDir = "dist"
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = Resolve-Path $OutDir
$Exe = Resolve-Path $Exe

# --- licences of everything inside the program --------------------------------
$licenses = Join-Path $OutDir "licenses"
New-Item -ItemType Directory -Force $licenses | Out-Null
Copy-Item (Join-Path $root "licenses/*") $licenses -Recurse -Force
Copy-Item "$BuildDir/_deps/juce-src/LICENSE.md" (Join-Path $licenses "JUCE-LICENSE.md")
Copy-Item "$BuildDir/_deps/tinysoundfont-src/LICENSE" (Join-Path $licenses "TinySoundFont-LICENSE.txt")
Copy-Item "$BuildDir/_deps/nlohmann_json-src/LICENSE.MIT" (Join-Path $licenses "nlohmann-json-LICENSE.txt")

# --- optional signing ----------------------------------------------------------
$signtool = $null
$pfx = $null
if ($env:SIGN_CERT_PFX_BASE64 -and $env:SIGN_CERT_PASSWORD) {
    $pfx = Join-Path $env:RUNNER_TEMP "sign.pfx"
    [IO.File]::WriteAllBytes($pfx, [Convert]::FromBase64String($env:SIGN_CERT_PFX_BASE64))
    $signtool = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" | Sort-Object FullName | Select-Object -Last 1
    $timestamp = if ($env:SIGN_TIMESTAMP_URL) { $env:SIGN_TIMESTAMP_URL } else { "http://timestamp.digicert.com" }
    & $signtool.FullName sign /fd sha256 /tr $timestamp /td sha256 /f $pfx /p $env:SIGN_CERT_PASSWORD $Exe
    if ($LASTEXITCODE -ne 0) { throw "Signing the exe failed" }
    Write-Host "Signed PianoAssist.exe"
} else {
    Write-Host "No signing secrets: building unsigned."
}

# --- installer -----------------------------------------------------------------
$iscc = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
if (-not (Test-Path $iscc)) { choco install innosetup --no-progress -y | Out-Null }
$isccArgs = @("/Qp", "/DAppVersion=$Version", "/DSourceExe=$Exe", "/DLicenses=$licenses", "/O$OutDir")
if ($signtool) {
    $isccArgs += "/DSign"
    $isccArgs += "/Spianoassist=`"$($signtool.FullName)`" sign /fd sha256 /tr $timestamp /td sha256 /f `"$pfx`" /p $env:SIGN_CERT_PASSWORD `$f"
}
& $iscc @isccArgs (Join-Path $root "installer/PianoAssist.iss")
if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed" }
$setup = Join-Path $OutDir "PianoAssist-$Version-windows-x64-setup.exe"

# --- portable ZIP ----------------------------------------------------------------
$stage = Join-Path $OutDir "portable/PianoAssist"
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item $Exe $stage
Copy-Item $licenses (Join-Path $stage "licenses") -Recurse -Force
Set-Content -Path (Join-Path $stage "PianoAssist.portable") -Value "This file makes PianoAssist portable: settings, logs and songs are kept in the Data folder next to PianoAssist.exe." -Encoding UTF8
Copy-Item (Join-Path $root "installer/README-portable.txt") (Join-Path $stage "README.txt")
$zip = Join-Path $OutDir "PianoAssist-$Version-windows-x64-portable.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path $stage -DestinationPath $zip

# --- checks and hashes -------------------------------------------------------------
$files = @($setup, $zip, $Exe)
$report = @("PianoAssist $Version - checks on the build machine ($(Get-Date -Format u))", "")
$mpcmd = "$env:ProgramFiles\Windows Defender\MpCmdRun.exe"
foreach ($file in $files) {
    $name = Split-Path -Leaf $file
    $sig = Get-AuthenticodeSignature $file
    $report += "$name"
    $report += "  Authenticode: $($sig.Status) $($sig.SignerCertificate.Subject)"
    if (Test-Path $mpcmd) {
        $scan = & $mpcmd -Scan -ScanType 3 -File $file -DisableRemediation 2>&1 | Out-String
        $report += "  Defender (exit $LASTEXITCODE): " + (($scan -split "`r?`n" | Where-Object { $_ -match '\S' }) -join " | ")
    } else {
        $report += "  Defender: MpCmdRun.exe not available on this machine"
    }
}
$report | Set-Content (Join-Path $OutDir "windows-checks.txt") -Encoding UTF8
$report | ForEach-Object { Write-Host $_ }

$sums = foreach ($file in @($setup, $zip)) {
    "$((Get-FileHash -Algorithm SHA256 $file).Hash.ToLower())  $(Split-Path -Leaf $file)"
}
$sums | Set-Content (Join-Path $OutDir "SHA256SUMS.txt") -Encoding ASCII
$sums | ForEach-Object { Write-Host $_ }
if ($pfx) { Remove-Item $pfx -Force }
