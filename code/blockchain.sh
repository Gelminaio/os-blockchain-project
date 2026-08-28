#!/bin/bash
# entry point for the bash side: --verify, --hash, --merkle

# the helpers are looked up next to this script, so we cd here. Resolve the
# argument against the caller's directory FIRST, otherwise a relative path
# would be searched inside code/ instead of where the user actually is.
CALLER_DIR="$PWD"

cd "$(dirname "$0")"

source scripts/errors.sh

[[ $# -eq 2 ]] || {
  echo "Usage: $0 --verify|--hash|--merkle <arg>" >&2
  exit "$ARGS_ERROR"
}

arg="$2"

# --verify takes a file: make a relative path absolute before we lose the cwd.
# --hash and --merkle take plain strings, leave them alone.
if [[ "$1" == "--verify" && "$arg" != /* ]]; then
  arg="$CALLER_DIR/$arg"
fi

case "$1" in
  --verify)
    exec scripts/verify.sh "$arg"
    ;;
  --hash)
    exec scripts/hash.sh "$arg"
    ;;
  --merkle)
    exec scripts/merkle.sh "$arg"
    ;;
  *)
    echo "Invalid operation" >&2
    exit "$ARGS_ERROR"
    ;;
esac
