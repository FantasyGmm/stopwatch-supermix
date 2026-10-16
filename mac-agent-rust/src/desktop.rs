//! Main-thread-owned macOS status UI and its bounded bridge to the Agent.
//!
//! AppKit handles never leave `run_ui`'s thread. The menu's C callbacks only
//! enqueue commands, and status/preview data reaches AppKit through a single
//! latest-value watch slot drained by an NSTimer on the main run loop.

use crate::{
    contracts::{AgentPaths, Desktop, DeviceCommand, InputAction, StatusSnapshot, TaskDetail, VoiceMode},
    macos::CodexSubmitController,
    pet::{PetPackage, convert_gif, render_rgba},
    voice::VoiceController,
};
use anyhow::{Context, Result, anyhow, bail};
use serde_json::{Value, json};
use std::{
    ffi::{CStr, CString, c_char, c_void},
    path::PathBuf,
    sync::{
        Arc, Mutex,
        atomic::{AtomicBool, AtomicU64, Ordering},
    },
    thread::JoinHandle,
    time::{Duration, Instant},
};
use tokio::sync::{mpsc, mpsc::error::TrySendError, watch};

const MAX_UI_VALUE_BYTES: usize = 4096;

#[derive(Clone, Default)]
struct UiState {
    snapshot: StatusSnapshot,
    connection: Value,
    pairing_code: String,
    preview: Option<Arc<PetPreview>>,
}

struct PetPreview {
    package: Arc<PetPackage>,
    frames: Vec<Arc<[u8]>>,
    durations: Vec<Duration>,
    started: Instant,
    label: String,
    kind: PetPreviewKind,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum PetPreviewKind {
    Candidate,
    Active,
}

impl PetPreview {
    fn current_frame(&self, now: Instant) -> usize {
        if self.frames.len() <= 1 || self.durations.len() != self.frames.len() {
            return 0;
        }
        let cycle = self.durations.iter().map(Duration::as_millis).sum::<u128>();
        if cycle == 0 {
            return 0;
        }
        let mut elapsed = now.saturating_duration_since(self.started).as_millis() % cycle;
        for (index, duration) in self.durations.iter().enumerate() {
            let frame_ms = duration.as_millis();
            if elapsed < frame_ms {
                return index;
            }
            elapsed -= frame_ms;
        }
        0
    }
}

pub struct MacDesktop {
    commands: mpsc::Sender<DeviceCommand>,
    paths: AgentPaths,
    ui_state: watch::Sender<UiState>,
    ui_receiver: Mutex<Option<watch::Receiver<UiState>>>,
    selected_watch_id: Arc<Mutex<Option<String>>>,
    latest_pet: Arc<Mutex<Option<Arc<PetPackage>>>>,
    active_preview_generation: Arc<AtomicU64>,
    voice: Arc<VoiceController>,
    submit: CodexSubmitController,
    ui_running: AtomicBool,
    install_callbacks: Arc<Mutex<Vec<JoinHandle<()>>>>,
}

impl MacDesktop {
    pub fn new(commands: mpsc::Sender<DeviceCommand>, paths: AgentPaths) -> Result<Self> {
        let (ui_state, receiver) = watch::channel(UiState::default());
        let codex_root = paths.codex.clone();
        Ok(Self {
            commands,
            paths,
            ui_state,
            ui_receiver: Mutex::new(Some(receiver)),
            selected_watch_id: Arc::new(Mutex::new(None)),
            latest_pet: Arc::new(Mutex::new(None)),
            active_preview_generation: Arc::new(AtomicU64::new(0)),
            voice: Arc::new(VoiceController::new(codex_root.clone())),
            submit: CodexSubmitController::with_codex_root(codex_root),
            ui_running: AtomicBool::new(false),
            install_callbacks: Arc::new(Mutex::new(Vec::new())),
        })
    }

    /// Read the retained product preference through NSUserDefaults. Call this
    /// only for a normal product launch; test/self-test entry points should not
    /// construct `MacDesktop` or touch real user defaults.
    pub fn preferences(&self) -> Value {
        json!({ "connectionEnabled": native_connection_enabled() })
    }

