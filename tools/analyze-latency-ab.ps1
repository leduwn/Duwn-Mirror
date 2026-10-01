<#
.SYNOPSIS
    Analyzes telemetry CSVs and manifests from Duwn Mirror A/B receiver latency benchmarks.
.DESCRIPTION
    Processes benchmark runs (B_OFF, B_ON, F_OFF, F_ON, C_OFF, C_ON),
    validates stream presence, extracts window distributions (mean, window-p50, window-p95),
    computes pipeline stage breakdowns (T0-T7), queue residence, frame age, drop rates,
    and formats comprehensive Markdown and JSON reports.
.PARAMETER RunsDir
    Directory containing benchmark run subdirectories (default: benchmarks/runs).
.PARAMETER OutputMarkdown
    Target markdown report path (default: benchmarks/analysis_report.md).
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $false)]
    [string]$RunsDir = 'benchmarks/runs',

    [Parameter(Mandatory = $false)]
    [string]$OutputMarkdown = 'benchmarks/analysis_report.md'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptDir = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Definition }
$repoRoot = Split-Path -Parent $scriptDir
$runsRoot = if ([System.IO.Path]::IsPathRooted($RunsDir)) { $RunsDir } else { Join-Path $repoRoot $RunsDir }
$targetMdPath = if ([System.IO.Path]::IsPathRooted($OutputMarkdown)) { $OutputMarkdown } else { Join-Path $repoRoot $OutputMarkdown }

$validationHelperPath = Join-Path $scriptDir 'benchmark-validation-helpers.ps1'
if (Test-Path -LiteralPath $validationHelperPath) {
    . $validationHelperPath
}

function Parse-DoubleSafe([object]$Value) {
    if ($null -eq $Value) { return $null }
    $s = [string]$Value
    if ([string]::IsNullOrWhiteSpace($s)) { return $null }
    $d = [double]0
    if ([double]::TryParse($s, [System.Globalization.NumberStyles]::Float, [System.Globalization.CultureInfo]::InvariantCulture, [ref]$d)) {
        return $d
    }
    return $null
}

function Get-Percentile {
    param([double[]]$Values, [double]$Percentile)
    if ($null -eq $Values -or $Values.Count -eq 0) { return $null }
    $sorted = $Values | Sort-Object
    $idx = [Math]::Floor(($Percentile / 100.0) * ($sorted.Count - 1))
    return [double]$sorted[[int]$idx]
}

function Get-Mean {
    param([double[]]$Values)
    if ($null -eq $Values -or $Values.Count -eq 0) { return $null }
    $sum = 0.0
    foreach ($v in $Values) { $sum += $v }
    return ($sum / $Values.Count)
}

function Extract-DoubleSeries([object[]]$Rows, [string]$ColName) {
    $list = [System.Collections.Generic.List[double]]::new()
    foreach ($r in $Rows) {
        $d = Parse-DoubleSafe $r.$ColName
        if ($null -ne $d) {
            $list.Add($d)
        }
    }
    return $list.ToArray()
}

function Extract-FilteredDoubleSeries {
    param(
        [object[]]$Rows,
        [string]$ColName,
        [scriptblock]$Filter = $null
    )
    $list = [System.Collections.Generic.List[double]]::new()
    foreach ($r in $Rows) {
        if ($null -ne $Filter) {
            $pass = $false
            try {
                $pass = [bool](& $Filter $r)
            } catch {
                $pass = $false
            }
            if (-not $pass) { continue }
        }
        $d = Parse-DoubleSafe $r.$ColName
        if ($null -ne $d) {
            $list.Add($d)
        }
    }
    return $list.ToArray()
}

function Get-CounterDelta {
    param([object[]]$Rows, [string]$ColumnName)
    $totalDelta = [int64]0
    $prevVal = $null
    $hasAny = $false
    foreach ($r in $Rows) {
        $raw = $r.$ColumnName
        if ($null -ne $raw -and $raw -ne '') {
            $currVal = [int64]$raw
            $hasAny = $true
            if ($null -ne $prevVal) {
                if ($currVal -ge $prevVal) {
                    $totalDelta += ($currVal - $prevVal)
                } else {
                    # Counter reset / reconnect detected
                    $totalDelta += $currVal
                }
            }
            $prevVal = $currVal
        }
    }
    if (-not $hasAny) { return $null }
    return $totalDelta
}

