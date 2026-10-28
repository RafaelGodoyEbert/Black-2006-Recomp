//! Original read-only readers for the layouts in docs/file-formats/.
//! These validate selected structures, not complete models or runtime behavior.
use serde_json::{json, Value};
use std::collections::HashSet;
mod animation;
mod models;

pub type Result<T> = std::result::Result<T, String>;
const MOVIES: u64 = 0x9592e53726307940;
const TEXDIC: u64 = 0xbcdeed81543c0000;
const TEXTURE: u64 = 0xbcdf4eb7dd9a8000;
const TEXDICT: u64 = 0xbcdf4eb7e02064d5;
const MODELS: u64 = 0x95820af2485c0000;
const CHARACTER: u64 = 0x5a459d036341c357;
const ACTOR: u64 = 0x4df8c073a8963792;
const BNKS: u64 = 0x555c57e557000000;
const SHARED: u64 = 0xb767793c8161d7c0;
const RCBS: u64 = 0xb0dc09942f000000;

fn label(id: u64) -> &'static str {
    match id {
        TEXDIC => "TEXDIC",
        TEXTURE => "TEXTURE",
        TEXDICT => "TEXTUREDICTI",
        MODELS => "MODELS",
        CHARACTER => "CHARACTERDIC",
        ACTOR => "ACTORDICTION",
        BNKS => "BNKS",
        SHARED => "SHAREDBNKS",
        RCBS => "RCBS",
        0x96f6a8e27244ea15 => "MYDICTIONARY",
        0x4f883c088278aedd => "ANIMDICTIONA",
        MOVIES => "MOVIEARRAY",
        _ => "unknown",
    }
}

/// Base-40 GtID spelling; numeric keys (notably texture indices) are not names.
pub fn decode_gtid(mut id: u64) -> Option<String> {
    let alphabet = b" -/0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_";
    let mut out = [b' '; 12];
    for c in out.iter_mut().rev() {
        *c = alphabet[(id % 40) as usize];
        id /= 40;
    }
    (id == 0).then(|| String::from_utf8_lossy(&out).trim_end().to_owned())
}

struct Reader<'a> {
    bytes: &'a [u8],
    big: bool,
}
impl<'a> Reader<'a> {
    fn range(&self, at: usize, size: usize) -> Result<&'a [u8]> {
        self.bytes
            .get(at..at.checked_add(size).ok_or("range overflow")?)
            .ok_or_else(|| format!("range 0x{at:x}+0x{size:x} exceeds 0x{:x}", self.bytes.len()))
    }
    fn u8(&self, at: usize) -> Result<u8> {
        Ok(self.range(at, 1)?[0])
    }
    fn u16(&self, at: usize) -> Result<u16> {
        let a = self.range(at, 2)?.try_into().unwrap();
        Ok(if self.big {
            u16::from_be_bytes(a)
        } else {
            u16::from_le_bytes(a)
        })
    }
    fn u32(&self, at: usize) -> Result<u32> {
        let a = self.range(at, 4)?.try_into().unwrap();
        Ok(if self.big {
            u32::from_be_bytes(a)
        } else {
            u32::from_le_bytes(a)
        })
    }
    fn u64(&self, at: usize) -> Result<u64> {
        let a = self.range(at, 8)?.try_into().unwrap();
        Ok(if self.big {
            u64::from_be_bytes(a)
        } else {
            u64::from_le_bytes(a)
        })
    }
    fn float(&self, at: usize) -> Result<f32> {
        let value = f32::from_bits(self.u32(at)?);
        if !value.is_finite() {
            return Err(format!("non-finite float at 0x{at:x}"));
        }
        Ok(value)
    }
    fn table(&self, at: usize, count: usize, stride: usize) -> Result<()> {
        self.range(at, count.checked_mul(stride).ok_or("table overflow")?)?;
        Ok(())
    }
    fn rel(&self, base: usize, field: usize) -> Result<usize> {
        let value = self.u32(field)? as usize;
        if value == 0 {
            return Err(format!("null required pointer at 0x{field:x}"));
        }
        base.checked_add(value)
            .ok_or_else(|| "relative pointer overflow".into())
    }
    fn name(&self, at: usize, limit: usize) -> Result<String> {
        if at >= limit || limit > self.bytes.len() {
            return Err("name pointer out of range".into());
        }
        let b = &self.bytes[at..limit];
        let n = b.iter().position(|v| *v == 0).ok_or("unterminated name")?;
        Ok(String::from_utf8_lossy(&b[..n]).into_owned())
    }
    fn fixed_name(&self, at: usize, size: usize) -> Result<String> {
        let b = self.range(at, size)?;
        let n = b.iter().position(|v| *v == 0).unwrap_or(b.len());
        Ok(String::from_utf8_lossy(&b[..n]).into_owned())
    }
    fn expect(&self, at: usize, value: u32) -> Result<()> {
        let got = self.u32(at)?;
        if got != value {
            return Err(format!("0x{at:x}: expected 0x{value:x}, got 0x{got:x}"));
        }
        Ok(())
    }
}

