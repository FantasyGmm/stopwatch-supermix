use std::{
    env,
    fs::{self, File, OpenOptions},
    io,
    os::{
        fd::AsRawFd,
        unix::fs::{MetadataExt, OpenOptionsExt, PermissionsExt},
    },
    path::{Path, PathBuf},
};

const MAX_LOG_BYTES: u64 = 8 * 1024 * 1024;

/// Open the production log, rotate bounded history, and send stderr to it.
/// This module is compiled only for the native macOS binary; CLI/fixture modes
/// do not call it.
pub fn initialize() -> io::Result<PathBuf> {
    let home = env::var_os("HOME")
        .map(PathBuf::from)
        .ok_or_else(|| io::Error::new(io::ErrorKind::NotFound, "HOME is unavailable"))?;
    if !home.is_absolute() {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "HOME must be an absolute path",
        ));
    }
    let logs = home.join("Library/Logs");
    fs::create_dir_all(&logs)?;

    let path = logs.join("stopwatch-codex-agent.log");
    let mut file = open_log(&path)?;
    if file.metadata()?.len() >= MAX_LOG_BYTES {
        file.sync_data()?;
        drop(file);
        rotate(&path)?;
        file = open_log(&path)?;
    }

    redirect_stderr(&file)?;
    Ok(path)
}

fn open_log(path: &Path) -> io::Result<File> {
    let file = OpenOptions::new()
        .append(true)
        .create(true)
        .mode(0o600)
        .custom_flags(libc::O_NOFOLLOW | libc::O_CLOEXEC | libc::O_NONBLOCK)
        .open(path)?;
    let metadata = file.metadata()?;
    if !metadata.is_file() || metadata.nlink() != 1 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "log target is not a private regular file",
        ));
    }
    file.set_permissions(fs::Permissions::from_mode(0o600))?;
    Ok(file)
}

fn backup_path(path: &Path, suffix: &str) -> io::Result<PathBuf> {
    let mut name = path
        .file_name()
        .ok_or_else(|| io::Error::new(io::ErrorKind::InvalidInput, "log path has no filename"))?
        .to_os_string();
    name.push(suffix);
    Ok(path.with_file_name(name))
}

fn private_regular_file(path: &Path) -> io::Result<Option<File>> {
    match fs::symlink_metadata(path) {
        Ok(metadata) => {
            if metadata.file_type().is_symlink() || !metadata.is_file() || metadata.nlink() != 1 {
                return Err(io::Error::new(
                    io::ErrorKind::InvalidInput,
                    "rotated log is not a regular file",
                ));
            }
            let file = OpenOptions::new()
                .read(true)
                .custom_flags(libc::O_NOFOLLOW | libc::O_CLOEXEC | libc::O_NONBLOCK)
                .open(path)?;
            if !file.metadata()?.is_file() {
                return Err(io::Error::new(
                    io::ErrorKind::InvalidInput,
                    "rotated log is not a regular file",
                ));
            }
            file.set_permissions(fs::Permissions::from_mode(0o600))?;
            Ok(Some(file))
        }
        Err(error) if error.kind() == io::ErrorKind::NotFound => Ok(None),
        Err(error) => Err(error),
    }
}

fn rotate(path: &Path) -> io::Result<()> {
    let first = backup_path(path, ".1")?;
    let second = backup_path(path, ".2")?;

    if private_regular_file(&second)?.is_some() {
        fs::remove_file(&second)?;
    }
    if let Some(first_file) = private_regular_file(&first)? {
        drop(first_file);
        fs::rename(&first, &second)?;
    }
    // The active file was opened with O_NOFOLLOW and is rechecked before the
    // rename so a pre-existing symlink or special file is never rotated in.
    let active_file = open_log(path)?;
    drop(active_file);
    fs::rename(path, &first)?;
    Ok(())
}

fn redirect_stderr(file: &File) -> io::Result<()> {
    // SAFETY: both descriptors are valid here; dup2 atomically replaces fd 2.
    if unsafe { libc::dup2(file.as_raw_fd(), libc::STDERR_FILENO) } == -1 {
        return Err(io::Error::last_os_error());
    }
    Ok(())
}
