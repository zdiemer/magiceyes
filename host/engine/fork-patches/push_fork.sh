#!/bin/bash
# Push the local Unicorn fork's `magiceyes` branch to the GitHub fork.
#
# The repo github.com/zdiemer/unicorn already exists (created via `gh repo fork`).
# ONE-TIME: the gh OAuth token needs the `workflow` scope, because unicorn's history
# contains .github/workflows/*.yml and GitHub refuses pushes touching them otherwise:
#     gh auth refresh -h github.com -s workflow      # (interactive, opens browser)
# (or configure an SSH key for github.com and use the git@ remote.)
# Then run this script.  Env: ME_UNICORN_FORK (default ~/me-unicorn-fork).
set -u
FORK="${ME_UNICORN_FORK:-$HOME/me-unicorn-fork}"
cd "$FORK" || exit 1
[ -f .git/shallow ] && git fetch --unshallow origin
git remote remove fork 2>/dev/null || true
git remote add fork https://github.com/zdiemer/unicorn.git
# gh.exe (Windows GitHub CLI) as the credential helper works from WSL:
git -c credential.helper='!gh.exe auth git-credential' push -u fork magiceyes:magiceyes
