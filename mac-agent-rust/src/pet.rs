//! Bounded GIF conversion and canonical CXP1 packages used by the existing watch.
use anyhow::{Context, Result, bail, ensure};
use image::{AnimationDecoder, ImageDecoder, RgbaImage};
use sha2::{Digest, Sha256};
use std::{collections::BTreeMap, fs, io::{BufReader, Cursor, Read}, path::Path};

pub const WIDTH: u16 = 144;
pub const HEIGHT: u16 = 156;
pub const MAX_PACKAGE_BYTES: usize = 256 * 1024;
const PIXELS: usize = WIDTH as usize * HEIGHT as usize;
const HEADER: usize = 72;
const MAX_DECODED_PIXELS: usize = 64_000_000;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PetFrame { pub duration_ms: u16, pub indices: Vec<u8> }
#[derive(Clone, Debug)]
pub struct PetPackage {
    pub encoded: Vec<u8>, pub digest: [u8; 32],
    pub width: u16, pub height: u16, pub frames: Vec<PetFrame>,
    pub palette: Vec<u16>, pub transparent_index: u16,
}

pub fn packbits_encode(data: &[u8]) -> Vec<u8> {
    let mut out = Vec::new();
    let mut cursor = 0;
    while cursor < data.len() {
        let mut run = 1;
        while run < 130 && cursor + run < data.len() && data[cursor + run] == data[cursor] { run += 1; }
        if run >= 3 {
            // CXP1 follows the original Python/C++ PackBits variant: controls
            // represent 258 - control bytes, including 130 bytes at 0x80.
            out.extend_from_slice(&[(258 - run) as u8, data[cursor]]);
            cursor += run;
            continue;
        } else {
            let start = cursor;
            cursor += run;
            while cursor < data.len() && cursor - start < 128 {
                let mut next_run = 1;
                while cursor + next_run < data.len() && next_run < 130 && data[cursor + next_run] == data[cursor] { next_run += 1; }
                if next_run >= 3 { break }
                if cursor - start + next_run > 128 { break }
                cursor += next_run;
            }
            out.push((cursor - start - 1) as u8);
            out.extend_from_slice(&data[start..cursor]);
        }
    }
    out
}

pub fn packbits_decode(data: &[u8], expected: usize) -> Result<Vec<u8>> {
    ensure!(expected <= MAX_DECODED_PIXELS, "PackBits output exceeds decoded pixel limit");
    let mut out = Vec::with_capacity(expected);
    let mut i = 0;
    while i < data.len() {
        let control = data[i]; i += 1;
        match control {
            0..=127 => {
                let count = control as usize + 1;
                ensure!(i + count <= data.len() && count <= expected - out.len(), "invalid PackBits literal");
                out.extend_from_slice(&data[i..i + count]); i += count;
            }
            128..=255 => {
                let count = 258 - control as usize;
                ensure!(i < data.len() && count <= expected - out.len(), "invalid PackBits run");
                out.resize(out.len() + count, data[i]); i += 1;
            }
        }
    }
    ensure!(out.len() == expected, "incomplete PackBits frame");
    Ok(out)
}

fn put_u16(out: &mut Vec<u8>, n: u16) { out.extend_from_slice(&n.to_le_bytes()); }
fn put_u32(out: &mut Vec<u8>, n: usize) { out.extend_from_slice(&(n as u32).to_le_bytes()); }
fn u16_at(data: &[u8], i: usize) -> Result<u16> { Ok(u16::from_le_bytes(data.get(i..i + 2).context("truncated CXP1")?.try_into()?)) }
fn u32_at(data: &[u8], i: usize) -> Result<usize> { Ok(u32::from_le_bytes(data.get(i..i + 4).context("truncated CXP1")?.try_into()?) as usize) }

