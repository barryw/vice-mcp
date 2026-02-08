#!/bin/bash
set -euo pipefail

# Windows build script for MSYS2/MINGW64 environment.
# Called from Woodpecker CI with MSYSTEM=MINGW64 already set.
#
# Usage: scripts/build-windows-msys2.sh <action>
#   actions: build-libieee1284, build-gui, package-gui,
#            build-headless, package-headless, upload
#
# Legacy aliases: build -> build-gui, package -> package-gui

ACTION="${1:-}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

COMMON_CONFIGURE_FLAGS=(
    --enable-option-checking=fatal
    --enable-mcp-server
    --enable-cpuhistory
    --enable-ethernet
    --enable-midi
    --enable-parsid
    --enable-catweasel
    --disable-arch
    --disable-pdf-docs
    --disable-html-docs
    --with-flac
    --with-gif
    --with-lame
    --with-libcurl
    --with-libieee1284
    --with-mpg123
    --with-png
    --with-portaudio
    --with-resid
    --with-vorbis
)

do_autogen() {
    cd "$REPO_ROOT/vice"
    rm -f src/config.h
    find . -name config.status -exec rm -f {} +
    ./src/buildtools/genvicedate_h.sh
    ./autogen.sh
}

case "$ACTION" in
    build-libieee1284)
        mkdir -p /tmp/work
        cd /tmp/work
        if [ ! -d libieee1284 ]; then
            git clone https://github.com/twaugh/libieee1284
        fi
        cd libieee1284
        export XML_CATALOG_FILES="/mingw64/etc/xml/catalog"
        ./bootstrap
        ./configure --without-python
        make CFLAGS="-Wno-incompatible-pointer-types"
        make install
        ;;

    build|build-gui)
        do_autogen
        cd "$REPO_ROOT/vice"
        rm -rf build-gui
        mkdir -p build-gui && cd build-gui
        ../configure \
            --enable-gtk3ui \
            "${COMMON_CONFIGURE_FLAGS[@]}"
        make -j"${NUMBER_OF_PROCESSORS:-4}" -s
        ;;

    package|package-gui)
        cd "$REPO_ROOT"
        git fetch --tags
        VERSION=$(bash "$REPO_ROOT/scripts/compute-version.sh" --current)
        cd "$REPO_ROOT/vice/build-gui"
        make bindistzip
        ZIP_FILE=$(ls *.zip 2>/dev/null | head -1)
        if [ -n "$ZIP_FILE" ]; then
            cp "$ZIP_FILE" "$REPO_ROOT/${VERSION}-windows-x86_64-gui.zip"
            cd "$REPO_ROOT"
            sha256sum "${VERSION}-windows-x86_64-gui.zip" > "${VERSION}-windows-x86_64-gui.zip.sha256"
            echo "Packaged: ${VERSION}-windows-x86_64-gui.zip"
        else
            echo "ERROR: No zip artifact found" >&2
            exit 1
        fi
        ;;

    build-headless)
        do_autogen
        cd "$REPO_ROOT/vice"
        rm -rf build-headless
        mkdir -p build-headless && cd build-headless
        ../configure \
            --enable-headlessui \
            "${COMMON_CONFIGURE_FLAGS[@]}"
        make -j"${NUMBER_OF_PROCESSORS:-4}" -s
        ;;

    package-headless)
        cd "$REPO_ROOT"
        git fetch --tags
        VERSION=$(bash "$REPO_ROOT/scripts/compute-version.sh" --current)
        cd "$REPO_ROOT/vice/build-headless"
        make DESTDIR="$PWD/install-tree" install-strip
        cd install-tree
        zip -r "$REPO_ROOT/${VERSION}-windows-x86_64-headless.zip" .
        cd "$REPO_ROOT"
        sha256sum "${VERSION}-windows-x86_64-headless.zip" > "${VERSION}-windows-x86_64-headless.zip.sha256"
        echo "Packaged: ${VERSION}-windows-x86_64-headless.zip"
        ;;

    upload)
        cd "$REPO_ROOT"
        git fetch --tags
        VERSION=$(bash scripts/compute-version.sh --current)
        for artifact in "${VERSION}-windows-x86_64"-*.zip "${VERSION}-windows-x86_64"-*.sha256; do
            if [ -f "$artifact" ]; then
                gh release upload "$VERSION" "$artifact" --repo "${CI_REPO}" --clobber
                echo "Uploaded: $artifact"
            fi
        done
        ;;

    *)
        echo "Usage: $0 {build-libieee1284|build-gui|package-gui|build-headless|package-headless|upload}" >&2
        exit 1
        ;;
esac
