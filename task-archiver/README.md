# CalDAV Task Archiver (C++)

Moves completed CalDAV task files (`.ics`) from a source folder into an archive folder when they were completed more than `N` days ago.

## Build

```bash
make task-archiver
```

## Usage

```bash
./task-archiver/task-archiver \\
  --source /path/to/caldav/tasks \
  --archive /path/to/archive \
  --days 30
```

### Options

- `--source <path>`: folder containing task `.ics` files
- `--archive <path>`: folder to move old completed tasks into
- `--days <N>`: move only if completed **more than** `N` days ago
- `--dry-run`: print planned moves without changing files
- `--no-recursive`: only scan top-level files (default is recursive)
- `--include-hidden`: include hidden files/folders (default is to ignore hidden paths such as `.Radicale.cache`)

## Notes

- A task is treated as complete when `STATUS:COMPLETED` exists.
- Completion date is read from the `COMPLETED:` property.
- UTC completion timestamps such as `20260301T101500Z` are converted into the current local day using the process time zone from `TZ` before age is calculated.
- If `TZ` is unset, the process falls back to the system local time zone.
- A completed task is archived only when all known tasks in its connected `RELATED-TO` tree are also old enough to pass the same `--days` threshold.
- The relative folder structure under `--source` is preserved under `--archive`.
- If a direct rename fails (for example across filesystems), the tool falls back to copy-then-remove.

## Fixture test data

Fixture `.ics` files are included under `test-data/source`.

Run a dry run against them:

```bash
make task-archiver

./task-archiver/task-archiver \
  --source test-data/source \
  --archive test-data/archive \
  --days 30 \
  --dry-run

# or use the helper script
./test-data/run-fixture-dry-run.sh
```

Expected to be moved:

- `normal-standalone-completed.ics`
- `normal-hierarchy-root-completed.ics`
- `normal-hierarchy-child-completed.ics`
- `normal-hierarchy-grandchild-completed.ics`

Expected to stay (blocked by incomplete relatives):

- `ancestor-parent-completed.ics`
- `ancestor-child-completed.ics`
- `descendant-root-completed.ics`
- `descendant-child-completed.ics`
- `recent-linked-old-completed.ics` (blocked by connected recent completion)
- `recent-linked-new-completed.ics` (not old enough yet)
