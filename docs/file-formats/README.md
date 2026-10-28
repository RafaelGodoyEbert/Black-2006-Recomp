# BLACK Xbox file formats

This documents the `.bin`, `.db` and `.awd` files requested in
[issue #42](https://github.com/portingpete/Black-2006-Recomp/issues/42).
It covers the **supported original Xbox retail release**. The issue's two
downloads were inspected: all 52 BIN/DB samples and both AWD samples are
byte-identical to files from that release. See [validation and sources](validation.md).

- [BIN and DB containers, dictionaries and textures](bin-db.md)
- [AWD wave dictionaries and audio payloads](awd.md)
- [Evidence, coverage, reproduction and remaining work](validation.md)
- [Read-only Rust inspector](../../tools/black-formats/README.md)

These are partial specifications with usable, tested layouts. They describe
resource lookup, container boundaries, texture metadata and audio extraction.
They do **not** constitute a complete model, animation, collision, scripting or
asset-authoring specification. Unknown fields are identified rather than assigned
invented meanings. A successful parse validates the structures listed here, not
every byte or the game's acceptance of an edited file.

## Conventions and evidence

Offsets are hexadecimal byte offsets. `u16`, `u32` and `u64` are unsigned
little-endian integers; `f32` is an IEEE 754 float. Xbox files use 32-bit serialized
pointers, even when a host tool runs in 64-bit mode. A relative pointer must be
added to the **stated owner base**, not automatically to the pointer field or
the beginning of the file. Checked addition and bounds checks are required.

**Verified** means the structure was checked against both the stock corpus and
the original Xbox loader in IDA. **Observed** means it was checked in stock bytes,
sometimes with an independent community reader, but its complete runtime meaning
has not been established. **Unknown** means no specification is claimed.
Native addresses and source revisions are recorded in [validation.md](validation.md).

The `.bin` extension has no universal signature or layout. The front-end archive,
font, language bank, level data, transition table and audio DSP program are
different formats. `.db` here means a Criterion resource container, not SQLite.
AWD is RenderWare's Audio Wave Dictionary, not EA's SSH/BKS streamed-audio pair.

## Inspect your own files

From the repository root, with Rust 1.85 or newer:

```powershell
cargo test --locked --manifest-path tools/black-formats/Cargo.toml
cargo run --release --locked --manifest-path tools/black-formats/Cargo.toml -- game > local/format-report.jsonl
```

The inspector writes JSON Lines containing file sizes, SHA-256 hashes, recognized
layouts, dictionary entries, texture metadata and AWD stream information. It reads
assets without launching the game or modifying them. No retail data, generated
game code or third-party decoder is included. Reports can contain resource names
from the user's files; inspect them before sharing.

The reference scan covered 307 files: 306 recognized layouts, zero structural
errors, and one explicitly unsupported DSP program (`sound/dsp/BkXboxFx.bin`).
PS2, prototypes, other Xbox revisions, other RenderWare games and unsupported
versions need separate validation; similar filenames are not evidence of identical
layouts.