function Format-Val([object]$Val, [string]$Suffix = '') {
    if ($null -eq $Val) { return 'N/A' }
    return "$([Math]::Round([double]$Val, 2))$Suffix"
}

$runOrder = @('B_OFF', 'B_ON', 'F_OFF', 'F_ON', 'C_OFF', 'C_ON')
$runResults = [ordered]@{}

foreach ($runId in $runOrder) {
    $dir = Join-Path $runsRoot $runId
    if (-not (Test-Path $dir)) {
        $runResults[$runId] = @{
            run_id = $runId
            status = 'NOT_RUN'
            notes  = 'Directory not found.'
        }
        continue
    }

    $manifestPath = Join-Path $dir 'manifest.json'
    $csvPath = Join-Path $dir 'metrics.csv'

    $manifest = if (Test-Path $manifestPath) {
        try {
            Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
        } catch { $null }
    } else { $null }

    if (-not (Test-Path $csvPath)) {
        $runResults[$runId] = @{
            run_id   = $runId
            status   = 'NO_CSV'
            manifest = $manifest
            notes    = 'CSV file not found.'
        }
        continue
    }

    $rows = @(Import-Csv -LiteralPath $csvPath)
    if ($rows.Count -eq 0) {
        $runResults[$runId] = @{
            run_id   = $runId
            status   = 'EMPTY_CSV'
            manifest = $manifest
            notes    = 'CSV file is empty.'
        }
        continue
    }

    # Incident counters computed across FULL measurement window (including cycles with no decoded video)
    $reconnect_count = @($rows | Where-Object { $_.reconnect_event -eq '1' -or $_.reconnect_event -eq 'true' }).Count
    $format_changes  = @($rows | Where-Object { $_.format_change -eq '1' -or $_.format_change -eq 'true' }).Count

    $out_ok_delta    = Get-CounterDelta $rows 'output_ok'
    $out_skip_delta  = Get-CounterDelta $rows 'output_skip'
    $out_err_delta   = Get-CounterDelta $rows 'output_err'

    $prev_ok_delta   = Get-CounterDelta $rows 'preview_ok'
    $prev_skip_delta = Get-CounterDelta $rows 'preview_skip'
    $prev_err_delta  = Get-CounterDelta $rows 'preview_err'

    $real_underruns  = Get-CounterDelta $rows 'real_underruns'
    $backlog_drops   = Get-CounterDelta $rows 'backlog_drops'

    # Strict validation of manifest and configuration
    $valRes = Test-BenchmarkRunValidation -RunId $runId -Manifest $manifest -Rows $rows
    if (-not $valRes.IsValid) {
        $runResults[$runId] = @{
            run_id         = $runId
            status         = $valRes.Status
            manifest       = $manifest
            total_rows     = $rows.Count
            notes          = ($valRes.Reasons -join '; ')
            reconnects     = $reconnect_count
            format_changes = $format_changes
            out_ok         = $out_ok_delta
            out_skip       = $out_skip_delta
            out_err        = $out_err_delta
            prev_ok        = $prev_ok_delta
            prev_skip      = $prev_skip_delta
            prev_err       = $prev_err_delta
            real_underruns = $real_underruns
            backlog_drops  = $backlog_drops
        }
        continue
    }

    $validRows = $valRes.ValidRows
    $lastRow = $validRows[-1]

    # Extract series with null/empty safety (never coerce empty cell to 0.0)
    # Latency samples: only extracted when sample count > 0 and data is fresh/active
    $t0_t7_vals = Extract-FilteredDoubleSeries $validRows 'native_t0_t7_total_ms' { param($r) $r.video_stale -ne '1' -and (Parse-DoubleSafe $r.v_dec_fps) -gt 0 }
    $t0_t1_vals = Extract-FilteredDoubleSeries $validRows 't0_t1_ms' { param($r) $r.video_stale -ne '1' -and (Parse-DoubleSafe $r.v_dec_fps) -gt 0 }
    $t1_t2_vals = Extract-FilteredDoubleSeries $validRows 't1_t2_ms' { param($r) $r.video_stale -ne '1' -and (Parse-DoubleSafe $r.v_dec_fps) -gt 0 }
    $t2_t3_vals = Extract-FilteredDoubleSeries $validRows 't2_t3_ms' { param($r) $r.video_stale -ne '1' -and (Parse-DoubleSafe $r.v_dec_fps) -gt 0 }
    $t3_t4_vals = Extract-FilteredDoubleSeries $validRows 't3_t4_ms' { param($r) $r.video_stale -ne '1' -and (Parse-DoubleSafe $r.v_dec_fps) -gt 0 }
    $t4_t5_vals = Extract-FilteredDoubleSeries $validRows 't4_t5_ms' { param($r) $r.video_stale -ne '1' -and (Parse-DoubleSafe $r.v_dec_fps) -gt 0 }
    $t5_t6_vals = Extract-FilteredDoubleSeries $validRows 't5_t6_ms' { param($r) $r.video_stale -ne '1' -and (Parse-DoubleSafe $r.v_dec_fps) -gt 0 }
    $t6_t7_vals = Extract-FilteredDoubleSeries $validRows 't6_t7_ms' { param($r) $r.video_stale -ne '1' -and (Parse-DoubleSafe $r.v_dec_fps) -gt 0 }

    $q_res_avg = Extract-FilteredDoubleSeries $validRows 'queue_res_avg_ms' { param($r) $r.video_stale -ne '1' -and [int64]$r.queue_res_n -gt 0 }
    $q_res_p50 = Extract-FilteredDoubleSeries $validRows 'queue_res_p50_ms' { param($r) $r.video_stale -ne '1' -and [int64]$r.queue_res_n -gt 0 }
    $q_res_p95 = Extract-FilteredDoubleSeries $validRows 'queue_res_p95_ms' { param($r) $r.video_stale -ne '1' -and [int64]$r.queue_res_n -gt 0 }

    $dec_fps   = Extract-FilteredDoubleSeries $validRows 'v_dec_fps' { param($r) $r.video_stale -ne '1' }
    $rend_fps  = Extract-FilteredDoubleSeries $validRows 'v_rend_fps' { param($r) $r.video_stale -ne '1' }
    $uniq_fps  = Extract-FilteredDoubleSeries $validRows 'unique_pres_fps' { param($r) $r.video_stale -ne '1' }

    $out_age_p50 = Extract-FilteredDoubleSeries $validRows 'out_pres_p50_ms' { param($r) [int64]$r.out_age_count -gt 0 }
    $out_age_p95 = Extract-FilteredDoubleSeries $validRows 'out_pres_p95_ms' { param($r) [int64]$r.out_age_count -gt 0 }

    $prev_age_p50 = Extract-FilteredDoubleSeries $validRows 'prev_pres_p50_ms' { param($r) [int64]$r.prev_age_count -gt 0 }

    # Physical Glass-to-Glass events if present
    $glassEventsPath = Join-Path $dir 'glass_events.csv'
    $glassStats = if (Test-Path $glassEventsPath) {
        $gRows = @(Import-Csv -LiteralPath $glassEventsPath)
        $gVals = Extract-DoubleSeries $gRows 'latency_ms'
        $camMeta = if ($gRows.Count -gt 0 -and $gRows[0].PSObject.Properties['camera_fps']) {
            "$($gRows[0].camera_fps) FPS camera"
        } else { 'Camera 120/240 FPS' }
        @{
            count       = $gVals.Count
            median      = if ($gVals.Count -gt 0) { Get-Percentile $gVals 50.0 } else { $null }
            p95         = if ($gVals.Count -gt 0) { Get-Percentile $gVals 95.0 } else { $null }
            provenance  = $camMeta
            status      = if ($gVals.Count -ge 30) { 'MEASURED' } else { "CHƯA ĐO ($($gVals.Count)/30)" }
        }
    } else {
        @{
            count       = 0
            median      = $null
            p95         = $null
            provenance  = 'None'
            status      = 'CHƯA ĐO'
        }
    }

    $qResMaxP95 = if ($q_res_p95.Count -gt 0) { ([double]($q_res_p95 | Measure-Object -Maximum).Maximum) } else { $null }

    $runResults[$runId] = @{
        run_id            = $runId
        status            = 'VALID'
        manifest          = $manifest
        sample_count      = $validRows.Count
        codec             = $lastRow.actual_codec
        res               = $lastRow.actual_res
        fps               = $lastRow.actual_fps
        coded_res         = $lastRow.coded_res
        vis_res           = $lastRow.vis_res
        stream_mode       = $lastRow.stream_mode
        policy_max_q      = $lastRow.policy_max_q
        policy_res_ms     = $lastRow.policy_res_ms

        t0_t7_mean_ms     = Get-Mean $t0_t7_vals
        t0_t7_p50_ms      = Get-Percentile $t0_t7_vals 50.0
        t0_t7_p95_ms      = Get-Percentile $t0_t7_vals 95.0

        t0_t1_mean_ms     = Get-Mean $t0_t1_vals
        t1_t2_mean_ms     = Get-Mean $t1_t2_vals
        t2_t3_mean_ms     = Get-Mean $t2_t3_vals
        t3_t4_mean_ms     = Get-Mean $t3_t4_vals
        t4_t5_mean_ms     = Get-Mean $t4_t5_vals
        t5_t6_mean_ms     = Get-Mean $t5_t6_vals
        t6_t7_mean_ms     = Get-Mean $t6_t7_vals

        q_res_mean_ms     = Get-Mean $q_res_avg
        q_res_win_p50_ms  = Get-Mean $q_res_p50
        q_res_win_p95_ms  = Get-Mean $q_res_p95
        q_res_win_max_p95 = $qResMaxP95

        out_age_mean_p50  = Get-Mean $out_age_p50
        out_age_mean_p95  = Get-Mean $out_age_p95

        prev_age_mean_p50 = Get-Mean $prev_age_p50

        dec_fps_mean      = Get-Mean $dec_fps
        rend_fps_mean     = Get-Mean $rend_fps
        uniq_fps_mean     = Get-Mean $uniq_fps

        out_ok            = $out_ok_delta
        out_skip          = $out_skip_delta
        out_err           = $out_err_delta

        prev_ok           = $prev_ok_delta
        prev_skip         = $prev_skip_delta
        prev_err          = $prev_err_delta

        real_underruns    = $real_underruns
        backlog_drops     = $backlog_drops
        reconnects        = $reconnect_count
        format_changes    = $format_changes

        glass_stats       = $glassStats
    }
}

