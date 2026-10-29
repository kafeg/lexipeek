#!/usr/bin/env bash
# A self-signed code-signing certificate for local builds ("LexiPeek Dev").
#
# Why: macOS ties an Accessibility grant (and the Keychain's AI keys) to the app's code signature. Ad-hoc signing
# makes a different identity on every build, so the grant goes after each rebuild; one stable certificate keeps it.
# It stays on this machine and signs local builds only; releases are signed separately.
#
# Usage: scripts/create-dev-certificate.sh [--save DIR | --import FILE.p12]
#   --save DIR      also keep the new certificate as DIR/lexipeek-dev.p12 (password in DIR/lexipeek-dev.password)
#   --import FILE   use a certificate saved before (another Mac): its password from FILE's .password next to it
# Remove it: security delete-identity -c "LexiPeek Dev"
set -euo pipefail

NAME="LexiPeek Dev"
KEYCHAIN="$HOME/Library/Keychains/login.keychain-db"
OPENSSL="$(command -v /opt/homebrew/bin/openssl || command -v openssl)"
SAVE="" IMPORT=""
case "${1:-}" in
  --save) SAVE="$2" ;;
  --import) IMPORT="$2" ;;
  "") ;;
  *) echo "usage: $0 [--save DIR | --import FILE.p12]" >&2; exit 2 ;;
esac

if security find-identity -v -p codesigning | grep -q "$NAME"; then
  echo "Identity \"$NAME\" already exists."
  exit 0
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

if [ -n "$IMPORT" ]; then
  PASS="$(cat "${IMPORT%.p12}.password")"
  cp "$IMPORT" "$WORK/identity.p12"
  "$OPENSSL" pkcs12 -in "$WORK/identity.p12" -passin "pass:$PASS" -nokeys -legacy -out "$WORK/cert.pem" 2>/dev/null \
    || "$OPENSSL" pkcs12 -in "$WORK/identity.p12" -passin "pass:$PASS" -nokeys -out "$WORK/cert.pem"
else
  PASS="$("$OPENSSL" rand -hex 16)"
  "$OPENSSL" req -x509 -newkey rsa:2048 -nodes -days 3650 \
    -keyout "$WORK/key.pem" -out "$WORK/cert.pem" \
    -subj "/CN=$NAME" \
    -addext "basicConstraints=critical,CA:false" \
    -addext "keyUsage=critical,digitalSignature" \
    -addext "extendedKeyUsage=critical,codeSigning" >/dev/null 2>&1
  "$OPENSSL" pkcs12 -export -legacy \
    -inkey "$WORK/key.pem" -in "$WORK/cert.pem" \
    -out "$WORK/identity.p12" -name "$NAME" -passout "pass:$PASS" >/dev/null 2>&1
  if [ -n "$SAVE" ]; then
    mkdir -p "$SAVE"
    cp "$WORK/identity.p12" "$SAVE/lexipeek-dev.p12"
    printf '%s\n' "$PASS" > "$SAVE/lexipeek-dev.password"
    echo "Saved $SAVE/lexipeek-dev.p12"
  fi
fi

security import "$WORK/identity.p12" -k "$KEYCHAIN" -P "$PASS" -T /usr/bin/codesign -T /usr/bin/security -A >/dev/null
# A self-signed certificate is not a usable signing identity until it is trusted for code signing.
security add-trusted-cert -r trustRoot -p codeSign -k "$KEYCHAIN" "$WORK/cert.pem"

if security find-identity -v -p codesigning | grep -q "$NAME"; then
  echo "Created \"$NAME\" — local builds will now keep their Accessibility grant."
else
  echo "Import succeeded but the identity is still not valid; check Keychain Access." >&2
  exit 1
fi
