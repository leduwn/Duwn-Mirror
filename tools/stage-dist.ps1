<#
.SYNOPSIS
    Stages the clean, standalone distribution layout for Duwn Mirror.
.DESCRIPTION
    Creates dist\Duwn Mirror\ containing the main executable, private AirPlay runtime,
    licenses, third-party notices, and firewall helpers.
    Strictly excludes development tools (.venv, tools\wired-probe, __pycache__, .pdb).
#>
[CmdletBinding()]
param(
    [string]$OutputDir = ""
)

$ErrorActionPreference = 'Stop'
$rootDir = (Resolve-Path "$PSScriptRoot\..").Path

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $rootDir "dist\Duwn Mirror"
}

Write-Host "=== Staging Duwn Mirror Distribution ===" -ForegroundColor Cyan
Write-Host "Output Directory: $OutputDir"

# Clean target directory
if (Test-Path $OutputDir) {
    Write-Host "Cleaning existing dist directory..." -ForegroundColor Gray
    Remove-Item -Path $OutputDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

# 1. Main Executable
$exeSource = Join-Path $rootDir "build-msvc\bin\Release\duwn-mirror.exe"
if (-not (Test-Path $exeSource)) {
    Write-Error "Main executable not found at: $exeSource. Please build Release configuration first."
    exit 1
}
Copy-Item $exeSource -Destination (Join-Path $OutputDir "duwn-mirror.exe") -Force
Write-Host " [OK] duwn-mirror.exe" -ForegroundColor Green

# 2. AirPlay Runtime
$runtimeSource = Join-Path $rootDir "runtime\duwn-airplay"
if (-not (Test-Path $runtimeSource)) {
    # Fallback to build-msvc\bin\Release\duwn-airplay if runtime was built there
    $runtimeSource = Join-Path $rootDir "build-msvc\bin\Release\duwn-airplay"
}

if (Test-Path $runtimeSource) {
    $targetRuntime = Join-Path $OutputDir "duwn-airplay"
    New-Item -ItemType Directory -Force -Path $targetRuntime | Out-Null

    # Copy DLLs, plugins, libexec, tools from runtime
    Get-ChildItem -Path $runtimeSource -Recurse | Where-Object {
        $_.Extension -ne ".pdb" -and
        $_.Name -notmatch "(\.ilk|\.exp|\.lib)$"
    } | ForEach-Object {
        $relPath = $_.FullName.Substring($runtimeSource.Length).TrimStart('\', '/')
        $destPath = Join-Path $targetRuntime $relPath
        if ($_.PSIsContainer) {
            New-Item -ItemType Directory -Force -Path $destPath | Out-Null
        } else {
            Copy-Item $_.FullName -Destination $destPath -Force
        }
    }
    Write-Host " [OK] duwn-airplay runtime staged ($((Get-ChildItem $targetRuntime -Recurse -File).Count) files)" -ForegroundColor Green
} else {
    Write-Warning "AirPlay runtime not found at $runtimeSource"
}

# 3. Legal and Notices
$docs = @("LICENSE", "THIRD_PARTY_NOTICES.txt", "UxPlay-GPL-3.0.txt")
foreach ($doc in $docs) {
    $docPath = Join-Path $rootDir $doc
    if (Test-Path $docPath) {
        Copy-Item $docPath -Destination (Join-Path $OutputDir $doc) -Force
        Write-Host " [OK] $doc" -ForegroundColor Green
    } else {
        Write-Warning "Missing documentation file: $doc"
    }
}

# 4. Firewall configuration helpers
$toolsDir = Join-Path $OutputDir "tools"
New-Item -ItemType Directory -Force -Path $toolsDir | Out-Null
$firewallScripts = @("configure-firewall.ps1", "configure-firewall.cmd")
foreach ($script in $firewallScripts) {
    $src = Join-Path $rootDir "tools\$script"
    if (Test-Path $src) {
        Copy-Item $src -Destination (Join-Path $toolsDir $script) -Force
        Write-Host " [OK] tools\$script" -ForegroundColor Green
    }
}

# 5. Verify exclusions (dev tools MUST NOT be in dist)
$bannedPatterns = @("wired-probe", ".venv", "__pycache__", "*.pyc", "*.pdb", "*.obj")
$violations = @()
foreach ($pat in $bannedPatterns) {
    $found = Get-ChildItem -Path $OutputDir -Recurse -Filter $pat -ErrorAction SilentlyContinue
    if ($found) {
        $violations += $found
    }
}

if ($violations.Count -gt 0) {
    Write-Error "Dev tools or banned files found in dist directory:"
    $violations | ForEach-Object { Write-Host "  $($_.FullName)" -ForegroundColor Red }
    exit 1
}

$totalFiles = (Get-ChildItem -Path $OutputDir -Recurse -File).Count
$totalSizeMB = [math]::Round(((Get-ChildItem -Path $OutputDir -Recurse -File | Measure-Object -Property Length -Sum).Sum / 1MB), 2)
Write-Host "`nStaging complete: $OutputDir" -ForegroundColor Green
Write-Host "Total files: $totalFiles ($totalSizeMB MB)" -ForegroundColor White
