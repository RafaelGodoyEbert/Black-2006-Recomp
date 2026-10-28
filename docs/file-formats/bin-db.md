# BIN and DB containers

Scope and confidence conventions are in the [index](README.md). The shared
dictionary and the pointer relocation rules below are verified by the Xbox loader;
the higher-level meanings of opaque payloads remain unknown. Community source
references are pinned in [validation.md](validation.md).

## Shared keyed dictionary

At dictionary base `D`:

```text
D+00  u64  dictionary GtID
D+08  u32  entry count N
D+0C  u32  entry-table offset, relative to D

entry size = 0x10 bytes
E+00  u64  lookup key
E+08  u32  value offset, relative to D
E+0C  u32  reserved/unknown; zero in the checked dictionaries
```

Compute `T = D + u32(D+0C)`, then read exactly `N` records at `T + 0x10*i`.
The value is `D + u32(E+08)`, **not** `E + u32(E+08)`. The original relocation
function at Xbox VA `0x000C08D0` performs both additions with the dictionary base.
`T-D` is often `0x80` in DBs and `0x10` in other containers. Read the field;
neither value is a universal header size.

Zero entries are valid. An empty dictionary can point just past its 16-byte
header or to an aligned location. A key can be an integer rather than a name:
texture dictionaries commonly use `1, 2, 3, ...`. The reserved word does not
provide a resource length. Resource values are not guaranteed to be ordered
by physical offset; do not calculate sizes by blindly subtracting adjacent
entries or assume the final entry runs to the end of the file.

### GtIDs

Named keys and dictionary IDs use a base-40 encoding. The alphabet, in digit
order, is:

```text
 -/0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_
```

Decode a `u64` into exactly 12 digits by repeatedly taking remainder modulo 40,
filling the output from right to left, and dividing by 40. Remove trailing spaces.
Reject a value with a nonzero remainder after 12 digits. Encoding performs the
inverse multiply-and-add, with names right-padded to 12 characters. This is not
a cryptographic hash. Arbitrary numeric keys must not be presented as decoded names.

Useful dictionary IDs, shown as numeric `u64` values rather than on-disk byte order:

```text
BCDEED81543C0000  TEXDIC
BCDF4EB7DD9A8000  TEXTURE
BCDF4EB7E02064D5  TEXTUREDICTI
95820AF2485C0000  MODELS
5A459D036341C357  CHARACTERDIC
4DF8C073A8963792  ACTORDICTION
555C57E557000000  BNKS
B767793C8161D7C0  SHAREDBNKS
B0DC09942F000000  RCBS
96F6A8E27244EA15  MYDICTIONARY
4F883C088278AEDD  ANIMDICTIONA
```

The truncated spellings are the actual twelve-character GtID names. In particular,
`TEXDIC`, `TEXTURE` and `TEXTUREDICTI` are distinct IDs, despite sharing the
dictionary layout. These ID names also appear in Edness's Noesis reader.

## DB version 9

For a standalone `levels/level_XX/fpguns/*.db`, the owning base `B` is file offset
zero. An embedded DB uses the same layout with a different `B`.

```text
B+00  u64  resource identifier 0
B+08  u64  resource identifier 1
B+10  u64  resource identifier 2
B+18  u32  version = 9
B+1C  u32  TEXTURE dictionary offset
B+20  u32  MODELS dictionary offset
B+24  u32  CHARACTERDIC dictionary offset
B+28  u32  ACTORDICTION dictionary offset
B+2C  u32  BNKS dictionary offset
B+30  u32  SHAREDBNKS dictionary offset
B+34  u32  RCBS dictionary offset
B+38  u32  additional field; payload meaning unknown
B+3C  u32  additional field; payload meaning unknown
B+40  u32  additional field; payload meaning unknown
```

The seven dictionary offsets are relative to `B`; dictionary-internal offsets
are subsequently relative to that dictionary. Native `0x000B5E10` relocates the
root fields from `+1C` through `+40`. The last three fields are not additional
standard dictionaries: standalone weapon DBs contain further data there, while
embedded DBs can contain values such as `0xFF400000`. Their semantics and these
serialized placeholders have not been resolved. The inspector preserves the
scope boundary by following only the seven verified dictionary roots.

