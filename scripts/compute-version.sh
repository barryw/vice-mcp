#!/bin/bash
set -euo pipefail

# Compute the vice-mcp version string from git state.
#
# Scheme: vice-mcp-{VICE_VERSION}-{GIT_SHA}-{MCP_REL}
#   VICE_VERSION  - from vice/configure.ac (e.g. 3.10.0)
#   GIT_SHA       - short git commit hash (7 chars)
#   MCP_REL       - increments per push; resets to 1 when upstream changes
#
# Usage:
#   scripts/compute-version.sh              # compute the NEXT version
#   scripts/compute-version.sh --current    # return the latest existing tag

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# --current mode: return the latest vice-mcp-* tag
if [ "${1:-}" = "--current" ]; then
    # Try local tags first (fast path)
    LATEST=$(git -C "$REPO_ROOT" tag -l 'vice-mcp-*' --sort=-v:refname | head -1 || true)

    # Fallback: query remote directly (handles case where git fetch --tags
    # didn't pick up tags created by gh release create via GitHub API)
    if [ -z "$LATEST" ]; then
        echo "No local vice-mcp-* tags, querying remote..." >&2
        LATEST=$(git -C "$REPO_ROOT" ls-remote --tags origin 'refs/tags/vice-mcp-*' \
            | sed 's|.*refs/tags/||' \
            | sort -V \
            | tail -1 || true)
    fi

    if [ -z "$LATEST" ]; then
        echo "ERROR: No vice-mcp-* tag found (local or remote)" >&2
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

# Get short git SHA (7 chars)
GIT_SHA=$(git -C "$REPO_ROOT" rev-parse --short=7 HEAD)

# Compute MCP release number
PREFIX="vice-mcp-${VICE_VERSION}-${GIT_SHA}"
LATEST_TAG=$(git -C "$REPO_ROOT" tag -l "${PREFIX}-*" --sort=-v:refname | head -1 || true)

if [ -n "$LATEST_TAG" ]; then
    CURRENT_NUM=$(echo "$LATEST_TAG" | grep -o '[0-9]*$')
    MCP_REL=$((CURRENT_NUM + 1))
else
    MCP_REL=1
fi

VERSION="${PREFIX}-${MCP_REL}"
echo "$VERSION"
