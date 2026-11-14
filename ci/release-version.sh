#!/bin/sh
# Prints the version tag (vX.Y.Z) that bump.yaml created for the commit this
# pipeline built, or nothing if this push created no release.
#
# cog bump commits "chore(version): vX.Y.Z" on top of the pushed commit and
# tags that bump commit, so the release for HEAD is the v tag whose commit's
# parent is HEAD. Publishing steps run only when this prints a tag: a push
# with no bump-worthy commits, or the pipeline of the bump commit itself,
# must not overwrite the latest release's notes or assets.
#
# Usage: VERSION=$(sh ci/release-version.sh)   (run after git fetch --tags)
set -eu

head=$(git rev-parse HEAD)
for tag in $(git tag -l 'v[0-9]*' --sort=-v:refname); do
    parent=$(git rev-parse -q --verify "$tag^{commit}^" 2>/dev/null) || continue
    if [ "$parent" = "$head" ]; then
        echo "$tag"
        exit 0
    fi
done
