//! Descriptor-relative, bounded pet preview cache compatible with the old Agent.
//! No directory or file symlink is followed, including during history removal.
use crate::pet::{PetPackage, MAX_PACKAGE_BYTES, parse_package};
use anyhow::{Context, Result, bail, ensure};
use serde_json::json;
use std::{collections::HashMap, ffi::{CStr, CString}, fs::File, io::{Read, Write}, os::fd::{AsRawFd, FromRawFd, OwnedFd}, path::{Component, Path, PathBuf}, sync::Mutex, time::SystemTime};

pub struct PetCache {
    root: PathBuf,
    transaction: Mutex<()>,
    last_active: Mutex<HashMap<String, String>>,
}

fn name(value: &str) -> Result<CString> { Ok(CString::new(value)?) }
fn dir_at(parent: i32, component: &str, create: bool) -> Result<OwnedFd> {
    let component = name(component)?;
    let flags = libc::O_RDONLY | libc::O_DIRECTORY | libc::O_NOFOLLOW | libc::O_CLOEXEC;
    let mut fd = unsafe { libc::openat(parent, component.as_ptr(), flags) };
    if fd < 0 && create && std::io::Error::last_os_error().kind() == std::io::ErrorKind::NotFound {
        let rc = unsafe { libc::mkdirat(parent, component.as_ptr(), 0o700) };
        if rc < 0 && std::io::Error::last_os_error().kind() != std::io::ErrorKind::AlreadyExists { return Err(std::io::Error::last_os_error().into()) }
        fd = unsafe { libc::openat(parent, component.as_ptr(), flags) };
    }
    if fd < 0 { return Err(std::io::Error::last_os_error().into()) }
    Ok(unsafe { OwnedFd::from_raw_fd(fd) })
}

fn open_root(path: &Path, create: bool) -> Result<OwnedFd> {
    ensure!(path.is_absolute(), "pet cache must be absolute");
    let root = name("/")?;
    let fd = unsafe { libc::open(root.as_ptr(), libc::O_RDONLY | libc::O_DIRECTORY | libc::O_NOFOLLOW | libc::O_CLOEXEC) };
    if fd < 0 { return Err(std::io::Error::last_os_error().into()) }
    let mut current = unsafe { OwnedFd::from_raw_fd(fd) };
    for component in path.components() {
        match component {
            Component::RootDir => (),
            Component::Normal(c) => current = dir_at(current.as_raw_fd(), c.to_str().context("invalid cache path")?, create)?,
            _ => bail!("non-canonical cache path"),
        }
    }
    Ok(current)
}

fn read_at(parent: i32, filename: &str, maximum: usize) -> Result<Vec<u8>> {
    let filename = name(filename)?;
    let mut before: libc::stat = unsafe { std::mem::zeroed() };
    ensure!(unsafe { libc::fstatat(parent, filename.as_ptr(), &mut before, libc::AT_SYMLINK_NOFOLLOW) } == 0, "cannot identify pet cache file");
    ensure!(before.st_mode & libc::S_IFMT == libc::S_IFREG && before.st_size >= 0 && before.st_size as u64 <= maximum as u64, "invalid pet cache file");
    let fd = unsafe { libc::openat(parent, filename.as_ptr(), libc::O_RDONLY | libc::O_NONBLOCK | libc::O_NOFOLLOW | libc::O_CLOEXEC) };
    if fd < 0 { return Err(std::io::Error::last_os_error().into()) }
    let file = unsafe { File::from_raw_fd(fd) };
    let mut opened: libc::stat = unsafe { std::mem::zeroed() };
    ensure!(unsafe { libc::fstat(file.as_raw_fd(), &mut opened) } == 0, "cannot identify opened pet cache file");
    ensure!(opened.st_mode & libc::S_IFMT == libc::S_IFREG && opened.st_dev == before.st_dev && opened.st_ino == before.st_ino && opened.st_size == before.st_size, "pet cache file changed while opening");
    let mut bytes = Vec::new();
    let mut limited = (&file).take((maximum + 1) as u64);
    limited.read_to_end(&mut bytes)?;
    drop(limited);
    ensure!(bytes.len() <= maximum, "pet cache file grew outside limit");
    let mut after: libc::stat = unsafe { std::mem::zeroed() };
    let mut named_after: libc::stat = unsafe { std::mem::zeroed() };
    ensure!(unsafe { libc::fstat(file.as_raw_fd(), &mut after) } == 0 && unsafe { libc::fstatat(parent, filename.as_ptr(), &mut named_after, libc::AT_SYMLINK_NOFOLLOW) } == 0, "cannot revalidate pet cache file");
    ensure!(after.st_mode & libc::S_IFMT == libc::S_IFREG && after.st_dev == opened.st_dev && after.st_ino == opened.st_ino && after.st_size == opened.st_size && named_after.st_dev == opened.st_dev && named_after.st_ino == opened.st_ino, "pet cache file changed while reading");
    ensure!(bytes.len() as u64 == after.st_size as u64, "pet cache file changed size while reading");
    Ok(bytes)
}

