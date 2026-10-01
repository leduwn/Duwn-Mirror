<#
.SYNOPSIS
    Automates A/B receiver latency benchmarking runs for Duwn Mirror.
.DESCRIPTION
    Executes a structured benchmark run for one or all six configurations:
    B_OFF, B_ON, F_OFF, F_ON, C_OFF, C_ON.
    Enforces manual UI configuration confirmation and live telemetry pre-check before measurement.
    Isolates each run with byte offsets, recording raw logs alongside metrics CSV into timestamped session directories.
    Strictly validates target mode, policy parameters, preview status, quality_pending, and active decoded/present streams.
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
.PARAMETER LogPath
    Path to duwn-mirror.log file.
.PARAMETER NonInteractive
    Skip interactive prompts (used for automated fixture testing).
.PARAMETER SessionId
    Explicit session directory identifier (default: auto timestamp).
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
    [string]$DeviceModel = 'Unspecified iPhone (AirPlay)',

    [Parameter(Mandatory = $false)]
    [string]$LogPath = (Join-Path $env:LOCALAPPDATA 'DUWN Mirror\Logs\duwn-mirror.log'),

    [Parameter(Mandatory = $false)]
    [switch]$NonInteractive,

    [Parameter(Mandatory = $false)]
    [string]$SessionId = $null
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$repoRoot = Split-Path -Parent $scriptDir

if ([string]::IsNullOrWhiteSpace($SessionId)) {
    $SessionId = "session_" + (Get-Date -Format "yyyyMMdd_HHmmss")
}

$sessionsRootDir = Join-Path $repoRoot 'benchmarks/sessions'
$currentSessionDir = Join-Path $sessionsRootDir $SessionId

# Configuration matrix
$configs = @{
    'B_OFF' = @{ Mode = 'Balanced'; Preview = 'OFF'; Desc = 'Balanced (SmoothLive), Preview OFF'; TargetMaxQ = '3'; TargetResMs = $null }
    'B_ON'  = @{ Mode = 'Balanced'; Preview = 'ON';  Desc = 'Balanced (SmoothLive), Preview ON';  TargetMaxQ = '3'; TargetResMs = $null }
    'F_OFF' = @{ Mode = 'Fastest';  Preview = 'OFF'; Desc = 'Fastest (LowLatency 1-frame), Preview OFF'; TargetMaxQ = '1'; TargetResMs = $null }
    'F_ON'  = @{ Mode = 'Fastest';  Preview = 'ON';  Desc = 'Fastest (LowLatency 1-frame), Preview ON';  TargetMaxQ = '1'; TargetResMs = $null }
    'C_OFF' = @{ Mode = 'Custom';   Preview = 'OFF'; Desc = 'Custom (2 frames / 25 ms), Preview OFF'; TargetMaxQ = '2'; TargetResMs = '25' }
    'C_ON'  = @{ Mode = 'Custom';   Preview = 'ON';  Desc = 'Custom (2 frames / 25 ms), Preview ON';  TargetMaxQ = '2'; TargetResMs = '25' }
}

$runsToExecute = if ($RunId -eq 'All') {
    @('B_OFF', 'B_ON', 'F_OFF', 'F_ON', 'C_OFF', 'C_ON')
} else {
    @($RunId)
}

