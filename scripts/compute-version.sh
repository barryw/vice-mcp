#!/bin/bash
set -euo pipefail

# Compute the vice-mcp version string from git state.
#
# Scheme: vice-mcp-{VICE_VERSION}-r{SVN_REV}-{MCP_REL}
#   VICE_VERSION  - from vice/configure.ac (e.g. 3.10.0)
#   SVN_REV       - from latest r* tag reachable from HEAD (e.g. 45966)
#   MCP_REL       - increments per push; resets to 1 when upstream changes
#
# Usage:
#   scripts/compute-version.sh              # compute the NEXT version
#   scripts/compute-version.sh --current    # return the latest existing tag
#   scripts/compute-version.sh --file FILE  # compute next and write to FILE

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# --current mode: return the latest vice-mcp-* tag
if [ "${1:-}" = "--current" ]; then
    LATEST=$(git -C "$REPO_ROOT" tag -l 'vice-mcp-*' --sort=-v:refname | head -1 || true)
    if [ -z "$LATEST" ]; then
        echo "ERROR: No vice-mcp-* tag found" >&2
        exit 1
    fi
    echo "$LATEST"
    exit 0
fi

# Extract VICE version from configure.ac
CONFIGURE_AC="$REPO_ROOT/vice/configure.ac"
if [ ! -f "$CONFIGURE_AC" ]; then
    echo "ERROR: $CONFIGURE_AC not found" >&2
    exit 1
fi

MAJOR=$(sed -n 's/.*m4_define(vice_version_major, *\([0-9]*\)).*/\1/p' "$CONFIGURE_AC")
MINOR=$(sed -n 's/.*m4_define(vice_version_minor, *\([0-9]*\)).*/\1/p' "$CONFIGURE_AC")
BUILD=$(sed -n 's/.*m4_define(vice_version_build, *\([0-9]*\)).*/\1/p' "$CONFIGURE_AC")
VICE_VERSION="${MAJOR}.${MINOR}.${BUILD}"

# Get upstream SVN revision from highest r* tag.
# Uses tag listing (not git describe) so it works in shallow clones.
SVN_TAG=$(git -C "$REPO_ROOT" tag -l 'r[0-9]*' --sort=-v:refname | head -1 || true)
if [ -z "$SVN_TAG" ]; then
    echo "ERROR: No r* tag found" >&2
    exit 1
fi
SVN_REV="$SVN_TAG"

# Compute MCP release number
PREFIX="vice-mcp-${VICE_VERSION}-${SVN_REV}"
LATEST_TAG=$(git -C "$REPO_ROOT" tag -l "${PREFIX}-*" --sort=-v:refname | head -1 || true)

if [ -n "$LATEST_TAG" ]; then
    CURRENT_NUM=$(echo "$LATEST_TAG" | grep -o '[0-9]*$')
    MCP_REL=$((CURRENT_NUM + 1))
else
    MCP_REL=1
fi

VERSION="${PREFIX}-${MCP_REL}"

# Optionally write to file
if [ "${1:-}" = "--file" ] && [ -n "${2:-}" ]; then
    echo "$VERSION" > "$2"
fi

echo "$VERSION"