fn write_at(parent: i32, filename: &str, data: &[u8]) -> Result<()> {
    let filename = name(filename)?;
    let fd = unsafe { libc::openat(parent, filename.as_ptr(), libc::O_WRONLY | libc::O_CREAT | libc::O_EXCL | libc::O_NOFOLLOW | libc::O_CLOEXEC, 0o600) };
    if fd < 0 { return Err(std::io::Error::last_os_error().into()) }
    let mut file = unsafe { File::from_raw_fd(fd) };
    file.write_all(data)?; file.sync_all()?; Ok(())
}

fn stat_at(parent: i32, filename: &str) -> Result<Option<libc::stat>> {
    let filename = name(filename)?;
    let mut stat: libc::stat = unsafe { std::mem::zeroed() };
    if unsafe { libc::fstatat(parent, filename.as_ptr(), &mut stat, libc::AT_SYMLINK_NOFOLLOW) } == 0 {
        return Ok(Some(stat));
    }
    let error = std::io::Error::last_os_error();
    if error.kind() == std::io::ErrorKind::NotFound { Ok(None) } else { Err(error.into()) }
}

fn same_identity(left: &libc::stat, right: &libc::stat) -> bool {
    left.st_dev == right.st_dev && left.st_ino == right.st_ino
}

fn expected_metadata(package: &PetPackage) -> serde_json::Value {
    json!({
        "schema": 1,
        "sha256": hex::encode(package.digest),
        "width": package.width,
        "height": package.height,
        "frame_count": package.frames.len(),
        "durations_ms": package.frames.iter().map(|f| f.duration_ms).collect::<Vec<_>>(),
    })
}

fn read_verified_entry(entry: i32, expected_hash: &str) -> Result<PetPackage> {
    let package = parse_package(&read_at(entry, "package.cxp", MAX_PACKAGE_BYTES)?)?;
    let metadata: serde_json::Value = serde_json::from_slice(&read_at(entry, "metadata.json", 4096)?)?;
    ensure!(hex::encode(package.digest) == expected_hash && metadata == expected_metadata(&package), "cache digest or metadata mismatch");
    Ok(package)
}

fn entries(parent: i32, limit: usize) -> Result<Vec<String>> {
    // fdopendir advances its descriptor's directory offset. Open a new file
    // description so traversal cannot consume the caller's directory stream.
    let dot = name(".")?;
    let copy = unsafe { libc::openat(parent, dot.as_ptr(), libc::O_RDONLY | libc::O_DIRECTORY | libc::O_NOFOLLOW | libc::O_CLOEXEC) };
    if copy < 0 { return Err(std::io::Error::last_os_error().into()) }
    let stream = unsafe { libc::fdopendir(copy) };
    if stream.is_null() { unsafe { libc::close(copy); } return Err(std::io::Error::last_os_error().into()) }
    struct Stream(*mut libc::DIR);
    impl Drop for Stream { fn drop(&mut self) { unsafe { libc::closedir(self.0); } } }
    let stream = Stream(stream);
    let mut result = Vec::new();
    loop {
        let entry = unsafe { libc::readdir(stream.0) };
        if entry.is_null() { break }
        let text = unsafe { CStr::from_ptr((*entry).d_name.as_ptr()) }.to_str()?.to_owned();
        if text == "." || text == ".." { continue }
        ensure!(result.len() < limit, "pet cache directory exceeds traversal limit");
        result.push(text);
    }
    Ok(result)
}

