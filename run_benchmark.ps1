$cachePath = "C:\Users\duwn\AppData\Local\Duwn Mirror\cache\sidecar_verification.json"
$logPath = "C:\Users\duwn\AppData\Local\Duwn Mirror\Logs\duwn-mirror.log"
$exePath = "D:\Projects\Duwn Mirror\build-msvc\bin\Release\duwn-mirror.exe"

function Run-AppInstance([bool]$isCold) {
    if ($isCold) {
        if ([System.IO.File]::Exists($cachePath)) {
            [System.IO.File]::Delete($cachePath)
        }
    }

    $linesBefore = 0
    if (Test-Path $logPath) {
        $linesBefore = (Get-Content $logPath).Count
    }

    $proc = Start-Process -FilePath $exePath -PassThru
    Start-Sleep -Milliseconds 5500
    Stop-Process -Id $proc.Id -Force
    Start-Sleep -Milliseconds 1500

    $allLines = Get-Content $logPath
    $sessionLines = $allLines[$linesBefore..($allLines.Count - 1)]

    $c1_t = 0; $c2_t = 0; $c3_t = 0; $c4_t = 0; $c5_t = 0; $v_ms = 0
    $success = $false

    foreach ($l in $sessionLines) {
        if ($l -match "C1 Network Discovery Complete.*?\+(\d+\.\d+) ms from C0") { $c1_t = [double]$matches[1] }
        if ($l -match "C2 Media Infrastructure Ready.*?\+(\d+\.\d+) ms from C0") { $c2_t = [double]$matches[1] }
        if ($l -match "C3 UxPlay Process Spawned.*?\+(\d+\.\d+) ms from C0") { $c3_t = [double]$matches[1] }
        if ($l -match "C4 UxPlay Sockets Initialized.*?\+(\d+\.\d+) ms from C0") { $c4_t = [double]$matches[1] }
        if ($l -match "C5 Advertising Ready.*?\+(\d+\.\d+) ms from C0") {
            $c5_t = [double]$matches[1]
            $success = $true
        }
        if ($l -match "verification_ms=(\d+\.\d+)") { $v_ms = [double]$matches[1] }
    }

    $c3_to_c4 = if ($c4_t -gt 0) { [math]::Round($c4_t - $c3_t, 2) } else { [math]::Round($c5_t - $c3_t, 2) }
    $c4_to_c5 = if ($c4_t -gt 0) { [math]::Round($c5_t - $c4_t, 2) } else { 0.0 }

    return [PSCustomObject]@{
        Success = $success
        C0_to_C1 = $c1_t
        C1_to_C2 = [math]::Round($c2_t - $c1_t, 2)
        C2_to_C3 = [math]::Round($c3_t - $c2_t, 2)
        C3_to_C4 = $c3_to_c4
        C4_to_C5 = $c4_to_c5
        C0_to_C5 = $c5_t
        Sidecar_Verify_ms = $v_ms
    }
}

Write-Host "=== EXECUTING 5 COLD RUNS ==="
$coldResults = @()
for ($i = 1; $i -le 5; $i++) {
    Write-Host "Starting Cold Run $i..."
    $res = Run-AppInstance -isCold $true
    $coldResults += $res
    Write-Host "Cold Run $i : C0->C5 = $($res.C0_to_C5) ms, C3->C4 = $($res.C3_to_C4) ms, C4->C5 = $($res.C4_to_C5) ms, Verify = $($res.Sidecar_Verify_ms) ms"
}

Write-Host "`n=== EXECUTING 5 WARM RUNS ==="
$warmResults = @()
for ($i = 1; $i -le 5; $i++) {
    Write-Host "Starting Warm Run $i..."
    $res = Run-AppInstance -isCold $false
    $warmResults += $res
    Write-Host "Warm Run $i : C0->C5 = $($res.C0_to_C5) ms, C3->C4 = $($res.C3_to_C4) ms, C4->C5 = $($res.C4_to_C5) ms, Verify = $($res.Sidecar_Verify_ms) ms"
}

function Get-Stats($arr, $prop) {
    $vals = $arr | ForEach-Object { $_.$prop } | Sort-Object
    $count = $vals.Count
    $sum = ($vals | Measure-Object -Sum).Sum
    $mean = [math]::Round($sum / $count, 2)
    $median = if ($count % 2 -eq 1) { $vals[[math]::Floor($count / 2)] } else { [math]::Round(($vals[$count/2 - 1] + $vals[$count/2]) / 2, 2) }
    $min = $vals[0]
    $max = $vals[-1]
    $p90Index = [math]::Min($count - 1, [math]::Floor($count * 0.9))
    $p90 = $vals[$p90Index]
    $stddev = if ($count -gt 1) {
        $sqDiffs = $vals | ForEach-Object { [math]::Pow($_ - $mean, 2) }
        $variance = ($sqDiffs | Measure-Object -Sum).Sum / ($count - 1)
        [math]::Round([math]::Sqrt($variance), 2)
    } else { 0 }

    return [PSCustomObject]@{
        Property = $prop
        Mean = $mean
        Median = $median
        Min = $min
        Max = $max
        P90 = $p90
        StdDev = $stddev
    }
}

Write-Host "`n=== COLD RUN STATS ==="
$props = @("C0_to_C1", "C1_to_C2", "C2_to_C3", "C3_to_C4", "C4_to_C5", "C0_to_C5", "Sidecar_Verify_ms")
$coldStats = $props | ForEach-Object { Get-Stats $coldResults $_ }
$coldStats | Format-Table -AutoSize

Write-Host "`n=== WARM RUN STATS ==="
$warmStats = $props | ForEach-Object { Get-Stats $warmResults $_ }
$warmStats | Format-Table -AutoSize

$coldResults | Export-Clixml -Path "D:\Projects\Duwn Mirror\cold_results.xml"
$warmResults | Export-Clixml -Path "D:\Projects\Duwn Mirror\warm_results.xml"