    /// Show a locally restored active watch pet without treating it as a new
    /// upload candidate. Frame rendering runs away from the UI/runtime thread.
    pub fn show_pet_preview(&self, package: PetPackage) -> Result<()> {
        let generation = self.active_preview_generation.fetch_add(1, Ordering::AcqRel).wrapping_add(1);
        let active_preview_generation = Arc::clone(&self.active_preview_generation);
        let sender = self.ui_state.clone();
        std::thread::Builder::new().name("pet-cache-preview".into()).spawn(move || {
            let result = build_pet_preview(package, "Cached watch pet".to_owned(), PetPreviewKind::Active);
            if active_preview_generation.load(Ordering::Acquire) != generation { return; }
            sender.send_modify(|state| {
                if active_preview_generation.load(Ordering::Acquire) != generation
                    || state.preview.as_ref().is_some_and(|preview| preview.kind == PetPreviewKind::Candidate)
                {
                    return;
                }
                match result {
                    Ok(preview) => {
                        state.preview = Some(Arc::new(preview));
                        merge_preview_pet_status(&mut state.connection, state.preview.as_deref());
                    }
                    Err(error) => set_pet_status(&mut state.connection, "Cached pet preview failed", &error.to_string(), 0.0, false, false, false),
                }
            });
        }).context("start cached pet preview rendering")?;
        Ok(())
    }

    /// Clear a cache-backed active pet preview while keeping a user's imported
    /// GIF candidate and its send state untouched.
    pub fn clear_pet_preview(&self) {
        self.active_preview_generation.fetch_add(1, Ordering::AcqRel);
        self.ui_state.send_modify(|state| {
            if !state.preview.as_ref().is_some_and(|preview| preview.kind == PetPreviewKind::Active) {
                return;
            }
            state.preview = None;
            let phase = state.connection.get("pet_phase").and_then(Value::as_str).unwrap_or("idle");
            if matches!(phase, "idle" | "complete" | "error" | "cancelled" | "ambiguous") {
                set_pet_status(&mut state.connection, "Choose a GIF to preview", "No converted pet selected", 0.0, false, false, false);
                if let Some(object) = state.connection.as_object_mut() {
                    object.insert("pet_phase".into(), json!("idle"));
                }
            }
        });
    }

    /// Blocks on the calling thread and owns AppKit until the menu exits.
    /// The native handle and all calls that mutate it stay on this thread.
    pub fn run_ui(&self) -> Result<()> {
        if self.ui_running.swap(true, Ordering::AcqRel) {
            bail!("the macOS status UI is already running");
        }
        let _running = UiRunningGuard(&self.ui_running);
        let receiver = self
            .ui_receiver
            .lock()
            .map_err(|_| anyhow!("macOS UI state is unavailable"))?
            .take()
            .ok_or_else(|| anyhow!("the macOS status UI can only run once"))?;

        #[cfg(target_os = "macos")]
        {
            let context = Box::new(UiRunContext {
                commands: self.commands.clone(),
                receiver,
                ui_sender: self.ui_state.clone(),
                selected_watch_id: Arc::clone(&self.selected_watch_id),
                latest_pet: Arc::clone(&self.latest_pet),
                active_preview_generation: Arc::clone(&self.active_preview_generation),
                pet_cache_dir: self.paths.state.join("pets"),
                voice: Arc::clone(&self.voice),
                install_callbacks: Arc::clone(&self.install_callbacks),
                last_state: UiState::default(),
                last_preview_frame: None,
            });
            let context = Box::into_raw(context);
            // SAFETY: AppKit validates main-thread ownership. The context remains
            // allocated from create through run and destroy, all on this thread.
            let handle = unsafe {
                swui_create(
                    context.cast(),
                    native_ui_event,
                    native_ui_tick,
                )
            };
            if handle.is_null() {
                // SAFETY: `context` was allocated above and no native callback ran.
                unsafe { drop(Box::from_raw(context)) };
                let cleanup = self.voice.stop(true);
                let callbacks = self.join_install_callbacks();
                cleanup.context("stop voice after UI creation failure")?;
                callbacks.context("join voice driver callback after UI creation failure")?;
                bail!("could not create the macOS status UI on the main thread");
            }

            // SAFETY: the opaque handle was created above and is only used by
            // this main-thread call and the callbacks it invokes.
            let run_status = unsafe { swui_run(handle) };
            // The app has left its event loop, so invalidate main-loop resources
            // and release AppKit objects before freeing the callback context.
            unsafe {
                swui_shutdown(handle);
                swui_destroy(handle);
                drop(Box::from_raw(context));
            }
            let cleanup = self.voice.stop(true);
            let callbacks = self.join_install_callbacks();
            if run_status != 0 {
                bail!("macOS status UI stopped with status {run_status}");
            }
            cleanup.context("voice cleanup after UI shutdown")?;
            callbacks.context("join voice driver callbacks after UI shutdown")?;
            return Ok(());
        }

        #[cfg(not(target_os = "macos"))]
        {
            let _ = receiver;
            bail!("the macOS status UI is available only on macOS")
        }
    }

