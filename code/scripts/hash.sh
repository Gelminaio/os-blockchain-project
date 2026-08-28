#!/bin/bash
# sha-256 of a hex string given on the command line

source "$(dirname "$0")/errors.sh"

len=$(printf '%s' "$1" | wc -c)

if [[ -z "$1" || ! "$1" =~ ^[0-9a-fA-F]+$ ]] || ((len%2!=0)); then
  echo "Error: the input must be a hex string of even length." >&2
  exit "$INVALID_BLOCK"
fi

printf '%s' "$1" | xxd -r -p | sha256sum | cut -d' ' -f1

exit "$OK"