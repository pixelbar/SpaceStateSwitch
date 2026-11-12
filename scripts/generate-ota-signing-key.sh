#!/usr/bin/env bash
# Generates the ECDSA P-256 key pair used to sign OTA firmware updates.
#
#   scripts/generate-ota-signing-key.sh [private-key-path]
#
# Writes the public key to keys/ota_signing_key.pub.pem (commit this) and the
# private key to the given path (default: ./ota_signing_key.pem, git-ignored).
# Store the private key as the OTA_SIGNING_KEY GitHub Actions secret and in a
# password manager, then delete the local copy. See README.md.
set -euo pipefail

cd "$(dirname "$0")/.."
private_key="${1:-ota_signing_key.pem}"
public_key="keys/ota_signing_key.pub.pem"

if [[ -e "$private_key" ]]; then
  echo "error: $private_key already exists" >&2
  exit 1
fi
if [[ -s "$public_key" && "${FORCE:-}" != "1" ]]; then
  cat >&2 <<MSG
error: $public_key already contains a key.
Devices only accept updates signed with the key compiled into their current
firmware, so replacing it will cut them off from future updates unless you first
ship a release (signed with the old key) that contains the new public key.
Re-run with FORCE=1 if you really want to replace it.
MSG
  exit 1
fi

umask 077
openssl ecparam -name prime256v1 -genkey -noout -out "$private_key"
openssl ec -in "$private_key" -pubout -out "$public_key" 2>/dev/null

echo "Private key: $private_key  (keep secret, do NOT commit)"
echo "Public key:  $public_key  (commit this)"
echo
echo "Next: gh secret set OTA_SIGNING_KEY < \"$private_key\""
