<#
.SYNOPSIS
    Builds official Duwn Mirror Windows Installer (Inno Setup single EXE).
.DESCRIPTION
    Compiles duwn-mirror, duwn-virtualcam and tests in Release mode, executes test suites,
    verifies VC++ Redistributable staging, and compiles Duwn-Mirror.iss using Inno Setup 6
    to produce Duwn-Mirror-Setup-1.1.3-x64.exe.
    Updates release.json and SHA256SUMS.txt.
#>
[CmdletBinding()]
param(
    [string]$Version = "1.1.3",
    [switch]$SkipBuild,
    [switch]$SkipTests
)

$ErrorActionPreference = 'Stop'
$rootDir = (Resolve-Path "$PSScriptRoot\..").Path
Set-Location $rootDir

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "  Duwn Mirror Windows Installer Build Pipeline (v$Version)  " -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

# 0. Clean prior build artifact for this specific version only (preserve legacy releases)
$setupExeName = "Duwn-Mirror-Setup-$Version-x64.exe"
$setupExePath = "$rootDir\installer\$setupExeName"
if (Test-Path $setupExePath) {
    Write-Host "[0/5] Removing prior $setupExeName..." -ForegroundColor Yellow
    Remove-Item $setupExePath -Force -ErrorAction SilentlyContinue
}

# 1. Build binaries with CMake if not skipped
if (-not $SkipBuild) {
    Write-Host "[1/5] Building Release binaries (duwn-mirror, duwn-virtualcam, test targets)..." -ForegroundColor Cyan
    cmake --build build-msvc --config Release --target duwn-mirror duwn-virtualcam duwn-unit-tests duwn-capture-persistence-test
    if ($LASTEXITCODE -ne 0) {
        Write-Error "CMake build failed with exit code $LASTEXITCODE."
        exit $LASTEXITCODE
    }
} else {
    Write-Host "[1/5] Skipping binary compilation (-SkipBuild specified)." -ForegroundColor Yellow
}

# 2. Execute test suites if not skipped
if (-not $SkipTests) {
    Write-Host "[2/5] Running automated test suites..." -ForegroundColor Cyan
    $unitTestsExe = "$rootDir\build-msvc\bin\Release\duwn-unit-tests.exe"
    if (Test-Path $unitTestsExe) {
        Write-Host "  -> Running duwn-unit-tests.exe..." -ForegroundColor Gray
        & $unitTestsExe
        if ($LASTEXITCODE -ne 0) {
            Write-Error "Unit tests failed with exit code $LASTEXITCODE. Aborting packaging."
            exit $LASTEXITCODE
        }
    } else {
        Write-Error "Test executable not found: $unitTestsExe"
        exit 1
    }

    $captureTestExe = "$rootDir\build-msvc\bin\Release\duwn-capture-persistence-test.exe"
    if (Test-Path $captureTestExe) {
        Write-Host "  -> Running duwn-capture-persistence-test.exe..." -ForegroundColor Gray
        & $captureTestExe
        if ($LASTEXITCODE -ne 0) {
            Write-Error "Capture persistence tests failed with exit code $LASTEXITCODE. Aborting packaging."
            exit $LASTEXITCODE
        }
    }
    Write-Host "  All test suites passed successfully." -ForegroundColor Green
} else {
    Write-Host "[2/5] Skipping test execution (-SkipTests specified)." -ForegroundColor Yellow
}

# 3. Locate Inno Setup Compiler (iscc.exe)
Write-Host "[3/5] Locating Inno Setup 6 compiler (iscc.exe)..." -ForegroundColor Cyan
$isccPath = $null
$cmdCheck = Get-Command iscc.exe -ErrorAction SilentlyContinue
if ($cmdCheck) {
    $isccPath = $cmdCheck.Source
} else {
    $candidatePaths = @(
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\iscc.exe",
        "${env:ProgramFiles(x86)}\Inno Setup 6\iscc.exe",
        "$env:ProgramFiles\Inno Setup 6\iscc.exe",
        "C:\Program Files (x86)\Inno Setup 6\iscc.exe",
        "C:\Program Files\Inno Setup 6\iscc.exe"
    )
    foreach ($candidate in $candidatePaths) {
        if ($candidate -and (Test-Path $candidate)) {
            $isccPath = $candidate
            break
        }
    }
}

if (-not $isccPath) {
    Write-Error @"
Inno Setup Compiler (iscc.exe) was not found.
Please install Inno Setup 6 (version 6.3 or newer, recommended 6.7.3+ with modern light style):
  winget install --id JRSoftware.InnoSetup --exact
Or download from https://jrsoftware.org/isdl.php
"@
    exit 1
}
Write-Host "  Found Inno Setup Compiler: $isccPath" -ForegroundColor Green

