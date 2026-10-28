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
  bases, keys and raw reserved words. Known model, skeleton and bank values gain
  a `resources` array; other values remain uninterpreted.
- MODEL version 0x23: 28-byte buffer records, 48-byte triangle-strip descriptors,
  24/28/36-byte vertex strides, index spans, referenced XYZ positions and bounds,
  model matrices/name arrays, raw auxiliary index words, texture names and LOD
  targets. Normals, UV/color/weight layouts, complete material programs and
  skinning are not decoded. Position spans use file bounds; a model's complete
  serialized length has not been established.
- RCB version 21: relocation tables, the checked single-skeleton layout,
  parent/flags records, finite 64-byte matrices and sorted joint-hash lookups.
- Bank version 8: relocation tables, named state/root pairs, clip headers and
  encoding-4 track boundaries. Joint counts come from matching local RCB IDs,
  including embedded ANIMDICTIONA bundles; missing/ambiguous counts are errors.
  Segmented tracks are walked recursively with a depth limit of 64. Constant
  first-joint values are previewed, including four packed 24-bit float rotation
  components. The tool does not play animations or decode state-graph opcodes.
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
payload bounds and truncation, model buffer/descriptor bases and indexed
positions, animation fixups, skeleton lookup bounds, track lengths, segment
nesting and packed rotation components. The documented retail scan is a separate,
locally reproduced check; no retail corpus is bundled or required by unit tests.

See [validation.md](../../docs/file-formats/validation.md) for the tested corpus,
independent source revisions, native-loader addresses and remaining unknowns.
