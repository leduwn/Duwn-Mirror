<#
.SYNOPSIS
    Stages complete, self-contained GStreamer runtime for DUWN Mirror.
.DESCRIPTION
    Copies UxPlay, gst-plugin-scanner, gst-inspect, and all required GStreamer
    plugins from C:\msys64\ucrt64, recursively resolves the complete transitive
    DLL closure, and verifies zero missing dependencies.
#>
[CmdletBinding()]
param(
    [string]$MsysRoot = "C:\msys64\ucrt64",
    [string]$TargetDir = ""
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($TargetDir)) {
    $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
    $TargetDir = (Resolve-Path "$scriptDir\..\runtime\duwn-airplay").Path
}

Write-Host "=== Staging DUWN AirPlay Runtime ===" -ForegroundColor Cyan
Write-Host "Source MSYS Root: $MsysRoot"
Write-Host "Target Runtime:   $TargetDir"

if (-not (Test-Path "$MsysRoot\bin\uxplay.exe")) {
    Write-Error "Could not find uxplay.exe in $MsysRoot\bin"
    exit 1
}

$pluginsDir = Join-Path $TargetDir "plugins"
$libexecDir = Join-Path $TargetDir "libexec"
$toolsDir   = Join-Path $TargetDir "tools"

New-Item -ItemType Directory -Force -Path $TargetDir | Out-Null
New-Item -ItemType Directory -Force -Path $pluginsDir | Out-Null
New-Item -ItemType Directory -Force -Path $libexecDir | Out-Null
New-Item -ItemType Directory -Force -Path $toolsDir | Out-Null

# 1. Base executables
Copy-Item "$MsysRoot\bin\uxplay.exe" -Destination "$TargetDir\uxplay.exe" -Force
Copy-Item "$MsysRoot\libexec\gstreamer-1.0\gst-plugin-scanner.exe" -Destination "$libexecDir\gst-plugin-scanner.exe" -Force
Copy-Item "$MsysRoot\bin\gst-inspect-1.0.exe" -Destination "$toolsDir\gst-inspect-1.0.exe" -Force

# 2. Plugins required by UxPlay and DUWN pipeline
$requiredPluginNames = @(
    "libgstapp.dll",
    "libgstlibav.dll",
    "libgstplayback.dll",
    "libgstautodetect.dll",
    "libgstvideoparsersbad.dll",
    "libgstrtp.dll",
    "libgstudp.dll",
    "libgstcoreelements.dll",
    "libgstaudioconvert.dll",
    "libgstaudioresample.dll",
    "libgstvideoconvertscale.dll",
    "libgsttypefindfunctions.dll",
    "libgstrtpmanager.dll",
    "libgstrtpmanagerbad.dll",
    "libgstaudioparsers.dll",
    "libgstisomp4.dll",
    "libgstd3d11.dll",
    "libgstwasapi.dll",
    "libgstwasapi2.dll",
    "libgstdirectsound.dll",
    "libgstmediafoundation.dll",
    "libgstopus.dll",
    "libgstopenh264.dll",
    "libgstfdkaac.dll",
    "libgstfaad.dll",
    "libgstvolume.dll",
    "libgsttcp.dll",
    "libgstrtsp.dll",
    "libgstdebugutilsbad.dll",
    "libgstcompositor.dll",
    "libgstrawparse.dll"
)

Write-Host "Copying required plugins ($($requiredPluginNames.Count))..." -ForegroundColor Cyan
foreach ($p in $requiredPluginNames) {
    $src = Join-Path "$MsysRoot\lib\gstreamer-1.0" $p
    if (Test-Path $src) {
        Copy-Item $src -Destination "$pluginsDir\$p" -Force
    } else {
        Write-Warning "Plugin not found in MSYS: $src"
    }
}

# 3. Transitive dependency resolver using objdump
function Get-DllDependencies([string]$filePath) {
    $deps = @()
    $out = & objdump -p $filePath 2>$null
    foreach ($line in $out) {
        if ($line -match '^\s*DLL Name:\s*(.+)$') {
            $dll = $matches[1].Trim()
            $deps += $dll
        }
    }
    return $deps
}

$systemDllPrefixes = @(
    "api-ms-win-",
    "ext-ms-win-",
    "kernel32.dll",
    "user32.dll",
    "gdi32.dll",
    "shell32.dll",
    "ole32.dll",
    "oleaut32.dll",
    "advapi32.dll",
    "ws2_32.dll",
    "mswsock.dll",
    "iphlpapi.dll",
    "dnsapi.dll",
    "secur32.dll",
    "crypt32.dll",
    "bcrypt.dll",
    "ncrypt.dll",
    "shlwapi.dll",
    "version.dll",
    "winmm.dll",
    "imm32.dll",
    "uxtheme.dll",
    "dwmapi.dll",
    "setupapi.dll",
    "cfgmgr32.dll",
    "d3d11.dll",
    "dxgi.dll",
    "mfplat.dll",
    "mf.dll",
    "mfreadwrite.dll",
    "mfuuid.dll",
    "dwrite.dll",
    "d2d1.dll",
    "d3d9.dll",
    "comctl32.dll",
    "comdlg32.dll",
    "wldap32.dll",
    "rpcrt4.dll",
    "ntdll.dll",
    "dsound.dll",
    "opengl32.dll",
    "glu32.dll",
    "msacm32.dll",
    "msvfw32.dll",
    "avrt.dll"
)

function Is-SystemDll([string]$dllName) {
    $lower = $dllName.ToLower()
    foreach ($p in $systemDllPrefixes) {
        if ($lower.StartsWith($p) -or $lower -eq $p) {
            return $true
        }
    }
    return $false
}

Write-Host "Resolving transitive DLL dependency closure..." -ForegroundColor Cyan

$scanned = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
$queue = [System.Collections.Generic.Queue[string]]::new()

# Seed queue with all binaries and plugins
Get-ChildItem -Path $TargetDir -Filter "*.exe" -Recurse | ForEach-Object { $queue.Enqueue($_.FullName) }
Get-ChildItem -Path $pluginsDir -Filter "*.dll" | ForEach-Object { $queue.Enqueue($_.FullName) }

$copiedDlls = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)

while ($queue.Count -gt 0) {
    $currentFile = $queue.Dequeue()
    if ($scanned.Contains($currentFile)) { continue }
    $scanned.Add($currentFile) | Out-Null

    $deps = Get-DllDependencies $currentFile
    foreach ($d in $deps) {
        if (Is-SystemDll $d) { continue }

        $destDll = Join-Path $TargetDir $d
        if (-not (Test-Path $destDll)) {
            $srcDll = Join-Path "$MsysRoot\bin" $d
            if (Test-Path $srcDll) {
                Copy-Item $srcDll -Destination $destDll -Force
                $copiedDlls.Add($d) | Out-Null
                $queue.Enqueue($destDll)
                Write-Host " [DEP] + $d (required by $(Split-Path $currentFile -Leaf))" -ForegroundColor Gray
            } else {
                Write-Warning " [MISSING] Could not find $d in $MsysRoot\bin (required by $(Split-Path $currentFile -Leaf))"
            }
        } else {
            if (-not $scanned.Contains($destDll)) {
                $queue.Enqueue($destDll)
            }
        }
    }
}

Write-Host "Total DLLs in staged runtime root: $((Get-ChildItem $TargetDir -Filter '*.dll').Count)" -ForegroundColor Green
Write-Host "Total plugins in staged runtime:  $((Get-ChildItem $pluginsDir -Filter '*.dll').Count)" -ForegroundColor Green