fn texture(r: &Reader<'_>, at: usize) -> Result<Value> {
    r.range(at, 0x80)?;
    r.expect(at, 0x00040001)?;
    let data = r.rel(at, at + 4)?;
    let format = r.u32(at + 0x34)?;
    let w = r.u32(at + 0x38)? as usize;
    let h = r.u32(at + 0x3c)? as usize;
    if w == 0 || h == 0 || w > 16384 || h > 16384 {
        return Err("invalid texture dimensions".into());
    }
    let old = matches!(r.u32(at + 0x40)?, 4 | 8 | 32);
    let name = r.fixed_name(at + if old { 0x48 } else { 0x44 }, 32)?;
    let meta = at + if old { 0x68 } else { 0x64 };
    let depth = if old {
        r.u32(at + 0x40)?
    } else {
        r.u8(meta)? as u32
    };
    let palettes = r.u8(meta + 2)? as usize;
    let pixels = w.checked_mul(h).ok_or("texture size overflow")?;
    let size = match format {
        0xb => pixels,
        0xc => w.div_ceil(4) * h.div_ceil(4) * 8,
        0xe | 0xf => w.div_ceil(4) * h.div_ceil(4) * 16,
        0x3a => pixels * 4,
        _ => return Err(format!("unsupported Xbox texture format 0x{format:x}")),
    };
    r.range(data, size)?;
    if format == 0xb {
        let bytes = match depth {
            4 => 64,
            8 => 1024,
            _ => return Err("unsupported palette depth".into()),
        };
        r.table(at + 0x14, palettes, 4)?;
        for i in 0..palettes {
            let p = r.rel(at, at + 0x14 + i * 4)?;
            if !matches!(r.u32(p)?, 0x00030001 | 0xc0030001) {
                return Err("bad palette header".into());
            }
            let payload = r.rel(at, p + 4)?; // Relative to texture, not palette.
            r.range(payload, bytes)?;
        }
    }
    Ok(
        json!({"offset":at,"name":name,"data_offset":data,"format":format,
        "width":w,"height":h,"bit_depth":depth,"palettes":palettes,"top_mip_bytes":size,
        "header_revision":if old {"older"} else {"newer"}}),
    )
}

fn dictionary(r: &Reader<'_>, at: usize, expected: Option<u64>) -> Result<Value> {
    let id = r.u64(at)?;
    if expected.is_some_and(|v| v != id) || label(id) == "unknown" {
        return Err(format!("unexpected dictionary ID 0x{id:016x} at 0x{at:x}"));
    }
    let n = r.u32(at + 8)? as usize;
    let table = r.rel(at, at + 12)?;
    r.table(table, n, 16)?;
    let is_texture = matches!(id, TEXDIC | TEXTURE | TEXDICT);
    let mut entries = Vec::new();
    let mut textures = Vec::new();
    for i in 0..n {
        let e = table + i * 16;
        let key = r.u64(e)?;
        let value = r.rel(at, e + 8)?;
        r.range(value, 1)?;
        entries.push(json!({"key":format!("{key:016x}"),"key_name":if is_texture {None} else {decode_gtid(key)},"offset":value,"reserved":r.u32(e+12)?}));
        if is_texture {
            textures.push(texture(r, value)?);
        }
    }
    Ok(
        json!({"offset":at,"id":format!("{id:016x}"),"name":label(id),"count":n,"table_offset":table,"entries":entries,"textures":textures}),
    )
}