fn unlink_at(parent: i32, filename: &str, directory: bool) -> Result<()> {
    let filename = name(filename)?;
    let rc = unsafe { libc::unlinkat(parent, filename.as_ptr(), if directory { libc::AT_REMOVEDIR } else { 0 }) };
    if rc < 0 { return Err(std::io::Error::last_os_error().into()) }
    Ok(())
}

fn valid_hash(s: &str) -> bool { s.len() == 64 && s.bytes().all(|b| b.is_ascii_hexdigit() && !b.is_ascii_uppercase()) }
fn valid_watch(s: &str) -> bool { s.len() == 32 && s.bytes().all(|b| b.is_ascii_hexdigit() && !b.is_ascii_uppercase()) }
fn valid_temp(s: &str) -> bool { s.strip_prefix(".tmp-").is_some_and(|suffix| suffix.len() == 32 && suffix.bytes().all(|b| b.is_ascii_hexdigit() && !b.is_ascii_uppercase())) }

struct QuarantinedEntry { name: String, identity: libc::stat }

fn quarantine_entry(watch: i32, entry: &str, expected: Option<&libc::stat>, regular_files_only: bool) -> Result<QuarantinedEntry> {
    ensure!(valid_hash(entry) || valid_temp(entry), "unmanaged cache entry");
    let directory = dir_at(watch, entry, false)?;
    let files = entries(directory.as_raw_fd(), 2)?;
    ensure!(files.iter().all(|f| f == "metadata.json" || f == "package.cxp"), "cache entry contains unmanaged files");
    if regular_files_only {
        for file in &files {
            let stat = stat_at(directory.as_raw_fd(), file)?.context("cache file disappeared before quarantine")?;
            ensure!(stat.st_mode & libc::S_IFMT == libc::S_IFREG, "refusing to replace a symlink or special cache file");
        }
    }
    // Pin the opened directory under a random quarantine name before cleanup.
    let quarantine = format!(".tmp-{}", uuid::Uuid::new_v4().simple());
    let from = name(entry)?; let to = name(&quarantine)?;
    let mut opened: libc::stat = unsafe { std::mem::zeroed() };
    let mut named: libc::stat = unsafe { std::mem::zeroed() };
    ensure!(unsafe { libc::fstat(directory.as_raw_fd(), &mut opened) } == 0, "cannot identify cache entry");
    if let Some(expected) = expected { ensure!(same_identity(&opened, expected), "cache entry changed after validation"); }
    ensure!(unsafe { libc::fstatat(watch, from.as_ptr(), &mut named, libc::AT_SYMLINK_NOFOLLOW) } == 0 && same_identity(&opened, &named) && named.st_mode & libc::S_IFMT == libc::S_IFDIR, "cache entry changed");
    ensure!(unsafe { libc::renameatx_np(watch, from.as_ptr(), watch, to.as_ptr(), libc::RENAME_EXCL) } == 0, "cannot quarantine cache entry");
    let pinned = dir_at(watch, &quarantine, false);
    let mut after: libc::stat = unsafe { std::mem::zeroed() };
    let matches = pinned.as_ref().is_ok_and(|pinned| {
        (unsafe { libc::fstat(pinned.as_raw_fd(), &mut after) }) == 0 && same_identity(&after, &opened)
    });
    if !matches {
        // A concurrent replacement may have raced the rename. Preserve the
        // moved object by attempting an exclusive rename back; never unlink it.
        if let Some(moved) = stat_at(watch, &quarantine)? {
            let moved_name = name(&quarantine)?;
            if same_identity(&moved, &named) && moved.st_mode & libc::S_IFMT == libc::S_IFDIR && stat_at(watch, entry)?.is_none() {
                let _ = unsafe { libc::renameatx_np(watch, moved_name.as_ptr(), watch, from.as_ptr(), libc::RENAME_EXCL) };
            }
        }
        bail!("quarantine identity changed");
    }
    if unsafe { libc::fsync(watch) } != 0 {
        let error = std::io::Error::last_os_error();
        if stat_at(watch, &quarantine)?.is_some_and(|moved| same_identity(&moved, &opened)) && stat_at(watch, entry)?.is_none() {
            let _ = unsafe { libc::renameatx_np(watch, to.as_ptr(), watch, from.as_ptr(), libc::RENAME_EXCL) };
        }
        return Err(error.into());
    }
    Ok(QuarantinedEntry { name: quarantine, identity: opened })
}

