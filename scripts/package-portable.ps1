# Assemble the portable PianoAssist test build (a folder that runs on a clean
# Windows 10/11 x64 PC without installing anything) from a Release build.
#
#   powershell -ExecutionPolicy Bypass -File scripts\package-portable.ps1 `
#       -BuildDir build-release -VcpkgBin C:\vcpkg\installed\x64-windows\bin `
#       -SoundFont GeneralUser-GS.sf2 -SoundFontLicense LICENSE.txt [-Zip]
#
# Output: dist\PianoAssist-<version>\  (+ dist\PianoAssist-<version>.zip with -Zip)
#
# Contents: the app + FluidSynth DLLs + the MSVC runtime (app-local), the
# factory resources, ONE SoundFont with its license, two demo songs and the
# Russian instructions / manual test plan.

param(
    [string]$BuildDir = "build-release",
    [string]$OutDir   = "dist",
    [string]$VcpkgBin = "",
    [Parameter(Mandatory = $true)][string]$SoundFont,
    [Parameter(Mandatory = $true)][string]$SoundFontLicense,
    [string]$SoundFontName = "GeneralUser-GS-v2.0.3.sf2",
    [string]$BuildId = "local",
    [switch]$Zip
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

$cmake = Get-Content (Join-Path $root "CMakeLists.txt") -TotalCount 10 | Out-String
if ($cmake -match 'project\(Cadenza VERSION ([0-9.]+)') { $version = $Matches[1] } else { $version = "0.0.0" }
$name = "PianoAssist-$version-$BuildId"

$exeDir = Join-Path $root "$BuildDir\Cadenza_artefacts\Release"
$exe    = Join-Path $exeDir "Cadenza Workstation.exe"
if (-not (Test-Path $exe)) { Write-Error "Release exe not found at '$exe'. Build the Release tree first." }
$demoTool = Join-Path $root "$BuildDir\adaptive-demo.exe"
if (-not (Test-Path $demoTool)) { Write-Error "adaptive-demo.exe not found in '$BuildDir'." }

$pkg = Join-Path $root "$OutDir\$name"
if (Test-Path $pkg) { Remove-Item -Recurse -Force $pkg }
New-Item -ItemType Directory -Force $pkg, "$pkg\Demo", "$pkg\resources\sf2" | Out-Null
Write-Host "Packaging $name -> $pkg"

# 1. App + DLLs next to the exe (vcpkg applocal already copies FluidSynth's).
Copy-Item $exe $pkg
Get-ChildItem $exeDir -Filter *.dll | Copy-Item -Destination $pkg
if ($VcpkgBin -and (Test-Path $VcpkgBin)) {
    Get-ChildItem $VcpkgBin -Filter *.dll | ForEach-Object {
        if (-not (Test-Path (Join-Path $pkg $_.Name))) { Copy-Item $_.FullName $pkg }
    }
}

# 2. MSVC runtime, app-local (so no VC++ Redistributable install is needed).
if ($env:VCToolsRedistDir) {
    $crt = Get-ChildItem (Join-Path $env:VCToolsRedistDir "x64") -Directory -Filter "Microsoft.VC*.CRT" | Select-Object -First 1
    if ($crt) { Get-ChildItem $crt.FullName -Filter *.dll | Copy-Item -Destination $pkg }
    else { Write-Warning "MSVC CRT folder not found under $env:VCToolsRedistDir" }
} else {
    Write-Warning "VCToolsRedistDir not set: the MSVC runtime is not bundled"
}
foreach ($dll in "vcruntime140.dll", "msvcp140.dll") {
    if (-not (Test-Path (Join-Path $pkg $dll))) { Write-Warning "$dll is missing from the package" }
}

# 3. Resources: factory styles/songs, and exactly one SoundFont with its license.
Copy-Item -Recurse (Join-Path $root "resources\factory") (Join-Path $pkg "resources\factory")
Copy-Item $SoundFont (Join-Path $pkg "resources\sf2\$SoundFontName")
Copy-Item $SoundFontLicense (Join-Path $pkg "resources\sf2\GeneralUser-GS-LICENSE.txt")

# 4. Demo songs (generated, copyright-free).
& $demoTool (Join-Path $pkg "Demo\demo-both-hands-80bpm.mid") --bpm 80
if ($LASTEXITCODE -ne 0) { Write-Error "adaptive-demo failed" }
& $demoTool (Join-Path $pkg "Demo\demo-right-hand-80bpm.mid") --bpm 80 --right-hand-only
if ($LASTEXITCODE -ne 0) { Write-Error "adaptive-demo failed" }

# 5. Docs (UTF-8 with BOM so Notepad shows Cyrillic on any Windows version).
$utf8Bom = New-Object System.Text.UTF8Encoding $true
foreach ($pair in @(@("docs\TEST_BUILD_RU.md", "START-HERE.txt"), @("docs\MANUAL_TEST_RU.md", "MANUAL-TEST.txt"))) {
    $text = [System.IO.File]::ReadAllText((Join-Path $root $pair[0]))
    [System.IO.File]::WriteAllText((Join-Path $pkg $pair[1]), $text, $utf8Bom)
}
Copy-Item (Join-Path $root "LICENSE") (Join-Path $pkg "LICENSE.txt")
Copy-Item (Join-Path $root "NOTICE") (Join-Path $pkg "NOTICE.txt")
Set-Content -Path (Join-Path $pkg "BUILD-INFO.txt") -Encoding ascii -Value @(
    "PianoAssist test build $version+$BuildId",
    "Built: $((Get-Date).ToUniversalTime().ToString('yyyy-MM-dd HH:mm')) UTC",
    "SoundFont: $SoundFontName (sha256 $((Get-FileHash $SoundFont -Algorithm SHA256).Hash.ToLower()))"
)

$size = [math]::Round((Get-ChildItem $pkg -Recurse -File | Measure-Object Length -Sum).Sum / 1MB, 1)
Write-Host "Package folder ready: $pkg ($size MB)"
Get-ChildItem $pkg | ForEach-Object { Write-Host "  $($_.Name)" }

if ($Zip) {
    $zipPath = Join-Path $root "$OutDir\$name.zip"
    if (Test-Path $zipPath) { Remove-Item -Force $zipPath }
    Compress-Archive -Path $pkg -DestinationPath $zipPath -CompressionLevel Optimal
    Write-Host "Zip ready: $zipPath"
}
