param(
    [Parameter(Mandatory = $true)]
    [string]$OutputPath,
    [int]$DurationSeconds = 1800
)

$ErrorActionPreference = 'Stop'
$logPath = Join-Path $env:LOCALAPPDATA 'DUWN Mirror\Logs\duwn-mirror.log'
$deadline = (Get-Date).AddSeconds($DurationSeconds)

'collector_time,source_time,ring_ms,target_ms,padding_ms,servo_ppm,real_underruns,overrun_frames,backlog_drops,recoveries,video_t0_t7_ms,video_queue,state' |
    Set-Content -LiteralPath $OutputPath -Encoding utf8

while ((Get-Date) -lt $deadline) {
    if (Test-Path -LiteralPath $logPath) {
        $tail = Get-Content -LiteralPath $logPath -Tail 500
        $latency = $tail | Where-Object { $_ -match '\[AUDIO LATENCY\]' } | Select-Object -Last 1
        $audio = $tail | Where-Object { $_ -match '\[STATS\] AUDIO:' } | Select-Object -Last 1
        $videoLatency = $tail | Where-Object { $_ -match '\[LATENCY\] T0-T7:' } | Select-Object -Last 1
        $video = $tail | Where-Object { $_ -match '\[STATS\] VIDEO:' } | Select-Object -Last 1
        $session = $tail | Where-Object { $_ -match '\[STATS\] SYNC/SESSION:' } | Select-Object -Last 1

        if ($latency -match '^\[(?<source>[^]]+)\].*ring=(?<ring>[\d.]+)ms target=(?<target>[\d.]+)ms.*padding=(?<padding>[\d.]+)ms.*servo=(?<servo>-?[\d.]+)ppm.*overrun_frames=(?<overrun>\d+) backlog_drops=(?<backlog>\d+) recoveries=(?<recoveries>\d+)') {
            $sourceTime = $Matches.source
            $ring = $Matches.ring
            $target = $Matches.target
            $padding = $Matches.padding
            $servo = $Matches.servo
            $overrun = $Matches.overrun
            $backlog = $Matches.backlog
            $recoveries = $Matches.recoveries
            $underruns = if ($audio -match 'real_underruns=\d+/s \(total=(?<value>\d+)') { $Matches.value } else { '' }
            $t0t7 = if ($videoLatency -match 'T0-T7: total=(?<value>[\d.]+)ms') { $Matches.value } else { '' }
            $queue = if ($video -match '\| q=(?<value>\d+) \|') { $Matches.value } else { '' }
            $state = if ($session -match 'state=(?<value>[^ |]+)') { $Matches.value } else { '' }
            $collectorTime = (Get-Date).ToString('o')
            "$collectorTime,$sourceTime,$ring,$target,$padding,$servo,$underruns,$overrun,$backlog,$recoveries,$t0t7,$queue,$state" |
                Add-Content -LiteralPath $OutputPath -Encoding utf8
        }
    }
    Start-Sleep -Seconds 1
}

(Get-Date).ToString('o') | Set-Content -LiteralPath "$OutputPath.done" -Encoding ascii
