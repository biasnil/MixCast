#!/usr/bin/env bash
# MixCast release packager for MSYS2 (Qt + GCC from MSYS2 instead of Visual Studio).
#
# Run in the "MSYS2 UCRT64" terminal:
#     cd "/c/Users/<you>/Documents/C++ Project/MixCast/release"
#     ./package-msys2.sh
#
# First time only, install the tools:
#     pacman -S --needed mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,qt6-base,qt6-tools}
#
# Output in release/dist/ (same as package.ps1):
#     MixCast-<version>/                     app folder
#     MixCast-<version>-win64-portable.zip   portable zip
#     MixCast-<version>-Setup.exe            installer (if Inno Setup 6 is installed)
#
# Options:  --skip-build   package the existing build
#           --no-installer only make the zip
set -euo pipefail

SKIP_BUILD=0; NO_INSTALLER=0
for a in "$@"; do
    case "$a" in
        --skip-build)   SKIP_BUILD=1 ;;
        --no-installer) NO_INSTALLER=1 ;;
        *) echo "Unknown option: $a"; exit 1 ;;
    esac
done

step() { printf '\n\033[36m==> %s\033[0m\n' "$1"; }
fail() { printf '\n\033[31mERROR: %s\033[0m\n' "$1"; exit 1; }

# ---------------------------------------------------------------------------
# Environment
# ---------------------------------------------------------------------------
case "${MSYSTEM:-}" in
    UCRT64|MINGW64|CLANG64) ;;
    *) fail "Open the 'MSYS2 UCRT64' terminal (Start menu > MSYS2 > MSYS2 UCRT64) and run this there." ;;
esac
PREFIX="${MINGW_PREFIX:-/ucrt64}"               # /ucrt64
PKG_PREFIX="${MINGW_PACKAGE_PREFIX:-mingw-w64-ucrt-x86_64}"

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/.." && pwd)"
engine="$root/engine"
build="$engine/build-msys2"                      # separate from the Visual Studio build
dist_root="$here/dist"

version="$(sed -n 's/^project(MixCast VERSION \([0-9.]*\).*/\1/p' "$engine/CMakeLists.txt")"
[ -n "$version" ] || fail "Couldn't read the version from engine/CMakeLists.txt"
printf '\033[32mMixCast %s (MSYS2 %s)\033[0m\n' "$version" "$MSYSTEM"