The three initial identifiers are observed GtIDs in weapon DBs; their exact roles
are not established. Embedded DBs may have zeros there. For `bg1_ak1.db` they
decode to `BG1_AK1LF`, `BG1_AK1SLF` and `BG1_AK1`.

Example from the checked first-mission `bg1_ak1.db`:

```text
file size                  792567 bytes
TEXTURE dictionary         0x00000080, 1 entry
MODELS dictionary          0x00020580, 2 entries
  BG1_AK1_SPR value         0x00020680
  BG1_AK1 value             0x00022A00
CHARACTERDIC dictionary    0x00020200
ACTORDICTION dictionary    0x00020400
BNKS dictionary            0x00087E80
SHAREDBNKS dictionary      0x000AA100, 0 entries
RCBS dictionary            0x000AA180
```

The file can have an unaligned final size even though many internal structures
are aligned to `0x80`. This is not a sequence of standard RenderWare chunk
headers. Locating the `MODELS` dictionary is sufficient to enumerate model
resources. The [model follow-up](models.md) now describes the verified headers,
vertex/index records and bone metadata. Complete skinning and material programs
remain unresolved.

All 139 standalone DBs in the reference corpus use this version and pass root,
dictionary, entry and texture bounds checks. Community support for other DB
versions does not establish compatibility with this specification.

## BIN wrapper families

### GlobData.bin, version 9

```text
00  u32  version = 9
04  u32  TEXDIC dictionary offset, file-relative
08  u32  MYDICTIONARY offset, file-relative
0C  u32  other section offset, file-relative; payload unknown
10  u32  other section offset, file-relative; payload unknown
14  u32  other section offset, file-relative; payload unknown
18  u32  other section offset, file-relative; payload unknown
```

The reference file has 64 textures at `0x80` and 28 `MYDICTIONARY` entries at
`0x18E080`. The other roots are `0x1C1400`, `0x1BF100`, `0x1C1C80`, and
`0x1C2400`, respectively. They do not all start with the keyed-dictionary header.
The inspector checks their starting bounds and reports their first word without
claiming to decode those payloads. This header is observed and agrees with the
community texture lookup; it has not been fully traced through the global loader.

### guns.bin and guns_s.bin

```text
00  u32  TEXTURE dictionary offset, file-relative
04  u32  MODELS dictionary offset, file-relative
08..     other wrapper fields, meaning unknown
```

There is **no leading version field** in this wrapper. In first-mission
`guns.bin`, the texture dictionary is at `0x23000` and the model dictionary is
at `0x80`. Both sections use the shared dictionary layout. The 16 checked files
include `guns.bin` and `guns_s.bin` for eight levels. The suffix's complete runtime
meaning is not specified here.

### LevelDat.bin, version 0x11

```text
000  u32  version = 0x11 (17 decimal)
390  u32  dictionary offset, file-relative; TEXDIC in the reference corpus
394  u32  TEXDIC dictionary offset, file-relative
398  u32  TEXDIC dictionary offset, file-relative
39C  u32  MYDICTIONARY offset, file-relative
```

These IDs were observed in all eight checked files. The four roots and their
actual IDs are reported by the inspector; the roles of the three texture
dictionaries are not assigned here.
The first-mission texture dictionary starts at `0x400`. The native relocation
routine `0x000B70E0` treats these late header fields as file-relative pointers.
It also relocates many earlier fields using different bases; for example, raw
`u32(08)` becomes `file_base + 04 + raw_value`, and raw `u32(20)` becomes
`file_base + 20 + raw_value`. Do not apply the dictionary-root rule to the entire
level header.

The numerous other fields describe level-specific data whose complete schema
has not been established. This specification does not claim to decode level
logic, collision or every object. The older community texture offset `0x2BC`
belongs to a different version (`0x0A`), not the checked version `0x11`.

### Unit_XX.bin, version 0x18

```text
00  u32  version = 0x18 (24 decimal)
1C  u32  instance-record array offset, file-relative
20  u32  MYDICTIONARY offset, file-relative
24  u32  TEXTUREDICTI offset, file-relative
28  u32  ANIMDICTIONA offset, file-relative
90  u16  instance-record count
```

