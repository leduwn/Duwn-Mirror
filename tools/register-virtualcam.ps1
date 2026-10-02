# register-virtualcam.ps1 — Register Duwn Mirror DirectShow Virtual Camera Filter (HKCU, non-elevated)
[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$root = (Resolve-Path "$PSScriptRoot\..").Path
$dllPath = Join-Path $root "build-msvc\bin\Release\duwn-virtualcam.dll"

if (-not (Test-Path $dllPath)) {
    Write-Error "Virtual camera DLL not found at $dllPath. Please build duwn-virtualcam first."
    exit 1
}

$exePath = Join-Path $root "build-msvc\bin\Release\duwn-mirror.exe"
if (Test-Path $exePath) {
    & $exePath --register-vcam
    $code = $LASTEXITCODE
    if ($code -eq 0) {
        Write-Host "Successfully registered 'Duwn Mirror Video' virtual camera via duwn-mirror.exe --register-vcam."
        exit 0
    }
}

# Fallback: direct HKCU registration
$clsid = "{8B9F51B8-3232-4518-A7D9-4828E0D71B20}"
$name = "Duwn Mirror Video"
$cat = "{860BB310-5D01-11d0-BD3B-00A0C911CE86}"

$clsidKey = "HKCU:\Software\Classes\CLSID\$clsid"
New-Item -Path $clsidKey -Force | Out-Null
Set-ItemProperty -Path $clsidKey -Name "(Default)" -Value $name
$inprocKey = "$clsidKey\InprocServer32"
New-Item -Path $inprocKey -Force | Out-Null
Set-ItemProperty -Path $inprocKey -Name "(Default)" -Value $dllPath
Set-ItemProperty -Path $inprocKey -Name "ThreadingModel" -Value "Both"

$catKey = "HKCU:\Software\Classes\CLSID\$cat\Instance\$clsid"
New-Item -Path $catKey -Force | Out-Null
Set-ItemProperty -Path $catKey -Name "FriendlyName" -Value $name
Set-ItemProperty -Path $catKey -Name "CLSID" -Value $clsid

Write-Host "Successfully registered '$name' in HKCU DirectShow Video Input Device Category."
exit 0
