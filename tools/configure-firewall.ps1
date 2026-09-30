<#
.SYNOPSIS
    Configures Windows Defender Firewall rules for Duwn Mirror.
.DESCRIPTION
    Adds or removes executable-scoped and port-scoped inbound firewall rules
    for Duwn Mirror core and UxPlay sidecar across Private and Domain profiles.
.PARAMETER Action
    'Add' to create firewall rules, 'Remove' to delete them. Default is 'Add'.
.PARAMETER InstallDir
    Base installation directory where duwn-mirror.exe is located. Defaults to script parent or current directory.
#>
[CmdletBinding()]
param(
    [ValidateSet('Add', 'Remove')]
    [string]$Action = 'Add',

    [string]$InstallDir = ''
)

$ErrorActionPreference = 'Stop'

# Ensure administrator privileges
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Error "This script requires administrative privileges. Please run in an elevated PowerShell session."
    exit 1
}

$RuleGroup = "Duwn Mirror"

if ($Action -eq 'Remove') {
    Write-Host "Removing all firewall rules for group '$RuleGroup' and legacy 'DUWN Mirror'..." -ForegroundColor Cyan
    try {
        Remove-NetFirewallRule -Group $RuleGroup -ErrorAction SilentlyContinue
        Remove-NetFirewallRule -Group "DUWN Mirror" -ErrorAction SilentlyContinue
        Remove-NetFirewallRule -DisplayName "Duwn Mirror Core (Public)" -ErrorAction SilentlyContinue
        Remove-NetFirewallRule -DisplayName "Duwn Mirror AirPlay (Public)" -ErrorAction SilentlyContinue
        Remove-NetFirewallRule -DisplayName "DUWN Mirror Core (Public)" -ErrorAction SilentlyContinue
        Remove-NetFirewallRule -DisplayName "DUWN Mirror AirPlay (Public)" -ErrorAction SilentlyContinue
        Write-Host "Successfully removed firewall rules." -ForegroundColor Green
    } catch {
        Write-Warning "Failed to remove some firewall rules: $_"
    }
    exit 0
}

# Resolve InstallDir
if ([string]::IsNullOrWhiteSpace($InstallDir)) {
    $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
    if (Test-Path "$scriptDir\..\build-msvc\bin\Release\duwn-mirror.exe") {
        $InstallDir = (Resolve-Path "$scriptDir\..\build-msvc\bin\Release").Path
    } elseif (Test-Path "$scriptDir\duwn-mirror.exe") {
        $InstallDir = $scriptDir
    } else {
        $InstallDir = (Get-Location).Path
    }
}

$coreExe = Join-Path $InstallDir "duwn-mirror.exe"
$sidecarExe = Join-Path $InstallDir "duwn-airplay\uxplay.exe"

Write-Host "Configuring Windows Defender Firewall for Duwn Mirror..." -ForegroundColor Cyan
Write-Host "Install Directory: $InstallDir"
Write-Host "Core Executable:   $coreExe"
Write-Host "Sidecar Binary:    $sidecarExe"

# Clean any existing rules for the group and legacy group first
Remove-NetFirewallRule -Group $RuleGroup -ErrorAction SilentlyContinue
Remove-NetFirewallRule -Group "DUWN Mirror" -ErrorAction SilentlyContinue

$rules = @(
    # 1. Duwn Mirror Core (Inbound Any)
    @{
        DisplayName = "Duwn Mirror Core Application (TCP-In)"
        Name        = "DUWN-Mirror-Core-TCP"
        Direction   = "Inbound"
        Action      = "Allow"
        Protocol    = "TCP"
        Program     = $coreExe
        Profile     = "Domain,Private"
    },
    @{
        DisplayName = "Duwn Mirror Core Application (UDP-In)"
        Name        = "DUWN-Mirror-Core-UDP"
        Direction   = "Inbound"
        Action      = "Allow"
        Protocol    = "UDP"
        Program     = $coreExe
        Profile     = "Domain,Private"
    },
    # 2. UxPlay AirPlay Receiver Sidecar (Inbound)
    @{
        DisplayName = "Duwn Mirror AirPlay Engine (TCP-In)"
        Name        = "DUWN-Mirror-UxPlay-TCP"
        Direction   = "Inbound"
        Action      = "Allow"
        Protocol    = "TCP"
        Program     = $sidecarExe
        Profile     = "Domain,Private"
    },
    @{
        DisplayName = "Duwn Mirror AirPlay Engine (UDP-In)"
        Name        = "DUWN-Mirror-UxPlay-UDP"
        Direction   = "Inbound"
        Action      = "Allow"
        Protocol    = "UDP"
        Program     = $sidecarExe
        Profile     = "Domain,Private"
    },
    # 3. Dedicated mDNS Discovery
    @{
        DisplayName = "Duwn Mirror mDNS Discovery (UDP-In 5353)"
        Name        = "DUWN-Mirror-mDNS-In"
        Direction   = "Inbound"
        Action      = "Allow"
        Protocol    = "UDP"
        LocalPort   = 5353
        Profile     = "Domain,Private"
    },
    # 4. AirPlay RTSP Control Ports (7000, 7001, 7100)
    @{
        DisplayName = "Duwn Mirror AirPlay RTSP (TCP-In 7000,7001,7100)"
        Name        = "DUWN-Mirror-RTSP-In"
        Direction   = "Inbound"
        Action      = "Allow"
        Protocol    = "TCP"
        LocalPort   = @("7000", "7001", "7100")
        Profile     = "Domain,Private"
    },
    # 5. AirPlay Media Stream UDP Range (6000-7100)
    @{
        DisplayName = "Duwn Mirror Media RTP Streams (UDP-In 6000-7100)"
        Name        = "DUWN-Mirror-Media-UDP-In"
        Direction   = "Inbound"
        Action      = "Allow"
        Protocol    = "UDP"
        LocalPort   = "6000-7100"
        Profile     = "Domain,Private"
    }
)

foreach ($r in $rules) {
    try {
        $params = @{
            DisplayName = $r.DisplayName
            Name        = $r.Name
            Group       = $RuleGroup
            Direction   = $r.Direction
            Action      = $r.Action
            Protocol    = $r.Protocol
            Profile     = $r.Profile
        }
        if ($r.ContainsKey('Program') -and (Test-Path $r.Program)) {
            $params['Program'] = $r.Program
        }
        if ($r.ContainsKey('LocalPort')) {
            $params['LocalPort'] = $r.LocalPort
        }

        New-NetFirewallRule @params | Out-Null
        Write-Host " [OK] Created rule: $($r.DisplayName)" -ForegroundColor Green
    } catch {
        Write-Warning " [WARN] Failed to create rule $($r.DisplayName): $_"
    }
}

Write-Host "Firewall configuration completed successfully." -ForegroundColor Green