fn delete_quarantine(watch: i32, quarantine: &QuarantinedEntry) -> Result<()> {
    let pinned = dir_at(watch, &quarantine.name, false)?;
    let mut after: libc::stat = unsafe { std::mem::zeroed() };
    ensure!(unsafe { libc::fstat(pinned.as_raw_fd(), &mut after) } == 0 && same_identity(&after, &quarantine.identity), "quarantine identity changed before deletion");
    let quarantined_files = entries(pinned.as_raw_fd(), 2)?;
    ensure!(quarantined_files.iter().all(|f| f == "metadata.json" || f == "package.cxp"), "quarantine contains unmanaged files");
    for f in &quarantined_files {
        if let Some(stat) = stat_at(pinned.as_raw_fd(), f)? {
            ensure!(stat.st_mode & libc::S_IFMT != libc::S_IFDIR, "cache entry contains an unmanaged directory");
        }
    }
    for f in quarantined_files {
        unlink_at(pinned.as_raw_fd(), &f, false)?;
    }
    unlink_at(watch, &quarantine.name, true)?;
    ensure!(unsafe { libc::fsync(watch) } == 0, "cannot sync pet cache removal");
    Ok(())
}

fn restore_quarantine(watch: i32, quarantine: &QuarantinedEntry, destination: &str) -> Result<()> {
    let current = stat_at(watch, &quarantine.name)?.context("quarantined cache entry disappeared")?;
    ensure!(current.st_mode & libc::S_IFMT == libc::S_IFDIR && same_identity(&current, &quarantine.identity), "quarantine identity changed before restore");
    ensure!(stat_at(watch, destination)?.is_none(), "cache destination appeared before restore");
    let from = name(&quarantine.name)?;
    let to = name(destination)?;
    ensure!(unsafe { libc::renameatx_np(watch, from.as_ptr(), watch, to.as_ptr(), libc::RENAME_EXCL) } == 0, "cannot restore quarantined cache entry");
    let restored = stat_at(watch, destination)?.context("restored cache entry disappeared")?;
    ensure!(same_identity(&restored, &quarantine.identity), "restored cache identity changed");
    ensure!(unsafe { libc::fsync(watch) } == 0, "cannot sync restored cache entry");
    Ok(())
}

fn rollback_published(watch: i32, hash: &str, published_identity: &libc::stat, prior: Option<&QuarantinedEntry>) -> Result<()> {
    let current = stat_at(watch, hash)?;
    if let Some(current) = current {
        ensure!(current.st_mode & libc::S_IFMT == libc::S_IFDIR && same_identity(&current, published_identity), "published cache entry changed before rollback");
        let rollback = format!(".tmp-{}", uuid::Uuid::new_v4().simple());
        let from = name(hash)?;
        let to = name(&rollback)?;
        ensure!(unsafe { libc::renameatx_np(watch, from.as_ptr(), watch, to.as_ptr(), libc::RENAME_EXCL) } == 0, "cannot isolate failed cache publication");
        let moved = stat_at(watch, &rollback)?.context("failed publication disappeared during rollback")?;
        ensure!(moved.st_mode & libc::S_IFMT == libc::S_IFDIR && same_identity(&moved, published_identity), "failed publication identity changed during rollback");
        if let Some(prior) = prior { restore_quarantine(watch, prior, hash)?; }
        remove_entry_if_identity(watch, &rollback, Some(published_identity))?;
    } else if let Some(prior) = prior {
        restore_quarantine(watch, prior, hash)?;
    }
    ensure!(unsafe { libc::fsync(watch) } == 0, "cannot sync pet cache rollback");
    Ok(())
}

