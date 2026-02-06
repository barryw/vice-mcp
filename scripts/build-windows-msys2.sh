#!/bin/bash
set -euo pipefail

# Windows build script for MSYS2/MINGW64 environment.
# Called from Woodpecker CI with MSYSTEM=MINGW64 already set.
#
# Usage: scripts/build-windows-msys2.sh <action>
#   actions: build-libieee1284, build, package, upload

ACTION="${1:-}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

case "$ACTION" in
    build-libieee1284)
        mkdir -p /tmp/work
        cd /tmp/work
        git clone https://github.com/twaugh/libieee1284
        cd libieee1284
        export XML_CATALOG_FILES="/mingw64/etc/xml/catalog"
        ./bootstrap
        ./configure --without-python
        make CFLAGS="-Wno-incompatible-pointer-types"
        make install
        ;;

    build)
        cd "$REPO_ROOT/vice"
        ./src/buildtools/genvicedate_h.sh
        ./autogen.sh
        SVN_REV=$(git tag -l 'r[0-9]*' --sort=-v:refname | head -1 | sed 's/^r//')
        ./configure \
            --enable-option-checking=fatal \
            --enable-gtk3ui \
            --enable-mcp-server \
            --enable-cpuhistory \
            --enable-ethernet \
            --enable-midi \
            --enable-parsid \
            --enable-catweasel \
            --disable-arch \
            --disable-pdf-docs \
            --disable-html-docs \
            --with-flac \
            --with-gif \
            --with-lame \
            --with-libcurl \
            --with-libieee1284 \
            --with-mpg123 \
            --with-png \
            --with-portaudio \
            --with-resid \
            --with-vorbis
        make -j"${NUMBER_OF_PROCESSORS:-4}" -s SVN_REVISION_OVERRIDE="$SVN_REV"
        ;;

    package)
        cd "$REPO_ROOT"
        git fetch --tags
        cd "$REPO_ROOT/vice"
        make bindistzip
        VERSION=$(bash "$REPO_ROOT/scripts/compute-version.sh" --current)
        ZIP_FILE=$(ls *.zip 2>/dev/null | head -1)
        if [ -n "$ZIP_FILE" ]; then
            cp "$ZIP_FILE" "$REPO_ROOT/${VERSION}-windows-x86_64.zip"
            echo "Packaged: ${VERSION}-windows-x86_64.zip"
        else
            echo "ERROR: No zip artifact found" >&2
            exit 1
        fi
        ;;

    upload)
        cd "$REPO_ROOT"
        git fetch --tags
        VERSION=$(bash scripts/compute-version.sh --current)
        gh release upload "$VERSION" "${VERSION}-windows-x86_64.zip" \
            --repo "${CI_REPO}" --clobber
        ;;

    *)
        echo "Usage: $0 {build-libieee1284|build|package|upload}" >&2
        exit 1
        ;;
esac