# Generate Markdown Report
$md = New-Object System.Text.StringBuilder

[void]$md.AppendLine("# Duwn Mirror — A/B Receiver Latency Benchmark Report")
[void]$md.AppendLine("")
[void]$md.AppendLine("Date: $([DateTime]::UtcNow.ToString('yyyy-MM-dd HH:mm:ss')) UTC")
[void]$md.AppendLine("Scope: Built-in iOS AirPlay Screen Mirroring receiver pipeline latency (T0–T7) and physical Glass-to-Glass assessment.")
[void]$md.AppendLine("")
[void]$md.AppendLine("---")
[void]$md.AppendLine("")
[void]$md.AppendLine("## 1. Test Configurations Matrix")
[void]$md.AppendLine("")
[void]$md.AppendLine("| Run ID | Mode | Queue Capacity | Threshold Strategy | Preview Window | Status |")
[void]$md.AppendLine("|---|---|---:|---|---|---|")
[void]$md.AppendLine("| **B_OFF** | Balanced (`SmoothLive`) | Up to 3 frames | Fresh FIFO (1.25× cadence catchup) | OFF | $(($runResults['B_OFF']).status) |")
[void]$md.AppendLine("| **B_ON**  | Balanced (`SmoothLive`) | Up to 3 frames | Fresh FIFO (1.25× cadence catchup) | ON  | $(($runResults['B_ON']).status) |")
[void]$md.AppendLine("| **F_OFF** | Fastest (`LowLatency`) | 1 frame | Always newest decoded frame | OFF | $(($runResults['F_OFF']).status) |")
[void]$md.AppendLine("| **F_ON**  | Fastest (`LowLatency`) | 1 frame | Always newest decoded frame | ON  | $(($runResults['F_ON']).status) |")
[void]$md.AppendLine("| **C_OFF** | Custom (`Custom`) | 2 frames | 25 ms freshness threshold | OFF | $(($runResults['C_OFF']).status) |")
[void]$md.AppendLine("| **C_ON**  | Custom (`Custom`) | 2 frames | 25 ms freshness threshold | ON  | $(($runResults['C_ON']).status) |")
[void]$md.AppendLine("")
[void]$md.AppendLine("---")
[void]$md.AppendLine("")
[void]$md.AppendLine("## 2. Telemetry Metrics Summary (Native Pipeline T0–T7)")
[void]$md.AppendLine("")
[void]$md.AppendLine("> **Lưu ý thống kê:**")
[void]$md.AppendLine("> - **Mean của Window-P95/P50**: Các giá trị p50/p95 trong bảng là trung bình cộng của các p50/p95 đo trong từng cửa sổ 1 giây (`Window-P50 / Window-P95 Distribution`), không phải percentile gộp của toàn bộ frame trong phiên.")
[void]$md.AppendLine("> - **T0–T7 Total Mean**: Trung bình cộng của rolling average T0–T7 qua các chu kỳ đo.")
[void]$md.AppendLine("> - **Video Skip Rate**: Tỷ lệ khung hình bị bỏ qua / thay thế sau decode trên tổng số khung hình gửi tới Present (`out_skip / (out_ok + out_skip)`).")
[void]$md.AppendLine("")
[void]$md.AppendLine("| Run ID | Actual Stream | Decoded FPS | Present FPS | Native T0–T7 Total (Mean) | Queue Dwell (Mean) | Window-P95 Queue | Out Age (P50) | Out Age (P95) | Video Skip Rate | Audio Drops | Audio Underruns | Status |")
[void]$md.AppendLine("|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|")

