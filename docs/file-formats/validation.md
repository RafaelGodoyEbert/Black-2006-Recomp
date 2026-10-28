# Evidence and validation for issue #42

Investigation date: **2026-10-27**. These results describe the local supported
Xbox retail corpus, not every release of BLACK. The repository starting revision
was `781447c`.

## Inputs and provenance

The supported `default.xbe` was verified against SHA-256:

```text
DF2739C372D254A90AEECCF5971D12097F22D89FBB90DB021FDA96DD4DEB68F6
```

The public links in
[issue #42](https://github.com/portingpete/Black-2006-Recomp/issues/42)
resolved to the following archives. Both passed 7-Zip's archive integrity check:

```text
black2006_3dmodels_xbox.rar
  size:   24,222,736 bytes
  SHA256: D0D5666DCC3D4D81A9C0A291418F7AF949C058A9B2DE78E1C5D9C615398796DF
  files:  52 BIN/DB files, 75,179,597 bytes unpacked

black2006_xbox_awd.rar
  size:   2,937,366 bytes
  SHA256: C519D980C2FD35924C567C01EB54FC54F9BCEB02F13C435E2DAAEE4B9B051669
  files:  Collide.awd and Level.awd, 3,942,400 bytes unpacked
```

All 52 model/archive samples matched their corresponding paths under the local
`game/levels/` by SHA-256. Both AWDs matched `game/levels/level_00/`:

```text
Collide.awd: 1FD031C23533A3ADA1DB698C415F3CDF85488F8725CF0565ED60A36F9777B6FE
Level.awd:   3C1310D5016B645315E9E2A5D18A2F97046FF8751919A099ECDA4EF7F3A54364
```

This establishes byte identity of those samples, not the identity of every
possible disc revision. Game bytes, unpacked resources, decoded WAVs, IDA
databases and decompilation were retained outside the repository and are not
included in these changes.

## Corpus coverage

The disc contained 132 BIN files, 139 DB files and 36 AWD files, 307 in total.
The reader recognized these layouts without structural errors:

- 139 standalone version-9 DBs.
- 42 version-0x18 Unit files and 42 version-3 StUnit wrappers.
- Eight version-0x11 LevelDat files and eight version-0x0A StLevel wrappers.
- 16 gun containers, one GlobData container and nine front-end MOVIEARRAY archives.
- Two fonts, the original English MainUS language bank and two transition tables.
- 36 AWD banks with 1,385 list entries: 1,372 PCM16LE and 13 Xbox IMA ADPCM.

The one explicitly unsupported file is `sound/dsp/BkXboxFx.bin`.
The 54 files supplied in the issue are included in this corpus, not additional
independent samples. Repeated files in different level directories are counted
as files, not unique payloads.

The recognized BIN/DB structures were checked for required versions and IDs,
root/table/entry bounds, relative-pointer bases and the top-mip/palette bounds
of textures reached by the documented paths. The reader does not recursively
validate every model or animation payload. Font glyph counts are reported from
the available record area; individual glyph metrics and lookup-table entries
are not validated by this tool. The later model/animation pass below extends
the resource checks without changing the 306 recognized-file count.

## Original executable evidence

Analysis used **IDA Pro 9.4**, observed IDAPython **3.13.12**, and the IDA Domain
API **0.5.1** on Windows x64. The game itself is Xbox x86, independently of the
analysis host. The IDA MCP worker could load the XBE but could not start its
session because the configured `ida-nexus` session module was missing. Analysis
continued against a copy using the installed IDA Python environment and its
supported `IDA_NEXUS_IDALIB_ONLY=1` mode. No user plugins were installed or changed.

The following are virtual addresses in the supported original XBE. Names were
not recovered; the descriptions are analysis labels, not original symbols.

- `0x000C08D0`: shared keyed-dictionary relocation. Adds the dictionary base to
  the table pointer and to the value field at `+08` in each 16-byte entry.
- `0x000B5E10`: version-9 DB root relocation. Adds the DB base to fields `+1C`
  through `+40`, and processes the resource dictionaries.
- `0x000B2940`: Xbox texture and palette pointer relocation. Pixel pointers are
  relative to the texture; palette header/data relocation uses the texture base.
- `0x000B6030`: StLevel wrapper relocation. Distinguishes the 40-byte stage-record
  array from the embedded DB and fixes record-relative fields.
- `0x001D3830`: StLevel file-completion callback that invokes that relocation path.
- `0x001DAF10`: StUnit file-completion callback. Relocates wrapper offsets `+04`
  and `+08`, then invokes DB relocation.
- `0x000B70E0`: LevelDat relocation, including the four dictionary roots at `+390`
  through `+39C` and earlier fields with distinct relocation bases.
- `0x001D3290` and `0x001D6200`: Unit relocation/loading. Confirm the 16-bit
  instance count at `+90`, 48-byte record stride and dictionary roots.
- `0x000B3490`: character-transition relocation. Confirms the three root arrays,
  48-byte record stride and record-relative field at `+28`.
- `0x001EB7F0`, `0x001EDC90`, and `0x0017C3D0`: path-selection/loading context
  for Unit/StUnit, LevelDat/StLevel and character-transition data respectively.

Decompiler output was used as private evidence; only original explanations and
addresses are documented here. No retail function bodies are copied into the
public reader or documents. The AWD runtime loader, GlobData's non-dictionary
sections and the complete model/animation formats were not fully traced.

## Independent references

The inspected source files were downloaded at these exact revisions. The pinned
versions were hash-checked against the initially inspected files:

- [EdnessP/scripts, 2ecf57078656f236c25640571ef301091da13197, fmt_Burnout3LRD.py](https://github.com/EdnessP/scripts/blob/2ecf57078656f236c25640571ef301091da13197/burnout/fmt_Burnout3LRD.py).
  Its `blkChk*`, `blkArcTex*`, `boArcTxdParse`, `boTexXbox` and front-end/font
  readers provided comparison points for dictionaries, wrapper texture roots,
  base-40 IDs and Xbox texture metadata. Its support list explicitly includes
  BLACK Xbox and PS2; that does not make the two platforms interchangeable.
- [vgmstream, d9875b1fff307e4759dfb85e49e3d5e6fb196f7c, awd.c](https://github.com/vgmstream/vgmstream/blob/d9875b1fff307e4759dfb85e49e3d5e6fb196f7c/src/meta/awd.c).
  Primary comparison for AWD headers, list traversal, descriptors and codec IDs.
- [vgmstream, same revision, ima_decoder.c](https://github.com/vgmstream/vgmstream/blob/d9875b1fff307e4759dfb85e49e3d5e6fb196f7c/src/coding/ima_decoder.c#L662).
  Xbox IMA block decoding and sample counts.
- [vgmstream release r2168](https://github.com/vgmstream/vgmstream/releases/tag/r2168).
  The official Windows x64 CLI was used for independent metadata comparison and
  two decoded audio samples; it is not bundled with this repository.
- Existing project [MainUS reader/writer](../../scripts/localization/mainus_bank.py)
  and [font reader](../../launcher/src/core/GameArt.cpp), as present at the
  investigation's starting repository revision.

An additional [community discussion of PS2 prototype DB/BIN data](https://reshax.com/topic/18615-ps2-criterion-games-black-aug-16-2005-prototype-db-bin/)
helped locate the original Noesis reader. Its prototype-specific mesh findings
were not used as a specification for Xbox retail meshes.

The Rust implementation is original code. These references are credited for
format research; their code and their decoder binaries have not been copied
into the repository.

## Audio cross-checks

For every AWD bank, vgmstream r2168's metadata was compared with the inspector's
sample count for the first and last entry. This produced **64 comparisons**;
the eight single-entry banks have only one distinct first/last entry. All sample
counts agreed. This checks representative list positions in every bank, not
independent decoding of every one of the 1,385 entries.

Two entries were decoded to WAV successfully:

- `sound/FEAudio.awd`, entry 1: mono PCM16LE, 44,100 Hz, 7,159 samples.
- `levels/level_00/stg_0001/Emitter.awd`, entry 1: mono Xbox IMA ADPCM,
  44,100 Hz, 320 samples from 180 encoded bytes.

Successful decoding establishes decoder acceptance and sample counts, not an
auditory evaluation or reproduction of in-game effects, pitch and loop behavior.

## Model and animation follow-up

This pass started from merged revision `2a7c4fd` on 2026-10-27 and reused the same
disc, issue samples and private IDA database. It adds original Rust readers in
`models.rs` and `animation.rs`; no additional external decoder or game asset is
bundled. The earlier community reader supplied context, but its Burnout model
support was not assumed to establish BLACK's model layout.

The expanded reader successfully checks the same 306 files, with the same DSP
image explicitly unsupported and no structural errors. It additionally reaches:

- 4,286 MODEL version-0x23 occurrences from MODELS/MYDICTIONARY, including 640
  with a nonzero model bone/name count.
- 10,037 buffer records and 10,133 draw descriptors. Every reached descriptor
  uses triangle-strip primitive 6. There are 4,944 stride-24, 4,587 stride-28 and
  506 stride-36 buffer occurrences. Every referenced XYZ position is finite
  and fits the physical input file. The largest reached index is 7,222.
- 180 version-21 RCBs: 172 dictionary values and eight embedded bundle RCBs.
  All have one skeleton header. The reader checks parent/index bounds, matrix
  spans and finite values, and sorted joint-hash lookup permutations.
- 211 version-8 banks: 163 BNKS values, 40 SHAREDBNKS values and eight embedded
  bundle banks. Associated RCB IDs resolve locally with consistent joint counts.
- 3,573 named state/root records, 4,013 encoding-4 clips and 459,470 track records
  including segmented children. Clip sample counts range from 2 to 321.
  The two-track-per-joint walks fit before each bank's relocation table.

These are occurrences across files, not unique assets. Repeated RCB identifiers
can occur within a file; the bank reader verifies that their joint counts agree.
It does not claim that matching a joint count establishes identical skeleton
transforms or that a bank can be paired with any model in that file.

Additional original-XBE evidence:

- `0x00014B50` iterates model dictionary values; `0x001640B0` relocates their
  root arrays, texture-name pointers, bone/name pointers and LOD fields.
- `0x001605D0` establishes the 28-byte buffer records, 48-byte draw descriptors
  and the different buffer/descriptor pointer bases.
- `0x00092B20` and the bank paths in `0x0017BD00` apply blob-relative fixup lists
  and mark the list processed by clearing its first entry.
- `0x000978E0` selects a 16-byte skeleton header. `0x000925F0` uses its count,
  8-byte parent/flags records and 64-byte matrix records.
- `0x0017AB40` binds model bone names through the RCB lookup. `0x0009CD30` uses
  multiplier 65599 and character case conversion; `0x00096630` compares hashes
  as unsigned values. Only the observed ASCII name behavior is implemented.
- `0x00092BA0` registers bank state/root pairs, clips and the third object
  category. It initializes a zero clip sample pointer to the inline data at +30.
- `0x0009B410` locates the sampling header. `0x00093CC0` dispatches encoding 4
  to the track path; `0x000964C0` reads rotation then translation per joint.
- `0x00095850` supplies track-size/segment rules; `0x000959A0` supplies the
  sampling cases used to identify their payload categories. The new tool walks
  boundaries, not the full interpolation algorithm.
- `0x000957D0` reads three float32 vector components. `0x000957F0` expands four
  packed 24-bit float components from twelve bytes. This is not a three-float
  quaternion reconstruction or a half-float format.

The main first-mission AK1 resource was additionally reconstructed privately
from indexed XYZ values into an orthographic wireframe and inspected. It gives
5,447 distinct referenced vertices and 5,580 nondegenerate triangles across its
two buffers, with coherent gun/arm geometry. This confirms a useful geometry
reading route, not material, normal, pose or skinning equivalence. The OBJ/PNG
remain outside the repository.

For that model, the 42 RCB joint hashes match 42 of the 44 model names. Its two
unmatched names are documented in [models.md](models.md). The model's relocated
`+58` word array was deliberately left raw: interpreting it as a parent hierarchy
produces self-parent entries. The verified RCB parent array is a different
structure.

All 54 issue-supplied samples are also checked with the expanded readers. The
test suite now includes synthetic malformed fixups, bounded tables, separate
pointer owners, triangle-strip degenerates, bone matrix bounds, missing skeleton
IDs, segment/quantized-track lengths and packed-component decoding. No native
animation-player oracle, new rendered animation, modified asset, writer or
in-game replacement was tested.

## Reproduce the structural checks

From the repository root:

```powershell
cargo test --locked --manifest-path tools/black-formats/Cargo.toml
cargo clippy --locked --manifest-path tools/black-formats/Cargo.toml --all-targets -- -D warnings
cargo run --release --locked --manifest-path tools/black-formats/Cargo.toml -- game > local/format-report.jsonl
```

The last command reads the user's own files and should report:

```text
306 parsed, 1 unsupported, 0 errors
```

For an individual file, pass its path instead of `game`. Every JSON object
includes the actual input hash and parse status so that a different corpus or
modified bank can be distinguished. Unsupported layouts are explicit; they
do not count as successful parses. Selected-layout structural errors cause a
nonzero exit code. See the [inspector README](../../tools/black-formats/README.md)
for its exact validation scope.

## Remaining work and writer requirements

- Complete MODEL material programs, normal/UV/color/weight layouts, skinning,
  exact payload lengths and matrix conventions. Indexed positions and metadata
  are now readable, but are not a complete posed model exporter.
- Decode CHARACTERDIC/ACTORDICTION and the full state/event/parameter graphs in
  BNKS/SHAREDBNKS, validate interpolation and timing, and establish animation
  playback equivalence. RCB skeletons, bank fixups and track boundaries are now
  documented; a bounds walk alone is not a verified animation player.
- Resolve DB fields `+38..40`, negative serialized placeholders and the remaining
  StLevel/Unit/LevelDat object and gameplay data.
- Decode GlobData's four non-dictionary sections, transition-record semantics,
  APT front-end bytecode, complete texture mip metadata and the DSP image.
- Trace AWD runtime flags, loop/event relationships and any additional platforms
  or codec variants before extending this Xbox-only reader.
- Before shipping an asset writer, preserve unknown data, establish exact
  subresource boundaries, perform byte/semantic round trips on stock samples,
  and validate one changed resource in the game. No writer, rebuilt asset or
  in-game replacement was tested as part of this documentation task.