pub fn encode_package(palette: Vec<u16>, frames: Vec<PetFrame>) -> Result<PetPackage> {
    ensure!((1..=255).contains(&palette.len()) && (1..=24).contains(&frames.len()), "invalid palette or frame count");
    let transparent_index = palette.len() as u16;
    for f in &frames {
        ensure!(f.indices.len() == PIXELS && (60..=1000).contains(&f.duration_ms), "invalid pet frame");
        ensure!(f.indices.iter().all(|i| *i as u16 <= transparent_index), "invalid palette index");
    }
    let palette_offset = HEADER + frames.len() * 12;
    let payload_offset = palette_offset + palette.len() * 2;
    let compressed: Vec<_> = frames.iter().map(|f| packbits_encode(&f.indices)).collect();
    let length = payload_offset + compressed.iter().map(Vec::len).sum::<usize>();
    ensure!(length <= MAX_PACKAGE_BYTES, "pet package exceeds watch capacity");
    let mut encoded = Vec::with_capacity(length);
    encoded.extend_from_slice(b"CXP1");
    for n in [1, HEADER as u16, WIDTH, HEIGHT, frames.len() as u16, palette.len() as u16, transparent_index, 0] { put_u16(&mut encoded, n); }
    for n in [HEADER, palette_offset, payload_offset, length, PIXELS * frames.len()] { put_u32(&mut encoded, n); }
    encoded.extend_from_slice(&[0; 32]);
    let mut offset = payload_offset;
    for (f, compressed) in frames.iter().zip(&compressed) {
        put_u16(&mut encoded, f.duration_ms); put_u16(&mut encoded, 0);
        put_u32(&mut encoded, offset); put_u32(&mut encoded, compressed.len()); offset += compressed.len();
    }
    for color in &palette { put_u16(&mut encoded, *color); }
    for frame in &compressed { encoded.extend_from_slice(frame); }
    let digest: [u8; 32] = Sha256::digest(&encoded).into();
    encoded[40..72].copy_from_slice(&digest);
    Ok(PetPackage { encoded, digest, width: WIDTH, height: HEIGHT, frames, palette, transparent_index })
}

pub fn parse_package(data: &[u8]) -> Result<PetPackage> {
    ensure!((HEADER..=MAX_PACKAGE_BYTES).contains(&data.len()), "invalid pet package size");
    ensure!(&data[0..4] == b"CXP1" && u16_at(data, 4)? == 1 && u16_at(data, 6)? == HEADER as u16, "invalid pet header");
    ensure!(u16_at(data, 8)? == WIDTH && u16_at(data, 10)? == HEIGHT, "unsupported pet dimensions");
    let frame_count = u16_at(data, 12)? as usize;
    let palette_count = u16_at(data, 14)? as usize;
    ensure!((1..=24).contains(&frame_count) && (1..=255).contains(&palette_count), "invalid pet counts");
    ensure!(u16_at(data, 16)? as usize == palette_count && u16_at(data, 18)? == 0, "invalid pet flags");
    let palette_offset = HEADER + frame_count * 12;
    let payload_offset = palette_offset + palette_count * 2;
    ensure!(u32_at(data, 20)? == HEADER && u32_at(data, 24)? == palette_offset && u32_at(data, 28)? == payload_offset, "invalid pet offsets");
    ensure!(u32_at(data, 32)? == data.len() && u32_at(data, 36)? == PIXELS * frame_count && payload_offset <= data.len(), "invalid pet length");
    let digest: [u8; 32] = data[40..72].try_into()?;
    let mut canonical = data.to_vec(); canonical[40..72].fill(0);
    ensure!(<[u8; 32]>::from(Sha256::digest(&canonical)) == digest, "pet digest mismatch");
    let palette = (0..palette_count).map(|i| u16_at(data, palette_offset + i * 2)).collect::<Result<Vec<_>>>()?;
    let mut frames = Vec::with_capacity(frame_count);
    let mut previous_end = payload_offset;
    for i in 0..frame_count {
        let e = HEADER + 12 * i;
        let duration_ms = u16_at(data, e)?;
        let offset = u32_at(data, e + 4)?;
        let length = u32_at(data, e + 8)?;
        let end = offset.checked_add(length).context("pet span overflow")?;
        ensure!((60..=1000).contains(&duration_ms) && u16_at(data, e + 2)? == 0 && length > 0 && offset == previous_end && end <= data.len(), "invalid pet frame span");
        let indices = packbits_decode(&data[offset..end], PIXELS)?;
        ensure!(indices.iter().all(|n| *n as usize <= palette_count), "pet palette index outside bounds");
        frames.push(PetFrame { duration_ms, indices }); previous_end = end;
    }
    ensure!(previous_end == data.len(), "pet trailing bytes");
    Ok(PetPackage { encoded: data.to_vec(), digest, width: WIDTH, height: HEIGHT, frames, palette, transparent_index: palette_count as u16 })
}

