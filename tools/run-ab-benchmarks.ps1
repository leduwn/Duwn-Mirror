<#
.SYNOPSIS
    Automates A/B receiver latency benchmarking runs for Duwn Mirror.
.DESCRIPTION
    Executes a structured benchmark run for one or all six configurations:
    B_OFF, B_ON, F_OFF, F_ON, C_OFF, C_ON.
    Collects system info, run manifest, logs, and telemetry CSV via capture-media-metrics.ps1.
    Validates active stream presence (decoded frames, output present delta, frame age, quality_pending).
.PARAMETER RunId
    One of: B_OFF, B_ON, F_OFF, F_ON, C_OFF, C_ON, or All.
.PARAMETER WarmupSeconds
    Seconds to wait for pipeline stabilization before measurement (default: 30).
.PARAMETER DurationSeconds
    Seconds of measurement data to record (default: 180).
.PARAMETER OutputDir
    Root directory to store benchmark runs (default: benchmarks/runs).
.PARAMETER DeviceModel
    Sender device description (e.g. "iPhone 13 Pro iOS 17.5.1").
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $false)]
    [ValidateSet('B_OFF', 'B_ON', 'F_OFF', 'F_ON', 'C_OFF', 'C_ON', 'All')]
    [string]$RunId = 'B_OFF',

    [Parameter(Mandatory = $false)]
    [int]$WarmupSeconds = 30,

    [Parameter(Mandatory = $false)]
    [int]$DurationSeconds = 180,

    [Parameter(Mandatory = $false)]
    [string]$OutputDir = 'benchmarks/runs',

    [Parameter(Mandatory = $false)]
    [string]$DeviceModel = 'Unspecified iPhone (AirPlay)'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$repoRoot = Split-Path -Parent $scriptDir

# Configuration matrix
$configs = @{
    'B_OFF' = @{ Mode = 'Balanced'; Preview = 'OFF'; Desc = 'Balanced (SmoothLive), Preview OFF' }
    'B_ON'  = @{ Mode = 'Balanced'; Preview = 'ON';  Desc = 'Balanced (SmoothLive), Preview ON' }
    'F_OFF' = @{ Mode = 'Fastest';  Preview = 'OFF'; Desc = 'Fastest (LowLatency 1-frame), Preview OFF' }
    'F_ON'  = @{ Mode = 'Fastest';  Preview = 'ON';  Desc = 'Fastest (LowLatency 1-frame), Preview ON' }
    'C_OFF' = @{ Mode = 'Custom';   Preview = 'OFF'; Desc = 'Custom (2 frames / 25 ms), Preview OFF' }
    'C_ON'  = @{ Mode = 'Custom';   Preview = 'ON';  Desc = 'Custom (2 frames / 25 ms), Preview ON' }
}

$runsToExecute = if ($RunId -eq 'All') {
    @('B_OFF', 'B_ON', 'F_OFF', 'F_ON', 'C_OFF', 'C_ON')
} else {
    @($RunId)
}

# Collect host environment info
function Get-HostManifest {
    $commit = & git -C $repoRoot rev-parse HEAD 2>$null
    $dirty = (& git -C $repoRoot status --porcelain 2>$null).Length -gt 0
    $branch = & git -C $repoRoot rev-parse --abbrev-ref HEAD 2>$null

    $os = (Get-CimInstance Win32_OperatingSystem).Caption
    $cpu = (Get-CimInstance Win32_Processor | Select-Object -First 1).Name
    $gpu = (Get-CimInstance Win32_VideoController | Select-Object -First 1).Name

    $displayHz = 'Unknown'
    try {
        $refresh = (Get-CimInstance -ClassName WmiMonitorListedSupportedDisplayModes -Namespace root\wmi -ErrorAction SilentlyContinue |
            Select-Object -First 1).VerticalRefreshRateDenominator
        if ($refresh) { $displayHz = "$refresh Hz" }
    } catch {}

    return @{
        commit     = $commit
        branch     = $branch
        dirty      = $dirty
        os         = $os
        cpu        = $cpu
        gpu        = $gpu
        display_hz = $displayHz
    }
}

$hostInfo = Get-HostManifest

