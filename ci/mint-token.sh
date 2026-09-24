#!/usr/bin/env bash
#
# mint-token.sh — vendored GitHub App installation-token minter for CI legs that
# can NOT run the ghcr.io/barryw/woodpecker-release:go plugin image, i.e. the
# darwin/local-backend and cross-platform-matrix release steps (WAL-74 / WAL-70b).
#
# WHY: WAL-72 replaced the shared admin PAT with a per-pipeline mint step on the
# Linux/k8s legs using the go plugin image. That image is linux/amd64 and can't
# execute on the Apple-silicon local backend or in a powershell leg. This is a
# faithful, dependency-minimal port of
# infrastructure/github-app-token/mint-installation-token.sh — same RS256 App
# JWT -> installation-token exchange, but using ONLY bash + openssl + curl (no
# jq), so it runs unchanged on a host macOS agent and on Linux build images.
#
# SCOPE: by default mints a token scoped to THIS repo only (basename of CI_REPO)
# with contents=write,metadata=read — the product-ci permission set, narrowed to
# a single repo (tighter than the shared product-ci profile, which spans all 7).
#
# REQUIRED ENV (Woodpecker org secrets, injected on the consuming step):
#   GH_APP_ID, GH_APP_INSTALLATION_ID, GH_APP_PRIVATE_KEY
# OPTIONAL ENV:
#   MINT_REPOS   comma-separated repo names (default: basename "$CI_REPO")
#   MINT_PERMS   comma-separated key=level (default: contents=write,metadata=read)
#   GITHUB_API_URL  override API base (default https://api.github.com)
#
# OUTPUT: the bare installation token on stdout (~1h TTL). Do not log it. Use:
#   export GITHUB_TOKEN="$(bash ci/mint-token.sh)"
set -euo pipefail

die() { echo "mint-token: $*" >&2; exit 1; }

API="${GITHUB_API_URL:-https://api.github.com}"
REPOS="${MINT_REPOS:-$(basename "${CI_REPO:?CI_REPO not set (Woodpecker provides it)}")}"
PERMS="${MINT_PERMS:-contents=write,metadata=read}"

: "${GH_APP_ID:?GH_APP_ID not set}"
: "${GH_APP_INSTALLATION_ID:?GH_APP_INSTALLATION_ID not set}"
: "${GH_APP_PRIVATE_KEY:?GH_APP_PRIVATE_KEY not set}"

# --- write the private key to a 0600 temp file (handles single-line "\n" PEMs)
KEYFILE="$(mktemp)"; chmod 600 "$KEYFILE"
trap 'rm -f "$KEYFILE"' EXIT
if printf '%s' "$GH_APP_PRIVATE_KEY" | grep -q 'BEGIN.*PRIVATE KEY' && printf '%s' "$GH_APP_PRIVATE_KEY" | grep -q $'\n'; then
  printf '%s\n' "$GH_APP_PRIVATE_KEY" > "$KEYFILE"
else
  # secret stored single-line with literal backslash-n: restore real newlines
  printf '%b\n' "$GH_APP_PRIVATE_KEY" > "$KEYFILE"
fi

# --- build a short-lived (9 min) RS256 App JWT ------------------------------
b64url() { openssl base64 -A | tr '+/' '-_' | tr -d '='; }
now="$(date +%s)"
iat="$((now - 60))"      # backdate 60s for clock skew (GitHub allows it)
exp="$((now + 540))"     # max 10 min; use 9
header='{"alg":"RS256","typ":"JWT"}'
payload="$(printf '{"iat":%d,"exp":%d,"iss":"%s"}' "$iat" "$exp" "$GH_APP_ID")"
unsigned="$(printf '%s' "$header" | b64url).$(printf '%s' "$payload" | b64url)"
sig="$(printf '%s' "$unsigned" | openssl dgst -sha256 -sign "$KEYFILE" -binary | b64url)"
jwt="$unsigned.$sig"

# --- assemble the down-scoped request body (no jq) --------------------------
json_array_of() {            # comma-list -> ["a","b"]
  local out="" item IFS=','
  for item in $1; do
    [ -n "$item" ] || continue
    out="$out,\"$item\""
  done
  printf '[%s]' "${out#,}"
}
json_object_of() {           # comma-list of k=v -> {"k":"v",...}
  local out="" kv k v IFS=','
  for kv in $1; do
    [ -n "$kv" ] || continue
    k="${kv%%=*}"; v="${kv#*=}"
    [ "$k" != "$kv" ] || die "bad perm '$kv' (want key=level)"
    out="$out,\"$k\":\"$v\""
  done
  printf '{%s}' "${out#,}"
}
body="$(printf '{"repositories":%s,"permissions":%s}' \
  "$(json_array_of "$REPOS")" "$(json_object_of "$PERMS")")"

# --- mint the installation token --------------------------------------------
resp="$(curl -fsS -X POST \
  -H "Authorization: Bearer $jwt" \
  -H "Accept: application/vnd.github+json" \
  -H "X-GitHub-Api-Version: 2022-11-28" \
  "$API/app/installations/$GH_APP_INSTALLATION_ID/access_tokens" \
  -d "$body")" || die "token request failed (check App ID / Installation ID / key / repos in install)"

# extract "token":"..." without jq (token value is alnum, never contains a comma)
token="$(printf '%s' "$resp" | tr ',' '\n' | grep -m1 '"token"' \
  | sed -E 's/.*"token"[[:space:]]*:[[:space:]]*"([^"]+)".*/\1/')"
[ -n "$token" ] || die "no token in response: $resp"

printf '%s\n' "$token"