foreach ($runId in $runOrder) {
    $res = $runResults[$runId]
    if ($res.status -ne 'VALID') {
        [void]$md.AppendLine("| **$runId** | *$($res.status)* | — | — | — | — | — | — | — | — | — | — | *$($res.status)* |")
        continue
    }

    $totP = if ($null -ne $res.out_ok -and $null -ne $res.out_skip) { $res.out_ok + $res.out_skip } else { 0 }
    $skipRateStr = if ($totP -gt 0) {
        "{0:P2} ({1}/{2})" -f ($res.out_skip / $totP), $res.out_skip, $totP
    } else { "0.0% (0/0)" }

    $dropsStr = if ($null -ne $res.backlog_drops) { "$($res.backlog_drops) drops" } else { "N/A" }
    $underrunsStr = if ($null -ne $res.real_underruns) { "$($res.real_underruns) underruns" } else { "N/A" }

    $streamStr = "$($res.vis_res) @ $(Format-Val $res.fps) ($($res.codec))"

    [void]$md.AppendLine("| **$runId** | $streamStr | $(Format-Val $res.dec_fps_mean) | $(Format-Val $res.uniq_fps_mean) | $(Format-Val $res.t0_t7_mean_ms ' ms') | $(Format-Val $res.q_res_mean_ms ' ms') | $(Format-Val $res.q_res_win_p95_ms ' ms') | $(Format-Val $res.out_age_mean_p50 ' ms') | $(Format-Val $res.out_age_mean_p95 ' ms') | $skipRateStr | $dropsStr | $underrunsStr | **VALID** |")
}

