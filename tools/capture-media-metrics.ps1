param(
    [Parameter(Mandatory = $true)]
    [string]$OutputPath,
    [int]$DurationSeconds = 1800,
    [string]$LogPath = (Join-Path $env:LOCALAPPDATA 'DUWN Mirror\Logs\duwn-mirror.log')
)

$ErrorActionPreference = 'Stop'
$runId = [System.Guid]::NewGuid().ToString('D')
$deadline = (Get-Date).AddSeconds($DurationSeconds)

$header = @(
    'run_id', 'collector_time', 'source_time', 'reconnect_event',
    'commit', 'transport', 'stream_mode', 'policy_max_q', 'policy_res_ms', 'policy_cad_pct', 'policy_always_latest', 'req_quality', 'actual_codec', 'actual_res', 'actual_fps',
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
    'v_rtp_rate', 'v_kb_rate', 'v_au_rate', 'v_dec_fps', 'v_rend_fps', 'unique_pres_fps', 'v_drop_rate', 'superseded_rate', 'late_rate', 'queue_depth', 'gen', 'coded_res', 'vis_res',
    'audio_stale', 'a_rtp_rate', 'a_kb_rate', 'audio_buf_ms', 'ring_ms', 'target_ms', 'padding_ms', 'servo_ppm', 'real_underruns', 'overrun_frames', 'backlog_drops', 'recoveries',
    'av_offset_ms', 'drift_ppm', 'sidecar', 'session_state', 'source_conn', 'lifecycle'
) -join ','

$outDir = Split-Path -Parent $OutputPath
if ($outDir -and -not (Test-Path -LiteralPath $outDir)) {
    New-Item -ItemType Directory -Path $outDir -Force | Out-Null
}

$header | Set-Content -LiteralPath $OutputPath -Encoding utf8

$lastFilePos = 0
$lastGen = $null
$lastSourceConn = $null

# Persistent state tracking across lines
$state_commit = ''
$state_transport = ''
$state_stream_mode = ''
$state_policy_max_q = ''
$state_policy_res_ms = ''
$state_policy_cad_pct = ''
$state_policy_always_latest = ''
$state_req_quality = ''
$state_actual_codec = ''
$state_actual_res = ''
$state_actual_fps = ''

$state_t0_t7_total = ''
$state_t0_t1 = ''
$state_t1_t2 = ''
$state_t2_t3 = ''
$state_t3_t4 = ''
$state_t4_t5 = ''
$state_t5_t6 = ''
$state_t6_t7 = ''

$state_decode_avg = ''
$state_decode_p50 = ''
$state_decode_p95 = ''
$state_decode_n = ''

$state_q_res_avg = ''
$state_q_res_p50 = ''
$state_q_res_p95 = ''
$state_q_res_n = ''

$state_dxgi_wait_avg = ''
$state_dxgi_wait_p50 = ''
$state_dxgi_wait_p95 = ''
$state_dxgi_wait_n = ''

$state_vp_avg = ''
$state_vp_p50 = ''
$state_vp_p95 = ''
$state_vp_n = ''

$state_pres_avg = ''
$state_pres_p50 = ''
$state_pres_p95 = ''
$state_pres_n = ''

$state_output_att = ''
$state_output_ok = ''
$state_output_skip = ''
$state_output_err = ''

$state_prev_att = ''
$state_prev_ok = ''
$state_prev_skip = ''
$state_prev_err = ''

$state_out_age_count = ''
$state_out_sel_p50 = ''
$state_out_sel_p95 = ''
$state_out_pres_p50 = ''
$state_out_pres_p95 = ''

$state_prev_age_count = ''
$state_prev_skips = ''
$state_prev_pres_p50 = ''

$state_v_rtp = ''
$state_v_kb = ''
$state_v_au = ''
$state_v_dec = ''
$state_v_rend = ''
$state_unique_pres = ''
$state_v_drop = ''
$state_superseded = ''
$state_v_late = ''
$state_q_depth = ''
$state_gen = ''
$state_coded_res = ''
$state_vis_res = ''

