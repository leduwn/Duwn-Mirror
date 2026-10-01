<#
.SYNOPSIS
    Shared validation definitions and routines for Duwn Mirror A/B latency benchmarks.
.DESCRIPTION
    Ensures runner (run-ab-benchmarks.ps1) and analyzer (analyze-latency-ab.ps1) use
    strictly identical configuration targets, cycle validation rules, and disqualification criteria.
#>

$BenchmarkConfigs = [ordered]@{
    'B_OFF' = @{ Mode = 'Balanced'; Preview = 'OFF'; Desc = 'Balanced (SmoothLive), Preview OFF'; TargetMaxQ = '3'; TargetResMs = $null }
    'B_ON'  = @{ Mode = 'Balanced'; Preview = 'ON';  Desc = 'Balanced (SmoothLive), Preview ON';  TargetMaxQ = '3'; TargetResMs = $null }
    'F_OFF' = @{ Mode = 'Fastest';  Preview = 'OFF'; Desc = 'Fastest (LowLatency 1-frame), Preview OFF'; TargetMaxQ = '1'; TargetResMs = $null }
    'F_ON'  = @{ Mode = 'Fastest';  Preview = 'ON';  Desc = 'Fastest (LowLatency 1-frame), Preview ON';  TargetMaxQ = '1'; TargetResMs = $null }
    'C_OFF' = @{ Mode = 'Custom';   Preview = 'OFF'; Desc = 'Custom (2 frames / 25 ms), Preview OFF'; TargetMaxQ = '2'; TargetResMs = '25' }
    'C_ON'  = @{ Mode = 'Custom';   Preview = 'ON';  Desc = 'Custom (2 frames / 25 ms), Preview ON';  TargetMaxQ = '2'; TargetResMs = '25' }
}

function Test-BenchmarkCycleRow {
    param(
        [Parameter(Mandatory = $true)]
        [object]$Row,
        [Parameter(Mandatory = $true)]
        [hashtable]$ExpectedCfg,
        [int]$CycleIndex = -1
    )

    $violations = [System.Collections.Generic.List[string]]::new()
    $prefix = if ($CycleIndex -ge 0) { "Cycle #$CycleIndex (id=$($Row.cycle_id)): " } else { "" }

    # 1. stream_mode check
    if ([string]::IsNullOrWhiteSpace($Row.stream_mode)) {
        $violations.Add("${prefix}Missing required stream_mode metadata.")
    } elseif ($Row.stream_mode -ne $ExpectedCfg.Mode) {
        $violations.Add("${prefix}stream_mode mismatch: reported '$($Row.stream_mode)', expected '$($ExpectedCfg.Mode)'.")
    }

    # 2. policy_max_q check
    if ([string]::IsNullOrWhiteSpace($Row.policy_max_q)) {
        $violations.Add("${prefix}Missing required policy_max_q metadata.")
    } elseif ($Row.policy_max_q -ne $ExpectedCfg.TargetMaxQ) {
        $violations.Add("${prefix}policy_max_q mismatch: reported '$($Row.policy_max_q)', expected '$($ExpectedCfg.TargetMaxQ)'.")
    }

    # 3. Custom policy_res_ms check
    if ($ExpectedCfg.Mode -eq 'Custom') {
        if ([string]::IsNullOrWhiteSpace($Row.policy_res_ms)) {
            $violations.Add("${prefix}Missing required policy_res_ms metadata for Custom mode.")
        } elseif ($Row.policy_res_ms -ne $ExpectedCfg.TargetResMs) {
            $violations.Add("${prefix}Custom policy_res_ms mismatch: reported '$($Row.policy_res_ms)', expected '$($ExpectedCfg.TargetResMs)'.")
        }
    }

    # 4. preview_visible check
    $expPrevVal = if ($ExpectedCfg.Preview -eq 'ON') { '1' } else { '0' }
    if ([string]::IsNullOrWhiteSpace($Row.preview_visible)) {
        $violations.Add("${prefix}Missing required preview_visible metadata.")
    } elseif ($Row.preview_visible -ne $expPrevVal) {
        $violations.Add("${prefix}preview_visible mismatch: reported '$($Row.preview_visible)', expected '$expPrevVal'.")
    }

    # 5. quality_pending check (must be strictly 0 / false)
    if ([string]::IsNullOrWhiteSpace($Row.quality_pending)) {
        $violations.Add("${prefix}Missing required quality_pending metadata.")
    } elseif ($Row.quality_pending -ne '0' -and $Row.quality_pending -ne 'false') {
        $violations.Add("${prefix}quality_pending is not 0 (value: '$($Row.quality_pending)'). Configuration change was still pending.")
    }

    return $violations.ToArray()
}