    fn join_install_callbacks(&self) -> Result<()> {
        let callbacks = {
            let mut callbacks = self.install_callbacks.lock()
                .map_err(|_| anyhow!("voice driver callback state is unavailable"))?;
            std::mem::take(&mut *callbacks)
        };
        for callback in callbacks {
            callback.join().map_err(|_| anyhow!("voice driver callback thread panicked"))?;
        }
        Ok(())
    }

}

impl Desktop for MacDesktop {
    fn readiness(&self) -> Value {
        let accessibility = macos::accessibility_trusted();
        let voice = self.voice.readiness();
        json!({
            "input_action": if accessibility { "ready" } else { "accessibility_required" },
            "accessibility": accessibility,
            "voice": voice,
        })
    }

    fn submit(&self, action: InputAction) -> Result<bool> {
        Ok(self.submit.handle(action))
    }

    fn voice_start(&self, mode: VoiceMode, task: Option<TaskDetail>) -> Result<()> {
        self.voice.start(mode, task)
    }

    fn voice_pcm(&self, samples: &[i16]) -> Result<()> {
        self.voice.pcm(samples)
    }

    fn voice_stop(&self, cancelled: bool) -> Result<()> {
        self.voice.stop(cancelled)
    }

    fn update_ui(&self, snapshot: &StatusSnapshot, _connection: &str) {
        let connection = snapshot
            .telemetry
            .get("_ui_connection")
            .filter(|value| value.is_object())
            .cloned()
            .unwrap_or_else(|| json!({"state":"Scanning"}));
        let pet_status = snapshot.telemetry.get("_ui_pet").cloned();
        let mut connection = connection;
        if connection.get("voice_state").is_none() {
            let readiness = self.voice.readiness();
            if let Some(object) = connection.as_object_mut() {
                object.insert(
                    "voice_state".into(),
                    readiness.get("state").cloned().unwrap_or_else(|| json!("unavailable")),
                );
                if let Some(detail) = readiness.get("detail") {
                    object.insert("voice_detail".into(), detail.clone());
                }
            }
        }
        if let (Some(connection), Some(pet)) = (connection.as_object_mut(), pet_status) {
            apply_pet_progress(connection, &pet);
        }
        if let Some(selected) = connection.get("selected_watch_id").and_then(Value::as_str) {
            if let Ok(mut stored) = self.selected_watch_id.lock() {
                *stored = Some(selected.to_owned());
            }
        }
        self.voice.update_snapshot(snapshot);
        self.ui_state.send_modify(|state| {
            state.snapshot = snapshot.clone();
            state.connection = connection.clone();
            if state.preview.is_some() {
                merge_preview_pet_status(&mut state.connection, state.preview.as_deref());
            }
        });
    }

    fn pairing_code(&self, code: &str) {
        let code = if code.len() == 6 && code.bytes().all(|byte| byte.is_ascii_digit()) {
            code.to_owned()
        } else {
            String::new()
        };
        self.ui_state.send_modify(|state| state.pairing_code = code.clone());
    }

    fn show_pet_preview(&self, package: PetPackage) -> Result<()> {
        MacDesktop::show_pet_preview(self, package)
    }

    fn clear_pet_preview(&self) {
        MacDesktop::clear_pet_preview(self)
    }
}

impl Drop for MacDesktop {
    fn drop(&mut self) {
        let _ = self.voice.stop(true);
        let _ = self.join_install_callbacks();
    }
}

struct UiRunningGuard<'a>(&'a AtomicBool);

impl Drop for UiRunningGuard<'_> {
    fn drop(&mut self) {
        self.0.store(false, Ordering::Release);
    }
}