$state_a_rtp = ''
$state_a_kb = ''
$state_a_buf = ''
$state_a_real_underruns = ''
$state_ring = ''
$state_target = ''
$state_padding = ''
$state_servo = ''
$state_overrun = ''
$state_backlog = ''
$state_recoveries = ''
$audio_last_updated = $null

$state_av_offset = ''
$state_drift = ''
$state_sidecar = ''
$state_session_state = ''
$state_source_conn = ''
$state_lifecycle = ''

while ((Get-Date) -lt $deadline) {
    if (Test-Path -LiteralPath $LogPath) {
        $lines = @()
        try {
            $fileInfo = New-Object System.IO.FileInfo($LogPath)
            if ($fileInfo.Length -lt $lastFilePos) {
                # Log file was truncated or rotated
                $lastFilePos = 0
            }
            if ($fileInfo.Length -gt $lastFilePos) {
                $fs = [System.IO.FileStream]::new(
                    $LogPath,
                    [System.IO.FileMode]::Open,
                    [System.IO.FileAccess]::Read,
                    [System.IO.FileShare]::ReadWrite
                )
                try {
                    $null = $fs.Seek($lastFilePos, [System.IO.SeekOrigin]::Begin)
                    $sr = [System.IO.StreamReader]::new($fs, [System.Text.Encoding]::UTF8)
                    while (-not $sr.EndOfStream) {
                        $l = $sr.ReadLine()
                        if ($l) { $lines += $l }
                    }
                    $lastFilePos = $fs.Position
                } finally {
                    $fs.Dispose()
                }
            }
        } catch {
            # Non-fatal read error (e.g. transient file access during log rotation)
        }

        if ($lines.Count -gt 0) {
            $hasDiagnosticsUpdate = $false
            $lastSourceTime = ''

            foreach ($line in $lines) {
                if ($line -match '^\[(?<time>\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\]') {
                    $lastSourceTime = $Matches.time
                }

                if ($line -match '\[METADATA\] commit=(?<commit>[^ ]*) \| transport=(?<transport>[^ ]*) \| stream_mode=(?<mode>[^ ]*) \| policy\(max_q=(?<mq>\d+), res_ms=(?<rms>\d+), cad_pct=(?<cp>\d+), always_latest=(?<al>\d+)\) \| req_quality=(?<rq>[^ |]*) \| actual_stream\(codec=(?<codec>[^,]*), res=(?<res>[^,]*), fps=(?<fps>[^)]*)\)') {
                    $state_commit = $Matches.commit
                    $state_transport = $Matches.transport
                    $state_stream_mode = $Matches.mode
                    $state_policy_max_q = $Matches.mq
                    $state_policy_res_ms = $Matches.rms
                    $state_policy_cad_pct = $Matches.cp
                    $state_policy_always_latest = $Matches.al
                    $state_req_quality = $Matches.rq
                    $state_actual_codec = $Matches.codec
                    $state_actual_res = $Matches.res
                    $state_actual_fps = $Matches.fps
                    $hasDiagnosticsUpdate = $true
                }
                elseif ($line -match '\[STATS\] VIDEO: rtp=(?<rtp>\d+)/s \((?<kb>[\d.]+) KB/s\) \| au=(?<au>\d+)/s \| dec=(?<dec>\d+) fps \| rend=(?<rend>\d+) fps \(unique=(?<up>\d+).*?\) \| .*? \| drop=(?<drop>\d+)/s \(superseded=(?<sup_rate>\d+)/s, late=(?<late_rate>\d+)/s.*?\) \| q=(?<q>\d+) \| gen=(?<gen>\d+) \| coded=(?<coded>\S+) vis=(?<vis>\S+)') {
                    $state_v_rtp = $Matches.rtp
                    $state_v_kb = $Matches.kb
                    $state_v_au = $Matches.au
                    $state_v_dec = $Matches.dec
                    $state_v_rend = $Matches.rend
                    $state_unique_pres = $Matches.up
                    $state_v_drop = $Matches.drop
                    $state_superseded = $Matches.sup_rate
                    $state_v_late = $Matches.late_rate
                    $state_q_depth = $Matches.q
                    $state_gen = $Matches.gen
                    $state_coded_res = $Matches.coded
                    $state_vis_res = $Matches.vis
                    $hasDiagnosticsUpdate = $true
                }
                elseif ($line -match '\[STATS\] PRESENT: interval_avg=.*? \| call_avg=.*? \| dxgi_wait_avg=(?<dwait>[\d.]+)ms(?: \| output\(att=(?<oatt>\d+), ok=(?<ook>\d+), skip=(?<oskip>\d+), err=(?<oerr>\d+)\) \| preview\(att=(?<patt>\d+), ok=(?<pok>\d+), skip=(?<pskip>\d+), err=(?<perr>\d+)\))?') {
                    $state_dxgi_wait_avg = $Matches.dwait
                    if ($Matches.ContainsKey('oatt')) {
                        $state_output_att = $Matches.oatt
                        $state_output_ok = $Matches.ook
                        $state_output_skip = $Matches.oskip
                        $state_output_err = $Matches.oerr
                        $state_prev_att = $Matches.patt
                        $state_prev_ok = $Matches.pok
                        $state_prev_skip = $Matches.pskip
                        $state_prev_err = $Matches.perr
                    }
                    $hasDiagnosticsUpdate = $true
                }
                elseif ($line -match '\[STAGE LATENCY\] decode: avg=(?<d_avg>[\d.]+)ms p50=(?<d_p50>[\d.]+)ms p95=(?<d_p95>[\d.]+)ms \(n=(?<d_n>\d+)\) \| queue_res: avg=(?<qr_avg>[\d.]+)ms p50=(?<qr_p50>[\d.]+)ms p95=(?<qr_p95>[\d.]+)ms \(n=(?<qr_n>\d+)\) \| dxgi_wait: avg=(?<dw_avg>[\d.]+)ms p50=(?<dw_p50>[\d.]+)ms p95=(?<dw_p95>[\d.]+)ms max=(?<dw_max>[\d.]+)ms \(n=(?<dw_n>\d+)\) \| vp: avg=(?<vp_avg>[\d.]+)ms p50=(?<vp_p50>[\d.]+)ms p95=(?<vp_p95>[\d.]+)ms \(n=(?<vp_n>\d+)\) \| present: avg=(?<pr_avg>[\d.]+)ms p50=(?<pr_p50>[\d.]+)ms p95=(?<pr_p95>[\d.]+)ms \(n=(?<pr_n>\d+)\)') {
                    $state_decode_avg = $Matches.d_avg
                    $state_decode_p50 = $Matches.d_p50
                    $state_decode_p95 = $Matches.d_p95
                    $state_decode_n = $Matches.d_n
                    $state_q_res_avg = $Matches.qr_avg
                    $state_q_res_p50 = $Matches.qr_p50
                    $state_q_res_p95 = $Matches.qr_p95
                    $state_q_res_n = $Matches.qr_n
                    $state_dxgi_wait_avg = $Matches.dw_avg
                    $state_dxgi_wait_p50 = $Matches.dw_p50
                    $state_dxgi_wait_p95 = $Matches.dw_p95
                    $state_dxgi_wait_n = $Matches.dw_n
                    $state_vp_avg = $Matches.vp_avg
                    $state_vp_p50 = $Matches.vp_p50
                    $state_vp_p95 = $Matches.vp_p95
                    $state_vp_n = $Matches.vp_n
                    $state_pres_avg = $Matches.pr_avg
                    $state_pres_p50 = $Matches.pr_p50
                    $state_pres_p95 = $Matches.pr_p95
                    $state_pres_n = $Matches.pr_n
                    $hasDiagnosticsUpdate = $true
                }
                elseif ($line -match '\[LATENCY\] T0-T7(?: \(native receiver latency\))?: total=(?<total>[\d.]+)ms \(T0-T1=(?<t01>[\d.]+)ms, T1-T2=(?<t12>[\d.]+)ms, T2-T3=(?<t23>[\d.]+)ms, T3-T4=(?<t34>[\d.]+)ms, T4-T5\[q_age\]=(?<t45>[\d.]+)ms, T5-T6\[vp\]=(?<t56>[\d.]+)ms, T6-T7\[pres\]=(?<t67>[\d.]+)ms\)') {
                    $state_t0_t7_total = $Matches.total
                    $state_t0_t1 = $Matches.t01
                    $state_t1_t2 = $Matches.t12
                    $state_t2_t3 = $Matches.t23
                    $state_t3_t4 = $Matches.t34
                    $state_t4_t5 = $Matches.t45
                    $state_t5_t6 = $Matches.t56
                    $state_t6_t7 = $Matches.t67
                    $hasDiagnosticsUpdate = $true
                }
                elseif ($line -match '\[FRAME AGE OUTPUT\] count=(?<cnt>\d+) \| select: p50=(?<sp50>[\d.]+)ms p95=(?<sp95>[\d.]+)ms.*? \| present: p50=(?<pp50>[\d.]+)ms p95=(?<pp95>[\d.]+)ms') {
                    $state_out_age_count = $Matches.cnt
                    $state_out_sel_p50 = $Matches.sp50
                    $state_out_sel_p95 = $Matches.sp95
                    $state_out_pres_p50 = $Matches.pp50
                    $state_out_pres_p95 = $Matches.pp95
                    $hasDiagnosticsUpdate = $true
                }
                elseif ($line -match '\[FRAME AGE PREVIEW\] count=(?<cnt>\d+) skips=(?<skips>\d+) \|.*? \| present: p50=(?<pp50>[\d.]+)ms') {
                    $state_prev_age_count = $Matches.cnt
                    $state_prev_skips = $Matches.skips
                    $state_prev_pres_p50 = $Matches.pp50
                    $hasDiagnosticsUpdate = $true
                }
                elseif ($line -match '\[STATS\] AUDIO: rtp=(?<artp>\d+)/s \((?<akb>[\d.]+) KB/s\).*? \| buf=(?<abuf>[\d.]+)ms.*? \| real_underruns=\d+/s \(total=(?<underruns>\d+)') {
                    $state_a_rtp = $Matches.artp
                    $state_a_kb = $Matches.akb
                    $state_a_buf = $Matches.abuf
                    $state_a_real_underruns = $Matches.underruns
                    $hasDiagnosticsUpdate = $true
                }
                elseif ($line -match '\[AUDIO LATENCY\] .*?ring=(?<ring>[\d.]+)ms target=(?<target>[\d.]+)ms.*?padding=(?<padding>[\d.]+)ms.*?servo=(?<servo>-?[\d.]+)ppm.*?overrun_frames=(?<overrun>\d+) backlog_drops=(?<backlog>\d+) recoveries=(?<recoveries>\d+)') {
                    $state_ring = $Matches.ring
                    $state_target = $Matches.target
                    $state_padding = $Matches.padding
                    $state_servo = $Matches.servo
                    $state_overrun = $Matches.overrun
                    $state_backlog = $Matches.backlog
                    $state_recoveries = $Matches.recoveries
                    $audio_last_updated = Get-Date
                    $hasDiagnosticsUpdate = $true
                }
                elseif ($line -match '\[STATS\] SYNC/SESSION: A/V=(?<av>-?[\d.]+)ms drift=(?<drift>-?[\d.]+)ms/min \| sidecar=(?<sidecar>[^ |]+) \| state=(?<state>[^ |]+) \| source=(?<source>[^ |]+) \|.*? \| lifecycle=(?<life>.*)') {
                    $state_av_offset = $Matches.av
                    $state_drift = $Matches.drift
                    $state_sidecar = $Matches.sidecar
                    $state_session_state = $Matches.state
                    $state_source_conn = $Matches.source
                    $state_lifecycle = $Matches.life.Trim()
                    $hasDiagnosticsUpdate = $true
                }
            }

            if ($hasDiagnosticsUpdate) {
                # Determine reconnect or format change
                $reconnectEvent = 0
                if ($null -ne $lastGen -and $state_gen -ne '' -and $state_gen -ne $lastGen) {
                    $reconnectEvent = 1
                }
                if ($null -ne $lastSourceConn -and $state_source_conn -eq 'connected' -and $lastSourceConn -eq 'disconnected') {
                    $reconnectEvent = 1
                }
                if ($state_gen -ne '') { $lastGen = $state_gen }
                if ($state_source_conn -ne '') { $lastSourceConn = $state_source_conn }

                # Audio freshness evaluation (mark stale if >3s since last [AUDIO LATENCY] update)
                $audioStale = 1
                if ($null -ne $audio_last_updated) {
                    $audioAgeSec = ((Get-Date) - $audio_last_updated).TotalSeconds
                    if ($audioAgeSec -le 3.0) {
                        $audioStale = 0
                    }
                }

                $collectorTime = (Get-Date).ToString('o')
                $sourceTime = if ($lastSourceTime -ne '') { $lastSourceTime } else { (Get-Date).ToString('yyyy-MM-dd HH:mm:ss.fff') }

                $csvRow = @(
                    $runId, $collectorTime, $sourceTime, $reconnectEvent,
                    $state_commit, $state_transport, $state_stream_mode, $state_policy_max_q, $state_policy_res_ms, $state_policy_cad_pct, $state_policy_always_latest, $state_req_quality, $state_actual_codec, $state_actual_res, $state_actual_fps,
                    $state_t0_t7_total, $state_t0_t1, $state_t1_t2, $state_t2_t3, $state_t3_t4, $state_t4_t5, $state_t5_t6, $state_t6_t7,
                    $state_decode_avg, $state_decode_p50, $state_decode_p95, $state_decode_n,
                    $state_q_res_avg, $state_q_res_p50, $state_q_res_p95, $state_q_res_n,
                    $state_dxgi_wait_avg, $state_dxgi_wait_p50, $state_dxgi_wait_p95, $state_dxgi_wait_n,
                    $state_vp_avg, $state_vp_p50, $state_vp_p95, $state_vp_n,
                    $state_pres_avg, $state_pres_p50, $state_pres_p95, $state_pres_n,
                    $state_output_att, $state_output_ok, $state_output_skip, $state_output_err,
                    $state_prev_att, $state_prev_ok, $state_prev_skip, $state_prev_err,
                    $state_out_age_count, $state_out_sel_p50, $state_out_sel_p95, $state_out_pres_p50, $state_out_pres_p95,
                    $state_prev_age_count, $state_prev_skips, $state_prev_pres_p50,
                    $state_v_rtp, $state_v_kb, $state_v_au, $state_v_dec, $state_v_rend, $state_unique_pres, $state_v_drop, $state_superseded, $state_v_late, $state_q_depth, $state_gen, $state_coded_res, $state_vis_res,
                    $audioStale, $state_a_rtp, $state_a_kb, $state_a_buf, $state_ring, $state_target, $state_padding, $state_servo, $state_a_real_underruns, $state_overrun, $state_backlog, $state_recoveries,
                    $state_av_offset, $state_drift, $state_sidecar, $state_session_state, $state_source_conn, $state_lifecycle
                ) -join ','

                $csvRow | Add-Content -LiteralPath $OutputPath -Encoding utf8
            }
        }
    }
    Start-Sleep -Seconds 1
}

(Get-Date).ToString('o') | Set-Content -LiteralPath "$OutputPath.done" -Encoding ascii