fn db_sections(r: &Reader<'_>, base: usize) -> Result<Vec<Value>> {
    r.expect(base + 0x18, 9)?;
    let types = [TEXTURE, MODELS, CHARACTER, ACTOR, BNKS, SHARED, RCBS];
    types
        .iter()
        .enumerate()
        .map(|(i, id)| {
            let at = r.rel(base, base + 0x1c + i * 4)?;
            dictionary(r, at, Some(*id))
        })
        .collect()
}

fn awd(bytes: &[u8]) -> Result<Value> {
    // This reader intentionally implements only the Xbox little-endian variant.
    let r = Reader { bytes, big: false };
    r.range(0, 0x2c)?;
    r.expect(0, 0x809)?;
    r.expect(4, 0)?;
    let data = r.u32(8)? as usize;
    let dict = r.u32(12)? as usize;
    if data != r.u32(0x28)? as usize || data > bytes.len() {
        return Err("invalid AWD header size".into());
    }
    if r.range(0x18, 16)?
        != [
            4, 45, 58, 69, 95, 228, 200, 75, 129, 240, 223, 117, 139, 1, 242, 115,
        ]
    {
        return Err("unsupported AWD platform UUID".into());
    }
    let payload_bytes = r.u32(0x14)? as usize;
    r.range(data, payload_bytes)?;
    let header = Reader {
        bytes: r.range(0, data)?,
        big: false,
    };
    let sentinel = dict.checked_add(12).ok_or("AWD sentinel overflow")?;
    header.range(dict, 20)?;
    let bank_name = match header.u32(dict + 4)? {
        0 => None,
        p => Some(header.name(p as usize, data)?),
    };
    let mut node = header.u32(sentinel + 4)? as usize;
    let mut previous = sentinel;
    let mut visited = HashSet::new();
    let mut entries = Vec::new();
    while node != sentinel {
        if !visited.insert(node) {
            return Err("AWD list cycles without returning to its sentinel".into());
        }
        header.range(node, 12)?;
        if (header.u32(node)? as usize) != previous {
            return Err("AWD previous link mismatch".into());
        }
        let info = header.u32(node + 8)? as usize;
        header.range(info, 0x5c)?;
        let name = header.name(header.u32(info + 4)? as usize, data)?;
        let rate = header.u32(info + 0x10)?;
        let codec = header.u32(info + 0x14)?;
        let size = header.u32(info + 0x18)? as usize;
        let channels = header.u8(info + 0x1d)? as usize;
        if channels != 1 || rate == 0 {
            return Err("unsupported AWD channel count or invalid rate".into());
        }
        let rel = header.u32(info + 0x4c)? as usize;
        let offset = data.checked_add(rel).ok_or("AWD sample offset overflow")?;
        if rel.checked_add(size).is_none_or(|end| end > payload_bytes) {
            return Err("AWD sample exceeds declared data size".into());
        }
        r.range(offset, size)?;
        let samples = match codec {
            1 if size % 2 == 0 => size / 2,
            4 if size % 36 == 0 => size / 36 * 64,
            _ => return Err(format!("unsupported AWD codec/size: {codec}/{size}")),
        };
        for field in [0x20, 0x3c] {
            let p = header.u32(info + field)? as usize;
            let n = header.u32(info + field + 4)? as usize;
            if n != 0 {
                header.range(p, n)?;
            }
        }
        entries.push(json!({"node_offset":node,"info_offset":info,"name":name,
            "sample_rate":rate,"codec":codec,"channels":channels,"bit_depth":header.u8(info+0x1c)?,
            "stream_offset":offset,"stream_size":size,"samples":samples,
            "seconds":samples as f64/rate as f64,
            "target_rate":header.u32(info+0x2c)?,"target_codec":header.u32(info+0x30)?,
            "target_size":header.u32(info+0x34)?,"target_offset_raw":header.u32(info+0x48)?}));
        previous = node;
        node = header.u32(node + 4)? as usize;
    }
    if header.u32(sentinel)? as usize != previous {
        return Err("AWD sentinel tail mismatch".into());
    }
    Ok(
        json!({"format":"awd-xbox","data_offset":data,"data_size":payload_bytes,
        "dictionary_offset":dict,"bank_name":bank_name,"entry_count":entries.len(),"entries":entries}),
    )
}

