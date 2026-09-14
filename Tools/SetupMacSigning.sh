#!/usr/bin/env bash
set -euo pipefail

fail() { echo "$1" >&2; exit 1; }

[ "$(uname -s)" = "Darwin" ] || fail "Signing setup must run on macOS"

NOTARY_PROFILE=${NOTARY_PROFILE:-xybass-notary}
KEYCHAIN_NAME=${KEYCHAIN_NAME:-xybass-signing}
KEYCHAIN="$HOME/Library/Keychains/$KEYCHAIN_NAME.keychain-db"

for required in MACOS_APPLICATION_P12_BASE64 MACOS_APPLICATION_P12_PASSWORD \
                MACOS_INSTALLER_P12_BASE64 MACOS_INSTALLER_P12_PASSWORD \
                APPLE_ID APPLE_APP_PASSWORD APPLE_TEAM_ID; do
    [ -n "${!required:-}" ] || fail "$required is not set"
done

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

KEYCHAIN_PASSWORD=$(openssl rand -base64 24)
security delete-keychain "$KEYCHAIN" 2>/dev/null || true
security create-keychain -p "$KEYCHAIN_PASSWORD" "$KEYCHAIN"
# Without this the keychain relocks mid-build and codesign fails at random.
security set-keychain-settings -lut 21600 "$KEYCHAIN"
security unlock-keychain -p "$KEYCHAIN_PASSWORD" "$KEYCHAIN"
security list-keychains -d user -s "$KEYCHAIN" "$HOME/Library/Keychains/login.keychain-db"
# notarytool resolves --keychain-profile against the default keychain, so the
# signing keychain has to become the default for the profile to be found.
security default-keychain -s "$KEYCHAIN"

import_identity() {
    local payload=$1 password=$2 file="$WORK/$3.p12"
    # Tolerates CRLF and wrapped base64, which a Windows-generated export can carry.
    printf '%s' "$payload" | tr -d '\r\n ' | base64 --decode > "$file"
    security import "$file" -k "$KEYCHAIN" -P "$password" -A \
        -T /usr/bin/codesign -T /usr/bin/productbuild -T /usr/bin/security
    rm -f "$file"
}

import_identity "$MACOS_APPLICATION_P12_BASE64" "$MACOS_APPLICATION_P12_PASSWORD" application
import_identity "$MACOS_INSTALLER_P12_BASE64" "$MACOS_INSTALLER_P12_PASSWORD" installer

# Suppresses the interactive "allow access" prompt that would hang a runner.
security set-key-partition-list -S apple-tool:,apple:,codesign: \
    -s -k "$KEYCHAIN_PASSWORD" "$KEYCHAIN" >/dev/null

SIGNING_IDENTITY=$(security find-identity -v -p codesigning "$KEYCHAIN" \
    | sed -n 's/.*"\(Developer ID Application: .*\)".*/\1/p' | head -n 1)
INSTALLER_IDENTITY=$(security find-identity -v "$KEYCHAIN" \
    | sed -n 's/.*"\(Developer ID Installer: .*\)".*/\1/p' | head -n 1)

[ -n "$SIGNING_IDENTITY" ] || fail "Developer ID Application identity was not imported"
[ -n "$INSTALLER_IDENTITY" ] || fail "Developer ID Installer identity was not imported"

xcrun notarytool store-credentials "$NOTARY_PROFILE" \
    --apple-id "$APPLE_ID" \
    --team-id "$APPLE_TEAM_ID" \
    --password "$APPLE_APP_PASSWORD" \
    --keychain "$KEYCHAIN" >/dev/null

echo "SIGNING_IDENTITY=$SIGNING_IDENTITY"
echo "INSTALLER_IDENTITY=$INSTALLER_IDENTITY"
echo "NOTARY_PROFILE=$NOTARY_PROFILE"

if [ -n "${GITHUB_ENV:-}" ]; then
    {
        echo "SIGNING_IDENTITY=$SIGNING_IDENTITY"
        echo "INSTALLER_IDENTITY=$INSTALLER_IDENTITY"
        echo "NOTARY_PROFILE=$NOTARY_PROFILE"
    } >> "$GITHUB_ENV"
fi
