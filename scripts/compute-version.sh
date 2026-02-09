#!/bin/bash
set -euo pipefail

# Compute the vice-mcp version string from git state.
#
# Scheme: vice-mcp-{VICE_VERSION}-{GIT_SHA}-1
#   VICE_VERSION  - from vice/configure.ac (e.g. 3.10.0)
#   GIT_SHA       - short git commit hash (7 chars)
#
# Deterministic: same commit always produces the same version string.
# No tag lookup needed — the SHA makes each version unique.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Accept and ignore --current for backward compatibility
shift $# 2>/dev/null || true

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

echo "vice-mcp-${VICE_VERSION}-${GIT_SHA}-1"
