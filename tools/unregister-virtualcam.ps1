# unregister-virtualcam.ps1 — Unregister Duwn Mirror DirectShow Virtual Camera Filter (HKCU)
[CmdletBinding()]
param()

$ErrorActionPreference = "SilentlyContinue"
$root = (Resolve-Path "$PSScriptRoot\..").Path
$exePath = Join-Path $root "build-msvc\bin\Release\duwn-mirror.exe"

if (Test-Path $exePath) {
    & $exePath --unregister-vcam
    if ($LASTEXITCODE -eq 0) {
        Write-Host "Successfully unregistered 'Duwn Mirror Video' virtual camera via duwn-mirror.exe."
        exit 0
    }
}

$clsid = "{8B9F51B8-3232-4518-A7D9-4828E0D71B20}"
$cat = "{860BB310-5D01-11d0-BD3B-00A0C911CE86}"

Remove-Item -Path "HKCU:\Software\Classes\CLSID\$clsid" -Recurse -Force
Remove-Item -Path "HKCU:\Software\Classes\CLSID\$cat\Instance\$clsid" -Recurse -Force

Write-Host "Successfully unregistered 'Duwn Mirror Video' from HKCU."
exit 0
