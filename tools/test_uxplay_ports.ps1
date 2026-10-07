$RuntimeDir = (Resolve-Path "$PSScriptRoot\..\runtime\duwn-airplay").Path
$uxplay = Join-Path $RuntimeDir "uxplay.exe"
$sysRoot = $env:SystemRoot
if ([string]::IsNullOrWhiteSpace($sysRoot)) { $sysRoot = "C:\Windows" }
$sys32   = Join-Path $sysRoot "System32"
$cleanPath = "$RuntimeDir;$(Join-Path $RuntimeDir 'tools');$(Join-Path $RuntimeDir 'libexec');$sys32;$sysRoot"
$pluginsDir = Join-Path $RuntimeDir "plugins"
$scanner = Join-Path $RuntimeDir "libexec\gst-plugin-scanner.exe"

function Test-PortArg([string]$label, [string]$portArg) {
    Write-Host "`n==========================================" -ForegroundColor Cyan
    Write-Host "Testing Case: $label" -ForegroundColor Yellow
    Write-Host "Port Argument: $portArg"

    $pipelineArgs = "-fps 60 -s 1920x1080@60 -FPSdata -vrtp `"config-interval=1 ! udpsink host=127.0.0.1 port=7010 sync=false`" -artp `"pt=96 ! udpsink host=127.0.0.1 port=7011 sync=false`" -nc -n `"PortTest_$label`" -nh -d"
    $fullArgs = if ([string]::IsNullOrWhiteSpace($portArg)) { $pipelineArgs } else { "$portArg $pipelineArgs" }

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $uxplay
    $psi.Arguments = $fullArgs
    $psi.WorkingDirectory = $RuntimeDir
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.EnvironmentVariables.Clear()
    $psi.EnvironmentVariables["PATH"] = $cleanPath
    $psi.EnvironmentVariables["GST_PLUGIN_PATH"] = $pluginsDir
    $psi.EnvironmentVariables["GST_PLUGIN_SYSTEM_PATH"] = $pluginsDir
    $psi.EnvironmentVariables["GST_PLUGIN_SCANNER"] = $scanner
    $psi.EnvironmentVariables["SystemRoot"] = $sysRoot
    $psi.EnvironmentVariables["TEMP"] = $env:TEMP
    $psi.EnvironmentVariables["TMP"] = $env:TEMP

    $proc = [System.Diagnostics.Process]::Start($psi)

    Start-Sleep -Seconds 3
    if ($proc.HasExited) {
        Write-Host "Process exited early with code $($proc.ExitCode)" -ForegroundColor Red
        $out = $proc.StandardOutput.ReadToEnd()
        $err = $proc.StandardError.ReadToEnd()
        Write-Host "STDOUT: `n$out"
        Write-Host "STDERR: `n$err"
        return
    }

    $pidNum = $proc.Id
    Write-Host "Process running with PID: $pidNum" -ForegroundColor Green

    # Check TCP and UDP connections with netstat
    $netstatOut = netstat -ano
    $procLines = $netstatOut | Where-Object { $_ -match "\s+$pidNum$" }
    Write-Host "Netstat lines for PID $pidNum :" -ForegroundColor Magenta
    $procLines | ForEach-Object { Write-Host "  $_" -ForegroundColor White }

    $proc.Kill()
    $proc.WaitForExit(1000) | Out-Null
    $out = $proc.StandardOutput.ReadToEnd()
    $err = $proc.StandardError.ReadToEnd()
    Write-Host "UxPlay Output (filtered):" -ForegroundColor Cyan
    foreach ($l in (($out + "`n" + $err) -split "`r?`n")) {
        if ($l -match "port" -or $l -match "AirPlay" -or $l -match "RAOP" -or $l -match "dnssd" -or $l -match "using") {
            Write-Host "  $l" -ForegroundColor Gray
        }
    }
}

Test-PortArg "MinusP_7000" "-p 7000"
Test-PortArg "MinusP_Triple7000" "-p 7000,7000,7000"
Test-PortArg "MinusP_7000_7001_7002" "-p 7000,7001,7002"
Test-PortArg "Default_NoP" ""


