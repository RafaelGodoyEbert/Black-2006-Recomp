# MODEL version 0x23

This extends the [BIN/DB dictionary specification](bin-db.md) into the model
payloads used by the supported Xbox retail release. The roots and relocation
bases are verified in the original loader. Vertex positions and index spans
were additionally checked across the stock corpus; complete rendering, skinning
and asset rebuilding remain outside this specification.

Both `MODELS` and `MYDICTIONARY` contain these resources. All 4,286 entries reached
through those dictionaries have the same `MODEL` identifier and version `0x23`
(35 decimal). These are resource occurrences, including duplicates in different
level directories, not 4,286 distinct models.

## Model root

Let `P` be the dictionary value's absolute file offset:

```text
P+00  f32[4]  observed bounds vector; repeated in LOD records
P+10  u64     GtID MODEL = 0x95820AD4B8000000
P+18  u32     version = 0x23
P+1C  u32     vertex-buffer record array offset, relative to P
P+20  u32     six-byte selector-record array offset, relative to P
P+24  u32     buffer/selector record count
P+28..37     other fields; not interpreted here
P+38  u32     material-related region offset, relative to P
P+3C  u32     opaque material-related field
P+40  u32     other material-related region offset, relative to P
P+44  u32     opaque material-related field
P+48  u32     LOD-record array offset, relative to P
P+4C  u32     texture-name pointer-array offset, relative to P
P+50  u32     texture-name count
P+54  u32     matrix-array offset, relative to P
P+58  u32     auxiliary index-word array offset, relative to P
P+5C  u32     model bone/name count
P+60  u32     bone-name pointer-array offset, relative to P
P+64  u32     other region offset, relative to P; meaning unresolved
P+68  u8      LOD-record count
P+69..6F     other fields; not interpreted here
```

Native `0x001640B0` performs the root relocation; `0x00014B50` invokes it for
each model dictionary value. The buffer count and bone/name count are different.
The selector records have a six-byte stride, with three observed 16-bit words.
The first signed word is used as an offset into `P+38`'s material data by the
loader. The other selector fields and material programs are not decoded by the
inspector.

The inspector reports the first four bounds floats without assigning a complete
sphere/coordinate-space convention. It reports the four raw words at `+38..44`
without claiming to parse the material regions.

## Vertex-buffer records and draw descriptors

Each buffer record has a `0x1C`-byte stride. At record base `V`:

```text
V+00  u32  native resource word
V+04  u32  vertex-data offset, relative to V
V+08  u32  runtime/native field; not interpreted
V+0C  u32  draw-descriptor array offset, relative to V
V+10  u32  draw-descriptor count
V+14  u32  vertex stride in bytes
V+18  u32  raw vertex-format flags; meanings not fully established
```

The buffer and its descriptor array do **not** use the model root as their
pointer base. Native `0x001605D0` relocates `V+0C` from V and the index field
inside each descriptor from that descriptor's base. It also relocates `V+04`
from V when preparing the native buffer.

Each draw descriptor has a `0x30`-byte stride. At descriptor base `D`:

```text
D+00  f32[3]  one observed position-bound vector
D+0C  u32     padding/opaque word; not assumed zero
D+10  f32[3]  the other observed position-bound vector
D+1C  u32     padding/opaque word
D+20  u32     primitive type; 6 in all checked MODEL draws
D+24  u32     index count
D+28  u32     index-data offset, relative to D
D+2C  u32     other field; not interpreted
```

Primitive 6 is a triangle strip. Read the indices as little-endian `u16` values;
they address vertices in that draw's buffer. In particular:

```text
vertex_data = V + u32(V+04)
index_data  = D + u32(D+28)
position(i) = f32[3] at vertex_data + u16(index_data+2*i) * u32(V+14)
```

The corpus contains 10,037 buffers and 10,133 draw descriptors. Every draw reached
here uses primitive 6. The buffer strides are:

- 24 bytes: 4,944 buffer occurrences.
- 28 bytes: 4,587 buffer occurrences.
- 36 bytes: 506 buffer occurrences.

The first three float32 values of referenced vertices produce coherent raw
geometry. All referenced positions were finite and within the file bounds.
A private orthographic wireframe reconstruction of the AK1 gun/arms was also
inspected. It is not included in the repository.

The remaining bytes in a vertex contain additional attributes. Normal encoding,
UV/color layouts across all variants, bone influences, matrix-register indices
and weight normalization are not fully specified. Do not infer an identical
attribute layout from a shared stride, or use the raw flags as a bone count.

The inspector reports unique referenced indices, the largest referenced index,
raw-position bounds and nondegenerate triangle counts. Degenerate index triples
are skipped when counting triangles. The largest referenced index plus one is
**not a verified total vertex count**: unused vertices may exist. Buffer payload
lengths and exact subresource ownership have not been established, so these
checks use the physical file bounds, not an inferred end at the next resource.

## Bone names, matrices and texture names

For model count `N=u32(P+5C)`, the checked data contains:

- `N` matrix records at `P+u32(P+54)`, each 64 bytes (16 finite float32 values).
- An observed `N`-word auxiliary array at `P+u32(P+58)`.
- `N` name pointers at `P+u32(P+60)`, each a `u32` relative to **P**.

Bone names are NUL-terminated strings. The matrix block is reported by offset;
its exact bind/inverse-bind and coordinate-space interpretation is not claimed.
The `+58` words are emitted as `index_word_raw`. Their meaning is unresolved;
interpreting them directly as a parent array can produce self-parent entries.
Use the [RCB skeleton hierarchy](animation.md#rcb-version-21) for verified
animation parent records instead.

The inspector validates the matrix/name/word-array ranges and finite matrix
values. It does not compose these matrices or apply skinning to the indexed
positions. Of the 4,286 checked model occurrences, 640 have a nonzero bone/name
count.

Texture names use their separate count at `+50` and pointer table at `+4C`.
Each pointer in that table is also relative to P. These are string names, not
necessarily the same numeric keys used in texture dictionaries. Name resolution
and complete material bindings still need further investigation.

## LOD records

Native `0x001640B0` walks `u8(P+68)` records at `P+u32(P+48)` with a `0xD0`-byte
stride. Fields at record offsets `+C0`, `+C4` and `+C8`, when nonzero, are
relative to **P**, not the LOD record. One of these paths invokes another
relocator at `0x000BE940`. The inspector checks the array and these targets,
but does not decode all visibility, grouping or rendering information within
the records. The repeated bounds bytes do not make the rest of the record a
standard mesh header.

## Worked example: bg1_ak1.db

The first-mission main `BG1_AK1` model starts at `P=0x22A00`:

```text
buffer count                    2
buffer records                  P+0x1110 = 0x23B10
first buffer vertex data         V+0xA0   = 0x23BB0
first buffer draw descriptor     V+0x40   = 0x23B50
vertex stride                   36 bytes
model bone/name count           44
matrix block                    P+0x70
auxiliary index words           P+0xB70
bone-name pointer table          P+0xC20
texture-name count              2
```

Its RCB has 42 animation joints. Case-insensitive name hashing matches 42 of the
44 model names to those joints. The two unmatched names in this example are
`AssualtRifle_Clip` and `Finger3_Tip_rt`; the spelling is from the stock data.
This is why model name indices and animation joint indices must not be equated.

See [animation.md](animation.md) for the hash function and bank/skeleton links,
and [validation.md](validation.md#model-and-animation-follow-up) for evidence
and the limits of this investigation.
