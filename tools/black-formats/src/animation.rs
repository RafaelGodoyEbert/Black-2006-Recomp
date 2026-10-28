//! RCB v21 skeletons and bank v8/encoding-4 track boundaries, not a pose player.
use super::{Reader, Result, MODELS};
use serde_json::{json, Value};
use std::collections::{HashMap, HashSet};

const RCB: u64 = 0xb0dc09942f000000;
const BANK: u64 = 0x555c57e557000000;
const SHARED: u64 = 0xb767793c8161d7c0;
const ANIM: u64 = 0x4f883c088278aedd;
const MODEL_DICT: u64 = 0x96f6a8e27244ea15;

/// ASCII behavior of the loader's case-insensitive 65599 name hash.
fn name_hash(name: &str) -> u32 {
    name.bytes().fold(0u32, |h, c| {
        h.wrapping_mul(65599)
            .wrapping_add(c.to_ascii_lowercase() as u32)
    })
}

struct Fixups<'a, 'b> {
    r: &'a Reader<'b>,
    base: usize,
    table: usize,
    end: usize,
    fields: HashSet<usize>,
}
impl<'a, 'b> Fixups<'a, 'b> {
    fn read(
        r: &'a Reader<'b>,
        base: usize,
        limit: usize,
        version: u32,
        header: usize,
    ) -> Result<Self> {
        r.range(base, header)?;
        r.expect(base + 4, version)?;
        if limit > r.bytes.len() || base > limit {
            return Err("invalid animation blob limit".into());
        }
        let relative = r.u32(base)? as usize;
        let table = base.checked_add(relative).ok_or("fixup table overflow")?;
        if relative < header || table.checked_add(4).is_none_or(|end| end > limit) {
            return Err("fixup table outside animation blob".into());
        }
        let mut cursor = table;
        let mut fields = HashSet::new();
        loop {
            if cursor.checked_add(4).is_none_or(|end| end > limit) {
                return Err("unterminated animation fixup table".into());
            }
            let offset = r.u32(cursor)? as usize;
            cursor += 4;
            if offset == 0 {
                break;
            }
            if offset.checked_add(4).is_none_or(|end| end > relative) {
                return Err("fixup field overlaps relocation table".into());
            }
            if !fields.insert(offset) {
                return Err("duplicate animation fixup".into());
            }
            // Empty auxiliary arrays in embedded RCBs point one past the data.
            if r.u32(base + offset)? as usize > relative {
                return Err("fixup target outside animation payload".into());
            }
        }
        Ok(Self {
            r,
            base,
            table,
            end: cursor,
            fields,
        })
    }
    fn range(&self, at: usize, n: usize) -> Result<()> {
        if at < self.base || at.checked_add(n).is_none_or(|end| end > self.table) {
            return Err(format!(
                "animation payload range 0x{at:x}+0x{n:x} crosses fixup table"
            ));
        }
        self.r.range(at, n)?;
        Ok(())
    }
    fn pointer(&self, field: usize) -> Result<usize> {
        self.range(field, 4)?;
        if !self.fields.contains(&(field - self.base)) {
            return Err(format!(
                "animation pointer field 0x{field:x} is absent from fixups"
            ));
        }
        let target = self.base + self.r.u32(field)? as usize;
        self.range(target, 1)?;
        Ok(target)
    }
    fn array(&self, field: usize, n: usize, stride: usize) -> Result<Option<usize>> {
        if n == 0 {
            return Ok(None);
        }
        let at = self.pointer(field)?;
        self.range(
            at,
            n.checked_mul(stride)
                .ok_or("animation array size overflow")?,
        )?;
        Ok(Some(at))
    }
    fn metadata(&self) -> Value {
        json!({"fixup_count":self.fields.len(),"fixup_table_offset":self.table,"serialized_end":self.end})
    }
}

