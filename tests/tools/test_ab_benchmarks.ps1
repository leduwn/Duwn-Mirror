# test_ab_benchmarks.ps1 — Test fixture verifying A/B benchmark suite correctness contracts
$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = (Get-Item $scriptDir).Parent.Parent.FullName
$runnerScript = Join-Path $repoRoot 'tools\run-ab-benchmarks.ps1'
$analyzerScript = Join-Path $repoRoot 'tools\analyze-latency-ab.ps1'
$collectorScript = Join-Path $repoRoot 'tools\capture-media-metrics.ps1'

$testTempDir = Join-Path $env:TEMP ("duwn_ab_bench_test_" + [System.Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testTempDir -Force | Out-Null

Write-Host "Running comprehensive A/B benchmark fixture tests in $testTempDir..." -ForegroundColor Cyan

$hdr = "run_id,session_id,cycle_id,collector_time,source_time,reconnect_event,format_change,commit,transport,stream_mode,policy_max_q,policy_res_ms,policy_cad_pct,policy_always_latest,req_quality,active_quality,quality_pending,preview_visible,actual_codec,actual_res,actual_fps,native_t0_t7_total_ms,t0_t1_ms,t1_t2_ms,t2_t3_ms,t3_t4_ms,t4_t5_ms,t5_t6_ms,t6_t7_ms,decode_avg_ms,decode_p50_ms,decode_p95_ms,decode_n,queue_res_avg_ms,queue_res_p50_ms,queue_res_p95_ms,queue_res_n,dxgi_wait_avg_ms,dxgi_wait_p50_ms,dxgi_wait_p95_ms,dxgi_wait_n,vp_avg_ms,vp_p50_ms,vp_p95_ms,vp_n,present_avg_ms,present_p50_ms,present_p95_ms,present_n,output_att,output_ok,output_skip,output_err,preview_att,preview_ok,preview_skip,preview_err,out_age_count,out_sel_p50_ms,out_sel_p95_ms,out_pres_p50_ms,out_pres_p95_ms,prev_age_count,prev_skips,prev_pres_p50_ms,video_stale,v_rtp_rate,v_kb_rate,v_au_rate,v_dec_fps,v_rend_fps,unique_pres_fps,v_drop_rate,superseded_rate,late_rate,queue_depth,gen,coded_res,vis_res,audio_stale,a_rtp_rate,a_kb_rate,audio_buf_ms,ring_ms,target_ms,padding_ms,servo_ppm,real_underruns,overrun_frames,backlog_drops,recoveries,av_offset_ms,drift_ppm,sidecar,session_state,source_conn,lifecycle"

function New-TestCsvRow {
    param(
        $CycleId = "1",
        $StreamMode = "Balanced",
        $MaxQ = "3",
        $ResMs = "50",
        $QualityPending = "0",
        $PreviewVisible = "0",
        $DecFps = "60",
        $OutputOk = "60",
        $OutputSkip = "0",
        $RealUnderruns = "0",
        $BacklogDrops = "0",
        $OutAgeCount = "60"
    )
    return "testrun,1,$CycleId,2026-10-01T12:00:00Z,2026-10-01 12:00:00.000,0,0,abc123,LocalRtpUdp,$StreamMode,$MaxQ,$ResMs,100,0,1080p60,1080p60,$QualityPending,$PreviewVisible,H264,1920x1080,60,3.5,0.2,0.3,1.8,0.1,0.2,0.4,0.5,2.0,2.0,2.5,60,1.2,1.1,1.5,60,15.0,15.0,15.5,60,0.8,0.7,0.9,60,1.0,1.0,1.2,60,60,$OutputOk,$OutputSkip,0,0,0,0,0,$OutAgeCount,3.1,3.5,4.5,5.0,0,0,0,0,60,1200,60,$DecFps,60,60,0,0,0,1,1,1920x1080,1920x1080,0,100,35.0,25.0,12.5,20.0,12.0,0.0,$RealUnderruns,0,$BacklogDrops,0,0.0,0.0,alive,Streaming,connected,active"
}

function New-TestManifest {
    param(
        $RunId,
        $Mode,
        $Preview,
        $Status = "VALID_STREAM"
    )
    return [ordered]@{
        run_id         = $RunId
        target_mode    = $Mode
        target_preview = $Preview
        status         = $Status
        csv_file       = "metrics.csv"
    } | ConvertTo-Json -Depth 3
}
function Setup-ProdTestRuns([string]$TargetDir) {
    # 1. B_OFF: Perfectly configured run with real stream, output, and frame age
    $bOffDir = Join-Path $TargetDir 'B_OFF'
    New-Item -ItemType Directory -Path $bOffDir -Force | Out-Null
    (New-TestManifest -RunId 'B_OFF' -Mode 'Balanced' -Preview 'OFF' -Status 'VALID_STREAM') | Set-Content -LiteralPath (Join-Path $bOffDir 'manifest.json') -Encoding utf8
    @($hdr,
      (New-TestCsvRow -CycleId "1" -StreamMode "Balanced" -MaxQ "3" -ResMs "50" -QualityPending "0" -PreviewVisible "0" -DecFps "60" -OutputOk "60" -OutAgeCount "60"),
      (New-TestCsvRow -CycleId "2" -StreamMode "Balanced" -MaxQ "3" -ResMs "50" -QualityPending "0" -PreviewVisible "0" -DecFps "60" -OutputOk "120" -OutAgeCount "60"),
      (New-TestCsvRow -CycleId "3" -StreamMode "Balanced" -MaxQ "3" -ResMs "50" -QualityPending "0" -PreviewVisible "0" -DecFps "60" -OutputOk "180" -OutAgeCount "60")
    ) | Set-Content -LiteralPath (Join-Path $bOffDir 'metrics.csv') -Encoding utf8

    # 2. F_OFF: Target Fastest but CSV stream_mode is Balanced
    $fOffDir = Join-Path $TargetDir 'F_OFF'
    New-Item -ItemType Directory -Path $fOffDir -Force | Out-Null
    (New-TestManifest -RunId 'F_OFF' -Mode 'Fastest' -Preview 'OFF' -Status 'VALID_STREAM') | Set-Content -LiteralPath (Join-Path $fOffDir 'manifest.json') -Encoding utf8
    @($hdr,
      (New-TestCsvRow -CycleId "1" -StreamMode "Balanced" -MaxQ "3" -ResMs "50" -QualityPending "0" -PreviewVisible "0" -DecFps "60" -OutputOk "60" -OutAgeCount "60"),
      (New-TestCsvRow -CycleId "2" -StreamMode "Balanced" -MaxQ "3" -ResMs "50" -QualityPending "0" -PreviewVisible "0" -DecFps "60" -OutputOk "120" -OutAgeCount "60")
    ) | Set-Content -LiteralPath (Join-Path $fOffDir 'metrics.csv') -Encoding utf8

    # 3. B_ON: Preview mismatch (target ON but telemetry reports preview_visible=0)
    $bOnDir = Join-Path $TargetDir 'B_ON'
    New-Item -ItemType Directory -Path $bOnDir -Force | Out-Null
    (New-TestManifest -RunId 'B_ON' -Mode 'Balanced' -Preview 'ON' -Status 'VALID_STREAM') | Set-Content -LiteralPath (Join-Path $bOnDir 'manifest.json') -Encoding utf8
    @($hdr,
      (New-TestCsvRow -CycleId "1" -StreamMode "Balanced" -MaxQ "3" -ResMs "50" -QualityPending "0" -PreviewVisible "0" -DecFps "60" -OutputOk "60" -OutAgeCount "60"),
      (New-TestCsvRow -CycleId "2" -StreamMode "Balanced" -MaxQ "3" -ResMs "50" -QualityPending "0" -PreviewVisible "0" -DecFps "60" -OutputOk "120" -OutAgeCount "60")
    ) | Set-Content -LiteralPath (Join-Path $bOnDir 'metrics.csv') -Encoding utf8

    # 4. F_ON: Manifest INVALID_OR_NO_STREAM despite CSV having positive FPS/output
    $fOnDir = Join-Path $TargetDir 'F_ON'
    New-Item -ItemType Directory -Path $fOnDir -Force | Out-Null
    (New-TestManifest -RunId 'F_ON' -Mode 'Fastest' -Preview 'ON' -Status 'INVALID_OR_NO_STREAM') | Set-Content -LiteralPath (Join-Path $fOnDir 'manifest.json') -Encoding utf8
    @($hdr,
      (New-TestCsvRow -CycleId "1" -StreamMode "Fastest" -MaxQ "1" -ResMs "0" -QualityPending "0" -PreviewVisible "1" -DecFps "60" -OutputOk "60" -OutAgeCount "60"),
      (New-TestCsvRow -CycleId "2" -StreamMode "Fastest" -MaxQ "1" -ResMs "0" -QualityPending "0" -PreviewVisible "1" -DecFps "60" -OutputOk "120" -OutAgeCount "60")
    ) | Set-Content -LiteralPath (Join-Path $fOnDir 'metrics.csv') -Encoding utf8

    # 5. C_OFF: Single cycle quality_pending=1 among good cycles -> Entire run disqualified
    $cOffDir = Join-Path $TargetDir 'C_OFF'
    New-Item -ItemType Directory -Path $cOffDir -Force | Out-Null
    (New-TestManifest -RunId 'C_OFF' -Mode 'Custom' -Preview 'OFF' -Status 'VALID_STREAM') | Set-Content -LiteralPath (Join-Path $cOffDir 'manifest.json') -Encoding utf8
    @($hdr,
      (New-TestCsvRow -CycleId "1" -StreamMode "Custom" -MaxQ "2" -ResMs "25" -QualityPending "0" -PreviewVisible "0" -DecFps "60" -OutputOk "60" -OutAgeCount "60"),
      (New-TestCsvRow -CycleId "2" -StreamMode "Custom" -MaxQ "2" -ResMs "25" -QualityPending "1" -PreviewVisible "0" -DecFps "60" -OutputOk "120" -OutAgeCount "60"),
      (New-TestCsvRow -CycleId "3" -StreamMode "Custom" -MaxQ "2" -ResMs "25" -QualityPending "0" -PreviewVisible "0" -DecFps "60" -OutputOk "180" -OutAgeCount "60")
    ) | Set-Content -LiteralPath (Join-Path $cOffDir 'metrics.csv') -Encoding utf8

    # 6. C_ON: Incident counter (real_underruns) increments during video loss (dec=0) then resets reconnect -> accurate total delta
    $cOnDir = Join-Path $TargetDir 'C_ON'
    New-Item -ItemType Directory -Path $cOnDir -Force | Out-Null
    (New-TestManifest -RunId 'C_ON' -Mode 'Custom' -Preview 'ON' -Status 'VALID_STREAM') | Set-Content -LiteralPath (Join-Path $cOnDir 'manifest.json') -Encoding utf8
    @($hdr,
      (New-TestCsvRow -CycleId "1" -StreamMode "Custom" -MaxQ "2" -ResMs "25" -QualityPending "0" -PreviewVisible "1" -DecFps "60" -OutputOk "60" -RealUnderruns "5" -OutAgeCount "60"),
      (New-TestCsvRow -CycleId "2" -StreamMode "Custom" -MaxQ "2" -ResMs "25" -QualityPending "0" -PreviewVisible "1" -DecFps "0"  -OutputOk "60" -RealUnderruns "15" -OutAgeCount "60"),
      (New-TestCsvRow -CycleId "3" -StreamMode "Custom" -MaxQ "2" -ResMs "25" -QualityPending "0" -PreviewVisible "1" -DecFps "60" -OutputOk "120" -RealUnderruns "3" -OutAgeCount "60")
    ) | Set-Content -LiteralPath (Join-Path $cOnDir 'metrics.csv') -Encoding utf8
}

try {
    # ------------------------------------------------------------------------
    # Test Suite 1-6: Production Analyzer Validation Contract on Real Manifests + CSVs
    # ------------------------------------------------------------------------
    $prodRunsDir = Join-Path $testTempDir 'prod_runs'
    New-Item -ItemType Directory -Path $prodRunsDir -Force | Out-Null
    Setup-ProdTestRuns $prodRunsDir

    $prodReportMd = Join-Path $testTempDir 'prod_report.md'
    $prodReportJson = Join-Path $testTempDir 'prod_report.json'
    & "$PSHOME\powershell.exe" -ExecutionPolicy Bypass -File $analyzerScript -RunsDir $prodRunsDir -OutputMarkdown $prodReportMd
    $prodJson = Get-Content -LiteralPath $prodReportJson -Raw -Encoding utf8 | ConvertFrom-Json

    # Assert 1: B_OFF is VALID
    if ($prodJson.B_OFF.status -ne 'VALID') {
        throw "Test 1 FAILED: Expected B_OFF status 'VALID', got '$($prodJson.B_OFF.status)' (notes: $($prodJson.B_OFF.notes))"
    }
    Write-Host "  [PASS] Test 1: Production Analyzer validates standard valid stream (B_OFF)" -ForegroundColor Green

    # Assert 2: F_OFF disqualified due to mode mismatch
    if ($prodJson.F_OFF.status -eq 'VALID') {
        throw "Test 2 FAILED: Expected F_OFF to be disqualified, but status is 'VALID'"
    }
    if (-not $prodJson.F_OFF.notes.Contains('stream_mode mismatch')) {
        throw "Test 2 FAILED: F_OFF notes do not mention stream_mode mismatch: '$($prodJson.F_OFF.notes)'"
    }
    Write-Host "  [PASS] Test 2: Production Analyzer rejects target mode mismatch (F_OFF)" -ForegroundColor Green

    # Assert 3: B_ON disqualified due to preview mismatch
    if ($prodJson.B_ON.status -eq 'VALID') {
        throw "Test 3 FAILED: Expected B_ON to be disqualified, but status is 'VALID'"
    }
    if (-not $prodJson.B_ON.notes.Contains('preview_visible mismatch')) {
        throw "Test 3 FAILED: B_ON notes do not mention preview_visible mismatch: '$($prodJson.B_ON.notes)'"
    }
    Write-Host "  [PASS] Test 3: Production Analyzer rejects preview mismatch (B_ON)" -ForegroundColor Green

    # Assert 4: F_ON rejected because Manifest is INVALID_OR_NO_STREAM
    if ($prodJson.F_ON.status -eq 'VALID') {
        throw "Test 4 FAILED: Expected F_ON to not be VALID when manifest status is INVALID_OR_NO_STREAM"
    }
    Write-Host "  [PASS] Test 4: Production Analyzer respects manifest status INVALID_OR_NO_STREAM (F_ON)" -ForegroundColor Green

    # Assert 5: C_OFF disqualified due to single quality_pending=1 cycle
    if ($prodJson.C_OFF.status -eq 'VALID') {
        throw "Test 5 FAILED: Expected C_OFF to be disqualified when cycle 2 has quality_pending=1"
    }
    if (-not $prodJson.C_OFF.notes.Contains('quality_pending is not 0')) {
        throw "Test 5 FAILED: C_OFF notes do not mention quality_pending: '$($prodJson.C_OFF.notes)'"
    }
    Write-Host "  [PASS] Test 5: Production Analyzer disqualifies run with any quality_pending != 0 cycle (C_OFF)" -ForegroundColor Green

    # Assert 6: C_ON computes incident counters across entire window (including video loss) with reset handling
    if ($prodJson.C_ON.status -ne 'VALID') {
        throw "Test 6 FAILED: Expected C_ON status 'VALID', got '$($prodJson.C_ON.status)' (notes: $($prodJson.C_ON.notes))"
    }
    if ($prodJson.C_ON.real_underruns -ne 13) {
        throw "Test 6 FAILED: Expected C_ON real_underruns = 13 (inclusive of video loss period and reset), got $($prodJson.C_ON.real_underruns)"
    }
    Write-Host "  [PASS] Test 6: Incident counter accurately accumulates across video loss cycles & resets (C_ON)" -ForegroundColor Green


    # ------------------------------------------------------------------------
    # Test 7: Collector Strict Cycles Contract
    # Incomplete cycle (missing END) or mismatched cycle_id between BEGIN/END must NOT emit benchmark rows
    # ------------------------------------------------------------------------
    $strictTestLog = Join-Path $testTempDir 'strict_test.log'
    $strictTestCsv = Join-Path $testTempDir 'strict_test.csv'

    $logLines = @(
        # Valid Cycle 1
        "[2026-10-01 12:00:00.001] [Info] [Diagnostics] [METRICS CYCLE BEGIN] cycle=1",
        "[2026-10-01 12:00:00.002] [Info] [Diagnostics] [METADATA] cycle=1 | commit=s1 | transport=LocalRtpUdp | stream_mode=Balanced | policy(max_q=3, res_ms=50, cad_pct=100, always_latest=0) | req_quality=Auto | actual_stream(codec=H264, res=1920x1080, fps=60.00)",
        "[2026-10-01 12:00:00.003] [Info] [Diagnostics] [STATS] VIDEO: rtp=60/s (1200.5 KB/s) | au=60/s | dec=60 fps | rend=60 fps (unique=60, opp=60/s) | ticks=60/s (hold=0/s) | drop=0/s (superseded=0/s, late=0/s, late_drop=0, trans_drop=0, q_overflow=0, sess_q_full=0, life_q_full=0) | q=1 | gen=1 | coded=1920x1080 vis=1920x1080",
        "[2026-10-01 12:00:00.004] [Info] [Diagnostics] [METRICS CYCLE END] cycle=1",

        # Incomplete Cycle 2 (no END before next BEGIN)
        "[2026-10-01 12:00:01.001] [Info] [Diagnostics] [METRICS CYCLE BEGIN] cycle=2",
        "[2026-10-01 12:00:01.002] [Info] [Diagnostics] [METADATA] cycle=2 | commit=s2 | transport=LocalRtpUdp | stream_mode=Balanced | policy(max_q=3, res_ms=50, cad_pct=100, always_latest=0) | req_quality=Auto | actual_stream(codec=H264, res=1920x1080, fps=60.00)",
        "[2026-10-01 12:00:01.003] [Info] [Diagnostics] [STATS] VIDEO: rtp=60/s (1200.5 KB/s) | au=60/s | dec=60 fps | rend=60 fps (unique=60, opp=60/s) | ticks=60/s (hold=0/s) | drop=0/s (superseded=0/s, late=0/s, late_drop=0, trans_drop=0, q_overflow=0, sess_q_full=0, life_q_full=0) | q=1 | gen=1 | coded=1920x1080 vis=1920x1080",

        # Mismatched Cycle 3 (BEGIN cycle=3, END cycle=99)
        "[2026-10-01 12:00:02.001] [Info] [Diagnostics] [METRICS CYCLE BEGIN] cycle=3",
        "[2026-10-01 12:00:02.002] [Info] [Diagnostics] [METADATA] cycle=3 | commit=s3 | transport=LocalRtpUdp | stream_mode=Balanced | policy(max_q=3, res_ms=50, cad_pct=100, always_latest=0) | req_quality=Auto | actual_stream(codec=H264, res=1920x1080, fps=60.00)",
        "[2026-10-01 12:00:02.003] [Info] [Diagnostics] [STATS] VIDEO: rtp=60/s (1200.5 KB/s) | au=60/s | dec=60 fps | rend=60 fps (unique=60, opp=60/s) | ticks=60/s (hold=0/s) | drop=0/s (superseded=0/s, late=0/s, late_drop=0, trans_drop=0, q_overflow=0, sess_q_full=0, life_q_full=0) | q=1 | gen=1 | coded=1920x1080 vis=1920x1080",
        "[2026-10-01 12:00:02.004] [Info] [Diagnostics] [METRICS CYCLE END] cycle=99",

        # Incomplete Cycle 4 at EOF
        "[2026-10-01 12:00:03.001] [Info] [Diagnostics] [METRICS CYCLE BEGIN] cycle=4",
        "[2026-10-01 12:00:03.002] [Info] [Diagnostics] [METADATA] cycle=4 | commit=s4 | transport=LocalRtpUdp | stream_mode=Balanced | policy(max_q=3, res_ms=50, cad_pct=100, always_latest=0) | req_quality=Auto | actual_stream(codec=H264, res=1920x1080, fps=60.00)"
    ) -join "`r`n"

    $logLines | Set-Content -LiteralPath $strictTestLog -Encoding utf8

    $job = Start-Job -ScriptBlock {
        param($col, $out, $log)
        & $col -OutputPath $out -DurationSeconds 2 -LogPath $log -StrictCycles
    } -ArgumentList $collectorScript, $strictTestCsv, $strictTestLog
    $job | Wait-Job -Timeout 10 | Out-Null
    Receive-Job $job | Out-Null

    $strictRows = @(Import-Csv -LiteralPath $strictTestCsv)
    if ($strictRows.Count -ne 1) {
        throw "Test 7 FAILED: Expected exactly 1 valid cycle under -StrictCycles, got $($strictRows.Count)"
    }
    if ($strictRows[0].cycle_id -ne '1') {
        throw "Test 7 FAILED: Emitted cycle_id is '$($strictRows[0].cycle_id)', expected '1'"
    }
    Write-Host "  [PASS] Test 7: Collector strict cycle boundary isolation (discard incomplete & mismatched cycles)" -ForegroundColor Green

    # ------------------------------------------------------------------------
    # Test 8: Live Telemetry Target Pre-Check (FRAME AGE OUTPUT required)
    # ------------------------------------------------------------------------
    $preCheckLog = Join-Path $testTempDir 'precheck_test.log'
    $validMetaLine = "[2026-10-01 12:00:00.001] [Info] [Diagnostics] [METADATA] commit=abc | transport=LocalRtpUdp | stream_mode=Balanced | policy(max_q=3, res_ms=50, cad_pct=100, always_latest=0) | req_quality=Auto | active_quality=Auto | quality_pending=0 | preview_visible=0 | actual_stream(codec=H264, res=1920x1080, fps=60.00)"
    $validVideoLine = "[2026-10-01 12:00:00.002] [Info] [Diagnostics] [STATS] VIDEO: dec=60 fps"
    $validPresentLine = "[2026-10-01 12:00:00.003] [Info] [Diagnostics] [STATS] PRESENT: output(att=60, ok=60, skip=0, err=0) | preview(att=0, ok=0, skip=0, err=0)"
    $validAgeLine = "[2026-10-01 12:00:00.004] [Info] [Diagnostics] [FRAME AGE OUTPUT] count=60 | select: p50=3.0ms | present: p50=4.0ms"

    $valHelperPath = Join-Path $repoRoot 'tools\benchmark-validation-helpers.ps1'
    . $valHelperPath

    # Case A: Missing FRAME AGE OUTPUT record -> fails pre-check
    @($validMetaLine, $validVideoLine, $validPresentLine) | Set-Content -LiteralPath $preCheckLog -Encoding utf8
    $checkOutA = Test-LiveTelemetryTarget -Path $preCheckLog -ExpectedCfg @{ Mode='Balanced'; Preview='OFF' }
    if ($checkOutA.IsReady -eq $true) {
        throw "Test 8 FAILED: Pre-check passed despite missing FRAME AGE OUTPUT record!"
    }

    # Case B: FRAME AGE OUTPUT present with count=60 -> passes pre-check
    @($validMetaLine, $validVideoLine, $validPresentLine, $validAgeLine) | Set-Content -LiteralPath $preCheckLog -Encoding utf8
    $checkOutPass = Test-LiveTelemetryTarget -Path $preCheckLog -ExpectedCfg @{ Mode='Balanced'; Preview='OFF' }
    if ($checkOutPass.IsReady -ne $true) {
        throw "Test 8 FAILED: Pre-check failed despite valid FRAME AGE OUTPUT count=60! Errors: $($checkOutPass.Errors)"
    }
    Write-Host "  [PASS] Test 8: Live Telemetry Pre-Check strictly requires [FRAME AGE OUTPUT] count > 0" -ForegroundColor Green

    # ------------------------------------------------------------------------
    # Test 9: Historical Log Byte Offset Isolation
    # ------------------------------------------------------------------------
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
        throw "Test 9 FAILED: Historical log before offset produced rows"
    }
    Write-Host "  [PASS] Test 9: Historical Log Offset Isolation" -ForegroundColor Green

    # ------------------------------------------------------------------------
    # Test 10: Elimination of Hardcoded Performance Claims
    # ------------------------------------------------------------------------
    $emptyRunsDir = Join-Path $testTempDir 'empty_runs'
    New-Item -ItemType Directory -Path $emptyRunsDir -Force | Out-Null
    $reportMd = Join-Path $testTempDir 'report.md'
    & "$PSHOME\powershell.exe" -ExecutionPolicy Bypass -File $analyzerScript -RunsDir $emptyRunsDir -OutputMarkdown $reportMd
    $reportContent = Get-Content -LiteralPath $reportMd -Raw -Encoding utf8
    $prohibited = @('< 5 ms', '0 frame drop', 'T2-T3 (Demux/NAL)', 'T3-T4 (HW Decode)')
    foreach ($p in $prohibited) {
        if ($reportContent.Contains($p)) { throw "Test 10 FAILED: Report contains prohibited claim: '$p'" }
    }
    if (-not $reportContent.Contains('NOT_RUN') -and -not $reportContent.Contains('CHƯA ĐO')) {
        throw "Test 10 FAILED: Report should indicate NOT_RUN / CHƯA ĐO"
    }
    Write-Host "  [PASS] Test 10: Elimination of Hardcoded Performance Claims" -ForegroundColor Green

    # ------------------------------------------------------------------------
    # Test 11: Display Refresh Hz does NOT use VerticalRefreshRateDenominator
    # ------------------------------------------------------------------------
    $runnerContent = Get-Content -LiteralPath $runnerScript -Raw
    if ($runnerContent.Contains('VerticalRefreshRateDenominator')) {
        throw "Test 11 FAILED: runner contains VerticalRefreshRateDenominator"
    }
    Write-Host "  [PASS] Test 11: Refresh Rate Accuracy (VerticalRefreshRateDenominator eradicated)" -ForegroundColor Green

    Write-Host "`nALL 11 A/B BENCHMARK FIXTURE TESTS PASSED!" -ForegroundColor Green
}
finally {
    if (Test-Path -LiteralPath $testTempDir) {
        Remove-Item -LiteralPath $testTempDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}