fn remove_entry(watch: i32, entry: &str) -> Result<()> {
    remove_entry_if_identity(watch, entry, None)
}

fn remove_entry_if_identity(watch: i32, entry: &str, expected: Option<&libc::stat>) -> Result<()> {
    let quarantine = quarantine_entry(watch, entry, expected, false)?;
    delete_quarantine(watch, &quarantine)
}

struct HistoryEntry { name: String, modified: SystemTime, size: usize, identity: libc::stat }

fn verified_history_entry(watch: i32, entry: &str) -> Result<Option<HistoryEntry>> {
    if !valid_hash(entry) { return Ok(None) }
    let Some(named) = stat_at(watch, entry)? else { return Ok(None) };
    if named.st_mode & libc::S_IFMT != libc::S_IFDIR { return Ok(None) }
    let directory = match dir_at(watch, entry, false) { Ok(directory) => directory, Err(_) => return Ok(None) };
    let mut opened: libc::stat = unsafe { std::mem::zeroed() };
    if unsafe { libc::fstat(directory.as_raw_fd(), &mut opened) } != 0 || !same_identity(&named, &opened) { return Ok(None) }
    let files = match entries(directory.as_raw_fd(), 2) { Ok(files) => files, Err(_) => return Ok(None) };
    if files.len() != 2 || !files.iter().all(|file| file == "metadata.json" || file == "package.cxp") { return Ok(None) }
    let package = match read_verified_entry(directory.as_raw_fd(), entry) { Ok(package) => package, Err(_) => return Ok(None) };
    let mut after_fd: libc::stat = unsafe { std::mem::zeroed() };
    let Some(after_name) = stat_at(watch, entry)? else { return Ok(None) };
    if unsafe { libc::fstat(directory.as_raw_fd(), &mut after_fd) } != 0 || !same_identity(&opened, &after_fd) || !same_identity(&opened, &after_name) { return Ok(None) }
    let duplicate = unsafe { libc::dup(directory.as_raw_fd()) };
    if duplicate < 0 { return Err(std::io::Error::last_os_error().into()) }
    let file = unsafe { File::from_raw_fd(duplicate) };
    let modified = file.metadata()?.modified().unwrap_or(SystemTime::UNIX_EPOCH);
    Ok(Some(HistoryEntry { name: entry.to_owned(), modified, size: package.encoded.len(), identity: opened }))
}

fn prune_history(watch: i32, protected_hashes: &[String]) -> Result<()> {
    let names = entries(watch, 128)?;
    let mut history = Vec::new();
    for entry in names {
        if let Some(item) = verified_history_entry(watch, &entry)? { history.push(item); }
    }
    let max_bytes = 8 * MAX_PACKAGE_BYTES;
    let mut total_bytes: usize = history.iter().map(|entry| entry.size).sum();
    while history.len() > 8 || total_bytes > max_bytes {
        let Some((index, _)) = history.iter().enumerate()
            .filter(|(_, entry)| !protected_hashes.iter().any(|hash| hash == &entry.name))
            .min_by_key(|(_, entry)| (entry.modified, entry.name.as_str())) else {
                bail!("pet cache history cannot retain its active package")
            };
        let victim = history.remove(index);
        remove_entry_if_identity(watch, &victim.name, Some(&victim.identity))?;
        total_bytes = total_bytes.saturating_sub(victim.size);
    }
    Ok(())
}

