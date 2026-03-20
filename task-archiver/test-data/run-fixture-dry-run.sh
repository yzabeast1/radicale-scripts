#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
workspace_root="$(cd "$project_root/.." && pwd)"
cd "$workspace_root"
make task-archiver

"$workspace_root/build/task-archiver" \
  --source "$project_root/test-data/source" \
  --archive "$project_root/test-data/archive" \
  --days 30 \
  --dry-run
cd - >/dev/null