struct Skeleton {
    id: u32,
    joints: usize,
    json: Value,
}
fn skeleton(r: &Reader<'_>, base: usize, limit: usize) -> Result<Skeleton> {
    let f = Fixups::read(r, base, limit, 21, 0x40)?;
    if r.u32(base + 0x18)? != 1 {
        return Err("only the checked single-skeleton RCB layout is supported".into());
    }
    let root = f.pointer(base + 0x1c)?;
    f.range(root, 16)?;
    let n = r.u32(root)? as usize;
    let parents = f.array(root + 4, n, 8)?;
    let matrices = f.array(root + 8, n, 64)?;
    let lookup = f.array(root + 12, n, 20)?;
    let mut hierarchy = Vec::new();
    let mut hashes = Vec::new();
    let mut indices = HashSet::new();
    let mut previous = None;
    for i in 0..n {
        let parent = r.u32(parents.unwrap() + i * 8)? as i32;
        if parent < -1 || parent >= n as i32 {
            return Err("RCB parent index out of range".into());
        }
        let matrix = matrices.unwrap() + i * 64;
        for j in 0..16 {
            r.float(matrix + j * 4)?;
        }
        hierarchy.push(json!({"index":i,"parent":parent,"flags_raw":r.u32(parents.unwrap()+i*8+4)?,"matrix_offset":matrix}));
        let entry = lookup.unwrap() + i * 20;
        let hash = r.u32(entry)?;
        let index = r.u32(entry + 16)? as usize;
        if previous.is_some_and(|p| hash < p) || index >= n || !indices.insert(index) {
            return Err("invalid RCB joint hash lookup".into());
        }
        previous = Some(hash);
        hashes.push(json!({"hash":format!("{hash:08x}"),"joint":index}));
    }
    let id = r.u32(base + 8)?;
    Ok(Skeleton {
        id,
        joints: n,
        json: json!({"offset":base,"version":21,"skeleton_id":format!("{id:08x}"),"joint_count":n,
        "hierarchy":hierarchy,"joint_hashes":hashes,"relocation":f.metadata()}),
    })
}

#[derive(Debug)]
struct Track {
    end: usize,
    tag: u8,
    count: usize,
    constant: Option<Vec<f32>>,
}
fn packed_quaternion(r: &Reader<'_>, at: usize) -> Result<Vec<f32>> {
    let b = r.range(at, 12)?;
    (0..4)
        .map(|i| {
            let v = f32::from_bits(u32::from_le_bytes([
                0,
                b[i * 3],
                b[i * 3 + 1],
                b[i * 3 + 2],
            ]));
            if v.is_finite() {
                Ok(v)
            } else {
                Err("non-finite packed animation component".into())
            }
        })
        .collect()
}
fn track(f: &Fixups<'_, '_>, at: usize, depth: usize, hist: &mut [usize; 18]) -> Result<Track> {
    if depth > 64 {
        return Err("animation segment nesting exceeds reader limit".into());
    }
    f.range(at, 4)?;
    let tag = f.r.u8(at)?;
    if tag > 17 {
        return Err(format!("unknown animation track tag {tag}"));
    }
    hist[tag as usize] += 1;
    let n = f.r.u16(at + 1)? as usize;
    if tag == 1 {
        let mut cursor = at + 4;
        for _ in 0..n {
            cursor = track(f, cursor, depth + 1, hist)?.end;
        }
        return Ok(Track {
            end: cursor,
            tag,
            count: n,
            constant: None,
        });
    }
    let padded = |n: usize| (n + 3) & !3;
    let size = match tag {
        0 => 4 + 4 * n,
        2 | 10 => 16,
        3 | 11 => 28,
        4 | 12 => 40,
        5 | 13 => 52,
        6 | 14 => 4 + 12 * n,
        7 => 28 + padded(3 * n),
        8 => 28 + padded(6 * n),
        9 | 17 => 4,
        15 => 28 + 4 * n,
        16 => 28 + 8 * n,
        _ => unreachable!(),
    };
    f.range(at, size)?;
    let constant = match tag {
        2 => Some(
            (0..3)
                .map(|i| f.r.float(at + 4 + i * 4))
                .collect::<Result<_>>()?,
        ),
        10 => Some(packed_quaternion(f.r, at + 4)?),
        9 => Some(vec![0.0; 3]),
        17 => Some(vec![0.0, 0.0, 0.0, 1.0]),
        _ => None,
    };
    Ok(Track {
        end: at + size,
        tag,
        count: n,
        constant,
    })
}

