#!/bin/bash
set -euo pipefail

# Cross-compile Windows headless binaries on Linux using MinGW-w64.
# Runs on linux/amd64 agent (Fedora container), replacing native Windows builds.
#
# Usage: scripts/build-windows-cross.sh <action>
#   actions: build-libmicrohttpd, build, package, upload

ACTION="${1:-}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

HOST=x86_64-w64-mingw32
SYSROOT="/usr/${HOST}/sys-root/mingw"
STRIP="${HOST}-strip"
OBJDUMP="${HOST}-objdump"

EMUS="x64sc x64dtv xscpu64 x128 xvic xplus4 xpet xcbm2 xcbm5x0 vsid"
TOOLS="c1541 tools/cartconv/cartconv tools/petcat/petcat"

# Windows system DLLs that should NOT be bundled
SYSTEM_DLLS="
    kernel32.dll user32.dll gdi32.dll advapi32.dll shell32.dll
    ws2_32.dll msvcrt.dll ole32.dll oleaut32.dll ntdll.dll
    comdlg32.dll comctl32.dll imm32.dll winmm.dll winspool.drv
    crypt32.dll shlwapi.dll setupapi.dll cfgmgr32.dll version.dll
    iphlpapi.dll dnsapi.dll secur32.dll bcrypt.dll ncrypt.dll
    wldap32.dll normaliz.dll rpcrt4.dll userenv.dll uxtheme.dll
    dwmapi.dll mswsock.dll psapi.dll powrprof.dll
"

is_system_dll() {
    local dll_lower
    dll_lower=$(echo "$1" | tr 'A-Z' 'a-z')
    # Check api-ms-* and ext-ms-* prefixes
    case "$dll_lower" in
        api-ms-*|ext-ms-*) return 0 ;;
    esac
    for sys_dll in $SYSTEM_DLLS; do
        if [ "$dll_lower" = "$sys_dll" ]; then
            return 0
        fi
    done
    return 1
}

# Recursively find and copy DLL dependencies
find_and_copy_dlls() {
    local binary="$1"
    local dest="$2"
    local dll dll_path
    for dll in $($OBJDUMP -p "$binary" 2>/dev/null | awk '/DLL Name:/{print $3}'); do
        if is_system_dll "$dll"; then
            continue
        fi
        # Already copied?
        if [ -f "$dest/$dll" ]; then
            continue
        fi
        dll_path="$SYSROOT/bin/$dll"
        if [ -f "$dll_path" ]; then
            echo "  Bundling DLL: $dll"
            cp "$dll_path" "$dest/"
            # Recurse into this DLL's dependencies
            find_and_copy_dlls "$dll_path" "$dest"
        else
            echo "  Warning: DLL not found in sysroot: $dll" >&2
        fi
    done
}

do_fix_timestamps() {
    cd "$REPO_ROOT/vice"
    rm -f src/config.h
    find . -name config.status -exec rm -f {} +
    ./src/buildtools/genvicedate_h.sh
    # Fix autotools timestamps: git clone gives all files same mtime,
    # causing make to think generated files need regeneration
    sleep 1 && find . -name aclocal.m4 -exec touch {} +
    sleep 1 && find . -name configure -exec touch {} + && find . -name config.h.in -exec touch {} +
    find . -name Makefile.in -exec touch {} +
}

