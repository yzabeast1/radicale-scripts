#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$project_root"

output="$(./test-data/run-fixture-dry-run.sh)"
printf '%s\n' "$output"

expected_moved=(
  "normal-standalone-completed.ics"
  "normal-hierarchy-root-completed.ics"
  "normal-hierarchy-child-completed.ics"
  "normal-hierarchy-grandchild-completed.ics"
)

expected_stay=(
  "ancestor-parent-completed.ics"
  "ancestor-child-completed.ics"
  "descendant-root-completed.ics"
  "descendant-child-completed.ics"
  "recent-linked-old-completed.ics"
  "recent-linked-new-completed.ics"
)

for file in "${expected_moved[@]}"; do
  if ! grep -q "Would move: .*${file}" <<< "$output"; then
    echo "ASSERTION FAILED: expected moved file missing from dry-run output: ${file}" >&2
    exit 1
  fi
done

for file in "${expected_stay[@]}"; do
  if grep -q "Would move: .*${file}" <<< "$output"; then
    echo "ASSERTION FAILED: blocked file unexpectedly present in move output: ${file}" >&2
    exit 1
  fi
done

if ! grep -q "Scanned: 12 .ics files" <<< "$output"; then
  echo "ASSERTION FAILED: expected fixture scan count missing" >&2
  exit 1
fi

if ! grep -q "Moved:   4" <<< "$output"; then
  echo "ASSERTION FAILED: expected moved count missing" >&2
  exit 1
fi

echo "Fixture assertions passed."
