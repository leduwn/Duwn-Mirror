# test_capture_media_metrics.ps1 — Test fixture verifying capture-media-metrics.ps1 contracts:
# 1. Video-only logs
# 2. Audio-only logs
# 3. Stale data (no phantom rows when log pauses)
# 4. Backlog with multiple cycles
# 5. Reconnect event vs format change (rotation)
# 6. Session splitting & counter reset
# 7. Log rotation handling
# 8. Split line across two reads

$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = (Get-Item $scriptDir).Parent.Parent.FullName
$collectorScript = Join-Path $repoRoot 'tools\capture-media-metrics.ps1'

$testTempDir = Join-Path $env:TEMP ("duwn_metrics_test_" + [System.Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testTempDir -Force | Out-Null

$testLog = Join-Path $testTempDir 'duwn-mirror.log'
$testCsv = Join-Path $testTempDir 'output.csv'

Write-Host "Running collector fixture tests in $testTempDir..."

$sampleMetadata = "[2026-10-01 12:00:00.001] [Info] [Diagnostics] [METADATA] commit=38b91a5 | transport=LocalRtpUdp | stream_mode=Balanced | policy(max_q=3, res_ms=50, cad_pct=100, always_latest=0) | req_quality=Auto | actual_stream(codec=H264, res=1920x1080, fps=60.00)"
$sampleVideoStats = "[2026-10-01 12:00:00.002] [Info] [Diagnostics] [STATS] VIDEO: rtp=60/s (1200.5 KB/s) | au=60/s | dec=60 fps | rend=60 fps (unique=60, opp=60/s) | ticks=60/s (hold=0/s) | drop=0/s (superseded=0/s, late=0/s, late_drop=0, trans_drop=0, q_overflow=0, sess_q_full=0, life_q_full=0) | q=1 | gen=1 | coded=1920x1080 vis=1920x1080"
$samplePresentStats = "[2026-10-01 12:00:00.003] [Info] [Diagnostics] [STATS] PRESENT: interval_avg=16.67ms p95=16.70ms | call_avg=1.20ms p50=1.15ms p95=1.35ms | dxgi_wait_avg=15.00ms | output(att=60, ok=60, skip=0, err=0) | preview(att=60, ok=59, skip=1, err=0)"
$sampleStageLatency = "[2026-10-01 12:00:00.004] [Info] [Diagnostics] [STAGE LATENCY] decode: avg=2.10ms p50=2.00ms p95=2.50ms (n=60) | queue_res: avg=3.20ms p50=3.10ms p95=3.50ms (n=60) | dxgi_wait: avg=15.00ms p50=15.00ms p95=15.50ms max=16.00ms (n=60) | vp: avg=0.80ms p50=0.75ms p95=0.90ms (n=60) | present: avg=1.10ms p50=1.05ms p95=1.25ms (n=60)"
$sampleLatencyT0T7 = "[2026-10-01 12:00:00.005] [Info] [Diagnostics] [LATENCY] T0-T7 (native receiver latency): total=3.50ms (T0-T1=0.20ms, T1-T2=0.30ms, T2-T3=1.80ms, T3-T4=0.10ms, T4-T5[q_age]=0.20ms, T5-T6[vp]=0.40ms, T6-T7[pres]=0.50ms) | audio_buffered=24.50ms (ring=12.50ms, wasapi_padding=12.00ms) | AV_skew=unavailable"
$sampleOutAge = "[2026-10-01 12:00:00.006] [Info] [Diagnostics] [FRAME AGE OUTPUT] count=60 | select: p50=3.10ms p95=3.50ms p99=4.00ms max=4.50ms | present: p50=4.50ms p95=5.00ms p99=5.50ms max=6.00ms"
$samplePrevAge = "[2026-10-01 12:00:00.007] [Info] [Diagnostics] [FRAME AGE PREVIEW] count=59 skips=1 | select: p50=4.80ms p95=5.20ms p99=5.60ms max=6.00ms | select->pres: p50=1.00ms p95=1.20ms p99=1.40ms max=1.60ms | present: p50=5.80ms p95=6.40ms p99=7.00ms max=7.50ms"
$sampleAudioStats = "[2026-10-01 12:00:00.008] [Info] [Diagnostics] [STATS] AUDIO: rtp=100/s (35.2 KB/s) | codec=L16 (pt=96) | 44100Hz -> 48000Hz (2ch) | buf=25.0ms | silence_fill=0/s (life=0) | real_underruns=0/s (total=0, legacy=0/s) | wasapi_run=1 | resumes=0 (last_gap=0.0ms)"
$sampleAudioLatency = "[2026-10-01 12:00:00.009] [Info] [Diagnostics] [AUDIO LATENCY] packet=10.00ms gap=10.00ms peak_gap=12.00ms A0-A1=0.100 A1-A2=0.100 A2-A3=0.100 A3-A4=0.100 A5-A6=0.100ms | ring=12.50ms target=20.00ms capacity=85.33ms | padding=12.00ms period=10.00ms stream_latency=22.00ms | servo=0.0ppm resampler_delay=1.250ms | underrun_frames=0 overrun_frames=0 backlog_drops=0 recoveries=0"
$sampleSyncStats = "[2026-10-01 12:00:00.010] [Info] [Diagnostics] [STATS] SYNC/SESSION: A/V=5.2ms drift=0.10ms/min | sidecar=alive | state=Streaming | source=connected | output=1920x1080 preview=960x540 | lifecycle=Healthy"
try {
    # ------------------------------------------------------------------------
    # Test 1 & 4: Multiple Cycles in Backlog + Full Cycle parsing
    # ------------------------------------------------------------------------
    $cycle1 = @(
        $sampleMetadata, $sampleVideoStats, $samplePresentStats, $sampleStageLatency,
        $sampleLatencyT0T7, $sampleOutAge, $samplePrevAge, $sampleAudioStats,
        $sampleAudioLatency, $sampleSyncStats
    ) -join "`r`n"

    $cycle2 = $cycle1 -replace "12:00:00", "12:00:01"
    $cycle3 = $cycle1 -replace "12:00:00", "12:00:02"

    ($cycle1 + "`r`n" + $cycle2 + "`r`n" + $cycle3 + "`r`n") | Set-Content -LiteralPath $testLog -Encoding utf8

    # Start collector for 2 seconds
    $job = Start-Job -ScriptBlock {
        param($col, $out, $log)
        & $col -OutputPath $out -DurationSeconds 2 -LogPath $log
    } -ArgumentList $collectorScript, $testCsv, $testLog

    $job | Wait-Job -Timeout 10 | Out-Null
    Receive-Job $job | Out-Null

    $rows = @(Import-Csv -LiteralPath $testCsv)
    if ($rows.Count -ne 3) {
        throw "Test 1/4 FAILED: Expected 3 rows for 3 backlog cycles, got $($rows.Count)"
    }
    if ($rows[0].source_time -ne '2026-10-01 12:00:00.001') {
        throw "Test 1/4 FAILED: source_time mismatch. Got '$($rows[0].source_time)'"
    }
    if ($rows[1].source_time -ne '2026-10-01 12:00:01.001') {
        throw "Test 1/4 FAILED: second cycle source_time mismatch. Got '$($rows[1].source_time)'"
    }
    if ($rows[0].actual_codec -ne 'H264' -or $rows[0].actual_res -ne '1920x1080') {
        throw "Test 1/4 FAILED: metadata codec/res mismatch"
    }
    if ($rows[0].video_stale -ne '0' -or $rows[0].audio_stale -ne '0') {
        throw "Test 1/4 FAILED: freshness flags incorrect for full cycle: video_stale='$($rows[0].video_stale)', audio_stale='$($rows[0].audio_stale)'"
    }
    Write-Host "  [PASS] Test 1 & 4: Multiple Backlog Cycles & Full Metric Extraction"

    # ------------------------------------------------------------------------
    # Test 2: Video-Only Cycle (Audio stale)
    # ------------------------------------------------------------------------
    $testCsv2 = Join-Path $testTempDir 'output_video_only.csv'
    $videoOnlyLog = Join-Path $testTempDir 'video_only.log'
    $vOnlyLines = @(
        $sampleMetadata, $sampleVideoStats, $samplePresentStats, $sampleStageLatency,
        $sampleLatencyT0T7, $sampleOutAge, $samplePrevAge
    ) -join "`r`n"
    ($vOnlyLines + "`r`n") | Set-Content -LiteralPath $videoOnlyLog -Encoding utf8

    $job = Start-Job -ScriptBlock {
        param($col, $out, $log)
        & $col -OutputPath $out -DurationSeconds 2 -LogPath $log
    } -ArgumentList $collectorScript, $testCsv2, $videoOnlyLog
    $job | Wait-Job -Timeout 10 | Out-Null
    Receive-Job $job | Out-Null

    $rows = @(Import-Csv -LiteralPath $testCsv2)
    if ($rows.Count -lt 1) { throw "Test 2 FAILED: No rows produced for video-only cycle" }
    if ($rows[0].video_stale -ne '0') { throw "Test 2 FAILED: video_stale should be 0" }
    if ($rows[0].audio_stale -ne '1') { throw "Test 2 FAILED: audio_stale should be 1 for video-only log" }
    if ($rows[0].v_dec_fps -ne '60') { throw "Test 2 FAILED: v_dec_fps should be 60" }
    Write-Host "  [PASS] Test 2: Video-Only Logging & Stale Audio Flag"

    # ------------------------------------------------------------------------
    # Test 3: Audio-Only Cycle (Video stale)
    # ------------------------------------------------------------------------
    $testCsv3 = Join-Path $testTempDir 'output_audio_only.csv'
    $audioOnlyLog = Join-Path $testTempDir 'audio_only.log'
    $aOnlyLines = @(
        $sampleMetadata, $sampleAudioStats, $sampleAudioLatency, $sampleSyncStats
    ) -join "`r`n"
    ($aOnlyLines + "`r`n") | Set-Content -LiteralPath $audioOnlyLog -Encoding utf8

    $job = Start-Job -ScriptBlock {
        param($col, $out, $log)
        & $col -OutputPath $out -DurationSeconds 2 -LogPath $log
    } -ArgumentList $collectorScript, $testCsv3, $audioOnlyLog
    $job | Wait-Job -Timeout 10 | Out-Null
    Receive-Job $job | Out-Null

    $rows = @(Import-Csv -LiteralPath $testCsv3)
    if ($rows.Count -lt 1) { throw "Test 3 FAILED: No rows produced for audio-only cycle" }
    if ($rows[0].audio_stale -ne '0') { throw "Test 3 FAILED: audio_stale should be 0" }
    if ($rows[0].video_stale -ne '1') { throw "Test 3 FAILED: video_stale should be 1 for audio-only log" }
    if ($rows[0].ring_ms -ne '12.50') { throw "Test 3 FAILED: ring_ms should be 12.50" }
    Write-Host "  [PASS] Test 3: Audio-Only Logging & Stale Video Flag"

    # ------------------------------------------------------------------------
    # Test 4b: Paused Log / Stale Data (No phantom or duplicate rows emitted)
    # ------------------------------------------------------------------------
    $testCsvPause = Join-Path $testTempDir 'output_pause.csv'
    $pauseLog = Join-Path $testTempDir 'pause.log'
    ($cycle1 + "`r`n") | Set-Content -LiteralPath $pauseLog -Encoding utf8

    $job = Start-Job -ScriptBlock {
        param($col, $out, $log)
        & $col -OutputPath $out -DurationSeconds 3 -LogPath $log
    } -ArgumentList $collectorScript, $testCsvPause, $pauseLog
    $job | Wait-Job -Timeout 10 | Out-Null
    Receive-Job $job | Out-Null

    $rows = @(Import-Csv -LiteralPath $testCsvPause)
    if ($rows.Count -ne 1) {
        throw "Test 4b FAILED: Expected exactly 1 row when log paused, got $($rows.Count)"
    }
    Write-Host "  [PASS] Test 4b: Paused Log emits no duplicate/phantom rows"

    # ------------------------------------------------------------------------
    # Test 5 & 6: Reconnect vs Format Change & Session Id
    # ------------------------------------------------------------------------
    $testCsv5 = Join-Path $testTempDir 'output_reconnect.csv'
    $reconnectLog = Join-Path $testTempDir 'reconnect.log'

    $c1 = $cycle1
    $c2 = ($cycle1 -replace "12:00:00", "12:00:01") -replace "gen=1", "gen=2"
    $c3 = (($cycle1 -replace "12:00:00", "12:00:02") -replace "source=connected", "source=disconnected") -replace "state=Streaming", "state=Disconnected"
    $c4 = ($cycle1 -replace "12:00:00", "12:00:03") -replace "gen=1", "gen=2"

    ($c1 + "`r`n" + $c2 + "`r`n" + $c3 + "`r`n" + $c4 + "`r`n") | Set-Content -LiteralPath $reconnectLog -Encoding utf8

    $job = Start-Job -ScriptBlock {
        param($col, $out, $log)
        & $col -OutputPath $out -DurationSeconds 2 -LogPath $log
    } -ArgumentList $collectorScript, $testCsv5, $reconnectLog
    $job | Wait-Job -Timeout 10 | Out-Null
    Receive-Job $job | Out-Null

    $rows = @(Import-Csv -LiteralPath $testCsv5)
    if ($rows.Count -ne 4) { throw "Test 5 FAILED: Expected 4 rows, got $($rows.Count)" }
    if ($rows[0].reconnect_event -ne '0' -or $rows[0].format_change -ne '0') {
        throw "Test 5 FAILED: Row 0 initial flags unexpected"
    }
    if ($rows[1].format_change -ne '1' -or $rows[1].reconnect_event -ne '0') {
        throw "Test 5 FAILED: Rotation should be format_change=1, reconnect_event=0. Got fc=$($rows[1].format_change), rc=$($rows[1].reconnect_event)"
    }
    if ($rows[3].reconnect_event -ne '1') {
        throw "Test 5 FAILED: Row 3 should have reconnect_event=1"
    }
    if ([int]$rows[3].session_id -le [int]$rows[0].session_id) {
        throw "Test 5 FAILED: session_id did not increment on reconnect"
    }
    Write-Host "  [PASS] Test 5 & 6: Reconnect vs Format Change & Session Id Isolation"

    # ------------------------------------------------------------------------
    # Test 7: Log Rotation with Backup drain
    # ------------------------------------------------------------------------
    $testCsv7 = Join-Path $testTempDir 'output_rotation.csv'
    $rotationLog = Join-Path $testTempDir 'rotation.log'

    ($cycle1 + "`r`n") | Set-Content -LiteralPath $rotationLog -Encoding utf8

    $job = Start-Job -ScriptBlock {
        param($col, $out, $log)
        & $col -OutputPath $out -DurationSeconds 4 -LogPath $log
    } -ArgumentList $collectorScript, $testCsv7, $rotationLog

    Start-Sleep -Milliseconds 1200

    $backupFile = Join-Path $testTempDir 'rotation.1.log'
    Move-Item -LiteralPath $rotationLog -Destination $backupFile -Force
    ($cycle2 + "`r`n") | Set-Content -LiteralPath $rotationLog -Encoding utf8

    $job | Wait-Job -Timeout 10 | Out-Null
    Receive-Job $job | Out-Null

    $rows = @(Import-Csv -LiteralPath $testCsv7)
    if ($rows.Count -lt 2) {
        throw "Test 7 FAILED: Expected at least 2 rows across rotation, got $($rows.Count)"
    }
    Write-Host "  [PASS] Test 7: Log Rotation with Backup Drain"

    # ------------------------------------------------------------------------
    # Test 8: Partial / Split Line across two reads
    # ------------------------------------------------------------------------
    $testCsv8 = Join-Path $testTempDir 'output_split.csv'
    $splitLog = Join-Path $testTempDir 'split.log'

    $half1 = $sampleMetadata + "`r`n" + $sampleVideoStats + "`r`n" + $samplePresentStats.Substring(0, 40)
    [System.IO.File]::WriteAllText($splitLog, $half1, [System.Text.Encoding]::UTF8)

    $job = Start-Job -ScriptBlock {
        param($col, $out, $log)
        & $col -OutputPath $out -DurationSeconds 3 -LogPath $log
    } -ArgumentList $collectorScript, $testCsv8, $splitLog

    Start-Sleep -Milliseconds 1200

    $half2 = $samplePresentStats.Substring(40) + "`r`n" + $sampleStageLatency + "`r`n" + $sampleSyncStats + "`r`n"
    [System.IO.File]::AppendAllText($splitLog, $half2, [System.Text.Encoding]::UTF8)

    $job | Wait-Job -Timeout 10 | Out-Null
    Receive-Job $job | Out-Null

    $rows = @(Import-Csv -LiteralPath $testCsv8)
    if ($rows.Count -lt 1) {
        throw "Test 8 FAILED: Expected 1 row from stitched split line, got $($rows.Count)"
    }
    if ($rows[0].output_ok -ne '60') {
        throw "Test 8 FAILED: output_ok should be 60 after stitching split line. Got '$($rows[0].output_ok)'"
    }
    Write-Host "  [PASS] Test 8: Incomplete Line & Split Line Stitching"

    Write-Host "`nALL 8 COLLECTOR FIXTURE TESTS PASSED!"
}
finally {
    if (Test-Path -LiteralPath $testTempDir) {
        Remove-Item -LiteralPath $testTempDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}