impl PetCache {
    pub fn new(root: PathBuf) -> Self { Self { root, transaction: Mutex::new(()), last_active: Mutex::new(HashMap::new()) } }

    fn activate_and_prune(&self, watch: i32, watch_id: &str, new_hash: &str, prior_active: Option<&str>) -> Result<()> {
        let mut active = self.last_active.lock().map_err(|_| anyhow::anyhow!("cache active lock poisoned"))?;
        let current_active = active.get(watch_id).cloned();
        let mut protected = vec![new_hash.to_owned()];
        if let Some(hash) = prior_active { if !protected.iter().any(|item| item == hash) { protected.push(hash.to_owned()); } }
        if let Some(hash) = current_active { if !protected.iter().any(|item| item == &hash) { protected.push(hash); } }
        active.insert(watch_id.to_owned(), new_hash.to_owned());
        prune_history(watch, &protected)
    }

    pub fn load(&self, watch_id: &str, active_hash: &str) -> Result<Option<PetPackage>> {
        ensure!(valid_watch(watch_id) && valid_hash(active_hash), "invalid cache identity");
        if active_hash.bytes().all(|b| b == b'0') {
            self.last_active.lock().map_err(|_| anyhow::anyhow!("cache active lock poisoned"))?.remove(watch_id);
            return Ok(None)
        }
        let result = (|| {
            let root = open_root(&self.root, false)?;
            let watch = dir_at(root.as_raw_fd(), watch_id, false)?;
            let named = stat_at(watch.as_raw_fd(), active_hash)?.context("cache entry disappeared")?;
            ensure!(named.st_mode & libc::S_IFMT == libc::S_IFDIR, "cache entry is not a directory");
            let entry = dir_at(watch.as_raw_fd(), active_hash, false)?;
            let mut opened: libc::stat = unsafe { std::mem::zeroed() };
            ensure!(unsafe { libc::fstat(entry.as_raw_fd(), &mut opened) } == 0 && same_identity(&named, &opened), "cache entry changed while opening");
            let package = read_verified_entry(entry.as_raw_fd(), active_hash)?;
            let mut after: libc::stat = unsafe { std::mem::zeroed() };
            let named_after = stat_at(watch.as_raw_fd(), active_hash)?.context("cache entry disappeared after read")?;
            ensure!(unsafe { libc::fstat(entry.as_raw_fd(), &mut after) } == 0 && same_identity(&opened, &after) && same_identity(&opened, &named_after), "cache entry changed while reading");
            Ok(package)
        })();
        match result {
            Ok(package) => {
                self.last_active.lock().map_err(|_| anyhow::anyhow!("cache active lock poisoned"))?.insert(watch_id.to_owned(), active_hash.to_owned());
                Ok(Some(package))
            }
            Err(_) => Ok(None),
        }
    }

