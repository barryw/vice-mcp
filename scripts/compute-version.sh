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
#   scripts/compute-version.sh              # compute the NEXT version (create-release only)
#   scripts/compute-version.sh --current    # return the tag for HEAD (build/upload steps)

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

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

# Prefix for this commit
PREFIX="vice-mcp-${VICE_VERSION}-${GIT_SHA}"

# --current mode: return the existing tag for THIS commit
if [ "${1:-}" = "--current" ]; then
    # Look for a tag matching this commit's SHA prefix
    TAG=$(git -C "$REPO_ROOT" tag -l "${PREFIX}-*" --sort=-v:refname | head -1 || true)

    # Fallback: query remote
    if [ -z "$TAG" ]; then
        echo "No local ${PREFIX}-* tag, querying remote..." >&2
        TAG=$(git -C "$REPO_ROOT" ls-remote --tags origin "refs/tags/${PREFIX}-*" \
            | sed 's|.*refs/tags/||' \
            | sort -V \
            | tail -1 || true)
    fi

    if [ -z "$TAG" ]; then
        echo "ERROR: No tag found for HEAD (${PREFIX}-*)" >&2
        echo "Has create-release run? Try: git fetch --tags" >&2
        exit 1
    fi

    echo "$TAG"
    exit 0
fi

# Default mode: compute the NEXT version (for create-release)
LATEST_TAG=$(git -C "$REPO_ROOT" tag -l "${PREFIX}-*" --sort=-v:refname | head -1 || true)

if [ -n "$LATEST_TAG" ]; then
    CURRENT_NUM=$(echo "$LATEST_TAG" | grep -o '[0-9]*$')
    MCP_REL=$((CURRENT_NUM + 1))
else
    MCP_REL=1
fi

VERSION="${PREFIX}-${MCP_REL}"
echo "$VERSION"
