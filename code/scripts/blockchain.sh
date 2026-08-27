#!/bin/bash
# entry point for the bash side: --verify, --hash, --merkle

# resolve the helpers next to this script, without changing the caller's
# working directory: the file argument stays relative to where we were called
DIR="$(dirname "$0")"

source "$DIR/errors.sh"

[[ $# -eq 2 ]] || {
  echo "Usage: $0 --verify|--hash|--merkle <arg>" >&2
  exit "$ARGS_ERROR"
}

case "$1" in
  --verify)
    exec "$DIR/verify.sh" "$2"
    ;;
  --hash)
    exec "$DIR/hash.sh" "$2"
    ;;
  --merkle)
    exec "$DIR/merkle.sh" "$2"
    ;;
  *)
    echo "Invalid operation" >&2
    exit "$ARGS_ERROR"
    ;;
esac
