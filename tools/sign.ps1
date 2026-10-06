<#
.SYNOPSIS
    Signs Duwn Mirror executables and installers with Authenticode digital signatures.
.DESCRIPTION
    Supports production code signing via environment variables (CERT_PATH, CERT_PASSWORD),
    local development self-signed certificates, and a -DryRun verification mode.
.PARAMETER CertPath
    Path to PFX certificate file. Defaults to $env:CERT_PATH.
.PARAMETER CertPassword
    Password for PFX certificate. Defaults to $env:CERT_PASSWORD.
.PARAMETER SelfSigned
    Generate or use a local self-signed development code-signing certificate.
.PARAMETER DryRun
    Verify files and certificate availability without applying signatures.
.PARAMETER Files
    Explicit list of files to sign. Defaults to release binaries and installers.
#>
[CmdletBinding()]
param(
    [string]$CertPath = $env:CERT_PATH,
    [string]$CertPassword = $env:CERT_PASSWORD,
    [switch]$SelfSigned,
    [switch]$DryRun,
    [string[]]$Files
)

$ErrorActionPreference = 'Stop'
$rootDir = (Resolve-Path "$PSScriptRoot\..").Path

if (-not $Files -or $Files.Count -eq 0) {
    $Files = @(
        "$rootDir\build-msvc\bin\Release\duwn-mirror.exe",
        "$rootDir\build-msvc\bin\Release\duwn-virtualcam.dll",
        "$rootDir\installer\Duwn-Mirror-Setup-1.1.3-x64.exe"
    )
}

Write-Host "=== Duwn Mirror Digital Signing Tool ===" -ForegroundColor Cyan

# Locate signtool.exe from Windows SDK if available
$signtool = $null
$sdkPaths = @(
    "C:\Program Files (x86)\Windows Kits\10\bin\*\x64\signtool.exe",
    "C:\Program Files\Windows Kits\10\bin\*\x64\signtool.exe"
)
foreach ($p in $sdkPaths) {
    $found = Get-ChildItem -Path $p -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1
    if ($found) {
        $signtool = $found.FullName
        break
    }
}

if ($signtool) {
    Write-Host "Found signtool: $signtool" -ForegroundColor Gray
} else {
    Write-Host "signtool.exe not found in Windows Kits; falling back to Set-AuthenticodeSignature cmdlet" -ForegroundColor Gray
}

# Resolve Certificate
$cert = $null
if ($CertPath -and (Test-Path $CertPath)) {
    Write-Host "Using certificate from: $CertPath" -ForegroundColor Green
    if ($CertPassword) {
        $securePass = ConvertTo-SecureString -String $CertPassword -AsPlainText -Force
        $cert = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2($CertPath, $securePass)
    } else {
        $cert = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2($CertPath)
    }
} elseif ($SelfSigned) {
    Write-Host "Self-signed certificate requested..." -ForegroundColor Yellow
    $cert = Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert | Where-Object { $_.Subject -like "*Duwn Mirror Development*" } | Select-Object -First 1
    if (-not $cert) {
        Write-Host "Creating new local self-signed development certificate..." -ForegroundColor Yellow
        $cert = New-SelfSignedCertificate -Subject "CN=Duwn Mirror Development" -Type CodeSigningCert -CertStoreLocation Cert:\CurrentUser\My -NotAfter (Get-Date).AddYears(1)
        Write-Host "Created certificate: $($cert.Thumbprint)" -ForegroundColor Green
    } else {
        Write-Host "Found existing self-signed certificate: $($cert.Thumbprint)" -ForegroundColor Green
    }
} else {
    Write-Warning "No certificate provided. Set CERT_PATH and CERT_PASSWORD, or pass -SelfSigned."
}

$timestampServer = "http://timestamp.digicert.com"

# Process each file
foreach ($file in $Files) {
    if (-not (Test-Path $file)) {
        Write-Host " [SKIP] File not found: $file" -ForegroundColor Gray
        continue
    }

    $fileName = Split-Path $file -Leaf
    Write-Host "`nTarget: $fileName" -ForegroundColor White

    if ($DryRun) {
        Write-Host " [DRY-RUN] Would sign $file" -ForegroundColor Cyan
        if ($cert) {
            Write-Host " [DRY-RUN] Signer: $($cert.Subject) ($($cert.Thumbprint))" -ForegroundColor Cyan
            Write-Host " [DRY-RUN] Timestamp: $timestampServer" -ForegroundColor Cyan
        } else {
            Write-Host " [DRY-RUN] No certificate available for signing" -ForegroundColor Yellow
        }
        continue
    }

    if (-not $cert) {
        Write-Warning " [UNSIGNED] Skipping $fileName (no certificate available)"
        continue
    }

    try {
        if ($signtool -and $CertPath) {
            $signArgs = @("sign", "/fd", "SHA256", "/tr", $timestampServer, "/td", "SHA256", "/f", $CertPath)
            if ($CertPassword) {
                $signArgs += @("/p", $CertPassword)
            }
            $signArgs += $file
            & $signtool @signArgs
            if ($LASTEXITCODE -eq 0) {
                Write-Host " [SIGNED] Successfully signed with signtool: $fileName" -ForegroundColor Green
            } else {
                Write-Error "signtool failed with code $LASTEXITCODE for $fileName"
            }
        } else {
            $sig = Set-AuthenticodeSignature -FilePath $file -Certificate $cert -TimestampServer $timestampServer -HashAlgorithm SHA256
            if ($sig.Status -eq "Valid") {
                Write-Host " [SIGNED] Successfully signed with Authenticode: $fileName" -ForegroundColor Green
            } else {
                Write-Warning " [SIGNED-WITH-STATUS] Signature status: $($sig.StatusMessage) for $fileName"
            }
        }
    } catch {
        Write-Error "Failed to sign $fileName : $_"
    }
}

Write-Host "`nSigning pass completed." -ForegroundColor Cyan
