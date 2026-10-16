use std::{fs, os::unix::fs::{FileTypeExt, symlink}, path::Path, time::Duration};
use stopwatch_codex_agent::{pet::{PetFrame, PetPackage, WIDTH, HEIGHT, convert_gif, encode_package, parse_package, render_rgba}, pet_cache::PetCache};

const WATCH_ID: &str = "00000000000000000000000000000001";
const PIXELS: usize = WIDTH as usize * HEIGHT as usize;

fn package(color: u16) -> PetPackage {
    encode_package(vec![color], vec![PetFrame { duration_ms: 120, indices: vec![0; PIXELS] }]).unwrap()
}

fn pixel(package: &PetPackage, frame: usize, x: u32, y: u32) -> [u8; 4] {
    render_rgba(package, frame).unwrap().get_pixel(x, y).0
}

fn watch_cache(root: &Path) -> PetCache {
    PetCache::new(root.join("Pets"))
}

#[test]
fn parses_python_cpp_golden_with_legacy_packbits_and_transparency() {
    // This is the existing Python/C++ CXP1 fixture, copied unchanged so the
    // Rust crate can validate the same on-wire bytes without changing oracle data.
    let hex = include_str!("fixtures/pet-cxp1-minimal.hex").trim();
    let encoded = hex::decode(hex).unwrap();
    let parsed = parse_package(&encoded).unwrap();
    assert_eq!(parsed.width, 144);
    assert_eq!(parsed.height, 156);
    assert_eq!(parsed.palette, vec![0xffff]);
    assert_eq!(parsed.transparent_index, 1);
    assert_eq!(parsed.frames.len(), 2);
    assert_eq!(parsed.frames[0].duration_ms, 180);
    assert_eq!(parsed.frames[1].duration_ms, 240);
    assert!(parsed.frames[0].indices.iter().all(|index| *index == 0));
    assert!(parsed.frames[1].indices.iter().all(|index| *index == 1));
    assert_eq!(hex::encode(parsed.digest), "1c636849611263e7c3dfbc351a8b5e4b296c1659e202410d32893858a334c978");
    assert_eq!(pixel(&parsed, 0, 0, 0), [255, 255, 255, 255]);
    assert_eq!(pixel(&parsed, 1, 0, 0), [0, 0, 0, 0]);
}

#[test]
fn converts_bounded_gif_with_disposal_and_transparent_pixels() {
    // MIT fixture follows the source shape used by pet_converter.py's disposal test.
    let directory = tempfile::tempdir().unwrap();
    let path = directory.path().join("pet-disposal-alpha.gif");
    fs::write(&path, include_bytes!("fixtures/pet-disposal-alpha.gif")).unwrap();
    let converted = convert_gif(&path).unwrap();
    assert_eq!((converted.width, converted.height), (144, 156));
    assert_eq!(converted.frames.len(), 5);
    assert_eq!(converted.frames.iter().map(|frame| frame.duration_ms).collect::<Vec<_>>(), vec![100; 5]);

    let center = |x: u32, y: u32| (18 + x * 36, 24 + y * 36);
    let (x, y) = center(0, 0);
    assert_eq!(pixel(&converted, 0, x, y), [255, 0, 0, 255]);
    let (x, y) = center(1, 1);
    assert_eq!(pixel(&converted, 1, x, y), [0, 255, 0, 255]);
    let (x, y) = center(2, 1);
    assert_eq!(pixel(&converted, 1, x, y), [255, 0, 0, 255]); // transparent overlay keeps prior red
    assert_eq!(pixel(&converted, 2, x, y), [0, 0, 255, 255]);
    assert_eq!(pixel(&converted, 3, x, y), [0, 0, 0, 0]); // previous frame disposal clears to transparency
    let (x, y) = center(3, 1);
    assert_eq!(pixel(&converted, 3, x, y), [255, 255, 0, 255]);
    assert_eq!(pixel(&converted, 4, x, y), [255, 0, 0, 255]); // restore previous canvas
    let (x, y) = center(0, 2);
    assert_eq!(pixel(&converted, 4, x, y), [0, 255, 255, 255]);
}

