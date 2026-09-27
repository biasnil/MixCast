<#
.SYNOPSIS
    Builds MixCast and packages it for release: a portable zip and (if Inno Setup
    is installed) a Windows installer.

.DESCRIPTION
    Run from Visual Studio's "Developer PowerShell" (2022 or 2026):

        cd MixCast\release
        .\package.ps1 -QtDir C:\Qt\6.8.0\msvc2022_64

    Output goes to release\dist\:
        MixCast-<version>\                     the app folder (what's inside the zip)
        MixCast-<version>-win64-portable.zip   unzip-and-run version
        MixCast-<version>-Setup.exe            installer (needs Inno Setup 6)

.PARAMETER QtDir
    Your Qt kit folder, e.g. C:\Qt\6.8.0\msvc2022_64. Only needed the first time
    (after that it's read from the build folder).

.PARAMETER SkipBuild
    Package the existing Release build without rebuilding.

.PARAMETER NoInstaller
    Only make the zip.
#>
param(
    [string]$QtDir,
    [switch]$SkipBuild,
    [switch]$NoInstaller
)

$ErrorActionPreference = 'Stop'

$root    = Split-Path $PSScriptRoot -Parent          # ...\MixCast
$engine  = Join-Path $root 'engine'
$build   = Join-Path $engine 'build'
$release = $PSScriptRoot

function Step($text) { Write-Host ""; Write-Host "==> $text" -ForegroundColor Cyan }
function Fail($text) { Write-Host ""; Write-Host "ERROR: $text" -ForegroundColor Red; exit 1 }

# ---------------------------------------------------------------------------
# Version (from engine\CMakeLists.txt)
# ---------------------------------------------------------------------------
$cmakeLists = Get-Content (Join-Path $engine 'CMakeLists.txt') -Raw
if ($cmakeLists -notmatch 'project\(MixCast VERSION ([0-9]+\.[0-9]+\.[0-9]+)') { Fail "Couldn't read the version from engine\CMakeLists.txt" }
$version = $Matches[1]
Write-Host "MixCast $version" -ForegroundColor Green

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Fail "cmake not found. Run this from the 'Developer PowerShell' that comes with Visual Studio."
}

# Pick the CMake generator for the Visual Studio you actually have
# (2026 = "Visual Studio 18 2026", 2022 = "Visual Studio 17 2022").
function Get-Generator {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vsVersion = if (Test-Path $vswhere) { & $vswhere -latest -products * -property installationVersion } else { $null }
    $major = if ($vsVersion) { [int]($vsVersion.Split('.')[0]) } else { 0 }
    $known = (cmake --help) -join "`n"
    $names = @{ 18 = 'Visual Studio 18 2026'; 17 = 'Visual Studio 17 2022' }
    if ($names.ContainsKey($major) -and $known -match [regex]::Escape($names[$major])) { return $names[$major] }
    # This CMake doesn't know your Visual Studio yet: Ninja works from any Developer PowerShell.
    if (Get-Command ninja -ErrorAction SilentlyContinue) { return 'Ninja' }
    Fail "Your CMake doesn't support Visual Studio $vsVersion and Ninja wasn't found. Update CMake from https://cmake.org/download/"
}

if (-not $SkipBuild) {
    $generator = Get-Generator
    $cacheFile = Join-Path $build 'CMakeCache.txt'

    # A build folder from a failed or different setup can't be reused: start fresh.
    if (Test-Path $cacheFile) {
        $cached = (Select-String -Path $cacheFile -Pattern '^CMAKE_GENERATOR:INTERNAL=(.+)$' | Select-Object -First 1)
        $cachedGen = if ($cached) { $cached.Matches[0].Groups[1].Value } else { '' }
        if ($cachedGen -ne $generator -or -not (Test-Path (Join-Path $build 'CMakeFiles'))) {
            Write-Host "Old build folder is for '$cachedGen'; recreating it for '$generator'." -ForegroundColor Yellow
            Remove-Item $build -Recurse -Force
        }
    }

    if (-not (Test-Path $cacheFile)) {
        if (-not $QtDir) { Fail "First build: pass -QtDir, e.g. -QtDir C:\Qt\6.8.0\msvc2022_64" }
        if (-not (Test-Path (Join-Path $QtDir 'lib\cmake\Qt6'))) {
            $found = Get-ChildItem 'C:\Qt' -Directory -ErrorAction SilentlyContinue |
                     ForEach-Object { Get-ChildItem $_.FullName -Directory -Filter 'msvc*_64' -ErrorAction SilentlyContinue } |
                     ForEach-Object { $_.FullName }
            $hint = if ($found) { "Found these Qt kits:`n  " + ($found -join "`n  ") } else { "No Qt kits found under C:\Qt." }
            if (-not $found -and (Test-Path 'C:\msys64\ucrt64\lib\cmake\Qt6')) {
                $hint += "`nYou have Qt from MSYS2 instead. It can't be used with Visual Studio, but release\package-msys2.sh"
                $hint += "`npackages MixCast with it: open 'MSYS2 UCRT64' and run ./package-msys2.sh (see release\TESTING.md)."
            } elseif (-not $found) {
                $hint += "`nInstall Qt's 'MSVC 2022 64-bit' kit with the Qt Online Installer (see release\TESTING.md)."
            }
            Fail "-QtDir '$QtDir' isn't a Qt kit (no lib\cmake\Qt6 inside).`n$hint"
        }

        Step "Configuring with '$generator' (first time)"
        if ($generator -eq 'Ninja') {
            cmake -S $engine -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$QtDir"
        } else {
            cmake -S $engine -B $build -G $generator -A x64 "-DCMAKE_PREFIX_PATH=$QtDir"
        }
        if ($LASTEXITCODE -ne 0) {
            Remove-Item $build -Recurse -Force -ErrorAction SilentlyContinue   # so the next run starts clean
            Fail "CMake configure failed. Scroll up for the first error."
        }
    }

    Step "Building Release"
    cmake --build $build --config Release
    if ($LASTEXITCODE -ne 0) { Fail "Build failed. Scroll up for the first error." }
}

# Visual Studio builds go to build\Release, Ninja builds to build\.
$bin = if (Test-Path (Join-Path $build 'Release\mixcast.exe')) { Join-Path $build 'Release' } else { $build }
foreach ($exe in 'mixcast.exe', 'mixcast-cli.exe') {
    if (-not (Test-Path (Join-Path $bin $exe))) { Fail "$exe not found in $bin. Build first (or drop -SkipBuild)." }
}

# ---------------------------------------------------------------------------
# Find Qt's windeployqt
# ---------------------------------------------------------------------------
if (-not $QtDir) {
    $cache = Get-Content (Join-Path $build 'CMakeCache.txt')
    $qtLine = $cache | Where-Object { $_ -match '^Qt6_DIR:PATH=(.+)$' } | Select-Object -First 1
    if ($qtLine -and $qtLine -match '^Qt6_DIR:PATH=(.+)$') {
        $QtDir = (Resolve-Path (Join-Path $Matches[1] '..\..\..')).Path   # ...\lib\cmake\Qt6 -> kit root
    }
}
$windeployqt = if ($QtDir) { Join-Path $QtDir 'bin\windeployqt.exe' } else { $null }
if (-not $windeployqt -or -not (Test-Path $windeployqt)) { Fail "windeployqt.exe not found. Pass -QtDir C:\Qt\<version>\msvc2022_64" }

# ---------------------------------------------------------------------------
# Assemble the app folder
# ---------------------------------------------------------------------------
$distRoot = Join-Path $release 'dist'
$app      = Join-Path $distRoot "MixCast-$version"
Step "Assembling $app"
if (Test-Path $app) { Remove-Item $app -Recurse -Force }
New-Item -ItemType Directory -Path $app | Out-Null

Copy-Item (Join-Path $bin 'mixcast.exe')     $app
Copy-Item (Join-Path $bin 'mixcast-cli.exe') $app

Step "Adding Qt files (windeployqt)"
& $windeployqt --release --no-translations --no-system-d3d-compiler --no-opengl-sw --no-compiler-runtime `
               (Join-Path $app 'mixcast.exe') | Out-Null
if ($LASTEXITCODE -ne 0) { Fail "windeployqt failed." }

# ---------------------------------------------------------------------------
# Microsoft C++ runtime, app-local (so users don't need the VC++ Redistributable)
# ---------------------------------------------------------------------------
Step "Adding the Microsoft C++ runtime"
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { Fail "vswhere.exe not found; is Visual Studio 2022 installed?" }
$vs = & $vswhere -latest -products * -property installationPath
$redistRoot = Join-Path $vs 'VC\Redist\MSVC'
$crt = Get-ChildItem $redistRoot -Directory |
       Sort-Object { try { [version]$_.Name } catch { [version]'0.0' } } -Descending |
       ForEach-Object { Get-ChildItem (Join-Path $_.FullName 'x64') -Directory -Filter 'Microsoft.VC14*.CRT' -ErrorAction SilentlyContinue } |
       Select-Object -First 1
if (-not $crt) { Fail "Couldn't find Microsoft.VC14x.CRT under $redistRoot" }
Copy-Item (Join-Path $crt.FullName '*.dll') $app
Write-Host "   from $($crt.FullName)"

# ---------------------------------------------------------------------------
# Docs and licences
# ---------------------------------------------------------------------------
Step "Adding README, licences and notices"
Copy-Item (Join-Path $root 'README.md')               $app
Copy-Item (Join-Path $root 'LICENSE')                 (Join-Path $app 'LICENSE.txt')
Copy-Item (Join-Path $root 'THIRD-PARTY-NOTICES.txt') $app
Copy-Item (Join-Path $root 'CHANGELOG.md')            $app

# Qt is LGPLv3: its licence texts must ship with the app. Downloaded once from
# gnu.org and cached in release\licenses\.
$licCache = Join-Path $release 'licenses'
New-Item -ItemType Directory -Path $licCache -Force | Out-Null
$licenses = @{
    'LGPL-3.0.txt' = 'https://www.gnu.org/licenses/lgpl-3.0.txt'
    'GPL-3.0.txt'  = 'https://www.gnu.org/licenses/gpl-3.0.txt'
}
foreach ($name in $licenses.Keys) {
    $file = Join-Path $licCache $name
    if (-not (Test-Path $file)) {
        try   { Invoke-WebRequest $licenses[$name] -OutFile $file -UseBasicParsing }
        catch { Fail "Couldn't download $name. Connect to the internet once, or save $($licenses[$name]) as $file" }
    }
}
$appLic = Join-Path $app 'licenses'
New-Item -ItemType Directory -Path $appLic | Out-Null
Copy-Item (Join-Path $licCache '*.txt') $appLic

# Tidy: nothing the app doesn't need.
Get-ChildItem $app -Recurse -Include 'vc_redist*.exe', '*.pdb', '*.ilk' | Remove-Item -Force

# ---------------------------------------------------------------------------
# Portable zip
# ---------------------------------------------------------------------------
$zip = Join-Path $distRoot "MixCast-$version-win64-portable.zip"
Step "Zipping $zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path $app -DestinationPath $zip

# ---------------------------------------------------------------------------
# Installer (Inno Setup 6)
# ---------------------------------------------------------------------------
$setup = $null
if (-not $NoInstaller) {
    $iscc = @(
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
    ) | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $iscc) { $iscc = (Get-Command ISCC.exe -ErrorAction SilentlyContinue).Source }

    if ($iscc) {
        Step "Building the installer (Inno Setup)"
        & $iscc /Q "/DAppVersion=$version" "/DSourceDir=$app" "/DOutputDir=$distRoot" "/DRootDir=$root" `
                (Join-Path $release 'MixCast.iss')
        if ($LASTEXITCODE -ne 0) { Fail "Inno Setup failed." }
        $setup = Join-Path $distRoot "MixCast-$version-Setup.exe"
    }
    else {
        Write-Host ""
        Write-Host "Inno Setup 6 not found, so no installer was made (the zip is ready)." -ForegroundColor Yellow
        Write-Host "Get it free from https://jrsoftware.org/isdl.php and run this script again."
    }
}

# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "Done." -ForegroundColor Green
$size = "{0:N1} MB" -f ((Get-ChildItem $app -Recurse | Measure-Object Length -Sum).Sum / 1MB)
Write-Host "  App folder : $app ($size)"
Write-Host "  Portable   : $zip"
if ($setup) { Write-Host "  Installer  : $setup" }
Write-Host ""
Write-Host "Next: test it on your VM (release\TESTING.md)."
