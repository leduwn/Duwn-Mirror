<#
.SYNOPSIS
    Builds official Duwn Mirror Windows Installers (MSI + Burn Bootstrapper EXE).
.DESCRIPTION
    Compiles duwn-mirror in Release mode, stages the private duwn-airplay runtime,
    runs WiX v5 to produce Duwn-Mirror-1.1.2-x64.msi (with firewall rules)
    and Duwn-Mirror-Setup-1.1.2-x64.exe (with VC++ Redistributable chaining).
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$rootDir = (Resolve-Path "$PSScriptRoot\..").Path
Set-Location $rootDir

Write-Host "=== Cleaning Prior 1.1.2 Artifacts ===" -ForegroundColor Cyan
Remove-Item "$rootDir\installer\Duwn-Mirror-1.1.2-*.msi" -Force -ErrorAction SilentlyContinue
Remove-Item "$rootDir\installer\Duwn-Mirror-Setup-1.1.2-*.exe" -Force -ErrorAction SilentlyContinue

Write-Host "=== 1/4: Building Duwn Mirror Release ===" -ForegroundColor Cyan
cmake --build build-msvc --config Release --target duwn-mirror duwn-virtualcam duwn-unit-tests
if ($LASTEXITCODE -ne 0) {
    Write-Error "CMake build failed."
    exit $LASTEXITCODE
}

Write-Host "=== 2/4: Building WiX MSI Installer (Duwn-Mirror-1.1.2-x64.msi) ===" -ForegroundColor Cyan
$wixTemp = "$rootDir\build-msvc\wix-temp"
if (-not (Test-Path "$wixTemp\msi")) { New-Item -ItemType Directory -Path "$wixTemp\msi" -Force | Out-Null }
if (-not (Test-Path "$wixTemp\bundle")) { New-Item -ItemType Directory -Path "$wixTemp\bundle" -Force | Out-Null }

wix build "$rootDir\installer\Package.wxs" -arch x64 -ext WixToolset.Firewall.wixext -ext WixToolset.Util.wixext -intermediateFolder "$wixTemp\msi" -out "$rootDir\installer\Duwn-Mirror-1.1.2-x64.msi"
if ($LASTEXITCODE -ne 0) {
    Write-Error "WiX MSI build failed."
    exit $LASTEXITCODE
}

$msi = Get-Item "$rootDir\installer\Duwn-Mirror-1.1.2-x64.msi"
Write-Host "MSI generated: $($msi.FullName) ($([math]::Round($msi.Length / 1MB, 2)) MB)" -ForegroundColor Green

Write-Host "=== 3/4: Verifying VC++ Redistributable Staging ===" -ForegroundColor Cyan
$vcRedist = "$rootDir\installer\vc_redist.x64.exe"
if (-not (Test-Path $vcRedist)) {
    $vsRedist = (Get-ChildItem -Path "C:\Program Files\Microsoft Visual Studio" -Recurse -Filter "vc_redist.x64.exe" -ErrorAction SilentlyContinue | Select-Object -First 1).FullName
    if ($vsRedist -and (Test-Path $vsRedist)) {
        Copy-Item $vsRedist -Destination $vcRedist -Force
        Write-Host "Staged VC++ Redistributable from $vsRedist" -ForegroundColor Green
    } else {
        Write-Error "vc_redist.x64.exe not found. Please place it in installer\vc_redist.x64.exe."
        exit 1
    }
}

Write-Host "=== 4/4: Building WiX Burn Bootstrapper (Duwn-Mirror-Setup-1.1.2-x64.exe) ===" -ForegroundColor Cyan
wix build "$rootDir\installer\Bundle.wxs" -arch x64 -ext WixToolset.BootstrapperApplications.wixext -ext WixToolset.Util.wixext -intermediateFolder "$wixTemp\bundle" -out "$rootDir\installer\Duwn-Mirror-Setup-1.1.2-x64.exe"
if ($LASTEXITCODE -ne 0) {
    Write-Error "WiX Bootstrapper build failed."
    exit $LASTEXITCODE
}

$exe = Get-Item "$rootDir\installer\Duwn-Mirror-Setup-1.1.2-x64.exe"
Write-Host "Bootstrapper Setup EXE generated: $($exe.FullName) ($([math]::Round($exe.Length / 1MB, 2)) MB)" -ForegroundColor Green

Write-Host "`n=== Installation Packages Summary ===" -ForegroundColor Magenta
Write-Host "  MSI Package:   $($msi.FullName) ($([math]::Round($msi.Length / 1MB, 2)) MB)"
Write-Host "  Setup Bundle:  $($exe.FullName) ($([math]::Round($exe.Length / 1MB, 2)) MB)"
Write-Host "  Firewall:      Integrated WiX Firewall extension (Domain + Private profiles)"
Write-Host "  Shortcuts:     Start Menu shortcut with AppIcon"
Write-Host "  Chaining:      Microsoft Visual C++ 2015-2026 Redistributable (x64) auto-detection"
