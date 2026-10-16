//! Content-free checkpoints for tracing collection-to-BLE freshness.
use crate::contracts::StatusSnapshot;
use serde_json::Value;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

#[derive(Default)]
pub(crate) struct StatusSyncLog {
    last: Option<Value>,
    logged_at: Option<Instant>,
}

impl StatusSyncLog {
    pub(crate) fn record(&mut self, phase: &str, snapshot: &StatusSnapshot, elapsed: Duration) {
        let usage = &snapshot.telemetry["usage"];
        let summary = serde_json::json!({
            "revision": snapshot.codex["codex_alert_revision"],
            "pending": snapshot.codex["codex_alert_pending"],
            "running": snapshot.codex["codex_alert_running"],
            "completed": snapshot.codex["codex_alert_completed"],
            "usage_state": usage["s"],
            "usage_at": usage["at"],
            "tokens": usage["t"],
            "cost_estimate": usage["c"],
            "model_count": usage["d"]["m"].as_array().map(Vec::len),
        });
        if self
            .logged_at
            .is_some_and(|at| at.elapsed() < Duration::from_secs(10))
        {
            return;
        }
        if self.last.as_ref() == Some(&summary)
            && self
                .logged_at
                .is_some_and(|at| at.elapsed() < Duration::from_secs(60))
        {
            return;
        }
        let now = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap_or_default()
            .as_secs();
        let usage_age_seconds = usage["at"]
            .as_u64()
            .filter(|at| *at > 0)
            .map(|at| now.saturating_sub(at));
        // Deliberately excludes task IDs, projects, titles, replies and credentials.
        // A sent checkpoint means GATT acknowledged writes, not that the watch rendered them.
        tracing::info!(phase, summary = %summary, ?usage_age_seconds,
            elapsed_ms = elapsed.as_millis() as u64, "Status synchronization checkpoint");
        self.last = Some(summary);
        self.logged_at = Some(Instant::now());
    }
}
