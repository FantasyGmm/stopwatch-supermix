//! Core Audio routing and mode-aware Typeless/task voice lifecycle.
//!
//! Audio is always mono signed 16-bit PCM at 16 kHz at this boundary. The
//! existing CoreAudio bridge owns the 48 kHz stereo sink conversion; Rust owns
//! bounded startup buffering, Typeless shortcut pairing, route binding, and
//! once-only cleanup.

use crate::contracts::{InputAction, StatusSnapshot, TaskDetail, VoiceMode};
use anyhow::{Context, Result, anyhow, bail};
use serde_json::{Value, json};
use sha2::{Digest, Sha256};
use std::{
    collections::{HashMap, VecDeque},
    ffi::{CString, c_char},
    fs::{self, File, OpenOptions},
    io::{Read, Write},
    path::{Path, PathBuf},
    process::{Command, Stdio},
    sync::{
        Arc, Condvar, Mutex, RwLock, OnceLock, mpsc,
        atomic::{AtomicBool, AtomicU64, Ordering},
    },
    thread::{self, JoinHandle},
    time::{Duration, Instant},
};
use rusqlite::OptionalExtension;

const BLACKHOLE_DEVICE_NAME: &str = "BlackHole 2ch";
const BLACKHOLE_PACKAGE_URL: &str = "https://existential.audio/downloads/BlackHole2ch-0.7.1.pkg";
const BLACKHOLE_SHA256: &str = "57b540f27a3e29c37e310e01bee0fdfab76733087e47f997ef9dccf851400dcf";
const BLACKHOLE_RECEIPT: &str = "audio.existential.BlackHole2ch";
const TYPELESS_BUNDLE_ID: &str = "now.typeless.desktop";
const MAX_STARTUP_SECONDS: usize = 10;
const MAX_STARTUP_SAMPLES: usize = 16_000 * MAX_STARTUP_SECONDS;
const MAX_PCM_CALL_SAMPLES: usize = 16_000 * 2;
const ROUTE_DRAIN: Duration = Duration::from_millis(150);
const TRANSCRIPT_TIMEOUT: Duration = Duration::from_secs(5);
const TRANSCRIPT_POLL: Duration = Duration::from_millis(100);
const MAX_TRANSCRIPT_BYTES: usize = 64 * 1024;
const DRIVER_INSTALL_TIMEOUT: Duration = Duration::from_secs(10 * 60);
const CHILD_POLL: Duration = Duration::from_millis(100);

#[derive(Clone, Debug)]
struct TranscriptCursor {
    created_at: String,
    row_id: String,
}

#[derive(Clone, Debug, PartialEq, Eq)]
struct TaskBinding {
    route: String,
    revision: u64,
    source: String,
    task_id: String,
    state: String,
    turn_id: Option<String>,
}

#[derive(Default)]
struct Session {
    mode: Option<VoiceMode>,
    route_started: bool,
    shortcut_started: bool,
    transcript: Option<TranscriptCursor>,
    task: Option<TaskBinding>,
    generation: u64,
}

struct ManagedJob {
    id: u64,
    cancelled: Arc<AtomicBool>,
    thread: JoinHandle<()>,
}

#[derive(Default)]
struct StartupBuffer {
    buffering: bool,
    overflowed: bool,
    samples: VecDeque<i16>,
}

/// Injectable seam for route, shortcut, transcript, and task submission.
/// Tests use a fake and never post input or access real application data.
trait VoiceBackend: Send + Sync {
    fn readiness(&self) -> Value;
    fn route_start(&self) -> Result<()>;
    fn route_write(&self, samples: &[i16]) -> Result<usize>;
    fn route_stop(&self) -> Result<()>;
    fn shortcut_start(&self) -> Result<()>;
    fn shortcut_stop(&self) -> Result<()>;
    fn transcript_mark(&self) -> Result<TranscriptCursor>;
    fn transcript_wait(&self, cursor: &TranscriptCursor, timeout: Duration, cancelled: &AtomicBool) -> Result<String>;
    fn submit_task(&self, task_id: &str, text: &str, action: InputAction, expected_turn: Option<&str>) -> Result<()>;
    fn submit_task_cancellable(
        &self,
        task_id: &str,
        text: &str,
        action: InputAction,
        expected_turn: Option<&str>,
        cancelled: &AtomicBool,
        _commit_gate: &Mutex<()>,
        validate_route: &dyn Fn() -> Result<()>,
    ) -> Result<()> {
        if cancelled.load(Ordering::Acquire) { return Ok(()); }
        validate_route()?;
        if cancelled.load(Ordering::Acquire) { return Ok(()); }
        self.submit_task(task_id, text, action, expected_turn)
    }
    fn install_driver(&self, state_dir: &Path, cancelled: &AtomicBool) -> Result<()>;
}

pub struct VoiceController {
    backend: Arc<dyn VoiceBackend>,
    session: Mutex<Session>,
    startup: Mutex<StartupBuffer>,
    routes: Arc<RwLock<HashMap<String, TaskBinding>>>,
    generation: Arc<AtomicU64>,
    completion_gate: Arc<Mutex<()>>,
    finalizer: Mutex<Option<ManagedJob>>,
    installer: Mutex<Option<ManagedJob>>,
    next_job_id: AtomicU64,
}

impl VoiceController {
    pub fn new(codex_root: PathBuf) -> Self {
        Self::with_backend(Arc::new(MacVoiceBackend {
            codex_root: Some(codex_root),
            ..MacVoiceBackend::default()
        }))
    }

    fn with_backend(backend: Arc<dyn VoiceBackend>) -> Self {
        Self {
            backend,
            session: Mutex::new(Session::default()),
            startup: Mutex::new(StartupBuffer::default()),
            routes: Arc::new(RwLock::new(HashMap::new())),
            generation: Arc::new(AtomicU64::new(0)),
            completion_gate: Arc::new(Mutex::new(())),
            finalizer: Mutex::new(None),
            installer: Mutex::new(None),
            next_job_id: AtomicU64::new(1),
        }
    }

    pub fn readiness(&self) -> Value {
        if self.session.lock().ok().is_some_and(|session| session.mode.is_some()) {
            return json!({"state":"recording"});
        }
        self.backend.readiness()
    }

    pub fn update_snapshot(&self, snapshot: &StatusSnapshot) {
        let alerts = snapshot.codex.get("codex_alerts").and_then(Value::as_array);
        let mut next = HashMap::new();
        for detail in snapshot.details.values() {
            if detail.source != "codex" || !valid_route(&detail.route) || detail.task_id.is_empty() {
                continue;
            }
            let matching_alert = alerts.and_then(|alerts| alerts.iter().find(|alert| {
                alert.get("route").and_then(Value::as_str) == Some(detail.route.as_str())
                    && alert.get("thread_id").and_then(Value::as_str) == Some(detail.task_id.as_str())
            }));
            let Some(alert) = matching_alert else { continue; };
            let Some(state) = alert.get("state").and_then(Value::as_str) else { continue; };
            if !matches!(state, "pending" | "running" | "completed") { continue; }
            let turn_id = alert.get("turn_id").and_then(Value::as_str).filter(|id| !id.is_empty()).map(str::to_owned);
            if state == "running" && turn_id.is_none() { continue; }
            next.insert(detail.route.clone(), TaskBinding {
                route: detail.route.clone(),
                revision: detail.revision,
                source: detail.source.clone(),
                task_id: detail.task_id.clone(),
                state: state.to_owned(),
                turn_id,
            });
        }
        if let Ok(mut routes) = self.routes.write() { *routes = next; }
    }