function Test-BenchmarkRunValidation {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RunId,
        [Parameter(Mandatory = $false)]
        [object]$Manifest,
        [Parameter(Mandatory = $true)]
        [object[]]$Rows
    )

    if (-not $BenchmarkConfigs.Contains($RunId)) {
        return @{
            IsValid    = $false
            Status     = 'INVALID_RUN_ID'
            Reasons    = @("Run ID '$RunId' is not recognized in benchmark configuration matrix.")
            ValidRows  = @()
            TotalRows  = $Rows.Count
        }
    }

    $expectedCfg = $BenchmarkConfigs[$RunId]

    # Validate manifest existence and status
    if ($null -eq $Manifest) {
        return @{
            IsValid    = $false
            Status     = 'NO_MANIFEST'
            Reasons    = @("manifest.json is missing or unreadable.")
            ValidRows  = @()
            TotalRows  = $Rows.Count
        }
    }

    if ($Manifest.status -ne 'VALID_STREAM') {
        return @{
            IsValid    = $false
            Status     = if ($Manifest.status) { $Manifest.status } else { 'INVALID_MANIFEST_STATUS' }
            Reasons    = @("Manifest status is '$($Manifest.status)', expected 'VALID_STREAM'.")
            ValidRows  = @()
            TotalRows  = $Rows.Count
        }
    }

    # Validate CSV rows
    if ($null -eq $Rows -or $Rows.Count -eq 0) {
        return @{
            IsValid    = $false
            Status     = 'EMPTY_CSV'
            Reasons    = @("No measurement rows found in CSV.")
            ValidRows  = @()
            TotalRows  = 0
        }
    }

    $allViolations = [System.Collections.Generic.List[string]]::new()
    $activeDecodedRows = 0
    $outOkDelta = [int64]0
    $prevOkVal = $null

    for ($i = 0; $i -lt $Rows.Count; $i++) {
        $row = $Rows[$i]

        # Inspect cycle row against expected configuration
        $rowViolations = Test-BenchmarkCycleRow -Row $row -ExpectedCfg $expectedCfg -CycleIndex $i
        foreach ($v in $rowViolations) {
            $allViolations.Add($v)
        }

        # Track active decoded rows
        $dFps = 0.0
        if ($row.v_dec_fps -and [double]::TryParse($row.v_dec_fps, [System.Globalization.NumberStyles]::Float, [System.Globalization.CultureInfo]::InvariantCulture, [ref]$dFps)) {
            if ($dFps -gt 0) {
                $activeDecodedRows++
            }
        }

        # Calculate output present delta (reset-aware)
        if ($row.output_ok -ne '' -and $null -ne $row.output_ok) {
            $currOk = [int64]$row.output_ok
            if ($null -ne $prevOkVal) {
                if ($currOk -ge $prevOkVal) {
                    $outOkDelta += ($currOk - $prevOkVal)
                } else {
                    $outOkDelta += $currOk # counter reset
                }
            }
            $prevOkVal = $currOk
        }
    }

    if ($allViolations.Count -gt 0) {
        return @{
            IsValid    = $false
            Status     = 'DISQUALIFIED_CONFIG'
            Reasons    = $allViolations.ToArray()
            ValidRows  = @()
            TotalRows  = $Rows.Count
        }
    }

    if ($activeDecodedRows -le 0) {
        return @{
            IsValid    = $false
            Status     = 'NO_VALID_STREAM'
            Reasons    = @("No cycles with active decoded video (v_dec_fps <= 0 across all cycles).")
            ValidRows  = @()
            TotalRows  = $Rows.Count
        }
    }

    if ($outOkDelta -le 0) {
        return @{
            IsValid    = $false
            Status     = 'NO_ACTIVE_OUTPUT'
            Reasons    = @("Decoded video present but output present count did not advance (delta_output_ok <= 0).")
            ValidRows  = @()
            TotalRows  = $Rows.Count
        }
    }

    # Filter rows with active video for latency metrics
    $validLatencyRows = @($Rows | Where-Object {
        $d = 0.0
        ($_.v_dec_fps -and [double]::TryParse($_.v_dec_fps, [System.Globalization.NumberStyles]::Float, [System.Globalization.CultureInfo]::InvariantCulture, [ref]$d) -and $d -gt 0)
    })

    return @{
        IsValid    = $true
        Status     = 'VALID'
        Reasons    = @()
        ValidRows  = $validLatencyRows
        TotalRows  = $Rows.Count
        OutOkDelta = $outOkDelta
    }
}

function Test-LiveTelemetryTarget {
    param(
        [string]$Path,
        [hashtable]$ExpectedCfg
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        return @{
            IsReady = $false
            Errors  = @("Log file not found at: $Path")
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
            Errors  = @("Failed to read log file: $($_.Exception.Message)")
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
                o_ok  = [int64]$Matches.ook
                p_ok  = [int64]$Matches.pok
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

    if ($null -eq $lastOutAge -or $lastOutAge.count -le 0) {
        $errors += "Missing [FRAME AGE OUTPUT] telemetry record or out_age_count <= 0. Frame scheduler has not presented frames to display yet."
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