fn bank(r: &Reader<'_>, base: usize, limit: usize, context: &HashMap<u32, usize>) -> Result<Value> {
    let f = Fixups::read(r, base, limit, 8, 0x3c)?;
    let id = r.u32(base + 8)?;
    let skeleton_id = r.u32(base + 12)?;
    let joints = *context
        .get(&skeleton_id)
        .ok_or_else(|| format!("no local RCB skeleton for bank ID {skeleton_id:08x}"))?;
    let nstates = r.u32(base + 0x24)? as usize;
    let states = f.array(base + 0x28, nstates, 4)?;
    let mut state_list = Vec::new();
    for i in 0..nstates {
        let state = f.pointer(states.unwrap() + i * 4)?;
        f.range(state, 8)?;
        let name = r.name(f.pointer(state)?, f.table)?;
        let root = f.pointer(state + 4)?;
        state_list.push(json!({"offset":state,"name_hash":format!("{:08x}",name_hash(&name)),"name":name,"graph_root_offset":root}));
    }
    let nclips = r.u32(base + 0x2c)? as usize;
    let clip_table = f.array(base + 0x30, nclips, 4)?;
    let mut clips = Vec::new();
    for i in 0..nclips {
        let c = f.pointer(clip_table.unwrap() + i * 4)?;
        f.range(c, 0x30)?;
        let frames = r.u32(c + 0x14)? as usize;
        let encoding = r.u32(c + 0x1c)?;
        if frames == 0 || encoding & 0x7fffffff != 4 {
            return Err(format!(
                "unverified animation clip encoding/frame count {encoding}/{frames}"
            ));
        }
        let data = if r.u32(c + 0x20)? == 0 {
            c + 0x30
        } else {
            f.pointer(c + 0x20)?
        };
        let count = joints.checked_mul(2).ok_or("track count overflow")?;
        let mut cursor = data;
        let mut histogram = [0; 18];
        let mut preview = Vec::new();
        for j in 0..count {
            let t = track(&f, cursor, 0, &mut histogram)?;
            if j < 2 {
                preview.push(json!({"offset":cursor,"tag":t.tag,"encoded_count":t.count,"constant_components":t.constant}));
            }
            cursor = t.end;
        }
        clips.push(json!({"offset":c,"id":format!("{:08x}",r.u32(c)?),"frames":frames,"encoding_raw":encoding,
            "joint_count":joints,"top_level_tracks":count,"track_tag_counts":histogram,
            "sample_data_offset":data,"sample_data_end":cursor,"first_joint_track_preview":preview}));
    }
    let nextra = r.u32(base + 0x34)? as usize;
    let extras = f.array(base + 0x38, nextra, 4)?;
    let mut extra_offsets = Vec::new();
    for i in 0..nextra {
        extra_offsets.push(f.pointer(extras.unwrap() + i * 4)?);
    }
    Ok(
        json!({"offset":base,"version":8,"id":format!("{id:08x}"),"skeleton_id":format!("{skeleton_id:08x}"),
        "skeleton_joint_count":joints,"states":state_list,"clips":clips,"other_object_offsets":extra_offsets,"relocation":f.metadata()}),
    )
}

fn bundle(r: &Reader<'_>, base: usize) -> Result<(Skeleton, usize, usize, Value)> {
    r.range(base, 16)?;
    let sk = r.rel(base, base + 8)?;
    let bk = r.rel(base, base + 12)?;
    let sn = r.u32(base)? as usize;
    let bn = r.u32(base + 4)? as usize;
    r.range(sk, sn)?;
    r.range(bk, bn)?;
    let s = skeleton(r, sk, sk + sn)?;
    let meta = json!({"offset":base,"skeleton_size":sn,"bank_size":bn,"skeleton_offset":sk,"bank_offset":bk});
    Ok((s, bk, bk + bn, meta))
}