# Collect host environment info
function Get-HostManifest {
    $commit = & git -C $repoRoot rev-parse HEAD 2>$null
    if (-not $commit) { $commit = 'unknown' }
    $dirty = (& git -C $repoRoot status --porcelain 2>$null).Length -gt 0
    $branch = & git -C $repoRoot rev-parse --abbrev-ref HEAD 2>$null
    if (-not $branch) { $branch = 'unknown' }

    $os = 'Unknown OS'
    $cpu = 'Unknown CPU'
    $gpu = 'Unknown GPU'
    try {
        $osObj = Get-CimInstance Win32_OperatingSystem -ErrorAction SilentlyContinue
        if ($osObj) { $os = $osObj.Caption }
    } catch {}
    try {
        $cpuObj = Get-CimInstance Win32_Processor -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($cpuObj) { $cpu = $cpuObj.Name }
    } catch {}
    try {
        $gpuObj = Get-CimInstance Win32_VideoController -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($gpuObj) { $gpu = $gpuObj.Name }
    } catch {}

    # Display refresh rate: strictly query CurrentRefreshRate or VideoModeDescription, never rate denominator
    $displayHz = 'unknown'
    try {
        $vc = Get-CimInstance Win32_VideoController -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($vc -and $vc.CurrentRefreshRate -and [int64]$vc.CurrentRefreshRate -gt 0) {
            $displayHz = "$($vc.CurrentRefreshRate) Hz"
        } elseif ($vc -and $vc.VideoModeDescription -match '(?<hz>\d+)\s*(?:Hz|Hertz)') {
            $displayHz = "$($Matches.hz) Hz"
        }
    } catch {}

    # Locate running process binary or build artifact
    $exePath = 'unknown'
    $exeHash = 'unknown'
    $exeTime = 'unknown'
    try {
        $proc = Get-Process duwn-mirror -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($proc -and $proc.MainModule -and $proc.MainModule.FileName) {
            $exePath = $proc.MainModule.FileName
        } else {
            $candidate = Join-Path $repoRoot 'build-msvc\bin\Release\duwn-mirror.exe'
            if (Test-Path -LiteralPath $candidate) {
                $exePath = (Get-Item -LiteralPath $candidate).FullName
            }
        }
        if ($exePath -ne 'unknown' -and (Test-Path -LiteralPath $exePath)) {
            $exeHash = (Get-FileHash -LiteralPath $exePath -Algorithm SHA256).Hash
            $exeTime = (Get-Item -LiteralPath $exePath).LastWriteTimeUtc.ToString('o')
        }
    } catch {}

    return @{
        commit         = $commit
        branch         = $branch
        dirty          = $dirty
        os             = $os
        cpu            = $cpu
        gpu            = $gpu
        display_hz     = $displayHz
        exe_path       = $exePath
        exe_sha256     = $exeHash
        exe_timestamp  = $exeTime
    }
}

$hostInfo = Get-HostManifest

