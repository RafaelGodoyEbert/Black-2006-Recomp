# Skeletons and animation banks

This documents the parts of BLACK Xbox animation data verified during the
follow-up to [issue #42](https://github.com/portingpete/Black-2006-Recomp/issues/42).
It covers version-21 RCB blobs, version-8 banks, embedded animation bundles and
encoding-4 track boundaries. It is not a complete state-machine, interpolation,
pose, event or animation-authoring specification.

The reader checks all 180 reached RCB occurrences, 211 banks, 4,013 clips and
459,470 track records, including segmented children. No animation was rebuilt
or played through a new decoder in the game. Counts include resources repeated
in different files. The [validation notes](validation.md#model-and-animation-follow-up)
record the native functions and exact scope.

## Blob relocation tables

RCB and bank blobs use the same relocation mechanism. At blob base B:

```text
B+00  u32  relocation-table offset, relative to B
B+04  u32  blob version (21 for checked RCBs; 8 for checked banks)
```

The table at `T=B+u32(B)` is a sequence of `u32` **field offsets**, terminated by
zero. For each nonzero entry F, the loader replaces the word at `B+F` with
`B+u32(B+F)`. Thus both the table's entries and the pointer values they identify
use the blob base, regardless of how deeply nested their records are.

The native functions `0x00092B20` and `0x0017BD00` establish this behavior.
The loader then zeros the first table entry to mark it processed. A relocated
memory image must not be parsed as an original on-disc blob.

Checked fields lie before T. Pointer targets generally lie before T as well,
but empty auxiliary arrays in embedded RCBs can point **at T**, one past their
data. The reader retains such fixups but rejects dereferencing them as payload.
A zero-valued word in a listed field would resolve to B; nullability cannot be
inferred from the raw pointer value without considering whether it is in the
relocation table. Runtime-only zero fields that are not listed remain distinct.

The inspector validates field/target ranges, duplicates and termination, without
applying fixups or changing the source file. For an embedded blob, its declared
byte length also bounds the table. For standalone dictionary values with no
established outer length, the file bounds limit the table; payload reads are
bounded by T.

## RCB version 21

`RCBS` dictionary values point to these blobs:

```text
B+00  u32  relocation-table offset
B+04  u32  version = 21
B+08  u32  skeleton/RCB identifier
B+0C..17   opaque fields
B+18  u32  skeleton-header count; 1 in every checked RCB
B+1C  u32  skeleton-header array offset, relative to B
B+20..2F   opaque fields
B+30  f32  runtime time-scale-related value; units not established here
B+34       opaque field
B+38  u32  auxiliary-data offset, relative to B; not decoded
```

Each skeleton header S has a `0x10`-byte stride:

```text
S+00  u32  joint count J
S+04  u32  parent/flags array offset, relative to B
S+08  u32  matrix-array offset, relative to B
S+0C  u32  joint-hash lookup offset, relative to B
```

The array pointers use **B, not S**. Native `0x000978E0` selects a 16-byte header,
and `0x000925F0` uses the joint count, parent records and 64-byte matrices. The
current inspector requires the single-skeleton layout actually found in these
files; it does not silently choose the first skeleton from a multi-skeleton RCB.

The parent/flags array contains J records:

```text
record+00  s32  parent joint index; -1 for no parent
record+04  u32  flags; retained raw
record stride = 8 bytes
```

The matrix array contains J records of 16 float32 values, 64 bytes each. The
reader checks finite values and reports offsets; full local/global matrix
composition and the flags' behavior remain outside its scope.

The lookup contains J records with a `0x14`-byte stride:

```text
entry+00  u32  case-insensitive joint-name hash
entry+04..0F   additional fields; not interpreted
entry+10  u32  joint index
```

The checked lookups are sorted by unsigned hash, and their indices form a
permutation of `0..J`. The inspector checks both properties and parent-index
bounds. It does not claim that the presence of these properties proves all
runtime behavior or that every possible RCB revision uses this arrangement.

### Bone-name hashing and model binding

Native `0x0009CD30` computes a 32-bit hash with multiplier 65599 and character
case conversion. For the ASCII names in this corpus:

```text
h = 0
for each byte c in the name:
    h = (65599 * h + ascii_lowercase(c)) modulo 2^32
```

Native `0x0017AB40` hashes model bone names and compares the result against the
RCB lookup; `0x00096630` compares the unsigned hash values. These hashes are
**not GtIDs**. Locale/non-ASCII behavior is not specified by this ASCII reader.

Model name order need not equal skeleton joint order. The main AK1 model has
44 names and the matching RCB has 42 joints; 42 names match by hash. The reader
keeps model names, raw auxiliary model words and RCB hierarchy indices separate.

## Bank version 8: BNKS and SHAREDBNKS

At bank base B:

```text
B+00  u32  relocation-table offset
B+04  u32  version = 8
B+08  u32  bank identifier
B+0C  u32  associated RCB identifier (matches RCB+08)
B+10..23   opaque fields
B+24  u32  named state/root record count
B+28  u32  state-pointer array offset, relative to B
B+2C  u32  clip count
B+30  u32  clip-pointer array offset, relative to B
B+34  u32  other-object count
B+38  u32  other-object pointer-array offset, relative to B
```

All pointer-array entries are relative to B and appear in the relocation table.
Native `0x00092BA0` registers the three categories separately. A state/root
record contains a name pointer at `+00` and graph-root pointer at `+04`, also
relative to B. The reader reports 3,573 named records across the corpus, their
name hashes and graph-root offsets. It does not interpret the state-graph
opcodes, transitions or the third category's object schemas.

The associated RCB identifier determines the joint count used to walk clip
tracks. The inspector resolves it from RCBs in the same file, including those
inside animation bundles. A missing RCB or conflicting joint counts for the
same identifier is an error. It does not guess a joint count from adjacent
bytes or the number of names in an unrelated model.

### Clip headers

At a clip descriptor C reached from the bank's clip-pointer array:

```text
C+00  u32  clip identifier
C+04..13   opaque fields
C+14  u32  sample/frame count
C+18  u32  other sampling field; not interpreted
C+1C  u32  encoding/flags; low 31 bits are 4 in the checked clips
C+20  u32  sample-data pointer, relative to B if present in fixups
C+24..2F   opaque fields
C+30       beginning of inline sample data when C+20 is zero
```

Native `0x0009B410` addresses the sampling header at `C+14`. During registration,
`0x00092BA0` fills a zero `C+20` with `C+30`. An original on-disc zero here therefore
means **inline samples**, not an empty animation. Follow a nonzero relocated
pointer from B rather than from C.

Sample counts range from 2 to 321 in the checked clips. They are not durations
in seconds. A universal frame rate, complete time scaling, looping and event
rules have not been established.

For encoding 4, native `0x000964C0` consumes two track records per skeleton
joint: rotation components first, then translation components. Tracks are stored
sequentially, with no per-joint pointer table. Native `0x00093CC0` dispatches the
encoding; other encodings require separate validation. The new reader does not
interpret a different encoding as format 4.

## Encoding-4 track records

At track start K:

```text
K+00  u8   type tag
K+01  u16  sample count, or child count for segmented tag 1
K+03  u8   additional flags; not fully interpreted
K+04       type-dependent payload
```

All sizes below include the four-byte header. Let `N=u16(K+01)` and
`align4(x)=(x+3)&~3`. The sizes follow the native skip routine `0x00095850` and
were checked by walking the stock clips:

```text
tag 00       4 + 4*N                         scalar sample array
tag 01       4 + size(child0) + ...          N nested segments
tag 02, 0A   16                              one vector/quaternion coefficient
tag 03, 0B   28                              two coefficients
tag 04, 0C   40                              three coefficients
tag 05, 0D   52                              four coefficients
tag 06, 0E   4 + 12*N                        sample array
tag 07       28 + align4(3*N)                quantized vector, byte components
tag 08       28 + align4(6*N)                quantized vector, 16-bit components
tag 09       4                               zero translation/vector
tag 0F       28 + 4*N                        quantized quaternion, byte components
tag 10       28 + 8*N                        quantized quaternion, 16-bit components
tag 11       4                               identity rotation
```

The N in tag 1 is a **number of child tracks**, not its frame count. Recursively
skip its children to find the next joint's track. Quantized records have two
12-byte coefficient blocks before their sample arrays; do not omit those blocks
or align only the header. The reader bounds every record against the start of
the bank's relocation table and limits nesting depth to 64.

Tags `01..0D`, `0F`, `10` and `11` were observed. Tags `00` and `0E` are supported
by the original skip routine and have synthetic size tests, but were not found
in this corpus. Unknown tags are errors, not an excuse to advance by four bytes.

The reader validates these spans, counts tag occurrences including child
segments, and previews the first joint's constant tracks where supported. It
does not evaluate polynomial or quantized curves, blend poses, or prove that a
clip's sample values produce the expected animation.

### Constant data and packed rotation components

Tag 2 stores three ordinary little-endian float32 components (`0x000957D0`).
Tag `0A` stores **four** components in twelve bytes (`0x000957F0`). Each component
keeps the high 24 bits of its float32 representation; the loader supplies zero
for its low eight bits:

```text
component i uses input bytes b[3*i], b[3*i+1], b[3*i+2]
float_bits = u32_le([0, b[3*i], b[3*i+1], b[3*i+2]])
```

This is not three full floats, four half-floats, or a "smallest three" quaternion
encoding. The inspector returns the unpacked components as stored, without
normalizing them or claiming they are already final unit quaternions.
Tag 9 previews as three zeros; tag `11` previews as `(0,0,0,1)`.

## Embedded ANIMDICTIONA bundles

Eight `ANIMDICTIONA` values in Unit files wrap an RCB and a bank. At bundle base A:

```text
A+00  u32  serialized RCB byte length
A+04  u32  serialized bank byte length
A+08  u32  RCB offset, relative to A
A+0C  u32  bank offset, relative to A
```

Native `0x001D3290` relocates `+08` and `+0C` from A. The size interpretation
was observed by checking the two contained blobs and their relocation-table
terminators against the declared spans. Padding can separate them; do not assume
that the bank follows the RCB without alignment.

For `CO00TOWER` in first-mission `Unit_01.bin`, A is `0x9FB680`: the RCB starts
at `A+80`, has length `0x41C`, and contains 10 joints. The bank starts at `A+500`,
has length `0xB1C`, and contains one encoding-4 clip. The inspector respects
these declared lengths, reads both resources and resolves their matching IDs.

## What is still missing

The state-graph opcodes, interpolation equivalence, event/parameter objects,
looping and time units, complete skeletal matrix conventions, model material
programs and skinning attributes still require research. A successful bounds
walk is not a verified animation player. Writers and modified assets still need
round-trip and in-game validation.