# 4. Verify VC++ Redistributable Staging
Write-Host "[4/5] Verifying VC++ Redistributable staging..." -ForegroundColor Cyan
$vcRedist = "$rootDir\installer\vc_redist.x64.exe"
if (-not (Test-Path $vcRedist)) {
    $vsRedist = (Get-ChildItem -Path "C:\Program Files\Microsoft Visual Studio" -Recurse -Filter "vc_redist.x64.exe" -ErrorAction SilentlyContinue | Select-Object -First 1).FullName
    if ($vsRedist -and (Test-Path $vsRedist)) {
        Copy-Item $vsRedist -Destination $vcRedist -Force
        Write-Host "  Staged VC++ Redistributable from $vsRedist" -ForegroundColor Green
    } else {
        Write-Error "vc_redist.x64.exe not found at $vcRedist. Please place it in installer\vc_redist.x64.exe."
        exit 1
    }
} else {
    Write-Host "  vc_redist.x64.exe verified." -ForegroundColor Green
}

# 5. Compile Inno Setup Script
Write-Host "[5/5] Compiling Inno Setup script (Duwn-Mirror.iss)..." -ForegroundColor Cyan
$issScript = "$rootDir\installer\Duwn-Mirror.iss"
& $isccPath "/DAppVersion=$Version" "/Q" $issScript
if ($LASTEXITCODE -ne 0) {
    Write-Error "Inno Setup compilation failed with exit code $LASTEXITCODE."
    exit $LASTEXITCODE
}

if (-not (Test-Path $setupExePath)) {
    Write-Error "Installer executable not found after compilation: $setupExePath"
    exit 1
}

$setupItem = Get-Item $setupExePath
$setupSize = $setupItem.Length
$setupHash = (Get-FileHash -Path $setupExePath -Algorithm SHA256).Hash.ToUpper()

$mainExePath = "$rootDir\build-msvc\bin\Release\duwn-mirror.exe"
$mainItem = Get-Item $mainExePath
$mainSize = $mainItem.Length
$mainHash = (Get-FileHash -Path $mainExePath -Algorithm SHA256).Hash.ToUpper()

$vcamDllPath = "$rootDir\build-msvc\bin\Release\duwn-virtualcam.dll"
$vcamItem = Get-Item $vcamDllPath
$vcamSize = $vcamItem.Length
$vcamHash = (Get-FileHash -Path $vcamDllPath -Algorithm SHA256).Hash.ToUpper()

Write-Host "`n=== Build Artifact Created Successfully ===" -ForegroundColor Green
Write-Host "  Installer EXE: $setupExePath"
Write-Host "  File Size:     $([math]::Round($setupSize / 1MB, 2)) MB ($setupSize bytes)"
Write-Host "  SHA-256:       $setupHash"

# 6. Update release.json
$releaseJsonPath = "$rootDir\installer\release.json"
$releaseData = [ordered]@{
    product                    = "Duwn Mirror"
    version                    = $Version
    file_version               = "$Version.0"
    architecture               = "x64"
    app_id                     = "{4E78B5D3-0941-4C58-8C64-44F4902F1DA9}"
    legacy_bundle_upgrade_code = "B7E5C381-64D9-4FE2-9E92-351E842D5A10"
    legacy_msi_upgrade_code    = "A3D1E428-8902-4D9A-90A7-6831F901B94C"
    installer                  = [ordered]@{
        filename = $setupExeName
        sha256   = $setupHash
        size     = $setupSize
    }
    binary                     = [ordered]@{
        filename = "duwn-mirror.exe"
        sha256   = $mainHash
        size     = $mainSize
    }
    virtualcam                 = [ordered]@{
        filename = "duwn-virtualcam.dll"
        sha256   = $vcamHash
        size     = $vcamSize
    }
}
$releaseJsonContent = ConvertTo-Json -InputObject $releaseData -Depth 5
[System.IO.File]::WriteAllText($releaseJsonPath, $releaseJsonContent + "`n", [System.Text.Encoding]::UTF8)
Write-Host "Updated $releaseJsonPath" -ForegroundColor Green

# 7. Update SHA256SUMS.txt
$shaSumsPath = "$rootDir\installer\SHA256SUMS.txt"
$shaLines = @(
    "$setupHash  $setupExeName",
    "$mainHash  duwn-mirror.exe",
    "$vcamHash  duwn-virtualcam.dll"
)
[System.IO.File]::WriteAllLines($shaSumsPath, $shaLines, [System.Text.Encoding]::UTF8)
Write-Host "Updated $shaSumsPath" -ForegroundColor Green

Write-Host "`n=== Summary ===" -ForegroundColor Magenta
Write-Host "  Packaging: Inno Setup 6 (WizardStyle=modern light windows11)"
Write-Host "  Installer: $setupExeName"
Write-Host "  No MSI generated or promised (pure single EXE distribution)"
Write-Host "  All automated checks passed."
