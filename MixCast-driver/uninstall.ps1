#Requires -RunAsAdministrator
<#
.SYNOPSIS
    Removes the MixCast device and deletes its driver package from the driver store.
#>
$ErrorActionPreference = 'Continue'
$hwid = 'ROOT\MixCast'

$devcon = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\Tools" -Recurse -Filter devcon.exe -ErrorAction SilentlyContinue |
          Where-Object { $_.DirectoryName -match '\\x64$' } |
          Sort-Object FullName -Descending | Select-Object -First 1

if ($devcon) {
    Write-Host "Removing device $hwid..."
    & $devcon.FullName remove $hwid
} else {
    Write-Warning "devcon.exe not found; remove the device manually in Device Manager (Sound, video and game controllers)."
}

Write-Host "Removing driver package(s) from the driver store..."
$pkgs = Get-WindowsDriver -Online -All | Where-Object { $_.OriginalFileName -like '*\mixcast.inf' }
foreach ($p in $pkgs) {
    Write-Host "  pnputil /delete-driver $($p.Driver)"
    pnputil /delete-driver $p.Driver /uninstall /force | Out-Null
}
if (-not $pkgs) { Write-Host "  (none found)" }

Write-Host "Done." -ForegroundColor Green