    pub fn start(&self, mode: VoiceMode, task: Option<TaskDetail>) -> Result<()> {
        let mut session = self.session.lock().map_err(|_| anyhow!("voice session is unavailable"))?;
        if session.mode.is_some() {
            bail!("a voice session is already active");
        }
        let generation = self.invalidate_finalizer()?;
        let binding = match mode {
            VoiceMode::TaskConversation => Some(self.resolve_task(task.as_ref().context("task voice requires a route")?)?),
            VoiceMode::PushToTalk if task.is_some() => bail!("push-to-talk does not accept a task route"),
            VoiceMode::PushToTalk | VoiceMode::PinnedMicrophone => None,
        };
        let transcript = if binding.is_some() {
            Some(self.backend.transcript_mark().context("Typeless transcript history is unavailable")?)
        } else {
            None
        };

        {
            let mut startup = self.startup.lock().map_err(|_| anyhow!("voice startup buffer is unavailable"))?;
            startup.buffering = true;
            startup.overflowed = false;
            startup.samples.clear();
        }
        session.mode = Some(mode);
        session.task = binding;
        session.transcript = transcript;
        session.generation = generation;
        if let Err(error) = self.backend.route_start() {
            self.reset_startup();
            *session = Session::default();
            return Err(error).context("start BlackHole audio route");
        }
        session.route_started = true;
        if matches!(mode, VoiceMode::PushToTalk | VoiceMode::TaskConversation) {
            if let Err(error) = self.backend.shortcut_start() {
                let cleanup = self.cleanup_locked(&mut session);
                return Err(combine_primary_cleanup(error, cleanup)).context("start Typeless dictation");
            }
            session.shortcut_started = true;
        }

        let mut startup = self.startup.lock().map_err(|_| anyhow!("voice startup buffer is unavailable"))?;
        if startup.overflowed {
            drop(startup);
            let cleanup = self.cleanup_locked(&mut session);
            return Err(combine_primary_cleanup(anyhow!("audio startup exceeded the ten-second buffer"), cleanup));
        }
        startup.buffering = false;
        let buffered: Vec<i16> = startup.samples.drain(..).collect();
        drop(startup);
        if !buffered.is_empty() {
            if let Err(error) = self.write_audio(&buffered) {
                let cleanup = self.cleanup_locked(&mut session);
                return Err(combine_primary_cleanup(error, cleanup)).context("flush startup audio buffer");
            }
        }
        Ok(())
    }

    pub fn pcm(&self, samples: &[i16]) -> Result<()> {
        if samples.is_empty() { return Ok(()); }
        if samples.len() > MAX_PCM_CALL_SAMPLES {
            bail!("audio frame exceeds the bounded PCM input size");
        }
        {
            let mut startup = self.startup.lock().map_err(|_| anyhow!("voice startup buffer is unavailable"))?;
            if startup.buffering {
                let remaining = MAX_STARTUP_SAMPLES.saturating_sub(startup.samples.len());
                if samples.len() > remaining {
                    startup.overflowed = true;
                    return Err(anyhow!("audio startup exceeded the ten-second buffer"));
                }
                startup.samples.extend(samples.iter().copied());
                return Ok(());
            }
        }
        let mut session = self.session.lock().map_err(|_| anyhow!("voice session is unavailable"))?;
        if session.mode.is_none() || !session.route_started {
            bail!("voice session is not recording");
        }
        if let Err(error) = self.write_audio(samples) {
            let cleanup = self.cleanup_locked(&mut session);
            return Err(combine_primary_cleanup(error, cleanup)).context("write voice audio");
        }
        Ok(())
    }

    pub fn stop(&self, cancelled: bool) -> Result<()> {
        let mut session = self.session.lock().map_err(|_| anyhow!("voice session is unavailable"))?;
        if cancelled {
            self.invalidate_finalizer()?;
            self.cancel_installer()?;
        }
        if session.mode.is_none() {
            drop(session);
            // A prior route cleanup may have failed while restoring the user's
            // input device. Keep this idle retry path so disconnect and quit
            // can retry the retained UID lease.
            return if cancelled { self.backend.route_stop() } else { Ok(()) };
        }
        let task = session.task.take();
        let transcript = session.transcript.take();
        let generation = session.generation;
        let cleanup = self.cleanup_locked(&mut session);
        if let Some(error) = cleanup { return Err(error).context("clean up voice route"); }
        drop(session);

        if cancelled { return Ok(()); }
        let (Some(task), Some(cursor)) = (task, transcript) else { return Ok(()); };
        self.spawn_finalizer(task, cursor, generation)
    }

    pub fn install_driver(&self, state_dir: &Path) -> Result<()> {
        let mut installer = self.installer.lock().map_err(|_| anyhow!("driver installer state is unavailable"))?;
        if installer.is_some() { bail!("BlackHole driver installation is already in progress"); }

        let id = self.next_job_id.fetch_add(1, Ordering::Relaxed);
        let cancelled = Arc::new(AtomicBool::new(false));
        let worker_cancelled = cancelled.clone();
        let backend = self.backend.clone();
        let state_dir = state_dir.to_path_buf();
        let (result_tx, result_rx) = mpsc::sync_channel(1);
        let worker = thread::Builder::new().name("voice-driver-installer".into()).spawn(move || {
            let result = backend.install_driver(&state_dir, &worker_cancelled);
            let _ = result_tx.send(result);
        }).context("start managed BlackHole driver installation")?;
        *installer = Some(ManagedJob { id, cancelled, thread: worker });
        drop(installer);

        let result = result_rx.recv().unwrap_or_else(|_| Err(anyhow!("BlackHole driver installer stopped unexpectedly")));
        self.join_job(&self.installer, id, false)?;
        result
    }

    fn resolve_task(&self, detail: &TaskDetail) -> Result<TaskBinding> {
        if detail.source != "codex" || !valid_route(&detail.route) || detail.task_id.is_empty() {
            bail!("Task voice route is unavailable");
        }
        let binding = self.routes.read().map_err(|_| anyhow!("Task routes are unavailable"))?
            .get(&detail.route).cloned().ok_or_else(|| anyhow!("Task voice route is unavailable"))?;
        if binding.source != detail.source || binding.task_id != detail.task_id || binding.revision != detail.revision {
            bail!("Task voice route is stale");
        }
        Ok(binding)
    }

    fn spawn_finalizer(&self, task: TaskBinding, cursor: TranscriptCursor, generation: u64) -> Result<()> {
        let mut finalizer = self.finalizer.lock().map_err(|_| anyhow!("task voice finalizer state is unavailable"))?;
        if finalizer.is_some() { bail!("a task voice transcript is already being finalized"); }
        let id = self.next_job_id.fetch_add(1, Ordering::Relaxed);
        let cancelled = Arc::new(AtomicBool::new(false));
        let worker_cancelled = cancelled.clone();
        let backend = self.backend.clone();
        let routes = self.routes.clone();
        let active_generation = self.generation.clone();
        let completion_gate = self.completion_gate.clone();
        let worker = thread::Builder::new().name("task-voice-finalizer".into()).spawn(move || {
            if let Err(error) = finalize_task_transcript(
                backend, routes, active_generation, completion_gate,
                worker_cancelled, generation, task, cursor,
            ) {
                tracing::warn!(error = %error, "task voice transcript finalization failed");
            }
        }).context("start task voice transcript finalizer")?;
        *finalizer = Some(ManagedJob { id, cancelled, thread: worker });
        Ok(())
    }

    fn invalidate_finalizer(&self) -> Result<u64> {
        let generation = self.generation.fetch_add(1, Ordering::AcqRel).wrapping_add(1);
        let job = {
            let mut finalizer = self.finalizer.lock().map_err(|_| anyhow!("task voice finalizer state is unavailable"))?;
            let job = finalizer.take();
            if let Some(job) = &job { job.cancelled.store(true, Ordering::Release); }
            job
        };
        if let Some(job) = job { let _ = job.thread.join(); }
        Ok(generation)
    }

    fn cancel_installer(&self) -> Result<()> {
        let job = {
            let mut installer = self.installer.lock().map_err(|_| anyhow!("driver installer state is unavailable"))?;
            let job = installer.take();
            if let Some(job) = &job { job.cancelled.store(true, Ordering::Release); }
            job
        };
        if let Some(job) = job {
            job.thread.join().map_err(|_| anyhow!("BlackHole driver installer thread panicked"))?;
        }
        Ok(())
    }

    fn join_job(&self, slot: &Mutex<Option<ManagedJob>>, id: u64, cancel: bool) -> Result<()> {
        let job = {
            let mut slot = slot.lock().map_err(|_| anyhow!("managed worker state is unavailable"))?;
            if slot.as_ref().is_some_and(|job| job.id == id) { slot.take() } else { None }
        };
        if let Some(job) = job {
            if cancel { job.cancelled.store(true, Ordering::Release); }
            job.thread.join().map_err(|_| anyhow!("managed worker thread panicked"))?;
        }
        Ok(())
    }