[void]$md.AppendLine("")
[void]$md.AppendLine("---")
[void]$md.AppendLine("")
[void]$md.AppendLine("## 3. Pipeline Stages Breakdown (T0–T7 Detailed Stages)")
[void]$md.AppendLine("")
[void]$md.AppendLine("> **Ghi chú kỹ thuật về các mốc thời gian:**")
[void]$md.AppendLine("> - **T0→T1**: RTP nội bộ đến khi lắp ráp Access Unit hoàn chỉnh.")
[void]$md.AppendLine("> - **T1→T2**: Access Unit hoàn chỉnh đến khi nạp vào decoder input (MFT ProcessInput).")
[void]$md.AppendLine("> - **T2→T3**: Decoder input đến khi decoder output xuất mẫu ảnh (MFT ProcessOutput). Đây là thời gian decoder xử lý thực tế, không phải Demux/NAL.")
[void]$md.AppendLine("> - **T3→T4**: Decoder output đến khi đưa vào hàng đợi sau decode.")
[void]$md.AppendLine("> - **T4→T5**: Chờ trong hàng đợi sau decode (Queue Dwell) đến khi bộ điều phối chọn frame.")
[void]$md.AppendLine("> - **T5→T6**: Chọn frame đến khi VideoProcessorBlt trả về. Timestamp CPU quanh Blt đo thời gian gọi API CPU, không chứng minh GPU đã hoàn tất render.")
[void]$md.AppendLine("> - **T6→T7**: Đến khi Present trả về. Timestamp CPU quanh Present đo thời gian gọi API CPU, không chứng minh GPU đã hoàn tất scanout lên màn hình.")
[void]$md.AppendLine("")
[void]$md.AppendLine("| Run ID | T0–T1 (RTP to AU) | T1–T2 (AU to Dec In) | T2–T3 (Dec In to Out) | T3–T4 (Dec Out to Queue) | T4–T5 (Queue Dwell) | T5–T6 (Blt Ret) | T6–T7 (Present Ret) | Native T0–T7 Total (Mean) |")
[void]$md.AppendLine("|---|---:|---:|---:|---:|---:|---:|---:|---:|")

