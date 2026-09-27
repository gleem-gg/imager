#!/usr/bin/env bash
#
# Signs and publishes a Gleem Imager release. Run on the machine with the
# code-signing token plugged in, after pushing a gleem-vX.Y.Z tag:
#
#   gleem/release.sh gleem-v1.0.0
#
# 1. waits for that tag's CI run and downloads its Windows installer and
#    Linux AppImage;
# 2. signs the installer with the key on the token (osslsigncode over
#    PKCS#11, RFC 3161 timestamp) and verifies the signature;
# 3. uploads installer, AppImage and SHA256SUMS to
#    https://get.gleem.gg/imager/vX.Y.Z/, and `imager/latest` last.
#
# A published version is never replaced: get.gleem.gg caches it for 30 days.
#
# Needs gh, jq, curl, sha256sum and osslsigncode, plus the token's PKCS#11
# module. Environment:
#   PKCS11_MODULE   the token's PKCS#11 library (required)
#   PKCS11_KEY      PKCS#11 URI of the signing key  (default: first private key)
#   PKCS11_CERT     PKCS#11 URI of its certificate  (default: first certificate)
#   TIMESTAMP_URL   default http://time.certum.pl
#   BUNNY_STORAGE_PASSWORD  storage zone password; fetched with ~/.bunny-api-key if unset
#   DRY_RUN=1       sign and verify, but upload nothing

set -euo pipefail

REPO=gleem-gg/imager
WORKFLOW=gleem-release.yml
STORAGE=https://storage.bunnycdn.com/gleem-cli/imager
STORAGE_ZONE_ID=1939332

die() { echo "release: $*" >&2; exit 1; }
say() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }

tag="${1:-}"
[[ "$tag" =~ ^gleem-v[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "usage: $0 gleem-vX.Y.Z"
version="${tag#gleem-}"

for tool in gh jq curl sha256sum osslsigncode; do
    command -v "$tool" >/dev/null || die "$tool is not installed"
done
[[ -n "${PKCS11_MODULE:-}" && -r "$PKCS11_MODULE" ]] || die "set PKCS11_MODULE to the token's PKCS#11 library"
PKCS11_KEY="${PKCS11_KEY:-pkcs11:type=private}"
PKCS11_CERT="${PKCS11_CERT:-pkcs11:type=cert}"
TIMESTAMP_URL="${TIMESTAMP_URL:-http://time.certum.pl}"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

say "Waiting for the CI run of $tag"
run=""
for _ in $(seq 60); do
    run="$(gh run list -R "$REPO" -w "$WORKFLOW" --branch "$tag" -L 1 \
        --json databaseId,status,conclusion -q '.[0] | select(.) | "\(.databaseId) \(.status) \(.conclusion)"')"
    [[ -n "$run" ]] && break
    sleep 10
done
[[ -n "$run" ]] || die "no CI run for $tag; was the tag pushed?"
id="${run%% *}"
gh run watch "$id" -R "$REPO" --exit-status >/dev/null || die "CI run $id did not succeed"

say "Downloading the build of run $id"
gh run download "$id" -R "$REPO" -D "$work/artifacts"
installer="$(find "$work/artifacts" -name "gleem-imager-*.exe" | head -1)"
appimage="$(find "$work/artifacts" -name "Gleem_Imager-*.AppImage" | head -1)"
[[ -n "$installer" && -n "$appimage" ]] || die "the run has no installer or no AppImage"

mkdir "$work/dist"
signed="$work/dist/$(basename "$installer")"
say "Signing $(basename "$installer") (the token may ask for its PIN)"
osslsigncode sign \
    -pkcs11module "$PKCS11_MODULE" \
    -pkcs11cert "$PKCS11_CERT" \
    -key "$PKCS11_KEY" \
    -h sha256 \
    -n "Gleem Imager" -i "https://gleem.gg" \
    -ts "$TIMESTAMP_URL" \
    -in "$installer" -out "$signed"
osslsigncode verify -in "$signed" >/dev/null || die "the signature does not verify"
say "Signature verifies"

cp "$appimage" "$work/dist/"
(cd "$work/dist" && sha256sum -- * > SHA256SUMS && cat SHA256SUMS)

if [[ "${DRY_RUN:-}" == 1 ]]; then
    mkdir -p dist && cp "$work/dist/"* dist/
    say "Dry run: signed files are in ./dist, nothing uploaded"
    exit 0
fi

if [[ -z "${BUNNY_STORAGE_PASSWORD:-}" ]]; then
    [[ -r ~/.bunny-api-key ]] || die "set BUNNY_STORAGE_PASSWORD or provide ~/.bunny-api-key"
    BUNNY_STORAGE_PASSWORD="$(curl -fsS -H "AccessKey: $(cat ~/.bunny-api-key)" \
        "https://api.bunny.net/storagezone/$STORAGE_ZONE_ID" | jq -r .Password)"
fi

if curl -fsS -H "AccessKey: $BUNNY_STORAGE_PASSWORD" "$STORAGE/$version/" | grep -q '"ObjectName"'; then
    die "$version is already published"
fi

put() {
    curl -fsS -o /dev/null -X PUT -H "AccessKey: $BUNNY_STORAGE_PASSWORD" \
        -H "Content-Type: application/octet-stream" --data-binary @"$1" "$STORAGE/$2"
    echo "  $2"
}
say "Publishing $version to get.gleem.gg/imager/"
for f in "$work/dist/"*; do put "$f" "$version/$(basename "$f")"; done
printf '%s\n' "$version" > "$work/latest"
put "$work/latest" latest
say "Published https://get.gleem.gg/imager/$version/"