    fn write_audio(&self, samples: &[i16]) -> Result<()> {
        let accepted = self.backend.route_write(samples)?;
        if accepted != samples.len() {
            bail!("short voice audio write: accepted {accepted} of {} input samples", samples.len());
        }
        Ok(())
    }

    fn reset_startup(&self) {
        if let Ok(mut startup) = self.startup.lock() {
            startup.buffering = false;
            startup.overflowed = false;
            startup.samples.clear();
        }
    }

    fn cleanup_locked(&self, session: &mut Session) -> Option<anyhow::Error> {
        if session.mode.is_none() { return None; }
        let shortcut_started = std::mem::take(&mut session.shortcut_started);
        let route_started = std::mem::take(&mut session.route_started);
        *session = Session::default();
        self.reset_startup();
        if shortcut_started || route_started { thread::sleep(ROUTE_DRAIN); }
        let mut failures = Vec::new();
        if shortcut_started {
            if let Err(error) = self.backend.shortcut_stop() { failures.push((0_u8, error)); }
        }
        if route_started {
            if let Err(error) = self.backend.route_stop() { failures.push((1_u8, error)); }
        }
        failures.into_iter().min_by_key(|(order, _)| *order).map(|(_, error)| error)
    }
}

impl Default for VoiceController {
    fn default() -> Self { Self::with_backend(Arc::new(MacVoiceBackend::default())) }
}

fn combine_primary_cleanup(primary: anyhow::Error, cleanup: Option<anyhow::Error>) -> anyhow::Error {
    match cleanup {
        Some(cleanup) => anyhow!("{primary}; cleanup also failed: {cleanup}"),
        None => primary,
    }
}

fn valid_route(route: &str) -> bool {
    route.len() == 16 && route.bytes().all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
}

fn cancelled_or_stale(cancelled: &AtomicBool, generation: &AtomicU64, expected_generation: u64) -> bool {
    cancelled.load(Ordering::Acquire) || generation.load(Ordering::Acquire) != expected_generation
}

fn task_submission_for(routes: &HashMap<String, TaskBinding>, expected: &TaskBinding) -> Result<(InputAction, Option<String>)> {
    let current = routes.get(&expected.route)
        .ok_or_else(|| anyhow!("Task voice route is no longer available"))?;
    if current.route != expected.route || current.source != expected.source || current.task_id != expected.task_id {
        bail!("Task voice route changed during recording");
    }
    match current.state.as_str() {
        "pending" | "completed" => Ok((InputAction::Submit, None)),
        "running" => {
            let expected_turn = expected.turn_id.as_deref().filter(|turn| !turn.is_empty());
            let current_turn = current.turn_id.as_deref().filter(|turn| !turn.is_empty());
            if expected.state != "running" || expected_turn.is_none() || current_turn != expected_turn {
                bail!("Task voice running turn changed during recording");
            }
            Ok((InputAction::ForceSteer, current_turn.map(str::to_owned)))
        }
        _ => bail!("Task voice route is no longer eligible"),
    }
}

fn finalize_task_transcript(
    backend: Arc<dyn VoiceBackend>,
    routes: Arc<RwLock<HashMap<String, TaskBinding>>>,
    generation: Arc<AtomicU64>,
    completion_gate: Arc<Mutex<()>>,
    cancelled: Arc<AtomicBool>,
    expected_generation: u64,
    expected: TaskBinding,
    cursor: TranscriptCursor,
) -> Result<()> {
    if cancelled_or_stale(&cancelled, &generation, expected_generation) { return Ok(()); }
    let text = match backend.transcript_wait(&cursor, TRANSCRIPT_TIMEOUT, &cancelled) {
        Ok(text) => text,
        Err(_) if cancelled_or_stale(&cancelled, &generation, expected_generation) => return Ok(()),
        Err(error) => return Err(error).context("wait for the completed Typeless transcript"),
    };
    if cancelled_or_stale(&cancelled, &generation, expected_generation) { return Ok(()); }
    if text.trim().is_empty() || text.len() > MAX_TRANSCRIPT_BYTES {
        bail!("Typeless transcript is empty or exceeds the size limit");
    }

    if cancelled_or_stale(&cancelled, &generation, expected_generation) { return Ok(()); }
    let (action, expected_turn) = {
        let routes = routes.read().map_err(|_| anyhow!("Task routes are unavailable"))?;
        task_submission_for(&routes, &expected)?
    };
    let validate_route = || {
        let routes = routes.read().map_err(|_| anyhow!("Task routes are unavailable"))?;
        let (current_action, current_turn) = task_submission_for(&routes, &expected)?;
        if current_action != action || current_turn != expected_turn {
            bail!("Task voice action or turn changed before dispatch");
        }
        Ok(())
    };
    backend.submit_task_cancellable(
        &expected.task_id,
        &text,
        action,
        expected_turn.as_deref(),
        &cancelled,
        &completion_gate,
        &validate_route,
    )
        .context("submit route-bound task transcript")
}

impl Drop for VoiceController {
    fn drop(&mut self) {
        let _ = self.stop(true);
        let _ = self.cancel_installer();
        let _ = self.backend.route_stop();
    }
}

#[derive(Default)]
struct MacVoiceBackend {
    codex_root: Option<PathBuf>,
    route: Mutex<NativeRoute>,
    shortcut: Mutex<Option<Vec<u16>>>,
}

#[derive(Default)]
struct NativeRoute {
    opened: bool,
    started: bool,
    previous_uid: Option<String>,
    lease_claimed: bool,
}

impl VoiceBackend for MacVoiceBackend {
    fn readiness(&self) -> Value {
        #[cfg(target_os = "macos")]
        {
            let device_visible = find_device_uid(BLACKHOLE_DEVICE_NAME).is_ok();
            let receipt = package_receipt_present();
            let state = if device_visible { "ready" } else if receipt { "restart_required" } else { "driver_required" };
            if state != "ready" { return json!({"state":state}); }
            if !native_accessibility_trusted() { return json!({"state":"accessibility_required"}); }
            if !native_typeless_running() { return json!({"state":"typeless_not_running"}); }
            return json!({"state":"ready"});
        }
        #[cfg(not(target_os = "macos"))]
        { json!({"state":"unavailable"}) }
    }

    fn route_start(&self) -> Result<()> {
        #[cfg(target_os = "macos")]
        {
            let global = native_route_claim();
            if global.compare_exchange(false, true, Ordering::AcqRel, Ordering::Acquire).is_err() {
                bail!("another Core Audio voice route is active");
            }
            let mut route = match self.route.lock() {
                Ok(route) => route,
                Err(_) => { global.store(false, Ordering::Release); bail!("Core Audio route is unavailable"); }
            };
            if route.opened || route.started {
                global.store(false, Ordering::Release);
                bail!("Core Audio route is already open");
            }
            route.lease_claimed = true;
            let output_uid = match find_device_uid(BLACKHOLE_DEVICE_NAME) {
                Ok(uid) => uid,
                Err(error) => { route.lease_claimed = false; global.store(false, Ordering::Release); return Err(error); }
            };
            let uid = match CString::new(output_uid.clone()) {
                Ok(uid) => uid,
                Err(_) => {
                    route.lease_claimed = false;
                    global.store(false, Ordering::Release);
                    bail!("BlackHole device UID is invalid");
                }
            };
            let open_status = unsafe { swva_open(uid.as_ptr()) };
            if open_status != 0 {
                route.lease_claimed = false;
                global.store(false, Ordering::Release);
                bail!("open BlackHole audio sink failed ({open_status})");
            }
            route.opened = true;
            let start_status = unsafe { swva_start() };
            if start_status != 0 {
                drop(route);
                let cleanup = self.route_stop();
                return Err(combine_primary_cleanup(anyhow!("start BlackHole audio sink failed ({start_status})"), cleanup.err()));
            }
            route.started = true;
            drop(route);

            let previous_uid = match default_input_uid() {
                Ok(uid) => uid,
                Err(error) => {
                    let cleanup = self.route_stop();
                    return Err(combine_primary_cleanup(error, cleanup.err()));
                }
            };
            {
                let mut route = self.route.lock().map_err(|_| anyhow!("Core Audio route is unavailable"))?;
                route.previous_uid = Some(previous_uid);
            }
            let switch_status = unsafe { swva_set_default_input_uid(uid.as_ptr()) };
            if switch_status != 0 {
                let cleanup = self.route_stop();
                return Err(combine_primary_cleanup(anyhow!("select BlackHole default input failed ({switch_status})"), cleanup.err()));
            }
            return Ok(());
        }
        #[cfg(not(target_os = "macos"))]
        { bail!("Core Audio voice routing is available only on macOS") }
    }