    pub fn store(&self, watch_id: &str, candidate: &PetPackage) -> Result<()> {
        ensure!(valid_watch(watch_id), "invalid watch identity");
        let package = parse_package(&candidate.encoded)?;
        ensure!(candidate.digest == package.digest, "noncanonical pet cache input");
        let _lock = self.transaction.lock().map_err(|_| anyhow::anyhow!("cache lock poisoned"))?;
        let root = open_root(&self.root, true)?;
        let watch = dir_at(root.as_raw_fd(), watch_id, true)?;
        let hash = hex::encode(package.digest);
        let prior_active = self.last_active.lock().map_err(|_| anyhow::anyhow!("cache active lock poisoned"))?.get(watch_id).cloned();
        if self.load(watch_id, &hash)?.is_some() {
            return self.activate_and_prune(watch.as_raw_fd(), watch_id, &hash, prior_active.as_deref());
        }
        let names = entries(watch.as_raw_fd(), 128)?;
        ensure!(names.len() < 128, "pet cache directory cannot safely stage a new entry");
        let temp = format!(".tmp-{}", uuid::Uuid::new_v4().simple());
        let temp_dir = dir_at(watch.as_raw_fd(), &temp, true)?;
        let mut staged_identity: libc::stat = unsafe { std::mem::zeroed() };
        if unsafe { libc::fstat(temp_dir.as_raw_fd(), &mut staged_identity) } != 0 {
            return Err(std::io::Error::last_os_error().into());
        }
        let staged = (|| -> Result<()> {
            write_at(temp_dir.as_raw_fd(), "package.cxp", &package.encoded)?;
            write_at(temp_dir.as_raw_fd(), "metadata.json", &serde_json::to_vec(&expected_metadata(&package))?)?;
            ensure!(unsafe { libc::fsync(temp_dir.as_raw_fd()) } == 0, "cache directory sync failed");
            Ok(())
        })();
        if let Err(error) = staged {
            drop(temp_dir);
            let _ = remove_entry_if_identity(watch.as_raw_fd(), &temp, Some(&staged_identity));
            return Err(error);
        }
        drop(temp_dir);

        let mut quarantined = None;
        let existing = match stat_at(watch.as_raw_fd(), &hash) {
            Ok(existing) => existing,
            Err(error) => { let _ = remove_entry_if_identity(watch.as_raw_fd(), &temp, Some(&staged_identity)); return Err(error); }
        };
        if let Some(existing) = existing {
            if existing.st_mode & libc::S_IFMT != libc::S_IFDIR {
                let _ = remove_entry_if_identity(watch.as_raw_fd(), &temp, Some(&staged_identity));
                bail!("refusing to replace non-directory pet cache entry");
            }
            let already_valid = match self.load(watch_id, &hash) {
                Ok(package) => package.is_some(),
                Err(error) => {
                    let _ = remove_entry_if_identity(watch.as_raw_fd(), &temp, Some(&staged_identity));
                    return Err(error);
                }
            };
            if already_valid {
                remove_entry_if_identity(watch.as_raw_fd(), &temp, Some(&staged_identity))?;
                return self.activate_and_prune(watch.as_raw_fd(), watch_id, &hash, prior_active.as_deref());
            }
            match quarantine_entry(watch.as_raw_fd(), &hash, Some(&existing), true) {
                Ok(entry) => quarantined = Some(entry),
                Err(error) => { let _ = remove_entry_if_identity(watch.as_raw_fd(), &temp, Some(&staged_identity)); return Err(error); }
            }
        }

        match stat_at(watch.as_raw_fd(), &temp) {
            Ok(Some(identity)) if identity.st_mode & libc::S_IFMT == libc::S_IFDIR && same_identity(&identity, &staged_identity) => (),
            Ok(_) => {
                let _ = remove_entry_if_identity(watch.as_raw_fd(), &temp, Some(&staged_identity));
                if let Some(old) = quarantined.as_ref() { let _ = restore_quarantine(watch.as_raw_fd(), old, &hash); }
                bail!("staged cache entry changed before publish");
            }
            Err(error) => {
                let _ = remove_entry_if_identity(watch.as_raw_fd(), &temp, Some(&staged_identity));
                if let Some(old) = quarantined.as_ref() { let _ = restore_quarantine(watch.as_raw_fd(), old, &hash); }
                return Err(error);
            }
        }
        let from = name(&temp)?;
        let to = name(&hash)?;
        if unsafe { libc::renameatx_np(watch.as_raw_fd(), from.as_ptr(), watch.as_raw_fd(), to.as_ptr(), libc::RENAME_EXCL) } != 0 {
            let error: anyhow::Error = std::io::Error::last_os_error().into();
            if let Some(old) = quarantined.as_ref() { let _ = restore_quarantine(watch.as_raw_fd(), old, &hash); }
            let _ = remove_entry_if_identity(watch.as_raw_fd(), &temp, Some(&staged_identity));
            return Err(error.context("cache publish failed"));
        }
        let verification = (|| -> Result<()> {
            let published = stat_at(watch.as_raw_fd(), &hash)?.context("published cache entry disappeared")?;
            ensure!(published.st_mode & libc::S_IFMT == libc::S_IFDIR && same_identity(&published, &staged_identity), "published cache entry identity changed");
            let published_dir = dir_at(watch.as_raw_fd(), &hash, false)?;
            let mut published_opened: libc::stat = unsafe { std::mem::zeroed() };
            ensure!(unsafe { libc::fstat(published_dir.as_raw_fd(), &mut published_opened) } == 0 && same_identity(&published, &published_opened), "published cache entry changed while opening");
            let verified = read_verified_entry(published_dir.as_raw_fd(), &hash)?;
            ensure!(verified.encoded == package.encoded, "published cache contents changed");
            let mut published_after: libc::stat = unsafe { std::mem::zeroed() };
            let named_after = stat_at(watch.as_raw_fd(), &hash)?.context("published cache entry disappeared")?;
            ensure!(unsafe { libc::fstat(published_dir.as_raw_fd(), &mut published_after) } == 0 && same_identity(&published_opened, &published_after) && same_identity(&published_opened, &named_after), "published cache entry changed while verifying");
            Ok(())
        })();
        if let Err(error) = verification {
            let rollback_result = rollback_published(watch.as_raw_fd(), &hash, &staged_identity, quarantined.as_ref());
            return match rollback_result {
                Ok(()) => Err(error),
                Err(rollback_error) => Err(error.context(format!("cache verification failed; rollback also failed: {rollback_error}"))),
            };
        }
        if unsafe { libc::fsync(watch.as_raw_fd()) } != 0 {
            let error: anyhow::Error = std::io::Error::last_os_error().into();
            let rollback_result = rollback_published(watch.as_raw_fd(), &hash, &staged_identity, quarantined.as_ref());
            return match rollback_result {
                Ok(()) => Err(error.context("cache publication sync failed")),
                Err(rollback_error) => Err(error.context(format!("cache publication sync failed; rollback also failed: {rollback_error}"))),
            };
        }
        if let Some(old) = quarantined.as_ref() { delete_quarantine(watch.as_raw_fd(), old)?; }
        self.activate_and_prune(watch.as_raw_fd(), watch_id, &hash, prior_active.as_deref())
    }