/// Inspect the named file. A filename selects non-magical layouts; each selected
/// reader still checks its version, required IDs, tables and payload bounds.
pub fn inspect(bytes: &[u8], name: &str) -> Result<Value> {
    let mut layout = inspect_layout(bytes, name)?;
    animation::enrich(&Reader { bytes, big: false }, &mut layout)?;
    Ok(layout)
}

fn inspect_layout(bytes: &[u8], name: &str) -> Result<Value> {
    let name = name.to_ascii_lowercase();
    let r = Reader { bytes, big: false };
    if name.ends_with(".awd") {
        return awd(bytes);
    }
    if name.ends_with(".db") {
        let ids: Vec<_> = (0..3)
            .map(|i| {
                r.u64(i * 8)
                    .map(|id| json!({"raw":format!("{id:016x}"),"gtid":decode_gtid(id)}))
            })
            .collect::<Result<_>>()?;
        return Ok(json!({"format":"db-v9","header_ids":ids,"dictionaries":db_sections(&r,0)?}));
    }
    if name == "globdata.bin" {
        r.expect(0, 9)?;
        let mut dicts = Vec::new();
        for i in 0..2 {
            dicts.push(dictionary(
                &r,
                r.rel(0, 4 + i * 4)?,
                Some(if i == 0 { TEXDIC } else { 0x96f6a8e27244ea15 }),
            )?);
        }
        let mut opaque = Vec::new();
        for field in [0x0c, 0x10, 0x14, 0x18] {
            let at = r.rel(0, field)?;
            r.range(at, 4)?;
            opaque.push(json!({"field":field,"offset":at,"first_word":r.u32(at)?}));
        }
        return Ok(json!({"format":"globdata-v9","dictionaries":dicts,"other_sections":opaque}));
    }
    if name == "guns.bin" || name == "guns_s.bin" {
        let tx = dictionary(&r, r.rel(0, 0)?, Some(TEXTURE))?;
        let models = dictionary(&r, r.rel(0, 4)?, Some(MODELS))?;
        return Ok(json!({"format":"guns","dictionaries":[tx,models]}));
    }
    if name == "stlevel.bin" {
        r.expect(0, 10)?;
        let base = r.rel(0, 12)?;
        let records = r.rel(0, 4)?;
        let count = r.u32(8)? as usize;
        r.table(records, count, 40)?;
        // The two record-relative fields can hold negative serialized placeholders.
        // Their runtime meaning is outside this reader's validation scope.
        let record_fields:Vec<_>=(0..count).map(|i|{let at=records+i*40;Ok(json!({"offset":at,"name":r.fixed_name(at,16)?,"id":format!("{:016x}",r.u64(at+16)?),"field_18_raw":r.u32(at+0x18)?,"field_1c_raw":r.u32(at+0x1c)?}))}).collect::<Result<_>>()?;
        let dicts = db_sections(&r, base)?;
        return Ok(
            json!({"format":"stlevel-v10","db_offset":base,"record_count":count,"records_offset":records,"records":record_fields,"dictionaries":dicts}),
        );
    }
    if name.starts_with("stunit") && name.ends_with(".bin") {
        r.expect(0, 3)?;
        let base = r.rel(0, 8)?;
        return Ok(
            json!({"format":"stunit-v3","db_offset":base,"dictionaries":db_sections(&r,base)?}),
        );
    }
    if name == "leveldat.bin" {
        r.expect(0, 0x11)?;
        let dictionaries: Vec<_> = (0..4)
            .map(|i| dictionary(&r, r.rel(0, 0x390 + i * 4)?, None))
            .collect::<Result<_>>()?;
        return Ok(json!({"format":"leveldat-v17","dictionaries":dictionaries}));
    }
    if name.starts_with("unit_") && name.ends_with(".bin") {
        r.expect(0, 0x18)?;
        let instances = r.u16(0x90)? as usize;
        let at = r.rel(0, 0x1c)?;
        r.table(at, instances, 48)?;
        let mut dictionaries = Vec::new();
        for (field, id) in [
            (0x20, 0x96f6a8e27244ea15),
            (0x24, TEXDICT),
            (0x28, 0x4f883c088278aedd),
        ] {
            dictionaries.push(dictionary(&r, r.rel(0, field)?, Some(id))?);
        }
        return Ok(
            json!({"format":"unit-v24","instance_count":instances,"instances_offset":at,"dictionaries":dictionaries}),
        );
    }
    if bytes.get(..4) == Some(b"4.1v") {
        let atlas = r.rel(0, 4)?;
        if atlas < 0x220 {
            return Err("font atlas overlaps header".into());
        }
        let tex = texture(&r, atlas)?;
        return Ok(json!({"format":"font-v1.4","atlas":tex,"glyph_records":(atlas-0x220)/0x28}));
    }
    if name == "mainus.bin" {
        let indexed = r.u16(0)? as usize;
        let total = r.u32(8)? as usize;
        let table = r.u32(12)? as usize;
        if indexed == 0 || total < indexed {
            return Err("invalid language record counts".into());
        }
        r.table(table, indexed, 4)?;
        let end_table = table + indexed * 4;
        let mut offsets = Vec::new();
        for i in 0..indexed {
            let at = r.u32(table + i * 4)? as usize;
            if at < end_table || offsets.last().is_some_and(|p| at <= *p) {
                return Err("language offsets overlap table or are unordered".into());
            }
            offsets.push(at);
        }
        let mut ids = HashSet::new();
        let mut cursor = 0;
        for i in 0..total {
            let at = if i < indexed { offsets[i] } else { cursor };
            let limit = if i + 1 < indexed {
                offsets[i + 1]
            } else {
                bytes.len()
            };
            let id = r.u32(at)?;
            if !ids.insert(id) {
                return Err("duplicate language ID".into());
            }
            let s = r.name(at + 4, limit)?;
            // Count source bytes; lossy decoding must not alter record boundaries.
            let raw = &bytes[at + 4..limit];
            let len = raw
                .iter()
                .position(|b| *b == 0)
                .ok_or("language terminator missing")?;
            std::str::from_utf8(&raw[..len]).map_err(|_| "invalid language UTF-8")?;
            cursor = (at + 4 + len + 1 + 3) & !3;
            let _ = s;
        }
        if cursor < bytes.len() && bytes[cursor..].iter().any(|b| *b != 0) {
            return Err("unparsed language tail".into());
        }
        return Ok(
            json!({"format":"mainus","indexed_records":indexed,"total_records":total,"table_offset":table}),
        );
    }
    if name == "trans_fp.bin" || name == "trans_ch.bin" {
        let n = r.u32(0)? as usize;
        let names = r.rel(0, 4)?;
        let kinds = r.rel(0, 8)?;
        let matrix = r.rel(0, 12)?;
        r.table(names, n, 4)?;
        r.table(kinds, n, 1)?;
        r.table(matrix, n, 48)?;
        for i in 0..n {
            let record = matrix + i * 48;
            if r.u32(record + 40)? != 0 {
                r.range(r.rel(record, record + 40)?, 1)?;
            }
        }
        for i in 0..n {
            r.name(r.rel(0, names + i * 4)?, bytes.len())?;
        }
        return Ok(
            json!({"format":"transition-table","count":n,"names_offset":names,"types_offset":kinds,"records_offset":matrix,"records_bytes":bytes.len()-matrix}),
        );
    }
    if bytes.len() >= 8 && r.u64(0)? == MOVIES {
        let n = r.u32(8)? as usize;
        let table = r.rel(0, 12)?;
        r.table(table, n, 4)?;
        let mut movies = Vec::new();
        for i in 0..n {
            let entry = r.rel(0, table + i * 4)?;
            let movie_name = r.name(r.rel(0, entry)?, bytes.len())?;
            let base = r.rel(0, entry + 4)?;
            let count = r.u32(base + 4)? as usize;
            let mut textures = Vec::new();
            if count > 0 {
                let refs = r.rel(base, base + 8)?;
                r.table(refs, count, 4)?;
                for j in 0..count {
                    textures.push(texture(&r, r.rel(base, refs + j * 4)?)?);
                }
            }
            movies.push(json!({"name":movie_name,"entry_offset":entry,"data_offset":base,"texture_count":count,"textures":textures}));
        }
        return Ok(json!({"format":"frontend-moviearray","movies":movies}));
    }
    Ok(json!({"format":"unsupported","reason":"No verified layout; file has not been interpreted"}))
}