    fn route_write(&self, samples: &[i16]) -> Result<usize> {
        #[cfg(target_os = "macos")]
        {
            let route = self.route.lock().map_err(|_| anyhow!("Core Audio route is unavailable"))?;
            if !route.started { bail!("Core Audio route is not started"); }
            let output_frames = unsafe { swva_write_mono_16k(samples.as_ptr(), samples.len()) };
            return Ok((output_frames / 3) as usize);
        }
        #[cfg(not(target_os = "macos"))]
        { let _ = samples; bail!("Core Audio voice routing is available only on macOS") }
    }

    fn route_stop(&self) -> Result<()> {
        #[cfg(target_os = "macos")]
        {
            let mut route = self.route.lock().map_err(|_| anyhow!("Core Audio route is unavailable"))?;
            if !route.lease_claimed && !route.opened && !route.started && route.previous_uid.is_none() {
                return Ok(());
            }
            let opened = std::mem::take(&mut route.opened);
            let started = std::mem::take(&mut route.started);
            let mut failures = Vec::new();
            if started { unsafe { swva_stop(); } }
            if let Some(previous) = route.previous_uid.clone() {
                match CString::new(previous) {
                    Ok(uid) => {
                        let status = unsafe { swva_set_default_input_uid(uid.as_ptr()) };
                        if status != 0 { failures.push(format!("restore previous default input failed ({status})")); }
                        else { route.previous_uid = None; }
                    }
                    Err(_) => failures.push("restore previous default input failed (invalid UID)".to_owned()),
                }
            }
            if opened { unsafe { swva_close(); } }
            if route.previous_uid.is_none() {
                route.lease_claimed = false;
                native_route_claim().store(false, Ordering::Release);
            }
            if failures.is_empty() { Ok(()) } else { bail!("{}", failures.join("; ")) }
        }
        #[cfg(not(target_os = "macos"))]
        { Ok(()) }
    }

    fn shortcut_start(&self) -> Result<()> {
        #[cfg(target_os = "macos")]
        {
            if !native_accessibility_trusted() { bail!("Accessibility permission is required for Typeless voice"); }
            if !native_typeless_running() { bail!("Typeless is not running"); }
            let keys = load_typeless_shortcut()?;
            post_shortcut(&keys)?;
            *self.shortcut.lock().map_err(|_| anyhow!("Typeless shortcut is unavailable"))? = Some(keys);
            return Ok(());
        }
        #[cfg(not(target_os = "macos"))]
        { bail!("Typeless voice is available only on macOS") }
    }

    fn shortcut_stop(&self) -> Result<()> {
        let keys = self.shortcut.lock().map_err(|_| anyhow!("Typeless shortcut is unavailable"))?.take();
        if let Some(keys) = keys { post_shortcut(&keys)?; }
        Ok(())
    }

    fn transcript_mark(&self) -> Result<TranscriptCursor> { transcript_mark() }

    fn transcript_wait(&self, cursor: &TranscriptCursor, timeout: Duration, cancelled: &AtomicBool) -> Result<String> {
        transcript_wait(cursor, timeout, cancelled)
    }

    fn submit_task(&self, task_id: &str, text: &str, action: InputAction, expected_turn: Option<&str>) -> Result<()> {
        #[cfg(target_os = "macos")]
        {
            if task_id.is_empty() || task_id.len() > 256 || task_id.chars().any(char::is_control) {
                bail!("Codex task identifier is invalid");
            }
            if text.is_empty() || text.len() > MAX_TRANSCRIPT_BYTES || text.contains('\0') {
                bail!("Codex task transcript is invalid");
            }
            match action {
                InputAction::Submit if expected_turn.is_none() => {}
                InputAction::ForceSteer if expected_turn.is_some_and(|turn| !turn.is_empty()) => {}
                _ => bail!("Codex task voice submission state is invalid"),
            }
            let codex_root = self.codex_root.as_deref()
                .context("Codex preferences path is unavailable")?;
            return crate::macos::submit_task_to_codex(codex_root, task_id, text, action, expected_turn)
                .context("submit route-bound Codex task transcript");
        }
        #[cfg(not(target_os = "macos"))]
        { let _ = (task_id, text, action, expected_turn); bail!("Codex task submission is available only on macOS") }
    }

    #[cfg(target_os = "macos")]
    fn submit_task_cancellable(
        &self,
        task_id: &str,
        text: &str,
        action: InputAction,
        expected_turn: Option<&str>,
        cancelled: &AtomicBool,
        commit_gate: &Mutex<()>,
        validate_route: &dyn Fn() -> Result<()>,
    ) -> Result<()> {
        if task_id.is_empty() || task_id.len() > 256 || task_id.chars().any(char::is_control) {
            bail!("Codex task identifier is invalid");
        }
        if text.is_empty() || text.len() > MAX_TRANSCRIPT_BYTES || text.contains('\0') {
            bail!("Codex task transcript is invalid");
        }
        match action {
            InputAction::Submit if expected_turn.is_none() => {}
            InputAction::ForceSteer if expected_turn.is_some_and(|turn| !turn.is_empty()) => {}
            _ => bail!("Codex task voice submission state is invalid"),
        }
        if cancelled.load(Ordering::Acquire) { return Ok(()); }
        let codex_root = self.codex_root.as_deref()
            .context("Codex preferences path is unavailable")?;
        crate::macos::submit_task_to_codex_cancellable(
            codex_root,
            task_id,
            text,
            action,
            expected_turn,
            cancelled,
            commit_gate,
            validate_route,
        )
        .context("submit route-bound Codex task transcript")
    }

    fn install_driver(&self, state_dir: &Path, cancelled: &AtomicBool) -> Result<()> {
        install_blackhole_driver(state_dir, cancelled)
    }
}

#[cfg(target_os = "macos")]
static NATIVE_ROUTE_ACTIVE: OnceLock<AtomicBool> = OnceLock::new();

#[cfg(target_os = "macos")]
fn native_route_claim() -> &'static AtomicBool { NATIVE_ROUTE_ACTIVE.get_or_init(|| AtomicBool::new(false)) }

#[cfg(target_os = "macos")]
unsafe extern "C" {
    fn swva_find_device_uid(name: *const c_char, output: *mut c_char, output_size: usize) -> i32;
    fn swva_get_default_input_uid(output: *mut c_char, output_size: usize) -> i32;
    fn swva_set_default_input_uid(uid: *const c_char) -> i32;
    fn swva_open(uid: *const c_char) -> i32;
    fn swva_start() -> i32;
    fn swva_write_mono_16k(samples: *const i16, count: usize) -> usize;
    fn swva_stop();
    fn swva_close();
    fn swvoice_typeless_running() -> i32;
    fn swvoice_post_key(key_code: u16, down: i32) -> i32;
}

#[cfg(target_os = "macos")]
fn find_device_uid(name: &str) -> Result<String> {
    let name = CString::new(name)?;
    let mut output = vec![0_i8; 1024];
    // SAFETY: the input and output buffers remain valid for the native call.
    let status = unsafe { swva_find_device_uid(name.as_ptr(), output.as_mut_ptr(), output.len()) };
    if status != 0 { bail!("find BlackHole audio device failed ({status})"); }
    let uid = unsafe { std::ffi::CStr::from_ptr(output.as_ptr()) }.to_str()
        .context("BlackHole device UID is not UTF-8")?.to_owned();
    if uid.is_empty() { bail!("BlackHole device UID is empty"); }
    Ok(uid)
}

#[cfg(target_os = "macos")]
fn default_input_uid() -> Result<String> {
    let mut output = vec![0_i8; 1024];
    // SAFETY: the output buffer remains valid for the native call.
    let status = unsafe { swva_get_default_input_uid(output.as_mut_ptr(), output.len()) };
    if status != 0 { bail!("read default audio input failed ({status})"); }
    let uid = unsafe { std::ffi::CStr::from_ptr(output.as_ptr()) }.to_str()
        .context("default input UID is not UTF-8")?.to_owned();
    if uid.is_empty() { bail!("default input UID is empty"); }
    Ok(uid)
}

