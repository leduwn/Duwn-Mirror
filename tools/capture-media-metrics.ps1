param(
    [Parameter(Mandatory = $true)]
    [string]$OutputPath,
    [int]$DurationSeconds = 1800,
    [string]$LogPath = (Join-Path $env:LOCALAPPDATA 'DUWN Mirror\Logs\duwn-mirror.log'),
    [int64]$StartByteOffset = 0,
    [string]$RawLogOutputPath = $null,
    [switch]$StrictCycles = $false
)

$ErrorActionPreference = 'Stop'
$runId = [System.Guid]::NewGuid().ToString('D')
$deadline = (Get-Date).AddSeconds($DurationSeconds)

function Escape-CsvField([object]$value) {
    if ($null -eq $value) { return '' }
    $s = [string]$value
    if ($s -match '[",\r\n]') {
        return '"' + ($s -replace '"', '""') + '"'
    }
    return $s
}

$headerCols = @(
    'run_id', 'session_id', 'cycle_id', 'collector_time', 'source_time', 'reconnect_event', 'format_change',
    'commit', 'transport', 'stream_mode', 'policy_max_q', 'policy_res_ms', 'policy_cad_pct', 'policy_always_latest',
    'req_quality', 'active_quality', 'quality_pending', 'preview_visible', 'actual_codec', 'actual_res', 'actual_fps',
    'native_t0_t7_total_ms', 't0_t1_ms', 't1_t2_ms', 't2_t3_ms', 't3_t4_ms', 't4_t5_ms', 't5_t6_ms', 't6_t7_ms',
    'decode_avg_ms', 'decode_p50_ms', 'decode_p95_ms', 'decode_n',
    'queue_res_avg_ms', 'queue_res_p50_ms', 'queue_res_p95_ms', 'queue_res_n',
    'dxgi_wait_avg_ms', 'dxgi_wait_p50_ms', 'dxgi_wait_p95_ms', 'dxgi_wait_n',
    'vp_avg_ms', 'vp_p50_ms', 'vp_p95_ms', 'vp_n',
    'present_avg_ms', 'present_p50_ms', 'present_p95_ms', 'present_n',
    'output_att', 'output_ok', 'output_skip', 'output_err',
    'preview_att', 'preview_ok', 'preview_skip', 'preview_err',
    'out_age_count', 'out_sel_p50_ms', 'out_sel_p95_ms', 'out_pres_p50_ms', 'out_pres_p95_ms',
    'prev_age_count', 'prev_skips', 'prev_pres_p50_ms',
    'video_stale', 'v_rtp_rate', 'v_kb_rate', 'v_au_rate', 'v_dec_fps', 'v_rend_fps', 'unique_pres_fps', 'v_drop_rate', 'superseded_rate', 'late_rate', 'queue_depth', 'gen', 'coded_res', 'vis_res',
    'audio_stale', 'a_rtp_rate', 'a_kb_rate', 'audio_buf_ms', 'ring_ms', 'target_ms', 'padding_ms', 'servo_ppm', 'real_underruns', 'overrun_frames', 'backlog_drops', 'recoveries',
    'av_offset_ms', 'drift_ppm', 'sidecar', 'session_state', 'source_conn', 'lifecycle'
)

$outDir = Split-Path -Parent $OutputPath
if ($outDir -and -not (Test-Path -LiteralPath $outDir)) {
    New-Item -ItemType Directory -Path $outDir -Force | Out-Null
}

($headerCols -join ',') | Set-Content -LiteralPath $OutputPath -Encoding utf8

$lastFilePos = $StartByteOffset
$script:waitingForFirstBegin = ($StartByteOffset -gt 0)
$lastCreationTime = $null
$fileHeaderPrefix = ''
$pendingText = ''
$pendingBytes = New-Object byte[] (0)
$sessionId = 1
$lastGen = $null
$lastSourceConn = $null
$script:inCycle = $false
$script:currentBeginId = $null

if ($RawLogOutputPath) {
    $rawLogDir = Split-Path -Parent $RawLogOutputPath
    if ($rawLogDir -and -not (Test-Path -LiteralPath $rawLogDir)) {
        New-Item -ItemType Directory -Path $rawLogDir -Force | Out-Null
    }
    # Initialize empty raw log file
    "" | Set-Content -LiteralPath $RawLogOutputPath -Encoding utf8
}

