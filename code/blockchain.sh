#!/bin/bash
# entry point for the bash side: --verify, --hash, --merkle

cd "$(dirname "$0")"

source scripts/errors.sh

[[ $# -eq 2 ]] || {
  echo "Usage: $0 --verify|--hash|--merkle <arg>" >&2
  exit "$ARGS_ERROR"
}

case "$1" in
  --verify)
    exec scripts/verify.sh "$2"
    ;;
  --hash)
    exec scripts/hash.sh "$2"
    ;;
  --merkle)
    exec scripts/merkle.sh "$2"
    ;;
  *)
    echo "Invalid operation" >&2
    exit "$ARGS_ERROR"
    ;;
esac
