# CalDAV Task Archiver (C++)

Moves completed CalDAV task files (`.ics`) from a source folder into an archive folder when they were completed more than `N` days ago.

## Build

```bash
make
```

## Usage

```bash
./caldav_task_archiver \\
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
- Completion date is read from the `COMPLETED:` property (supports date/datetime values like `20260301` or `20260301T101500Z`, date portion used).
- The relative folder structure under `--source` is preserved under `--archive`.
- If a direct rename fails (for example across filesystems), the tool falls back to copy-then-remove.
