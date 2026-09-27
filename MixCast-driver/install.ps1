#Requires -RunAsAdministrator
<#
.SYNOPSIS
    Installs (or updates) the test-signed MixCast driver.

.PARAMETER Configuration
    Debug or Release (default Debug).
#>
param([ValidateSet('Debug','Release')][string]$Configuration = 'Debug')

$ErrorActionPreference = 'Stop'
$hwid = 'ROOT\MixCast'

# 1) Test signing must be on for a test-signed kernel driver to load.
$bcd = bcdedit /enum '{current}' | Out-String
if ($bcd -notmatch 'testsigning\s+Yes') {
    Write-Warning "Test signing is OFF. Run:  bcdedit /set testsigning on   then reboot."
    Write-Warning "(Secure Boot must be disabled in firmware for test signing to work.)"
    exit 1
}

# 2) Locate the built package.
$pkgDir = Join-Path $PSScriptRoot "driver\x64\$Configuration\package"
$inf = Join-Path $pkgDir 'MixCast.inf'
if (-not (Test-Path $inf)) {
    $found = Get-ChildItem (Join-Path $PSScriptRoot 'driver') -Recurse -Filter 'MixCast.inf' -ErrorAction SilentlyContinue |
             Where-Object { $_.DirectoryName -like "*$Configuration*package*" } |
             Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $found) { throw "MixCast.inf not found. Build the 'package' project ($Configuration|x64) first." }
    $inf = $found.FullName; $pkgDir = $found.DirectoryName
}
Write-Host "Package: $pkgDir"

# 3) Trust the WDK test certificate (Root + TrustedPublisher).
$cer = Get-ChildItem $pkgDir -Filter '*.cer' | Select-Object -First 1
if (-not $cer) {
    $cer = Get-ChildItem (Split-Path $pkgDir) -Filter '*.cer' -Recurse | Select-Object -First 1
}
if ($cer) {
    Write-Host "Trusting test certificate: $($cer.Name)"
    Import-Certificate -FilePath $cer.FullName -CertStoreLocation Cert:\LocalMachine\Root | Out-Null
    Import-Certificate -FilePath $cer.FullName -CertStoreLocation Cert:\LocalMachine\TrustedPublisher | Out-Null
} else {
    Write-Warning "No .cer found next to the package; install may prompt or fail signature checks."
}

# 4) Find devcon (ships with the WDK).
$devcon = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\Tools" -Recurse -Filter devcon.exe -ErrorAction SilentlyContinue |
          Where-Object { $_.DirectoryName -match '\\x64$' } |
          Sort-Object FullName -Descending | Select-Object -First 1
if (-not $devcon) { throw "devcon.exe not found. Install the Windows Driver Kit (WDK)." }
$devcon = $devcon.FullName

# 5) Install a new root-enumerated device, or update the existing one.
$existing = & $devcon hwids $hwid | Out-String
if ($existing -match 'MixCast') {
    Write-Host "Updating existing MixCast device..."
    & $devcon update $inf $hwid
} else {
    Write-Host "Installing MixCast device..."
    & $devcon install $inf $hwid
}

if ($LASTEXITCODE -ne 0) { throw "devcon failed with exit code $LASTEXITCODE" }
Write-Host ""
Write-Host "Installed. Check Sound settings for 'MixCast Input' (playback) and 'MixCast Mic' (recording)." -ForegroundColor Green
