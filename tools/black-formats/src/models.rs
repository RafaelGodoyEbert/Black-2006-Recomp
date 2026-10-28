//! MODEL version 0x23. Pointer owners are established by Xbox loader relocation.
use super::{Reader, Result};
use serde_json::{json, Value};
use std::collections::HashSet;

fn array(
    r: &Reader<'_>,
    base: usize,
    field: usize,
    count: usize,
    stride: usize,
) -> Result<Option<usize>> {
    if count == 0 {
        return Ok(None);
    }
    let at = r.rel(base, field)?;
    r.table(at, count, stride)?;
    Ok(Some(at))
}

pub(super) fn model(r: &Reader<'_>, base: usize) -> Result<Value> {
    r.range(base, 0x70)?;
    if r.u64(base + 0x10)? != 0x95820ad4b8000000 {
        return Err("missing MODEL identifier".into());
    }
    r.expect(base + 0x18, 0x23)?;
    let bounds: Vec<_> = (0..4)
        .map(|i| r.float(base + i * 4))
        .collect::<Result<_>>()?;
    let count = r.u32(base + 0x24)? as usize;
    let buffers = array(r, base, base + 0x1c, count, 0x1c)?;
    let selectors = array(r, base, base + 0x20, count, 6)?;
    let mut parts = Vec::new();
    for i in 0..count {
        let vb = buffers.unwrap() + i * 0x1c;
        let stride = r.u32(vb + 0x14)? as usize;
        if !matches!(stride, 24 | 28 | 36) {
            return Err(format!("unverified MODEL vertex stride {stride}"));
        }
        let draw_count = r.u32(vb + 0x10)? as usize;
        let descriptors = array(r, vb, vb + 0x0c, draw_count, 0x30)?;
        let mut draws = Vec::new();
        let data = if draw_count > 0 {
            Some(r.rel(vb, vb + 4)?)
        } else {
            None
        };
        for j in 0..draw_count {
            let d = descriptors.unwrap() + j * 0x30;
            let primitive = r.u32(d + 0x20)?;
            if primitive != 6 {
                return Err(format!("unverified MODEL primitive {primitive}"));
            }
            let n = r.u32(d + 0x24)? as usize;
            let indices = array(r, d, d + 0x28, n, 2)?;
            let mut referenced = HashSet::new();
            let mut min = [f32::INFINITY; 3];
            let mut max = [f32::NEG_INFINITY; 3];
            let mut max_index = None;
            let mut triangles = 0;
            let mut last = [0u16; 2];
            for k in 0..n {
                let idx = r.u16(indices.unwrap() + k * 2)?;
                if k >= 2 && idx != last[0] && idx != last[1] && last[0] != last[1] {
                    triangles += 1;
                }
                last = [last[1], idx];
                max_index = Some(max_index.map_or(idx, |old: u16| old.max(idx)));
                if referenced.insert(idx) {
                    let vertex = data
                        .unwrap()
                        .checked_add(
                            (idx as usize)
                                .checked_mul(stride)
                                .ok_or("vertex offset overflow")?,
                        )
                        .ok_or("vertex offset overflow")?;
                    r.range(vertex, stride)?;
                    for axis in 0..3 {
                        let v = r.float(vertex + axis * 4)?;
                        min[axis] = min[axis].min(v);
                        max[axis] = max[axis].max(v);
                    }
                }
            }
            let descriptor_bounds: Vec<_> = [0, 4, 8, 0x10, 0x14, 0x18]
                .into_iter()
                .map(|o| r.float(d + o))
                .collect::<Result<_>>()?;
            draws.push(json!({"offset":d,"primitive":primitive,"index_count":n,"index_offset":indices,
                "unique_referenced_vertices":referenced.len(),"max_index":max_index,"nondegenerate_triangles":triangles,
                "indexed_position_bounds":if n==0 {Value::Null}else{json!({"min":min,"max":max})},"descriptor_bounds_xyz":descriptor_bounds}));
        }
        let selection = selectors.unwrap() + i * 6;
        let raw_selectors: Vec<_> = (0..3)
            .map(|j| r.u16(selection + j * 2))
            .collect::<Result<_>>()?;
        parts.push(json!({"offset":vb,"vertex_data_offset":data,"stride":stride,"flags_raw":r.u32(vb+0x18)?,"selectors_raw":raw_selectors,"draws":draws}));
    }
    let bone_count = r.u32(base + 0x5c)? as usize;
    let matrices = array(r, base, base + 0x54, bone_count, 64)?;
    let index_words = array(r, base, base + 0x58, bone_count, 4)?;
    let names = array(r, base, base + 0x60, bone_count, 4)?;
    let mut bones = Vec::new();
    for i in 0..bone_count {
        // This relocated array is not established to be a parent hierarchy.
        let index_word = r.u32(index_words.unwrap() + i * 4)?;
        let matrix = matrices.unwrap() + i * 64;
        for j in 0..16 {
            r.float(matrix + j * 4)?;
        }
        let name = r.name(r.rel(base, names.unwrap() + i * 4)?, r.bytes.len())?;
        bones.push(
            json!({"index":i,"name":name,"index_word_raw":index_word,"matrix_offset":matrix}),
        );
    }
    let texture_count = r.u32(base + 0x50)? as usize;
    let texture_table = array(r, base, base + 0x4c, texture_count, 4)?;
    let mut texture_names = Vec::new();
    for i in 0..texture_count {
        texture_names.push(r.name(r.rel(base, texture_table.unwrap() + i * 4)?, r.bytes.len())?);
    }
    let lod_count = r.u8(base + 0x68)? as usize;
    let lods = array(r, base, base + 0x48, lod_count, 0xd0)?;
    let mut lod_offsets = Vec::new();
    for i in 0..lod_count {
        let at = lods.unwrap() + i * 0xd0;
        lod_offsets.push(at);
        for field in [0xc0, 0xc4, 0xc8] {
            if r.u32(at + field)? != 0 {
                r.range(r.rel(base, at + field)?, 1)?;
            }
        }
    }
    Ok(
        json!({"offset":base,"version":0x23,"bounds_vec4":bounds,"buffer_count":count,"buffers":parts,
        "bone_count":bone_count,"bones":bones,"texture_names":texture_names,"lod_offsets":lod_offsets,
        "opaque_model_fields":[r.u32(base+0x38)?,r.u32(base+0x3c)?,r.u32(base+0x40)?,r.u32(base+0x44)?]}),
    )
}

