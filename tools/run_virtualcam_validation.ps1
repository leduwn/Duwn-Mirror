param(
    [int]$Duration = 10,
    [switch]$DualGraph
)

Write-Host "Starting duwn-mirror with --test-motion..."
$mirror = Start-Process -FilePath "D:\Projects\Duwn Mirror\build-msvc\bin\Release\duwn-mirror.exe" -ArgumentList "--test-motion" -PassThru
Start-Sleep -Seconds 3

try {
    if ($DualGraph) {
        Write-Host "Running Dual Graph Validation ($Duration s)..."
        $job1 = Start-Job -ScriptBlock {
            param($d)
            & "D:\Projects\Duwn Mirror\build-msvc\bin\Release\duwn-virtualcam-test.exe" --mode stream --duration $d
        } -ArgumentList $Duration

        $job2 = Start-Job -ScriptBlock {
            param($d)
            & "D:\Projects\Duwn Mirror\build-msvc\bin\Release\duwn-virtualcam-test.exe" --mode stream --duration $d
        } -ArgumentList $Duration

        $res1 = Receive-Job -Job $job1 -Wait
        $res2 = Receive-Job -Job $job2 -Wait
        Remove-Job -Job $job1, $job2

        Write-Host "=== GRAPH 1 RESULT ==="
        Write-Host ($res1 -join "`n")
        Write-Host "=== GRAPH 2 RESULT ==="
        Write-Host ($res2 -join "`n")
    } else {
        Write-Host "Running Single Graph Validation ($Duration s)..."
        $res = & "D:\Projects\Duwn Mirror\build-msvc\bin\Release\duwn-virtualcam-test.exe" --mode stream --duration $Duration
        Write-Host ($res -join "`n")
    }
} finally {
    Write-Host "Stopping duwn-mirror..."
    Stop-Process -Id $mirror.Id -Force -ErrorAction SilentlyContinue
}