/// Resolve bank skeleton IDs within this file. GtID keys and bone-name hashes
/// are different namespaces; no association is inferred from physical proximity.
pub(super) fn enrich(r: &Reader<'_>, layout: &mut Value) -> Result<()> {
    let Some(dicts) = layout["dictionaries"].as_array_mut() else {
        return Ok(());
    };
    let mut context = HashMap::new();
    let mut parsed = HashMap::new();
    let mut bundles = HashMap::new();
    for d in dicts.iter() {
        let id = u64::from_str_radix(d["id"].as_str().ok_or("missing dictionary ID")?, 16)
            .map_err(|e| e.to_string())?;
        if !matches!(id, RCB | ANIM) {
            continue;
        }
        for entry in d["entries"]
            .as_array()
            .ok_or("missing dictionary entries")?
        {
            let at = entry["offset"].as_u64().ok_or("missing resource offset")? as usize;
            let s = if id == RCB {
                skeleton(r, at, r.bytes.len())?
            } else {
                let (s, bk, end, meta) = bundle(r, at)?;
                bundles.insert(at, (bk, end, meta));
                s
            };
            if context
                .insert(s.id, s.joints)
                .is_some_and(|previous| previous != s.joints)
            {
                return Err("ambiguous local skeleton ID with different joint counts".into());
            }
            parsed.insert(at, s.json);
        }
    }
    for d in dicts.iter_mut() {
        let id = u64::from_str_radix(d["id"].as_str().unwrap(), 16).unwrap();
        let mut resources = Vec::new();
        for entry in d["entries"].as_array().unwrap() {
            let at = entry["offset"].as_u64().unwrap() as usize;
            let result = match id {
                MODELS | MODEL_DICT => super::models::model(r, at),
                RCB => Ok(parsed[&at].clone()),
                BANK | SHARED => bank(r, at, r.bytes.len(), &context),
                ANIM => {
                    let (bk, end, mut meta) = bundles[&at].clone();
                    meta["skeleton"] = parsed[&at].clone();
                    meta["bank"] = bank(r, bk, end, &context)?;
                    Ok(meta)
                }
                _ => continue,
            };
            let mut value = result
                .map_err(|e| format!("{} resource {} at 0x{at:x}: {e}", d["name"], entry["key"]))?;
            value["key"] = entry["key"].clone();
            value["key_name"] = entry["key_name"].clone();
            resources.push(value);
        }
        if !resources.is_empty() {
            d["resources"] = json!(resources);
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    fn put(b: &mut [u8], at: usize, v: u32) {
        b[at..at + 4].copy_from_slice(&v.to_le_bytes());
    }
    fn fixture(version: u32) -> Vec<u8> {
        let mut b = vec![0; 0x320];
        put(&mut b, 0x20, 0x200);
        put(&mut b, 0x24, version);
        b
    }
    fn fixups(b: &mut [u8], fields: &[u32]) {
        for (i, field) in fields.iter().enumerate() {
            put(b, 0x220 + i * 4, *field);
        }
        put(b, 0x220 + fields.len() * 4, 0);
    }
    fn bank_fixture() -> Vec<u8> {
        let mut b = fixture(8);
        let p = 0x20;
        put(&mut b, p + 12, 0x1234);
        put(&mut b, p + 0x2c, 1);
        put(&mut b, p + 0x30, 0x40);
        put(&mut b, p + 0x40, 0x60);
        put(&mut b, p + 0x60 + 0x14, 2);
        put(&mut b, p + 0x60 + 0x1c, 4);
        b[p + 0x90] = 17;
        b[p + 0x94] = 9;
        fixups(&mut b, &[0x30, 0x40]);
        b
    }
    #[test]
    fn bank_clip_tracks_use_skeleton_count_and_inline_pointer() {
        let b = bank_fixture();
        let context = HashMap::from([(0x1234, 1)]);
        let v = bank(
            &Reader {
                bytes: &b,
                big: false,
            },
            0x20,
            b.len(),
            &context,
        )
        .unwrap();
        let c = &v["clips"][0];
        assert_eq!(c["sample_data_offset"], 0xb0);
        assert_eq!(c["sample_data_end"], 0xb8);
        assert_eq!(c["top_level_tracks"], 2);
        assert_eq!(c["track_tag_counts"][17], 1);
        assert_eq!(c["track_tag_counts"][9], 1);
    }
    #[test]
    fn missing_skeleton_is_not_guessed() {
        let b = bank_fixture();
        assert!(bank(
            &Reader {
                bytes: &b,
                big: false
            },
            0x20,
            b.len(),
            &HashMap::new()
        )
        .is_err());
    }
    #[test]
    fn fixup_fields_targets_and_duplicates_are_checked() {
        let mut b = bank_fixture();
        fixups(&mut b, &[0x30, 0x30]);
        assert!(Fixups::read(
            &Reader {
                bytes: &b,
                big: false
            },
            0x20,
            b.len(),
            8,
            0x3c
        )
        .is_err());
        let mut b = bank_fixture();
        fixups(&mut b, &[0x200]);
        assert!(Fixups::read(
            &Reader {
                bytes: &b,
                big: false
            },
            0x20,
            b.len(),
            8,
            0x3c
        )
        .is_err());
        let mut b = bank_fixture();
        put(&mut b, 0x50, 0x201);
        assert!(Fixups::read(
            &Reader {
                bytes: &b,
                big: false
            },
            0x20,
            b.len(),
            8,
            0x3c
        )
        .is_err());
    }
    #[test]
    fn fixups_can_preserve_one_past_empty_data_and_root_targets() {
        let mut b = bank_fixture();
        put(&mut b, 0x50, 0x200);
        let r = Reader {
            bytes: &b,
            big: false,
        };
        let f = Fixups::read(&r, 0x20, b.len(), 8, 0x3c).unwrap();
        assert!(f.pointer(0x50).is_err());
        let mut b = bank_fixture();
        put(&mut b, 0x50, 0);
        let r = Reader {
            bytes: &b,
            big: false,
        };
        let f = Fixups::read(&r, 0x20, b.len(), 8, 0x3c).unwrap();
        assert_eq!(f.pointer(0x50).unwrap(), 0x20);
    }
    #[test]
    fn fixup_table_must_terminate_inside_blob_limit() {
        let b = bank_fixture();
        assert!(Fixups::read(
            &Reader {
                bytes: &b,
                big: false
            },
            0x20,
            0x228,
            8,
            0x3c
        )
        .is_err());
    }
    #[test]
    fn pointer_fields_must_be_listed() {
        let mut b = bank_fixture();
        fixups(&mut b, &[0x30]);
        assert!(bank(
            &Reader {
                bytes: &b,
                big: false
            },
            0x20,
            b.len(),
            &HashMap::from([(0x1234, 1)])
        )
        .is_err());
    }
    #[test]
    fn all_native_track_sizes_and_alignment() {
        let mut b = fixture(8);
        fixups(&mut b, &[]);
        let r = Reader {
            bytes: &b,
            big: false,
        };
        let f = Fixups::read(&r, 0x20, b.len(), 8, 0x3c).unwrap();
        // Three samples: fixed constants, polynomial coefficients, quantized arrays.
        let cases = [
            (0, 16),
            (2, 16),
            (3, 28),
            (4, 40),
            (5, 52),
            (6, 40),
            (7, 40),
            (8, 48),
            (9, 4),
            (10, 16),
            (11, 28),
            (12, 40),
            (13, 52),
            (14, 40),
            (15, 40),
            (16, 52),
            (17, 4),
        ];
        for (tag, size) in cases {
            let mut data = b.clone();
            data[0x60] = tag;
            data[0x61] = 3;
            let rr = Reader {
                bytes: &data,
                big: false,
            };
            let ff = Fixups {
                r: &rr,
                base: f.base,
                table: f.table,
                end: f.end,
                fields: f.fields.clone(),
            };
            assert_eq!(
                track(&ff, 0x60, 0, &mut [0; 18]).unwrap().end,
                0x60 + size,
                "tag {tag}"
            );
        }
    }
    #[test]
    fn segmented_tracks_and_bad_tags() {
        let mut b = fixture(8);
        fixups(&mut b, &[]);
        b[0x60] = 1;
        b[0x61] = 2;
        b[0x64] = 17;
        b[0x68] = 9;
        let r = Reader {
            bytes: &b,
            big: false,
        };
        let f = Fixups::read(&r, 0x20, b.len(), 8, 0x3c).unwrap();
        let mut h = [0; 18];
        assert_eq!(track(&f, 0x60, 0, &mut h).unwrap().end, 0x6c);
        assert_eq!(h[1], 1);
        assert_eq!(h[17], 1);
        assert_eq!(h[9], 1);
        let mut bad = b.clone();
        bad[0x60] = 18;
        let r = Reader {
            bytes: &bad,
            big: false,
        };
        let f = Fixups::read(&r, 0x20, bad.len(), 8, 0x3c).unwrap();
        assert!(track(&f, 0x60, 0, &mut [0; 18]).is_err());
    }
    #[test]
    fn track_payload_cannot_run_into_fixups() {
        let mut b = fixture(8);
        fixups(&mut b, &[]);
        b[0x210] = 8;
        b[0x211] = 255;
        let r = Reader {
            bytes: &b,
            big: false,
        };
        let f = Fixups::read(&r, 0x20, b.len(), 8, 0x3c).unwrap();
        assert!(track(&f, 0x210, 0, &mut [0; 18]).is_err());
    }
    #[test]
    fn nested_segments_have_bounded_recursion() {
        let mut b = fixture(8);
        fixups(&mut b, &[]);
        for i in 0..67 {
            b[0x60 + i * 4] = 1;
            b[0x61 + i * 4] = 1;
        }
        b[0x60 + 67 * 4] = 17;
        let r = Reader {
            bytes: &b,
            big: false,
        };
        let f = Fixups::read(&r, 0x20, b.len(), 8, 0x3c).unwrap();
        assert!(track(&f, 0x60, 0, &mut [0; 18]).is_err());
    }
    #[test]
    fn packed_quaternion_is_four_high_24_bit_float_components() {
        let b = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x80, 0x3f];
        assert_eq!(
            packed_quaternion(
                &Reader {
                    bytes: &b,
                    big: false
                },
                0
            )
            .unwrap(),
            [0.0, 0.0, 0.0, 1.0]
        );
        let b = [0, 0x80, 0x3f, 0, 0, 0xc0, 0, 0x80, 0x3e, 0, 0, 0x40];
        assert_eq!(
            packed_quaternion(
                &Reader {
                    bytes: &b,
                    big: false
                },
                0
            )
            .unwrap(),
            [1.0, -2.0, 0.25, 2.0]
        );
    }
    #[test]
    fn name_hash_is_case_insensitive_ascii() {
        assert_eq!(name_hash("Flash_Loc"), name_hash("FLASH_LOC"));
        assert_eq!(name_hash("A"), 97);
        assert_eq!(name_hash("ab"), 6_363_201);
    }
    fn skeleton_fixture() -> Vec<u8> {
        let mut b = fixture(21);
        let p = 0x20;
        put(&mut b, p + 8, 0x1234);
        put(&mut b, p + 0x18, 1);
        put(&mut b, p + 28, 0x40);
        put(&mut b, p + 0x40, 2);
        put(&mut b, p + 0x44, 0x50);
        put(&mut b, p + 0x48, 0x60);
        put(&mut b, p + 0x4c, 0xe0);
        put(&mut b, p + 0x50, u32::MAX);
        put(&mut b, p + 0xe0, 10);
        put(&mut b, p + 0xf0, 1);
        put(&mut b, p + 0xf4, 20);
        put(&mut b, p + 0x104, 0);
        fixups(&mut b, &[0x1c, 0x44, 0x48, 0x4c]);
        b
    }
    #[test]
    fn skeleton_arrays_are_relative_to_blob_not_skeleton_header() {
        let b = skeleton_fixture();
        let s = skeleton(
            &Reader {
                bytes: &b,
                big: false,
            },
            0x20,
            b.len(),
        )
        .unwrap();
        assert_eq!(s.joints, 2);
        assert_eq!(s.json["hierarchy"][0]["matrix_offset"], 0x80);
        assert_eq!(s.json["hierarchy"][1]["parent"], 0);
        assert_eq!(s.json["joint_hashes"][0]["joint"], 1);
    }
    #[test]
    fn unsupported_multi_skeleton_blob_is_not_silently_truncated() {
        let mut b = skeleton_fixture();
        put(&mut b, 0x38, 2);
        assert!(skeleton(
            &Reader {
                bytes: &b,
                big: false
            },
            0x20,
            b.len()
        )
        .is_err());
    }
    #[test]
    fn invalid_skeleton_lookup_and_parent_are_rejected() {
        let mut b = skeleton_fixture();
        put(&mut b, 0x20 + 0x104, 1);
        assert!(skeleton(
            &Reader {
                bytes: &b,
                big: false
            },
            0x20,
            b.len()
        )
        .is_err());
        let mut b = skeleton_fixture();
        put(&mut b, 0x20 + 0x50, 2);
        assert!(skeleton(
            &Reader {
                bytes: &b,
                big: false
            },
            0x20,
            b.len()
        )
        .is_err());
    }
}
