<#
.SYNOPSIS
    Audits AppleModelDatabase.cpp for consistency, duplicates, and coverage.
.DESCRIPTION
    Parses src\airplay\AppleModelDatabase.cpp and src\airplay\AppleModelDatabase.h,
    checking:
    1. Total models count (iPhone, iPad, Total)
    2. Oldest and newest models for iPhone and iPad
    3. Duplicate identifiers
    4. Canonical OS family strings (iOS, iPadOS)
    5. Formatting and verification audit comments
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$rootDir = (Resolve-Path "$PSScriptRoot\..").Path
$cppFile = Join-Path $rootDir "src\airplay\AppleModelDatabase.cpp"
$headerFile = Join-Path $rootDir "src\airplay\AppleModelDatabase.h"

Write-Host "=== Duwn Mirror Apple Model Database Audit ===" -ForegroundColor Cyan

if (-not (Test-Path $cppFile)) {
    Write-Error "Could not find $cppFile"
    exit 1
}

$lines = Get-Content -Path $cppFile
$entries = @()
$entryRegex = '\{\s*L"([^"]+)"\s*,\s*L"([^"]+)"\s*,\s*L"([^"]+)"\s*\}'

foreach ($line in $lines) {
    if ($line -match $entryRegex) {
        $entries += [PSCustomObject]@{
            Id   = $Matches[1]
            Name = $Matches[2]
            OS   = $Matches[3]
        }
    }
}

Write-Host "`nParsed Entries: $($entries.Count)" -ForegroundColor White

# 1. Duplicate Detection
$duplicates = $entries | Group-Object -Property Id | Where-Object { $_.Count -gt 1 }
if ($duplicates.Count -gt 0) {
    Write-Host "`n[ERROR] Found Duplicate Identifiers:" -ForegroundColor Red
    foreach ($dup in $duplicates) {
        Write-Host "  - $($dup.Name) ($($dup.Count) occurrences)" -ForegroundColor Red
    }
    exit 1
} else {
    Write-Host "[OK] Zero duplicate identifiers found." -ForegroundColor Green
}

# 2. Canonical OS check
$invalidOs = $entries | Where-Object { $_.OS -ne "iOS" -and $_.OS -ne "iPadOS" }
if ($invalidOs.Count -gt 0) {
    Write-Host "`n[ERROR] Found Non-Canonical OS strings:" -ForegroundColor Red
    foreach ($item in $invalidOs) {
        Write-Host "  - $($item.Id): '$($item.OS)'" -ForegroundColor Red
    }
    exit 1
} else {
    Write-Host "[OK] All OS strings canonical (iOS / iPadOS)." -ForegroundColor Green
}

# 3. Categorization
$iphones = $entries | Where-Object { $_.Id -like "iPhone*" }
$ipads   = $entries | Where-Object { $_.Id -like "iPad*" }
$others  = $entries | Where-Object { $_.Id -notlike "iPhone*" -and $_.Id -notlike "iPad*" }

Write-Host "`nBreakdown:" -ForegroundColor White
Write-Host "  - iPhones: $($iphones.Count)" -ForegroundColor Gray
Write-Host "  - iPads:   $($ipads.Count)" -ForegroundColor Gray
Write-Host "  - Others:  $($others.Count)" -ForegroundColor Gray

# 4. Oldest and Newest
Write-Host "`nCoverage Boundaries:" -ForegroundColor White
if ($iphones.Count -gt 0) {
    Write-Host "  - Oldest iPhone: $($iphones[0].Id) -> $($iphones[0].Name)" -ForegroundColor Cyan
    Write-Host "  - Newest iPhone: $($iphones[-1].Id) -> $($iphones[-1].Name)" -ForegroundColor Cyan
}
if ($ipads.Count -gt 0) {
    Write-Host "  - Oldest iPad:   $($ipads[0].Id) -> $($ipads[0].Name)" -ForegroundColor Cyan
    Write-Host "  - Newest iPad:   $($ipads[-1].Id) -> $($ipads[-1].Name)" -ForegroundColor Cyan
}

# 5. Metadata Check in Header
$headerContent = Get-Content -Path $headerFile -Raw
if ($headerContent -match 'kRevision\{L"([^"]+)"\}') {
    Write-Host "`nDatabase Revision: $($Matches[1])" -ForegroundColor Green
}
if ($headerContent -match 'kUpdatedDate\{L"([^"]+)"\}') {
    Write-Host "Updated Date:      $($Matches[1])" -ForegroundColor Green
}

Write-Host "`n[PASS] Database audit completed successfully with zero violations." -ForegroundColor Green
