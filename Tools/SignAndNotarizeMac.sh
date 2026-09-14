#!/usr/bin/env bash
set -euo pipefail

fail() { echo "$1" >&2; exit 1; }

[ "$(uname -s)" = "Darwin" ] || fail "Signing must run on macOS"
[ $# -ge 3 ] || fail "usage: SignAndNotarizeMac.sh <artefacts-dir> <signing-identity> <notary-profile>"

ARTEFACTS=$(cd "$1" && pwd)
SIGNING_IDENTITY=$2
NOTARY_PROFILE=$3

VST3="$ARTEFACTS/VST3/XY Bass.vst3"
AU="$ARTEFACTS/AU/XY Bass.component"

for bundle in "$VST3" "$AU"; do
    [ -e "$bundle" ] || fail "Missing artifact $bundle"
done

security find-identity -v -p codesigning | grep -F "$SIGNING_IDENTITY" >/dev/null \
    || fail "Signing identity was not found in the keychain"

for bundle in "$VST3" "$AU"; do
    codesign --force --options runtime --timestamp --sign "$SIGNING_IDENTITY" "$bundle"
    codesign --verify --strict --verbose=2 "$bundle"
done

# The notary service issues one ticket covering every signed item inside the upload,
# so a single submission replaces one round trip per bundle.
NOTARY_DIR=$(mktemp -d)
trap 'rm -rf "$NOTARY_DIR"' EXIT
mkdir -p "$NOTARY_DIR/XY Bass"

for bundle in "$VST3" "$AU"; do
    ditto "$bundle" "$NOTARY_DIR/XY Bass/$(basename "$bundle")"
done

ARCHIVE="$NOTARY_DIR/XY-Bass.zip"
ditto -c -k --keepParent "$NOTARY_DIR/XY Bass" "$ARCHIVE"

# Without --timeout a stalled submission waits until the runner is killed.
result=$(xcrun notarytool submit "$ARCHIVE" --keychain-profile "$NOTARY_PROFILE" \
    --wait --timeout 25m --output-format json) || true
echo "$result"

SUBMISSION=$(echo "$result" | grep -oE '[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}' | head -n 1)
if ! echo "$result" | grep -qE '"status"[[:space:]]*:[[:space:]]*"Accepted"'; then
    [ -n "$SUBMISSION" ] && xcrun notarytool log "$SUBMISSION" --keychain-profile "$NOTARY_PROFILE" || true
    fail "Notarization was not accepted"
fi

# The ticket can lag a few seconds behind acceptance on Apple's CDN.
for bundle in "$VST3" "$AU"; do
    for attempt in 1 2 3 4 5; do
        xcrun stapler staple "$bundle" && break
        [ "$attempt" = 5 ] && fail "Stapling failed for $(basename "$bundle")"
        sleep 15
    done
    xcrun stapler validate "$bundle"
done

# Plug-in bundles are not applications, so Gatekeeper assesses their primary signature.
for bundle in "$VST3" "$AU"; do
    spctl --assess --type open --context context:primary-signature --verbose=4 "$bundle"
done
