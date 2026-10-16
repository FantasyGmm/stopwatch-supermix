//! Fail-closed macOS Codex Accessibility submission.
//!
//! The Objective-C++ shim owns AX objects and performs the final revalidation
//! and dispatch. This Rust layer owns policy and can be exercised with a fake
//! backend without ever posting a real keyboard event.

use anyhow::{Result, anyhow};
use std::{
    path::Path,
    sync::{Arc, Mutex, atomic::{AtomicBool, Ordering}},
    thread,
    time::{Duration, Instant},
};

use crate::contracts::InputAction;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SubmitSemantic {
    Unknown,
    Send,
    Queue,
    Steer,
    Running,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ComposerSnapshot {
    pub codex_foreground: bool,
    pub editable: bool,
    pub has_text: bool,
    pub semantic: SubmitSemantic,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Dispatch {
    PressVerifiedButton,
    TargetedCommandReturn { shift: bool },
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum ComposerEnterBehavior {
    Enter,
    CmdIfMultiline,
    CmdAlways,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum FollowUpQueueMode {
    Queue,
    Steer,
    Interrupt,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct ComposerPreferences {
    enter_behavior: Option<ComposerEnterBehavior>,
    follow_up_queue_mode: Option<FollowUpQueueMode>,
}

impl ComposerPreferences {
    fn from_value(value: &serde_json::Value) -> Self {
        let enter_behavior = match value.get("composer_enter_behavior").and_then(serde_json::Value::as_str) {
            Some("enter") => Some(ComposerEnterBehavior::Enter),
            Some("cmdIfMultiline") => Some(ComposerEnterBehavior::CmdIfMultiline),
            Some("cmdAlways") => Some(ComposerEnterBehavior::CmdAlways),
            _ => None,
        };
        let follow_up_queue_mode = match value.get("follow_up_queue_mode").and_then(serde_json::Value::as_str) {
            Some("queue") => Some(FollowUpQueueMode::Queue),
            Some("steer") => Some(FollowUpQueueMode::Steer),
            Some("interrupt") => Some(FollowUpQueueMode::Interrupt),
            _ => None,
        };
        Self { enter_behavior, follow_up_queue_mode }
    }
}

trait PreferenceSource: Send + Sync {
    fn read(&self) -> serde_json::Value;
}

struct ProductPreferences { codex_root: std::path::PathBuf }

impl PreferenceSource for ProductPreferences {
    fn read(&self) -> serde_json::Value {
        crate::product_prefs::read(&self.codex_root)
    }
}

struct NoProductPreferences;

impl PreferenceSource for NoProductPreferences {
    fn read(&self) -> serde_json::Value { serde_json::json!({}) }
}

/// Test seam for Accessibility inspection and the final targeted action.
/// Production's implementation keeps the AX elements in the native process;
/// callers receive only a single-use opaque handle.
trait AccessibilityBackend: Send + Sync {
    fn prepare(&self) -> Result<Option<(ComposerSnapshot, u64)>>;
    fn dispatch(&self, handle: u64, dispatch: Dispatch) -> Result<()>;
    fn discard(&self, handle: u64);
}

pub struct CodexSubmitController {
    backend: Arc<dyn AccessibilityBackend>,
    preferences: Arc<dyn PreferenceSource>,
}

impl CodexSubmitController {
    pub fn new() -> Self {
        Self {
            backend: Arc::new(MacOSAccessibility),
            preferences: Arc::new(NoProductPreferences),
        }
    }

    pub fn with_codex_root(codex_root: std::path::PathBuf) -> Self {
        Self {
            backend: Arc::new(MacOSAccessibility),
            preferences: Arc::new(ProductPreferences { codex_root }),
        }
    }

    fn with_backend(backend: Arc<dyn AccessibilityBackend>, preferences: Arc<dyn PreferenceSource>) -> Self {
        Self { backend, preferences }
    }

    /// Returns true only when one prepared composer was dispatched once.
    pub fn handle(&self, action: InputAction) -> bool {
        // Read once before inspection and again immediately before final native
        // revalidation. A configuration change during preparation fails closed
        // for the alternate Queue-to-Steer shortcut.
        let before = ComposerPreferences::from_value(&self.preferences.read());
        let Ok(Some((snapshot, handle))) = self.backend.prepare() else {
            return false;
        };
        let current = ComposerPreferences::from_value(&self.preferences.read());
        let dispatch = match dispatch_for(snapshot, action, before, current) {
            Some(dispatch) => dispatch,
            None => {
                self.backend.discard(handle);
                return false;
            }
        };
        self.backend.dispatch(handle, dispatch).is_ok()
    }
}

impl Default for CodexSubmitController {
    fn default() -> Self { Self::new() }
}

fn dispatch_for(
    snapshot: ComposerSnapshot,
    action: InputAction,
    before: ComposerPreferences,
    current: ComposerPreferences,
) -> Option<Dispatch> {
    if !snapshot.codex_foreground || !snapshot.editable {
        return None;
    }
    if !snapshot.has_text
        && !(action == InputAction::ForceSteer
            && snapshot.semantic == SubmitSemantic::Running)
    {
        return None;
    }

    match (action, snapshot.semantic) {
        (InputAction::Submit, SubmitSemantic::Send | SubmitSemantic::Queue | SubmitSemantic::Steer)
        | (InputAction::ForceSteer, SubmitSemantic::Send | SubmitSemantic::Steer) => {
            Some(Dispatch::PressVerifiedButton)
        }
        (InputAction::ForceSteer, SubmitSemantic::Queue | SubmitSemantic::Running) => {
            if before != current
                || current.follow_up_queue_mode != Some(FollowUpQueueMode::Queue)
            {
                return None;
            }
            let shift = match current.enter_behavior? {
                ComposerEnterBehavior::Enter => false,
                ComposerEnterBehavior::CmdIfMultiline | ComposerEnterBehavior::CmdAlways => true,
            };
            Some(Dispatch::TargetedCommandReturn { shift })
        }
        _ => None,
    }
}

#[repr(C)]
#[derive(Default)]
struct NativeSnapshot {
    codex_foreground: i32,
    accessibility_trusted: i32,
    editable: i32,
    has_text: i32,
    semantic: i32,
    handle: u64,
}

#[cfg(target_os = "macos")]
unsafe extern "C" {
    fn swax_prepare(output: *mut NativeSnapshot) -> i32;
    fn swax_dispatch(handle: u64, alternate: i32) -> i32;
    fn swax_discard(handle: u64);
    fn swax_restore_value(handle: u64) -> i32;
    fn swax_open_task(thread_id: *const std::ffi::c_char) -> i32;
    fn swax_set_value(handle: u64, value: *const std::ffi::c_char, output: *mut NativeSnapshot) -> i32;
    fn swax_accessibility_trusted() -> i32;
}

pub fn accessibility_trusted() -> bool {
    #[cfg(target_os = "macos")]
    {
        // SAFETY: the native query is read-only and takes no pointers.
        unsafe { swax_accessibility_trusted() == 1 }
    }
    #[cfg(not(target_os = "macos"))]
    { false }
}

/// Open the route-bound Codex task, then submit into its focused composer only
/// after checking the composer is empty and revalidating its AX identity.
#[cfg(target_os = "macos")]
pub fn submit_task_to_codex(
    codex_root: &Path,
    thread_id: &str,
    text: &str,
    action: InputAction,
    expected_turn: Option<&str>,
) -> Result<()> {
    static NEVER_CANCELLED: AtomicBool = AtomicBool::new(false);
    let commit_gate = Mutex::new(());
    let validate_route = || Ok(());
    submit_task_to_codex_cancellable(
        codex_root,
        thread_id,
        text,
        action,
        expected_turn,
        &NEVER_CANCELLED,
        &commit_gate,
        &validate_route,
    )
}

#[cfg(target_os = "macos")]
pub fn submit_task_to_codex_cancellable(
    codex_root: &Path,
    thread_id: &str,
    text: &str,
    action: InputAction,
    expected_turn: Option<&str>,
    cancelled: &AtomicBool,
    commit_gate: &Mutex<()>,
    validate_route: &dyn Fn() -> Result<()>,
) -> Result<()> {
    if thread_id.is_empty() || thread_id.len() > 256 || thread_id.chars().any(char::is_control) {
        anyhow::bail!("Codex task identifier is invalid");
    }
    if text.is_empty() || text.len() > 64 * 1024 || text.contains('\0') {
        anyhow::bail!("Codex task transcript is invalid");
    }
    match action {
        InputAction::Submit if expected_turn.is_none() => {}
        InputAction::ForceSteer if expected_turn.is_some_and(|turn| !turn.is_empty()) => {}
        _ => anyhow::bail!("Codex task voice state or turn binding is invalid"),
    }
    if cancelled.load(Ordering::Acquire) { return Ok(()); }
    validate_route()?;
    let before = ComposerPreferences::from_value(&crate::product_prefs::read(codex_root));
    let thread_id = std::ffi::CString::new(thread_id)?;
    let text = std::ffi::CString::new(text)?;
    if cancelled.load(Ordering::Acquire) { return Ok(()); }
    validate_route()?;
    // SAFETY: the validated thread identifier remains NUL-terminated for the
    // synchronous native deep-link request.
    let status = unsafe { swax_open_task(thread_id.as_ptr()) };
    if status != 0 { anyhow::bail!("Codex task could not be opened ({status})"); }

    let deadline = Instant::now() + Duration::from_secs(2);
    let prepared = loop {
        if cancelled.load(Ordering::Acquire) { return Ok(()); }
        validate_route()?;
        let mut native = NativeSnapshot::default();
        // SAFETY: `native` is a live C-compatible output struct.
        let status = unsafe { swax_prepare(&mut native) };
        if status != 0 { anyhow::bail!("Codex task composer preparation failed ({status})"); }
        if cancelled.load(Ordering::Acquire) {
            if native.handle != 0 { unsafe { swax_discard(native.handle) }; }
            return Ok(());
        }
        if native.handle != 0 {
            if native.has_text == 1 {
                // SAFETY: this unconsumed handle was returned by swax_prepare.
                unsafe { swax_discard(native.handle) };
                anyhow::bail!("Codex task composer already contains a draft");
            }
            break native;
        }
        if native.codex_foreground == 1 && native.accessibility_trusted == 0 {
            anyhow::bail!("Accessibility permission is required for Codex task voice");
        }
        if Instant::now() >= deadline {
            anyhow::bail!("Codex task composer did not become ready");
        }
        thread::sleep(Duration::from_millis(50));
    };

    // Navigation and AX preparation stay outside the commit lock. Cancellation
    // invalidates the token before it can wait for this short set/dispatch pair.
    let _commit = commit_gate.lock().map_err(|_| anyhow!("task voice completion state is unavailable"))?;
    if cancelled.load(Ordering::Acquire) {
        unsafe { swax_discard(prepared.handle) };
        return Ok(());
    }
    if let Err(error) = validate_route() {
        unsafe { swax_discard(prepared.handle) };
        return Err(error);
    }
    if cancelled.load(Ordering::Acquire) {
        unsafe { swax_discard(prepared.handle) };
        return Ok(());
    }

    let mut filled = NativeSnapshot::default();
    // The native setter consumes the prepared handle, checks that the same
    // foreground composer is still empty, writes once, and returns a new
    // single-use handle for the final revalidation.
    let status = unsafe { swax_set_value(prepared.handle, text.as_ptr(), &mut filled) };
    if status != 0 { anyhow::bail!("Codex task composer rejected the transcript ({status})"); }

    if cancelled.load(Ordering::Acquire) {
        let _ = unsafe { swax_restore_value(filled.handle) };
        return Ok(());
    }
    if let Err(error) = validate_route() {
        let _ = unsafe { swax_restore_value(filled.handle) };
        return Err(error);
    }
    let current = ComposerPreferences::from_value(&crate::product_prefs::read(codex_root));
    let snapshot = composer_snapshot(&filled);
    let dispatch = match dispatch_for(snapshot, action, before, current) {
        Some(dispatch) => dispatch,
        None => {
            // SAFETY: the filled handle remains pending and has not been used.
            // The native side restores only if this exact composer still holds
            // the transcript we wrote; any later user edit is left untouched.
            let _ = unsafe { swax_restore_value(filled.handle) };
            anyhow::bail!("Codex task action or composer preferences changed before dispatch");
        }
    };
    if cancelled.load(Ordering::Acquire) {
        let _ = unsafe { swax_restore_value(filled.handle) };
        return Ok(());
    }
    if let Err(error) = validate_route() {
        let _ = unsafe { swax_restore_value(filled.handle) };
        return Err(error);
    }
    if cancelled.load(Ordering::Acquire) {
        let _ = unsafe { swax_restore_value(filled.handle) };
        return Ok(());
    }
    MacOSAccessibility.dispatch(filled.handle, dispatch)
}

#[cfg(not(target_os = "macos"))]
pub fn submit_task_to_codex(
    _codex_root: &Path,
    _thread_id: &str,
    _text: &str,
    _action: InputAction,
    _expected_turn: Option<&str>,
) -> Result<()> {
    anyhow::bail!("Codex task submission is available only on macOS")
}

#[cfg(not(target_os = "macos"))]
pub fn submit_task_to_codex_cancellable(
    _codex_root: &Path,
    _thread_id: &str,
    _text: &str,
    _action: InputAction,
    _expected_turn: Option<&str>,
    _cancelled: &AtomicBool,
    _commit_gate: &Mutex<()>,
    _validate_route: &dyn Fn() -> Result<()>,
) -> Result<()> {
    anyhow::bail!("Codex task submission is available only on macOS")
}

fn composer_snapshot(native: &NativeSnapshot) -> ComposerSnapshot {
    ComposerSnapshot {
        codex_foreground: native.codex_foreground == 1,
        editable: native.accessibility_trusted == 1 && native.editable == 1,
        has_text: native.has_text == 1,
        semantic: match native.semantic {
            1 => SubmitSemantic::Send,
            2 => SubmitSemantic::Queue,
            3 => SubmitSemantic::Steer,
            4 => SubmitSemantic::Running,
            _ => SubmitSemantic::Unknown,
        },
    }
}

struct MacOSAccessibility;

impl AccessibilityBackend for MacOSAccessibility {
    fn prepare(&self) -> Result<Option<(ComposerSnapshot, u64)>> {
        #[cfg(target_os = "macos")]
        {
            let mut native = NativeSnapshot::default();
            // SAFETY: output points to a live, correctly sized C-compatible struct.
            let status = unsafe { swax_prepare(&mut native) };
            if status != 0 {
                return Err(anyhow!("Accessibility preparation failed ({status})"));
            }
            let snapshot = ComposerSnapshot {
                codex_foreground: native.codex_foreground == 1,
                editable: native.accessibility_trusted == 1 && native.editable == 1,
                has_text: native.has_text == 1,
                semantic: match native.semantic {
                    1 => SubmitSemantic::Send,
                    2 => SubmitSemantic::Queue,
                    3 => SubmitSemantic::Steer,
                    4 => SubmitSemantic::Running,
                    _ => SubmitSemantic::Unknown,
                },
            };
            return Ok((native.handle != 0).then_some((snapshot, native.handle)));
        }
        #[cfg(not(target_os = "macos"))]
        {
            Ok(None)
        }
    }

    fn dispatch(&self, handle: u64, dispatch: Dispatch) -> Result<()> {
        #[cfg(target_os = "macos")]
        {
            let alternate = match dispatch {
                Dispatch::PressVerifiedButton => 0,
                Dispatch::TargetedCommandReturn { shift: false } => 1,
                Dispatch::TargetedCommandReturn { shift: true } => 2,
            };
            // SAFETY: `handle` was issued by swax_prepare and is single-use.
            let status = unsafe { swax_dispatch(handle, alternate) };
            if status == 0 {
                Ok(())
            } else {
                Err(anyhow!("targeted Codex Accessibility dispatch failed ({status})"))
            }
        }
        #[cfg(not(target_os = "macos"))]
        {
            let _ = (handle, dispatch);
            Err(anyhow!("Codex Accessibility is available only on macOS"))
        }
    }

    fn discard(&self, handle: u64) {
        #[cfg(target_os = "macos")]
        // SAFETY: discarding an unknown or already-consumed handle is a no-op.
        unsafe { swax_discard(handle) }
        #[cfg(not(target_os = "macos"))]
        let _ = handle;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;
    use std::sync::{Mutex, atomic::{AtomicUsize, Ordering}};

    struct FixedPreferences(serde_json::Value);

    impl PreferenceSource for FixedPreferences {
        fn read(&self) -> serde_json::Value { self.0.clone() }
    }

    struct SequencedPreferences {
        values: Vec<serde_json::Value>,
        reads: AtomicUsize,
    }

    impl PreferenceSource for SequencedPreferences {
        fn read(&self) -> serde_json::Value {
            let index = self.reads.fetch_add(1, Ordering::Relaxed);
            self.values[index.min(self.values.len() - 1)].clone()
        }
    }

    fn preferences(enter: &str, follow_up: &str) -> ComposerPreferences {
        ComposerPreferences::from_value(&json!({
            "composer_enter_behavior": enter,
            "follow_up_queue_mode": follow_up,
        }))
    }

    #[derive(Default)]
    struct FakeBackend {
        current: Mutex<Option<ComposerSnapshot>>,
        prepared: Mutex<Vec<u64>>,
        dispatched: Mutex<Vec<(u64, Dispatch)>>,
        discarded: Mutex<Vec<u64>>,
        dispatch_error: Mutex<bool>,
    }

    impl AccessibilityBackend for FakeBackend {
        fn prepare(&self) -> Result<Option<(ComposerSnapshot, u64)>> {
            let Some(snapshot) = *self.current.lock().unwrap() else { return Ok(None) };
            let mut handles = self.prepared.lock().unwrap();
            let handle = handles.len() as u64 + 1;
            handles.push(handle);
            Ok(Some((snapshot, handle)))
        }

        fn dispatch(&self, handle: u64, dispatch: Dispatch) -> Result<()> {
            self.dispatched.lock().unwrap().push((handle, dispatch));
            if *self.dispatch_error.lock().unwrap() {
                Err(anyhow!("injected dispatch failure"))
            } else {
                Ok(())
            }
        }

        fn discard(&self, handle: u64) { self.discarded.lock().unwrap().push(handle); }
    }

    fn snapshot(semantic: SubmitSemantic, has_text: bool) -> ComposerSnapshot {
        ComposerSnapshot { codex_foreground: true, editable: true, has_text, semantic }
    }

    fn default_preferences() -> ComposerPreferences { preferences("enter", "queue") }

    #[test]
    fn submit_and_force_steer_match_existing_semantics() {
        for semantic in [SubmitSemantic::Send, SubmitSemantic::Queue, SubmitSemantic::Steer] {
            assert_eq!(dispatch_for(snapshot(semantic, true), InputAction::Submit, default_preferences(), default_preferences()), Some(Dispatch::PressVerifiedButton));
        }
        for (semantic, expected) in [
            (SubmitSemantic::Send, Dispatch::PressVerifiedButton),
            (SubmitSemantic::Steer, Dispatch::PressVerifiedButton),
            (SubmitSemantic::Queue, Dispatch::TargetedCommandReturn { shift: false }),
            (SubmitSemantic::Running, Dispatch::TargetedCommandReturn { shift: false }),
        ] {
            assert_eq!(dispatch_for(snapshot(semantic, true), InputAction::ForceSteer, default_preferences(), default_preferences()), Some(expected));
        }
    }

    #[test]
    fn empty_running_composer_is_only_valid_for_force_steer() {
        let current = snapshot(SubmitSemantic::Running, false);
        assert_eq!(dispatch_for(current, InputAction::Submit, default_preferences(), default_preferences()), None);
        assert_eq!(dispatch_for(current, InputAction::ForceSteer, default_preferences(), default_preferences()), Some(Dispatch::TargetedCommandReturn { shift: false }));
    }

    #[test]
    fn synthetic_ax_draft_value_is_authoritative_over_placeholder_evidence() {
        // The native adapter reports `has_text` from AXValue whenever that
        // attribute exists. A placeholder child cannot make a nonempty value
        // appear empty; an empty fallback remains ineligible for normal send.
        assert_eq!(
            dispatch_for(snapshot(SubmitSemantic::Send, true), InputAction::Submit, default_preferences(), default_preferences()),
            Some(Dispatch::PressVerifiedButton),
        );
        assert_eq!(
            dispatch_for(snapshot(SubmitSemantic::Send, false), InputAction::Submit, default_preferences(), default_preferences()),
            None,
        );
    }

    #[test]
    fn unknown_background_and_noneditable_states_fail_closed() {
        for current in [
            ComposerSnapshot { codex_foreground: false, ..snapshot(SubmitSemantic::Send, true) },
            ComposerSnapshot { editable: false, ..snapshot(SubmitSemantic::Send, true) },
            snapshot(SubmitSemantic::Unknown, true),
        ] {
            assert_eq!(dispatch_for(current, InputAction::Submit, default_preferences(), default_preferences()), None);
        }
    }

    #[test]
    fn controller_dispatches_only_the_policy_selected_action() {
        let backend = Arc::new(FakeBackend::default());
        *backend.current.lock().unwrap() = Some(snapshot(SubmitSemantic::Queue, true));
        let controller = CodexSubmitController::with_backend(backend.clone(), Arc::new(FixedPreferences(json!({
            "composer_enter_behavior":"enter", "follow_up_queue_mode":"queue"
        }))));
        assert!(controller.handle(InputAction::ForceSteer));
        assert_eq!(
            *backend.dispatched.lock().unwrap(),
            vec![(1, Dispatch::TargetedCommandReturn { shift: false })]
        );
    }

    #[test]
    fn failed_preparation_and_dispatch_are_never_reported_as_accepted() {
        let backend = Arc::new(FakeBackend::default());
        let controller = CodexSubmitController::with_backend(backend.clone(), Arc::new(FixedPreferences(json!({
            "composer_enter_behavior":"enter", "follow_up_queue_mode":"queue"
        }))));
        assert!(!controller.handle(InputAction::Submit));
        *backend.current.lock().unwrap() = Some(snapshot(SubmitSemantic::Send, true));
        *backend.dispatch_error.lock().unwrap() = true;
        assert!(!controller.handle(InputAction::Submit));
        assert_eq!(backend.dispatched.lock().unwrap().len(), 1);
    }

    #[test]
    fn queue_force_steer_uses_the_opposite_shortcut_only_with_current_known_preferences() {
        let known_multiline = preferences("cmdAlways", "queue");
        assert_eq!(dispatch_for(snapshot(SubmitSemantic::Queue, true), InputAction::ForceSteer, known_multiline, known_multiline), Some(Dispatch::TargetedCommandReturn { shift: true }));
        let unknown = ComposerPreferences { enter_behavior: None, follow_up_queue_mode: Some(FollowUpQueueMode::Queue) };
        assert_eq!(dispatch_for(snapshot(SubmitSemantic::Queue, true), InputAction::ForceSteer, unknown, unknown), None);
        let changed = preferences("enter", "queue");
        assert_eq!(dispatch_for(snapshot(SubmitSemantic::Queue, true), InputAction::ForceSteer, changed, preferences("cmdAlways", "queue")), None);
        let unknown_but_normal = ComposerPreferences { enter_behavior: None, follow_up_queue_mode: None };
        assert_eq!(dispatch_for(snapshot(SubmitSemantic::Steer, true), InputAction::ForceSteer, unknown_but_normal, unknown_but_normal), Some(Dispatch::PressVerifiedButton));
    }

    #[test]
    fn queue_force_steer_rechecks_preferences_after_preparing_the_composer() {
        let backend = Arc::new(FakeBackend::default());
        *backend.current.lock().unwrap() = Some(snapshot(SubmitSemantic::Queue, true));
        let preferences = Arc::new(SequencedPreferences {
            values: vec![
                json!({"composer_enter_behavior":"enter","follow_up_queue_mode":"queue"}),
                json!({"composer_enter_behavior":"cmdAlways","follow_up_queue_mode":"queue"}),
            ],
            reads: AtomicUsize::new(0),
        });
        let controller = CodexSubmitController::with_backend(backend.clone(), preferences);

        assert!(!controller.handle(InputAction::ForceSteer));
        assert!(backend.dispatched.lock().unwrap().is_empty());
        assert_eq!(*backend.discarded.lock().unwrap(), [1]);
    }
}
