#!/bin/bash
# Entry point for the magiceyes MCP server (launched from .mcp.json via wsl.exe).
#
# This is a script FILE on purpose: `wsl.exe ... bash -lc '...'` mangles inline variables and
# VAR=/path assignments through MSYS path conversion, so all logic must live in a file
# (CLAUDE.md "Dev environment & gotchas").
#
# stdout is the MCP stdio transport -- it carries JSON-RPC and NOTHING else. Every diagnostic here
# must go to stderr or it will corrupt the protocol.
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
export PATH="$HOME/.local/bin:$PATH"

if ! command -v uv >/dev/null 2>&1; then
    echo "magiceyes-mcp: uv not found. Install it with:" >&2
    echo "  curl -LsSf https://astral.sh/uv/install.sh | sh" >&2
    exit 1
fi

# Keep the venv on ext4. When the repo lives on drvfs, venv creation is slow and the
# engine's exe-adjacent cache costs ~20% throughput (see CLAUDE.md).
export UV_PROJECT_ENVIRONMENT="${UV_PROJECT_ENVIRONMENT:-$HOME/.magiceyes/mcp-venv}"

# The corpus is a NETWORK share, so WSL does not auto-mount it and the mount does not survive a WSL
# restart. Ensure it here rather than making every tool call fail with a confusing ENOENT.
# Non-fatal: the legacy F: corpus and any explicit path still work without it.
# The share names a private host: keep it in the environment or tools/local.env, never in git.
[ -f "$HERE/../local.env" ] && . "$HERE/../local.env"
if [ ! -d /mnt/s/GP2X ] && [ -n "${MAGICEYES_CORPUS_UNC:-}" ]; then
    sudo mkdir -p /mnt/s 2>/dev/null
    if sudo mount -t drvfs "$MAGICEYES_CORPUS_UNC" /mnt/s 2>/dev/null; then
        echo "magiceyes-mcp: mounted the corpus share at /mnt/s" >&2
    else
        echo "magiceyes-mcp: could not mount /mnt/s (corpus tools limited to the local corpus)" >&2
    fi
fi

exec uv run --project "$HERE" --quiet magiceyes-mcp