case "$ACTION" in
    build-libmicrohttpd)
        # Cross-compile libmicrohttpd (not available as Fedora mingw64 package)
        echo "=== Cross-compiling libmicrohttpd ==="
        MHD_VERSION="1.0.1"
        MHD_DIR="/tmp/libmicrohttpd-build"
        mkdir -p "$MHD_DIR"
        cd "$MHD_DIR"
        if [ ! -f "libmicrohttpd-${MHD_VERSION}.tar.gz" ]; then
            curl -fSL "https://ftp.gnu.org/gnu/libmicrohttpd/libmicrohttpd-${MHD_VERSION}.tar.gz" \
                -o "libmicrohttpd-${MHD_VERSION}.tar.gz"
        fi
        tar xzf "libmicrohttpd-${MHD_VERSION}.tar.gz"
        cd "libmicrohttpd-${MHD_VERSION}"
        ./configure \
            --host="${HOST}" \
            --prefix="${SYSROOT}" \
            --disable-doc \
            --disable-examples \
            --disable-https
        make -j"$(nproc)" -s
        make install
        echo "=== libmicrohttpd installed to ${SYSROOT} ==="
        ;;

    build)
        echo "=== Cross-compiling VICE headless for Windows ==="
        do_fix_timestamps
        cd "$REPO_ROOT/vice"
        rm -rf build-headless
        mkdir -p build-headless && cd build-headless

        # Set pkg-config to find mingw64 packages
        export PKG_CONFIG_LIBDIR="${SYSROOT}/lib/pkgconfig:${SYSROOT}/share/pkgconfig"
        export PKG_CONFIG_SYSROOT_DIR="/"

        ../configure \
            --host="${HOST}" \
            --enable-option-checking=fatal \
            --enable-headlessui \
            --enable-mcp-server \
            --enable-cpuhistory \
            --disable-arch \
            --disable-pdf-docs \
            --disable-html-docs \
            --with-flac \
            --with-gif \
            --with-libcurl \
            --with-png \
            --with-portaudio \
            --with-resid \
            --with-vorbis \
            --without-lame \
            --without-mpg123 \
            --without-libieee1284 \
            --without-pulse \
            --without-alsa
        make -j"$(nproc)" -s
        echo "=== Build complete ==="
        ;;

    package)
        echo "=== Packaging Windows headless distribution ==="
        cd "$REPO_ROOT"
        git fetch --tags
        VERSION=$(bash "$REPO_ROOT/scripts/compute-version.sh" --current)
        echo "Version: $VERSION"

        BUILD_DIR="$REPO_ROOT/vice/build-headless"
        SRC_DIR="$REPO_ROOT/vice"
        DIST_NAME="HeadlessVICE-windows-x86_64"
        DIST_DIR="$BUILD_DIR/$DIST_NAME"
        rm -rf "$DIST_DIR"
        mkdir -p "$DIST_DIR"

        # Copy emulator binaries
        echo "--- Copying binaries ---"
        for emu in $EMUS; do
            binary="$BUILD_DIR/src/${emu}.exe"
            if [ -f "$binary" ]; then
                echo "  $emu.exe"
                cp "$binary" "$DIST_DIR/"
            else
                echo "  Warning: $emu.exe not found" >&2
            fi
        done

        # Copy tools
        for tool in $TOOLS; do
            binary="$BUILD_DIR/src/${tool}.exe"
            toolname=$(basename "$tool")
            if [ -f "$binary" ]; then
                echo "  $toolname.exe"
                cp "$binary" "$DIST_DIR/"
            else
                echo "  Warning: $toolname.exe not found" >&2
            fi
        done

        # Strip binaries
        echo "--- Stripping binaries ---"
        find "$DIST_DIR" -name '*.exe' -exec $STRIP {} +

        # Find and copy DLL dependencies
        echo "--- Collecting DLL dependencies ---"
        for exe in "$DIST_DIR"/*.exe; do
            find_and_copy_dlls "$exe" "$DIST_DIR"
        done

        # Copy data files (ROMs, palettes, etc.) — same logic as VICE's own bindist
        echo "--- Copying data files ---"
        cp -R "$SRC_DIR/data" "$DIST_DIR/data"
        # Remove build/source artifacts from data
        find "$DIST_DIR/data" -type f \
            \( -name 'Makefile*' -o -name '*.vhk' -o -name '*.vjk' -o \
               -name '*.vjm' -o -name '*.vkm' -o -name '*.rc' -o -name '*.png' -o \
               -name '*.svg' -o -name '*.xml' -o -name '*.ttf' \) \
            -exec rm {} \;
        rm -rf "$DIST_DIR/data/GLSL"
        # Move data subdirectories up one level (VICE convention)
        mv "$DIST_DIR/data/"* "$DIST_DIR/" 2>/dev/null || true
        rmdir "$DIST_DIR/data" 2>/dev/null || true

        # Copy documentation
        echo "--- Copying documentation ---"
        mkdir -p "$DIST_DIR/doc"
        for doc in COPYING NEWS README; do
            [ -f "$SRC_DIR/$doc" ] && cp "$SRC_DIR/$doc" "$DIST_DIR/doc/"
        done

        # Generate checksums of all files in the distribution
        echo "--- Generating checksums ---"
        cd "$DIST_DIR"
        find . -type f -exec sha256sum {} + > SHA256SUMS
        cd "$BUILD_DIR"

        # Create zip
        echo "--- Creating archive ---"
        zip -r -9 -q "$REPO_ROOT/${VERSION}-windows-x86_64-headless.zip" "$DIST_NAME/"
        cd "$REPO_ROOT"
        echo "Packaged: ${VERSION}-windows-x86_64-headless.zip"
        ls -lh "${VERSION}-windows-x86_64-headless.zip"
        ;;

    upload)
        cd "$REPO_ROOT"
        git fetch --tags
        VERSION=$(bash scripts/compute-version.sh)
        for artifact in "${VERSION}-windows-x86_64"-*.zip; do
            if [ -f "$artifact" ]; then
                gh release upload "$VERSION" "$artifact" --repo "${CI_REPO}" --clobber
                echo "Uploaded: $artifact"
            fi
        done
        ;;

    *)
        echo "Usage: $0 {build-libmicrohttpd|build|package|upload}" >&2
        exit 1
        ;;
esac
