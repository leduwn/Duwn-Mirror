param(
    [int]$Duration = 20
)

$ErrorActionPreference = "Continue"
$crashDir = "C:\Users\duwn\AppData\Local\DUWN Mirror\Crashes"
$crashesBefore = (Get-ChildItem -Path $crashDir -Filter "*.dmp" -ErrorAction SilentlyContinue).Count

Write-Host "=== PHASE 1: Launching Duwn Mirror (Instance 1) ==="
$p1 = Start-Process ".\build-msvc\bin\Release\duwn-mirror.exe" -ArgumentList "--test-motion" -PassThru
Start-Sleep -Seconds 2

Write-Host "=== PHASE 2: Launching DirectShow Graph (duwn-virtualcam-test.exe $Duration s) ==="
$vcamJob = Start-Job -ScriptBlock {
    param($d)
    & "D:\Projects\Duwn Mirror\build-msvc\bin\Release\duwn-virtualcam-test.exe" --mode stream --duration $d
} -ArgumentList $Duration

Start-Sleep -Seconds 5

Write-Host "=== PHASE 3: Exiting Duwn Mirror normally (WM_CLOSE) ==="
$p1.CloseMainWindow() | Out-Null
$p1.WaitForExit(3000) | Out-Null
if (!$p1.HasExited) { Stop-Process -Id $p1.Id -Force }
Write-Host "Instance 1 exited normally. Graph continues running during source loss..."

Start-Sleep -Seconds 3

Write-Host "=== PHASE 4: Reopening Duwn Mirror (Instance 2) ==="
$p2 = Start-Process ".\build-msvc\bin\Release\duwn-mirror.exe" -ArgumentList "--test-motion" -PassThru
Write-Host "Instance 2 reopened (PID: $($p2.Id)). Graph recovering moving frames..."

$null = Wait-Job $vcamJob
$result = Receive-Job $vcamJob
Remove-Job $vcamJob

Write-Host "=== DIRECTSHOW GRAPH RESULT ==="
$result | ForEach-Object { Write-Host $_ }

Write-Host "=== PHASE 5: Exiting Duwn Mirror Instance 2 normally ==="
$p2.CloseMainWindow() | Out-Null
$p2.WaitForExit(3000) | Out-Null
if (!$p2.HasExited) { Stop-Process -Id $p2.Id -Force }

$crashesAfter = (Get-ChildItem -Path $crashDir -Filter "*.dmp" -ErrorAction SilentlyContinue).Count
Write-Host "Crash dumps before test: $crashesBefore, after test: $crashesAfter"