foreach ($runId in $runOrder) {
    $res = $runResults[$runId]
    if ($res.status -ne 'VALID') {
        [void]$md.AppendLine("| **$runId** | — | — | — | — | — | — | — | — |")
        continue
    }
    [void]$md.AppendLine("| **$runId** | $(Format-Val $res.t0_t1_mean_ms ' ms') | $(Format-Val $res.t1_t2_mean_ms ' ms') | $(Format-Val $res.t2_t3_mean_ms ' ms') | $(Format-Val $res.t3_t4_mean_ms ' ms') | $(Format-Val $res.t4_t5_mean_ms ' ms') | $(Format-Val $res.t5_t6_mean_ms ' ms') | $(Format-Val $res.t6_t7_mean_ms ' ms') | **$(Format-Val $res.t0_t7_mean_ms ' ms')** |")
}

[void]$md.AppendLine("")
[void]$md.AppendLine("---")
[void]$md.AppendLine("")
[void]$md.AppendLine("## 4. Physical Glass-to-Glass Latency (External Camera 120/240 FPS)")
[void]$md.AppendLine("")
[void]$md.AppendLine("| Run ID | Mode | Preview | Measured Events | Median Latency | P95 Latency | Provenance / Rig | Measurement Status |")
[void]$md.AppendLine("|---|---|---|---:|---:|---:|---|---|")

foreach ($runId in $runOrder) {
    $res = $runResults[$runId]
    $g = if ($res.ContainsKey('glass_stats')) { $res.glass_stats } else { @{ count = 0; median = $null; p95 = $null; provenance = 'None'; status = 'CHƯA ĐO' } }
    $medStr = if ($null -ne $g.median) { "$([Math]::Round([double]$g.median, 2)) ms" } else { "—" }
    $p95Str = if ($null -ne $g.p95) { "$([Math]::Round([double]$g.p95, 2)) ms" } else { "—" }

    $stMode = if ($res.ContainsKey('stream_mode')) { $res.stream_mode } else { "—" }
    [void]$md.AppendLine("| **$runId** | $stMode | $(if ($runId.EndsWith('_ON')) { 'ON' } else { 'OFF' }) | $($g.count) | $medStr | $p95Str | $($g.provenance) | **$($g.status)** |")
}