foreach ($currentRunId in $runsToExecute) {
    $cfg = $configs[$currentRunId]
    Write-Host "`n================================================================================" -ForegroundColor Cyan
    Write-Host "BENCHMARK RUN: $currentRunId — $($cfg.Desc)" -ForegroundColor Cyan
    Write-Host "================================================================================" -ForegroundColor Cyan
    Write-Host "1. Configure Duwn Mirror settings:" -ForegroundColor Yellow
    Write-Host "   - Delivery Mode: $($cfg.Mode)" -ForegroundColor White
    if ($cfg.Mode -eq 'Custom') {
        Write-Host "   - Video Queue: 2 frames" -ForegroundColor White
        Write-Host "   - Video Freshness: 25 ms" -ForegroundColor White
    }
    Write-Host "   - Preview Window: $($cfg.Preview)" -ForegroundColor White
    Write-Host "   - Receiver Quality: Auto / 1080p60 (Ensure identical across all runs)" -ForegroundColor White
    Write-Host "   - Output: Same display, same refresh rate, same lighting" -ForegroundColor White
    Write-Host "2. Start iPhone AirPlay Screen Mirroring with repeated motion test pattern." -ForegroundColor Yellow

    $targetRunDir = Join-Path $repoRoot (Join-Path $OutputDir $currentRunId)
    if (-not (Test-Path $targetRunDir)) {
        New-Item -ItemType Directory -Path $targetRunDir -Force | Out-Null
    }

    $csvPath = Join-Path $targetRunDir "metrics.csv"
    $manifestPath = Join-Path $targetRunDir "manifest.json"

    $startTime = [DateTime]::UtcNow

    if ($WarmupSeconds -gt 0) {
        Write-Host "`nWaiting for $WarmupSeconds seconds warmup/stabilization..." -ForegroundColor DarkCyan
        Start-Sleep -Seconds $WarmupSeconds
    }

    Write-Host "Starting metrics collection: $DurationSeconds seconds -> $csvPath" -ForegroundColor Green
    $collectorScript = Join-Path $scriptDir "capture-media-metrics.ps1"
    
    # Run collector
    & powershell -ExecutionPolicy Bypass -File $collectorScript -OutputPath $csvPath -DurationSeconds $DurationSeconds

    $endTime = [DateTime]::UtcNow

    # Validate output CSV
    $isValidStream = $false
    $validationNotes = @()
    $totalRows = 0
    $activeDecodedRows = 0
    $qualityPendingRows = 0
    $deltaOutputOk = 0

    if (Test-Path $csvPath) {
        $rows = Import-Csv -LiteralPath $csvPath
        $totalRows = $rows.Count
        if ($totalRows -gt 0) {
            $firstOk = [int64]$rows[0].output_ok
            $lastOk = [int64]$rows[-1].output_ok
            $deltaOutputOk = $lastOk - $firstOk

            foreach ($r in $rows) {
                if ([double]$r.v_dec_fps -gt 0) { $activeDecodedRows++ }
                if ($r.quality_pending -eq '1' -or $r.quality_pending -eq 'true') { $qualityPendingRows++ }
            }

            if ($activeDecodedRows -gt ($totalRows * 0.5) -and $deltaOutputOk -gt 0) {
                $isValidStream = $true
            } else {
                $validationNotes += "Decoded frames or output present count insufficient (active_rows=$activeDecodedRows, delta_ok=$deltaOutputOk)."
            }

            if ($qualityPendingRows -gt 0) {
                $validationNotes += "Detected $qualityPendingRows cycles with quality_pending != 0 during run."
            }
        } else {
            $validationNotes += "CSV file is empty."
        }
    } else {
        $validationNotes += "CSV file was not created."
    }

    $statusStr = if ($isValidStream) { "VALID_STREAM" } else { "NO_ACTIVE_STREAM_OR_INVALID" }

    $manifest = @{
        run_id             = $currentRunId
        description        = $cfg.Desc
        mode               = $cfg.Mode
        preview            = $cfg.Preview
        status             = $statusStr
        device_model       = $DeviceModel
        git_commit         = $hostInfo.commit
        git_branch         = $hostInfo.branch
        git_dirty          = $hostInfo.dirty
        os                 = $hostInfo.os
        cpu                = $hostInfo.cpu
        gpu                = $hostInfo.gpu
        display_hz         = $hostInfo.display_hz
        warmup_seconds     = $WarmupSeconds
        duration_seconds   = $DurationSeconds
        start_time_utc     = $startTime.ToString('o')
        end_time_utc       = $endTime.ToString('o')
        csv_file           = "metrics.csv"
        total_cycles       = $totalRows
        active_cycles      = $activeDecodedRows
        delta_output_ok    = $deltaOutputOk
        quality_pending    = $qualityPendingRows
        validation_notes   = $validationNotes
    }

    $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding utf8

    Write-Host "Manifest written to: $manifestPath" -ForegroundColor Gray
    if ($isValidStream) {
        Write-Host "RUN VALIDATION PASSED ($activeDecodedRows active cycles, delta_ok=$deltaOutputOk)" -ForegroundColor Green
    } else {
        Write-Host "RUN VALIDATION WARNING: Stream not active or invalid ($($validationNotes -join '; '))" -ForegroundColor Red
    }
}

Write-Host "`nAll benchmark tasks complete." -ForegroundColor Cyan