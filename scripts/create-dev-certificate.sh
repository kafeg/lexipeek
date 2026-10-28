#!/usr/bin/env bash
# Creates a self-signed code-signing certificate for local development.
#
# Why: macOS ties an Accessibility grant to the app's code signature. Ad-hoc
# signing produces a different identity on every build, so the permission has
# to be granted again after each rebuild. Signing with one stable certificate
# keeps the grant.
#
# The certificate stays on this machine and is used for local builds only;
# releases are signed separately. Remove it any time with:
#   security delete-certificate -c "LexiPeek Dev" ~/Library/Keychains/login.keychain-db
set -euo pipefail

NAME="LexiPeek Dev"
KEYCHAIN="$HOME/Library/Keychains/login.keychain-db"
OPENSSL="$(command -v /opt/homebrew/bin/openssl || command -v openssl)"

if security find-identity -v -p codesigning | grep -q "$NAME"; then
    echo "Identity \"$NAME\" already exists."
    exit 0
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

"$OPENSSL" req -x509 -newkey rsa:2048 -nodes -days 3650 \
    -keyout "$WORK/key.pem" -out "$WORK/cert.pem" \
    -subj "/CN=$NAME" \
    -addext "basicConstraints=critical,CA:false" \
    -addext "keyUsage=critical,digitalSignature" \
    -addext "extendedKeyUsage=critical,codeSigning" >/dev/null 2>&1

"$OPENSSL" pkcs12 -export -legacy \
    -inkey "$WORK/key.pem" -in "$WORK/cert.pem" \
    -out "$WORK/identity.p12" -name "$NAME" -passout pass:lexipeek >/dev/null 2>&1

security import "$WORK/identity.p12" -k "$KEYCHAIN" -P lexipeek \
    -T /usr/bin/codesign -T /usr/bin/security -A >/dev/null

# A self-signed certificate is not a usable signing identity until it is
# trusted for code signing; without this step `codesign` never sees it.
security add-trusted-cert -r trustRoot -p codeSign -k "$KEYCHAIN" "$WORK/cert.pem"

if security find-identity -v -p codesigning | grep -q "$NAME"; then
    echo "Created \"$NAME\" — local builds will now keep their Accessibility grant."
else
    echo "Import succeeded but the identity is still not valid; check Keychain Access." >&2
    exit 1
fi
