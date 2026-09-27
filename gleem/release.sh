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
# module. The defaults are for the Certum card in the ACR40T reader: its
# "standard" profile (sc30pkcs11 from proCertumCardManager) holds the
# "Open Source Developer René Preuß" code-signing certificate, valid until
# 2027-03-11. The card's "secure" profile is a different one and not used.
# Environment:
#   PKCS11_MODULE   the token's PKCS#11 library
#   PKCS11_KEY      PKCS#11 URI of the signing key
#   PKCS11_CERT     PKCS#11 URI of its certificate
#   TIMESTAMP_URL   default http://time.certum.pl
#   CA_BUNDLE       system roots to check the signature against (default:
#                   Fedora's or Debian's bundle)
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
if [[ -z "${PKCS11_MODULE:-}" ]]; then
    PKCS11_MODULE="$(ls /opt/proCertumCardManager/sc30pkcs11-*.so 2>/dev/null | sort -V | tail -1)"
fi
[[ -n "$PKCS11_MODULE" && -r "$PKCS11_MODULE" ]] || die "set PKCS11_MODULE to the token's PKCS#11 library"
certum="pkcs11:token=profil%20standardowy;id=%f0%a5%f7%cf%02%39%27%39%f9%76%99%83%ec%c6%48%ae%34%82%65%bc"
PKCS11_KEY="${PKCS11_KEY:-$certum;type=private}"
PKCS11_CERT="${PKCS11_CERT:-$certum;type=cert}"
TIMESTAMP_URL="${TIMESTAMP_URL:-http://time.certum.pl}"
here="$(cd "$(dirname "$0")" && pwd)"
# Embedded in the signature so Windows can build the chain without fetching
# it: Certum Code Signing 2021 CA, issued by Certum Trusted Network CA 2.
intermediate="$here/certs/certum-code-signing-2021-ca.pem"
if [[ -z "${CA_BUNDLE:-}" ]]; then
    for f in /etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem /etc/ssl/certs/ca-certificates.crt; do
        [[ -r "$f" ]] && CA_BUNDLE="$f" && break
    done
fi
[[ -r "${CA_BUNDLE:-}" ]] || die "set CA_BUNDLE to the system's root certificates"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# The run of this tag at the commit it points at now: a moved tag leaves the
# old runs behind, and the branch's run of the same commit was built before
# the tag existed, so it carries another version.
commit="$(gh api "repos/$REPO/commits/$tag" -q .sha)" || die "no tag $tag on GitHub"
say "Waiting for the CI run of $tag ($commit)"
run=""
for _ in $(seq 60); do
    run="$(gh run list -R "$REPO" -w "$WORKFLOW" --commit "$commit" --event push -L 20 \
        --json databaseId,headBranch -q "[.[] | select(.headBranch == \"$tag\")][0].databaseId // empty")"
    [[ -n "$run" ]] && break
    sleep 10
done
[[ -n "$run" ]] || die "no CI run for $tag; was the tag pushed?"
id="$run"
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
    -ac "$intermediate" \
    -h sha256 \
    -n "Gleem Imager" -i "https://gleem.gg" \
    -ts "$TIMESTAMP_URL" \
    -in "$installer" -out "$signed"
osslsigncode verify -CAfile "$CA_BUNDLE" -TSA-CAfile "$CA_BUNDLE" -in "$signed" >"$work/verify.txt" 2>&1 || {
    cat "$work/verify.txt" >&2
    die "the signature does not verify"
}
for f in "$signed" "$appimage"; do
    case "$(basename "$f")" in
        *-dirty*|*-g[0-9a-f]*) die "$(basename "$f") is not a clean release build" ;;
    esac
done
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
