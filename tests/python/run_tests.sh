#!/bin/bash
# Run the Python unit tests. Mirrors tests/c/build_tests.sh for the other half of the suite.
#
# pytest and numpy are TEST-time dependencies only: the modules under tools/test stay pure stdlib
# (CI runs them directly), so nothing here is added to their imports. uv fetches both into an
# ephemeral environment, which is why there is no requirements file to keep in sync.
#
# Env: ME_UV  path to uv (default: whichever is on PATH, else ~/.local/bin/uv)
# Args: passed through to pytest (e.g. `run_tests.sh -k pilot -v`).
set -eu
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO"

UV="${ME_UV:-}"
if [ -z "$UV" ]; then
  if command -v uv >/dev/null 2>&1; then UV="$(command -v uv)"
  elif [ -x "$HOME/.local/bin/uv" ]; then UV="$HOME/.local/bin/uv"
  else
    echo "uv not found -- install it with:  curl -LsSf https://astral.sh/uv/install.sh | sh"
    exit 1
  fi
fi

exec "$UV" run --quiet --with pytest --with numpy pytest "$@"