#[cfg(test)]
mod tests {
    use super::*;
    fn put(b: &mut [u8], at: usize, v: u32) {
        b[at..at + 4].copy_from_slice(&v.to_le_bytes());
    }
    fn fixture() -> Vec<u8> {
        let mut b = vec![0; 0x300];
        let p = 0x20;
        b[p + 0x10..p + 0x18].copy_from_slice(&0x95820ad4b8000000u64.to_le_bytes());
        put(&mut b, p + 0x18, 35);
        put(&mut b, p + 0x1c, 0x80);
        put(&mut b, p + 0x20, 0x70);
        put(&mut b, p + 0x24, 1);
        let v = p + 0x80;
        put(&mut b, v + 4, 0x90);
        put(&mut b, v + 12, 0x20);
        put(&mut b, v + 16, 1);
        put(&mut b, v + 20, 24);
        let d = v + 0x20;
        put(&mut b, d + 32, 6);
        put(&mut b, d + 36, 4);
        put(&mut b, d + 40, 0x60);
        for (i, index) in [0u16, 1, 2, 2].iter().enumerate() {
            b[d + 0x60 + i * 2..d + 0x62 + i * 2].copy_from_slice(&index.to_le_bytes());
        }
        for i in 0..3 {
            put(&mut b, v + 0x90 + i * 24, (i as f32).to_bits());
        }
        b
    }
    #[test]
    fn nested_pointer_bases_and_degenerate_strips() {
        let b = fixture();
        let v = model(
            &Reader {
                bytes: &b,
                big: false,
            },
            0x20,
        )
        .unwrap();
        let d = &v["buffers"][0]["draws"][0];
        assert_eq!(d["index_offset"], 0x120);
        assert_eq!(d["nondegenerate_triangles"], 1);
        assert_eq!(d["unique_referenced_vertices"], 3);
        assert_eq!(d["indexed_position_bounds"]["max"][0], 2.0);
    }
    #[test]
    fn referenced_vertex_must_fit() {
        let mut b = fixture();
        b[0x120..0x122].copy_from_slice(&u16::MAX.to_le_bytes());
        assert!(model(
            &Reader {
                bytes: &b,
                big: false
            },
            0x20
        )
        .is_err());
    }
    #[test]
    fn model_bone_matrices_are_bounded() {
        let mut b = fixture();
        put(&mut b, 0x20 + 0x5c, 1);
        put(&mut b, 0x20 + 0x54, 0x2e0);
        put(&mut b, 0x20 + 0x58, 0x240);
        put(&mut b, 0x20 + 0x60, 0x244);
        put(&mut b, 0x260, 1);
        assert!(model(
            &Reader {
                bytes: &b,
                big: false
            },
            0x20
        )
        .is_err());
    }
    #[test]
    fn unsupported_versions_are_not_guessed() {
        let mut b = fixture();
        put(&mut b, 0x38, 36);
        assert!(model(
            &Reader {
                bytes: &b,
                big: false
            },
            0x20
        )
        .is_err());
    }
    #[test]
    fn auxiliary_model_words_are_not_misrepresented_as_parents() {
        let mut b = fixture();
        let p = 0x20;
        put(&mut b, p + 0x5c, 1);
        put(&mut b, p + 0x54, 0x180);
        put(&mut b, p + 0x58, 0x1c0);
        put(&mut b, p + 0x60, 0x1c4);
        put(&mut b, p + 0x1c0, 0xcccccccc);
        put(&mut b, p + 0x1c4, 0x1d0);
        b[p + 0x1d0..p + 0x1d6].copy_from_slice(b"joint\0");
        let v = model(
            &Reader {
                bytes: &b,
                big: false,
            },
            p,
        )
        .unwrap();
        assert_eq!(v["bones"][0]["index_word_raw"], 0xccccccccu32);
        assert!(v["bones"][0].get("parent").is_none());
    }
}