#[cfg(test)]
mod tests {
    use super::*;
    fn put(b: &mut [u8], at: usize, v: u32) {
        b[at..at + 4].copy_from_slice(&v.to_le_bytes());
    }
    fn dict(b: &mut [u8], at: usize, id: u64, n: u32, table: u32) {
        b[at..at + 8].copy_from_slice(&id.to_le_bytes());
        put(b, at + 8, n);
        put(b, at + 12, table);
    }
    fn bank() -> Vec<u8> {
        let mut b = vec![0; 0x202];
        put(&mut b, 0, 0x809);
        put(&mut b, 8, 0x200);
        put(&mut b, 12, 0x2c);
        put(&mut b, 0x14, 2);
        put(&mut b, 0x28, 0x200);
        b[0x18..0x28].copy_from_slice(&[
            4, 45, 58, 69, 95, 228, 200, 75, 129, 240, 223, 117, 139, 1, 242, 115,
        ]);
        put(&mut b, 0x38, 0x50);
        put(&mut b, 0x3c, 0x50);
        put(&mut b, 0x50, 0x38);
        put(&mut b, 0x54, 0x38);
        put(&mut b, 0x58, 0x60);
        put(&mut b, 0x64, 0xc0);
        put(&mut b, 0x70, 22050);
        put(&mut b, 0x74, 1);
        put(&mut b, 0x78, 2);
        b[0x7c] = 16;
        b[0x7d] = 1;
        b[0xc0..0xc2].copy_from_slice(b"x\0");
        b
    }
    #[test]
    fn known_gtids() {
        assert_eq!(decode_gtid(MODELS).as_deref(), Some("MODELS"));
        assert_eq!(decode_gtid(TEXDIC).as_deref(), Some("TEXDIC"));
        assert_eq!(decode_gtid(u64::MAX), None);
    }
    #[test]
    fn valid_pcm_bank() {
        let v = inspect(&bank(), "sample.awd").unwrap();
        assert_eq!(v["entry_count"], 1);
        assert_eq!(v["entries"][0]["samples"], 1);
        assert_eq!(v["entries"][0]["stream_offset"], 0x200);
    }
    #[test]
    fn bad_awd_cycles_and_links() {
        let mut b = bank();
        put(&mut b, 0x54, 0x50);
        assert!(inspect(&b, "x.awd").is_err());
        let mut b = bank();
        put(&mut b, 0x50, 0);
        assert!(inspect(&b, "x.awd").is_err());
    }
    #[test]
    fn awd_payload_bounds() {
        let mut b = bank();
        put(&mut b, 0x78, 4);
        assert!(inspect(&b, "x.awd").is_err());
        let mut b = bank();
        put(&mut b, 0xac, u32::MAX);
        assert!(inspect(&b, "x.awd").is_err());
    }
    #[test]
    fn awd_names_are_header_bounded() {
        let mut b = bank();
        put(&mut b, 0x64, 0x200);
        assert!(inspect(&b, "x.awd").is_err());
    }
    #[test]
    fn dictionary_uses_its_own_base() {
        let mut b = vec![0; 0x100];
        dict(&mut b, 0x20, MODELS, 1, 0x10);
        put(&mut b, 0x38, 0x40);
        let r = Reader {
            bytes: &b,
            big: false,
        };
        let d = dictionary(&r, 0x20, Some(MODELS)).unwrap();
        assert_eq!(d["entries"][0]["offset"], 0x60);
    }
    #[test]
    fn dictionary_rejects_huge_table() {
        let mut b = vec![0; 0x20];
        dict(&mut b, 0, MODELS, u32::MAX, 0x10);
        let r = Reader {
            bytes: &b,
            big: false,
        };
        assert!(dictionary(&r, 0, None).is_err());
    }
    #[test]
    fn truncated_files_never_panic() {
        let b = bank();
        for len in 0..b.len() {
            assert!(inspect(&b[..len], "x.awd").is_err());
        }
    }
    #[test]
    fn unsupported_is_explicit() {
        assert_eq!(
            inspect(&[0; 16], "unknown.bin").unwrap()["format"],
            "unsupported"
        );
    }
    #[test]
    fn ima_blocks_are_36_bytes_for_64_samples() {
        let mut b = bank();
        b.resize(0x224, 0);
        put(&mut b, 0x14, 36);
        put(&mut b, 0x74, 4);
        put(&mut b, 0x78, 36);
        assert_eq!(inspect(&b, "x.awd").unwrap()["entries"][0]["samples"], 64);
        put(&mut b, 0x78, 35);
        assert!(inspect(&b, "x.awd").is_err());
    }
    #[test]
    fn empty_awd_returns_to_sentinel() {
        let mut b = bank();
        put(&mut b, 0x38, 0x38);
        put(&mut b, 0x3c, 0x38);
        assert_eq!(inspect(&b, "x.awd").unwrap()["entry_count"], 0);
    }
    #[test]
    fn awd_descriptor_must_fit_inside_header() {
        let mut b = bank();
        put(&mut b, 0x58, 0x1f0);
        assert!(inspect(&b, "x.awd").is_err());
    }
    #[test]
    fn stlevel_uses_db_texture_field_not_outer_record_field() {
        let mut b = vec![0; 0x400];
        put(&mut b, 0, 10);
        put(&mut b, 4, 0x40);
        put(&mut b, 8, 1);
        put(&mut b, 12, 0x100);
        put(&mut b, 0x58, 0xfcb2ff00);
        put(&mut b, 0x118, 9);
        for (i, id) in [TEXTURE, MODELS, CHARACTER, ACTOR, BNKS, SHARED, RCBS]
            .iter()
            .enumerate()
        {
            let at = 0x180 + i * 0x20;
            put(&mut b, 0x11c + i * 4, (at - 0x100) as u32);
            dict(&mut b, at, *id, 0, 0x10);
        }
        let v = inspect(&b, "StLevel.bin").unwrap();
        assert_eq!(v["records_offset"], 0x40);
        assert_eq!(v["dictionaries"][0]["offset"], 0x180);
        assert_eq!(v["records"][0]["field_18_raw"], 0xfcb2ff00u32);
    }
    #[test]
    fn mainus_tail_and_utf8_are_checked() {
        let mut b = vec![0; 36];
        b[0] = 1;
        put(&mut b, 8, 2);
        put(&mut b, 12, 16);
        put(&mut b, 16, 20);
        put(&mut b, 20, 7);
        b[24..27].copy_from_slice(&[0xc3, 0xa9, 0]);
        put(&mut b, 28, 8);
        b[32..34].copy_from_slice(b"z\0");
        assert_eq!(inspect(&b, "MainUS.bin").unwrap()["total_records"], 2);
        b[24] = 0xff;
        assert!(inspect(&b, "MainUS.bin").is_err());
    }
    #[test]
    fn malformed_supported_layouts_return_errors_without_panicking() {
        let names = [
            "x.db",
            "x.awd",
            "GlobData.bin",
            "guns.bin",
            "StLevel.bin",
            "StUnit01.bin",
            "LevelDat.bin",
            "Unit_01.bin",
            "MainUS.bin",
            "trans_ch.bin",
        ];
        let mut state = 0x91827364u32;
        for length in 0..256 {
            let mut b = vec![0; length];
            for byte in &mut b {
                state ^= state << 13;
                state ^= state >> 17;
                state ^= state << 5;
                *byte = state as u8;
            }
            for name in names {
                let _ = inspect(&b, name);
            }
        }
    }
}