fn set_pet_status(connection: &mut Value, headline: &str, detail: &str, progress: f64, can_send: bool, can_cancel: bool, can_restore: bool) {
    let Some(object) = connection.as_object_mut() else { return; };
    object.insert("pet_headline".into(), json!(headline));
    object.insert("pet_detail".into(), json!(detail));
    object.insert("pet_progress".into(), json!(progress.clamp(0.0, 1.0)));
    object.insert("pet_can_send".into(), json!(can_send));
    object.insert("pet_can_cancel".into(), json!(can_cancel));
    object.insert("pet_can_restore".into(), json!(can_restore));
}

fn apply_pet_progress(connection: &mut serde_json::Map<String, Value>, progress: &Value) {
    let phase = progress.get("phase").and_then(Value::as_str).unwrap_or("idle");
    let acknowledged = progress.get("acknowledged_bytes").and_then(Value::as_u64).unwrap_or(0);
    let total = progress.get("total_bytes").and_then(Value::as_u64).unwrap_or(0);
    let required = progress.get("required_bytes").and_then(Value::as_u64);
    let available = progress.get("available_bytes").and_then(Value::as_u64);
    let error = progress.get("error_code").and_then(Value::as_str).unwrap_or("");
    let fraction = if total == 0 { 0.0 } else { acknowledged as f64 / total as f64 };
    let detail = if !error.is_empty() {
        format!("Transfer failed · {error}")
    } else if phase == "complete" {
        "Watch confirmed the new pet package".to_owned()
    } else if let (Some(required), Some(available)) = (required, available) {
        format!("{acknowledged}/{total} bytes · needs {required}, available {available}")
    } else if total > 0 {
        format!("{acknowledged}/{total} bytes acknowledged")
    } else {
        "No transfer in progress".to_owned()
    };
    let complete = phase == "complete";
    connection.insert("pet_headline".into(), json!(if complete { "Watch pet updated" } else { phase }));
    connection.insert("pet_phase".into(), json!(phase));
    connection.insert("pet_detail".into(), json!(detail));
    connection.insert("pet_progress".into(), json!(fraction.clamp(0.0, 1.0)));
    connection.insert("pet_can_send".into(), json!(false));
    connection.insert("pet_can_cancel".into(), json!(total > 0 && !complete && error.is_empty()));
    connection.insert("pet_can_restore".into(), json!(complete));
    connection.insert("pet_request_id".into(), progress.get("request_id").cloned().unwrap_or(Value::Null));
}

fn merge_preview_pet_status(connection: &mut Value, preview: Option<&PetPreview>) {
    let Some(preview) = preview else { return; };
    if connection.get("pet_phase").and_then(Value::as_str).is_some_and(|phase| phase != "idle") {
        return;
    }
    match preview.kind {
        PetPreviewKind::Candidate => set_pet_status(
            connection,
            &preview.label,
            &format!("{} frames · {} × {} · ready to send", preview.package.frames.len(), preview.package.width, preview.package.height),
            0.0,
            true,
            false,
            false,
        ),
        PetPreviewKind::Active => {
            let connected = connection
                .get("state")
                .and_then(Value::as_str)
                .is_some_and(|state| matches!(state, "connected" | "live" | "ready" | "BLE LIVE"));
            let pet = connection.get("pet");
            let restore_available = connected
                && pet.and_then(|value| value.get("capability")).and_then(Value::as_str) == Some("ready")
                && pet.and_then(|value| value.get("source")).and_then(Value::as_str) == Some("custom")
                && pet.and_then(|value| value.get("usable")).and_then(Value::as_bool) == Some(true)
                && pet.and_then(|value| value.get("operation_active")).and_then(Value::as_bool) != Some(true);
            set_pet_status(
                connection,
                &preview.label,
                &format!("{} frames · {} × {} · cached watch pet", preview.package.frames.len(), preview.package.width, preview.package.height),
                0.0,
                false,
                false,
                restore_available,
            );
        }
    }
}

fn build_pet_preview(package: PetPackage, label: String, kind: PetPreviewKind) -> Result<PetPreview> {
    let frames = (0..package.frames.len())
        .map(|index| render_rgba(&package, index).map(|image| Arc::<[u8]>::from(image.into_raw())))
        .collect::<Result<Vec<_>>>()?;
    if frames.is_empty() {
        bail!("converted pet package contains no frames");
    }
    let durations = package
        .frames
        .iter()
        .map(|frame| Duration::from_millis(frame.duration_ms as u64))
        .collect();
    Ok(PetPreview {
        package: Arc::new(package),
        frames,
        durations,
        started: Instant::now(),
        label,
        kind,
    })
}