missing=()
for t in cmake ninja g++; do command -v "$t" >/dev/null || missing+=("$t"); done
[ -d "$PREFIX/lib/cmake/Qt6" ] || missing+=("qt6-base")
if [ ${#missing[@]} -gt 0 ]; then
    fail "Missing: ${missing[*]}
Install with:
  pacman -S --needed ${PKG_PREFIX}-{gcc,cmake,ninja,qt6-base,qt6-tools}"
fi

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
if [ "$SKIP_BUILD" = 0 ]; then
    if [ ! -f "$build/CMakeCache.txt" ]; then
        step "Configuring (first time)"
        cmake -S "$engine" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
            || { rm -rf "$build"; fail "CMake configure failed. Scroll up for the first error."; }
    fi
    step "Building Release"
    cmake --build "$build" || fail "Build failed. Scroll up for the first error."
fi

for exe in mixcast.exe mixcast-cli.exe; do
    [ -f "$build/$exe" ] || fail "$exe not found in $build. Build first (or drop --skip-build)."
done

# ---------------------------------------------------------------------------
# Assemble
# ---------------------------------------------------------------------------
app="$dist_root/MixCast-$version"
step "Assembling $app"
rm -rf "$app"
mkdir -p "$app"
cp "$build/mixcast.exe" "$build/mixcast-cli.exe" "$app/"

step "Adding Qt plugins"
wdq=""
for n in windeployqt6 windeployqt-qt6 windeployqt; do
    if command -v "$n" >/dev/null; then wdq="$n"; break; fi
done
if [ -n "$wdq" ]; then
    # Skip plugin types MixCast doesn't use (touch input, network, TLS): they
    # would drag in Qt6Network, GLib and friends for nothing.
    "$wdq" --release --no-translations --no-system-d3d-compiler --no-opengl-sw --no-compiler-runtime \
           --skip-plugin-types generic,networkinformation,tls \
           "$app/mixcast.exe" >/dev/null 2>&1 \
    || "$wdq" --release --no-translations --no-system-d3d-compiler --no-opengl-sw --no-compiler-runtime \
           "$app/mixcast.exe" >/dev/null 2>&1 \
    || echo "   (windeployqt reported a problem; copying plugins by hand as well)"
fi
# Make sure the essentials are there even if windeployqt isn't installed.
plugins="$PREFIX/share/qt6/plugins"
for p in platforms/qwindows.dll styles/qmodernwindowsstyle.dll styles/qwindowsvistastyle.dll \
         imageformats/qico.dll imageformats/qpng.dll; do
    if [ -f "$plugins/$p" ] && [ ! -f "$app/$p" ]; then
        mkdir -p "$app/$(dirname "$p")"
        cp "$plugins/$p" "$app/$p"
    fi
done
[ -f "$app/platforms/qwindows.dll" ] || fail "Qt's Windows platform plugin not found (install ${PKG_PREFIX}-qt6-base)."

# ---------------------------------------------------------------------------
# Every DLL the app needs from MSYS2 (Qt, GCC runtime, and their libraries).
# Repeats until nothing new turns up, so dependencies of dependencies come too.
# ---------------------------------------------------------------------------
step "Adding MSYS2 runtime DLLs"
while :; do
    added=0
    while IFS= read -r dll; do
        base="$(basename "$dll")"
        if [ ! -f "$app/$base" ]; then
            cp "$dll" "$app/"
            added=1
        fi
    done < <(find "$app" \( -name '*.exe' -o -name '*.dll' \) -print0 \
               | xargs -0 -n1 ldd 2>/dev/null \
               | awk -v p="$PREFIX/" 'index($3, p) == 1 { print $3 }' | sort -u)
    [ "$added" = 0 ] && break
done
echo "   $(find "$app" -maxdepth 1 -name '*.dll' | wc -l) DLLs"

# ---------------------------------------------------------------------------
# Docs and licences
# ---------------------------------------------------------------------------
step "Adding README, licences and notices"
cp "$root/README.md" "$root/THIRD-PARTY-NOTICES.txt" "$root/CHANGELOG.md" "$app/"
cp "$root/LICENSE" "$app/LICENSE.txt"

lic_cache="$here/licenses"
mkdir -p "$lic_cache" "$app/licenses"
for pair in "LGPL-3.0.txt https://www.gnu.org/licenses/lgpl-3.0.txt" \
            "GPL-3.0.txt https://www.gnu.org/licenses/gpl-3.0.txt"; do
    set -- $pair
    [ -f "$lic_cache/$1" ] || curl -fsSL "$2" -o "$lic_cache/$1" \
        || fail "Couldn't download $1. Connect to the internet once, or save $2 as $lic_cache/$1"
    cp "$lic_cache/$1" "$app/licenses/"
done

# Each bundled MSYS2 library brings its own licence: copy them from the
# packages they came from (share/licenses/<name>/).
mkdir -p "$app/licenses/msys2"
{
    echo "DLLs bundled from MSYS2 packages (licence texts in the folders next to this file):"
    echo
} > "$app/licenses/msys2/PACKAGES.txt"
for dll in "$app"/*.dll "$app"/*/*.dll; do
    src="$PREFIX/bin/$(basename "$dll")"
    [ -f "$src" ] || src="$plugins/${dll#$app/}"
    pkg="$(pacman -Qqo "$src" 2>/dev/null || true)"
    [ -n "$pkg" ] || continue
    echo "$(basename "$dll")  <-  $pkg" >> "$app/licenses/msys2/PACKAGES.txt"
    name="${pkg#${PKG_PREFIX}-}"
    if [ -d "$PREFIX/share/licenses/$name" ] && [ ! -d "$app/licenses/msys2/$name" ]; then
        cp -r "$PREFIX/share/licenses/$name" "$app/licenses/msys2/$name"
    fi
done

# ---------------------------------------------------------------------------
# Portable zip (Windows' own zip tool, via PowerShell)
# ---------------------------------------------------------------------------
zip="$dist_root/MixCast-$version-win64-portable.zip"
step "Zipping $zip"
rm -f "$zip"
powershell.exe -NoProfile -Command \
    "Compress-Archive -Path '$(cygpath -w "$app")' -DestinationPath '$(cygpath -w "$zip")'" \
    || fail "Zipping failed."

# ---------------------------------------------------------------------------
# Installer (Inno Setup 6)
# ---------------------------------------------------------------------------
setup=""
if [ "$NO_INSTALLER" = 0 ]; then
    iscc=""
    for c in "/c/Program Files (x86)/Inno Setup 6/ISCC.exe" \
             "/c/Program Files/Inno Setup 6/ISCC.exe" \
             "$(cygpath -u "${LOCALAPPDATA:-C:\\}")/Programs/Inno Setup 6/ISCC.exe"; do
        [ -f "$c" ] && { iscc="$c"; break; }
    done
    if [ -n "$iscc" ]; then
        step "Building the installer (Inno Setup)"
        # MSYS2 rewrites arguments that start with "/" into paths ("/Q" -> "C:/msys64/Q"),
        # which ISCC then reads as extra script names. Switch that off for this call.
        MSYS2_ARG_CONV_EXCL='*' \
        "$iscc" /Q "/DAppVersion=$version" "/DSourceDir=$(cygpath -w "$app")" \
                "/DOutputDir=$(cygpath -w "$dist_root")" "/DRootDir=$(cygpath -w "$root")" \
                "$(cygpath -w "$here/MixCast.iss")" || fail "Inno Setup failed."
        setup="$dist_root/MixCast-$version-Setup.exe"
    else
        printf '\n\033[33mInno Setup 6 not found, so no installer was made (the zip is ready).\033[0m\n'
        echo "Get it free from https://jrsoftware.org/isdl.php and run this script again."
    fi
fi

# ---------------------------------------------------------------------------
size="$(du -sm "$app" | cut -f1)"
printf '\n\033[32mDone.\033[0m\n'
echo "  App folder : $(cygpath -w "$app") (${size} MB)"
echo "  Portable   : $(cygpath -w "$zip")"
[ -n "$setup" ] && echo "  Installer  : $(cygpath -w "$setup")"
echo
echo "Next: test it on your VM (release/TESTING.md)."