pub fn render_rgba(package: &PetPackage, index: usize) -> Result<RgbaImage> {
    let frame = package.frames.get(index).context("unknown pet frame")?;
    let mut output = Vec::with_capacity(PIXELS * 4);
    for &n in &frame.indices {
        if n as u16 == package.transparent_index { output.extend_from_slice(&[0, 0, 0, 0]); }
        else {
            let color = *package.palette.get(n as usize).context("invalid pet palette")?;
            let (r, g, b) = ((color >> 11) & 31, (color >> 5) & 63, color & 31);
            output.extend_from_slice(&[((r << 3) | (r >> 2)) as u8, ((g << 2) | (g >> 4)) as u8, ((b << 3) | (b >> 2)) as u8, 255]);
        }
    }
    RgbaImage::from_raw(WIDTH as u32, HEIGHT as u32, output).context("invalid pet image")
}

fn bounded_delays(delays: &[u32]) -> Vec<u16> {
    let mut result: Vec<i64> = delays.iter().map(|d| (*d).clamp(60, 1000) as i64).collect();
    let target = delays.iter().map(|d| *d as i64).sum::<i64>().clamp(delays.len() as i64 * 60, delays.len() as i64 * 1000);
    let mut residual = target - result.iter().sum::<i64>();
    let order = (0..delays.len()).filter(|i| (60..=1000).contains(&delays[*i])).chain((0..delays.len()).filter(|i| !(60..=1000).contains(&delays[*i])));
    for i in order {
        let adjustment = if residual > 0 { residual.min(1000 - result[i]) } else { residual.max(60 - result[i]) };
        result[i] += adjustment; residual -= adjustment;
    }
    result.into_iter().map(|d| d as u16).collect()
}

fn rgb565(pixel: &image::Rgba<u8>) -> u16 { ((pixel[0] as u16 >> 3) << 11) | ((pixel[1] as u16 >> 2) << 5) | (pixel[2] as u16 >> 3) }
fn channels(color: u16) -> [u16; 3] { [(color >> 11) & 31, (color >> 5) & 63, color & 31] }

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct GifInspection { width: u32, height: u32, frame_count: usize }

// Port of the bounded block preflight in pet_converter.py, distributed under
// this repository's MIT license (Copyright (c) 2026 M5Stack Technology CO LTD).
fn gif_need(source: &[u8], cursor: usize, count: usize) -> Result<()> {
    let end = cursor.checked_add(count).context("GIF block offset overflow")?;
    ensure!(end <= source.len(), "truncated GIF block stream");
    Ok(())
}

fn gif_skip_sub_blocks(source: &[u8], mut cursor: usize) -> Result<usize> {
    loop {
        gif_need(source, cursor, 1)?;
        let length = source[cursor] as usize;
        cursor += 1;
        if length == 0 { return Ok(cursor) }
        gif_need(source, cursor, length)?;
        cursor += length;
    }
}

fn inspect_gif_blocks(source: &[u8]) -> Result<GifInspection> {
    gif_need(source, 0, 13)?;
    ensure!(&source[..6] == b"GIF87a" || &source[..6] == b"GIF89a", "invalid GIF signature");
    let width = u16::from_le_bytes([source[6], source[7]]) as u32;
    let height = u16::from_le_bytes([source[8], source[9]]) as u32;
    ensure!(width > 0 && height > 0, "GIF logical canvas is empty");
    let packed = source[10];
    let mut cursor = 13usize;
    if packed & 0x80 != 0 {
        let table_size = 3usize * (1usize << ((packed & 0x07) + 1));
        gif_need(source, cursor, table_size)?;
        cursor += table_size;
    }

    let mut frame_count = 0usize;
    let mut saw_trailer = false;
    while cursor < source.len() {
        let marker = source[cursor];
        cursor += 1;
        if marker == 0x3b {
            saw_trailer = true;
            break;
        }
        if marker == 0x21 {
            gif_need(source, cursor, 1)?;
            cursor = gif_skip_sub_blocks(source, cursor + 1)?;
            continue;
        }
        ensure!(marker == 0x2c, "unknown GIF block marker");
        gif_need(source, cursor, 9)?;
        let left = u16::from_le_bytes([source[cursor], source[cursor + 1]]) as u32;
        let top = u16::from_le_bytes([source[cursor + 2], source[cursor + 3]]) as u32;
        let frame_width = u16::from_le_bytes([source[cursor + 4], source[cursor + 5]]) as u32;
        let frame_height = u16::from_le_bytes([source[cursor + 6], source[cursor + 7]]) as u32;
        let image_packed = source[cursor + 8];
        cursor += 9;
        ensure!(frame_width > 0 && frame_height > 0 && left + frame_width <= width && top + frame_height <= height, "GIF image descriptor is outside its canvas");
        if image_packed & 0x80 != 0 {
            let table_size = 3usize * (1usize << ((image_packed & 0x07) + 1));
            gif_need(source, cursor, table_size)?;
            cursor += table_size;
        }
        gif_need(source, cursor, 1)?; // LZW minimum code size
        cursor = gif_skip_sub_blocks(source, cursor + 1)?;
        frame_count = frame_count.checked_add(1).context("GIF frame count overflow")?;
    }
    ensure!(saw_trailer, "truncated GIF is missing its trailer");
    ensure!(cursor == source.len(), "GIF has trailing bytes after its trailer");
    ensure!(frame_count > 0, "GIF contains no image frames");
    ensure!(width <= 1024 && height <= 1024, "GIF canvas exceeds 1024 x 1024");
    ensure!(frame_count <= 240, "GIF has more than 240 source frames");
    ensure!(width as u64 * height as u64 * frame_count as u64 <= MAX_DECODED_PIXELS as u64, "GIF decoded pixel budget exceeds 64,000,000");
    Ok(GifInspection { width, height, frame_count })
}