[void]$md.AppendLine("")
[void]$md.AppendLine("---")
[void]$md.AppendLine("")
[void]$md.AppendLine("## 5. Phân tích kết quả và Khuyến nghị sử dụng")
[void]$md.AppendLine("")

$validRuns = @($runOrder | Where-Object { $runResults[$_].status -eq 'VALID' })

if ($validRuns.Count -eq 0) {
    [void]$md.AppendLine("### 5.1. Đánh giá trạng thái thực nghiệm")
    [void]$md.AppendLine("- **Chưa có phiên phát thực tế hợp lệ**: Toàn bộ các cấu hình đo đang ở trạng thái chưa hoàn tất benchmark (`NOT_RUN`, `NO_VALID_STREAM` hoặc `INVALID`).")
    [void]$md.AppendLine("- **Không xếp hạng hay chọn chế độ tối ưu**: Báo cáo từ chối đưa ra kết luận so sánh hiệu năng hoặc chọn chế độ thắng cuộc khi chưa có dữ liệu telemetry hợp lệ từ phiên phát AirPlay thực tế.")
    [void]$md.AppendLine("- **Chỉ số Glass-to-Glass**: Ghi nhận trạng thái **CHƯA ĐO** do chưa có hệ thống camera ngoài 120/240 FPS ghi hình quang học đối chiếu.")
} else {
    [void]$md.AppendLine("### 5.1. Đánh giá dựa trên dữ liệu đo thực tế ($($validRuns.Count)/6 cấu hình hợp lệ)")
    foreach ($vId in $validRuns) {
        $vRes = $runResults[$vId]
        $totP = if ($null -ne $vRes.out_ok -and $null -ne $vRes.out_skip) { $vRes.out_ok + $vRes.out_skip } else { 0 }
        $skipSummary = if ($totP -gt 0) { "$($vRes.out_skip)/$totP frames" } else { "N/A" }
        [void]$md.AppendLine("- **$vId ($($vRes.stream_mode))**: Native T0–T7 Total Mean = $(Format-Val $vRes.t0_t7_mean_ms ' ms'), Queue Dwell Mean = $(Format-Val $vRes.q_res_mean_ms ' ms'), Video Skip Rate = $skipSummary.")
    }
    [void]$md.AppendLine("")
    [void]$md.AppendLine("### 5.2. Nhận định về mối quan hệ giữa Native T0–T7 và Glass-to-Glass")
    [void]$md.AppendLine("- Khoảng thời gian T0–T7 chỉ phản ánh độ trễ nội bộ trong phần mềm nhận (từ socket nhận gói tin đến khi lệnh Present CPU trả về).")
    [void]$md.AppendLine("- Nếu độ trễ Glass-to-Glass đo được qua camera ngoài cao trong khi Native T0–T7 thấp, phần trễ chênh lệch nằm ở các khâu bên ngoài phạm vi T0–T7 (bao gồm cả các khâu trước T0: chụp màn hình và mã hóa trên iOS sender, độ trễ mạng truyền dẫn Wi-Fi, sidecar forwarder; và các khâu sau T7: GPU rendering, DWM desktop compositing, scanout và độ trễ phản hồi vật lý của tấm nền màn hình).")
    [void]$md.AppendLine("- Tuyệt đối không tự quy kết phần trễ chênh lệch chủ yếu cho sender iOS, Wi-Fi hay UxPlay khi chưa có số đo quang học cô lập từng chặng.")
}

$outDir = Split-Path -Parent $targetMdPath
if ($outDir -and -not (Test-Path -LiteralPath $outDir)) {
    New-Item -ItemType Directory -Path $outDir -Force | Out-Null
}

[System.IO.File]::WriteAllText($targetMdPath, $md.ToString(), [System.Text.Encoding]::UTF8)

# Write JSON summary
$jsonPath = [System.IO.Path]::ChangeExtension($targetMdPath, '.json')
$runResults | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $jsonPath -Encoding utf8

Write-Host "Analysis report generated:" -ForegroundColor Green
Write-Host "  Markdown: $targetMdPath" -ForegroundColor White
Write-Host "  JSON:     $jsonPath" -ForegroundColor White