#[cfg(target_os = "macos")]
fn native_accessibility_trusted() -> bool {
    crate::macos::accessibility_trusted()
}

#[cfg(target_os = "macos")]
fn native_typeless_running() -> bool {
    // SAFETY: read-only NSWorkspace query.
    unsafe { swvoice_typeless_running() == 1 }
}

#[cfg(target_os = "macos")]
fn load_typeless_shortcut() -> Result<Vec<u16>> {
    let home = std::env::var_os("HOME").ok_or_else(|| anyhow!("Typeless settings path is unavailable"))?;
    let path = PathBuf::from(home).join("Library/Application Support/Typeless/app-settings.json");
    let bytes = fs::read(path).context("Typeless dictation shortcut could not be read")?;
    let settings: Value = serde_json::from_slice(&bytes).context("Typeless settings are invalid")?;
    let shortcut = settings.pointer("/featureShortcutBindings/dictationMode/0")
        .and_then(Value::as_str).ok_or_else(|| anyhow!("Typeless dictation shortcut is not configured"))?;
    if shortcut.is_empty() || shortcut.len() > 128 { bail!("Typeless dictation shortcut has an invalid shape"); }
    let mut keys = Vec::new();
    for token in shortcut.split('+') {
        let key = typeless_key_code(token).ok_or_else(|| anyhow!("unsupported Typeless shortcut key"))?;
        if keys.contains(&key) { bail!("Typeless dictation shortcut repeats a key"); }
        keys.push(key);
    }
    if keys.is_empty() || keys.len() > 8 { bail!("Typeless dictation shortcut has an invalid shape"); }
    Ok(keys)
}

#[cfg(target_os = "macos")]
fn typeless_key_code(token: &str) -> Option<u16> {
    Some(match token {
        "A" => 0x00, "S" => 0x01, "D" => 0x02, "F" => 0x03, "H" => 0x04, "G" => 0x05,
        "Z" => 0x06, "X" => 0x07, "C" => 0x08, "V" => 0x09, "Section" => 0x0a, "B" => 0x0b,
        "Q" => 0x0c, "W" => 0x0d, "E" => 0x0e, "R" => 0x0f, "Y" => 0x10, "T" => 0x11,
        "1" => 0x12, "2" => 0x13, "3" => 0x14, "4" => 0x15, "6" => 0x16, "5" => 0x17,
        "=" => 0x18, "9" => 0x19, "7" => 0x1a, "-" => 0x1b, "8" => 0x1c, "0" => 0x1d,
        "]" => 0x1e, "O" => 0x1f, "U" => 0x20, "[" => 0x21, "I" => 0x22, "P" => 0x23,
        "Enter" => 0x24, "L" => 0x25, "J" => 0x26, "'" => 0x27, "K" => 0x28, ";" => 0x29,
        "\\" | "Backslash2" => 0x2a, "," => 0x2b, "/" => 0x2c, "N" => 0x2d, "M" => 0x2e,
        "." => 0x2f, "Tab" => 0x30, "Space" => 0x31, "`" => 0x32, "Backspace" | "Delete" => 0x33,
        "Escape" => 0x35, "LeftCmd" => 0x37, "LeftShift" => 0x38, "CapsLock" => 0x39,
        "LeftOption" => 0x3a, "LeftCtrl" => 0x3b, "RightShift" => 0x3c, "RightOption" => 0x3d,
        "RightCtrl" => 0x3e, "Fn" => 0x3f, "F17" => 0x40, "NumpadDecimal" => 0x41,
        "NumpadMultiply" => 0x43, "NumpadAdd" => 0x45, "NumpadClear" => 0x47,
        "VolumeUp" => 0x48, "VolumeDown" => 0x49, "Mute" => 0x4a, "NumpadDivide" => 0x4b,
        "NumpadEnter" => 0x4c, "NumpadSubtract" => 0x4e, "F18" => 0x4f, "F19" => 0x50,
        "NumpadEquals" => 0x51, "Numpad0" => 0x52, "Numpad1" => 0x53, "Numpad2" => 0x54,
        "Numpad3" => 0x55, "Numpad4" => 0x56, "Numpad5" => 0x57, "Numpad6" => 0x58,
        "Numpad7" => 0x59, "F20" => 0x5a, "Numpad8" => 0x5b, "Numpad9" => 0x5c,
        "F5" => 0x60, "F6" => 0x61, "F7" => 0x62, "F3" => 0x63, "F8" => 0x64, "F9" => 0x65,
        "Eisu" => 0x66, "F11" => 0x67, "Kana" => 0x68, "F13" => 0x69, "F16" => 0x6a,
        "F14" => 0x6b, "F10" => 0x6d, "ContextMenu" => 0x6e, "F12" => 0x6f, "F15" => 0x71,
        "Help" => 0x72, "Home" => 0x73, "PageUp" => 0x74, "ForwardDelete" => 0x75,
        "F4" => 0x76, "End" => 0x77, "F2" => 0x78, "PageDown" => 0x79, "F1" => 0x7a,
        "Left" => 0x7b, "Right" => 0x7c, "Down" => 0x7d, "Up" => 0x7e, _ => return None,
    })
}

#[cfg(target_os = "macos")]
fn post_shortcut(keys: &[u16]) -> Result<()> {
    let mut pressed = Vec::new();
    let mut failure = None;
    for key in keys {
        // SAFETY: the bridge posts one configured Typeless key event only after
        // trust and running-app checks succeeded.
        let status = unsafe { swvoice_post_key(*key, 1) };
        if status != 0 { failure = Some(anyhow!("Typeless shortcut key-down failed ({status})")); break; }
        pressed.push(*key);
    }
    if failure.is_none() { thread::sleep(Duration::from_millis(50)); }
    for key in pressed.into_iter().rev() {
        // Always release keys that were successfully pressed, even on error.
        let status = unsafe { swvoice_post_key(key, 0) };
        if status != 0 && failure.is_none() { failure = Some(anyhow!("Typeless shortcut key-up failed ({status})")); }
    }
    match failure { Some(error) => Err(error), None => Ok(()) }
}

fn transcript_database() -> Result<PathBuf> {
    let home = std::env::var_os("HOME").ok_or_else(|| anyhow!("Typeless transcript path is unavailable"))?;
    Ok(PathBuf::from(home).join("Library/Application Support/Typeless/typeless.db"))
}

fn transcript_mark() -> Result<TranscriptCursor> {
    let path = transcript_database()?;
    let connection = rusqlite::Connection::open_with_flags(
        path,
        rusqlite::OpenFlags::SQLITE_OPEN_READ_ONLY | rusqlite::OpenFlags::SQLITE_OPEN_NO_MUTEX,
    ).context("Typeless transcript history is unavailable")?;
    let row = connection.query_row(
        "SELECT created_at, id FROM history_v2 WHERE mode='voice_transcript' AND created_at IS NOT NULL AND id IS NOT NULL ORDER BY created_at DESC, id DESC LIMIT 1",
        [],
        |row| Ok(TranscriptCursor { created_at: row.get(0)?, row_id: row.get(1)? }),
    ).optional().context("Typeless transcript history is unavailable")?;
    Ok(row.unwrap_or(TranscriptCursor { created_at: String::new(), row_id: String::new() }))
}

fn transcript_wait(cursor: &TranscriptCursor, timeout: Duration, cancelled: &AtomicBool) -> Result<String> {
    let path = transcript_database()?;
    let deadline = Instant::now() + timeout;
    loop {
        if cancelled.load(Ordering::Acquire) { bail!("Typeless transcript wait was cancelled"); }
        let connection = rusqlite::Connection::open_with_flags(
            &path,
            rusqlite::OpenFlags::SQLITE_OPEN_READ_ONLY | rusqlite::OpenFlags::SQLITE_OPEN_NO_MUTEX,
        ).context("Typeless transcript history is unavailable")?;
        let transcript = connection.query_row(
            "SELECT refined_text FROM history_v2 WHERE (created_at > ?1 OR (created_at = ?1 AND id > ?2)) AND mode='voice_transcript' AND status='completed' AND typeof(refined_text)='text' AND length(refined_text)>0 ORDER BY created_at ASC, id ASC LIMIT 1",
            rusqlite::params![cursor.created_at, cursor.row_id],
            |row| row.get::<_, String>(0),
        ).optional().context("Typeless transcript history is unavailable")?;
        if let Some(text) = transcript {
            if text.len() > MAX_TRANSCRIPT_BYTES { bail!("Typeless transcript exceeds the size limit"); }
            return Ok(text);
        }
        if cancelled.load(Ordering::Acquire) { bail!("Typeless transcript wait was cancelled"); }
        if Instant::now() >= deadline { bail!("Typeless transcript timed out"); }
        thread::sleep(TRANSCRIPT_POLL.min(deadline.saturating_duration_since(Instant::now())));
    }
}