fn palette_for(histogram: &BTreeMap<u16, u64>) -> Vec<u16> {
    if histogram.len() <= 255 { return if histogram.is_empty() { vec![0] } else { histogram.keys().copied().collect() } }
    // Weighted median cut creates one palette across the whole animation.
    let mut boxes: Vec<Vec<(u16, u64)>> = vec![histogram.iter().map(|(c, n)| (*c, *n)).collect()];
    while boxes.len() < 255 {
        let choice = boxes.iter().enumerate().filter(|(_, b)| b.len() > 1).max_by_key(|(_, b)| b.iter().map(|(_, n)| *n).sum::<u64>()).map(|(i, _)| i);
        let Some(index) = choice else { break };
        let mut block = boxes.swap_remove(index);
        let axis = (0..3).max_by_key(|&a| {
            let min = block.iter().map(|(c, _)| channels(*c)[a]).min().unwrap_or(0);
            let max = block.iter().map(|(c, _)| channels(*c)[a]).max().unwrap_or(0);
            (max - min) * if a == 1 { 1 } else { 2 }
        }).unwrap();
        block.sort_by_key(|(c, _)| channels(*c)[axis]);
        let total = block.iter().map(|(_, n)| *n).sum::<u64>();
        let mut accumulated = 0;
        let mut split = 1;
        for (i, (_, n)) in block.iter().enumerate().take(block.len() - 1) {
            accumulated += *n; split = i + 1; if accumulated >= total / 2 { break }
        }
        let other = block.split_off(split); boxes.push(block); boxes.push(other);
    }
    boxes.iter().map(|b| {
        let total = b.iter().map(|(_, n)| *n).sum::<u64>().max(1);
        let means = [0, 1, 2].map(|a| (b.iter().map(|(c, n)| channels(*c)[a] as u64 * n).sum::<u64>() / total) as u16);
        (means[0] << 11) | (means[1] << 5) | means[2]
    }).collect()
}

