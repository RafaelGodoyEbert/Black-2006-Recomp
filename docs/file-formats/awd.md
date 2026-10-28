# AWD: Xbox Audio Wave Dictionary

AWD is a RenderWare Audio Wave Dictionary with a metadata region and a separate
sample-data region. This specification covers BLACK's **little-endian Xbox**
variant. It was checked against all 36 banks in the reference disc and both AWD
files attached to [issue #42](https://github.com/portingpete/Black-2006-Recomp/issues/42).

The principal independent reference is
[vgmstream's AWD reader](https://github.com/vgmstream/vgmstream/blob/d9875b1fff307e4759dfb85e49e3d5e6fb196f7c/src/meta/awd.c).
Its broader support for PS2, GameCube and other RenderWare games does not imply
that those variants were tested here. The AWD layouts below are **observed and
cross-checked with that reader**; the original AWD loader and all runtime flags
have not been fully reverse engineered.

## File header

All fields below are file-relative unless another base is explicitly given.

```text
00  u32       format identifier = 0x00000809
04  u32       endianness field; 0 in the checked Xbox files
08  u32       beginning of sample data, A
0C  u32       wave-dictionary offset, W
10  u32       unknown; zero in the checked files
14  u32       declared sample-data region size
18  byte[16]  platform UUID
28  u32       metadata/header size H; equals A
```

The Xbox platform UUID is `{453A2D04-E45F-4BC8-81F0-DF758B01F273}`. Its raw bytes
in these files are:

```text
04 2D 3A 45 5F E4 C8 4B 81 F0 DF 75 8B 01 F2 73
```

The common reader recognizes `0x809` in either byte order and consults byte `04`
for endianness. The Rust inspector deliberately requires the Xbox UUID and
little-endian fields. It does not guess a platform from the filename.

`H` is aligned to `0x800` bytes in this corpus. Stream starts are aligned to
`0x80` bytes and often have finer packing than the header. Always follow the
stored offsets. The declared region ends at `A + u32(14)`; the physical file
can contain further alignment padding. A stream's size excludes that padding.
Header/metadata pointers must stay inside `[0,H)`, and each stream must fit inside
the declared sample-data region as well as the physical file.

## Wave dictionary and circular linked list

At wave-dictionary base `W`:

```text
W+00  u32  unknown/optional field; zero in the checked files
W+04  u32  dictionary-name pointer; zero in the checked BLACK banks
W+08  u32  unknown field
W+0C  u32  sentinel's previous-node pointer
W+10  u32  sentinel's next-node pointer
```

Let the sentinel address be `S=W+0C`. The first node is `u32(S+04)`. Each
ordinary node `L` contains:

```text
L+00  u32  previous-node offset, file-relative
L+04  u32  next-node offset, file-relative
L+08  u32  wave-descriptor offset Q, file-relative
```

Follow `next` until it equals **S**, not W, zero, the first ordinary node, or EOF.
There is no stream count in this list header; count nodes while traversing.
The list is empty if the first `next` is S. A robust reader also checks backward
links, the sentinel's tail link, and repeated nodes to reject malformed cycles.
Do not assume descriptor, node or name records are physically contiguous.

Stream numbers used by vgmstream are **one-based list order**. They are not
GtIDs and need not be stable if a bank is rebuilt. Names point to NUL-terminated
strings in the metadata region.

## Wave descriptor

The checked descriptors provide these fields at `Q`:

```text
Q+00  u32  optional UUID pointer; zero in the checked BLACK files
Q+04  u32  stream-name offset, file-relative
Q+08..0F   opaque runtime/exporter fields
Q+10  u32  source sample rate in Hz
Q+14  u32  source codec ID
Q+18  u32  source encoded-stream size in bytes, Z
Q+1C  u8   source bit depth
Q+1D  u8   source channel count
Q+1E..1F   additional format flags; unknown
Q+20  u32  source extra-metadata pointer; zero in checked BLACK entries
Q+24  u32  source extra-metadata size; zero in checked BLACK entries
Q+28  u32  opaque field
Q+2C  u32  target sample rate
Q+30  u32  target codec ID
Q+34  u32  target stream size
Q+38  u8   target bit depth
Q+39  u8   target channel count
Q+3A..3B   additional target format flags; unknown
Q+3C  u32  target extra-metadata pointer
Q+40  u32  target extra-metadata size
Q+44  u32  opaque field
Q+48  u32  opaque offset-like field; not the stream pointer used by vgmstream
Q+4C  u32  stream offset relative to sample-data base A
Q+50..5B   opaque fields; not interpreted here
```

For the checked files the source and target format fields agree, but they are
separate fields. A reader should not interpret them as the header for a second
adjacent sound. Some opaque fields contain values resembling old addresses; do
not treat every 32-bit word as a valid file pointer.

The actual encoded bytes are:

```text
start = A + u32(Q+4C)
end   = start + u32(Q+18)
```

In particular, `Q+4C` is relative to **A**, while the name, list and descriptor
pointers are file-relative. Reading `Q+48` as the stream offset or adding `Q`
instead of A silently selects the wrong audio.

## Codecs observed in BLACK Xbox

All 1,385 checked entries have one channel. They comprise 1,372 entries of codec
1 and 13 of codec 4. Stored sample rates range from 11,025 to 48,000 Hz; use the
individual descriptor value rather than a fixed 44,100/48,000 Hz output rate.

### Codec 1: signed 16-bit little-endian PCM

Read the Z bytes as signed little-endian 16-bit samples. For the checked mono
streams:

```text
sample_count = Z / 2
duration     = sample_count / sample_rate
```

Require an even byte length. Some banks store separately named left and right
mono entries, such as `10_l` and `10_r`; this does not make each entry interleaved
stereo. Pairing, spatialization and playback routing are separate runtime tasks.

### Codec 4: Xbox IMA ADPCM (XADPCM)

The checked mono payloads comprise complete `0x24`-byte blocks. Each block has
a signed 16-bit little-endian predictor at `+00`, a step index at `+02`, a
reserved byte at `+03`, and 32 bytes of ADPCM data. Low nibbles are processed
first. A block yields **64 samples**, including the predictor; the final encoded
nibble is skipped. Treating it as ordinary Microsoft IMA with 65 samples per
block produces the wrong duration.

```text
sample_count = (Z / 0x24) * 64
duration     = sample_count / sample_rate
```

The Rust reader requires a complete-block byte length. vgmstream also handles
some partial-block cases in other games; those are outside this checked corpus.
For an actual decoder, use its pinned
[Xbox IMA implementation](https://github.com/vgmstream/vgmstream/blob/d9875b1fff307e4759dfb85e49e3d5e6fb196f7c/src/coding/ima_decoder.c#L662).
The inspector reports metadata and sample counts; it does not decode ADPCM.

Other codec IDs exist in RenderWare AWD, including PS ADPCM, floating point,
GameCube DSP and various compressed audio formats. They were not observed or
validated in this Xbox corpus and are explicitly unsupported by this reader.
The codec numbering is unrelated to the EA XAS codec in BLACK's SSH/BKS files.

## Worked examples

The issue's first-mission `Collide.awd` has:

```text
physical file size   2,973,696 bytes
dictionary W         0x2C
sentinel S           0x38
sample-data base A   0x3000
declared data size   2,960,386 bytes
list entries         89
```

The issue's `Level.awd` has 31 entries, `A=0x1000`, a declared data size of
964,164 bytes, and a physical size of 968,704 bytes.

For an example of codec 4, first-mission `stg_0001/Emitter.awd`, entry 1
(`sinec5`), has `A=0x800`, stream-relative offset zero, `Z=180`, rate 44,100 Hz
and 320 decoded samples. Its declared region is 184 bytes: the extra four bytes
are not part of the encoded stream. This demonstrates why stream size and
declared data-region size must be kept separate.

## Decode with an existing tool

Use the [official vgmstream CLI](https://github.com/vgmstream/vgmstream/releases)
with its supplied DLLs. These commands were verified with **r2168**:

```powershell
vgmstream-cli.exe -m game/levels/level_00/Collide.awd
vgmstream-cli.exe -m -s 2 game/levels/level_00/Collide.awd
vgmstream-cli.exe -s 1 -o local/collide-1.wav game/levels/level_00/Collide.awd
```

The first two commands inspect metadata; the third decodes one entry to a new
WAV path. Use separate output paths for different streams. The resulting audio
is derived from the user's game files and is not included in this repository.

## Limits: loops, playback and rebuilding

vgmstream's AWD reader exports entries without loop points. That does **not**
establish that the game never loops them: the game can apply looping, routing,
pitch, volume, effects or event rules elsewhere. Names such as `birdloop1` are
not evidence of a particular loop-point field.

The Rust inspector checks the circular list, descriptor/name bounds, mono codec
support, payload spans and sample counts. It does not validate every codec block,
all runtime fields, effects or event associations. Metadata was independently
compared for each bank's first and last entry; one PCM stream and one XADPCM
stream were decoded successfully. No rebuilt bank or in-game replacement was
tested. A writer must establish the opaque metadata and prove a round trip
before claiming compatibility.