function Get-IncompleteUtf8ByteCount([byte[]]$bytes) {
    if ($null -eq $bytes -or $bytes.Length -eq 0) { return 0 }
    $len = $bytes.Length
    $trailingContinuation = 0
    $i = $len - 1
    while ($i -ge 0 -and ($bytes[$i] -band 0xC0) -eq 0x80) {
        $trailingContinuation++
        $i--
        if ($trailingContinuation -gt 3) { break }
    }
    if ($i -lt 0) {
        return $len
    }
    $lead = $bytes[$i]
    $needed = 0
    if (($lead -band 0x80) -eq 0) {
        $needed = 0
    } elseif (($lead -band 0xE0) -eq 0xC0) {
        $needed = 1
    } elseif (($lead -band 0xF0) -eq 0xE0) {
        $needed = 2
    } elseif (($lead -band 0xF8) -eq 0xF0) {
        $needed = 3
    } else {
        return 0
    }
    if ($trailingContinuation -ge $needed) {
        return 0
    }
    return ($trailingContinuation + 1)
}

function Decode-Bytes([byte[]]$newBytes, [bool]$flush = $false) {
    if ($null -eq $newBytes -or $newBytes.Length -eq 0) {
        if ($flush -and $script:pendingBytes.Length -gt 0) {
            $res = [System.Text.Encoding]::UTF8.GetString($script:pendingBytes)
            $script:pendingBytes = New-Object byte[] (0)
            return $res
        }
        return ''
    }
    $allBytes = if ($script:pendingBytes.Length -gt 0) {
        $combined = New-Object byte[] ($script:pendingBytes.Length + $newBytes.Length)
        [System.Array]::Copy($script:pendingBytes, 0, $combined, 0, $script:pendingBytes.Length)
        [System.Array]::Copy($newBytes, 0, $combined, $script:pendingBytes.Length, $newBytes.Length)
        $combined
    } else {
        $newBytes
    }

    if ($flush) {
        $script:pendingBytes = New-Object byte[] (0)
        return [System.Text.Encoding]::UTF8.GetString($allBytes)
    }

    $inc = Get-IncompleteUtf8ByteCount $allBytes
    $compLen = $allBytes.Length - $inc
    $decoded = if ($compLen -gt 0) {
        [System.Text.Encoding]::UTF8.GetString($allBytes, 0, $compLen)
    } else {
        ''
    }

    if ($inc -gt 0) {
        $pb = New-Object byte[] ($inc)
        [System.Array]::Copy($allBytes, $compLen, $pb, 0, $inc)
        $script:pendingBytes = $pb
    } else {
        $script:pendingBytes = New-Object byte[] (0)
    }

    return $decoded
}

function New-CycleState {
    return [ordered]@{
        has_update = $false
        has_video  = $false
        has_audio  = $false
        has_meta   = $false
        source_time= ''
        cycle_id   = ''

        commit = ''; transport = ''; stream_mode = ''
        policy_max_q = ''; policy_res_ms = ''; policy_cad_pct = ''; policy_always_latest = ''
        req_quality = ''; active_quality = ''; quality_pending = ''; preview_visible = ''
        actual_codec = ''; actual_res = ''; actual_fps = ''

        t0_t7_total = ''; t0_t1 = ''; t1_t2 = ''; t2_t3 = ''; t3_t4 = ''; t4_t5 = ''; t5_t6 = ''; t6_t7 = ''

        decode_avg = ''; decode_p50 = ''; decode_p95 = ''; decode_n = ''
        q_res_avg = ''; q_res_p50 = ''; q_res_p95 = ''; q_res_n = ''
        dxgi_wait_avg = ''; dxgi_wait_p50 = ''; dxgi_wait_p95 = ''; dxgi_wait_n = ''
        vp_avg = ''; vp_p50 = ''; vp_p95 = ''; vp_n = ''
        pres_avg = ''; pres_p50 = ''; pres_p95 = ''; pres_n = ''

        output_att = ''; output_ok = ''; output_skip = ''; output_err = ''
        prev_att = ''; prev_ok = ''; prev_skip = ''; prev_err = ''

        out_age_count = ''; out_sel_p50 = ''; out_sel_p95 = ''; out_pres_p50 = ''; out_pres_p95 = ''
        prev_age_count = ''; prev_skips = ''; prev_pres_p50 = ''

        v_rtp = ''; v_kb = ''; v_au = ''; v_dec = ''; v_rend = ''; unique_pres = ''
        v_drop = ''; superseded = ''; v_late = ''; q_depth = ''; gen = ''; coded_res = ''; vis_res = ''

        a_rtp = ''; a_kb = ''; a_buf = ''; ring = ''; target = ''; padding = ''; servo = ''
        a_real_underruns = ''; overrun = ''; backlog = ''; recoveries = ''

        av_offset = ''; drift = ''; sidecar = ''; session_state = ''; source_conn = ''; lifecycle = ''
    }
}