Instance records have a `0x30`-byte stride. This stride, the count and their
relocation are verified in native routines `0x001D3290` and `0x001D6200`.
Their full field meanings are not specified. The first-mission unit has its
texture dictionary at `0x670`. There are 42 checked unit files. Other header
pointers and resource-specific payloads remain outside the reader's scope.

### StUnitXX.bin, version 3

```text
00  u32  wrapper version = 3
04  u32  other region offset, file-relative
08  u32  embedded DB offset B, file-relative
```

Read the version-9 DB at `B`. Its texture dictionary is at `B + u32(B+1C)`;
the other six DB roots use the same base. Native `0x001DAF10` relocates the two
wrapper offsets and calls the DB relocation path. In the first-mission sample,
`B=0x80`, so the texture dictionary is at `0x100`. There are 42 checked files.
The `+04` region is not decoded by the reference inspector.

### StLevel.bin, version 0x0A

```text
00  u32  wrapper version = 0x0A (10 decimal)
04  u32  stage-record array offset, file-relative
08  u32  stage-record count
0C  u32  embedded DB offset B, file-relative
10  u32  additional dictionary/data offset, file-relative; not decoded
14  u32  optional region; not decoded
18  u32  optional region; not decoded
```

The stage records have a `0x28`-byte stride. Their first `0x10` bytes contain
an observed fixed-width name and `+10` contains a GtID. Native `0x000B6030`
relocates fields `+18` and `+1C` of each record relative to the **record**.
The stored values can include negative placeholders, so their raw values must
not all be treated as valid file offsets. Their runtime roles remain unknown.

The embedded DB's texture dictionary uses `B + u32(B+1C)`. For the first mission,
`B=0xA00` and its texture dictionary is at `0xA80`.

An important distinction from the community texture reader: the outer `+04`
field points to the stage-record array, not the texture dictionary. Both it and
the embedded DB's texture offset happen to equal `0x80` in these files. Adding
outer `+04` to `B` therefore finds the right texture bytes by coincidence.
Native code establishes the fields' distinct meanings. All eight wrappers were
checked using the embedded DB's own field, not that shortcut.

## Xbox texture resources

The texture dictionary value points at a texture header `X`. The checked files
use the newer Xbox layout:

```text
X+00  u32       native resource flags/type; checked value 0x00040001
X+04  u32       pixel-data offset, relative to X
X+14  u32[]     palette-header offsets, relative to X, when paletted
X+34  u32       Xbox texture format
X+38  u32       width
X+3C  u32       height
X+44  char[32]  texture name, NUL-terminated or bounded by the field
X+64  u8        bit depth
X+65  u8        additional metadata; meaning not verified here
X+66  u8        palette count
```

Pixel and palette relocation is independently verified in native `0x000B2940`.
The bits in the native resource word affect physical-address masking at runtime;
an offline reader should retain serialized offsets and not copy live GPU addresses
into files. Do not use `X+80` as a universal pixel-data address: follow `X+04`.

Observed pixel formats:

- `0x0B`: paletted, Morton-swizzled indices. The community reader reads one index
  byte per pixel; palette entries are BGRA8. Palette depth 4 selects 64 palette
  bytes and depth 8 selects 1024. **Do not infer packed 4-bit pixel indices solely
  from the depth field.** The checked paletted textures report depth 8.
- `0x0C`: DXT1/BC1, 8 bytes per 4-by-4 block.
- `0x0E`: DXT3/BC2, 16 bytes per 4-by-4 block.
- `0x0F`: DXT5/BC3, 16 bytes per 4-by-4 block.
- `0x3A`: 32-bit uncompressed pixels in the community reader, Morton-swizzled.
  A precise channel-order export convention has not been independently validated
  by the new inspector.

For DXT textures the top mip requires `ceil(width/4) * ceil(height/4)` blocks.
The inspector checks that footprint against the file. It does not validate every
lower mip, unswizzle pixels or export images. For `0x0B`, a palette header begins
with observed native resource words `0x00030001` or `0xC0030001`; its `+04`
pixel/palette-data offset is still relative to the **texture X**, not the palette
header. Other resource/header words have not been fully assigned meanings.