pub fn convert_gif(path: &Path) -> Result<PetPackage> {
    let file = fs::File::open(path)?;
    ensure!(file.metadata()?.len() <= 10 * 1024 * 1024, "GIF exceeds input limit");
    let mut raw = Vec::new(); file.take(10 * 1024 * 1024 + 1).read_to_end(&mut raw)?;
    ensure!(raw.len() <= 10 * 1024 * 1024 && raw.len() >= 13 && (&raw[..6] == b"GIF87a" || &raw[..6] == b"GIF89a"), "invalid GIF");
    let inspection = inspect_gif_blocks(&raw)?;
    let mut decoder = image::codecs::gif::GifDecoder::new(BufReader::new(Cursor::new(raw)))?;
    let (w, h) = decoder.dimensions();
    ensure!((w, h) == (inspection.width, inspection.height), "GIF canvas changed during decode");
    let mut limits = image::Limits::default();
    limits.max_image_width = Some(1024); limits.max_image_height = Some(1024); limits.max_alloc = Some(64 * 1024 * 1024);
    decoder.set_limits(limits)?;
    let mut decoded: Vec<(RgbaImage, u32)> = Vec::new();
    let mut count = 0_u64;
    for frame in decoder.into_frames() {
        let frame = frame?; count += 1;
        ensure!(count <= inspection.frame_count as u64 && count <= 240 && count * w as u64 * h as u64 <= MAX_DECODED_PIXELS as u64, "GIF decoded size exceeds limit");
        let (numerator, denominator) = frame.delay().numer_denom_ms();
        let delay = if denominator == 0 { 0 } else { numerator / denominator };
        let img = frame.into_buffer();
        if let Some((previous, prior_delay)) = decoded.last_mut() {
            if previous == &img { *prior_delay = prior_delay.saturating_add(delay); continue }
        }
        decoded.push((img, delay));
    }
    ensure!(count == inspection.frame_count as u64, "GIF image count changed during decode");
    ensure!(!decoded.is_empty(), "GIF has no frames");
    if decoded.len() > 24 {
        let total = decoded.iter().map(|(_, d)| *d as u64).sum::<u64>().max(decoded.len() as u64);
        let mut sampled: Vec<(RgbaImage, u32)> = Vec::new();
        let mut fi = 0; let mut boundary = decoded[0].1 as u64;
        for i in 0..24 {
            let start = ((i as f64 * total as f64) / 24.0).round() as u64;
            let end = (((i + 1) as f64 * total as f64) / 24.0).round() as u64;
            while fi + 1 < decoded.len() && boundary <= start { fi += 1; boundary += decoded[fi].1 as u64; }
            if let Some((previous, delay)) = sampled.last_mut() {
                if previous == &decoded[fi].0 { *delay += (end - start) as u32; continue }
            }
            sampled.push((decoded[fi].0.clone(), (end - start) as u32));
        }
        decoded = sampled;
    }
    let delays = bounded_delays(&decoded.iter().map(|(_, d)| *d).collect::<Vec<_>>());
    let mut canvases = Vec::new();
    let mut histogram = BTreeMap::new();
    for (img, _) in decoded {
        let scale = (WIDTH as f64 / img.width() as f64).min(HEIGHT as f64 / img.height() as f64);
        let width = (img.width() as f64 * scale).round().max(1.0) as u32;
        let height = (img.height() as f64 * scale).round().max(1.0) as u32;
        let resized = image::imageops::resize(&img, width, height, image::imageops::FilterType::Nearest);
        let mut canvas = RgbaImage::new(WIDTH as u32, HEIGHT as u32);
        image::imageops::overlay(&mut canvas, &resized, ((WIDTH as u32 - width) / 2) as i64, ((HEIGHT as u32 - height) / 2) as i64);
        for p in canvas.pixels().filter(|p| p[3] >= 128) { *histogram.entry(rgb565(p)).or_insert(0) += 1; }
        canvases.push(canvas);
    }
    let palette = palette_for(&histogram);
    let mut lookup = BTreeMap::new();
    for color in histogram.keys() {
        let c = channels(*color);
        let index = palette.iter().enumerate().min_by_key(|(_, p)| {
            let p = channels(**p);
            let dr = c[0] as i32 - p[0] as i32; let dg = c[1] as i32 - p[1] as i32; let db = c[2] as i32 - p[2] as i32;
            4 * dr * dr + dg * dg + 4 * db * db
        }).unwrap().0 as u8;
        lookup.insert(*color, index);
    }
    let frames = canvases.iter().zip(delays).map(|(canvas, duration_ms)| PetFrame { duration_ms,
        indices: canvas.pixels().map(|p| if p[3] < 128 { palette.len() as u8 } else { lookup[&rgb565(p)] }).collect() }).collect();
    encode_package(palette, frames)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn package_bounds_roundtrip_and_digest_corruption() {
        let p = encode_package(vec![0xF800, 0x07E0], vec![PetFrame { duration_ms: 120, indices: (0..PIXELS).map(|i| (i % 3) as u8).collect() }]).unwrap();
        let decoded = parse_package(&p.encoded).unwrap();
        assert_eq!(decoded.frames, p.frames);
        assert_eq!(decoded.digest, p.digest);
        assert_eq!(render_rgba(&decoded, 0).unwrap().get_pixel(2, 0)[3], 0);
        let mut corrupt = p.encoded; corrupt[73] ^= 1;
        assert!(parse_package(&corrupt).is_err());
        assert!(packbits_decode(&[128], 1).is_err());
        assert!(packbits_decode(&[0], 1).is_err());
        assert!(encode_package(vec![0], vec![PetFrame { duration_ms: 59, indices: vec![0; PIXELS] }]).is_err());
    }
    #[test]
    fn legacy_packbits_run_and_literal_boundaries() {
        for (length, control) in [(2, None), (3, Some(255)), (127, Some(131)), (128, Some(130)), (129, Some(129)), (130, Some(128))] {
            let data = vec![7; length];
            let encoded = packbits_encode(&data);
            if let Some(control) = control {
                assert_eq!(encoded, vec![control, 7]);
            } else {
                assert_eq!(encoded, vec![1, 7, 7]);
            }
            assert_eq!(packbits_decode(&encoded, length).unwrap(), data);
        }
        let split_run = packbits_encode(&vec![7; 131]);
        assert_eq!(split_run, vec![128, 7, 0, 7]);
        assert_eq!(packbits_decode(&split_run, 131).unwrap(), vec![7; 131]);

        let literals: Vec<u8> = (0..=127).collect();
        assert_eq!(packbits_encode(&literals), [vec![127], literals.clone()].concat());
        let literals: Vec<u8> = (0..=128).collect();
        let encoded = packbits_encode(&literals);
        assert_eq!(&encoded[..129], [vec![127], (0..=127).collect()].concat());
        assert_eq!(&encoded[129..], &[0, 128]);
        assert_eq!(packbits_decode(&encoded, literals.len()).unwrap(), literals);

        for data in [vec![7; 257], (0..=255).collect(), vec![0, 0, 1, 2, 2, 2, 3]] {
            assert_eq!(packbits_decode(&packbits_encode(&data), data.len()).unwrap(), data);
        }
        assert_eq!(packbits_decode(&[128, 5], 130).unwrap(), vec![5; 130]);
        assert_eq!(packbits_decode(&[129, 5], 129).unwrap(), vec![5; 129]);
        assert_eq!(packbits_decode(&[255, 5], 3).unwrap(), vec![5; 3]);
        assert!(packbits_decode(&[128], 130).is_err());
        assert!(packbits_decode(&[127, 1], 128).is_err());
        assert!(packbits_decode(&[128, 5], 129).is_err());
        assert!(packbits_decode(&[], usize::MAX).is_err());
    }

    fn header_only_gif(width: u16, height: u16, frames: usize) -> Vec<u8> {
        let mut source = b"GIF89a".to_vec();
        source.extend_from_slice(&width.to_le_bytes());
        source.extend_from_slice(&height.to_le_bytes());
        source.extend_from_slice(&[0, 0, 0]);
        for _ in 0..frames {
            source.push(0x2c);
            source.extend_from_slice(&[0, 0, 0, 0]);
            source.extend_from_slice(&width.to_le_bytes());
            source.extend_from_slice(&height.to_le_bytes());
            source.extend_from_slice(&[0, 2, 1, 0, 0]);
        }
        source.push(0x3b);
        source
    }

    #[test]
    fn gif_block_preflight_checks_limits_before_decode() {
        let max_pixels = header_only_gif(1024, 1024, 61);
        assert_eq!(inspect_gif_blocks(&max_pixels).unwrap(), GifInspection { width: 1024, height: 1024, frame_count: 61 });
        assert!(inspect_gif_blocks(&header_only_gif(1024, 1024, 62)).is_err());
        assert_eq!(inspect_gif_blocks(&header_only_gif(1, 1, 240)).unwrap().frame_count, 240);
        assert!(inspect_gif_blocks(&header_only_gif(1, 1, 241)).is_err());

        let mut truncated = header_only_gif(1, 1, 1);
        truncated.pop();
        assert!(inspect_gif_blocks(&truncated).is_err());
        let mut trailing = header_only_gif(1, 1, 1);
        trailing.push(0);
        assert!(inspect_gif_blocks(&trailing).is_err());
    }
    #[test]
    fn delays_preserve_loop_when_possible() {
        assert_eq!(bounded_delays(&[10, 120, 400]), vec![60, 70, 400]);
        // Upstream _redistribute_delays preserves the 2000 ms bounded loop by
        // redistributing the long frame's excess time into the first frame.
        assert_eq!(bounded_delays(&[0, 4000]), vec![1000, 1000]);
    }
}