#[test]
fn cache_replaces_corrupt_digest_entry_without_losing_other_previews() {
    let directory = tempfile::tempdir().unwrap();
    let root = directory.path().canonicalize().unwrap();
    let cache = watch_cache(&root);
    let first = package(0x001f);
    let second = package(0xf800);
    cache.store(WATCH_ID, &first).unwrap();
    cache.store(WATCH_ID, &second).unwrap();
    let first_hash = hex::encode(first.digest);
    let second_hash = hex::encode(second.digest);
    let damaged = root.join("Pets").join(WATCH_ID).join(&first_hash).join("package.cxp");
    fs::write(&damaged, b"damaged package").unwrap();
    assert!(cache.load(WATCH_ID, &first_hash).unwrap().is_none());

    cache.store(WATCH_ID, &first).unwrap();
    assert_eq!(cache.load(WATCH_ID, &first_hash).unwrap().unwrap().digest, first.digest);
    assert_eq!(cache.load(WATCH_ID, &second_hash).unwrap().unwrap().digest, second.digest);
}

#[test]
fn cache_rejects_symlink_and_fifo_leaves_without_blocking_or_following() {
    let directory = tempfile::tempdir().unwrap();
    let root = directory.path().canonicalize().unwrap();
    let cache = watch_cache(&root);
    let candidate = package(0x07e0);
    let hash = hex::encode(candidate.digest);
    cache.store(WATCH_ID, &candidate).unwrap();
    let entry = root.join("Pets").join(WATCH_ID).join(&hash);
    let leaf = entry.join("package.cxp");
    let outside = root.join("outside.cxp");
    fs::write(&outside, b"outside cache data").unwrap();
    fs::remove_file(&leaf).unwrap();
    symlink(&outside, &leaf).unwrap();
    assert!(cache.load(WATCH_ID, &hash).unwrap().is_none());
    assert!(cache.store(WATCH_ID, &candidate).is_err());
    assert_eq!(fs::read(&outside).unwrap(), b"outside cache data");
    assert!(fs::symlink_metadata(&leaf).unwrap().file_type().is_symlink());

    fs::remove_file(&leaf).unwrap();
    let fifo = std::ffi::CString::new(leaf.as_os_str().as_encoded_bytes()).unwrap();
    assert_eq!(unsafe { libc::mkfifo(fifo.as_ptr(), 0o600) }, 0);
    let started = std::time::Instant::now();
    assert!(cache.load(WATCH_ID, &hash).unwrap().is_none());
    assert!(started.elapsed() < Duration::from_secs(1));
    assert!(cache.store(WATCH_ID, &candidate).is_err());
    assert!(fs::symlink_metadata(&leaf).unwrap().file_type().is_fifo());
    cache.remove_watch(WATCH_ID).unwrap();
    assert_eq!(fs::read(&outside).unwrap(), b"outside cache data");
}

#[test]
fn cache_keeps_new_and_last_active_hashes_inside_eight_entry_history() {
    let directory = tempfile::tempdir().unwrap();
    let root = directory.path().canonicalize().unwrap();
    let cache = watch_cache(&root);
    let packages: Vec<_> = (0..9).map(|color| package(color + 1)).collect();
    for candidate in &packages[..8] { cache.store(WATCH_ID, candidate).unwrap(); }
    let previous_active = hex::encode(packages[7].digest);
    let newest = hex::encode(packages[8].digest);
    cache.store(WATCH_ID, &packages[8]).unwrap();

    let history = root.join("Pets").join(WATCH_ID);
    let names: Vec<_> = fs::read_dir(&history).unwrap().map(|entry| entry.unwrap().file_name()).collect();
    assert_eq!(names.len(), 8);
    assert!(history.join(&previous_active).is_dir());
    assert!(history.join(&newest).is_dir());
}