#[cfg(target_os = "macos")]
struct UiRunContext {
    commands: mpsc::Sender<DeviceCommand>,
    receiver: watch::Receiver<UiState>,
    ui_sender: watch::Sender<UiState>,
    selected_watch_id: Arc<Mutex<Option<String>>>,
    latest_pet: Arc<Mutex<Option<Arc<PetPackage>>>>,
    active_preview_generation: Arc<AtomicU64>,
    pet_cache_dir: PathBuf,
    voice: Arc<VoiceController>,
    install_callbacks: Arc<Mutex<Vec<JoinHandle<()>>>>,
    last_state: UiState,
    last_preview_frame: Option<usize>,
}

#[cfg(target_os = "macos")]
#[repr(C)]
struct SwuiHandle {
    _private: [u8; 0],
}

#[cfg(target_os = "macos")]
unsafe extern "C" {
    fn swui_create(context: *mut c_void, callback: UiCallback, tick: UiTickCallback) -> *mut SwuiHandle;
    fn swui_run(handle: *mut SwuiHandle) -> i32;
    fn swui_update(handle: *mut SwuiHandle, snapshot: *const c_char, connection: *const c_char);
    fn swui_pairing_code(handle: *mut SwuiHandle, code: *const c_char);
    fn swui_pet_state(handle: *mut SwuiHandle, headline: *const c_char, detail: *const c_char, progress: f64, can_send: i32, can_cancel: i32, can_restore: i32);
    fn swui_pet_preview_rgba(handle: *mut SwuiHandle, bytes: *const u8, byte_count: usize, width: i32, height: i32);
    fn swui_pet_preview_visible(handle: *mut SwuiHandle) -> i32;
    fn swui_shutdown(handle: *mut SwuiHandle);
    fn swui_destroy(handle: *mut SwuiHandle);
    fn swui_connection_enabled() -> i32;
    fn swui_set_connection_enabled(enabled: i32);
}

#[cfg(target_os = "macos")]
type UiCallback = unsafe extern "C" fn(*mut c_void, i32, *const c_char);
#[cfg(target_os = "macos")]
type UiTickCallback = unsafe extern "C" fn(*mut c_void, *mut SwuiHandle);

#[cfg(target_os = "macos")]
unsafe extern "C" fn native_ui_event(context: *mut c_void, command: i32, raw_value: *const c_char) {
    if context.is_null() { return; }
    // SAFETY: the callback context remains a Box<UiRunContext> for the entire
    // main-thread run loop, and the C shim invokes menu callbacks synchronously.
    let context = unsafe { &*(context.cast::<UiRunContext>()) };
    let value = if raw_value.is_null() {
        String::new()
    } else {
        // SAFETY: native callback values are temporary NUL-terminated UTF-8.
        let bytes = unsafe { CStr::from_ptr(raw_value) }.to_bytes();
        if bytes.len() > MAX_UI_VALUE_BYTES { return; }
        match std::str::from_utf8(bytes) { Ok(value) => value.to_owned(), Err(_) => return }
    };
    match command {
        1 => send_connection_command(context, DeviceCommand::Reconnect, true),
        2 if !value.is_empty() && value.len() <= 128 => send_command(&context.commands, DeviceCommand::SelectWatch(value)),
        3 if !value.trim().is_empty() && value.len() <= 24 && !value.chars().any(char::is_control) => send_command(&context.commands, DeviceCommand::RenameAgent(value.trim().to_owned())),
        4 => {
            let selected = context.selected_watch_id.lock().ok().and_then(|value| value.clone());
            if let Some(selected) = selected { send_command(&context.commands, DeviceCommand::ForgetWatch(selected)); }
        }
        5 if matches!(value.as_str(), "true" | "false") => {
            let enabled = value == "true";
            send_connection_command(context, DeviceCommand::SetPreferences(json!({"connectionEnabled": enabled})), enabled);
        }
        6 => {
            let voice = Arc::clone(&context.voice);
            let sender = context.ui_sender.clone();
            let state_dir = context.pet_cache_dir.parent().unwrap_or(&context.pet_cache_dir).to_path_buf();
            let callback = std::thread::Builder::new().name("voice-driver-setup".into()).spawn(move || {
                let result = voice.install_driver(&state_dir);
                let receiver = sender.subscribe();
                let mut text = receiver.borrow().clone();
                if let Err(error) = result {
                    let mut connection = text.connection.as_object().cloned().map(Value::Object).unwrap_or_else(|| json!({}));
                    connection["voice_state"] = json!("error");
                    connection["voice_detail"] = json!(error.to_string());
                    text.connection = connection;
                } else {
                    text.connection["voice_state"] = json!(voice.readiness().get("state").and_then(Value::as_str).unwrap_or("ready"));
                }
                sender.send_replace(text);
            });
            if let Ok(callback) = callback {
                if let Ok(mut callbacks) = context.install_callbacks.lock() {
                    callbacks.push(callback);
                }
            }
        }
        7 => { /* The native action already opened the preview window. */ }
        8 => choose_pet(context, PathBuf::from(value)),
        9 => {
            let package = context.latest_pet.lock().ok().and_then(|package| package.clone());
            if let Some(package) = package { send_command(&context.commands, DeviceCommand::UploadPet((*package).clone())); }
        }
        10 => send_command(&context.commands, DeviceCommand::CancelPet),
        11 => send_command(&context.commands, DeviceCommand::RestorePet),
        12 => send_command(&context.commands, DeviceCommand::Quit),
        _ => {}
    }
}

