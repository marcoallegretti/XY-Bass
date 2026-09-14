#!/usr/bin/env bash
set -euo pipefail

fail() { echo "$1" >&2; exit 1; }

[ "$(uname -s)" = "Darwin" ] || fail "This installer pipeline must run on macOS"
[ $# -ge 4 ] || fail "usage: PackageMacInstaller.sh <artefacts-dir> <installer-identity> <notary-profile> <output.pkg>"

ARTEFACTS=$(cd "$1" && pwd)
INSTALLER_IDENTITY=$2
NOTARY_PROFILE=$3
OUTPUT=$4
VERSION=${RELEASE_VERSION:?RELEASE_VERSION must be set}
PKG_VERSION="${VERSION%%-*}"

[ -n "$INSTALLER_IDENTITY" ] || fail "Installer identity must not be empty"
security find-identity -v | grep -F "$INSTALLER_IDENTITY" >/dev/null \
    || fail "Installer identity was not found in the keychain"

VST3="$ARTEFACTS/VST3/XY Bass.vst3"
AU="$ARTEFACTS/AU/XY Bass.component"

# The installer must only ever carry code that already passed notarization.
for bundle in "$VST3" "$AU"; do
    [ -e "$bundle" ] || fail "Missing artifact $bundle"
    codesign --verify --strict --verbose=2 "$bundle" || fail "Unsigned artifact $bundle"
    xcrun stapler validate "$bundle" >/dev/null || fail "Unstapled artifact $bundle"
done

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/roots/vst3" "$WORK/roots/au" "$WORK/components"

ditto "$VST3" "$WORK/roots/vst3/$(basename "$VST3")"
ditto "$AU" "$WORK/roots/au/$(basename "$AU")"

build_component() {
    pkgbuild --identifier "com.23dsp.xybass.$1" \
        --version "$PKG_VERSION" \
        --root "$WORK/roots/$1" \
        --install-location "$2" \
        "$WORK/components/$1.pkg" >/dev/null
}

build_component vst3 "/Library/Audio/Plug-Ins/VST3"
build_component au "/Library/Audio/Plug-Ins/Components"

cat > "$WORK/distribution.xml" <<DISTRIBUTION
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>XY Bass $VERSION</title>
    <organization>com.23dsp</organization>
    <options customize="always" require-scripts="false" hostArchitectures="x86_64,arm64"/>
    <allowed-os-versions><os-version min="11.0"/></allowed-os-versions>
    <domains enable_anywhere="false" enable_currentUserHome="false" enable_localSystem="true"/>
    <choices-outline>
        <line choice="vst3"/>
        <line choice="au"/>
    </choices-outline>
    <choice id="vst3" title="VST3" description="Installs into /Library/Audio/Plug-Ins/VST3">
        <pkg-ref id="com.23dsp.xybass.vst3"/>
    </choice>
    <choice id="au" title="Audio Unit" description="Installs into /Library/Audio/Plug-Ins/Components">
        <pkg-ref id="com.23dsp.xybass.au"/>
    </choice>
    <pkg-ref id="com.23dsp.xybass.vst3" version="$PKG_VERSION">vst3.pkg</pkg-ref>
    <pkg-ref id="com.23dsp.xybass.au" version="$PKG_VERSION">au.pkg</pkg-ref>
</installer-gui-script>
DISTRIBUTION

rm -f "$OUTPUT"
productbuild --distribution "$WORK/distribution.xml" \
    --package-path "$WORK/components" \
    --sign "$INSTALLER_IDENTITY" \
    --timestamp \
    "$OUTPUT" >/dev/null

pkgutil --check-signature "$OUTPUT" >/dev/null || fail "Installer signature verification failed"

result=$(xcrun notarytool submit "$OUTPUT" --keychain-profile "$NOTARY_PROFILE" \
    --wait --timeout 25m --output-format json) || true
echo "$result"
echo "$result" | grep -qE '"status"[[:space:]]*:[[:space:]]*"Accepted"' || fail "Notarization failed for the installer"

xcrun stapler staple "$OUTPUT"
xcrun stapler validate "$OUTPUT"
spctl --assess --type install --verbose=4 "$OUTPUT"

shasum -a 256 "$OUTPUT"