$currentCycle = New-CycleState

function Emit-Cycle([System.Collections.IDictionary]$cycle) {
    if (-not $cycle.has_update) { return }

    $reconnectEvent = 0
    $formatChange = 0

    if ($null -ne $script:lastGen -and $cycle.gen -ne '' -and $cycle.gen -ne $script:lastGen) {
        $formatChange = 1
    }
    if ($cycle.gen -ne '') { $script:lastGen = $cycle.gen }

    if ($null -ne $script:lastSourceConn -and $cycle.source_conn -eq 'connected' -and $script:lastSourceConn -eq 'disconnected') {
        $reconnectEvent = 1
        $script:sessionId++
    }
    if ($cycle.source_conn -ne '') { $script:lastSourceConn = $cycle.source_conn }

    $videoStale = if ($cycle.has_video) { 0 } else { 1 }
    $audioStale = if ($cycle.has_audio) { 0 } else { 1 }

    $prevVis = $cycle.preview_visible
    if ($prevVis -eq '' -or $null -eq $prevVis) {
        if (([int64]$cycle.prev_att -gt 0) -or ([int64]$cycle.prev_ok -gt 0)) {
            $prevVis = '1'
        } else {
            $prevVis = '0'
        }
    }

    $collectorTime = (Get-Date).ToString('o')
    $sourceTime = $cycle.source_time

    $rowValues = @(
        $runId, $script:sessionId, $cycle.cycle_id, $collectorTime, $sourceTime, $reconnectEvent, $formatChange,
        $cycle.commit, $cycle.transport, $cycle.stream_mode, $cycle.policy_max_q, $cycle.policy_res_ms, $cycle.policy_cad_pct, $cycle.policy_always_latest,
        $cycle.req_quality, $cycle.active_quality, $cycle.quality_pending, $prevVis, $cycle.actual_codec, $cycle.actual_res, $cycle.actual_fps,
        $cycle.t0_t7_total, $cycle.t0_t1, $cycle.t1_t2, $cycle.t2_t3, $cycle.t3_t4, $cycle.t4_t5, $cycle.t5_t6, $cycle.t6_t7,
        $cycle.decode_avg, $cycle.decode_p50, $cycle.decode_p95, $cycle.decode_n,
        $cycle.q_res_avg, $cycle.q_res_p50, $cycle.q_res_p95, $cycle.q_res_n,
        $cycle.dxgi_wait_avg, $cycle.dxgi_wait_p50, $cycle.dxgi_wait_p95, $cycle.dxgi_wait_n,
        $cycle.vp_avg, $cycle.vp_p50, $cycle.vp_p95, $cycle.vp_n,
        $cycle.pres_avg, $cycle.pres_p50, $cycle.pres_p95, $cycle.pres_n,
        $cycle.output_att, $cycle.output_ok, $cycle.output_skip, $cycle.output_err,
        $cycle.prev_att, $cycle.prev_ok, $cycle.prev_skip, $cycle.prev_err,
        $cycle.out_age_count, $cycle.out_sel_p50, $cycle.out_sel_p95, $cycle.out_pres_p50, $cycle.out_pres_p95,
        $cycle.prev_age_count, $cycle.prev_skips, $cycle.prev_pres_p50,
        $videoStale, $cycle.v_rtp, $cycle.v_kb, $cycle.v_au, $cycle.v_dec, $cycle.v_rend, $cycle.unique_pres, $cycle.v_drop, $cycle.superseded, $cycle.v_late, $cycle.q_depth, $cycle.gen, $cycle.coded_res, $cycle.vis_res,
        $audioStale, $cycle.a_rtp, $cycle.a_kb, $cycle.a_buf, $cycle.ring, $cycle.target, $cycle.padding, $cycle.servo, $cycle.a_real_underruns, $cycle.overrun, $cycle.backlog, $cycle.recoveries,
        $cycle.av_offset, $cycle.drift, $cycle.sidecar, $cycle.session_state, $cycle.source_conn, $cycle.lifecycle
    )

    $escaped = $rowValues | ForEach-Object { Escape-CsvField $_ }
    ($escaped -join ',') | Add-Content -LiteralPath $OutputPath -Encoding utf8
}