#[cfg(target_os = "macos")]
fn send_connection_command(context: &UiRunContext, command: DeviceCommand, enabled: bool) {
    match context.commands.try_send(command) {
        Ok(()) => {
            // Persist only an accepted control request. The supervisor publishes
            // the authoritative enabled state after it applies the command.
            unsafe { swui_set_connection_enabled(i32::from(enabled)); }
            context.ui_sender.send_modify(|state| {
                if !state.connection.is_object() { state.connection = json!({}); }
                state.connection["connection_enabled"] = json!(enabled);
                state.connection["state"] = json!(if enabled { "initializing" } else { "disabled" });
                state.connection.as_object_mut().unwrap().remove("error");
                state.pairing_code.clear();
            });
            tracing::info!(connection_enabled=enabled, "Watch connection command accepted");
        }
        Err(error) => {
            let reason = match error {
                TrySendError::Closed(_) => "Agent runtime is not running; restart the Agent",
                TrySendError::Full(_) => "Agent command queue is busy; try again",
            };
            tracing::error!(error=reason, "Watch connection command rejected");
            context.ui_sender.send_modify(|state| {
                if !state.connection.is_object() { state.connection = json!({}); }
                state.connection["state"] = json!("error");
                state.connection["error"] = json!(reason);
            });
        }
    }
}

#[cfg(target_os = "macos")]
fn send_command(commands: &mpsc::Sender<DeviceCommand>, command: DeviceCommand) {
    match commands.try_send(command) {
        Ok(()) | Err(TrySendError::Closed(_)) => {}
        Err(TrySendError::Full(command)) if matches!(&command, DeviceCommand::Quit) => {
            // Quit cannot be lost behind a temporarily full bounded command
            // queue. Keep AppKit responsive while a worker performs one
            // blocking send; runtime shutdown closes the sender if needed.
            let commands = commands.clone();
            let _ = std::thread::Builder::new()
                .name("agent-ui-quit".into())
                .spawn(move || { let _ = commands.blocking_send(command); });
        }
        Err(TrySendError::Full(_)) => {
            // At-most-once menu actions are consumed. The next status update
            // communicates the resulting state; no callback retries a command.
        }
    }
}

