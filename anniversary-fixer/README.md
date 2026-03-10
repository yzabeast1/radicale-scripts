# Anniversary Fixer (C++)

Normalizes anniversary fields in vCard contact files (`.vcf`) inside a folder.

## Build

```bash
make anniversary-fixer
```

## Usage

```bash
./anniversary-fixer --contacts-folder /path/to/contacts-folder
```

## Rules Applied

- If a contact has `ANNIVERSARY` but no `item1.X-ABDATE`, add:
  - `item1.X-ABDATE:<anniversary-value formatted as YYYY-MM-DD>`
  - `item1.X-ABLABEL:_$!<Anniversary>!$_`
- If a contact has `item1.X-ABDATE` but no `ANNIVERSARY`, add:
  - `ANNIVERSARY:<item1-date-value formatted as YYYY-MM-DD>`
- If both exist and values match, leave unchanged.
- If both exist and differ, trust `item1.X-ABDATE` and overwrite `ANNIVERSARY`.
- Output is normalized so `ANNIVERSARY` uses `YYYY-MM-DD` and `item1.X-ABDATE` uses `YYYY-MM-DD`.

Only `.vcf` files in the given folder are processed.