function Test-LiveTelemetryTarget {
    param(
        [string]$Path,
        [hashtable]$ExpectedCfg
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        return @{
            IsReady = $false
            Errors = @("Log file not found at: $Path")
            Details = @{}
        }
    }

    $tailLines = @()
    try {
        $fs = [System.IO.FileStream]::new($Path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
        try {
            $seekPos = [Math]::Max(0L, $fs.Length - 16384L)
            $null = $fs.Seek($seekPos, [System.IO.SeekOrigin]::Begin)
            $buf = New-Object byte[] ([int]($fs.Length - $seekPos))
            $read = $fs.Read($buf, 0, $buf.Length)
            $tailText = [System.Text.Encoding]::UTF8.GetString($buf, 0, $read)
            $tailLines = $tailText -split "`r?`n"
        } finally {
            $fs.Dispose()
        }
    } catch {
        return @{
            IsReady = $false
            Errors = @("Failed to read log file: $($_.Exception.Message)")
            Details = @{}
        }
    }

    $lastMeta = $null
    $lastVideo = $null
    $lastPresent = $null
    $lastOutAge = $null

    for ($i = $tailLines.Count - 1; $i -ge 0; $i--) {
        $line = $tailLines[$i]
        if ($null -eq $lastMeta -and $line -match '\[METADATA\](?: cycle=\d+ \|)? commit=(?<commit>[^ ]*) \| transport=[^ ]* \| stream_mode=(?<mode>[^ ]*) \| policy\(max_q=(?<mq>\d+), res_ms=(?<rms>\d+), cad_pct=\d+, always_latest=(?<al>\d+)\) \| req_quality=[^ |]*(?: \| active_quality=[^ |]* \| quality_pending=(?<qp>[^ |]*))?(?: \| preview_visible=(?<pv>[^ |]*))?') {
            $lastMeta = @{
                commit          = $Matches.commit
                mode            = $Matches.mode
                max_q           = $Matches.mq
                res_ms          = $Matches.rms
                always_latest   = $Matches.al
                quality_pending = if ($Matches.ContainsKey('qp')) { $Matches.qp } else { '0' }
                preview_visible = if ($Matches.ContainsKey('pv')) { $Matches.pv } else { $null }
            }
        }
        if ($null -eq $lastVideo -and $line -match '\[STATS\] VIDEO:.*?dec=(?<dec>\d+) fps') {
            $lastVideo = @{ dec = [int]$Matches.dec }
        }
        if ($null -eq $lastPresent -and $line -match '\[STATS\] PRESENT:.*?output\(att=(?<oatt>\d+), ok=(?<ook>\d+), skip=(?<oskip>\d+), err=(?<oerr>\d+)\) \| preview\(att=(?<patt>\d+), ok=(?<pok>\d+)') {
            $lastPresent = @{
                o_ok = [int64]$Matches.ook
                p_ok = [int64]$Matches.pok
                p_att = [int64]$Matches.patt
            }
        }
        if ($null -eq $lastOutAge -and $line -match '\[FRAME AGE OUTPUT\] count=(?<cnt>\d+)') {
            $lastOutAge = @{ count = [int64]$Matches.cnt }
        }
    }

    $errors = @()
    if ($null -eq $lastMeta) {
        $errors += "No [METADATA] telemetry record found in recent log."
    } else {
        if ($lastMeta.mode -ne $ExpectedCfg.Mode) {
            $errors += "stream_mode mismatch: telemetry reported '$($lastMeta.mode)', expected '$($ExpectedCfg.Mode)'."
        }
        if ($ExpectedCfg.Mode -eq 'Custom') {
            if ($lastMeta.max_q -ne '2' -or $lastMeta.res_ms -ne '25') {
                $errors += "Custom policy params mismatch: telemetry policy(max_q=$($lastMeta.max_q), res_ms=$($lastMeta.res_ms)), expected (max_q=2, res_ms=25)."
            }
        } elseif ($ExpectedCfg.Mode -eq 'Fastest') {
            if ($lastMeta.max_q -ne '1') {
                $errors += "Fastest policy max_q mismatch: telemetry max_q=$($lastMeta.max_q), expected 1."
            }
        } elseif ($ExpectedCfg.Mode -eq 'Balanced') {
            if ($lastMeta.max_q -ne '3') {
                $errors += "Balanced policy max_q mismatch: telemetry max_q=$($lastMeta.max_q), expected 3."
            }
        }

        if ($lastMeta.quality_pending -ne '0') {
            $errors += "quality_pending is not 0 (reported: $($lastMeta.quality_pending)). Settings not yet applied."
        }

        $expectedPrev = ($ExpectedCfg.Preview -eq 'ON')
        if ($null -ne $lastMeta.preview_visible) {
            $actualPrev = ($lastMeta.preview_visible -eq '1')
            if ($actualPrev -ne $expectedPrev) {
                $errors += "Preview visibility mismatch: telemetry preview_visible=$($lastMeta.preview_visible), expected $(if ($expectedPrev) { '1' } else { '0' })."
            }
        } elseif ($null -ne $lastPresent) {
            $hasPrevPresent = ($lastPresent.p_att -gt 0 -or $lastPresent.p_ok -gt 0)
            if ($expectedPrev -and -not $hasPrevPresent) {
                $errors += "Preview is expected ON, but present counter shows 0 preview attempts/ok."
            }
        }
    }

    if ($null -eq $lastVideo -or $lastVideo.dec -le 0) {
        $errors += "No active decoded video stream detected (v_dec_fps <= 0). Ensure iPhone AirPlay Mirroring is active."
    }

    if ($null -ne $lastOutAge -and $lastOutAge.count -le 0) {
        $errors += "out_age_count <= 0. Frame scheduler has not presented frames to display yet."
    }

    return @{
        IsReady = ($errors.Count -eq 0)
        Errors  = $errors
        Details = @{
            meta    = $lastMeta
            video   = $lastVideo
            present = $lastPresent
            out_age = $lastOutAge
        }
    }
}

$hostInfo = Get-HostManifest

Write-Host "================================================================================" -ForegroundColor Cyan
Write-Host "DUWN MIRROR A/B BENCHMARK SUITE" -ForegroundColor Cyan
Write-Host "Session ID: $SessionId" -ForegroundColor White
Write-Host "Host: OS: $($hostInfo.os) | CPU: $($hostInfo.cpu) | GPU: $($hostInfo.gpu)" -ForegroundColor Gray
Write-Host "Display Refresh: $($hostInfo.display_hz) | Workspace Git: $($hostInfo.commit) (dirty=$($hostInfo.dirty))" -ForegroundColor Gray
Write-Host "Binary: $($hostInfo.exe_path)" -ForegroundColor Gray
Write-Host "Binary SHA256: $($hostInfo.exe_sha256)" -ForegroundColor Gray
Write-Host "================================================================================" -ForegroundColor Cyan

foreach ($currentRunId in $runsToExecute) {
    $cfg = $configs[$currentRunId]
    Write-Host "`n--------------------------------------------------------------------------------" -ForegroundColor Yellow
    Write-Host "PREPARING RUN: $currentRunId ($($cfg.Desc))" -ForegroundColor Yellow
    Write-Host "--------------------------------------------------------------------------------" -ForegroundColor Yellow
    Write-Host "Target Configuration Required in Duwn Mirror GUI:" -ForegroundColor Cyan
    Write-Host "  1. Delivery / Streaming Mode: $($cfg.Mode)" -ForegroundColor White
    if ($cfg.Mode -eq 'Custom') {
        Write-Host "     - Video Queue Frames: 2" -ForegroundColor White
        Write-Host "     - Video Freshness:    25 ms" -ForegroundColor White
    }
    Write-Host "  2. Preview Window:            $($cfg.Preview)" -ForegroundColor White
    Write-Host "  3. Receiver Quality:          Auto / 1080p60 (Must remain UNCHANGED across all 6 runs)" -ForegroundColor White
    Write-Host "  4. iPhone Screen Mirroring:   Active with high-rate timer / continuous motion test video" -ForegroundColor White

    # Interactive confirmation step
    if (-not $NonInteractive) {
        $confirmed = $false
        while (-not $confirmed) {
            Write-Host "`nPlease verify the Duwn Mirror window matches target settings above." -ForegroundColor Magenta
            $userInput = Read-Host "Type [Enter] when configured and streaming (or 'q' to abort)"
            if ($userInput.Trim().ToLower() -eq 'q') {
                Write-Host "Benchmark suite aborted by user." -ForegroundColor Red
                exit 1
            }

            Write-Host "Performing pre-measurement telemetry inspection..." -ForegroundColor DarkCyan
            $preCheck = Test-LiveTelemetryTarget -Path $LogPath -ExpectedCfg $cfg
            if ($preCheck.IsReady) {
                Write-Host "Telemetry verification PASSED: target configuration active and stream verified." -ForegroundColor Green
                $confirmed = $true
            } else {
                Write-Host "Telemetry pre-check FAILED with the following issues:" -ForegroundColor Red
                foreach ($err in $preCheck.Errors) {
                    Write-Host "  * $err" -ForegroundColor Red
                }
                Write-Host "`nPlease adjust Duwn Mirror settings / iPhone connection and retry." -ForegroundColor Yellow
            }
        }
    }

    $targetRunDir = Join-Path $repoRoot (Join-Path $OutputDir $currentRunId)
    $sessionRunDir = Join-Path $currentSessionDir $currentRunId
    if (-not (Test-Path $targetRunDir)) { New-Item -ItemType Directory -Path $targetRunDir -Force | Out-Null }
    if (-not (Test-Path $sessionRunDir)) { New-Item -ItemType Directory -Path $sessionRunDir -Force | Out-Null }

    $csvPath = Join-Path $targetRunDir "metrics.csv"
    $rawLogPath = Join-Path $targetRunDir "metrics_raw.log"
    $manifestPath = Join-Path $targetRunDir "manifest.json"

    # Step 1: Warmup period
    $warmupStartUtc = [DateTime]::UtcNow
    if ($WarmupSeconds -gt 0) {
        Write-Host "`nWarming up pipeline ($WarmupSeconds s) for jitter buffer and swapchain stabilization..." -ForegroundColor DarkCyan
        Start-Sleep -Seconds $WarmupSeconds
    }
    $warmupEndUtc = [DateTime]::UtcNow

    # Step 2: Establish byte offset immediately before measurement
    $startByteOffset = [int64]0
    if (Test-Path -LiteralPath $LogPath) {
        $startByteOffset = [int64](Get-Item -LiteralPath $LogPath).Length
    }
    $measurementStartUtc = [DateTime]::UtcNow

    Write-Host "Starting measurement: $DurationSeconds seconds from byte offset $startByteOffset..." -ForegroundColor Green
    $collectorScript = Join-Path $scriptDir "capture-media-metrics.ps1"

    # Step 3: Run collector with offset isolation and raw log recording
    & powershell -ExecutionPolicy Bypass -File $collectorScript `
        -OutputPath $csvPath `
        -DurationSeconds $DurationSeconds `
        -LogPath $LogPath `
        -StartByteOffset $startByteOffset `
        -RawLogOutputPath $rawLogPath

    $measurementEndUtc = [DateTime]::UtcNow

    # Step 4: Strict Validation of recorded data
    $isValidStream = $false
    $validationNotes = @()
    $totalRows = 0
    $activeDecodedRows = 0
    $qualityPendingRows = 0
    $modeMismatchRows = 0
    $previewMismatchRows = 0
    $customParamMismatchRows = 0
    $deltaOutputOk = [int64]0
    $appReportedCommit = 'unknown'

    if (Test-Path -LiteralPath $csvPath) {
        $rows = @(Import-Csv -LiteralPath $csvPath)
        $totalRows = $rows.Count
        if ($totalRows -gt 0) {
            $appReportedCommit = $rows[-1].commit

            # Reset-aware calculation of delta output_ok
            $prevOk = $null
            foreach ($r in $rows) {
                if ($r.output_ok -ne '' -and $null -ne $r.output_ok) {
                    $currOk = [int64]$r.output_ok
                    if ($null -ne $prevOk) {
                        if ($currOk -ge $prevOk) {
                            $deltaOutputOk += ($currOk - $prevOk)
                        } else {
                            $deltaOutputOk += $currOk # counter reset / reconnect
                        }
                    }
                    $prevOk = $currOk
                }

                if ([double]$r.v_dec_fps -gt 0) { $activeDecodedRows++ }
                if ($r.quality_pending -ne '0' -and $r.quality_pending -ne 'false') { $qualityPendingRows++ }

                # Verify mode stability during measurement
                if ($r.stream_mode -ne $cfg.Mode) { $modeMismatchRows++ }

                if ($cfg.Mode -eq 'Custom') {
                    if ($r.policy_max_q -ne '2' -or $r.policy_res_ms -ne '25') {
                        $customParamMismatchRows++
                    }
                }

                $expPrevVal = if ($cfg.Preview -eq 'ON') { '1' } else { '0' }
                if ($r.preview_visible -ne '' -and $r.preview_visible -ne $expPrevVal) {
                    $previewMismatchRows++
                }
            }

            if ($qualityPendingRows -gt 0) {
                $validationNotes += "DISQUALIFIED: Detected $qualityPendingRows cycles with quality_pending != 0 during measurement."
            }
            if ($modeMismatchRows -gt 0) {
                $validationNotes += "DISQUALIFIED: Detected $modeMismatchRows cycles where stream_mode deviated from target '$($cfg.Mode)'."
            }
            if ($customParamMismatchRows -gt 0) {
                $validationNotes += "DISQUALIFIED: Detected $customParamMismatchRows cycles where Custom policy deviated from 2F/25ms."
            }
            if ($previewMismatchRows -gt 0) {
                $validationNotes += "DISQUALIFIED: Detected $previewMismatchRows cycles where preview_visible deviated from target $($cfg.Preview)."
            }
            if ($deltaOutputOk -le 0) {
                $validationNotes += "DISQUALIFIED: Output Present count did not advance (delta_output_ok <= 0)."
            }
            if ($activeDecodedRows -le ($totalRows * 0.5)) {
                $validationNotes += "DISQUALIFIED: Less than 50% of cycles contained active decoded frames ($activeDecodedRows / $totalRows)."
            }

            if ($validationNotes.Count -eq 0) {
                $isValidStream = $true
            }
        } else {
            $validationNotes += "DISQUALIFIED: CSV file is empty (no completed cycles captured after offset)."
        }
    } else {
        $validationNotes += "DISQUALIFIED: CSV output file was not created."
    }

    $statusStr = if ($isValidStream) { "VALID_STREAM" } else { "INVALID_OR_NO_STREAM" }

    $manifest = [ordered]@{
        run_id                 = $currentRunId
        session_id             = $SessionId
        description            = $cfg.Desc
        target_mode            = $cfg.Mode
        target_preview         = $cfg.Preview
        status                 = $statusStr
        device_model           = $DeviceModel
        git_checkout_commit    = $hostInfo.commit
        git_branch             = $hostInfo.branch
        git_is_dirty           = $hostInfo.dirty
        app_reported_commit    = $appReportedCommit
        executable_path        = $hostInfo.exe_path
        executable_sha256      = $hostInfo.exe_sha256
        executable_timestamp   = $hostInfo.exe_timestamp
        os                     = $hostInfo.os
        cpu                    = $hostInfo.cpu
        gpu                    = $hostInfo.gpu
        display_hz             = $hostInfo.display_hz
        warmup_seconds         = $WarmupSeconds
        duration_seconds       = $DurationSeconds
        warmup_start_utc       = $warmupStartUtc.ToString('o')
        warmup_end_utc         = $warmupEndUtc.ToString('o')
        measurement_start_utc  = $measurementStartUtc.ToString('o')
        measurement_end_utc    = $measurementEndUtc.ToString('o')
        start_byte_offset      = $startByteOffset
        csv_file               = "metrics.csv"
        raw_log_file           = "metrics_raw.log"
        total_cycles           = $totalRows
        active_cycles          = $activeDecodedRows
        delta_output_ok        = $deltaOutputOk
        quality_pending_cycles = $qualityPendingRows
        validation_notes       = $validationNotes
    }

    $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding utf8

    # Duplicate run artifacts into unique timestamped session folder
    Copy-Item -LiteralPath $csvPath -Destination (Join-Path $sessionRunDir "metrics.csv") -Force -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $rawLogPath) {
        Copy-Item -LiteralPath $rawLogPath -Destination (Join-Path $sessionRunDir "metrics_raw.log") -Force -ErrorAction SilentlyContinue
    }
    Copy-Item -LiteralPath $manifestPath -Destination (Join-Path $sessionRunDir "manifest.json") -Force -ErrorAction SilentlyContinue

    Write-Host "Manifest recorded to: $manifestPath" -ForegroundColor Gray
    Write-Host "Session snapshot saved to: $sessionRunDir" -ForegroundColor Gray
    if ($isValidStream) {
        Write-Host ">>> RUN VALIDATION PASSED ($activeDecodedRows active cycles, delta_ok=$deltaOutputOk) <<<" -ForegroundColor Green
    } else {
        Write-Host ">>> RUN VALIDATION FAILED: $($validationNotes -join ' | ') <<<" -ForegroundColor Red
    }
}

Write-Host "`nAll benchmark runs complete." -ForegroundColor Cyan