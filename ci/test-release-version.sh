#!/bin/sh
# Tests for ci/release-version.sh in throwaway git repositories.
# Run: sh ci/test-release-version.sh   (needs git; exits non-zero on failure)
set -eu

script="$(cd "$(dirname "$0")" && pwd)/release-version.sh"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
failures=0

check() {  # check <name> <expected> <actual>
    if [ "$2" = "$3" ]; then
        echo "ok   $1"
    else
        echo "FAIL $1: expected '$2', got '$3'"
        failures=$((failures + 1))
    fi
}

new_repo() {
    rm -rf "$work/repo"
    git init -q "$work/repo"
    cd "$work/repo"
    git config user.email ci@example.invalid
    git config user.name ci
    git commit -q --allow-empty -m "feat: base"
    git tag v1.0.0
}

commit() { git commit -q --allow-empty -m "$1"; }

# The pipeline's own commit, then cog's bump commit tagged on top of it:
# this push created v1.1.0.
new_repo
commit "feat: new thing"
pushed=$(git rev-parse HEAD)
commit "chore(version): v1.1.0 [skip ci]"
git tag v1.1.0
git checkout -q "$pushed"
check "bump created a tag for this commit" "v1.1.0" "$(sh "$script")"

# A push with no bump-worthy commits: the latest tag is older.
new_repo
commit "docs: wording"
check "no bump, latest tag is older" "" "$(sh "$script")"

# The bump commit itself (if its pipeline runs): HEAD is the tagged commit.
new_repo
commit "feat: new thing"
commit "chore(version): v1.1.0"
git tag v1.1.0
check "pipeline for the bump commit itself" "" "$(sh "$script")"

# A newer commit already sits on main, but this commit's bump is v1.1.0.
new_repo
commit "fix: a"
pushed=$(git rev-parse HEAD)
commit "chore(version): v1.0.1"
git tag v1.0.1
commit "feat: b"
commit "chore(version): v1.1.0"
git tag v1.1.0
git checkout -q "$pushed"
check "finds this commit's tag, not the newest" "v1.0.1" "$(sh "$script")"

# No version tags at all.
rm -rf "$work/repo"
git init -q "$work/repo"
cd "$work/repo"
git config user.email ci@example.invalid
git config user.name ci
git commit -q --allow-empty -m "feat: base"
check "no tags" "" "$(sh "$script")"

if [ "$failures" -ne 0 ]; then
    echo "$failures failure(s)"
    exit 1
fi
echo "all passed"