#[cfg(target_os = "macos")]
fn package_receipt_present() -> bool {
    Command::new("/usr/sbin/pkgutil").args(["--pkg-info", BLACKHOLE_RECEIPT])
        .stdout(Stdio::null()).stderr(Stdio::null()).status().is_ok_and(|status| status.success())
}

#[cfg(not(target_os = "macos"))]
fn package_receipt_present() -> bool { false }

fn install_blackhole_driver(state_dir: &Path, cancelled: &AtomicBool) -> Result<()> {
    #[cfg(target_os = "macos")]
    {
        ensure_not_cancelled(cancelled)?;
        let cache = state_dir.join("voice-driver");
        fs::create_dir_all(&cache).context("create voice driver cache")?;
        let package = cache.join("BlackHole2ch-0.7.1.pkg");
        if !verify_sha256(&package)? {
            let temporary = cache.join(format!(".download-{}.pkg", uuid::Uuid::new_v4()));
            if let Err(error) = download_driver_package(&temporary, cancelled) {
                let _ = fs::remove_file(&temporary);
                return Err(error);
            }
            if let Err(error) = ensure_not_cancelled(cancelled) {
                let _ = fs::remove_file(&temporary);
                return Err(error);
            }
            if !verify_sha256(&temporary)? { let _ = fs::remove_file(&temporary); bail!("BlackHole driver checksum verification failed"); }
            verify_package_signature_and_id(&temporary)?;
            fs::rename(&temporary, &package).context("publish verified BlackHole package")?;
        } else {
            verify_package_signature_and_id(&package)?;
        }
        ensure_not_cancelled(cancelled)?;
        let script = "on run argv\n  set packagePath to item 1 of argv\n  do shell script \"/usr/sbin/installer -pkg \" & quoted form of packagePath & \" -target /\" with administrator privileges\nend run";
        use std::os::unix::process::CommandExt;
        let mut command = Command::new("/usr/bin/osascript");
        command.args(["-e", script, package.to_string_lossy().as_ref()])
            .stdin(Stdio::null()).stdout(Stdio::null()).stderr(Stdio::null())
            .process_group(0);
        let child = command.spawn()
            .context("start authorized BlackHole driver installer")?;
        let mut child = OsascriptProcess { child };
        wait_for_installer(&mut child, cancelled, DRIVER_INSTALL_TIMEOUT, CHILD_POLL)?;
        ensure_not_cancelled(cancelled)?;
        if !package_receipt_present() { bail!("BlackHole installer receipt verification failed"); }
        if find_device_uid(BLACKHOLE_DEVICE_NAME).is_err() { bail!("BlackHole driver installed; restart macOS to finish setup"); }
        return Ok(());
    }
    #[cfg(not(target_os = "macos"))]
    { let _ = (state_dir, cancelled); bail!("BlackHole driver installation is available only on macOS") }
}

#[cfg(target_os = "macos")]
fn download_driver_package(destination: &Path, cancelled: &AtomicBool) -> Result<()> {
    ensure_not_cancelled(cancelled)?;
    let runtime = tokio::runtime::Builder::new_current_thread()
        .enable_all()
        .build()
        .context("prepare cancellable driver download")?;
    runtime.block_on(download_driver_package_async(destination, cancelled))
}

#[cfg(target_os = "macos")]
async fn download_driver_package_async(destination: &Path, cancelled: &AtomicBool) -> Result<()> {
    let client = reqwest::Client::builder().https_only(true)
        .timeout(Duration::from_secs(120))
        .connect_timeout(Duration::from_secs(3))
        .build()
        .context("prepare verified driver download")?;
    let mut response = tokio::select! {
        _ = wait_for_driver_cancellation(cancelled) => bail!("BlackHole driver installation was cancelled"),
        response = client.get(BLACKHOLE_PACKAGE_URL).send() => {
            response.context("download BlackHole driver package")?
        }
    }
    .error_for_status()
    .context("download BlackHole driver package")?;
    if response.content_length().is_some_and(|length| length > 128 * 1024 * 1024) { bail!("BlackHole package exceeds size limit"); }
    let mut file = OpenOptions::new().write(true).create_new(true).open(destination)
        .context("create temporary BlackHole package")?;
    let mut digest = Sha256::new();
    let mut total = 0_u64;
    loop {
        let chunk = tokio::select! {
            _ = wait_for_driver_cancellation(cancelled) => bail!("BlackHole driver installation was cancelled"),
            chunk = response.chunk() => chunk.context("read BlackHole package")?,
        };
        let Some(chunk) = chunk else { break };
        total = total.checked_add(chunk.len() as u64).ok_or_else(|| anyhow!("BlackHole package size overflow"))?;
        if total > 128 * 1024 * 1024 { bail!("BlackHole package exceeds size limit"); }
        digest.update(&chunk);
        file.write_all(&chunk).context("write BlackHole package")?;
    }
    file.sync_all().context("sync verified BlackHole package")?;
    let actual = format!("{:x}", digest.finalize());
    if actual != BLACKHOLE_SHA256 { bail!("BlackHole driver checksum verification failed"); }
    Ok(())
}

#[cfg(target_os = "macos")]
async fn wait_for_driver_cancellation(cancelled: &AtomicBool) {
    while !cancelled.load(Ordering::Acquire) {
        tokio::time::sleep(Duration::from_millis(25)).await;
    }
}

fn ensure_not_cancelled(cancelled: &AtomicBool) -> Result<()> {
    if cancelled.load(Ordering::Acquire) { bail!("BlackHole driver installation was cancelled"); }
    Ok(())
}

trait InstallerProcess {
    fn try_wait_success(&mut self) -> std::io::Result<Option<bool>>;
    fn terminate(&mut self) -> std::io::Result<()>;
    fn wait_success(&mut self) -> std::io::Result<bool>;
}

#[cfg(target_os = "macos")]
struct OsascriptProcess { child: std::process::Child }

#[cfg(target_os = "macos")]
impl InstallerProcess for OsascriptProcess {
    fn try_wait_success(&mut self) -> std::io::Result<Option<bool>> {
        self.child.try_wait().map(|status| status.map(|status| status.success()))
    }

    fn terminate(&mut self) -> std::io::Result<()> {
        // osascript may have started a shell and installer child. Kill the
        // dedicated process group before reaping the osascript parent.
        let process_group = -(self.child.id() as i32);
        let result = unsafe { libc::kill(process_group, libc::SIGKILL) };
        if result == 0 { return Ok(()); }
        let error = std::io::Error::last_os_error();
        if error.raw_os_error() == Some(libc::ESRCH) { return Ok(()); }
        self.child.kill()
    }

    fn wait_success(&mut self) -> std::io::Result<bool> { self.child.wait().map(|status| status.success()) }
}

fn wait_for_installer(
    child: &mut impl InstallerProcess,
    cancelled: &AtomicBool,
    timeout: Duration,
    poll: Duration,
) -> Result<()> {
    let deadline = Instant::now() + timeout;
    loop {
        if cancelled.load(Ordering::Acquire) {
            let _ = child.terminate();
            let _ = child.wait_success();
            bail!("BlackHole driver installation was cancelled");
        }
        if let Some(success) = child.try_wait_success().context("wait for BlackHole installer")? {
            if success { return Ok(()); }
            bail!("BlackHole driver installation was cancelled or failed");
        }
        if Instant::now() >= deadline {
            let _ = child.terminate();
            let _ = child.wait_success();
            bail!("BlackHole driver installation timed out");
        }
        thread::sleep(poll.min(deadline.saturating_duration_since(Instant::now())));
    }
}