    pub fn remove_watch(&self, watch_id: &str) -> Result<()> {
        ensure!(valid_watch(watch_id), "invalid watch identity");
        let _lock = self.transaction.lock().map_err(|_| anyhow::anyhow!("cache lock poisoned"))?;
        let root = open_root(&self.root, false)?;
        let watch = dir_at(root.as_raw_fd(), watch_id, false)?;
        for entry in entries(watch.as_raw_fd(), 128)? { remove_entry(watch.as_raw_fd(), &entry)?; }
        unlink_at(root.as_raw_fd(), watch_id, true)?;
        self.last_active.lock().map_err(|_| anyhow::anyhow!("cache active lock poisoned"))?.remove(watch_id);
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::pet::{PetFrame, encode_package};
    use std::os::unix::fs::symlink;
    #[test]
    fn compatible_cache_and_symlink_rejection() {
        let dir = tempfile::tempdir().unwrap();
        let base = dir.path().canonicalize().unwrap();
        let cache = PetCache::new(base.join("Pets"));
        let id = "00000000000000000000000000000001";
        let p = encode_package(vec![0], vec![PetFrame { duration_ms:120,indices:vec![0;144*156] }]).unwrap();
        cache.store(id, &p).unwrap();
        let hash = hex::encode(p.digest);
        assert_eq!(cache.load(id, &hash).unwrap().unwrap().digest, p.digest);
        let path = base.join("Pets").join(id).join(&hash).join("package.cxp");
        std::fs::remove_file(&path).unwrap();
        symlink(base.join("outside"), &path).unwrap();
        assert!(cache.load(id, &hash).unwrap().is_none());
        assert!(cache.store("../outside", &p).is_err());
        cache.remove_watch(id).unwrap();
        assert!(!base.join("Pets").join(id).exists());
    }
}
