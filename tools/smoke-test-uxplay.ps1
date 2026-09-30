<#
.SYNOPSIS
    Runs UxPlay startup smoke test and production-argument pipeline test in isolated environment.
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

Write-Host "=== Running UxPlay Isolated Startup Smoke Tests ===" -ForegroundColor Cyan

$uxplay = Join-Path $RuntimeDir "uxplay.exe"
if (-not (Test-Path $uxplay)) {
    Write-Error "uxplay.exe not found at $uxplay"
    exit 1
}

$sysRoot = $env:SystemRoot
if ([string]::IsNullOrWhiteSpace($sysRoot)) { $sysRoot = "C:\Windows" }
$sys32   = Join-Path $sysRoot "System32"

$cleanPath = "$RuntimeDir;$(Join-Path $RuntimeDir 'tools');$(Join-Path $RuntimeDir 'libexec');$sys32;$sysRoot"
$pluginsDir = Join-Path $RuntimeDir "plugins"
$scanner = Join-Path $RuntimeDir "libexec\gst-plugin-scanner.exe"
$cacheFile = Join-Path $env:TEMP "duwn_uxplay_test.bin"

$envMap = [System.Collections.Generic.Dictionary[string, string]]::new()
$envMap["PATH"] = $cleanPath
$envMap["GST_PLUGIN_PATH"] = $pluginsDir
$envMap["GST_PLUGIN_SYSTEM_PATH"] = $pluginsDir
$envMap["GST_PLUGIN_SCANNER"] = $scanner
$envMap["GST_REGISTRY"] = $cacheFile
$envMap["SystemRoot"] = $sysRoot
$envMap["TEMP"] = $env:TEMP
$envMap["TMP"] = $env:TEMP

function Run-UxPlayTest([string]$name, [string]$arguments) {
    Write-Host "`n--> Testing: $name" -ForegroundColor Yellow
    Write-Host "Command: uxplay.exe $arguments"

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $uxplay
    $psi.Arguments = $arguments
    $psi.WorkingDirectory = $RuntimeDir
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.EnvironmentVariables.Clear()
    foreach ($kv in $envMap.GetEnumerator()) {
        $psi.EnvironmentVariables[$kv.Key] = $kv.Value
    }

    $proc = [System.Diagnostics.Process]::Start($psi)

    $outputBuilder = [System.Text.StringBuilder]::new()
    $errBuilder    = [System.Text.StringBuilder]::new()

    $outHandler = [System.Diagnostics.DataReceivedEventHandler]{
        if ($args[1].Data) {
            $outputBuilder.AppendLine($args[1].Data) | Out-Null
            Write-Host " [UxPlay stdout] $($args[1].Data)" -ForegroundColor DarkGray
        }
    }
    $errHandler = [System.Diagnostics.DataReceivedEventHandler]{
        if ($args[1].Data) {
            $errBuilder.AppendLine($args[1].Data) | Out-Null
            Write-Host " [UxPlay stderr] $($args[1].Data)" -ForegroundColor Red
        }
    }

    $proc.add_OutputDataReceived($outHandler)
    $proc.add_ErrorDataReceived($errHandler)
    $proc.BeginOutputReadLine()
    $proc.BeginErrorReadLine()

    # Wait 3 seconds to see if UxPlay survives startup and stays running
    $survived = $false
    for ($i = 0; $i -lt 6; $i++) {
        Start-Sleep -Milliseconds 500
        if ($proc.HasExited) {
            break
        }
    }

    if (-not $proc.HasExited) {
        $survived = $true
        Write-Host " [PASS] UxPlay passed startup validation and remained alive waiting for connection!" -ForegroundColor Green
        # Gracefully stop process
        $proc.Kill()
        $proc.WaitForExit(1000) | Out-Null
    } else {
        Write-Host " [FAIL] UxPlay exited prematurely with code $($proc.ExitCode)" -ForegroundColor Red
        Write-Host "Captured STDERR: `n$($errBuilder.ToString())"
    }

    return $survived
}

$ok1 = Run-UxPlayTest "Simple Startup Smoke Test" "-d 1 -n `"DUWN Mirror Runtime Test`""
$ok2 = Run-UxPlayTest "Production Pipeline Test" "-fps 60 -s 1920x1080@60 -FPSdata -vrtp `"config-interval=1 ! udpsink host=127.0.0.1 port=7010 sync=false`" -artp `"pt=96 ! udpsink host=127.0.0.1 port=7011 sync=false`" -nc -n `"DUWN Mirror Pipeline Test`""

if (Test-Path $cacheFile) {
    [System.IO.File]::Delete($cacheFile)
}

if (-not ($ok1 -and $ok2)) {
    Write-Error "UxPlay smoke tests failed!"
    exit 1
}

Write-Host "`nAll UxPlay smoke tests passed successfully!" -ForegroundColor Green
[Environment]::Exit(0)