#[cfg(target_os = "macos")]
fn verify_sha256(path: &Path) -> Result<bool> {
    if !path.is_file() { return Ok(false); }
    let mut file = File::open(path).context("read cached BlackHole package")?;
    let mut digest = Sha256::new();
    let mut buffer = [0_u8; 64 * 1024];
    loop { let count = file.read(&mut buffer)?; if count == 0 { break; } digest.update(&buffer[..count]); }
    Ok(format!("{:x}", digest.finalize()) == BLACKHOLE_SHA256)
}

#[cfg(target_os = "macos")]
fn verify_package_signature_and_id(package: &Path) -> Result<()> {
    let output = Command::new("/usr/sbin/pkgutil").args(["--check-signature", package.to_string_lossy().as_ref()])
        .output().context("verify BlackHole package signature")?;
    let message = String::from_utf8_lossy(&output.stdout).to_lowercase();
    let error = String::from_utf8_lossy(&output.stderr).to_lowercase();
    let result = format!("{message}\n{error}");
    if !output.status.success() || !result.contains("signed by a certificate") || !result.contains("trusted by mac os") {
        bail!("BlackHole package signature is not trusted");
    }
    let temporary = std::env::temp_dir().join(format!("stopwatch-blackhole-{}", uuid::Uuid::new_v4()));
    let expanded = Command::new("/usr/sbin/pkgutil").args(["--expand", package.to_string_lossy().as_ref(), temporary.to_string_lossy().as_ref()])
        .stdout(Stdio::null()).stderr(Stdio::null()).status().context("inspect BlackHole package identifier")?;
    if !expanded.success() { bail!("BlackHole package metadata could not be inspected"); }
    let mut found = false;
    for entry in walk_package_info(&temporary, 6)? {
        let content = fs::read_to_string(entry).context("read BlackHole package metadata")?;
        for tag in content.split('<').filter(|part| part.starts_with("pkg-info")) {
            let tag = tag.split('>').next().unwrap_or("");
            if tag.contains("identifier=\"audio.existential.BlackHole2ch\"")
                || tag.contains("identifier='audio.existential.BlackHole2ch'") { found = true; }
        }
    }
    let _ = fs::remove_dir_all(&temporary);
    if !found { bail!("BlackHole package identifier does not match the expected driver"); }
    Ok(())
}

