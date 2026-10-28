# BLACK format inspector

An original, read-only Rust reader for the Xbox layouts in
[docs/file-formats](../../docs/file-formats/README.md). It produces JSON Lines;
it does not extract retail payloads, write game assets or start the game.

Requires Rust 1.85 or newer. From the repository root:

```powershell
cargo test --locked --manifest-path tools/black-formats/Cargo.toml
cargo run --release --locked --manifest-path tools/black-formats/Cargo.toml -- game > local/format-report.jsonl
cargo run --release --locked --manifest-path tools/black-formats/Cargo.toml -- game/levels/level_00/fpguns/bg1_ak1.db
```

One or more file/directory arguments are accepted. Directory scans select
`.bin`, `.db` and `.awd` case-insensitively and skip symbolic links. Explicit
symbolic-link arguments are rejected. Files larger than 512 MiB are rejected
before reading. Output is written to stdout; summary/errors go to stderr.

Each report contains `path`, `bytes`, `sha256`, `status` and either `layout` or
`error`. Status `ok` means the selected reader's structural checks passed.
`unsupported` explicitly identifies an uninterpreted file. Structural or I/O
errors cause exit 1; the presence of unsupported layouts alone does not.

Several layouts have no reliable magic, so their reader is selected by basename
or extension, then checks the required version, root IDs and bounds. Preserve
the original basename for BIN files. A recognized filename alone is never a
successful parse. PS2, big-endian AWD and undocumented versions are unsupported.

## Validation scope

- Shared keyed dictionaries: header IDs, counts, table and value bounds, owning
  bases, keys and raw reserved words. Values other than textures are not decoded.
- DB, Globals, Unit/StUnit, LevelDat/StLevel and gun wrappers: the documented
  version and selected root fields. Opaque sections and placeholder fields are
  either reported raw or left uninterpreted.
- Xbox textures: selected metadata, top-mip byte span and palette data bounds.
  Mip chains, pixels, palette colors and rendered appearance are not validated.
- Front-end archives: movie names and reachable texture headers, not APT scripts.
- Fonts: atlas and potential record count, not glyph lookup/metric correctness.
- MainUS: pointer order/bounds, record counts, unique IDs, UTF-8, terminators and
  sequential tail records. No replacement text or language bank is written.
- Transition tables: root arrays, name pointers, 48-byte records and nonzero
  record-relative `+28` fields. Transition behavior is not interpreted.
- AWD Xbox: platform/header, circular list including backward links, names,
  descriptor bounds, source codec/rate/channel metadata and stream ranges. PCM
  sizes must be even; Xbox IMA sizes must contain complete 36-byte mono blocks.
  Encoded audio is not decoded and target/runtime fields are not fully validated.

The synthetic tests contain no game assets. They exercise malformed dictionary
tables, dictionary-relative addressing, AWD cycles/backlinks, bounded names,
payload bounds and truncation. The documented retail scan is a separate,
locally reproduced check; no retail corpus is bundled or required by unit tests.

See [validation.md](../../docs/file-formats/validation.md) for the tested corpus,
independent source revisions, native-loader addresses and remaining unknowns.