Edness's reader also handles an older texture layout with a 32-bit bit depth at
`X+40`, a name at `X+48` and metadata at `X+68`. The inspector recognizes that
variant, but the checked BLACK corpus uses the newer offsets. Supporting an old
layout in code is not evidence of testing it on BLACK files from another release.

## Other BIN files

### Front-end MOVIEARRAY archives

The nine `export/frontend/*.bin` files begin with GtID
`0x9592E53726307940` (`MOVIEARRAY`):

```text
00  u64  MOVIEARRAY
08  u32  movie/directory count
0C  u32  pointer-array offset, file-relative

pointer-array element: u32 file-relative entry offset
entry+00: u32 file-relative name offset
entry+04: u32 file-relative movie-data base M

M+04: u32 texture count
M+08: u32 texture-pointer-array offset, relative to M
M+0C: u32 APT data offset, relative to M
M+10: u32 APT constants offset, relative to M
```

Each texture-array element is a `u32` offset relative to `M`, pointing at the Xbox
texture header described above. Names are NUL-terminated. These layouts are
observed and match the community reader. The inspector enumerates movie names
and textures; it does not interpret APT bytecode, navigation or animation.

### Bitmap fonts: Small.bin and Big.bin

The first four bytes are ASCII `4.1v` (the little-endian spelling of the version
tag). At `+04` is a file-relative texture-header offset. The current launcher
reader in [GameArt.cpp](../../launcher/src/core/GameArt.cpp) additionally uses:

```text
00C  f32  atlas-height / nominal-size ratio
01C       beginning of the 128-entry glyph-pointer table
220       beginning of glyph records, stride 0x28

glyph+00  f32  atlas U
glyph+04  f32  atlas V
glyph+08  f32  cell width
glyph+0C  f32  cell height
glyph+10  f32  horizontal offset
glyph+14  f32  vertical offset
glyph+18  f32  advance
glyph+1C  u16  character code
glyph+1E..27   not interpreted here
```

The float dimensions/offsets are normalized to atlas dimensions in that reader.
The atlas uses the Xbox texture layout. The generic format inspector validates
the atlas and reports the potential record count; it does not validate the glyph
table or every glyph metric. See the launcher's parser for its rendering policy.

### Language strings: MainUS.bin

The existing [mainus_bank.py](../../scripts/localization/mainus_bank.py) reader
and writer establish this layout:

```text
00  u16  indexed-record count I
02..07   preserved prefix bytes; complete meaning unknown
08  u32  total-record count N
0C  u32  file-relative indexed-record pointer table

record+00  u32    string ID
record+04  byte[] UTF-8 text followed by NUL
```

The `I` pointer-table entries are file-relative `u32`s in increasing physical
offset order. After the last indexed record, the remaining `N-I` records occur
sequentially on 4-byte boundaries. Align the next record's start, not the string
length alone. The last record need not end at an aligned file size.

The stock English bank has `I=776`, `N=1724`, a table at `0x10`, and SHA-256
`B23EEB4AD86A72894B40E7239D0EA8842E5A6BC05936A936401B608070E8C7BB`.
`MainUS.bin` must not be identified by a fixed eight-byte magic: the leading count
changes when the search index is rebuilt. The project can index every record,
sorting IDs as signed 32-bit values; see the existing writer. This task did not
change or retest the existing language writer's game behavior.

### Character transition tables: trans_ch.bin and trans_fp.bin

Verified relocation at native `0x000B3490`:

```text
00  u32  record count N
04  u32  name-pointer array offset, file-relative
08  u32  per-record byte-array offset, file-relative; byte meanings unknown
0C  u32  record-array offset, file-relative
```

Name-array entries are file-relative `u32` offsets to NUL-terminated names.
Records have a `0x30`-byte stride; a nonzero `u32(record+28)` is relative to that
record. The exact transition conditions, destination lists and timing fields
have not been decoded. The data is **not** established to be an `N*N` matrix.
`trans_ch.bin` has 161 records; `trans_fp.bin` has 37.

### BkXboxFx.bin

`sound/dsp/BkXboxFx.bin` is an audio DSP program/data image, not a keyed resource
dictionary. Its bytes and container format were not decoded in this investigation.
The inspector returns `unsupported`, and this file is excluded from the 306
recognized layouts. Renaming it to `.db` or applying a texture parser is invalid.
