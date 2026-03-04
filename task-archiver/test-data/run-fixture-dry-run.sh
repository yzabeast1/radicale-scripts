#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"
make
"$repo_root/task-archiver" \
  --source "$repo_root/test-data/source" \
  --archive "$repo_root/test-data/archive" \
  --days 30 \
  --dry-run
cd - >/dev/null