<#
.SYNOPSIS
    Validates GStreamer features using the private staged runtime in an isolated environment.
.DESCRIPTION
    Runs gst-inspect-1.0.exe strictly using the private runtime directory with zero MSYS2 in PATH.
    Fails if any required feature is missing.
#>
[CmdletBinding()]
param(
    [string]$RuntimeDir = ""
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($RuntimeDir)) {
    $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
    $RuntimeDir = (Resolve-Path "$scriptDir\..\runtime\duwn-airplay").Path
} else {
    $RuntimeDir = (Resolve-Path $RuntimeDir).Path
}

Write-Host "=== Validating Staged GStreamer Runtime ===" -ForegroundColor Cyan
Write-Host "Runtime Directory: $RuntimeDir"

$inspector = Join-Path $RuntimeDir "tools\gst-inspect-1.0.exe"
if (-not (Test-Path $inspector)) {
    Write-Error "Inspector binary not found at $inspector"
    exit 1
}

$scanner = Join-Path $RuntimeDir "libexec\gst-plugin-scanner.exe"
if (-not (Test-Path $scanner)) {
    Write-Error "Scanner binary not found at $scanner"
    exit 1
}

$pluginsDir = Join-Path $RuntimeDir "plugins"
if (-not (Test-Path $pluginsDir)) {
    Write-Error "Plugins directory not found at $pluginsDir"
    exit 1
}

$tempCache = Join-Path $env:TEMP "duwn_gst_validate.bin"

# Isolated environment: Only private runtime, tools, libexec, and Windows System32
$sysRoot = $env:SystemRoot
if ([string]::IsNullOrWhiteSpace($sysRoot)) { $sysRoot = "C:\Windows" }
$sys32   = Join-Path $sysRoot "System32"

$cleanPath = "$RuntimeDir;$(Join-Path $RuntimeDir 'tools');$(Join-Path $RuntimeDir 'libexec');$sys32;$sysRoot"

$envMap = [System.Collections.Generic.Dictionary[string, string]]::new()
$envMap["PATH"] = $cleanPath
$envMap["GST_PLUGIN_PATH"] = $pluginsDir
$envMap["GST_PLUGIN_SYSTEM_PATH"] = $pluginsDir
$envMap["GST_PLUGIN_SCANNER"] = $scanner
$envMap["GST_REGISTRY"] = $tempCache
$envMap["SystemRoot"] = $sysRoot
$envMap["TEMP"] = $env:TEMP
$envMap["TMP"] = $env:TEMP

$requiredFeatures = @(
    "app",
    "libav",
    "playback",
    "autodetect",
    "videoparsersbad",
    "rtph264pay",
    "rtpL16pay",
    "udpsink",
    "audioconvert",
    "audioresample",
    "videoconvert",
    "videoscale",
    "queue",
    "capsfilter"
)

$failed = 0
foreach ($feat in $requiredFeatures) {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $inspector
    $psi.Arguments = $feat
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.EnvironmentVariables.Clear()

    foreach ($kv in $envMap.GetEnumerator()) {
        $psi.EnvironmentVariables[$kv.Key] = $kv.Value
    }

    $proc = [System.Diagnostics.Process]::Start($psi)
    $stdout = $proc.StandardOutput.ReadToEnd()
    $stderr = $proc.StandardError.ReadToEnd()
    $proc.WaitForExit()

    if ($proc.ExitCode -eq 0) {
        Write-Host " [PASS] Feature '$feat' verified" -ForegroundColor Green
    } else {
        Write-Host " [FAIL] Feature '$feat' missing (exit code $($proc.ExitCode)): $stderr" -ForegroundColor Red
        $failed++
    }
}

if (Test-Path $tempCache) {
    [System.IO.File]::Delete($tempCache)
}

if ($failed -gt 0) {
    Write-Error "Runtime validation failed: $failed required GStreamer features missing."
    exit 1
}

Write-Host "All $($requiredFeatures.Count) required GStreamer features verified successfully!" -ForegroundColor Green
