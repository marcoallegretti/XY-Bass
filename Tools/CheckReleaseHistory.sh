#!/usr/bin/env bash
set -euo pipefail

fail() { echo "$1" >&2; exit 1; }

SOURCE_DIR=$(cd "$(dirname "$0")/.." && pwd)

# Without this the greps below read an empty history and report success.
git -C "$SOURCE_DIR" rev-parse --git-dir >/dev/null 2>&1 || fail "Not a git repository: $SOURCE_DIR"

if git -C "$SOURCE_DIR" log --name-only --pretty=format: | grep -qiE '\.md$'; then
    fail "Release history contains Markdown files"
fi

if git -C "$SOURCE_DIR" log --format='%B' \
    | grep -qiE '^co-authored-by:|generated-by:|(^|[^[:alnum:]])(llm|claude|codex)([^[:alnum:]]|$)'; then
    fail "Release history contains prohibited message metadata"
fi

echo "Release history is clean"