function Process-LogLine([string]$line) {
    if (-not $line) { return }
    $line = $line.TrimStart([char]0xFEFF)

    if ($script:waitingForFirstBegin) {
        if ($line -match '\[METRICS CYCLE BEGIN\]') {
            $script:waitingForFirstBegin = $false
        } else {
            return
        }
    }

    if ($RawLogOutputPath -and $line.Trim().Length -gt 0) {
        $line | Add-Content -LiteralPath $RawLogOutputPath -Encoding utf8
    }

    if ($line -match '\[METRICS CYCLE END\](?: cycle=(?<cid>\d+))?') {
        $endId = if ($Matches.ContainsKey('cid') -and $Matches.cid) { $Matches.cid } else { $null }
        if ($StrictCycles) {
            # In strict mode: only emit if currently in a cycle and begin/end cycle_id match
            $isMatching = $script:inCycle -and (
                ($null -eq $script:currentBeginId -and $null -eq $endId) -or
                ($null -ne $script:currentBeginId -and $script:currentBeginId -eq $endId)
            )
            if ($isMatching) {
                if ($endId -and -not $script:currentCycle.cycle_id) {
                    $script:currentCycle.cycle_id = $endId
                }
                Emit-Cycle $script:currentCycle
            }
            # Always reset state after END
            $script:inCycle = $false
            $script:currentBeginId = $null
            $script:currentCycle = New-CycleState
            return
        } else {
            if ($Matches.ContainsKey('cid') -and $Matches.cid -and -not $script:currentCycle.cycle_id) {
                $script:currentCycle.cycle_id = $Matches.cid
            }
            Emit-Cycle $script:currentCycle
            $script:currentCycle = New-CycleState
            return
        }
    }

    if ($line -match '\[METRICS CYCLE BEGIN\](?: cycle=(?<cid>\d+))?') {
        $beginId = if ($Matches.ContainsKey('cid') -and $Matches.cid) { $Matches.cid } else { $null }
        if ($StrictCycles) {
            # Incomplete prior cycle without END is dropped in strict benchmark mode
            $script:currentCycle = New-CycleState
            $script:inCycle = $true
            $script:currentBeginId = $beginId
            if ($beginId) {
                $script:currentCycle.cycle_id = $beginId
            }
        } else {
            if ($script:currentCycle.has_update) {
                Emit-Cycle $script:currentCycle
                $script:currentCycle = New-CycleState
            }
            if ($beginId) {
                $script:currentCycle.cycle_id = $beginId
            }
        }
    }
    elseif ($line -match '\[METADATA\]') {
        if (-not $StrictCycles -and $script:currentCycle.has_meta) {
            Emit-Cycle $script:currentCycle
            $script:currentCycle = New-CycleState
        }
    }
    elseif ($line -match '\[STATS\] VIDEO:') {
        if (-not $StrictCycles -and $script:currentCycle.has_video) {
            Emit-Cycle $script:currentCycle
            $script:currentCycle = New-CycleState
        }
    }

    if ($line -match '^\[(?<time>\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\]') {
        if ($script:currentCycle.source_time -eq '') {
            $script:currentCycle.source_time = $Matches.time
        }
    }

    if ($line -match '\[METADATA\](?: cycle=(?<cid>\d+) \|)? commit=(?<commit>[^ ]*) \| transport=(?<transport>[^ ]*) \| stream_mode=(?<mode>[^ ]*) \| policy\(max_q=(?<mq>\d+), res_ms=(?<rms>\d+), cad_pct=(?<cp>\d+), always_latest=(?<al>\d+)\) \| req_quality=(?<rq>[^ |]*)(?: \| active_quality=(?<aq>[^ |]*) \| quality_pending=(?<qp>[^ |]*))?(?: \| preview_visible=(?<pv>[^ |]*))? \| actual_stream\(codec=(?<codec>[^,]*), res=(?<res>[^,]*), fps=(?<fps>[^)]*)\)') {
        if ($Matches.ContainsKey('cid') -and $Matches.cid) {
            $script:currentCycle.cycle_id = $Matches.cid
        }
        $script:currentCycle.commit = $Matches.commit
        $script:currentCycle.transport = $Matches.transport
        $script:currentCycle.stream_mode = $Matches.mode
        $script:currentCycle.policy_max_q = $Matches.mq
        $script:currentCycle.policy_res_ms = $Matches.rms
        $script:currentCycle.policy_cad_pct = $Matches.cp
        $script:currentCycle.policy_always_latest = $Matches.al
        $script:currentCycle.req_quality = $Matches.rq
        if ($Matches.ContainsKey('aq') -and $Matches.aq) {
            $script:currentCycle.active_quality = $Matches.aq
        } else {
            $script:currentCycle.active_quality = $Matches.rq
        }
        if ($Matches.ContainsKey('qp') -and $Matches.qp) {
            $script:currentCycle.quality_pending = $Matches.qp
        } else {
            $script:currentCycle.quality_pending = '0'
        }
        if ($Matches.ContainsKey('pv') -and $Matches.pv) {
            $script:currentCycle.preview_visible = $Matches.pv
        }
        $script:currentCycle.actual_codec = $Matches.codec
        $script:currentCycle.actual_res = $Matches.res
        $script:currentCycle.actual_fps = $Matches.fps
        $script:currentCycle.has_meta = $true
        $script:currentCycle.has_update = $true
    }
    elseif ($line -match '\[STATS\] VIDEO: rtp=(?<rtp>\d+)/s \((?<kb>[\d.]+) KB/s\) \| au=(?<au>\d+)/s \| dec=(?<dec>\d+) fps \| rend=(?<rend>\d+) fps \(unique=(?<up>\d+).*?\) \| .*? \| drop=(?<drop>\d+)/s \(superseded=(?<sup_rate>\d+)/s, late=(?<late_rate>\d+)/s.*?\) \| q=(?<q>\d+) \| gen=(?<gen>\d+) \| coded=(?<coded>\S+) vis=(?<vis>\S+)') {
        $script:currentCycle.v_rtp = $Matches.rtp
        $script:currentCycle.v_kb = $Matches.kb
        $script:currentCycle.v_au = $Matches.au
        $script:currentCycle.v_dec = $Matches.dec
        $script:currentCycle.v_rend = $Matches.rend
        $script:currentCycle.unique_pres = $Matches.up
        $script:currentCycle.v_drop = $Matches.drop
        $script:currentCycle.superseded = $Matches.sup_rate
        $script:currentCycle.v_late = $Matches.late_rate
        $script:currentCycle.q_depth = $Matches.q
        $script:currentCycle.gen = $Matches.gen
        $script:currentCycle.coded_res = $Matches.coded
        $script:currentCycle.vis_res = $Matches.vis
        $script:currentCycle.has_video = $true
        $script:currentCycle.has_update = $true
    }
    elseif ($line -match '\[STATS\] PRESENT: interval_avg=.*? \| call_avg=.*? \| dxgi_wait_avg=(?<dwait>[\d.]+)ms(?: \| output\(att=(?<oatt>\d+), ok=(?<ook>\d+), skip=(?<oskip>\d+), err=(?<oerr>\d+)\) \| preview\(att=(?<patt>\d+), ok=(?<pok>\d+), skip=(?<pskip>\d+), err=(?<perr>\d+)\))?') {
        $script:currentCycle.dxgi_wait_avg = $Matches.dwait
        if ($Matches.ContainsKey('oatt')) {
            $script:currentCycle.output_att = $Matches.oatt
            $script:currentCycle.output_ok = $Matches.ook
            $script:currentCycle.output_skip = $Matches.oskip
            $script:currentCycle.output_err = $Matches.oerr
            $script:currentCycle.prev_att = $Matches.patt
            $script:currentCycle.prev_ok = $Matches.pok
            $script:currentCycle.prev_skip = $Matches.pskip
            $script:currentCycle.prev_err = $Matches.perr
        }
        $script:currentCycle.has_video = $true
        $script:currentCycle.has_update = $true
    }
    elseif ($line -match '\[STAGE LATENCY\] decode: avg=(?<d_avg>[\d.]+)ms p50=(?<d_p50>[\d.]+)ms p95=(?<d_p95>[\d.]+)ms \(n=(?<d_n>\d+)\) \| queue_res: avg=(?<qr_avg>[\d.]+)ms p50=(?<qr_p50>[\d.]+)ms p95=(?<qr_p95>[\d.]+)ms \(n=(?<qr_n>\d+)\) \| dxgi_wait: avg=(?<dw_avg>[\d.]+)ms p50=(?<dw_p50>[\d.]+)ms p95=(?<dw_p95>[\d.]+)ms max=(?<dw_max>[\d.]+)ms \(n=(?<dw_n>\d+)\) \| vp: avg=(?<vp_avg>[\d.]+)ms p50=(?<vp_p50>[\d.]+)ms p95=(?<vp_p95>[\d.]+)ms \(n=(?<vp_n>\d+)\) \| present: avg=(?<pr_avg>[\d.]+)ms p50=(?<pr_p50>[\d.]+)ms p95=(?<pr_p95>[\d.]+)ms \(n=(?<pr_n>\d+)\)') {
        $script:currentCycle.decode_avg = $Matches.d_avg
        $script:currentCycle.decode_p50 = $Matches.d_p50
        $script:currentCycle.decode_p95 = $Matches.d_p95
        $script:currentCycle.decode_n = $Matches.d_n
        $script:currentCycle.q_res_avg = $Matches.qr_avg
        $script:currentCycle.q_res_p50 = $Matches.qr_p50
        $script:currentCycle.q_res_p95 = $Matches.qr_p95
        $script:currentCycle.q_res_n = $Matches.qr_n
        $script:currentCycle.dxgi_wait_avg = $Matches.dw_avg
        $script:currentCycle.dxgi_wait_p50 = $Matches.dw_p50
        $script:currentCycle.dxgi_wait_p95 = $Matches.dw_p95
        $script:currentCycle.dxgi_wait_n = $Matches.dw_n
        $script:currentCycle.vp_avg = $Matches.vp_avg
        $script:currentCycle.vp_p50 = $Matches.vp_p50
        $script:currentCycle.vp_p95 = $Matches.vp_p95
        $script:currentCycle.vp_n = $Matches.vp_n
        $script:currentCycle.pres_avg = $Matches.pr_avg
        $script:currentCycle.pres_p50 = $Matches.pr_p50
        $script:currentCycle.pres_p95 = $Matches.pr_p95
        $script:currentCycle.pres_n = $Matches.pr_n
        $script:currentCycle.has_video = $true
        $script:currentCycle.has_update = $true
    }
    elseif ($line -match '\[LATENCY\] T0-T7(?: \(native receiver latency\))?: total=(?<total>[\d.]+)ms \(T0-T1=(?<t01>[\d.]+)ms, T1-T2=(?<t12>[\d.]+)ms, T2-T3=(?<t23>[\d.]+)ms, T3-T4=(?<t34>[\d.]+)ms, T4-T5\[q_age\]=(?<t45>[\d.]+)ms, T5-T6\[vp\]=(?<t56>[\d.]+)ms, T6-T7\[pres\]=(?<t67>[\d.]+)ms\)') {
        $script:currentCycle.t0_t7_total = $Matches.total
        $script:currentCycle.t0_t1 = $Matches.t01
        $script:currentCycle.t1_t2 = $Matches.t12
        $script:currentCycle.t2_t3 = $Matches.t23
        $script:currentCycle.t3_t4 = $Matches.t34
        $script:currentCycle.t4_t5 = $Matches.t45
        $script:currentCycle.t5_t6 = $Matches.t56
        $script:currentCycle.t6_t7 = $Matches.t67
        $script:currentCycle.has_video = $true
        $script:currentCycle.has_update = $true
    }
    elseif ($line -match '\[FRAME AGE OUTPUT\] count=(?<cnt>\d+) \| select: p50=(?<sp50>[\d.]+)ms p95=(?<sp95>[\d.]+)ms.*? \| present: p50=(?<pp50>[\d.]+)ms p95=(?<pp95>[\d.]+)ms') {
        $script:currentCycle.out_age_count = $Matches.cnt
        $script:currentCycle.out_sel_p50 = $Matches.sp50
        $script:currentCycle.out_sel_p95 = $Matches.sp95
        $script:currentCycle.out_pres_p50 = $Matches.pp50
        $script:currentCycle.out_pres_p95 = $Matches.pp95
        $script:currentCycle.has_video = $true
        $script:currentCycle.has_update = $true
    }
    elseif ($line -match '\[FRAME AGE PREVIEW\] count=(?<cnt>\d+) skips=(?<skips>\d+) \|.*? \| present: p50=(?<pp50>[\d.]+)ms') {
        $script:currentCycle.prev_age_count = $Matches.cnt
        $script:currentCycle.prev_skips = $Matches.skips
        $script:currentCycle.prev_pres_p50 = $Matches.pp50
        $script:currentCycle.has_video = $true
        $script:currentCycle.has_update = $true
    }
    elseif ($line -match '\[STATS\] AUDIO: rtp=(?<artp>\d+)/s \((?<akb>[\d.]+) KB/s\).*? \| buf=(?<abuf>[\d.]+)ms.*? \| real_underruns=\d+/s \(total=(?<underruns>\d+)') {
        $script:currentCycle.a_rtp = $Matches.artp
        $script:currentCycle.a_kb = $Matches.akb
        $script:currentCycle.a_buf = $Matches.abuf
        $script:currentCycle.a_real_underruns = $Matches.underruns
        $script:currentCycle.has_audio = $true
        $script:currentCycle.has_update = $true
    }
    elseif ($line -match '\[AUDIO LATENCY\] .*?ring=(?<ring>[\d.]+)ms target=(?<target>[\d.]+)ms.*?padding=(?<padding>[\d.]+)ms.*?servo=(?<servo>-?[\d.]+)ppm.*?overrun_frames=(?<overrun>\d+) backlog_drops=(?<backlog>\d+) recoveries=(?<recoveries>\d+)') {
        $script:currentCycle.ring = $Matches.ring
        $script:currentCycle.target = $Matches.target
        $script:currentCycle.padding = $Matches.padding
        $script:currentCycle.servo = $Matches.servo
        $script:currentCycle.overrun = $Matches.overrun
        $script:currentCycle.backlog = $Matches.backlog
        $script:currentCycle.recoveries = $Matches.recoveries
        $script:currentCycle.has_audio = $true
        $script:currentCycle.has_update = $true
    }
    elseif ($line -match '\[STATS\] SYNC/SESSION: A/V=(?<av>-?[\d.]+)ms drift=(?<drift>-?[\d.]+)ms/min \| sidecar=(?<sidecar>[^ |]+) \| state=(?<state>[^ |]+) \| source=(?<source>[^ |]+) \|.*? \| lifecycle=(?<life>.*)') {
        $script:currentCycle.av_offset = $Matches.av
        $script:currentCycle.drift = $Matches.drift
        $script:currentCycle.sidecar = $Matches.sidecar
        $script:currentCycle.session_state = $Matches.state
        $script:currentCycle.source_conn = $Matches.source
        $script:currentCycle.lifecycle = $Matches.life.Trim()
        $script:currentCycle.has_update = $true
    }
}
while ((Get-Date) -lt $deadline) {
    if (Test-Path -LiteralPath $LogPath) {
        try {
            $fileInfo = New-Object System.IO.FileInfo($LogPath)
            $creationTime = $fileInfo.CreationTimeUtc

            # Detect log rotation (file truncated OR replacement file created)
            $isRotated = $false
            if ($fileInfo.Length -lt $lastFilePos) {
                $isRotated = $true
            } elseif ($lastFilePos -gt 0 -and $fileHeaderPrefix -ne '') {
                try {
                    $checkStream = [System.IO.FileStream]::new($LogPath, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
                    try {
                        $checkBuf = New-Object byte[] (64)
                        $readBytes = $checkStream.Read($checkBuf, 0, 64)
                        $currPrefix = [System.Text.Encoding]::UTF8.GetString($checkBuf, 0, $readBytes)
                        if ($currPrefix -ne $fileHeaderPrefix) {
                            $isRotated = $true
                        }
                    } finally {
                        $checkStream.Dispose()
                    }
                } catch {}
            }

            if ($isRotated) {
                # Drain remaining data from previous log file (.1.log) if present
                $baseName = [System.IO.Path]::GetFileNameWithoutExtension($LogPath)
                $backupPath = Join-Path $fileInfo.DirectoryName "$baseName.1.log"
                if (-not (Test-Path -LiteralPath $backupPath)) {
                    $backupPath = Join-Path $fileInfo.DirectoryName "duwn-mirror.1.log"
                }
                if (Test-Path -LiteralPath $backupPath) {
                    try {
                        $bfs = [System.IO.FileStream]::new($backupPath, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
                        try {
                            if ($bfs.Length -gt $lastFilePos) {
                                $null = $bfs.Seek($lastFilePos, [System.IO.SeekOrigin]::Begin)
                                $readLen = [int]($bfs.Length - $lastFilePos)
                                $rawBytes = New-Object byte[] ($readLen)
                                $actualRead = 0
                                while ($actualRead -lt $readLen) {
                                    $n = $bfs.Read($rawBytes, $actualRead, $readLen - $actualRead)
                                    if ($n -le 0) { break }
                                    $actualRead += $n
                                }
                                if ($actualRead -gt 0) {
                                    if ($actualRead -lt $readLen) {
                                        $trimmedBytes = New-Object byte[] ($actualRead)
                                        [System.Array]::Copy($rawBytes, 0, $trimmedBytes, 0, $actualRead)
                                        $rawBytes = $trimmedBytes
                                    }
                                    $pendingText += Decode-Bytes $rawBytes
                                }
                            }
                        } finally {
                            $bfs.Dispose()
                        }
                    } catch {}
                }
                $lastFilePos = 0
                $fileHeaderPrefix = ''
                $lastCreationTime = $creationTime
            }

            if ($null -eq $lastCreationTime) {
                $lastCreationTime = $creationTime
            }

            if ($fileInfo.Length -gt $lastFilePos) {
                $fs = [System.IO.FileStream]::new(
                    $LogPath,
                    [System.IO.FileMode]::Open,
                    [System.IO.FileAccess]::Read,
                    [System.IO.FileShare]::ReadWrite
                )
                try {
                    if ($lastFilePos -eq 0) {
                        $pBuf = New-Object byte[] (64)
                        $rBytes = $fs.Read($pBuf, 0, 64)
                        $fileHeaderPrefix = [System.Text.Encoding]::UTF8.GetString($pBuf, 0, $rBytes)
                    }
                    $null = $fs.Seek($lastFilePos, [System.IO.SeekOrigin]::Begin)
                    $readLen = [int]($fileInfo.Length - $lastFilePos)
                    if ($readLen -gt 0) {
                        $rawBytes = New-Object byte[] ($readLen)
                        $actualRead = 0
                        while ($actualRead -lt $readLen) {
                            $n = $fs.Read($rawBytes, $actualRead, $readLen - $actualRead)
                            if ($n -le 0) { break }
                            $actualRead += $n
                        }
                        $lastFilePos = $fs.Position
                        if ($actualRead -gt 0) {
                            if ($actualRead -lt $readLen) {
                                $trimmedBytes = New-Object byte[] ($actualRead)
                                [System.Array]::Copy($rawBytes, 0, $trimmedBytes, 0, $actualRead)
                                $rawBytes = $trimmedBytes
                            }
                            $pendingText += Decode-Bytes $rawBytes
                        }
                    }
                } finally {
                    $fs.Dispose()
                }
            }
        } catch {
            # Non-fatal read error (e.g. transient file sharing during rotation)
        }

        # Process complete lines from pendingText buffer, preserve incomplete lines
        if ($pendingText.Length -gt 0) {
            $lastNl = $pendingText.LastIndexOf("`n")
            if ($lastNl -ge 0) {
                $completeChunk = $pendingText.Substring(0, $lastNl)
                $pendingText = $pendingText.Substring($lastNl + 1)
                $chunkLines = $completeChunk -split "`r?`n"
                foreach ($cl in $chunkLines) {
                    Process-LogLine $cl
                }
            }
        }
    }
    Start-Sleep -Seconds 1
}

# Flush any pending bytes and lines at termination
$flushText = Decode-Bytes @() -flush $true
if ($flushText.Length -gt 0) {
    $pendingText += $flushText
}

if ($pendingText.Length -gt 0) {
    $chunkLines = $pendingText -split "`r?`n"
    foreach ($cl in $chunkLines) {
        Process-LogLine $cl
    }
    $pendingText = ''
}

if (-not $StrictCycles -and $script:currentCycle.has_update) {
    Emit-Cycle $script:currentCycle
    $script:currentCycle = New-CycleState
}