#[cfg(target_os = "macos")]
fn choose_pet(context: &UiRunContext, path: PathBuf) {
    if !path.is_file() || !path.extension().is_some_and(|extension| extension.eq_ignore_ascii_case("gif")) {
        return;
    }
    context.active_preview_generation.fetch_add(1, Ordering::AcqRel);
    let sender = context.ui_sender.clone();
    let latest_pet = Arc::clone(&context.latest_pet);
    let path_label = path.file_name().and_then(|name| name.to_str()).unwrap_or("Selected GIF").to_owned();
    let _ = std::thread::Builder::new().name("pet-gif-convert".into()).spawn(move || {
        let result = convert_gif(&path).and_then(|package| build_pet_preview(package, path_label, PetPreviewKind::Candidate));
        let receiver = sender.subscribe();
        let mut current = receiver.borrow().clone();
        match result {
            Ok(preview) => {
                if let Ok(mut latest) = latest_pet.lock() { *latest = Some(Arc::clone(&preview.package)); }
                current.preview = Some(Arc::new(preview));
                merge_preview_pet_status(&mut current.connection, current.preview.as_deref());
            }
            Err(error) => set_pet_status(&mut current.connection, "GIF conversion failed", &error.to_string(), 0.0, false, false, false),
        }
        sender.send_replace(current);
    });
}

#[cfg(target_os = "macos")]
unsafe extern "C" fn native_ui_tick(context: *mut c_void, handle: *mut SwuiHandle) {
    if context.is_null() || handle.is_null() { return; }
    // SAFETY: this callback is invoked by the NSRunLoop timer on the main
    // thread while `run_ui` keeps the context and native handle alive.
    let context = unsafe { &mut *(context.cast::<UiRunContext>()) };
    let changed = context.receiver.has_changed().unwrap_or(false);
    if changed {
        context.last_state = context.receiver.borrow_and_update().clone();
        let snapshot = CString::new(serde_json::to_string(&context.last_state.snapshot).unwrap_or_else(|_| "{}".into())).unwrap_or_default();
        let connection = CString::new(serde_json::to_string(&context.last_state.connection).unwrap_or_else(|_| "{}".into())).unwrap_or_default();
        let pairing = CString::new(context.last_state.pairing_code.clone()).unwrap_or_default();
        let pet = context.last_state.connection.get("pet_headline").and_then(Value::as_str).unwrap_or("Choose a GIF to preview");
        let detail = context.last_state.connection.get("pet_detail").and_then(Value::as_str).unwrap_or("No converted pet selected");
        let progress = context.last_state.connection.get("pet_progress").and_then(Value::as_f64).unwrap_or(0.0);
        let can_send = context.last_state.connection.get("pet_can_send").and_then(Value::as_bool).unwrap_or(false);
        let can_cancel = context.last_state.connection.get("pet_can_cancel").and_then(Value::as_bool).unwrap_or(false);
        let can_restore = context.last_state.connection.get("pet_can_restore").and_then(Value::as_bool).unwrap_or(false);
        // SAFETY: these FFI calls are intentionally made only from this AppKit
        // main-loop callback, and each CString is alive for the call.
        unsafe {
            swui_update(handle, snapshot.as_ptr(), connection.as_ptr());
            swui_pairing_code(handle, pairing.as_ptr());
            let headline = CString::new(pet).unwrap_or_default();
            let detail = CString::new(detail).unwrap_or_default();
            swui_pet_state(handle, headline.as_ptr(), detail.as_ptr(), progress, i32::from(can_send), i32::from(can_cancel), i32::from(can_restore));
        }
        context.last_preview_frame = None;
    }
    // Animate pet frames only while the retained preview window is visible.
    // The surrounding status menu still refreshes on its bounded UI timer.
    if let Some(preview) = context.last_state.preview.as_ref()
        && unsafe { swui_pet_preview_visible(handle) == 1 }
    {
        let frame = preview.current_frame(Instant::now());
        if context.last_preview_frame != Some(frame) {
            if let Some(bytes) = preview.frames.get(frame) {
                // SAFETY: bytes are an RGBA frame sized by the validated pet
                // renderer, and the native function copies them synchronously.
                unsafe { swui_pet_preview_rgba(handle, bytes.as_ptr(), bytes.len(), preview.package.width as i32, preview.package.height as i32); }
                context.last_preview_frame = Some(frame);
            }
        }
    }
}

#[cfg(target_os = "macos")]
fn native_connection_enabled() -> bool {
    // SAFETY: this Foundation preference read is independent of the AppKit
    // handle and is intentionally performed only on normal product launch.
    unsafe { swui_connection_enabled() == 1 }
}

#[cfg(not(target_os = "macos"))]
fn native_connection_enabled() -> bool { true }

mod macos {
    pub fn accessibility_trusted() -> bool {
        crate::macos::accessibility_trusted()
    }
}

