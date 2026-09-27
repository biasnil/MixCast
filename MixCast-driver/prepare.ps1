<#
.SYNOPSIS
    Builds the MixCast driver source tree: Microsoft's SimpleAudioSample + the MixCast overlay.

.DESCRIPTION
    Result goes to .\driver next to this script. Run once; re-run to reset
    (it deletes .\driver first).

.PARAMETER SamplesRoot
    Path to an existing clone of https://github.com/microsoft/Windows-driver-samples.
    If omitted, the script does a sparse git clone of just audio\simpleaudiosample.

.EXAMPLE
    .\prepare.ps1
    .\prepare.ps1 -SamplesRoot C:\src\Windows-driver-samples
#>
param([string]$SamplesRoot)

$ErrorActionPreference = 'Stop'
$here    = $PSScriptRoot
$dest    = Join-Path $here 'driver'
$overlay = Join-Path $here 'overlay'

if (-not $SamplesRoot) {
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
        throw "git not found. Install Git or pass -SamplesRoot to an existing Windows-driver-samples clone."
    }
    $SamplesRoot = Join-Path $env:TEMP 'wds-mixcast'
    if (Test-Path $SamplesRoot) { Remove-Item $SamplesRoot -Recurse -Force }
    Write-Host "Cloning Microsoft Windows-driver-samples (sparse)..."
    git clone --depth 1 --filter=blob:none --sparse https://github.com/microsoft/Windows-driver-samples.git $SamplesRoot
    git -C $SamplesRoot sparse-checkout set audio/simpleaudiosample
}

$sample = Join-Path $SamplesRoot 'audio\simpleaudiosample'
if (-not (Test-Path $sample)) { throw "Not found: $sample" }

if (Test-Path $dest) { Remove-Item $dest -Recurse -Force }
Write-Host "Copying sample -> $dest"
Copy-Item $sample $dest -Recurse

# The overlay ships MixCast.inx; drop the original INF so only one gets built.
Remove-Item (Join-Path $dest 'Source\Main\SimpleAudioSample.inx') -Force

Write-Host "Applying MixCast overlay"
Copy-Item (Join-Path $overlay '*') $dest -Recurse -Force

Write-Host ""
Write-Host "Done. Open driver\SimpleAudioSample.sln in Visual Studio, select x64 / Debug, and build the 'package' project." -ForegroundColor Green