#[cfg(target_os = "macos")]
fn walk_package_info(root: &Path, max_depth: usize) -> Result<Vec<PathBuf>> {
    let mut result = Vec::new();
    let mut pending = vec![(root.to_path_buf(), 0_usize)];
    while let Some((directory, depth)) = pending.pop() {
        if depth > max_depth { continue; }
        for entry in fs::read_dir(directory)? {
            let entry = entry?;
            let file_type = entry.file_type()?;
            if file_type.is_file() && entry.file_name() == "PackageInfo" { result.push(entry.path()); }
            else if file_type.is_dir() && depth < max_depth { pending.push((entry.path(), depth + 1)); }
            if result.len() > 32 { bail!("BlackHole package has excessive metadata entries"); }
        }
    }
    Ok(result)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::Mutex;

    #[derive(Default)]
    struct WaitState { blocked: bool, entered: bool }

    #[derive(Default)]
    struct FakeBackend {
        events: Mutex<Vec<&'static str>>,
        transcript: Mutex<Option<String>>,
        submissions: Mutex<Vec<(String, String, InputAction, Option<String>)>>,
        fail_route_stop: AtomicBool,
        wait_state: Mutex<WaitState>,
        wait_changed: Condvar,
        submission_changed: Condvar,
        installer_state: Mutex<WaitState>,
        installer_changed: Condvar,
    }

    impl VoiceBackend for FakeBackend {
        fn readiness(&self) -> Value { json!({"state":"ready"}) }
        fn route_start(&self) -> Result<()> { self.events.lock().unwrap().push("route_start"); Ok(()) }
        fn route_write(&self, samples: &[i16]) -> Result<usize> { self.events.lock().unwrap().push("route_write"); Ok(samples.len()) }
        fn route_stop(&self) -> Result<()> {
            self.events.lock().unwrap().push("route_stop");
            if self.fail_route_stop.load(Ordering::Acquire) { bail!("injected route cleanup failure"); }
            Ok(())
        }
        fn shortcut_start(&self) -> Result<()> { self.events.lock().unwrap().push("shortcut_start"); Ok(()) }
        fn shortcut_stop(&self) -> Result<()> { self.events.lock().unwrap().push("shortcut_stop"); Ok(()) }
        fn transcript_mark(&self) -> Result<TranscriptCursor> { Ok(TranscriptCursor { created_at: "t".into(), row_id: "r".into() }) }
        fn transcript_wait(&self, _cursor: &TranscriptCursor, _timeout: Duration, cancelled: &AtomicBool) -> Result<String> {
            let mut state = self.wait_state.lock().unwrap();
            state.entered = true;
            self.wait_changed.notify_all();
            while state.blocked && !cancelled.load(Ordering::Acquire) {
                state = self.wait_changed.wait_timeout(state, Duration::from_millis(10)).unwrap().0;
            }
            if cancelled.load(Ordering::Acquire) { bail!("injected transcript wait cancellation"); }
            drop(state);
            Ok(self.transcript.lock().unwrap().clone().unwrap_or_else(|| "hello".into()))
        }
        fn submit_task(&self, task_id: &str, text: &str, action: InputAction, expected_turn: Option<&str>) -> Result<()> {
            self.events.lock().unwrap().push("task_submit");
            self.submissions.lock().unwrap().push((task_id.to_owned(), text.to_owned(), action, expected_turn.map(str::to_owned)));
            self.submission_changed.notify_all();
            Ok(())
        }
        fn install_driver(&self, _state_dir: &Path, cancelled: &AtomicBool) -> Result<()> {
            self.events.lock().unwrap().push("install_driver");
            let mut state = self.installer_state.lock().unwrap();
            state.entered = true;
            self.installer_changed.notify_all();
            while state.blocked && !cancelled.load(Ordering::Acquire) {
                state = self.installer_changed.wait_timeout(state, Duration::from_millis(10)).unwrap().0;
            }
            drop(state);
            ensure_not_cancelled(cancelled)
        }
    }

    impl FakeBackend {
        fn block_transcript_wait(&self) {
            self.wait_state.lock().unwrap().blocked = true;
        }

        fn wait_until_transcript_waiting(&self) {
            let deadline = Instant::now() + Duration::from_secs(1);
            let mut state = self.wait_state.lock().unwrap();
            while !state.entered && Instant::now() < deadline {
                state = self.wait_changed.wait_timeout(state, Duration::from_millis(10)).unwrap().0;
            }
            assert!(state.entered, "task finalizer did not enter the transcript wait");
        }

        fn wait_for_submission(&self) {
            let deadline = Instant::now() + Duration::from_secs(1);
            let mut submissions = self.submissions.lock().unwrap();
            while submissions.is_empty() && Instant::now() < deadline {
                submissions = self.submission_changed.wait_timeout(submissions, Duration::from_millis(10)).unwrap().0;
            }
        }

        fn block_installer(&self) {
            self.installer_state.lock().unwrap().blocked = true;
        }

        fn wait_until_installer_started(&self) {
            let deadline = Instant::now() + Duration::from_secs(1);
            let mut state = self.installer_state.lock().unwrap();
            while !state.entered && Instant::now() < deadline {
                state = self.installer_changed.wait_timeout(state, Duration::from_millis(10)).unwrap().0;
            }
            assert!(state.entered, "managed installer did not start");
        }
    }

    fn task() -> TaskDetail {
        TaskDetail { route: "0123456789abcdef".into(), revision: 4, source: "codex".into(), task_id: "thread-1".into(), project: "project".into(), title: "task".into(), reply: String::new() }
    }

    fn snapshot(task: &TaskDetail, state: &str, turn: Option<&str>) -> StatusSnapshot {
        let mut snapshot = StatusSnapshot::default();
        snapshot.details.insert(task.route.clone(), task.clone());
        snapshot.codex = json!({"codex_alerts":[{"route":task.route,"thread_id":task.task_id,"state":state,"turn_id":turn}]});
        snapshot
    }

    #[test]
    fn pinned_microphone_never_toggles_typeless_and_stops_route_once() {
        let backend = Arc::new(FakeBackend::default());
        let controller = VoiceController::with_backend(backend.clone());
        controller.start(VoiceMode::PinnedMicrophone, None).unwrap();
        controller.pcm(&[1, 2, 3]).unwrap();
        controller.stop(false).unwrap();
        controller.stop(false).unwrap();
        assert_eq!(*backend.events.lock().unwrap(), ["route_start", "route_write", "route_stop"]);
    }

    #[test]
    fn push_to_talk_triggers_typeless_and_pairs_exactly_one_stop() {
        let backend = Arc::new(FakeBackend::default());
        let controller = VoiceController::with_backend(backend.clone());
        controller.start(VoiceMode::PushToTalk, None).unwrap();
        controller.stop(false).unwrap();
        assert_eq!(*backend.events.lock().unwrap(), ["route_start", "shortcut_start", "shortcut_stop", "route_stop"]);
    }

    #[test]
    fn task_voice_checks_route_and_revision_before_recording_and_submit() {
        let backend = Arc::new(FakeBackend::default());
        let controller = VoiceController::with_backend(backend.clone());
        let task = task();
        controller.update_snapshot(&snapshot(&task, "running", Some("turn-1")));
        controller.start(VoiceMode::TaskConversation, Some(task.clone())).unwrap();
        controller.update_snapshot(&snapshot(&task, "running", Some("turn-2")));
        controller.stop(false).unwrap();
        wait_for_finalizer(&controller);
        let events = backend.events.lock().unwrap();
        assert!(events.contains(&"shortcut_stop"));
        assert!(!events.contains(&"task_submit"));
    }

    #[test]
    fn task_voice_uses_fresh_matching_route_once_after_transcript() {
        let backend = Arc::new(FakeBackend::default());
        let controller = VoiceController::with_backend(backend.clone());
        let task = task();
        controller.update_snapshot(&snapshot(&task, "pending", None));
        controller.start(VoiceMode::TaskConversation, Some(task.clone())).unwrap();
        controller.stop(false).unwrap();
        backend.wait_for_submission();
        controller.stop(false).unwrap();
        assert_eq!(backend.events.lock().unwrap().iter().filter(|event| **event == "task_submit").count(), 1);
        assert_eq!(*backend.submissions.lock().unwrap(), [("thread-1".to_owned(), "hello".to_owned(), InputAction::Submit, None)]);
    }

    #[test]
    fn task_route_requires_exact_revision_and_source() {
        let backend = Arc::new(FakeBackend::default());
        let controller = VoiceController::with_backend(backend.clone());
        let task = task();
        controller.update_snapshot(&snapshot(&task, "completed", None));
        let mut stale_revision = task.clone(); stale_revision.revision += 1;
        let mut stale_source = task.clone(); stale_source.source = "trae".into();
        let mut stale_task = task.clone(); stale_task.task_id = "thread-2".into();
        let mut stale_route = task.clone(); stale_route.route = "fedcba9876543210".into();
        for stale in [stale_revision, stale_source, stale_task, stale_route] {
            assert!(controller.start(VoiceMode::TaskConversation, Some(stale)).is_err());
        }
        assert!(backend.events.lock().unwrap().is_empty());
    }

    #[test]
    fn task_voice_stop_returns_without_waiting_and_cancel_suppresses_submission() {
        let backend = Arc::new(FakeBackend::default());
        backend.block_transcript_wait();
        let controller = VoiceController::with_backend(backend.clone());
        let task = task();
        controller.update_snapshot(&snapshot(&task, "pending", None));
        controller.start(VoiceMode::TaskConversation, Some(task)).unwrap();
        let started = Instant::now();
        controller.stop(false).unwrap();
        assert!(started.elapsed() < Duration::from_secs(1));
        backend.wait_until_transcript_waiting();
        controller.stop(true).unwrap();
        assert!(backend.submissions.lock().unwrap().is_empty());
    }

    #[test]
    fn task_voice_cancellation_retries_idle_route_cleanup() {
        let backend = Arc::new(FakeBackend::default());
        let controller = VoiceController::with_backend(backend.clone());
        controller.start(VoiceMode::PinnedMicrophone, None).unwrap();
        backend.fail_route_stop.store(true, Ordering::Release);
        assert!(controller.stop(true).is_err());
        backend.fail_route_stop.store(false, Ordering::Release);
        controller.stop(true).unwrap();
        assert_eq!(backend.events.lock().unwrap().iter().filter(|event| **event == "route_stop").count(), 2);
    }

    #[test]
    fn task_voice_state_transition_preserves_action_and_turn_rules() {
        let mut expected = TaskBinding {
            route: "0123456789abcdef".into(), revision: 4, source: "codex".into(),
            task_id: "thread-1".into(), state: "pending".into(), turn_id: None,
        };
        let mut current = expected.clone();
        current.revision = 5; // Display/title revision changes do not invalidate the route.
        assert_eq!(task_submission_for(&HashMap::from([(current.route.clone(), current.clone())]), &expected).unwrap(), (InputAction::Submit, None));

        current.state = "completed".into();
        assert_eq!(task_submission_for(&HashMap::from([(current.route.clone(), current.clone())]), &expected).unwrap(), (InputAction::Submit, None));

        expected.state = "running".into();
        expected.turn_id = Some("turn-1".into());
        current.state = "running".into();
        current.turn_id = Some("turn-1".into());
        current.revision = 6;
        assert_eq!(task_submission_for(&HashMap::from([(current.route.clone(), current.clone())]), &expected).unwrap(), (InputAction::ForceSteer, Some("turn-1".into())));

        current.turn_id = Some("turn-2".into());
        assert!(task_submission_for(&HashMap::from([(current.route.clone(), current.clone())]), &expected).is_err());
        expected.state = "pending".into();
        expected.turn_id = None;
        assert!(task_submission_for(&HashMap::from([(current.route.clone(), current)]), &expected).is_err());

        let mut switched = expected.clone();
        switched.task_id = "thread-2".into();
        assert!(task_submission_for(&HashMap::from([(switched.route.clone(), switched)]), &expected).is_err());
    }

    #[test]
    fn stop_cancels_and_joins_managed_driver_worker() {
        let backend = Arc::new(FakeBackend::default());
        backend.block_installer();
        let controller = Arc::new(VoiceController::with_backend(backend.clone()));
        let worker_controller = controller.clone();
        let installer = thread::spawn(move || worker_controller.install_driver(Path::new("fake-state")));
        backend.wait_until_installer_started();
        controller.stop(true).unwrap();
        assert!(installer.join().unwrap().is_err());
        assert!(backend.events.lock().unwrap().contains(&"install_driver"));
    }

    fn wait_for_finalizer(controller: &VoiceController) {
        if let Some(job) = controller.finalizer.lock().unwrap().take() {
            job.thread.join().unwrap();
        }
    }

    struct FakeProcess {
        polls_before_exit: Option<usize>,
        killed: bool,
        reaped: bool,
    }

    impl InstallerProcess for FakeProcess {
        fn try_wait_success(&mut self) -> std::io::Result<Option<bool>> {
            if self.killed { return Ok(Some(false)); }
            if let Some(remaining) = self.polls_before_exit.as_mut() {
                if *remaining == 0 { return Ok(Some(true)); }
                *remaining -= 1;
            }
            Ok(None)
        }
        fn terminate(&mut self) -> std::io::Result<()> { self.killed = true; Ok(()) }
        fn wait_success(&mut self) -> std::io::Result<bool> { self.reaped = true; Ok(false) }
    }

    #[test]
    fn installer_wait_reaps_fake_child_on_cancel_and_deadline() {
        let cancelled = AtomicBool::new(true);
        let mut child = FakeProcess { polls_before_exit: None, killed: false, reaped: false };
        assert!(wait_for_installer(&mut child, &cancelled, Duration::from_secs(1), Duration::from_millis(1)).is_err());
        assert!(child.killed && child.reaped);

        let cancelled = AtomicBool::new(false);
        let mut child = FakeProcess { polls_before_exit: None, killed: false, reaped: false };
        assert!(wait_for_installer(&mut child, &cancelled, Duration::ZERO, Duration::from_millis(1)).is_err());
        assert!(child.killed && child.reaped);
    }
}

