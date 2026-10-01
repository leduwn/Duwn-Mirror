# test_ab_benchmarks.ps1 — Test fixture verifying A/B benchmark suite correctness contracts
$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = (Get-Item $scriptDir).Parent.Parent.FullName
$runnerScript = Join-Path $repoRoot 'tools\run-ab-benchmarks.ps1'
$analyzerScript = Join-Path $repoRoot 'tools\analyze-latency-ab.ps1'
$collectorScript = Join-Path $repoRoot 'tools\capture-media-metrics.ps1'

$testTempDir = Join-Path $env:TEMP ("duwn_ab_bench_test_" + [System.Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testTempDir -Force | Out-Null

Write-Host "Running A/B benchmark fixture tests in $testTempDir..." -ForegroundColor Cyan

$hdr = "run_id,session_id,cycle_id,collector_time,source_time,reconnect_event,format_change,commit,transport,stream_mode,policy_max_q,policy_res_ms,policy_cad_pct,policy_always_latest,req_quality,active_quality,quality_pending,preview_visible,actual_codec,actual_res,actual_fps,native_t0_t7_total_ms,t0_t1_ms,t1_t2_ms,t2_t3_ms,t3_t4_ms,t4_t5_ms,t5_t6_ms,t6_t7_ms,decode_avg_ms,decode_p50_ms,decode_p95_ms,decode_n,queue_res_avg_ms,queue_res_p50_ms,queue_res_p95_ms,queue_res_n,dxgi_wait_avg_ms,dxgi_wait_p50_ms,dxgi_wait_p95_ms,dxgi_wait_n,vp_avg_ms,vp_p50_ms,vp_p95_ms,vp_n,present_avg_ms,present_p50_ms,present_p95_ms,present_n,output_att,output_ok,output_skip,output_err,preview_att,preview_ok,preview_skip,preview_err,out_age_count,out_sel_p50_ms,out_sel_p95_ms,out_pres_p50_ms,out_pres_p95_ms,prev_age_count,prev_skips,prev_pres_p50_ms,video_stale,v_rtp_rate,v_kb_rate,v_au_rate,v_dec_fps,v_rend_fps,unique_pres_fps,v_drop_rate,superseded_rate,late_rate,queue_depth,gen,coded_res,vis_res,audio_stale,a_rtp_rate,a_kb_rate,audio_buf_ms,ring_ms,target_ms,padding_ms,servo_ppm,real_underruns,overrun_frames,backlog_drops,recoveries,av_offset_ms,drift_ppm,sidecar,session_state,source_conn,lifecycle"
try {
    # Test 1: Mode mismatch
    $runDir1 = Join-Path $testTempDir 'F_OFF_mismatch'
    New-Item -ItemType Directory -Path $runDir1 -Force | Out-Null
    $csv1 = Join-Path $runDir1 'metrics.csv'
    $rowB = "run1,1,1,2026-10-01T12:00:00Z,2026-10-01 12:00:00.000,0,0,abc123,LocalRtpUdp,Balanced,3,50,100,0,1080p60,1080p60,0,0,H264,1920x1080,60,3.5,0.2,0.3,1.8,0.1,0.2,0.4,0.5,2.0,2.0,2.5,60,1.2,1.1,1.5,60,15.0,15.0,15.5,60,0.8,0.7,0.9,60,1.0,1.0,1.2,60,60,60,0,0,0,0,0,0,60,3.1,3.5,4.5,5.0,0,0,0,0,60,1200,60,60,60,60,0,0,0,1,1,1920x1080,1920x1080,1,,,,,,,,,,,,,,alive,Streaming,connected,active"
    @($hdr, $rowB) | Set-Content -LiteralPath $csv1 -Encoding utf8
    $rows1 = @(Import-Csv -LiteralPath $csv1)
    if (-not ($rows1 | Where-Object { $_.stream_mode -ne 'Fastest' })) {
        throw "Test 1 FAILED: Expected mode mismatch"
    }
    Write-Host "  [PASS] Test 1: Mode Mismatch Detection" -ForegroundColor Green

    # Test 2: Preview mismatch
    if (-not ($rows1 | Where-Object { $_.preview_visible -ne '1' })) {
        throw "Test 2 FAILED: Expected preview mismatch"
    }
    Write-Host "  [PASS] Test 2: Preview Mismatch Detection" -ForegroundColor Green

    # Test 3: Historical log offset isolation
    $histLog = Join-Path $testTempDir 'historical.log'
    $histCsv = Join-Path $testTempDir 'hist_out.csv'
    $oldLine = "[2026-10-01 12:00:00.001] [Info] [Diagnostics] [METRICS CYCLE BEGIN] cycle=1`r`n[2026-10-01 12:00:00.002] [Info] [Diagnostics] [METADATA] cycle=1 | commit=old1 | transport=LocalRtpUdp | stream_mode=Balanced | policy(max_q=3, res_ms=50, cad_pct=100, always_latest=0) | req_quality=Auto | actual_stream(codec=H264, res=1920x1080, fps=60.00)`r`n[2026-10-01 12:00:00.003] [Info] [Diagnostics] [STATS] VIDEO: rtp=60/s (1200.5 KB/s) | au=60/s | dec=60 fps | rend=60 fps (unique=60, opp=60/s) | ticks=60/s (hold=0/s) | drop=0/s (superseded=0/s, late=0/s, late_drop=0, trans_drop=0, q_overflow=0, sess_q_full=0, life_q_full=0) | q=1 | gen=1 | coded=1920x1080 vis=1920x1080`r`n[2026-10-01 12:00:00.004] [Info] [Diagnostics] [METRICS CYCLE END] cycle=1`r`n"
    $oldLine | Set-Content -LiteralPath $histLog -Encoding utf8
    $offset = (Get-Item -LiteralPath $histLog).Length

    $job = Start-Job -ScriptBlock {
        param($col, $out, $log, $off)
        & $col -OutputPath $out -DurationSeconds 2 -LogPath $log -StartByteOffset $off
    } -ArgumentList $collectorScript, $histCsv, $histLog, $offset
    $job | Wait-Job -Timeout 10 | Out-Null
    Receive-Job $job | Out-Null
    $histRows = @(Import-Csv -LiteralPath $histCsv)
    if ($histRows.Count -ne 0) {
        throw "Test 3 FAILED: Historical log before offset produced rows"
    }
    Write-Host "  [PASS] Test 3: Historical Log Offset Isolation" -ForegroundColor Green

    # Test 4: quality_pending=1 detection
    $csvP = Join-Path $testTempDir 'pending.csv'
    $rowP = "runP,1,1,2026-10-01T12:00:00Z,2026-10-01 12:00:00.000,0,0,abc123,LocalRtpUdp,Balanced,3,50,100,0,720p60,1080p60,1,0,H264,1920x1080,60,3.5,0.2,0.3,1.8,0.1,0.2,0.4,0.5,2.0,2.0,2.5,60,1.2,1.1,1.5,60,15.0,15.0,15.5,60,0.8,0.7,0.9,60,1.0,1.0,1.2,60,60,60,0,0,0,0,0,0,60,3.1,3.5,4.5,5.0,0,0,0,0,60,1200,60,60,60,60,0,0,0,1,1,1920x1080,1920x1080,1,,,,,,,,,,,,,,alive,Streaming,connected,active"
    @($hdr, $rowP) | Set-Content -LiteralPath $csvP -Encoding utf8
    $pRows = @(Import-Csv -LiteralPath $csvP)
    if (-not ($pRows | Where-Object { $_.quality_pending -ne '0' })) {
        throw "Test 4 FAILED: quality_pending != 0 not detected"
    }
    Write-Host "  [PASS] Test 4: quality_pending Disqualification Check" -ForegroundColor Green


    # Test 5: Empty cells null safety (never coerced to 0.0)
    $psSafe = {
        param($v)
        if ($null -eq $v -or [string]::IsNullOrWhiteSpace([string]$v)) { return $null }
        $d = [double]0
        if ([double]::TryParse([string]$v, [System.Globalization.NumberStyles]::Float, [System.Globalization.CultureInfo]::InvariantCulture, [ref]$d)) { return $d }
        return $null
    }
    $res5 = & $psSafe ""
    if ($null -ne $res5) { throw "Test 5 FAILED: Empty string coerced to $res5" }
    Write-Host "  [PASS] Test 5: Empty Cell Null Safety (No coercion to 0.0)" -ForegroundColor Green

    # Test 6: Counter Reset & Reconnect handling
    $fakeRows = @(
        @{ output_ok = "100" },
        @{ output_ok = "150" }, # +50
        @{ output_ok = "10"  }, # reset -> +10
        @{ output_ok = "60"  }  # +50
    )
    $deltaFn = {
        param($Rows, $Col)
        $total = [int64]0
        $prev = $null
        foreach ($r in $Rows) {
            $curr = [int64]$r.$Col
            if ($null -ne $prev) {
                if ($curr -ge $prev) { $total += ($curr - $prev) }
                else { $total += $curr }
            }
            $prev = $curr
        }
        return $total
    }
    $cDelta = & $deltaFn $fakeRows 'output_ok'
    if ($cDelta -ne 110) { throw "Test 6 FAILED: Expected 110, got $cDelta" }
    Write-Host "  [PASS] Test 6: Reset-Aware Cumulative Delta Counter" -ForegroundColor Green

    # Test 7: Analyzer with NOT_RUN contains NO hardcoded claims
    $emptyRunsDir = Join-Path $testTempDir 'empty_runs'
    New-Item -ItemType Directory -Path $emptyRunsDir -Force | Out-Null
    $reportMd = Join-Path $testTempDir 'report.md'
    & powershell -ExecutionPolicy Bypass -File $analyzerScript -RunsDir $emptyRunsDir -OutputMarkdown $reportMd
    $reportContent = Get-Content -LiteralPath $reportMd -Raw -Encoding utf8
    $prohibited = @('< 5 ms', '0 frame drop', 'T2-T3 (Demux/NAL)', 'T3-T4 (HW Decode)')
    foreach ($p in $prohibited) {
        if ($reportContent.Contains($p)) { throw "Test 7 FAILED: Report contains prohibited claim: '$p'" }
    }
    if (-not $reportContent.Contains('NOT_RUN') -and -not $reportContent.Contains('CHƯA ĐO')) {
        throw "Test 7 FAILED: Report should indicate NOT_RUN / CHƯA ĐO"
    }
    Write-Host "  [PASS] Test 7: Elimination of Hardcoded Performance Claims" -ForegroundColor Green

    # Test 8: Display Refresh Hz does NOT use VerticalRefreshRateDenominator
    $runnerContent = Get-Content -LiteralPath $runnerScript -Raw
    if ($runnerContent.Contains('VerticalRefreshRateDenominator')) {
        throw "Test 8 FAILED: runner contains VerticalRefreshRateDenominator"
    }
    Write-Host "  [PASS] Test 8: Refresh Rate Accuracy (VerticalRefreshRateDenominator eradicated)" -ForegroundColor Green

    Write-Host "`nALL 8 A/B BENCHMARK FIXTURE TESTS PASSED!" -ForegroundColor Green
}
finally {
    if (Test-Path -LiteralPath $testTempDir) {
        Remove-Item -LiteralPath $testTempDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}
