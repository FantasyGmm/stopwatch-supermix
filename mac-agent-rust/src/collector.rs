//! Read-only Codex/Trae collection and durable task-alert reduction.
//!
//! This module mirrors the former Python collector's status keys. Long-running
//! or network-dependent telemetry is delegated to `telemetry::TelemetrySampler`;
//! `poll` only reads local task state and cached telemetry.

use crate::{
    contracts::{AgentPaths, MAX_TASK_DETAIL_BYTES, StatusSnapshot, TaskDetail},
    pricing::PricingCatalog,
    telemetry::TelemetrySampler,
};
use anyhow::{Context, Result};
use chrono::{DateTime, Local, Utc};
use regex::Regex;
use rusqlite::{Connection, OpenFlags, OptionalExtension, params};
use serde::de::Deserializer;
use serde::{Deserialize, Serialize};
use serde_json::{Map, Value, json};
use sha2::{Digest, Sha256};
use std::{
    collections::{BTreeMap, HashMap, HashSet},
    fs::{self, File},
    io::{BufRead, BufReader, Read, Seek, SeekFrom, Write},
    path::{Path, PathBuf},
    process::{Command, Stdio},
    sync::{
        Arc, LazyLock,
        atomic::{AtomicBool, Ordering},
        mpsc,
    },
    thread::{self, JoinHandle},
    time::{Duration, UNIX_EPOCH},
};

const ALERT_LEDGER_VERSION: u8 = 3;
const CLASSIFIER_VERSION: u8 = 4;
const REPLY_METADATA_VERSION: u8 = 2;
const ABORT_REDUCER_VERSION: u8 = 1;
const ALERT_RETENTION_DAYS: i64 = 7;
const MAX_DISCOVERED_THREADS: usize = 50;
const ALERT_READ_CHUNK: u64 = 2 * 1024 * 1024;
const MAX_REPLAY_LINE_BYTES: usize = 16 * 1024 * 1024;
const TRAE_MAX_LOG_FILES: usize = 128;
const TRAE_MAX_TASKS: usize = 32;
const TRAE_TITLE_SCAN_BYTES_PER_POLL: u64 = 2 * 1024 * 1024;
const APP_SERVER_TIMEOUT: Duration = Duration::from_secs(2);
const APP_SERVER_REFRESH: Duration = Duration::from_secs(30);
const APP_SERVER_THREAD_LIMIT: usize = 200;
const MAX_APP_SERVER_LINE_BYTES: usize = 128 * 1024;
const MAX_LEGACY_LEDGER_BYTES: u64 = 4 * 1024 * 1024;
const CLASSIFIER_STREAM_THRESHOLD_BYTES: usize = 64 * 1024;
const CLASSIFIER_WINDOW_BYTES: usize = 32 * 1024;
const CLASSIFIER_WINDOW_OVERLAP_BYTES: usize = 2 * 1024;

pub struct Collector {
    paths: AgentPaths,
    alert_tracker: AlertTracker,
    trae_tracker: TraeTracker,
    telemetry: TelemetrySampler,
    app_server_sampler: Option<ThreadTitleSampler>,
    remembered_pair: Option<(String, String)>,
    closed: bool,
}

impl Collector {
    pub fn new(paths: AgentPaths, pricing: Arc<PricingCatalog>) -> Result<Self> {
        Self::with_ccswitch_usage(paths, pricing, None)
    }

    /// The normal runtime explicitly opts into CC Switch. Existing collectors,
    /// including --collect-once fixture collection, never discover home data.
    pub fn with_ccswitch_usage(
        paths: AgentPaths,
        pricing: Arc<PricingCatalog>,
        ccswitch_database: Option<PathBuf>,
    ) -> Result<Self> {
        Self::with_ccswitch_usage_and_host_enabled(paths, pricing, ccswitch_database, true)
    }

    /// Creates a collector with an explicit host-telemetry policy. The normal
    /// runtime disables host sampling while legacy direct callers keep the
    /// original host-enabled behavior through `with_ccswitch_usage`.
    pub fn with_ccswitch_usage_and_host_enabled(
        paths: AgentPaths,
        pricing: Arc<PricingCatalog>,
        ccswitch_database: Option<PathBuf>,
        host_enabled: bool,
    ) -> Result<Self> {
        fs::create_dir_all(&paths.state)
            .with_context(|| format!("create agent state directory {}", paths.state.display()))?;
        let thread_names = Arc::new(std::sync::RwLock::new(HashMap::new()));
        let alert_tracker = AlertTracker::new(
            paths.codex.join("state_5.sqlite"),
            paths.state.join("codex-alerts.json"),
            paths.codex.join(".stopwatch_alerts.json"),
            Arc::clone(&thread_names),
        )?;
        let trae_root = paths.trae.clone();
        let trae_tracker = TraeTracker::new(
            trae_root,
            paths.state.join("trae-cursors.json"),
            paths.state.join("trae-tasks.json"),
        );
        let remembered_pair = load_model_pair(&paths.state.join("model-selection.json"));
        if let Some(database) = ccswitch_database.as_deref().filter(|path| path.exists()) {
            tracing::info!(path = %database.display(), "Daily usage source: CC Switch local session statistics; models.dev API-equivalent estimate");
        }
        let telemetry = TelemetrySampler::with_ccswitch_usage_and_host_enabled(
            paths.codex.clone(),
            Arc::clone(&pricing),
            ccswitch_database,
            host_enabled,
        );
        Ok(Self {
            paths,
            alert_tracker,
            trae_tracker,
            telemetry,
            app_server_sampler: None,
            remembered_pair,
            closed: false,
        })
    }

    /// Opts into Codex app-server thread names. The default keeps tests and
    /// fixture/self-test collectors entirely process-free; runtime opts in.
    pub fn with_app_server_enabled(mut self, enabled: bool) -> Self {
        if enabled && self.app_server_sampler.is_none() {
            let names = Arc::clone(&self.alert_tracker.thread_names);
            self.app_server_sampler = ThreadTitleSampler::new(names);
        } else if !enabled {
            if let Some(mut sampler) = self.app_server_sampler.take() {
                sampler.close();
            }
        }
        self
    }

    /// Enables optional provider telemetry when runtime supplies its inputs.
    /// Fixture collectors remain process-free because both inputs default off.
    pub fn with_optional_providers(
        mut self,
        claude_token: Option<String>,
        opencode_import: Option<PathBuf>,
    ) -> Self {
        self.telemetry
            .configure_optional_providers(claude_token, opencode_import);
        self
    }

    /// Imports the former TRAE state files from an explicitly supplied legacy
    /// directory. Existing agent-owned state always takes precedence.
    pub fn with_legacy_trae_state_dir(mut self, legacy_dir: Option<PathBuf>) -> Self {
        if let Some(legacy_dir) = legacy_dir
            && self
                .trae_tracker
                .import_legacy_state_dir(&legacy_dir)
                .is_err()
        {
            tracing::warn!("Legacy TRAE state could not be imported; keeping current local state");
        }
        self
    }

    pub fn poll(&mut self) -> Result<StatusSnapshot> {
        if self.closed {
            anyhow::bail!("collector is closed");
        }
        let alerts = self
            .alert_tracker
            .refresh()
            .unwrap_or_else(|_| self.alert_tracker.snapshot());
        let model_pairs = read_task_model_pairs(&self.paths.codex.join("state_5.sqlite"), &alerts);
        let codex_status = read_codex_base_status(&self.paths.codex);
        let winner = majority_running_pair(&alerts, &model_pairs);
        if let Some(winner) = winner {
            if self.remembered_pair.as_ref() != Some(&winner) {
                self.remembered_pair = Some(winner.clone());
                save_model_pair(&self.paths.state.join("model-selection.json"), &winner);
            }
        }
        let selected = self.remembered_pair.clone().or_else(|| {
            let model = codex_status.get("model").cloned().unwrap_or_default();
            let effort = codex_status.get("effort").cloned().unwrap_or_default();
            (!model.is_empty() || !effort.is_empty()).then_some((model, effort))
        });
        let telemetry = self.telemetry.snapshot();
        let plan = self.telemetry.quota_plan().unwrap_or_default();
        let codex = build_codex_status(&alerts, &model_pairs, selected.as_ref(), &plan, &telemetry);
        let trae = self
            .trae_tracker
            .refresh()
            .unwrap_or_else(|_| TraeSnapshot::unavailable());
        let trae_value = trae.to_json();
        let mut details = BTreeMap::new();
        for alert in &alerts.alerts {
            let route = route_id_for_thread(&alert.thread_id);
            let detail = TaskDetail {
                route: route.clone(),
                revision: alerts.revision,
                source: "codex".into(),
                task_id: alert.thread_id.clone(),
                project: alert.project.clone(),
                title: alert.title.clone(),
                reply: bounded_string(&alert.last_reply, MAX_TASK_DETAIL_BYTES),
            };
            details.entry(route).or_insert(detail);
        }
        Ok(StatusSnapshot {
            codex,
            trae: trae_value,
            telemetry,
            details,
            notifications: alerts
                .alerts
                .iter()
                .filter(|alert| alert.state == "pending" && !alert.watch_notified)
                .map(|alert| alert.id.clone())
                .collect(),
        })
    }

    /// Commit notification IDs only after the transport reports a successful
    /// delivery. Failed/ambiguous sends leave the durable IDs pending.
    pub fn mark_notified(&mut self, ids: &[String]) -> Result<()> {
        self.alert_tracker.mark_notified(ids)
    }

    pub fn close(&mut self) {
        if self.closed {
            return;
        }
        self.closed = true;
        self.telemetry.close();
        if let Some(mut sampler) = self.app_server_sampler.take() {
            sampler.close();
        }
    }
}

impl Drop for Collector {
    fn drop(&mut self) {
        self.close();
    }
}

struct ThreadTitleSampler {
    stop: Arc<AtomicBool>,
    worker: Option<JoinHandle<()>>,
}

impl ThreadTitleSampler {
    fn new(names: Arc<std::sync::RwLock<HashMap<String, String>>>) -> Option<Self> {
        let stop = Arc::new(AtomicBool::new(false));
        let worker_stop = Arc::clone(&stop);
        let worker = thread::Builder::new()
            .name("stopwatch-codex-thread-titles".into())
            .spawn(move || {
                while !worker_stop.load(Ordering::Acquire) {
                    if let Some(binary) = find_codex_binary() {
                        if let Ok(sampled) =
                            read_app_server_thread_names(&binary, APP_SERVER_TIMEOUT)
                            && !sampled.is_empty()
                        {
                            let mut current = names
                                .write()
                                .unwrap_or_else(|poisoned| poisoned.into_inner());
                            current.extend(sampled);
                            if current.len() > APP_SERVER_THREAD_LIMIT {
                                let mut entries: Vec<_> = current.keys().cloned().collect();
                                entries.sort();
                                for stale in entries
                                    .into_iter()
                                    .take(current.len() - APP_SERVER_THREAD_LIMIT)
                                {
                                    current.remove(&stale);
                                }
                            }
                        }
                    }
                    sleep_until_stopped(&worker_stop, APP_SERVER_REFRESH);
                }
            })
            .ok()?;
        Some(Self {
            stop,
            worker: Some(worker),
        })
    }

    fn close(&mut self) {
        self.stop.store(true, Ordering::Release);
        if let Some(worker) = self.worker.take() {
            let _ = worker.join();
        }
    }
}

impl Drop for ThreadTitleSampler {
    fn drop(&mut self) {
        self.close();
    }
}

fn sleep_until_stopped(stop: &AtomicBool, duration: Duration) {
    let deadline = std::time::Instant::now() + duration;
    while !stop.load(Ordering::Acquire) {
        let remaining = deadline.saturating_duration_since(std::time::Instant::now());
        if remaining.is_zero() {
            break;
        }
        thread::sleep(remaining.min(Duration::from_millis(200)));
    }
}

fn find_codex_binary() -> Option<PathBuf> {
    if let Some(configured) = std::env::var_os("STOPWATCH_CODEX_BINARY") {
        let path = PathBuf::from(configured);
        if path.is_file() {
            return Some(path);
        }
    }
    if let Some(path) = std::env::var_os("PATH") {
        for directory in std::env::split_paths(&path) {
            let candidate = directory.join("codex");
            if candidate.is_file() {
                return Some(candidate);
            }
        }
    }
    [
        "/Applications/ChatGPT.app/Contents/Resources/codex",
        "/Applications/Codex.app/Contents/Resources/codex",
    ]
    .into_iter()
    .map(PathBuf::from)
    .find(|path| path.is_file())
}

fn read_app_server_thread_names(
    binary: &Path,
    timeout: Duration,
) -> Result<HashMap<String, String>> {
    let mut child = Command::new(binary)
        .args(["app-server", "--stdio"])
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::null())
        .spawn()
        .with_context(|| format!("start Codex app-server at {}", binary.display()))?;
    let mut stdin = child
        .stdin
        .take()
        .context("Codex app-server stdin missing")?;
    let stdout = child
        .stdout
        .take()
        .context("Codex app-server stdout missing")?;
    let (sender, receiver) = mpsc::channel::<Value>();
    let reader = match thread::Builder::new()
        .name("stopwatch-codex-app-server-reader".into())
        .spawn(move || {
            let mut stdout = BufReader::new(stdout);
            loop {
                let mut line = Vec::new();
                let read = match stdout
                    .by_ref()
                    .take(MAX_APP_SERVER_LINE_BYTES as u64 + 1)
                    .read_until(b'\n', &mut line)
                {
                    Ok(read) => read,
                    Err(_) => break,
                };
                if read == 0 || line.len() > MAX_APP_SERVER_LINE_BYTES {
                    break;
                }
                if let Ok(message) = serde_json::from_slice::<Value>(&line)
                    && sender.send(message).is_err()
                {
                    break;
                }
            }
        }) {
        Ok(reader) => reader,
        Err(error) => {
            let _ = child.kill();
            let _ = child.wait();
            return Err(error).context("start app-server response reader");
        }
    };

    let deadline = std::time::Instant::now() + timeout;
    let result = (|| {
        send_app_server_request(
            &mut stdin,
            1,
            "initialize",
            json!({
                "clientInfo": {
                    "name": "stopwatch-codex-agent",
                    "title": "StopWatch Codex Agent",
                    "version": env!("CARGO_PKG_VERSION")
                },
                "capabilities": {}
            }),
        )?;
        let initialized = await_app_server_response(&receiver, 1, deadline)?;
        if !initialized.get("result").is_some_and(Value::is_object) {
            anyhow::bail!("Codex app-server initialize returned no result");
        }
        send_app_server_request(
            &mut stdin,
            2,
            "thread/list",
            json!({
                "limit": APP_SERVER_THREAD_LIMIT,
                "sortKey": "recency_at",
                "sortDirection": "desc"
            }),
        )?;
        let response = await_app_server_response(&receiver, 2, deadline)?;
        let Some(rows) = response
            .get("result")
            .and_then(|result| result.get("data"))
            .and_then(Value::as_array)
        else {
            anyhow::bail!("Codex app-server thread/list returned no data");
        };
        let mut names = HashMap::new();
        for row in rows.iter().take(APP_SERVER_THREAD_LIMIT) {
            let Some(id) = row.get("id").and_then(Value::as_str) else {
                continue;
            };
            let Some(name) = row
                .get("name")
                .and_then(Value::as_str)
                .map(str::trim)
                .filter(|name| !name.is_empty())
            else {
                continue;
            };
            if id.len() <= 512 && name.len() <= 1024 {
                names.insert(id.to_owned(), name.to_owned());
            }
        }
        Ok(names)
    })();

    drop(stdin);
    let _ = child.kill();
    let _ = child.wait();
    drop(receiver);
    let _ = reader.join();
    result
}

fn send_app_server_request(
    stdin: &mut impl Write,
    id: u64,
    method: &str,
    params: Value,
) -> Result<()> {
    let request = json!({"jsonrpc":"2.0", "id":id, "method":method, "params":params});
    serde_json::to_writer(&mut *stdin, &request)?;
    stdin.write_all(b"\n")?;
    stdin.flush()?;
    Ok(())
}

fn await_app_server_response(
    receiver: &mpsc::Receiver<Value>,
    request_id: u64,
    deadline: std::time::Instant,
) -> Result<Value> {
    loop {
        let remaining = deadline.saturating_duration_since(std::time::Instant::now());
        if remaining.is_zero() {
            anyhow::bail!("Codex app-server request timed out");
        }
        let message = receiver
            .recv_timeout(remaining)
            .context("Codex app-server closed before replying")?;
        if message.get("id").and_then(Value::as_u64) == Some(request_id) {
            return Ok(message);
        }
    }
}

fn build_codex_status(
    alerts: &AlertSnapshot,
    model_pairs: &HashMap<String, (String, String)>,
    selected: Option<&(String, String)>,
    plan: &str,
    telemetry: &Value,
) -> Value {
    let usage = telemetry.get("usage").unwrap_or(&Value::Null);
    let mut five_hour = usage.get("h").cloned().unwrap_or(Value::Null);
    let mut week = usage.get("w").cloned().unwrap_or(Value::Null);
    // Legacy status values are remaining percentages, and missing/expired
    // quota fields remain JSON null instead of silently becoming 100%.
    if usage.get("q").and_then(Value::as_str) != Some("ok") {
        five_hour = Value::Null;
        week = Value::Null;
    }
    let (model, effort) = selected
        .map(|(m, e)| (m.as_str(), e.as_str()))
        .unwrap_or(("", ""));
    let entries: Vec<Value> = alerts
        .alerts
        .iter()
        .map(|alert| {
            let (task_model, task_effort) = model_pairs
                .get(&alert.thread_id)
                .map(|(model, effort)| (model.as_str(), effort.as_str()))
                .unwrap_or(("", ""));
            let mut item = Map::new();
            item.insert("route".into(), json!(route_id_for_thread(&alert.thread_id)));
            item.insert("thread_id".into(), json!(alert.thread_id));
            item.insert("project".into(), json!(alert.project));
            item.insert("title".into(), json!(alert.title));
            item.insert("time".into(), json!(format_local_time(&alert.progress_at)));
            item.insert("state".into(), json!(alert.state));
            item.insert("outcome".into(), json!(alert.outcome));
            item.insert("turn_id".into(), json!(alert.turn_id));
            // The full response is private detail data and is transported through
            // StatusSnapshot.details only.
            if !task_model.is_empty() {
                item.insert("model".into(), json!(task_model));
            }
            if !task_effort.is_empty() {
                item.insert("effort".into(), json!(task_effort));
            }
            Value::Object(item)
        })
        .collect();
    json!({
        "codex_model": model,
        "codex_effort": effort,
        "codex_5h_left_pct": five_hour,
        "codex_week_left_pct": week,
        "codex_status_ok": !model.is_empty(),
        "codex_plan": plan,
        "codex_alert_pending": alerts.pending_count,
        "codex_alert_running": alerts.running_count,
        "codex_alert_completed": alerts.completed_count,
        "codex_alert_revision": alerts.revision,
        "codex_alerts": entries,
    })
}

fn read_codex_base_status(root: &Path) -> HashMap<String, String> {
    let mut output = HashMap::new();
    if let Ok(text) = fs::read_to_string(root.join("config.toml")) {
        for line in text.lines() {
            let Some((key, value)) = line.split_once('=') else {
                continue;
            };
            let value = value.trim().trim_matches('"').trim();
            match key.trim() {
                "model" if !value.is_empty() => {
                    output.insert("model".into(), value.into());
                }
                "model_reasoning_effort" if !value.is_empty() => {
                    output.insert("effort".into(), value.to_lowercase());
                }
                _ => {}
            }
        }
    }
    let logs = root.join("logs_2.sqlite");
    if let Ok(connection) = open_readonly(&logs) {
        if let Ok(Some(body)) = connection.query_row(
            "SELECT feedback_log_body FROM logs WHERE feedback_log_body LIKE '%codex.turn.reasoning_effort=%' ORDER BY id DESC LIMIT 1",
            [], |row| row.get::<_, String>(0),
        ).optional() {
            if let Some(effort) = Regex::new(r"codex\.turn\.reasoning_effort=(\w+)").ok().and_then(|re| re.captures(&body).and_then(|cap| cap.get(1)).map(|m| m.as_str().to_lowercase())) {
                output.insert("effort".into(), effort);
            }
            if let Some(model) = Regex::new(r"\bmodel=([\w.\-]+)").ok().and_then(|re| re.captures(&body).and_then(|cap| cap.get(1)).map(|m| m.as_str().to_string())) {
                output.insert("model".into(), model);
            }
        }
    }
    output
}

fn open_readonly(path: &Path) -> rusqlite::Result<Connection> {
    let connection = Connection::open_with_flags(
        path,
        OpenFlags::SQLITE_OPEN_READ_ONLY | OpenFlags::SQLITE_OPEN_NO_MUTEX,
    )?;
    connection.busy_timeout(Duration::from_secs(1))?;
    Ok(connection)
}

fn read_task_model_pairs(path: &Path, alerts: &AlertSnapshot) -> HashMap<String, (String, String)> {
    let active: Vec<String> = alerts
        .alerts
        .iter()
        .filter(|alert| matches!(alert.state.as_str(), "pending" | "running"))
        .map(|alert| alert.thread_id.clone())
        .collect();
    if active.is_empty() {
        return HashMap::new();
    }
    let Ok(connection) = open_readonly(path) else {
        return HashMap::new();
    };
    let mut output = HashMap::new();
    let mut statement =
        match connection.prepare("SELECT id, model, reasoning_effort FROM threads WHERE id = ?1") {
            Ok(statement) => statement,
            Err(_) => return output,
        };
    for thread in active {
        if let Ok(Some((model, effort))) = statement
            .query_row(params![thread], |row| {
                Ok((
                    row.get::<_, Option<String>>(1)?.unwrap_or_default(),
                    row.get::<_, Option<String>>(2)?.unwrap_or_default(),
                ))
            })
            .optional()
        {
            if !model.trim().is_empty() && !effort.trim().is_empty() {
                output.insert(
                    thread,
                    (model.trim().to_string(), effort.trim().to_lowercase()),
                );
            }
        }
    }
    output
}

fn majority_running_pair(
    alerts: &AlertSnapshot,
    pairs: &HashMap<String, (String, String)>,
) -> Option<(String, String)> {
    let mut counts: HashMap<(String, String), (usize, String)> = HashMap::new();
    for alert in alerts
        .alerts
        .iter()
        .filter(|alert| alert.state == "running")
    {
        let Some(pair) = pairs.get(&alert.thread_id) else {
            continue;
        };
        if pair.0.is_empty() || pair.1.is_empty() {
            continue;
        }
        let current = counts.entry(pair.clone()).or_insert((0, String::new()));
        current.0 += 1;
        if alert.progress_at > current.1 {
            current.1 = alert.progress_at.clone();
        }
    }
    counts
        .into_iter()
        .max_by(|(pair_a, a), (pair_b, b)| (a.0, &a.1, pair_a).cmp(&(b.0, &b.1, pair_b)))
        .map(|(pair, _)| pair)
}

fn load_model_pair(path: &Path) -> Option<(String, String)> {
    let value: Value = serde_json::from_slice(&fs::read(path).ok()?).ok()?;
    let model = value.get("model")?.as_str()?.trim();
    let effort = value.get("effort")?.as_str()?.trim().to_lowercase();
    (!model.is_empty() && !effort.is_empty()).then(|| (model.to_string(), effort))
}

fn save_model_pair(path: &Path, pair: &(String, String)) {
    let _ = atomic_json(path, &json!({"model": pair.0, "effort": pair.1}));
}

fn atomic_json(path: &Path, value: &Value) -> Result<()> {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)?;
    }
    let tmp = path.with_extension(format!(
        "{}.tmp",
        path.extension().and_then(|s| s.to_str()).unwrap_or("json")
    ));
    let bytes = serde_json::to_vec(value)?;
    let mut file = File::create(&tmp)?;
    file.write_all(&bytes)?;
    file.sync_all()?;
    fs::rename(&tmp, path)?;
    Ok(())
}

fn parse_time(value: &str) -> Option<DateTime<Utc>> {
    DateTime::parse_from_rfc3339(value)
        .ok()
        .map(|time| time.with_timezone(&Utc))
        .or_else(|| {
            chrono::NaiveDateTime::parse_from_str(value, "%Y-%m-%dT%H:%M:%S%.f")
                .ok()
                .map(|time| time.and_utc())
        })
}

fn iso_time(time: DateTime<Utc>) -> String {
    time.to_rfc3339_opts(chrono::SecondsFormat::AutoSi, true)
}

fn event_time(event: &Value, payload: &Value) -> Option<DateTime<Utc>> {
    event
        .get("timestamp")
        .and_then(Value::as_str)
        .or_else(|| payload.get("timestamp").and_then(Value::as_str))
        .and_then(parse_time)
}

fn route_id_for_thread(thread_id: &str) -> String {
    let digest = blake2b_64_personalized(thread_id.as_bytes(), b"SWRoute1");
    digest.iter().map(|byte| format!("{byte:02x}")).collect()
}

fn blake2b_64_personalized(input: &[u8], personal: &[u8; 8]) -> [u8; 8] {
    const IV: [u64; 8] = [
        0x6a09e667f3bcc908,
        0xbb67ae8584caa73b,
        0x3c6ef372fe94f82b,
        0xa54ff53a5f1d36f1,
        0x510e527fade682d1,
        0x9b05688c2b3e6c1f,
        0x1f83d9abfb41bd6b,
        0x5be0cd19137e2179,
    ];
    const SIGMA: [[usize; 16]; 12] = [
        [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15],
        [14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3],
        [11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4],
        [7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8],
        [9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13],
        [2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9],
        [12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11],
        [13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10],
        [6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5],
        [10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0],
        [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15],
        [14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3],
    ];
    fn g(v: &mut [u64; 16], a: usize, b: usize, c: usize, d: usize, x: u64, y: u64) {
        v[a] = v[a].wrapping_add(v[b]).wrapping_add(x);
        v[d] = (v[d] ^ v[a]).rotate_right(32);
        v[c] = v[c].wrapping_add(v[d]);
        v[b] = (v[b] ^ v[c]).rotate_right(24);
        v[a] = v[a].wrapping_add(v[b]).wrapping_add(y);
        v[d] = (v[d] ^ v[a]).rotate_right(16);
        v[c] = v[c].wrapping_add(v[d]);
        v[b] = (v[b] ^ v[c]).rotate_right(63);
    }
    let mut h = IV;
    h[0] ^= 0x0101_0000 ^ 8;
    h[6] ^= u64::from_le_bytes(*personal);
    let personal2 = [0u8; 8];
    h[7] ^= u64::from_le_bytes(personal2);
    let blocks = if input.is_empty() {
        1
    } else {
        input.len().div_ceil(128)
    };
    for block_index in 0..blocks {
        let start = block_index * 128;
        let end = input.len().min(start + 128);
        let mut block = [0u8; 128];
        if end > start {
            block[..end - start].copy_from_slice(&input[start..end]);
        }
        let mut m = [0u64; 16];
        for (index, chunk) in block.chunks_exact(8).enumerate() {
            m[index] = u64::from_le_bytes(chunk.try_into().unwrap());
        }
        let mut v = [0u64; 16];
        v[..8].copy_from_slice(&h);
        v[8..].copy_from_slice(&IV);
        let count = end as u64;
        v[12] ^= count;
        if block_index + 1 == blocks {
            v[14] = !v[14];
        }
        for round in 0..12 {
            let s = SIGMA[round];
            g(&mut v, 0, 4, 8, 12, m[s[0]], m[s[1]]);
            g(&mut v, 1, 5, 9, 13, m[s[2]], m[s[3]]);
            g(&mut v, 2, 6, 10, 14, m[s[4]], m[s[5]]);
            g(&mut v, 3, 7, 11, 15, m[s[6]], m[s[7]]);
            g(&mut v, 0, 5, 10, 15, m[s[8]], m[s[9]]);
            g(&mut v, 1, 6, 11, 12, m[s[10]], m[s[11]]);
            g(&mut v, 2, 7, 8, 13, m[s[12]], m[s[13]]);
            g(&mut v, 3, 4, 9, 14, m[s[14]], m[s[15]]);
        }
        for i in 0..8 {
            h[i] ^= v[i] ^ v[i + 8];
        }
    }
    h[0].to_le_bytes()
}

fn bounded_string(value: &str, max_bytes: usize) -> String {
    if value.len() <= max_bytes {
        return value.to_string();
    }
    let mut end = max_bytes;
    while !value.is_char_boundary(end) {
        end -= 1;
    }
    value[..end].to_string()
}

fn format_local_time(value: &str) -> String {
    parse_time(value)
        .map(|time| time.with_timezone(&Local).format("%H:%M").to_string())
        .unwrap_or_else(|| "--:--".into())
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
struct AlertCursor {
    #[serde(default)]
    path: String,
    #[serde(default)]
    offset: u64,
    #[serde(default)]
    line_start_offset: Option<u64>,
    #[serde(default)]
    discarding_line: bool,
}

struct CompleteLineBatch {
    lines: Vec<Vec<u8>>,
    next_offset: u64,
    reset: bool,
    line_start_offset: Option<u64>,
    discarding_line: bool,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
struct TurnSignal {
    #[serde(default)]
    turn_id: String,
    #[serde(default)]
    unfinished_plan: bool,
    #[serde(default)]
    input_calls: Vec<String>,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
struct AlertRecord {
    #[serde(default)]
    id: String,
    #[serde(default)]
    thread_id: String,
    #[serde(default = "default_project")]
    project: String,
    #[serde(default = "default_title")]
    title: String,
    #[serde(default)]
    progress_at: String,
    #[serde(default)]
    handled_at: Option<String>,
    #[serde(default)]
    state: String,
    #[serde(default)]
    watch_notified: bool,
    #[serde(default)]
    outcome: String,
    #[serde(default)]
    reason: String,
    #[serde(default)]
    turn_id: Option<String>,
    #[serde(default, deserialize_with = "deserialize_nullable_string")]
    last_reply: String,
    #[serde(flatten)]
    extra: Map<String, Value>,
}

fn deserialize_nullable_string<'de, D>(deserializer: D) -> std::result::Result<String, D::Error>
where
    D: Deserializer<'de>,
{
    Ok(Option::<String>::deserialize(deserializer)?.unwrap_or_default())
}

fn default_project() -> String {
    "--".into()
}
fn default_title() -> String {
    "Untitled conversation".into()
}

#[derive(Clone, Debug, Serialize, Deserialize)]
struct AlertLedger {
    #[serde(default)]
    version: u8,
    #[serde(default)]
    classifier_version: u8,
    #[serde(default)]
    classifier_retry_threads: Vec<String>,
    #[serde(default)]
    reply_metadata_version: u8,
    #[serde(default)]
    reply_metadata_retry_threads: Vec<String>,
    #[serde(default)]
    abort_reducer_version: u8,
    #[serde(default)]
    abort_retry_threads: Vec<String>,
    #[serde(default)]
    initialized_at: String,
    #[serde(default)]
    last_scan_at: String,
    #[serde(default)]
    revision: u64,
    #[serde(default)]
    cursors: HashMap<String, AlertCursor>,
    #[serde(default)]
    alerts: Vec<AlertRecord>,
    #[serde(default)]
    turn_signals: HashMap<String, TurnSignal>,
    #[serde(flatten)]
    extra: Map<String, Value>,
}

impl Default for AlertLedger {
    fn default() -> Self {
        Self {
            version: ALERT_LEDGER_VERSION,
            classifier_version: CLASSIFIER_VERSION,
            classifier_retry_threads: Vec::new(),
            reply_metadata_version: REPLY_METADATA_VERSION,
            reply_metadata_retry_threads: Vec::new(),
            abort_reducer_version: ABORT_REDUCER_VERSION,
            abort_retry_threads: Vec::new(),
            initialized_at: String::new(),
            last_scan_at: String::new(),
            revision: 0,
            cursors: HashMap::new(),
            alerts: Vec::new(),
            turn_signals: HashMap::new(),
            extra: Map::new(),
        }
    }
}

#[derive(Clone, Debug)]
struct ThreadInfo {
    id: String,
    cwd: String,
    title: String,
    rollout: PathBuf,
}

impl ThreadInfo {
    fn project(&self) -> String {
        let path = Path::new(self.cwd.trim_end_matches('/'));
        if path
            .file_name()
            .is_some_and(|name| name.to_string_lossy().starts_with("g-p-"))
            && path
                .parent()
                .and_then(Path::file_name)
                .is_some_and(|name| name == ".chatgpt-projects")
        {
            return "临时会话".into();
        }
        path.file_name()
            .map(|name| name.to_string_lossy().into_owned())
            .filter(|s| !s.is_empty())
            .unwrap_or_else(|| {
                if self.cwd.is_empty() {
                    "--".into()
                } else {
                    self.cwd.clone()
                }
            })
    }
}

#[derive(Clone, Debug, Default)]
struct AlertSnapshot {
    pending_count: usize,
    running_count: usize,
    completed_count: usize,
    revision: u64,
    alerts: Vec<AlertRecord>,
}

struct AlertTracker {
    state_db: PathBuf,
    ledger_path: PathBuf,
    state: Option<AlertLedger>,
    needs_save: bool,
    thread_names: Arc<std::sync::RwLock<HashMap<String, String>>>,
}

impl AlertTracker {
    fn new(
        state_db: PathBuf,
        ledger_path: PathBuf,
        legacy_ledger_path: PathBuf,
        thread_names: Arc<std::sync::RwLock<HashMap<String, String>>>,
    ) -> Result<Self> {
        let mut state = Self::load(&ledger_path);
        let mut needs_save = state
            .as_mut()
            .is_some_and(|ledger| normalize_ledger(ledger));
        if state.is_none() && !ledger_path.exists() && legacy_ledger_path.exists() {
            match load_legacy_ledger(&legacy_ledger_path) {
                Ok(mut imported) => {
                    normalize_ledger(&mut imported);
                    atomic_json(&ledger_path, &serde_json::to_value(&imported)?)
                        .context("persist imported Codex alert ledger to agent state")?;
                    state = Some(imported);
                    needs_save = false;
                }
                Err(_) => {
                    // Legacy state is read-only input. Keep it untouched and let
                    // refresh establish a clean baseline in the agent-owned state.
                    tracing::warn!(
                        "Legacy Codex alert ledger could not be imported; continuing with clean local state"
                    );
                }
            }
        }
        Ok(Self {
            state_db,
            ledger_path,
            state,
            needs_save,
            thread_names,
        })
    }

    fn refresh(&mut self) -> Result<AlertSnapshot> {
        let threads = self.discover_threads();
        if self.state.is_none() {
            let mut state = AlertLedger::default();
            let now = iso_time(Utc::now());
            state.initialized_at = now.clone();
            state.last_scan_at = now;
            for thread in &threads {
                let offset = fs::metadata(&thread.rollout)
                    .map(|meta| meta.len())
                    .unwrap_or(0);
                state.cursors.insert(
                    thread.id.clone(),
                    AlertCursor {
                        path: thread.rollout.display().to_string(),
                        offset,
                        line_start_offset: None,
                        discarding_line: false,
                    },
                );
            }
            self.state = Some(state);
            self.save()?;
            return Ok(self.snapshot());
        }

        let state = self.state.as_mut().expect("initialized ledger");
        let mut changed = self.needs_save;
        changed |= scan_alert_threads(state, &threads);
        changed |= reconcile_abort_version(state, &threads);
        changed |= reconcile_classifier_version(state, &threads);
        changed |= reconcile_reply_version(state, &threads);
        changed |= sync_alert_metadata(state, &threads);
        changed |= prune_alerts(state);
        if changed {
            state.last_scan_at = iso_time(Utc::now());
            self.save()?;
        }
        Ok(self.snapshot())
    }

    fn mark_notified(&mut self, ids: &[String]) -> Result<()> {
        if self.state.is_none() {
            self.state = Self::load(&self.ledger_path);
        }
        let Some(state) = self.state.as_mut() else {
            return Ok(());
        };
        let target: HashSet<&str> = ids.iter().map(String::as_str).collect();
        let mut changed = false;
        for alert in &mut state.alerts {
            if target.contains(alert.id.as_str())
                && alert.state == "pending"
                && !alert.watch_notified
            {
                alert.watch_notified = true;
                changed = true;
            }
        }
        if changed {
            self.needs_save = true;
        }
        if self.needs_save {
            self.save()?;
        }
        Ok(())
    }

    fn snapshot(&self) -> AlertSnapshot {
        let Some(state) = self.state.as_ref() else {
            return AlertSnapshot::default();
        };
        let today = Local::now().date_naive();
        let mut latest: HashMap<&str, (&AlertRecord, DateTime<Utc>)> = HashMap::new();
        for alert in &state.alerts {
            if !matches!(alert.state.as_str(), "pending" | "running" | "completed") {
                continue;
            }
            let Some(timestamp) = parse_time(&alert.progress_at) else {
                continue;
            };
            let replace = latest
                .get(alert.thread_id.as_str())
                .is_none_or(|(_, old)| timestamp >= *old);
            if replace {
                latest.insert(&alert.thread_id, (alert, timestamp));
            }
        }
        let mut alerts: Vec<AlertRecord> = latest
            .into_values()
            .filter_map(|(alert, timestamp)| {
                if alert.state == "completed"
                    && timestamp.with_timezone(&Local).date_naive() != today
                {
                    None
                } else {
                    Some(alert.clone())
                }
            })
            .collect();
        alerts.sort_by(|left, right| {
            let priority = |state: &str| match state {
                "pending" => 0,
                "running" => 1,
                _ => 2,
            };
            (
                priority(&left.state),
                std::cmp::Reverse(
                    parse_time(&left.progress_at)
                        .map(|t| t.timestamp())
                        .unwrap_or(0),
                ),
            )
                .cmp(&(
                    priority(&right.state),
                    std::cmp::Reverse(
                        parse_time(&right.progress_at)
                            .map(|t| t.timestamp())
                            .unwrap_or(0),
                    ),
                ))
        });
        AlertSnapshot {
            pending_count: alerts.iter().filter(|item| item.state == "pending").count(),
            running_count: alerts.iter().filter(|item| item.state == "running").count(),
            completed_count: alerts
                .iter()
                .filter(|item| item.state == "completed")
                .count(),
            revision: state.revision,
            alerts,
        }
    }

    fn discover_threads(&self) -> Vec<ThreadInfo> {
        let Ok(connection) = open_readonly(&self.state_db) else {
            return Vec::new();
        };
        let rows_result = connection.prepare(
            "SELECT id, cwd, title, rollout_path FROM threads WHERE model_provider = 'openai' AND (thread_source IS NULL OR thread_source != 'subagent') ORDER BY updated_at_ms DESC LIMIT ?1",
        );
        let Ok(mut statement) = rows_result else {
            return Vec::new();
        };
        let mut threads = Vec::new();
        if let Ok(rows) = statement.query_map(params![MAX_DISCOVERED_THREADS as i64], |row| {
            Ok((
                row.get::<_, String>(0)?,
                row.get::<_, Option<String>>(1)?.unwrap_or_default(),
                row.get::<_, Option<String>>(2)?.unwrap_or_default(),
                row.get::<_, Option<String>>(3)?.unwrap_or_default(),
            ))
        }) {
            for row in rows.flatten() {
                if !row.0.is_empty() && !row.3.is_empty() {
                    let title_override = self
                        .thread_names
                        .try_read()
                        .ok()
                        .and_then(|names| names.get(&row.0).cloned());
                    threads.push(ThreadInfo {
                        id: row.0,
                        cwd: row.1,
                        title: title_override.unwrap_or_else(|| {
                            if row.2.trim().is_empty() {
                                default_title()
                            } else {
                                row.2
                            }
                        }),
                        rollout: PathBuf::from(row.3),
                    });
                }
            }
        }
        if let Some(state) = self.state.as_ref() {
            let present: HashSet<String> = threads.iter().map(|item| item.id.clone()).collect();
            for missing in state.cursors.keys().filter(|id| !present.contains(*id)) {
                if let Ok(Some((cwd, title, rollout))) = connection
                    .query_row(
                        "SELECT cwd, title, rollout_path FROM threads WHERE id = ?1",
                        params![missing],
                        |row| {
                            Ok((
                                row.get::<_, Option<String>>(0)?.unwrap_or_default(),
                                row.get::<_, Option<String>>(1)?.unwrap_or_default(),
                                row.get::<_, Option<String>>(2)?.unwrap_or_default(),
                            ))
                        },
                    )
                    .optional()
                {
                    if !rollout.is_empty() {
                        let title_override = self
                            .thread_names
                            .try_read()
                            .ok()
                            .and_then(|names| names.get(missing).cloned());
                        threads.push(ThreadInfo {
                            id: missing.clone(),
                            cwd,
                            title: title_override.unwrap_or_else(|| {
                                if title.trim().is_empty() {
                                    default_title()
                                } else {
                                    title
                                }
                            }),
                            rollout: PathBuf::from(rollout),
                        });
                    }
                }
            }
        }
        let mut seen = HashSet::new();
        threads.retain(|thread| seen.insert(thread.id.clone()));
        threads
    }

    fn save(&mut self) -> Result<()> {
        let Some(state) = self.state.as_ref() else {
            return Ok(());
        };
        let value = serde_json::to_value(state)?;
        atomic_json(&self.ledger_path, &value)?;
        self.needs_save = false;
        Ok(())
    }

    fn load(path: &Path) -> Option<AlertLedger> {
        let bytes = fs::read(path).ok()?;
        let state: AlertLedger = match serde_json::from_slice(&bytes) {
            Ok(state) => state,
            Err(_) => {
                preserve_corrupt_ledger(path);
                return None;
            }
        };
        if !matches!(state.version, 1 | 2 | ALERT_LEDGER_VERSION) {
            preserve_corrupt_ledger(path);
            return None;
        }
        Some(state)
    }
}

fn load_legacy_ledger(path: &Path) -> Result<AlertLedger> {
    let metadata = fs::metadata(path)
        .with_context(|| format!("inspect legacy Codex alert ledger {}", path.display()))?;
    if !metadata.is_file() || metadata.len() > MAX_LEGACY_LEDGER_BYTES {
        anyhow::bail!(
            "legacy Codex alert ledger {} is not a bounded regular file; left untouched",
            path.display()
        );
    }
    let mut file = File::open(path)
        .with_context(|| format!("read legacy Codex alert ledger {}", path.display()))?;
    let mut bytes = Vec::new();
    std::io::Read::by_ref(&mut file)
        .take(MAX_LEGACY_LEDGER_BYTES + 1)
        .read_to_end(&mut bytes)
        .with_context(|| format!("read legacy Codex alert ledger {}", path.display()))?;
    if bytes.len() as u64 > MAX_LEGACY_LEDGER_BYTES {
        anyhow::bail!(
            "legacy Codex alert ledger {} grew beyond the migration limit; left untouched",
            path.display()
        );
    }
    let raw: Value = serde_json::from_slice(&bytes).with_context(|| {
        format!(
            "legacy Codex alert ledger {} is invalid JSON; left untouched",
            path.display()
        )
    })?;
    validate_legacy_ledger(&raw).with_context(|| {
        format!(
            "legacy Codex alert ledger {} has an unsupported schema; left untouched",
            path.display()
        )
    })?;
    serde_json::from_value(raw).with_context(|| {
        format!(
            "legacy Codex alert ledger {} could not be migrated; left untouched",
            path.display()
        )
    })
}

fn validate_legacy_ledger(value: &Value) -> Result<()> {
    let Some(object) = value.as_object() else {
        anyhow::bail!("ledger root is not an object");
    };
    let version = object.get("version").and_then(Value::as_u64);
    if !matches!(version, Some(1 | 2 | 3)) {
        anyhow::bail!("unsupported ledger version");
    }
    if !object.get("cursors").is_some_and(Value::is_object) {
        anyhow::bail!("cursors must be an object");
    }
    if !object.get("alerts").and_then(Value::as_array).is_some() {
        anyhow::bail!("alerts must be an array");
    }
    let alerts = object["alerts"].as_array().expect("validated array");
    if alerts.len() > 4096 {
        anyhow::bail!("too many alert entries");
    }
    let mut ids = HashSet::new();
    for alert in alerts {
        let Some(alert) = alert.as_object() else {
            anyhow::bail!("alert entry is not an object");
        };
        let Some(id) = alert
            .get("id")
            .and_then(Value::as_str)
            .filter(|id| !id.is_empty())
        else {
            anyhow::bail!("alert entry has no stable ID");
        };
        if !ids.insert(id) {
            anyhow::bail!("duplicate alert ID");
        }
        if !alert
            .get("thread_id")
            .and_then(Value::as_str)
            .is_some_and(|thread_id| !thread_id.is_empty())
        {
            anyhow::bail!("alert entry has no thread ID");
        }
        if !matches!(
            alert.get("state").and_then(Value::as_str),
            Some("pending" | "running" | "completed" | "handled")
        ) {
            anyhow::bail!("alert entry has an unsupported state");
        }
    }
    Ok(())
}

fn preserve_corrupt_ledger(path: &Path) {
    let Some(name) = path.file_name().and_then(|name| name.to_str()) else {
        return;
    };
    let base = format!("{name}.corrupt-{}", Utc::now().timestamp());
    for suffix in 0..100u32 {
        let candidate = if suffix == 0 {
            path.with_file_name(&base)
        } else {
            path.with_file_name(format!("{base}-{suffix}"))
        };
        if !candidate.exists() && fs::rename(path, candidate).is_ok() {
            break;
        }
    }
}

fn normalize_ledger(state: &mut AlertLedger) -> bool {
    let mut changed = false;
    for item in &mut state.alerts {
        match item.state.as_str() {
            "handled" => {
                item.state = "completed".into();
                item.outcome = "completed".into();
                changed = true;
            }
            "pending" if item.outcome != "waiting" => {
                if item.outcome == "completed" {
                    item.state = "completed".into();
                    item.outcome = "completed".into();
                } else {
                    item.outcome = "waiting".into();
                }
                changed = true;
            }
            "running" if item.outcome != "running" => {
                item.outcome = "running".into();
                changed = true;
            }
            "completed" if item.outcome != "completed" => {
                item.outcome = "completed".into();
                changed = true;
            }
            _ => {}
        }
        if item.state.is_empty() {
            item.state = "completed".into();
            item.outcome = "completed".into();
            item.watch_notified = true;
            changed = true;
        }
        if item.project.is_empty() {
            item.project = default_project();
            changed = true;
        }
        if item.title.is_empty() {
            item.title = default_title();
            changed = true;
        }
    }
    if state.version != ALERT_LEDGER_VERSION {
        state.version = ALERT_LEDGER_VERSION;
        changed = true;
    }
    changed
}

fn scan_alert_threads(state: &mut AlertLedger, threads: &[ThreadInfo]) -> bool {
    let mut changed = false;
    let last_scan = parse_time(&state.last_scan_at);
    for thread in threads {
        let path = thread.rollout.display().to_string();
        let existing = state.cursors.get(&thread.id).cloned();
        let path_changed = existing.as_ref().is_some_and(|cursor| cursor.path != path);
        let offset = if path_changed {
            0
        } else {
            existing.as_ref().map_or(0, |cursor| cursor.offset)
        };
        let line_start_offset = if path_changed {
            None
        } else {
            existing
                .as_ref()
                .and_then(|cursor| cursor.line_start_offset)
        };
        let discarding_line = !path_changed
            && existing
                .as_ref()
                .is_some_and(|cursor| cursor.discarding_line);
        let mut threshold = existing.is_none().then_some(last_scan).flatten();
        if path_changed {
            threshold = last_scan;
        }
        let CompleteLineBatch {
            lines,
            next_offset,
            reset,
            line_start_offset: next_line_start,
            discarding_line: next_discarding_line,
        } = read_complete_lines(&thread.rollout, offset, line_start_offset, discarding_line);
        if reset {
            threshold = last_scan;
        }
        if existing.is_none()
            || next_offset != offset
            || reset
            || path_changed
            || next_line_start != line_start_offset
            || next_discarding_line != discarding_line
        {
            state.cursors.insert(
                thread.id.clone(),
                AlertCursor {
                    path,
                    offset: next_offset,
                    line_start_offset: next_line_start,
                    discarding_line: next_discarding_line,
                },
            );
            changed = true;
        }
        for line in lines {
            if reduce_alert_line(state, thread, &line, threshold) {
                changed = true;
            }
        }
    }
    changed
}

// Scan at most one alert chunk per poll. The cursor stores only an in-flight
// line's start offset, so complete rows spanning chunks are reread once (under
// the existing 16 MiB line cap) when their newline arrives. Oversized rows are
// discarded in later bounded chunks without buffering them.
fn read_complete_lines(
    path: &Path,
    offset: u64,
    line_start_offset: Option<u64>,
    discarding_line: bool,
) -> CompleteLineBatch {
    let Ok(metadata) = fs::metadata(path) else {
        return CompleteLineBatch {
            lines: Vec::new(),
            next_offset: offset,
            reset: false,
            line_start_offset,
            discarding_line,
        };
    };
    let reset = offset > metadata.len();
    let start = if reset { 0 } else { offset };
    let mut line_start_offset = if reset {
        None
    } else {
        line_start_offset.filter(|line_start| *line_start <= start)
    };
    let mut discarding_line = !reset && discarding_line;
    if discarding_line && line_start_offset.is_none() {
        line_start_offset = Some(start);
    }
    let Ok(mut file) = File::open(path) else {
        return CompleteLineBatch {
            lines: Vec::new(),
            next_offset: start,
            reset,
            line_start_offset,
            discarding_line,
        };
    };
    if file.seek(SeekFrom::Start(start)).is_err() {
        return CompleteLineBatch {
            lines: Vec::new(),
            next_offset: start,
            reset,
            line_start_offset,
            discarding_line,
        };
    }
    let mut data = Vec::new();
    if file.take(ALERT_READ_CHUNK).read_to_end(&mut data).is_err() {
        return CompleteLineBatch {
            lines: Vec::new(),
            next_offset: start,
            reset,
            line_start_offset,
            discarding_line,
        };
    }
    if data.is_empty() {
        return CompleteLineBatch {
            lines: Vec::new(),
            next_offset: start,
            reset,
            line_start_offset,
            discarding_line,
        };
    }

    let next_offset = start + data.len() as u64;
    let mut lines = Vec::new();
    let mut position = 0;
    while position < data.len() {
        if discarding_line {
            let Some(relative_newline) = data[position..].iter().position(|byte| *byte == b'\n')
            else {
                return CompleteLineBatch {
                    lines,
                    next_offset,
                    reset,
                    line_start_offset,
                    discarding_line,
                };
            };
            position += relative_newline + 1;
            line_start_offset = None;
            discarding_line = false;
            continue;
        }

        let current_line_start = line_start_offset.unwrap_or(start + position as u64);
        let Some(relative_newline) = data[position..].iter().position(|byte| *byte == b'\n') else {
            let line_length = next_offset.saturating_sub(current_line_start);
            line_start_offset = Some(current_line_start);
            if line_length > MAX_REPLAY_LINE_BYTES as u64 {
                discarding_line = true;
            }
            break;
        };

        let line_end = position + relative_newline + 1;
        let line_end_offset = start + line_end as u64;
        let line_length = line_end_offset.saturating_sub(current_line_start);
        if line_length <= MAX_REPLAY_LINE_BYTES as u64 {
            let line = if current_line_start < start + position as u64 {
                read_complete_line_bytes(path, current_line_start, line_length)
            } else {
                Some(data[position..line_end].to_vec())
            };
            let Some(mut line) = line else {
                return CompleteLineBatch {
                    lines: Vec::new(),
                    next_offset: start,
                    reset,
                    line_start_offset,
                    discarding_line,
                };
            };
            line.pop();
            if line.last() == Some(&b'\r') {
                line.pop();
            }
            if !line.is_empty() {
                lines.push(line);
            }
        }
        position = line_end;
        line_start_offset = None;
    }

    CompleteLineBatch {
        lines,
        next_offset,
        reset,
        line_start_offset,
        discarding_line,
    }
}

fn read_complete_line_bytes(path: &Path, start: u64, length: u64) -> Option<Vec<u8>> {
    let size = usize::try_from(length).ok()?;
    let mut file = File::open(path).ok()?;
    file.seek(SeekFrom::Start(start)).ok()?;
    let mut line = vec![0; size];
    file.read_exact(&mut line).ok()?;
    (line.last() == Some(&b'\n')).then_some(line)
}

fn reduce_alert_line(
    state: &mut AlertLedger,
    thread: &ThreadInfo,
    line: &[u8],
    threshold: Option<DateTime<Utc>>,
) -> bool {
    let Ok(item) = serde_json::from_slice::<Value>(line) else {
        return false;
    };
    let Some(payload) = item.get("payload").and_then(Value::as_object) else {
        return false;
    };
    let payload = Value::Object(payload.clone());
    let Some(timestamp) = event_time(&item, &payload) else {
        return false;
    };
    if threshold.is_some_and(|limit| timestamp <= limit) {
        return false;
    }
    let Some(kind) = item.get("type").and_then(Value::as_str) else {
        return false;
    };
    if kind == "response_item" {
        return reduce_response_item(state, thread, &payload);
    }
    if kind != "event_msg" {
        return false;
    }
    let Some(event_kind) = payload.get("type").and_then(Value::as_str) else {
        return false;
    };
    let timestamp_text = iso_time(timestamp);
    match event_kind {
        "task_started" => {
            let turn_id = payload
                .get("turn_id")
                .and_then(Value::as_str)
                .filter(|s| !s.is_empty());
            let mut signal_changed = false;
            if let Some(turn_id) = turn_id {
                let next = TurnSignal {
                    turn_id: turn_id.to_string(),
                    ..TurnSignal::default()
                };
                if state
                    .turn_signals
                    .get(&thread.id)
                    .is_none_or(|old| old.turn_id != next.turn_id)
                {
                    state.turn_signals.insert(thread.id.clone(), next);
                    signal_changed = true;
                }
            }
            let id = format!("{}:{}", thread.id, timestamp_text);
            if state.alerts.iter().any(|alert| alert.id == id) {
                return signal_changed;
            }
            let latest_reply = state
                .alerts
                .iter()
                .filter(|entry| entry.thread_id == thread.id && !entry.last_reply.is_empty())
                .filter_map(|entry| {
                    parse_time(&entry.progress_at).map(|time| (time, entry.last_reply.clone()))
                })
                .filter(|(time, _)| *time < timestamp)
                .max_by_key(|(time, _)| *time)
                .map(|(_, reply)| reply);
            state.alerts.push(AlertRecord {
                id,
                thread_id: thread.id.clone(),
                project: thread.project(),
                title: thread.title.clone(),
                progress_at: timestamp_text,
                handled_at: None,
                state: "running".into(),
                watch_notified: true,
                outcome: "running".into(),
                reason: String::new(),
                turn_id: turn_id.map(str::to_string),
                last_reply: latest_reply.unwrap_or_default(),
                extra: Map::new(),
            });
            state.revision += 1;
            true
        }
        "turn_aborted" => {
            let Some(turn_id) = payload
                .get("turn_id")
                .and_then(Value::as_str)
                .filter(|s| !s.is_empty())
            else {
                return false;
            };
            if complete_aborted_running(state, thread, timestamp, turn_id, true, None) {
                state.revision += 1;
                true
            } else {
                false
            }
        }
        "task_complete" => reduce_task_complete(state, thread, timestamp, &payload),
        "user_message" => {
            let mut changed = false;
            for alert in &mut state.alerts {
                if alert.thread_id == thread.id
                    && alert.state == "pending"
                    && parse_time(&alert.progress_at).is_some_and(|progress| progress < timestamp)
                {
                    alert.state = "completed".into();
                    alert.outcome = "completed".into();
                    alert.handled_at = Some(timestamp_text.clone());
                    alert.watch_notified = true;
                    alert.reason = "user_replied".into();
                    changed = true;
                }
            }
            if changed {
                state.revision += 1;
            }
            changed
        }
        _ => false,
    }
}

fn reduce_task_complete(
    state: &mut AlertLedger,
    thread: &ThreadInfo,
    timestamp: DateTime<Utc>,
    payload: &Value,
) -> bool {
    let turn_id = payload.get("turn_id").and_then(Value::as_str);
    let signal = state
        .turn_signals
        .get(&thread.id)
        .filter(|signal| turn_id.is_some_and(|turn| turn == signal.turn_id));
    let classification = classify_completion(
        payload.get("last_agent_message").and_then(Value::as_str),
        signal.is_some_and(|signal| signal.unfinished_plan),
        signal.is_some_and(|signal| !signal.input_calls.is_empty()),
    );
    let state_name = if classification.outcome == "waiting" {
        "pending"
    } else {
        "completed"
    };
    let timestamp_text = iso_time(timestamp);
    let reply = payload
        .get("last_agent_message")
        .and_then(Value::as_str)
        .map(|value| bounded_string(value, MAX_TASK_DETAIL_BYTES))
        .unwrap_or_default();
    let latest_running = state
        .alerts
        .iter()
        .enumerate()
        .filter(|(_, alert)| alert.thread_id == thread.id && alert.state == "running")
        .filter_map(|(index, alert)| {
            parse_time(&alert.progress_at)
                .filter(|progress| *progress <= timestamp)
                .map(|progress| (index, progress))
        })
        .max_by_key(|(_, progress)| *progress)
        .map(|(index, _)| index);
    if let Some(index) = latest_running {
        let target = &mut state.alerts[index];
        target.id = format!("{}:{}", thread.id, timestamp_text);
        target.project = thread.project();
        target.title = thread.title.clone();
        target.progress_at = timestamp_text.clone();
        target.handled_at = (state_name == "completed").then_some(timestamp_text.clone());
        target.state = state_name.into();
        target.watch_notified = state_name != "pending";
        target.outcome = classification.outcome.into();
        target.reason = classification.reason.into();
        if payload
            .get("last_agent_message")
            .and_then(Value::as_str)
            .is_some()
        {
            target.last_reply = reply;
        }
        if let Some(turn) = turn_id.filter(|s| !s.is_empty()) {
            target.turn_id = Some(turn.to_string());
        }
    } else {
        let id = format!("{}:{}", thread.id, timestamp_text);
        if state.alerts.iter().any(|entry| entry.id == id) {
            return false;
        }
        state.alerts.push(AlertRecord {
            id,
            thread_id: thread.id.clone(),
            project: thread.project(),
            title: thread.title.clone(),
            progress_at: timestamp_text.clone(),
            handled_at: (state_name == "completed").then_some(timestamp_text),
            state: state_name.into(),
            watch_notified: state_name != "pending",
            outcome: classification.outcome.into(),
            reason: classification.reason.into(),
            turn_id: turn_id.map(str::to_string),
            last_reply: reply,
            extra: Map::new(),
        });
    }
    if turn_id.is_some_and(|turn| {
        state
            .turn_signals
            .get(&thread.id)
            .is_some_and(|signal| signal.turn_id == turn)
    }) {
        state.turn_signals.remove(&thread.id);
    }
    state.revision += 1;
    true
}

fn complete_aborted_running(
    state: &mut AlertLedger,
    thread: &ThreadInfo,
    timestamp: DateTime<Utc>,
    turn_id: &str,
    require_signal: bool,
    started_at: Option<DateTime<Utc>>,
) -> bool {
    let signal_matches = state
        .turn_signals
        .get(&thread.id)
        .is_some_and(|signal| signal.turn_id == turn_id);
    if require_signal && !signal_matches {
        return false;
    }
    let target = state
        .alerts
        .iter()
        .enumerate()
        .filter(|(_, alert)| alert.thread_id == thread.id && alert.state == "running")
        .filter_map(|(index, alert)| {
            let progress = parse_time(&alert.progress_at)?;
            if progress > timestamp {
                return None;
            }
            let turn_matches = alert
                .turn_id
                .as_deref()
                .is_none_or(|value| value.is_empty() || value == turn_id);
            let legacy_owned = alert.turn_id.as_deref().is_none_or(str::is_empty)
                && (signal_matches || started_at.is_some_and(|start| start == progress));
            (turn_matches
                && (alert
                    .turn_id
                    .as_deref()
                    .is_some_and(|value| value == turn_id)
                    || legacy_owned))
                .then_some((index, progress))
        })
        .max_by_key(|(_, progress)| *progress)
        .map(|(index, _)| index);
    let mut changed = false;
    if let Some(index) = target {
        let timestamp_text = iso_time(timestamp);
        let alert = &mut state.alerts[index];
        alert.id = format!("{}:{}", thread.id, timestamp_text);
        alert.project = thread.project();
        alert.title = thread.title.clone();
        alert.progress_at = timestamp_text.clone();
        alert.handled_at = Some(timestamp_text);
        alert.state = "completed".into();
        alert.watch_notified = true;
        alert.outcome = "completed".into();
        alert.reason = "turn_aborted".into();
        alert.turn_id = Some(turn_id.into());
        changed = true;
    }
    if signal_matches {
        state.turn_signals.remove(&thread.id);
        changed = true;
    }
    changed
}

fn reduce_response_item(state: &mut AlertLedger, thread: &ThreadInfo, payload: &Value) -> bool {
    let metadata_turn = payload
        .get("internal_chat_message_metadata_passthrough")
        .and_then(|v| v.get("turn_id"))
        .and_then(Value::as_str);
    let Some(signal) = state.turn_signals.get_mut(&thread.id) else {
        return false;
    };
    if metadata_turn.is_some_and(|turn| !turn.is_empty() && turn != signal.turn_id) {
        return false;
    }
    match payload.get("type").and_then(Value::as_str) {
        Some("function_call")
            if payload.get("name").and_then(Value::as_str) == Some("update_plan") =>
        {
            let raw = payload.get("arguments");
            let parsed = match raw {
                Some(Value::String(text)) => serde_json::from_str::<Value>(text).ok(),
                Some(value) => Some(value.clone()),
                None => None,
            };
            let Some(plan) = parsed.and_then(|value| value.get("plan").cloned()) else {
                return false;
            };
            let Some(items) = plan.as_array() else {
                return false;
            };
            let unfinished = items.iter().any(|item| {
                matches!(
                    item.get("status").and_then(Value::as_str),
                    Some("in_progress" | "pending")
                )
            });
            if signal.unfinished_plan == unfinished {
                false
            } else {
                signal.unfinished_plan = unfinished;
                true
            }
        }
        Some("function_call")
            if payload.get("name").and_then(Value::as_str) == Some("request_user_input") =>
        {
            let Some(call_id) = payload
                .get("call_id")
                .and_then(Value::as_str)
                .filter(|s| !s.is_empty())
            else {
                return false;
            };
            if signal.input_calls.iter().any(|value| value == call_id) {
                false
            } else {
                signal.input_calls.push(call_id.into());
                true
            }
        }
        Some("function_call_output") => {
            let Some(call_id) = payload.get("call_id").and_then(Value::as_str) else {
                return false;
            };
            let before = signal.input_calls.len();
            signal.input_calls.retain(|value| value != call_id);
            before != signal.input_calls.len()
        }
        _ => false,
    }
}

fn prune_alerts(state: &mut AlertLedger) -> bool {
    let cutoff = Local::now().date_naive() - chrono::Duration::days(ALERT_RETENTION_DAYS - 1);
    let old_len = state.alerts.len();
    state.alerts.retain(|alert| {
        parse_time(&alert.progress_at)
            .is_some_and(|time| time.with_timezone(&Local).date_naive() >= cutoff)
    });
    if state.alerts.len() != old_len {
        state.revision += 1;
        true
    } else {
        false
    }
}

fn sync_alert_metadata(state: &mut AlertLedger, threads: &[ThreadInfo]) -> bool {
    let by_id: HashMap<&str, &ThreadInfo> = threads
        .iter()
        .map(|thread| (thread.id.as_str(), thread))
        .collect();
    let mut changed = false;
    for alert in &mut state.alerts {
        let Some(thread) = by_id.get(alert.thread_id.as_str()) else {
            continue;
        };
        let project = thread.project();
        if alert.project != project {
            alert.project = project;
            changed = true;
        }
        if alert.title != thread.title {
            alert.title = thread.title.clone();
            changed = true;
        }
    }
    if changed {
        state.revision += 1;
    }
    changed
}

fn latest_by_thread(state: &AlertLedger) -> HashMap<String, (DateTime<Utc>, AlertRecord)> {
    let mut latest = HashMap::new();
    for alert in &state.alerts {
        let Some(timestamp) = parse_time(&alert.progress_at) else {
            continue;
        };
        let replace = latest
            .get(&alert.thread_id)
            .is_none_or(|(previous, _)| timestamp >= *previous);
        if replace {
            latest.insert(alert.thread_id.clone(), (timestamp, alert.clone()));
        }
    }
    latest
}

fn reconcile_abort_version(state: &mut AlertLedger, threads: &[ThreadInfo]) -> bool {
    let version_changed = state.abort_reducer_version != ABORT_REDUCER_VERSION;
    let retry: HashSet<String> = state.abort_retry_threads.iter().cloned().collect();
    if !version_changed && retry.is_empty() {
        return false;
    }
    let latest = latest_by_thread(state);
    let by_id: HashMap<&str, &ThreadInfo> = threads
        .iter()
        .map(|thread| (thread.id.as_str(), thread))
        .collect();
    let mut failed = HashSet::new();
    let mut changed = false;
    for (thread_id, (started_at, alert)) in latest {
        if (!version_changed && !retry.contains(&thread_id)) || alert.state != "running" {
            continue;
        }
        let Some(thread) = by_id.get(thread_id.as_str()) else {
            failed.insert(thread_id);
            continue;
        };
        match replay_abort(&thread.rollout, started_at) {
            Ok(Some((timestamp, turn_id))) => {
                changed |= complete_aborted_running(
                    state,
                    thread,
                    timestamp,
                    &turn_id,
                    false,
                    Some(started_at),
                )
            }
            Ok(None) => {}
            Err(true) => {
                failed.insert(thread_id);
            }
            Err(false) => {}
        }
    }
    if state.abort_reducer_version != ABORT_REDUCER_VERSION {
        state.abort_reducer_version = ABORT_REDUCER_VERSION;
        changed = true;
    }
    let next: Vec<String> = failed.into_iter().collect();
    if next != state.abort_retry_threads {
        state.abort_retry_threads = next;
        changed = true;
    }
    if changed {
        state.revision += 1;
    }
    changed
}

fn reconcile_classifier_version(state: &mut AlertLedger, threads: &[ThreadInfo]) -> bool {
    let version_changed = state.classifier_version != CLASSIFIER_VERSION;
    let retry: HashSet<String> = state.classifier_retry_threads.iter().cloned().collect();
    if !version_changed && retry.is_empty() {
        return false;
    }
    let latest = latest_by_thread(state);
    let by_id: HashMap<&str, &ThreadInfo> = threads
        .iter()
        .map(|thread| (thread.id.as_str(), thread))
        .collect();
    let mut failed = HashSet::new();
    let mut updates = Vec::new();
    for (thread_id, (_time, alert)) in latest {
        if (!version_changed && !retry.contains(&thread_id))
            || !matches!(alert.state.as_str(), "pending" | "completed")
        {
            continue;
        }
        let Some(thread) = by_id.get(thread_id.as_str()) else {
            failed.insert(thread_id);
            continue;
        };
        match replay_latest_completion(&thread.rollout) {
            Ok(Some(completion))
                if alert.id == format!("{}:{}", thread_id, iso_time(completion.timestamp)) =>
            {
                let classification = classify_completion(
                    completion.reply.as_deref(),
                    completion.unfinished_plan,
                    completion.unresolved_input,
                );
                updates.push((alert.id.clone(), classification));
            }
            Ok(_) => {}
            Err(true) => {
                failed.insert(thread_id);
            }
            Err(false) => {}
        }
    }
    let mut changed = false;
    for (id, classification) in updates {
        if let Some(alert) = state.alerts.iter_mut().find(|alert| alert.id == id) {
            let next_state = if classification.outcome == "waiting" {
                "pending"
            } else {
                "completed"
            };
            let notified = if next_state == "pending" && alert.state != "pending" {
                false
            } else if next_state == "pending" {
                alert.watch_notified
            } else {
                true
            };
            let handled = (next_state == "completed").then(|| alert.progress_at.clone());
            if alert.state != next_state
                || alert.outcome != classification.outcome
                || alert.reason != classification.reason
                || alert.watch_notified != notified
                || alert.handled_at != handled
            {
                alert.state = next_state.into();
                alert.outcome = classification.outcome.into();
                alert.reason = classification.reason.into();
                alert.watch_notified = notified;
                alert.handled_at = handled;
                changed = true;
            }
        }
    }
    if state.classifier_version != CLASSIFIER_VERSION {
        state.classifier_version = CLASSIFIER_VERSION;
        changed = true;
    }
    let next: Vec<String> = failed.into_iter().collect();
    if next != state.classifier_retry_threads {
        state.classifier_retry_threads = next;
        changed = true;
    }
    if changed {
        state.revision += 1;
    }
    changed
}

fn reconcile_reply_version(state: &mut AlertLedger, threads: &[ThreadInfo]) -> bool {
    let version_changed = state.reply_metadata_version != REPLY_METADATA_VERSION;
    let retry: HashSet<String> = state.reply_metadata_retry_threads.iter().cloned().collect();
    if !version_changed && retry.is_empty() {
        return false;
    }
    let latest = latest_by_thread(state);
    let by_id: HashMap<&str, &ThreadInfo> = threads
        .iter()
        .map(|thread| (thread.id.as_str(), thread))
        .collect();
    let mut failed = HashSet::new();
    let mut updates = Vec::new();
    for (thread_id, (timestamp, alert)) in latest {
        if (!version_changed && !retry.contains(&thread_id))
            || (!alert.last_reply.is_empty() && !(version_changed && alert.state == "running"))
        {
            continue;
        }
        let Some(thread) = by_id.get(thread_id.as_str()) else {
            failed.insert(thread_id);
            continue;
        };
        match replay_stable_reply(&thread.rollout, timestamp) {
            Ok(Some(reply)) => updates.push((alert.id.clone(), reply)),
            Ok(None) => {}
            Err(true) => {
                failed.insert(thread_id);
            }
            Err(false) => {}
        }
    }
    let mut changed = false;
    for (id, reply) in updates {
        if let Some(alert) = state.alerts.iter_mut().find(|alert| alert.id == id) {
            if alert.last_reply != reply {
                alert.last_reply = reply;
                changed = true;
            }
        }
    }
    if state.reply_metadata_version != REPLY_METADATA_VERSION {
        state.reply_metadata_version = REPLY_METADATA_VERSION;
        changed = true;
    }
    let next: Vec<String> = failed.into_iter().collect();
    if next != state.reply_metadata_retry_threads {
        state.reply_metadata_retry_threads = next;
        changed = true;
    }
    if changed {
        state.revision += 1;
    }
    changed
}

// Full-rollout replay only serves durable-version reconciliation. Each line is
// capped at 16 MiB; an oversized row is drained at most one alert read chunk.
fn replay_lines(
    path: &Path,
    mut visit: impl FnMut(Value) -> bool,
) -> std::result::Result<(), bool> {
    let mut file = File::open(path).map_err(|_| true)?;
    let mut line = Vec::new();
    let mut reader = std::io::BufReader::new(&mut file);
    let mut oversized_line_seen = false;
    loop {
        line.clear();
        let count = reader
            .by_ref()
            .take(MAX_REPLAY_LINE_BYTES as u64 + 1)
            .read_until(b'\n', &mut line)
            .map_err(|_| true)?;
        if count == 0 {
            break;
        }
        if !line.ends_with(b"\n") {
            if line.len() > MAX_REPLAY_LINE_BYTES {
                oversized_line_seen = true;
                line = Vec::new();
                if !drain_replay_line(&mut reader, ALERT_READ_CHUNK as usize).map_err(|_| true)? {
                    return Err(true);
                }
                continue;
            }
            return Err(true);
        }
        if line.len() > MAX_REPLAY_LINE_BYTES {
            oversized_line_seen = true;
            line = Vec::new();
            continue;
        }
        let item: Value = serde_json::from_slice(&line).map_err(|_| false)?;
        if visit(item) {
            return if oversized_line_seen {
                Err(true)
            } else {
                Ok(())
            };
        }
    }
    if oversized_line_seen {
        Err(true)
    } else {
        Ok(())
    }
}

fn drain_replay_line(reader: &mut impl BufRead, max_bytes: usize) -> std::io::Result<bool> {
    let mut drained = 0usize;
    while drained < max_bytes {
        let (count, found_newline) = {
            let available = reader.fill_buf()?;
            if available.is_empty() {
                return Ok(false);
            }
            let count = available.len().min(max_bytes - drained);
            let newline = available[..count].iter().position(|byte| *byte == b'\n');
            (newline.map_or(count, |index| index + 1), newline.is_some())
        };
        reader.consume(count);
        drained += count;
        if found_newline {
            return Ok(true);
        }
    }
    Ok(false)
}

fn replay_abort(
    path: &Path,
    started_at: DateTime<Utc>,
) -> std::result::Result<Option<(DateTime<Utc>, String)>, bool> {
    let mut turn_id: Option<String> = None;
    let mut result = None;
    replay_lines(path, |item| {
        if item.get("type").and_then(Value::as_str) != Some("event_msg") {
            return false;
        }
        let Some(payload) = item.get("payload") else {
            return false;
        };
        match payload.get("type").and_then(Value::as_str) {
            Some("task_started") if event_time(&item, payload) == Some(started_at) => {
                turn_id = payload
                    .get("turn_id")
                    .and_then(Value::as_str)
                    .filter(|s| !s.is_empty())
                    .map(str::to_string);
            }
            Some("turn_aborted")
                if turn_id.as_deref().is_some_and(|id| {
                    payload.get("turn_id").and_then(Value::as_str) == Some(id)
                }) =>
            {
                if let (Some(timestamp), Some(id)) = (event_time(&item, payload), turn_id.clone()) {
                    result = Some((timestamp, id));
                    return true;
                }
            }
            _ => {}
        }
        false
    })?;
    Ok(result)
}

struct ReplayCompletion {
    timestamp: DateTime<Utc>,
    reply: Option<String>,
    unfinished_plan: bool,
    unresolved_input: bool,
}

fn replay_latest_completion(path: &Path) -> std::result::Result<Option<ReplayCompletion>, bool> {
    let mut signal: Option<TurnSignal> = None;
    let mut candidate = None;
    replay_lines(path, |item| {
        let Some(payload) = item.get("payload") else {
            return false;
        };
        let Some(kind) = item.get("type").and_then(Value::as_str) else {
            return false;
        };
        if kind == "response_item" {
            if let Some(signal) = signal.as_mut() {
                let turn = payload
                    .get("internal_chat_message_metadata_passthrough")
                    .and_then(|v| v.get("turn_id"))
                    .and_then(Value::as_str);
                if turn.is_none_or(|id| id.is_empty() || id == signal.turn_id) {
                    reduce_signal_payload(signal, payload);
                }
            }
            return false;
        }
        if kind != "event_msg" {
            return false;
        }
        match payload.get("type").and_then(Value::as_str) {
            Some("task_started") => {
                signal = payload
                    .get("turn_id")
                    .and_then(Value::as_str)
                    .filter(|id| !id.is_empty())
                    .map(|id| TurnSignal {
                        turn_id: id.into(),
                        ..TurnSignal::default()
                    });
                candidate = None;
            }
            Some("user_message") => candidate = None,
            Some("task_complete") => {
                if let Some(timestamp) = event_time(&item, payload) {
                    let turn = payload.get("turn_id").and_then(Value::as_str);
                    let matching = signal
                        .as_ref()
                        .filter(|signal| turn == Some(signal.turn_id.as_str()));
                    candidate = Some(ReplayCompletion {
                        timestamp,
                        reply: payload
                            .get("last_agent_message")
                            .and_then(Value::as_str)
                            .map(str::to_string),
                        unfinished_plan: matching.is_some_and(|signal| signal.unfinished_plan),
                        unresolved_input: matching
                            .is_some_and(|signal| !signal.input_calls.is_empty()),
                    });
                    signal = None;
                }
            }
            _ => {}
        }
        false
    })?;
    Ok(candidate)
}

fn replay_stable_reply(
    path: &Path,
    at_or_before: DateTime<Utc>,
) -> std::result::Result<Option<String>, bool> {
    let mut result: Option<(DateTime<Utc>, String)> = None;
    replay_lines(path, |item| {
        if item.get("type").and_then(Value::as_str) != Some("event_msg") {
            return false;
        }
        let Some(payload) = item.get("payload") else {
            return false;
        };
        if payload.get("type").and_then(Value::as_str) != Some("task_complete") {
            return false;
        }
        let Some(timestamp) = event_time(&item, payload) else {
            return false;
        };
        let Some(reply) = payload.get("last_agent_message").and_then(Value::as_str) else {
            return false;
        };
        if timestamp <= at_or_before && result.as_ref().is_none_or(|(old, _)| timestamp >= *old) {
            result = Some((timestamp, bounded_string(reply, MAX_TASK_DETAIL_BYTES)));
        }
        false
    })?;
    Ok(result.map(|(_, reply)| reply))
}

fn reduce_signal_payload(signal: &mut TurnSignal, payload: &Value) -> bool {
    match payload.get("type").and_then(Value::as_str) {
        Some("function_call")
            if payload.get("name").and_then(Value::as_str) == Some("update_plan") =>
        {
            let parsed = match payload.get("arguments") {
                Some(Value::String(text)) => serde_json::from_str::<Value>(text).ok(),
                Some(value) => Some(value.clone()),
                None => None,
            };
            let Some(items) = parsed
                .and_then(|value| value.get("plan").cloned())
                .and_then(|value| value.as_array().cloned())
            else {
                return false;
            };
            let next = items.iter().any(|item| {
                matches!(
                    item.get("status").and_then(Value::as_str),
                    Some("in_progress" | "pending")
                )
            });
            if next == signal.unfinished_plan {
                false
            } else {
                signal.unfinished_plan = next;
                true
            }
        }
        Some("function_call")
            if payload.get("name").and_then(Value::as_str) == Some("request_user_input") =>
        {
            let Some(call_id) = payload
                .get("call_id")
                .and_then(Value::as_str)
                .filter(|s| !s.is_empty())
            else {
                return false;
            };
            if signal.input_calls.iter().any(|id| id == call_id) {
                false
            } else {
                signal.input_calls.push(call_id.into());
                true
            }
        }
        Some("function_call_output") => {
            let Some(call_id) = payload.get("call_id").and_then(Value::as_str) else {
                return false;
            };
            let old = signal.input_calls.len();
            signal.input_calls.retain(|id| id != call_id);
            old != signal.input_calls.len()
        }
        _ => false,
    }
}

#[derive(Clone, Copy)]
struct Evidence<'a> {
    offset: usize,
    kind: &'a str,
    scopes: &'static [&'static str],
    strength: u8,
}

struct TextBlock {
    text: String,
    offset: usize,
}

struct Classification {
    outcome: &'static str,
    reason: &'static str,
}

static MEM_CITATION_RE: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"(?is)<oai-mem-citation>.*?</oai-mem-citation>").unwrap());
static FENCED_CODE_RE: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"(?s)```.*?```").unwrap());
static INLINE_CODE_RE: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"`[^`\n]*`").unwrap());
static LINK_TARGET_RE: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"\]\([^\)\n]*\)").unwrap());
static URL_RE: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"https?://\S+").unwrap());
static BLOCK_BREAK_RE: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"\r?\n[ \t]*\r?\n").unwrap());
static QUESTION_RE: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"[?？؟¿]").unwrap());
static QUOTED_TEXT_RE: LazyLock<Regex> = LazyLock::new(|| {
    Regex::new(r#"(?:“[^”\n]*”|‘[^’\n]*’|"[^"\n]*"|'[^'\n]*[?？؟¿][^'\n]*')"#).unwrap()
});
static NON_PROSE_LINE_RE: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"^\s*(?:#{1,6}\s+|>)").unwrap());
static ANSWER_CUE_RE: LazyLock<Regex> = LazyLock::new(|| {
    Regex::new(r"(?i)(?:原因(?:是|在于)|答案(?:是|为)|\bbecause\b|\bthe answer is\b)").unwrap()
});
static SECOND_PERSON_RE: LazyLock<Regex> = LazyLock::new(|| {
    Regex::new(r"(?i)(?:你|您|\b(?:you|your|yours|yourself|yourselves)\b)").unwrap()
});
static REQUIRED_ACTION_RE: LazyLock<Regex> = LazyLock::new(|| {
    Regex::new(r"(?i)(?:请(?:确认|批准|选择|提供|回复|复核|重试|重新)|确认后(?:继续|(?:我|我们)(?:继续|(?:就(?:直接)?|会|再|将)[^。！？\n]{1,40}))|等待(?:你的|您的)?(?:回复|确认|选择|输入)|需要(?:你|您)(?:确认|批准|选择|提供|回复|操作)|需要进一步指令|无法继续|\bplease (?:confirm|approve|choose|select|provide|reply|review|retry)\b|\bwaiting for your (?:reply|confirmation|approval|choice|input)\b|\bneed your (?:confirmation|approval|choice|input|reply)\b|\bcannot continue\b)").unwrap()
});
static COURTESY_RE: LazyLock<Regex> = LazyLock::new(|| {
    Regex::new(r"(?i)(?:如(?:果)?需(?:要)?更多帮助请告诉我|如果你愿意(?:，|,)?请告诉我|\blet me know if you (?:would like|want|need) more help\b|\bif you(?:'d| would) like(?:,)? let me know\b)").unwrap()
});
static RESOLUTION_RE: LazyLock<Regex> = LazyLock::new(|| {
    Regex::new(r"(?i)(?P<all>无需(?:你|您)?(?:回复|操作)|不需要(?:你|您)?(?:回复|操作)|问题已经解决，?无需回复|\bno (?:reply|user action) is required\b|\bno action remains\b|\bthe question (?:is|has been) resolved\b)|(?P<confirmation>无需(?:你|您)?(?:确认|批准)|不需要(?:你|您)?(?:确认|批准)|\bno (?:confirmation|approval) is required\b)|(?P<choice>无需(?:你|您)?选择|不需要(?:你|您)?选择|已经按.+?(?:实施|完成)，?不需要再选择|\bno choice is required\b|\bthe choice (?:is|has been) resolved\b)").unwrap()
});
static DECISION_CUE_RE: LazyLock<Regex> = LazyLock::new(|| {
    Regex::new(r"(?i)(?:选(?:择|用)?|请选择|方案|选项|推荐|\b(?:choose|select|pick|reply|respond|option|recommend)\b)").unwrap()
});
static INLINE_CHOICE_RE: LazyLock<Regex> = LazyLock::new(|| {
    Regex::new(r"(?i)(?:还是|\b(?:or|versus|vs\.?|which)\b)[^\n]{0,100}[?？؟]|[?？؟][^\n]{0,100}(?:还是|\b(?:or|versus|vs\.?|which)\b)").unwrap()
});
static RECOMMEND_RE: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"(?i)(?:推荐|recommend)\s*(?:第\s*)?([A-Z1-9])").unwrap());
static OPTION_LINE_RE: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"(?m)^\s*(?:[-*]\s*)?([A-Z]|[1-9])\s*[.、:：)）]\s*\S+").unwrap());
static UNICODE_CATEGORY_C_RE: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"\p{C}").unwrap());

fn classify_completion(
    message: Option<&str>,
    unfinished_plan: bool,
    unresolved_input: bool,
) -> Classification {
    if unresolved_input {
        return Classification {
            outcome: "waiting",
            reason: "structured_input_request",
        };
    }
    if unfinished_plan {
        return Classification {
            outcome: "waiting",
            reason: "structured_unfinished_plan",
        };
    }
    let Some(message) = message else {
        return Classification {
            outcome: "completed",
            reason: "completion_only",
        };
    };
    if message.len() > CLASSIFIER_STREAM_THRESHOLD_BYTES {
        return classify_completion_streamed(message);
    }
    classify_completion_materialized(message)
}

fn classify_completion_materialized(message: &str) -> Classification {
    let blocks = text_blocks(&message);
    if blocks.is_empty() {
        return Classification {
            outcome: "completed",
            reason: "completion_only",
        };
    }
    let mut unresolved: Vec<Evidence<'_>> = Vec::new();
    let mut saw_resolution = false;
    for evidence in text_evidence(&blocks) {
        if evidence.kind == "text_explicitly_resolved" {
            unresolved.retain(|prior| {
                prior
                    .scopes
                    .iter()
                    .all(|scope| !evidence.scopes.contains(scope))
            });
            saw_resolution = true;
        } else {
            unresolved.push(evidence);
        }
    }
    if let Some(strongest) = unresolved.into_iter().max_by_key(|item| item.strength) {
        return Classification {
            outcome: "waiting",
            reason: strongest.kind,
        };
    }
    if saw_resolution {
        Classification {
            outcome: "completed",
            reason: "text_explicitly_resolved",
        }
    } else {
        Classification {
            outcome: "completed",
            reason: "completion_only",
        }
    }
}

fn text_blocks(message: &str) -> Vec<TextBlock> {
    let mut source = MEM_CITATION_RE.replace_all(message, "").into_owned();
    source = FENCED_CODE_RE.replace_all(&source, "").into_owned();
    source = INLINE_CODE_RE.replace_all(&source, "").into_owned();
    source = LINK_TARGET_RE.replace_all(&source, "]").into_owned();
    source = URL_RE.replace_all(&source, "").into_owned();
    let mut blocks = Vec::new();
    let mut cursor = 0;
    for split in BLOCK_BREAK_RE.find_iter(&source) {
        append_text_block(&mut blocks, &source[cursor..split.start()], cursor);
        cursor = split.end();
    }
    append_text_block(&mut blocks, &source[cursor..], cursor);
    while blocks
        .last()
        .is_some_and(|block| block.text.starts_with("::"))
    {
        blocks.pop();
    }
    blocks
}

fn append_text_block(blocks: &mut Vec<TextBlock>, raw: &str, offset: usize) {
    let trimmed = raw.trim();
    if trimmed.is_empty() {
        return;
    }
    let leading = raw.len() - raw.trim_start().len();
    blocks.push(TextBlock {
        text: trimmed.into(),
        offset: offset + leading,
    });
}

fn prose_block(block: &TextBlock) -> String {
    let mut lines = Vec::new();
    for line in block.text.split('\n') {
        if NON_PROSE_LINE_RE.is_match(line) {
            lines.push(" ".repeat(line.len()));
        } else {
            lines.push(line.to_string());
        }
    }
    let source = lines.join("\n");
    QUOTED_TEXT_RE
        .replace_all(&source, |captures: &regex::Captures<'_>| {
            " ".repeat(captures.get(0).map_or(0, |m| m.as_str().len()))
        })
        .into_owned()
}

fn text_evidence(blocks: &[TextBlock]) -> Vec<Evidence<'static>> {
    text_evidence_with_question_policy(blocks, true)
}

fn text_evidence_with_question_policy(
    blocks: &[TextBlock],
    skip_self_answered: bool,
) -> Vec<Evidence<'static>> {
    let prose: Vec<String> = blocks.iter().map(prose_block).collect();
    let mut evidence = Vec::new();
    let mut questions = Vec::<(usize, Evidence<'static>)>::new();
    for (index, (block, text)) in blocks.iter().zip(prose.iter()).enumerate() {
        let inline_choice = INLINE_CHOICE_RE.is_match(text);
        for question in QUESTION_RE.find_iter(text) {
            let suffix = &text[question.end()..];
            let self_answered = ANSWER_CUE_RE.is_match(suffix)
                && !SECOND_PERSON_RE.is_match(text)
                && !REQUIRED_ACTION_RE.is_match(text)
                && !inline_choice;
            if skip_self_answered && self_answered {
                continue;
            }
            let item = if inline_choice {
                Evidence {
                    offset: block.offset + question.start(),
                    kind: "text_choice_handoff",
                    scopes: &["choice"],
                    strength: 2,
                }
            } else {
                Evidence {
                    offset: block.offset + question.start(),
                    kind: "text_direct_question",
                    scopes: &["reply"],
                    strength: 1,
                }
            };
            evidence.push(item);
            questions.push((index, item));
        }

        let courtesy: Vec<(usize, usize)> = COURTESY_RE
            .find_iter(text)
            .map(|m| (m.start(), m.end()))
            .collect();
        let resolutions: Vec<_> = RESOLUTION_RE
            .captures_iter(text)
            .filter_map(|captures| {
                let found = captures.get(0)?;
                let scopes: &'static [&'static str] = if captures.name("all").is_some() {
                    &["reply", "confirmation", "choice", "input", "action"]
                } else if captures.name("confirmation").is_some() {
                    &["confirmation"]
                } else {
                    &["choice"]
                };
                Some((
                    found.start(),
                    found.end(),
                    Evidence {
                        offset: block.offset + found.start(),
                        kind: "text_explicitly_resolved",
                        scopes,
                        strength: 0,
                    },
                ))
            })
            .collect();
        for (_, _, item) in &resolutions {
            evidence.push(*item);
        }
        for action in REQUIRED_ACTION_RE.find_iter(text) {
            let overlaps = courtesy
                .iter()
                .any(|(start, end)| action.start() < *end && *start < action.end())
                || resolutions
                    .iter()
                    .any(|(start, end, _)| action.start() < *end && *start < action.end());
            if overlaps {
                continue;
            }
            let matched = action.as_str();
            let scope = if Regex::new(r"(?i)确认|批准|confirm|approval|approve")
                .unwrap()
                .is_match(matched)
            {
                "confirmation"
            } else if Regex::new(r"(?i)选择|choose|select|choice")
                .unwrap()
                .is_match(matched)
            {
                "choice"
            } else if Regex::new(r"(?i)提供|输入|provide|input")
                .unwrap()
                .is_match(matched)
            {
                "input"
            } else if Regex::new(r"(?i)回复|reply").unwrap().is_match(matched) {
                "reply"
            } else {
                "action"
            };
            let scopes: &'static [&'static str] = match scope {
                "confirmation" => &["confirmation"],
                "choice" => &["choice"],
                "input" => &["input"],
                "reply" => &["reply"],
                _ => &["action"],
            };
            evidence.push(Evidence {
                offset: block.offset + action.start(),
                kind: "text_required_action",
                scopes,
                strength: 3,
            });
        }
    }

    let mut choice_offsets = HashSet::new();
    for (index, text) in prose.iter().enumerate() {
        let mut labels = Vec::new();
        for option in OPTION_LINE_RE.captures_iter(text) {
            if let Some(label) = option.get(1).map(|m| m.as_str().to_ascii_uppercase()) {
                if !labels.contains(&label) {
                    labels.push(label);
                }
            }
        }
        if labels.len() < 2 {
            continue;
        }
        let start = index.saturating_sub(1);
        let end = (index + 2).min(blocks.len());
        let near_question = questions
            .iter()
            .find(|(block_index, _)| *block_index >= start && *block_index < end);
        let mut recommendation = None;
        let mut cue = None;
        for near in start..end {
            if recommendation.is_none() {
                recommendation = RECOMMEND_RE.captures(&prose[near]).and_then(|captures| {
                    let label = captures.get(1)?.as_str().to_ascii_uppercase();
                    labels
                        .contains(&label)
                        .then(|| (near, captures.get(0).unwrap().start()))
                });
            }
            if cue.is_none() {
                cue = DECISION_CUE_RE
                    .find(&prose[near])
                    .map(|m| (near, m.start()));
            }
        }
        let offset = near_question
            .map(|(_, item)| item.offset)
            .or_else(|| recommendation.map(|(near, pos)| blocks[near].offset + pos))
            .or_else(|| cue.map(|(near, pos)| blocks[near].offset + pos));
        if let Some(offset) = offset {
            choice_offsets.insert(offset);
            evidence.push(Evidence {
                offset,
                kind: "text_choice_handoff",
                scopes: &["choice"],
                strength: 2,
            });
        }
    }
    evidence.retain(|item| {
        item.kind != "text_direct_question" || !choice_offsets.contains(&item.offset)
    });
    evidence.sort_by_key(|item| (item.offset, item.kind));
    evidence
}

const CLASSIFIER_SCOPES: [&str; 5] = ["reply", "confirmation", "choice", "input", "action"];

struct StreamParagraphEvidence {
    questions: [Option<Evidence<'static>>; 5],
    after_resolutions: [Option<Evidence<'static>>; 5],
    resolved: [bool; 5],
    saw_second_person: bool,
    saw_required_action: bool,
    saw_inline_choice: bool,
    last_question_end: Option<usize>,
    last_answer_start: Option<usize>,
    option_labels: [bool; 35],
    recommendation_labels: [bool; 35],
    saw_decision_cue: bool,
}

impl Default for StreamParagraphEvidence {
    fn default() -> Self {
        Self {
            questions: [None; 5],
            after_resolutions: [None; 5],
            resolved: [false; 5],
            saw_second_person: false,
            saw_required_action: false,
            saw_inline_choice: false,
            last_question_end: None,
            last_answer_start: None,
            option_labels: [false; 35],
            recommendation_labels: [false; 35],
            saw_decision_cue: false,
        }
    }
}

#[derive(Default)]
struct StreamCompletionEvidence {
    unresolved: [Option<Evidence<'static>>; 5],
    saw_resolution: bool,
}

fn classify_completion_streamed(message: &str) -> Classification {
    let mut evidence = StreamCompletionEvidence::default();
    let mut paragraph_start = 0;
    let mut ignored_breaks = [
        FENCED_CODE_RE.find_iter(message).peekable(),
        MEM_CITATION_RE.find_iter(message).peekable(),
    ];

    for boundary in BLOCK_BREAK_RE.find_iter(message) {
        for matches in &mut ignored_breaks {
            while matches
                .peek()
                .is_some_and(|found| found.end() <= boundary.start())
            {
                matches.next();
            }
        }
        let hidden_by_removed_block = ignored_breaks.iter_mut().any(|matches| {
            matches.peek().is_some_and(|found| {
                found.start() < boundary.end() && found.end() > boundary.start()
            })
        });
        if hidden_by_removed_block {
            continue;
        }
        scan_classifier_paragraph(&message[paragraph_start..boundary.start()], &mut evidence);
        paragraph_start = boundary.end();
    }
    scan_classifier_paragraph(&message[paragraph_start..], &mut evidence);

    if let Some(strongest) = evidence
        .unresolved
        .into_iter()
        .flatten()
        .max_by_key(|item| item.strength)
    {
        Classification {
            outcome: "waiting",
            reason: strongest.kind,
        }
    } else if evidence.saw_resolution {
        Classification {
            outcome: "completed",
            reason: "text_explicitly_resolved",
        }
    } else {
        Classification {
            outcome: "completed",
            reason: "completion_only",
        }
    }
}

fn scan_classifier_paragraph(paragraph: &str, completion: &mut StreamCompletionEvidence) {
    let mut local = StreamParagraphEvidence::default();
    let mut exclusions = vec![
        MEM_CITATION_RE.find_iter(paragraph).peekable(),
        FENCED_CODE_RE.find_iter(paragraph).peekable(),
        INLINE_CODE_RE.find_iter(paragraph).peekable(),
        LINK_TARGET_RE.find_iter(paragraph).peekable(),
        URL_RE.find_iter(paragraph).peekable(),
        QUOTED_TEXT_RE.find_iter(paragraph).peekable(),
    ];
    let mut cursor = 0;
    loop {
        let next = exclusions
            .iter_mut()
            .filter_map(|matches| matches.peek().copied())
            .min_by_key(|found| found.start());
        let Some(next) = next else {
            scan_visible_classifier_text(&paragraph[cursor..], cursor, &mut local);
            break;
        };
        if next.start() > cursor {
            scan_visible_classifier_text(&paragraph[cursor..next.start()], cursor, &mut local);
        }
        cursor = cursor.max(next.end());
        for matches in &mut exclusions {
            while matches.peek().is_some_and(|found| found.start() < cursor) {
                matches.next();
            }
        }
    }

    if local.saw_inline_choice {
        if let Some(direct) = local.questions[0].take() {
            record_stream_candidate(
                &mut local.questions,
                Evidence {
                    kind: "text_choice_handoff",
                    scopes: &["choice"],
                    strength: 2,
                    ..direct
                },
            );
        }
    } else if !local.saw_second_person
        && !local.saw_required_action
        && local
            .last_question_end
            .zip(local.last_answer_start)
            .is_some_and(|(question_end, answer_start)| answer_start > question_end)
    {
        local.questions[0] = None;
    }
    let option_count = local.option_labels.iter().filter(|found| **found).count();
    let recommended_option = local
        .option_labels
        .iter()
        .zip(local.recommendation_labels.iter())
        .any(|(option, recommended)| *option && *recommended);
    if option_count >= 2
        && (local.questions.iter().any(|candidate| candidate.is_some())
            || local.saw_decision_cue
            || recommended_option)
    {
        record_stream_candidate(
            &mut local.after_resolutions,
            Evidence {
                offset: 0,
                kind: "text_choice_handoff",
                scopes: &["choice"],
                strength: 2,
            },
        );
    }

    for (index, resolved) in local.resolved.into_iter().enumerate() {
        if resolved {
            completion.unresolved[index] = None;
            completion.saw_resolution = true;
        }
    }
    for (index, candidate) in local.questions.into_iter().enumerate() {
        if !local.resolved[index]
            && let Some(candidate) = candidate
        {
            record_stream_candidate(&mut completion.unresolved, candidate);
        }
    }
    for candidate in local.after_resolutions.into_iter().flatten() {
        record_stream_candidate(&mut completion.unresolved, candidate);
    }
}

fn scan_visible_classifier_text(
    text: &str,
    source_offset: usize,
    paragraph: &mut StreamParagraphEvidence,
) {
    let mut line_offset = 0;
    for raw_line in text.split_inclusive('\n') {
        let line = raw_line.trim_end_matches(['\r', '\n']);
        if !NON_PROSE_LINE_RE.is_match(line) {
            paragraph.saw_second_person |= SECOND_PERSON_RE.is_match(line);
            paragraph.saw_required_action |= REQUIRED_ACTION_RE.is_match(line);
            paragraph.saw_inline_choice |= INLINE_CHOICE_RE.is_match(line);
            for question in QUESTION_RE.find_iter(line) {
                paragraph.last_question_end = Some(source_offset + line_offset + question.end());
            }
            for answer in ANSWER_CUE_RE.find_iter(line) {
                paragraph.last_answer_start = Some(source_offset + line_offset + answer.start());
            }
            paragraph.saw_decision_cue |= DECISION_CUE_RE.is_match(line);
            for option in OPTION_LINE_RE.captures_iter(line) {
                if let Some(label) = option
                    .get(1)
                    .and_then(|capture| capture.as_str().chars().next())
                    && let Some(index) = option_label_index(label)
                {
                    paragraph.option_labels[index] = true;
                }
            }
            if let Some(label) = RECOMMEND_RE
                .captures(line)
                .and_then(|captures| captures.get(1))
                .and_then(|capture| capture.as_str().chars().next())
                && let Some(index) = option_label_index(label)
            {
                paragraph.recommendation_labels[index] = true;
            }
            scan_classifier_line_windows(line, paragraph);
        }
        line_offset += raw_line.len();
    }
}

fn scan_classifier_line_windows(line: &str, paragraph: &mut StreamParagraphEvidence) {
    if line.is_empty() {
        return;
    }
    let core_width = CLASSIFIER_WINDOW_BYTES - 2 * CLASSIFIER_WINDOW_OVERLAP_BYTES;
    let mut core_start = 0;
    while core_start < line.len() {
        let mut core_end = (core_start + core_width).min(line.len());
        core_end = ceil_char_boundary(line, core_end);
        let window_start = floor_char_boundary(
            line,
            core_start.saturating_sub(CLASSIFIER_WINDOW_OVERLAP_BYTES),
        );
        let window_end = ceil_char_boundary(
            line,
            (core_end + CLASSIFIER_WINDOW_OVERLAP_BYTES).min(line.len()),
        );
        let window = &line[window_start..window_end];
        let core_local_start = core_start.saturating_sub(window_start);
        let core_local_end = core_end.saturating_sub(window_start);
        let blocks = text_blocks(window);
        for item in text_evidence_with_question_policy(&blocks, false) {
            // Trimming in text_blocks can move an evidence offset by leading
            // whitespace; a small guard preserves markers near a window edge.
            let guarded_start = core_local_start.saturating_sub(256);
            let guarded_end = (core_local_end + 256).min(window.len());
            if item.offset >= guarded_start && item.offset < guarded_end {
                record_paragraph_evidence(paragraph, item);
            }
        }
        if core_end >= line.len() {
            break;
        }
        core_start = core_end;
    }
}

fn record_paragraph_evidence(paragraph: &mut StreamParagraphEvidence, item: Evidence<'static>) {
    if item.kind == "text_explicitly_resolved" {
        for scope in item.scopes {
            if let Some(index) = classifier_scope_index(scope) {
                paragraph.resolved[index] = true;
            }
        }
        return;
    }
    if matches!(item.kind, "text_direct_question" | "text_choice_handoff") {
        record_stream_candidate(&mut paragraph.questions, item);
    } else {
        record_stream_candidate(&mut paragraph.after_resolutions, item);
    }
}

fn record_stream_candidate(
    candidates: &mut [Option<Evidence<'static>>; 5],
    item: Evidence<'static>,
) {
    for scope in item.scopes {
        let Some(index) = classifier_scope_index(scope) else {
            continue;
        };
        if candidates[index].is_none_or(|previous| item.strength > previous.strength) {
            candidates[index] = Some(item);
        }
    }
}

fn classifier_scope_index(scope: &str) -> Option<usize> {
    CLASSIFIER_SCOPES
        .iter()
        .position(|candidate| *candidate == scope)
}

fn option_label_index(label: char) -> Option<usize> {
    match label.to_ascii_uppercase() {
        'A'..='Z' => Some((label.to_ascii_uppercase() as usize) - ('A' as usize)),
        '1'..='9' => Some(26 + (label as usize) - ('1' as usize)),
        _ => None,
    }
}

fn floor_char_boundary(text: &str, mut index: usize) -> usize {
    index = index.min(text.len());
    while !text.is_char_boundary(index) {
        index -= 1;
    }
    index
}

fn ceil_char_boundary(text: &str, mut index: usize) -> usize {
    index = index.min(text.len());
    while !text.is_char_boundary(index) {
        index += 1;
    }
    index
}

#[derive(Clone, Debug, Serialize, Deserialize)]
struct TraeCursor {
    path: String,
    inode: u64,
    offset: u64,
    mtime_ns: u128,
    #[serde(default)]
    line_start_offset: Option<u64>,
    #[serde(default)]
    discarding_line: bool,
    #[serde(default)]
    model: String,
    #[serde(default)]
    effort: String,
    #[serde(default)]
    model_updated_at: f64,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
struct TraeTask {
    session_id: String,
    task_id: String,
    #[serde(default = "default_project")]
    project: String,
    #[serde(default = "default_trae_title")]
    title: String,
    #[serde(default)]
    detail: String,
    state: String,
    updated_at: f64,
    #[serde(default)]
    model: String,
    #[serde(default)]
    effort: String,
}

fn default_trae_title() -> String {
    "Trae task".into()
}

#[derive(Clone, Debug, Serialize, Deserialize)]
struct TraeLedger {
    #[serde(default)]
    version: u8,
    #[serde(default)]
    model: Option<String>,
    #[serde(default)]
    effort: Option<String>,
    #[serde(default)]
    model_updated_at: f64,
    #[serde(default)]
    revision: u64,
    #[serde(default)]
    tasks: Vec<TraeTask>,
}

impl Default for TraeLedger {
    fn default() -> Self {
        Self {
            version: 4,
            model: None,
            effort: None,
            model_updated_at: 0.0,
            revision: 0,
            tasks: Vec::new(),
        }
    }
}

#[derive(Clone, Debug, Default)]
struct TraeSnapshot {
    online: bool,
    process_state: String,
    model: String,
    effort: String,
    revision: u64,
    pending: usize,
    running: usize,
    truncated: bool,
    tasks: Vec<TraeTask>,
}

impl TraeSnapshot {
    fn unavailable() -> Self {
        Self {
            online: false,
            process_state: "unknown".into(),
            model: "--".into(),
            effort: "--".into(),
            ..Self::default()
        }
    }
    fn to_json(&self) -> Value {
        json!({
            "trae_online": self.online,
            "trae_process_state": if self.process_state.is_empty() { "off" } else { &self.process_state },
            "trae_model": if self.model.is_empty() { "--" } else { &self.model },
            "trae_effort": if self.effort.is_empty() { "--" } else { &self.effort },
            "trae_revision": self.revision,
            "trae_pending_count": self.pending,
            "trae_running_count": self.running,
            "trae_tasks_truncated": self.truncated,
            "trae_tasks": self.tasks.iter().map(|task| {
                let timestamp = DateTime::<Utc>::from_timestamp(task.updated_at.floor() as i64, 0)
                    .map(|value| value.with_timezone(&Local).format("%H:%M").to_string()).unwrap_or_else(|| "--:--".into());
                json!({
                    "id": trae_task_route_id(&task.session_id, &task.task_id),
                    "project": clean_display(&task.project, 32),
                    "title": clean_display(&task.title, 48),
                    "detail": clean_display(&task.detail, 72),
                    "time": timestamp,
                    "state": task.state,
                    "model": if task.model.is_empty() { if self.model.is_empty() { "--" } else { &self.model } } else { &task.model },
                    "effort": if task.effort.is_empty() { if self.effort.is_empty() { "--" } else { &self.effort } } else { &task.effort },
                })
            }).collect::<Vec<_>>(),
        })
    }
}

struct TraeTracker {
    root: Option<PathBuf>,
    cursor_path: PathBuf,
    ledger_path: PathBuf,
    cursors: HashMap<String, TraeCursor>,
    ledger: TraeLedger,
    session_titles: HashMap<String, String>,
    title_retry_after: HashMap<String, std::time::Instant>,
    title_scan_cursors: HashMap<String, TraeTitleScanCursor>,
    title_scan_targets: HashSet<String>,
    title_scan_active: bool,
}

#[derive(Default)]
struct TraeTitleScanCursor {
    inode: u64,
    offset: u64,
    partial_line: Vec<u8>,
    discarding_line: bool,
}

impl TraeTracker {
    fn new(root: Option<PathBuf>, cursor_path: PathBuf, ledger_path: PathBuf) -> Self {
        let cursors = load_trae_cursors(&cursor_path);
        let ledger = load_trae_ledger(&ledger_path);
        Self {
            root,
            cursor_path,
            ledger_path,
            cursors,
            ledger,
            session_titles: HashMap::new(),
            title_retry_after: HashMap::new(),
            title_scan_cursors: HashMap::new(),
            title_scan_targets: HashSet::new(),
            title_scan_active: false,
        }
    }

    fn import_legacy_state_dir(&mut self, legacy_dir: &Path) -> Result<()> {
        let mut save_failed = false;
        if !self.cursor_path.exists() {
            if let Some(value) = read_bounded_json(&legacy_dir.join(".stopwatch_cursor.json")) {
                if let Some(cursors) = decode_trae_cursors(value) {
                    self.cursors = cursors;
                    let cursor_value = json!({ "version": 2, "cursors": self.cursors });
                    if atomic_json(&self.cursor_path, &cursor_value).is_err() {
                        save_failed = true;
                    }
                }
            }
        }
        if !self.ledger_path.exists() {
            if let Some(value) = read_bounded_json(&legacy_dir.join(".stopwatch_tasks.json")) {
                if let Some(ledger) = decode_trae_ledger(value) {
                    self.ledger = ledger;
                    let ledger_value = serde_json::to_value(&self.ledger)?;
                    if atomic_json(&self.ledger_path, &ledger_value).is_err() {
                        save_failed = true;
                    }
                }
            }
        }
        if save_failed {
            anyhow::bail!("persist imported TRAE state to agent-owned files failed");
        }
        Ok(())
    }

    fn refresh(&mut self) -> Result<TraeSnapshot> {
        let Some(root) = self.root.as_deref() else {
            return Ok(TraeSnapshot::unavailable());
        };
        let process_state = trae_process_state(root);
        if process_state != "live" {
            return Ok(TraeSnapshot {
                online: false,
                process_state,
                model: "--".into(),
                effort: "--".into(),
                revision: 0,
                ..TraeSnapshot::default()
            });
        }

        let logs_root = root.join("logs");
        let logs = trae_log_paths(&logs_root);
        let mut changed = false;
        let mut activity_events = Vec::new();
        let mut model_events = Vec::new();
        for path in logs {
            let Ok(metadata) = fs::metadata(&path) else {
                continue;
            };
            let key = path.display().to_string();
            let inode = metadata_inode(&metadata);
            let mtime_ns = metadata_mtime_ns(&metadata);
            let cursor = self.cursors.get(&key).cloned();
            let reset = cursor.as_ref().is_none_or(|cursor| {
                cursor.path != key || cursor.inode != inode || metadata.len() < cursor.offset
            });
            if reset {
                if let Some(event) = latest_trae_model_from_tail(&path, metadata.len())
                    .ok()
                    .flatten()
                {
                    let model_at = event_time_float(&event, mtime_ns, 0);
                    model_events.push((model_at, key.clone(), 0usize, event.clone()));
                    apply_trae_event(&mut self.ledger.tasks, &event, model_at, None, None);
                }
                self.cursors.insert(
                    key.clone(),
                    TraeCursor {
                        path: key,
                        inode,
                        offset: metadata.len(),
                        mtime_ns,
                        line_start_offset: None,
                        discarding_line: false,
                        model: String::new(),
                        effort: String::new(),
                        model_updated_at: 0.0,
                    },
                );
                continue;
            }
            let mut current = cursor.unwrap();
            let local_model = if current.model.is_empty() {
                let complete_offset = current.line_start_offset.unwrap_or(current.offset);
                latest_trae_model_from_tail(&path, complete_offset)
                    .ok()
                    .flatten()
            } else {
                Some(TraeEvent {
                    kind: "model",
                    session_id: None,
                    task_id: None,
                    model: Some(current.model.clone()),
                    effort: (!current.effort.is_empty()).then(|| current.effort.clone()),
                    occurred_at: (current.model_updated_at > 0.0)
                        .then_some(current.model_updated_at),
                })
            };
            let model_updated = local_model
                .as_ref()
                .map(|event| event_time_float(event, current.mtime_ns, 0))
                .unwrap_or(0.0);
            if let Some(event) = local_model.as_ref() {
                model_events.push((model_updated, key.clone(), 0, event.clone()));
                changed |=
                    apply_trae_event(&mut self.ledger.tasks, event, model_updated, None, None);
            }
            let CompleteLineBatch {
                lines,
                next_offset: new_offset,
                line_start_offset,
                discarding_line,
                ..
            } = read_complete_lines(
                &path,
                current.offset,
                current.line_start_offset,
                current.discarding_line,
            );
            for (line_index, line) in lines.into_iter().enumerate() {
                let line = String::from_utf8_lossy(&line);
                let Some(event) = parse_trae_event(&line) else {
                    continue;
                };
                if event.kind == "model" {
                    current.model = event.model.clone().unwrap_or_default();
                    current.effort = event.effort.clone().unwrap_or_default();
                    current.model_updated_at = event_time_float(&event, mtime_ns, line_index);
                    model_events.push((
                        current.model_updated_at,
                        key.clone(),
                        line_index,
                        event.clone(),
                    ));
                    if event.session_id.is_some() && event.task_id.is_some() {
                        activity_events.push((
                            current.model_updated_at,
                            key.clone(),
                            line_index,
                            event.clone(),
                            Some(event.clone()),
                        ));
                    }
                } else {
                    activity_events.push((
                        event_time_float(&event, mtime_ns, line_index),
                        key.clone(),
                        line_index,
                        event,
                        local_model.clone(),
                    ));
                }
            }
            current.offset = new_offset;
            current.line_start_offset = line_start_offset;
            current.discarding_line = discarding_line;
            current.mtime_ns = mtime_ns;
            self.cursors.insert(key, current);
        }

        if let Some((observed, _, _, event)) = model_events.into_iter().max_by(|a, b| {
            a.0.total_cmp(&b.0)
                .then_with(|| a.1.cmp(&b.1))
                .then_with(|| a.2.cmp(&b.2))
        }) {
            let next = (event.model.clone(), event.effort.clone());
            if observed > self.ledger.model_updated_at
                || (self.ledger.model.is_none() && observed == self.ledger.model_updated_at)
            {
                changed |= self.ledger.model != next.0
                    || self.ledger.effort != next.1
                    || self.ledger.model_updated_at != observed;
                self.ledger.model = next.0;
                self.ledger.effort = next.1;
                self.ledger.model_updated_at = observed;
            }
        }
        activity_events.sort_by(|a, b| {
            a.0.total_cmp(&b.0)
                .then_with(|| a.1.cmp(&b.1))
                .then_with(|| a.2.cmp(&b.2))
        });
        for (when, _, _, event, model_event) in activity_events {
            changed |= apply_trae_event(
                &mut self.ledger.tasks,
                &event,
                when,
                model_event.as_ref().and_then(|e| e.model.as_deref()),
                model_event.as_ref().and_then(|e| e.effort.as_deref()),
            );
        }

        let workspace_storage = root.join("User/workspaceStorage");
        let fallback_project = workspace_project(&workspace_storage);
        let catalog = trae_model_catalog(&workspace_storage);
        let mut metadata = trae_current_agent_metadata(root);
        let requested_sessions: HashSet<String> = self
            .ledger
            .tasks
            .iter()
            .filter(|task| matches!(task.state.as_str(), "pending" | "running"))
            .map(|task| task.session_id.clone())
            .collect();
        let now = std::time::Instant::now();
        let missing_titles: HashSet<String> = requested_sessions
            .iter()
            .filter(|session| {
                !self.session_titles.contains_key(*session)
                    && self
                        .title_retry_after
                        .get(*session)
                        .is_none_or(|retry| now >= *retry)
            })
            .cloned()
            .collect();
        if self.title_scan_active {
            let new_targets: Vec<_> = missing_titles
                .iter()
                .filter(|session| !self.title_scan_targets.contains(*session))
                .cloned()
                .collect();
            if !new_targets.is_empty() {
                self.title_scan_targets.extend(new_targets);
                self.title_scan_cursors.clear();
            }
        } else if !missing_titles.is_empty() {
            self.title_scan_targets = missing_titles;
            self.title_scan_cursors.clear();
            self.title_scan_active = true;
        }
        if self.title_scan_active {
            let (found, finished) = trae_session_titles_step(
                &logs_root,
                &self.title_scan_targets,
                &mut self.title_scan_cursors,
                TRAE_TITLE_SCAN_BYTES_PER_POLL,
            );
            self.session_titles.extend(found);
            self.title_scan_targets
                .retain(|session| !self.session_titles.contains_key(session));
            if self.title_scan_targets.is_empty() || finished {
                for session in &self.title_scan_targets {
                    self.title_retry_after
                        .insert(session.clone(), now + Duration::from_secs(30));
                }
                self.title_scan_targets.clear();
                self.title_scan_cursors.clear();
                self.title_scan_active = false;
            }
            self.title_retry_after
                .retain(|session, _| !self.session_titles.contains_key(session));
        }
        metadata.extend(trae_real_session_metadata(
            root,
            &self.ledger.tasks,
            &catalog,
            &self.session_titles,
        ));
        metadata = newest_trae_metadata(metadata);
        for item in &metadata {
            changed |= enrich_trae_tasks(&mut self.ledger.tasks, &item, &fallback_project);
        }
        for task in &mut self.ledger.tasks {
            if let Some((display, effort)) = catalog.get(&task.model) {
                if task.model != *display {
                    task.model = display.clone();
                    changed = true;
                }
                if task.effort.is_empty() && !effort.is_empty() {
                    task.effort = effort.clone();
                    changed = true;
                }
            }
        }
        let latest_task = self
            .ledger
            .tasks
            .iter()
            .filter(|task| !task.model.is_empty())
            .max_by(|a, b| a.updated_at.total_cmp(&b.updated_at));
        let mut model = self.ledger.model.clone().unwrap_or_default();
        let mut effort = self.ledger.effort.clone().unwrap_or_default();
        if model.is_empty() {
            if let Some(task) = latest_task {
                model = task.model.clone();
                effort = task.effort.clone();
            }
        }
        if let Some(metadata) = metadata
            .iter()
            .filter(|item| !item.selected_model.is_empty())
            .max_by(|left, right| {
                left.observed_at
                    .total_cmp(&right.observed_at)
                    .then_with(|| left.session_id.cmp(&right.session_id))
                    .then_with(|| left.task_id.cmp(&right.task_id))
            })
            .filter(|item| item.observed_at > self.ledger.model_updated_at)
        {
            model = metadata.selected_model.clone();
            effort = metadata.selected_effort.clone();
        } else if let Some((display, default_effort)) = catalog.get(&model) {
            model = display.clone();
            if effort.is_empty() {
                effort = default_effort.clone();
            }
        }
        self.ledger.tasks = retain_trae_tasks(std::mem::take(&mut self.ledger.tasks));
        if changed {
            self.ledger.revision += 1;
        }
        self.save()?;
        let mut active: Vec<TraeTask> = self
            .ledger
            .tasks
            .iter()
            .filter(|task| matches!(task.state.as_str(), "pending" | "running"))
            .cloned()
            .collect();
        active.sort_by(|a, b| {
            let state_order = |value: &str| if value == "pending" { 0 } else { 1 };
            state_order(&a.state)
                .cmp(&state_order(&b.state))
                .then_with(|| b.updated_at.total_cmp(&a.updated_at))
                .then_with(|| a.session_id.cmp(&b.session_id))
                .then_with(|| a.task_id.cmp(&b.task_id))
        });
        let pending = active.iter().filter(|task| task.state == "pending").count();
        let running = active.iter().filter(|task| task.state == "running").count();
        let truncated = active.len() > 4;
        active.truncate(4);
        Ok(TraeSnapshot {
            online: true,
            process_state: "live".into(),
            model,
            effort,
            revision: self.ledger.revision,
            pending,
            running,
            truncated,
            tasks: active,
        })
    }

    fn save(&self) -> Result<()> {
        let cursor_value = json!({ "version": 2, "cursors": self.cursors });
        atomic_json(&self.cursor_path, &cursor_value)?;
        let ledger_value = serde_json::to_value(&self.ledger)?;
        atomic_json(&self.ledger_path, &ledger_value)?;
        Ok(())
    }
}

fn read_bounded_json(path: &Path) -> Option<Value> {
    let metadata = fs::metadata(path).ok()?;
    if !metadata.is_file() || metadata.len() > MAX_LEGACY_LEDGER_BYTES {
        return None;
    }
    let mut file = File::open(path).ok()?;
    let mut bytes = Vec::new();
    std::io::Read::by_ref(&mut file)
        .take(MAX_LEGACY_LEDGER_BYTES + 1)
        .read_to_end(&mut bytes)
        .ok()?;
    if bytes.len() as u64 > MAX_LEGACY_LEDGER_BYTES {
        return None;
    }
    serde_json::from_slice(&bytes).ok()
}

fn decode_trae_cursors(value: Value) -> Option<HashMap<String, TraeCursor>> {
    let mut output = HashMap::new();
    if value.get("version").and_then(Value::as_u64) == Some(2) {
        let cursors = value.get("cursors")?.as_object()?;
        for raw in cursors.values() {
            if let Ok(cursor) = serde_json::from_value::<TraeCursor>(raw.clone()) {
                output.insert(cursor.path.clone(), cursor);
            }
        }
    } else {
        let cursor = serde_json::from_value::<TraeCursor>(value).ok()?;
        output.insert(cursor.path.clone(), cursor);
    }
    Some(output)
}

fn load_trae_cursors(path: &Path) -> HashMap<String, TraeCursor> {
    read_bounded_json(path)
        .and_then(decode_trae_cursors)
        .unwrap_or_default()
}

fn decode_trae_ledger(value: Value) -> Option<TraeLedger> {
    let mut ledger = serde_json::from_value::<TraeLedger>(value).ok()?;
    if ledger.version != 4 {
        ledger.tasks.clear();
        ledger.version = 4;
        ledger.revision = ledger.revision.saturating_add(1);
    }
    ledger
        .tasks
        .retain(|task| matches!(task.state.as_str(), "pending" | "running" | "completed"));
    Some(ledger)
}

fn load_trae_ledger(path: &Path) -> TraeLedger {
    read_bounded_json(path)
        .and_then(decode_trae_ledger)
        .unwrap_or_default()
}

fn retain_trae_tasks(tasks: Vec<TraeTask>) -> Vec<TraeTask> {
    let mut active: Vec<_> = tasks
        .iter()
        .filter(|task| task.state != "completed")
        .cloned()
        .collect();
    let mut completed: Vec<_> = tasks
        .into_iter()
        .filter(|task| task.state == "completed")
        .collect();
    active.sort_by(|a, b| {
        b.updated_at
            .total_cmp(&a.updated_at)
            .then_with(|| a.session_id.cmp(&b.session_id))
            .then_with(|| a.task_id.cmp(&b.task_id))
    });
    completed.sort_by(|a, b| {
        b.updated_at
            .total_cmp(&a.updated_at)
            .then_with(|| a.session_id.cmp(&b.session_id))
            .then_with(|| a.task_id.cmp(&b.task_id))
    });
    completed.truncate(TRAE_MAX_TASKS.saturating_sub(active.len()));
    active.extend(completed);
    active
}

#[derive(Clone, Debug)]
struct TraeEvent {
    kind: &'static str,
    session_id: Option<String>,
    task_id: Option<String>,
    model: Option<String>,
    effort: Option<String>,
    occurred_at: Option<f64>,
}

static TRAE_TIMESTAMP_RE: LazyLock<Regex> = LazyLock::new(|| {
    Regex::new(r"^(\d{4}-\d{2}-\d{2}[ T]\d{2}:\d{2}:\d{2}(?:[.,]\d{1,6})?(?:Z|[+-]\d{2}:\d{2})?)")
        .unwrap()
});
static TRAE_SESSION_RE: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"\bsession_id=(?P<value>[A-Za-z0-9_.:-]{1,128})\b").unwrap());
static TRAE_TASK_RE: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"\btask_id=(?P<value>[A-Za-z0-9_.:-]{1,128})\b").unwrap());
static TRAE_MODEL_RE: LazyLock<Regex> = LazyLock::new(|| {
    Regex::new(r#"model_info:\s*CustomModel\s*\{[^}\r\n]{0,512}?display_model_name:\s*Some\("(?P<model>[^"\r\n]{1,64})"\)[^}\r\n]{0,512}?reasoning_effort:\s*(?:(?:Some\("(?P<effort>[^"\r\n]{1,32})"\))|None)[^}\r\n]{0,512}?\}"#).unwrap()
});
static TRAE_TIMING_MODEL_RE: LazyLock<Regex> = LazyLock::new(|| {
    Regex::new(r#"TimingCostOriginEvent[^\r\n]{0,4096}?\\"provider_model_name\\":\\"(?P<model>[^"\\\r\n]{1,64})\\""#).unwrap()
});
static TRAE_TASK_STATUS_RE: LazyLock<Regex> = LazyLock::new(|| {
    Regex::new(r"(?i)ai_agent::domain::task::cloud_service:\s+TASK:\s+[^\r\n]{0,1024}?\bstatus=(?P<status>Created|Completed)\b").unwrap()
});
static TRAE_MODEL_PREFIX_RE: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^\d+_-_").unwrap());

fn parse_trae_event(line: &str) -> Option<TraeEvent> {
    if line.is_empty() {
        return None;
    }
    let occurred_at = TRAE_TIMESTAMP_RE
        .captures(line)
        .and_then(|captures| captures.get(1))
        .and_then(|m| {
            let value = m.as_str().replace(',', ".");
            DateTime::parse_from_rfc3339(&value)
                .ok()
                .map(|time| time.timestamp() as f64)
                .or_else(|| {
                    chrono::NaiveDateTime::parse_from_str(&value, "%Y-%m-%d %H:%M:%S%.f")
                        .ok()
                        .map(|time| {
                            time.and_local_timezone(Local)
                                .single()
                                .map(|dt| dt.timestamp() as f64)
                        })
                        .flatten()
                })
        });
    if let Some(captures) = TRAE_TIMING_MODEL_RE.captures(line) {
        return Some(TraeEvent {
            kind: "model",
            session_id: TRAE_SESSION_RE
                .captures(line)
                .and_then(|c| c.name("value"))
                .map(|m| m.as_str().to_string()),
            task_id: TRAE_TASK_RE
                .captures(line)
                .and_then(|c| c.name("value"))
                .map(|m| m.as_str().to_string()),
            model: captures.name("model").map(|m| m.as_str().to_string()),
            effort: None,
            occurred_at,
        });
    }
    if let Some(captures) = TRAE_MODEL_RE.captures(line) {
        return Some(TraeEvent {
            kind: "model",
            session_id: None,
            task_id: None,
            model: captures.name("model").map(|m| m.as_str().to_string()),
            effort: captures.name("effort").map(|m| m.as_str().to_string()),
            occurred_at,
        });
    }
    let kind = if let Some(captures) = TRAE_TASK_STATUS_RE.captures(line) {
        if captures
            .name("status")
            .is_some_and(|m| m.as_str().eq_ignore_ascii_case("created"))
        {
            "started"
        } else {
            "completed"
        }
    } else if line.contains("progress_notice") {
        "progress"
    } else if line.contains("ToolCall arrived name=AskUserQuestion") {
        "pending"
    } else if line.contains("[append_resume_listener] exiting") {
        "completed"
    } else {
        return None;
    };
    let session_id = TRAE_SESSION_RE
        .captures(line)
        .and_then(|c| c.name("value"))
        .map(|m| m.as_str().to_string());
    let task_id = TRAE_TASK_RE
        .captures(line)
        .and_then(|c| c.name("value"))
        .map(|m| m.as_str().to_string());
    Some(TraeEvent {
        kind,
        session_id,
        task_id,
        model: None,
        effort: None,
        occurred_at,
    })
}

fn apply_trae_event(
    tasks: &mut Vec<TraeTask>,
    event: &TraeEvent,
    now: f64,
    model: Option<&str>,
    effort: Option<&str>,
) -> bool {
    let (Some(session_id), Some(task_id)) = (event.session_id.as_ref(), event.task_id.as_ref())
    else {
        return false;
    };
    let index = tasks
        .iter()
        .position(|task| &task.session_id == session_id && &task.task_id == task_id);
    if event.kind == "model" {
        let Some(index) = index else {
            return false;
        };
        let Some(model) = event.model.as_ref() else {
            return false;
        };
        let task = &mut tasks[index];
        let next_effort = event.effort.clone().unwrap_or_else(|| task.effort.clone());
        if task.model.as_str() == model.as_str() && task.effort == next_effort {
            return false;
        }
        task.model = model.clone();
        task.effort = next_effort;
        return true;
    }
    if event.kind == "progress" && index.is_none() {
        return false;
    }
    if event.kind == "completed" && index.is_some_and(|i| tasks[i].state == "pending") {
        return false;
    }
    let next_state = if event.kind == "completed" {
        "completed"
    } else if event.kind == "pending" {
        "pending"
    } else {
        "running"
    };
    if let Some(index) = index {
        let task = &mut tasks[index];
        let next_model = model
            .map(str::to_string)
            .unwrap_or_else(|| task.model.clone());
        let next_effort = if model.is_some() {
            effort.unwrap_or("").to_string()
        } else {
            task.effort.clone()
        };
        let changed = task.state != next_state
            || task.updated_at != now
            || task.model != next_model
            || task.effort != next_effort;
        task.state = next_state.into();
        task.updated_at = now;
        task.model = next_model;
        task.effort = next_effort;
        changed
    } else {
        tasks.push(TraeTask {
            session_id: session_id.clone(),
            task_id: task_id.clone(),
            project: "--".into(),
            title: default_trae_title(),
            detail: String::new(),
            state: next_state.into(),
            updated_at: now,
            model: model.unwrap_or_default().into(),
            effort: if model.is_some() {
                effort.unwrap_or_default().into()
            } else {
                String::new()
            },
        });
        true
    }
}

fn event_time_float(event: &TraeEvent, mtime_ns: u128, line_index: usize) -> f64 {
    event
        .occurred_at
        .unwrap_or((mtime_ns as f64 / 1_000_000_000.0) + (line_index as f64 / 1_000_000.0))
}

fn trae_log_paths(root: &Path) -> Vec<PathBuf> {
    if root.is_file() {
        return vec![root.to_path_buf()];
    }
    let mut pending = vec![(root.to_path_buf(), 0usize)];
    let mut output = Vec::new();
    while output.len() < TRAE_MAX_LOG_FILES {
        let Some((directory, depth)) = pending.pop() else {
            break;
        };
        if depth > 6 {
            continue;
        }
        let Ok(entries) = fs::read_dir(directory) else {
            continue;
        };
        for entry in entries.flatten() {
            let path = entry.path();
            if entry.file_type().ok().is_some_and(|kind| kind.is_dir()) {
                pending.push((path, depth + 1));
            } else if path.extension().is_some_and(|ext| ext == "log") {
                output.push(path);
            }
            if output.len() >= TRAE_MAX_LOG_FILES {
                break;
            }
        }
    }
    output.sort();
    output
}

fn metadata_inode(metadata: &fs::Metadata) -> u64 {
    #[cfg(unix)]
    {
        use std::os::unix::fs::MetadataExt;
        metadata.ino()
    }
    #[cfg(not(unix))]
    {
        metadata.len()
    }
}

fn metadata_mtime_ns(metadata: &fs::Metadata) -> u128 {
    metadata
        .modified()
        .ok()
        .and_then(|time| time.duration_since(UNIX_EPOCH).ok())
        .map(|value| value.as_nanos())
        .unwrap_or(0)
}

fn latest_trae_model_from_tail(path: &Path, size: u64) -> std::io::Result<Option<TraeEvent>> {
    let start = size.saturating_sub(256 * 1024);
    let mut file = File::open(path)?;
    file.seek(SeekFrom::Start(start))?;
    let mut bytes = Vec::new();
    file.read_to_end(&mut bytes)?;
    if start > 0 {
        let first = bytes.iter().position(|byte| *byte == b'\n');
        if let Some(index) = first {
            bytes.drain(..=index);
        } else {
            bytes.clear();
        }
    }
    let text = String::from_utf8_lossy(&bytes);
    Ok(text
        .lines()
        .filter_map(parse_trae_event)
        .filter(|event| event.kind == "model")
        .last())
}

fn trae_process_state(root: &Path) -> String {
    let lock = root.join("code.lock");
    let Ok(text) = fs::read_to_string(lock) else {
        return "off".into();
    };
    let Ok(pid) = text.trim().parse::<u32>() else {
        return "unknown".into();
    };
    if pid == 0 {
        return "unknown".into();
    }
    let mut system = sysinfo::System::new();
    system.refresh_processes(sysinfo::ProcessesToUpdate::All, true);
    let process_id = sysinfo::Pid::from_u32(pid);
    if system.process(process_id).is_some() {
        "live".into()
    } else {
        "off".into()
    }
}

fn trae_task_route_id(session: &str, task: &str) -> String {
    let mut digest = Sha256::new();
    digest.update(session.as_bytes());
    digest.update([0]);
    digest.update(task.as_bytes());
    hex::encode(&digest.finalize()[..8])
}

fn clean_display(value: &str, max_chars: usize) -> String {
    let cleaned: String = value
        .chars()
        .map(|ch| if is_unicode_category_c(ch) { ' ' } else { ch })
        .collect();
    cleaned
        .split_whitespace()
        .collect::<Vec<_>>()
        .join(" ")
        .chars()
        .take(max_chars)
        .collect()
}

fn is_unicode_category_c(ch: char) -> bool {
    let mut encoded = [0; 4];
    UNICODE_CATEGORY_C_RE.is_match(ch.encode_utf8(&mut encoded))
}

fn project_name(value: &str) -> String {
    let value = value.trim();
    if value.is_empty() {
        return String::new();
    }
    let before_fragment = value.split('#').next().unwrap_or(value);
    let scheme_end = before_fragment.find(':').filter(|index| {
        let prefix = &before_fragment[..*index];
        !prefix.is_empty()
            && prefix.as_bytes()[0].is_ascii_alphabetic()
            && prefix
                .bytes()
                .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'+' | b'-' | b'.'))
    });
    let path = if let Some(index) = scheme_end {
        let remainder = &before_fragment[index + 1..];
        if let Some(authority) = remainder.strip_prefix("//") {
            authority
                .find('/')
                .map(|slash| &authority[slash..])
                .unwrap_or("")
        } else {
            remainder
        }
    } else {
        before_fragment
    };
    let path = path.split('?').next().unwrap_or(path).trim_end_matches('/');
    let decoded = percent_decode_lossy(path);
    let name = decoded.rsplit('/').next().unwrap_or(&decoded);
    clean_display(name, 32)
}

fn percent_decode_lossy(value: &str) -> String {
    fn hex(byte: u8) -> Option<u8> {
        match byte {
            b'0'..=b'9' => Some(byte - b'0'),
            b'a'..=b'f' => Some(byte - b'a' + 10),
            b'A'..=b'F' => Some(byte - b'A' + 10),
            _ => None,
        }
    }
    let bytes = value.as_bytes();
    let mut decoded = Vec::with_capacity(bytes.len());
    let mut index = 0;
    while index < bytes.len() {
        if bytes[index] == b'%' && index + 2 < bytes.len() {
            if let (Some(high), Some(low)) = (hex(bytes[index + 1]), hex(bytes[index + 2])) {
                decoded.push(high * 16 + low);
                index += 3;
                continue;
            }
        }
        decoded.push(bytes[index]);
        index += 1;
    }
    String::from_utf8_lossy(&decoded).into_owned()
}

#[derive(Clone, Debug, Default)]
struct TraeMetadata {
    session_id: String,
    task_id: String,
    selected_model: String,
    selected_effort: String,
    project_path: String,
    task_title: String,
    task_detail: String,
    state: String,
    observed_at: f64,
}

fn trae_current_agent_metadata(root: &Path) -> Vec<TraeMetadata> {
    let mut candidates = Vec::new();
    let sandbox = root.join("ModularData/ai-agent/sandbox");
    for path in find_files(&sandbox, Some("json"), 256, 6) {
        let Ok(bytes) = fs::read(&path) else {
            continue;
        };
        let Ok(value) = serde_json::from_slice::<Value>(&bytes) else {
            continue;
        };
        if let Some(mut item) = normalize_trae_metadata(&value) {
            let time = fs::metadata(&path)
                .ok()
                .map(|meta| metadata_mtime_ns(&meta) as f64 / 1e9)
                .unwrap_or(0.0);
            item.observed_at = time;
            candidates.push((time, item));
        }
    }
    let storage = root.join("User/workspaceStorage");
    for path in find_named_files(&storage, "state.vscdb", 128, 6) {
        let value = read_sqlite_json_row(&path, "trae.currentAgent");
        let Some(mut item) = value.as_ref().and_then(normalize_trae_metadata) else {
            continue;
        };
        item.observed_at = fs::metadata(&path)
            .ok()
            .map(|meta| metadata_mtime_ns(&meta) as f64 / 1e9)
            .unwrap_or(0.0);
        candidates.push((item.observed_at, item));
    }
    let mut newest: HashMap<(String, String), (f64, TraeMetadata)> = HashMap::new();
    for (at, item) in candidates {
        let key = (item.session_id.clone(), item.task_id.clone());
        match newest.get_mut(&key) {
            None => {
                newest.insert(key, (at, item));
            }
            Some((old_at, old)) if at > *old_at => {
                *old_at = at;
                *old = item;
            }
            Some((old_at, old)) if at == *old_at => merge_trae_metadata(old, item),
            Some(_) => {}
        }
    }
    newest.into_values().map(|(_, item)| item).collect()
}

fn trae_real_session_metadata(
    root: &Path,
    tasks: &[TraeTask],
    catalog: &HashMap<String, (String, String)>,
    titles: &HashMap<String, String>,
) -> Vec<TraeMetadata> {
    let active: Vec<&TraeTask> = tasks
        .iter()
        .filter(|task| matches!(task.state.as_str(), "pending" | "running"))
        .collect();
    if active.is_empty() {
        return Vec::new();
    }
    let storage = root.join("User/workspaceStorage");
    let mut observations = Vec::new();
    for state_path in find_named_files(&storage, "state.vscdb", 128, 6) {
        let model_map = read_sqlite_json_row_suffix(
            &state_path,
            "lukeren_ai-chat:sessionRelation:modelMap",
            "ai-chat:sessionRelation:modelMap",
        );
        let model_map = model_map
            .and_then(|value| value.as_object().cloned())
            .unwrap_or_default();
        let global_model = read_sqlite_json_row_suffix(
            &state_path,
            "lukeren_ai-chat:sessionRelation:globalModelMap",
            "ai-chat:sessionRelation:globalModelMap",
        );
        let memento = read_sqlite_json_row(&state_path, "memento/icube-ai-agent-storage");
        let mut sessions: HashSet<String> = model_map.keys().cloned().collect();
        if let Some(list) = memento
            .as_ref()
            .and_then(|value| value.get("list"))
            .and_then(Value::as_array)
        {
            sessions.extend(list.iter().filter_map(|item| {
                item.get("sessionId")
                    .and_then(Value::as_str)
                    .filter(|value| !value.is_empty())
                    .map(str::to_string)
            }));
        }
        if !active
            .iter()
            .any(|task| sessions.contains(&task.session_id))
        {
            continue;
        }
        let workspace_file = state_path.with_file_name("workspace.json");
        let workspace = fs::read(&workspace_file)
            .ok()
            .and_then(|bytes| serde_json::from_slice::<Value>(&bytes).ok());
        let project_path = workspace
            .as_ref()
            .and_then(|value| value.get("folder"))
            .and_then(Value::as_str)
            .unwrap_or_default()
            .to_string();
        let observed_at = fs::metadata(&state_path)
            .ok()
            .map(|metadata| metadata_mtime_ns(&metadata) as f64 / 1_000_000_000.0)
            .unwrap_or_default()
            .max(
                fs::metadata(&workspace_file)
                    .ok()
                    .map(|metadata| metadata_mtime_ns(&metadata) as f64 / 1_000_000_000.0)
                    .unwrap_or_default(),
            );
        for task in &active {
            if !sessions.contains(&task.session_id) {
                continue;
            }
            let selection = model_map
                .get(&task.session_id)
                .filter(|value| value.is_object())
                .or(global_model.as_ref());
            let model_id = trae_selected_model_name(selection);
            let (selected_model, selected_effort) = catalog
                .get(&model_id)
                .cloned()
                .unwrap_or_else(|| (model_id, String::new()));
            observations.push(TraeMetadata {
                session_id: task.session_id.clone(),
                task_id: task.task_id.clone(),
                selected_model,
                selected_effort,
                project_path: project_path.clone(),
                task_title: titles.get(&task.session_id).cloned().unwrap_or_default(),
                task_detail: String::new(),
                state: task.state.clone(),
                observed_at,
            });
        }
    }
    observations
}

fn trae_selected_model_name(value: Option<&Value>) -> String {
    let Some(object) = value.and_then(Value::as_object) else {
        return String::new();
    };
    let selected = object
        .get("solo_coder")
        .and_then(Value::as_str)
        .or_else(|| {
            object
                .iter()
                .filter_map(|(key, value)| value.as_str().map(|value| (key, value)))
                .min_by(|left, right| left.0.cmp(right.0))
                .map(|(_, value)| value)
        })
        .unwrap_or_default();
    clean_display(TRAE_MODEL_PREFIX_RE.replace(selected, "").as_ref(), 64)
}

fn newest_trae_metadata(candidates: Vec<TraeMetadata>) -> Vec<TraeMetadata> {
    let mut newest: HashMap<(String, String), TraeMetadata> = HashMap::new();
    for candidate in candidates {
        let key = (candidate.session_id.clone(), candidate.task_id.clone());
        match newest.get_mut(&key) {
            None => {
                newest.insert(key, candidate);
            }
            Some(old) if candidate.observed_at > old.observed_at => *old = candidate,
            Some(old) if candidate.observed_at == old.observed_at => {
                merge_trae_metadata(old, candidate)
            }
            Some(_) => {}
        }
    }
    newest.into_values().collect()
}

fn merge_trae_metadata(target: &mut TraeMetadata, incoming: TraeMetadata) {
    if target.selected_model.is_empty() && !incoming.selected_model.is_empty() {
        target.selected_model = incoming.selected_model;
    }
    if target.selected_effort.is_empty() && !incoming.selected_effort.is_empty() {
        target.selected_effort = incoming.selected_effort;
    }
    if target.project_path.is_empty() && !incoming.project_path.is_empty() {
        target.project_path = incoming.project_path;
    }
    if target.task_title.is_empty() && !incoming.task_title.is_empty() {
        target.task_title = incoming.task_title;
    }
    if target.task_detail.is_empty() && !incoming.task_detail.is_empty() {
        target.task_detail = incoming.task_detail;
    }
    if target.state.is_empty() && !incoming.state.is_empty() {
        target.state = incoming.state;
    }
}

fn trae_session_titles_step(
    logs_root: &Path,
    requested: &HashSet<String>,
    cursors: &mut HashMap<String, TraeTitleScanCursor>,
    byte_budget: u64,
) -> (HashMap<String, String>, bool) {
    if requested.is_empty() {
        return (HashMap::new(), true);
    }
    let paths = trae_log_paths(logs_root)
        .into_iter()
        .filter(|path| {
            path.file_name().is_some_and(|name| {
                let name = name.to_string_lossy();
                name.contains("ai-agent") || name == "agent.log"
            })
        })
        .collect::<Vec<_>>();
    let mut remaining = byte_budget;
    let mut found = HashMap::new();
    let mut finished = true;
    for path in paths {
        if found.len() == requested.len() {
            return (found, true);
        }
        let Ok(metadata) = fs::metadata(&path) else {
            finished = false;
            continue;
        };
        let key = path.display().to_string();
        let cursor = cursors.entry(key).or_default();
        let inode = metadata_inode(&metadata);
        if cursor.inode != 0 && (cursor.inode != inode || cursor.offset > metadata.len()) {
            cursor.offset = 0;
            cursor.partial_line.clear();
            cursor.discarding_line = false;
        }
        cursor.inode = inode;
        if cursor.offset >= metadata.len() {
            finish_trae_title_line(cursor, requested, &mut found);
            continue;
        }
        if remaining == 0 {
            finished = false;
            continue;
        }
        let Ok(mut file) = File::open(&path) else {
            finished = false;
            continue;
        };
        if file.seek(SeekFrom::Start(cursor.offset)).is_err() {
            finished = false;
            continue;
        }
        while cursor.offset < metadata.len() && remaining > 0 {
            let count = (32 * 1024u64)
                .min(remaining)
                .min(metadata.len() - cursor.offset) as usize;
            let mut bytes = vec![0u8; count];
            let Ok(read) = file.read(&mut bytes) else {
                break;
            };
            if read == 0 {
                break;
            }
            bytes.truncate(read);
            consume_trae_title_bytes(cursor, &bytes, requested, &mut found);
            cursor.offset += read as u64;
            remaining -= read as u64;
            if found.len() == requested.len() {
                return (found, true);
            }
        }
        if cursor.offset < metadata.len() {
            finished = false;
        } else {
            finish_trae_title_line(cursor, requested, &mut found);
        }
    }
    (found, finished)
}

fn consume_trae_title_bytes(
    cursor: &mut TraeTitleScanCursor,
    bytes: &[u8],
    requested: &HashSet<String>,
    found: &mut HashMap<String, String>,
) {
    let mut start = 0;
    while start < bytes.len() {
        let newline = bytes[start..]
            .iter()
            .position(|byte| *byte == b'\n')
            .map(|offset| start + offset);
        let end = newline.unwrap_or(bytes.len());
        let complete = newline.is_some();
        let segment = &bytes[start..end];
        if cursor.discarding_line {
            if complete {
                cursor.discarding_line = false;
            }
        } else if cursor.partial_line.len() + segment.len() > 4096 {
            cursor.partial_line.clear();
            cursor.discarding_line = !complete;
        } else {
            cursor.partial_line.extend_from_slice(segment);
            if complete {
                record_trae_title_line(&cursor.partial_line, requested, found);
                cursor.partial_line.clear();
            }
        }
        start = if complete { end + 1 } else { bytes.len() };
    }
}

fn finish_trae_title_line(
    cursor: &TraeTitleScanCursor,
    requested: &HashSet<String>,
    found: &mut HashMap<String, String>,
) {
    if cursor.discarding_line || cursor.partial_line.is_empty() {
        return;
    }
    record_trae_title_line(&cursor.partial_line, requested, found);
}

fn record_trae_title_line(
    bytes: &[u8],
    requested: &HashSet<String>,
    found: &mut HashMap<String, String>,
) {
    let line = bytes.strip_suffix(b"\r").unwrap_or(bytes);
    let line = String::from_utf8_lossy(line);
    if let Some((session, title)) = parse_trae_title_line(&line)
        && requested.contains(&session)
    {
        let title = clean_display(&title, 48);
        if !title.is_empty() {
            found.insert(session, title);
        }
    }
}

fn parse_trae_title_line(line: &str) -> Option<(String, String)> {
    let (start, title_field) =
        if let Some(index) = line.find("generate_session_title_and_icon_cloud:") {
            (
                index + "generate_session_title_and_icon_cloud:".len(),
                "title:",
            )
        } else if let Some(index) = line.find("generate_session_title:") {
            (index + "generate_session_title:".len(), "generated_title:")
        } else {
            return None;
        };
    let fields = &line[start..];
    let session = parse_quoted_log_field(fields, "session_id:")?;
    let title = parse_quoted_log_field(fields, title_field)?;
    (!session.is_empty() && !title.is_empty()).then_some((session, title))
}

fn parse_quoted_log_field(line: &str, field: &str) -> Option<String> {
    let after_field = line.split_once(field)?.1;
    let quote_start = after_field.find('"')?;
    let mut escaped = false;
    let mut end = None;
    for (index, byte) in after_field
        .as_bytes()
        .iter()
        .enumerate()
        .skip(quote_start + 1)
    {
        if escaped {
            escaped = false;
        } else if *byte == b'\\' {
            escaped = true;
        } else if *byte == b'"' {
            end = Some(index);
            break;
        }
    }
    let end = end?;
    let raw = &after_field[quote_start + 1..end];
    let quoted = format!("\"{raw}\"");
    Some(serde_json::from_str::<String>(&quoted).unwrap_or_else(|_| raw.to_string()))
}

fn normalize_trae_metadata(value: &Value) -> Option<TraeMetadata> {
    let session_id = value.get("session_id")?.as_str()?.trim();
    let task_id = value.get("task_id")?.as_str()?.trim();
    if session_id.is_empty() || task_id.is_empty() {
        return None;
    }
    let raw_state = value
        .get("state")
        .and_then(Value::as_str)
        .unwrap_or("running");
    let state = if matches!(raw_state, "pending" | "running") {
        raw_state
    } else if matches!(raw_state, "completed" | "cancelled" | "failed") {
        "completed"
    } else {
        "running"
    };
    Some(TraeMetadata {
        session_id: clean_display(session_id, 128),
        task_id: clean_display(task_id, 128),
        selected_model: clean_display(
            value
                .get("selected_model")
                .and_then(Value::as_str)
                .unwrap_or(""),
            64,
        ),
        selected_effort: clean_display(
            value
                .get("selected_effort")
                .and_then(Value::as_str)
                .unwrap_or(""),
            32,
        ),
        project_path: clean_display(
            value
                .get("project_path")
                .and_then(Value::as_str)
                .unwrap_or(""),
            512,
        ),
        task_title: clean_display(
            value
                .get("task_title")
                .and_then(Value::as_str)
                .unwrap_or(""),
            48,
        ),
        task_detail: clean_display(
            value
                .get("task_detail")
                .and_then(Value::as_str)
                .unwrap_or(""),
            72,
        ),
        state: state.into(),
        observed_at: 0.0,
    })
}

fn enrich_trae_tasks(
    tasks: &mut Vec<TraeTask>,
    metadata: &TraeMetadata,
    fallback_project: &str,
) -> bool {
    let project = project_name(&metadata.project_path);
    if let Some(task) = tasks
        .iter_mut()
        .find(|task| task.session_id == metadata.session_id && task.task_id == metadata.task_id)
    {
        let is_newer = metadata.observed_at > task.updated_at;
        let mut changed = false;
        let next_project = if project.is_empty() {
            if fallback_project.is_empty() {
                task.project.clone()
            } else {
                fallback_project.to_string()
            }
        } else {
            project
        };
        if task.project != next_project {
            task.project = next_project;
            changed = true;
        }
        if !metadata.task_title.is_empty() && task.title != metadata.task_title {
            task.title = metadata.task_title.clone();
            changed = true;
        }
        if !metadata.task_detail.is_empty() && task.detail != metadata.task_detail {
            task.detail = metadata.task_detail.clone();
            changed = true;
        }
        if is_newer {
            if task.state != metadata.state || task.updated_at != metadata.observed_at {
                task.state = metadata.state.clone();
                task.updated_at = metadata.observed_at;
                changed = true;
            }
        }
        if task.model.is_empty() && !metadata.selected_model.is_empty() {
            task.model = metadata.selected_model.clone();
            changed = true;
        }
        if task.effort.is_empty() && !metadata.selected_effort.is_empty() {
            task.effort = metadata.selected_effort.clone();
            changed = true;
        }
        changed
    } else {
        tasks.push(TraeTask {
            session_id: metadata.session_id.clone(),
            task_id: metadata.task_id.clone(),
            project: if project.is_empty() {
                if fallback_project.is_empty() {
                    default_project()
                } else {
                    fallback_project.to_string()
                }
            } else {
                project
            },
            title: if metadata.task_title.is_empty() {
                default_trae_title()
            } else {
                metadata.task_title.clone()
            },
            detail: metadata.task_detail.clone(),
            state: metadata.state.clone(),
            updated_at: metadata.observed_at,
            model: metadata.selected_model.clone(),
            effort: metadata.selected_effort.clone(),
        });
        true
    }
}

fn trae_model_catalog(workspace_storage: &Path) -> HashMap<String, (String, String)> {
    let Some(parent) = workspace_storage.parent() else {
        return HashMap::new();
    };
    let path = parent.join("globalStorage/state.vscdb");
    let Some(value) = read_sqlite_json_row_suffix(
        &path,
        "lukeren_AI.agent.model.model_list_map",
        "AI.agent.model.model_list_map",
    ) else {
        return HashMap::new();
    };
    let Some(groups) = value.as_object() else {
        return HashMap::new();
    };
    let mut sorted: Vec<_> = groups.iter().collect();
    sorted.sort_by_key(|(key, _)| (*key != "solo_agent", key.as_str()));
    let mut output = HashMap::new();
    for (_, records) in sorted {
        let Some(records) = records.as_array() else {
            continue;
        };
        for record in records {
            let Some(name) = record
                .get("name")
                .and_then(Value::as_str)
                .filter(|name| !name.is_empty())
            else {
                continue;
            };
            output.entry(name.to_string()).or_insert_with(|| {
                (
                    record
                        .get("display_name")
                        .and_then(Value::as_str)
                        .filter(|v| !v.is_empty())
                        .unwrap_or(name)
                        .to_string(),
                    record
                        .get("default_reasoning_effort")
                        .and_then(Value::as_str)
                        .unwrap_or("")
                        .to_string(),
                )
            });
        }
    }
    output
}

fn read_sqlite_json_row(path: &Path, key: &str) -> Option<Value> {
    let connection = open_readonly(path).ok()?;
    let raw: Option<String> = connection
        .query_row(
            "SELECT value FROM ItemTable WHERE key = ?1 LIMIT 1",
            params![key],
            |row| row.get(0),
        )
        .optional()
        .ok()
        .flatten();
    serde_json::from_str(&raw?).ok()
}

fn read_sqlite_json_row_suffix(path: &Path, key: &str, suffix: &str) -> Option<Value> {
    let connection = open_readonly(path).ok()?;
    let raw: Option<String> = connection
        .query_row(
            "SELECT value FROM ItemTable WHERE key = ?1 OR substr(key, -length(?2)) = ?2 ORDER BY CASE WHEN key = ?1 THEN 0 ELSE 1 END, key LIMIT 1",
            params![key, suffix],
            |row| row.get(0),
        )
        .optional()
        .ok()
        .flatten();
    serde_json::from_str(&raw?).ok()
}

fn workspace_project(workspace_storage: &Path) -> String {
    let mut candidates = Vec::new();
    for path in find_named_files(workspace_storage, "workspace.json", 128, 6) {
        let Ok(bytes) = fs::read(&path) else {
            continue;
        };
        let Ok(value) = serde_json::from_slice::<Value>(&bytes) else {
            continue;
        };
        let Some(folder) = value.get("folder").and_then(Value::as_str) else {
            continue;
        };
        let project = project_name(folder);
        if !project.is_empty() {
            if let Ok(metadata) = fs::metadata(&path) {
                candidates.push((metadata_mtime_ns(&metadata), project));
            }
        }
    }
    candidates
        .into_iter()
        .max_by_key(|item| item.0)
        .map(|item| item.1)
        .unwrap_or_default()
}

fn find_named_files(root: &Path, name: &str, limit: usize, depth_limit: usize) -> Vec<PathBuf> {
    let mut output = Vec::new();
    let mut pending = vec![(root.to_path_buf(), 0usize)];
    while let Some((directory, depth)) = pending.pop() {
        if depth > depth_limit || output.len() >= limit {
            continue;
        }
        let Ok(entries) = fs::read_dir(directory) else {
            continue;
        };
        for entry in entries.flatten() {
            let path = entry.path();
            if entry.file_type().ok().is_some_and(|kind| kind.is_dir()) {
                pending.push((path, depth + 1));
            } else if path.file_name().is_some_and(|value| value == name) {
                output.push(path);
                if output.len() >= limit {
                    break;
                }
            }
        }
    }
    output.sort();
    output
}

fn find_files(
    root: &Path,
    extension: Option<&str>,
    limit: usize,
    depth_limit: usize,
) -> Vec<PathBuf> {
    let mut output = Vec::new();
    let mut pending = vec![(root.to_path_buf(), 0usize)];
    while let Some((directory, depth)) = pending.pop() {
        if depth > depth_limit {
            continue;
        }
        let Ok(entries) = fs::read_dir(directory) else {
            continue;
        };
        for entry in entries.flatten() {
            let path = entry.path();
            if entry.file_type().ok().is_some_and(|kind| kind.is_dir()) {
                pending.push((path, depth + 1));
            } else if extension.is_none_or(|ext| path.extension().is_some_and(|value| value == ext))
            {
                output.push(path);
            }
            if output.len() >= limit {
                return output;
            }
        }
    }
    output
}

#[cfg(test)]
mod tests {
    use super::*;

    fn write_oversized_completion(path: &Path, timestamp: &str, turn_id: &str) {
        let mut file = File::create(path).unwrap();
        let prefix = format!(
            "{{\"timestamp\":\"{timestamp}\",\"type\":\"event_msg\",\"payload\":{{\"type\":\"task_complete\",\"turn_id\":\"{turn_id}\",\"last_agent_message\":\"No action remains. "
        );
        file.write_all(prefix.as_bytes()).unwrap();
        let filler = vec![b'x'; 64 * 1024];
        let mut written = prefix.len();
        while written <= MAX_REPLAY_LINE_BYTES {
            file.write_all(&filler).unwrap();
            written += filler.len();
        }
        file.write_all(b"\"}}\n").unwrap();
    }

    fn append_json_line(path: &Path, value: &Value) {
        let mut file = fs::OpenOptions::new().append(true).open(path).unwrap();
        file.write_all(&serde_json::to_vec(value).unwrap()).unwrap();
        file.write_all(b"\n").unwrap();
    }

    fn executable_fixture(directory: &Path, name: &str, source: &str) -> PathBuf {
        use std::os::unix::fs::PermissionsExt;
        let path = directory.join(name);
        fs::write(&path, source).unwrap();
        let mut permissions = fs::metadata(&path).unwrap().permissions();
        permissions.set_mode(0o755);
        fs::set_permissions(&path, permissions).unwrap();
        path
    }

    #[test]
    fn app_server_thread_names_are_loaded_from_an_injected_fake_process() {
        let directory = tempfile::tempdir().unwrap();
        let fake = executable_fixture(
            directory.path(),
            "fake-codex",
            r##"#!/bin/sh
while IFS= read -r request; do
  case "$request" in
    *'"id":1'*) printf '%s\n' '{"jsonrpc":"2.0","id":1,"result":{}}' ;;
    *'"id":2'*) printf '%s\n' '{"jsonrpc":"2.0","id":2,"result":{"data":[{"id":"thread-a","name":"Human title"},{"id":"thread-b","name":"  "}]}}' ;;
  esac
done
"##,
        );

        let names = read_app_server_thread_names(&fake, APP_SERVER_TIMEOUT).unwrap();
        assert_eq!(
            names.get("thread-a").map(String::as_str),
            Some("Human title")
        );
        assert!(!names.contains_key("thread-b"));
    }

    #[test]
    fn app_server_timeout_kills_the_child_without_blocking_the_caller() {
        let directory = tempfile::tempdir().unwrap();
        let fake = executable_fixture(directory.path(), "hung-codex", "#!/bin/sh\nexec sleep 5\n");
        let started = std::time::Instant::now();
        let result = read_app_server_thread_names(&fake, Duration::from_millis(100));
        assert!(result.is_err());
        assert!(started.elapsed() < Duration::from_secs(2));
    }

    #[test]
    fn rollout_fork_and_late_old_turn_evidence_do_not_leak_into_the_new_turn() {
        let directory = tempfile::tempdir().unwrap();
        let rollout = directory.path().join("fork.jsonl");
        let events = [
            json!({"timestamp":"2026-10-14T08:00:00Z","type":"event_msg","payload":{"type":"task_started","turn_id":"turn-a"}}),
            json!({"timestamp":"2026-10-14T08:00:01Z","type":"event_msg","payload":{"type":"task_complete","turn_id":"turn-a","last_agent_message":"Please confirm?"}}),
            json!({"timestamp":"2026-10-14T08:00:02Z","type":"event_msg","payload":{"type":"task_started","turn_id":"turn-b"}}),
            json!({"timestamp":"2026-10-14T08:00:03Z","type":"response_item","payload":{"type":"function_call","name":"request_user_input","call_id":"old-call","internal_chat_message_metadata_passthrough":{"turn_id":"turn-a"}}}),
            json!({"timestamp":"2026-10-14T08:00:04Z","type":"event_msg","payload":{"type":"task_complete","turn_id":"turn-b","last_agent_message":"Work completed."}}),
        ];
        let contents = events[..3]
            .iter()
            .map(|event| serde_json::to_string(event).unwrap() + "\n")
            .collect::<String>();
        fs::write(&rollout, contents).unwrap();
        assert!(replay_latest_completion(&rollout).unwrap().is_none());
        let contents = events
            .iter()
            .map(|event| serde_json::to_string(event).unwrap() + "\n")
            .collect::<String>();
        fs::write(&rollout, contents).unwrap();

        let latest = replay_latest_completion(&rollout).unwrap().unwrap();
        assert!(!latest.unfinished_plan);
        assert!(!latest.unresolved_input);
        assert_eq!(
            classify_completion(
                latest.reply.as_deref(),
                latest.unfinished_plan,
                latest.unresolved_input
            )
            .outcome,
            "completed"
        );
    }

    #[test]
    fn oversized_replay_row_is_bounded_and_does_not_block_later_rows_or_threads() {
        let directory = tempfile::tempdir().unwrap();
        let timestamp = "2026-10-14T08:00:00Z";
        let oversized_rollout = directory.path().join("oversized.jsonl");
        write_oversized_completion(&oversized_rollout, timestamp, "oversized-turn");
        append_json_line(
            &oversized_rollout,
            &json!({
                "timestamp": "2026-10-14T08:00:01Z",
                "type": "event_msg",
                "payload": {"type": "task_started", "turn_id": "later-turn"}
            }),
        );

        let mut later_rows = Vec::new();
        let result = replay_lines(&oversized_rollout, |item| {
            later_rows.push(item);
            false
        });
        assert_eq!(result, Err(true));
        assert_eq!(later_rows.len(), 1);
        assert_eq!(
            later_rows[0]
                .pointer("/payload/type")
                .and_then(Value::as_str),
            Some("task_started")
        );

        let healthy_rollout = directory.path().join("healthy.jsonl");
        let healthy_event = json!({
            "timestamp": timestamp,
            "type": "event_msg",
            "payload": {
                "type": "task_complete",
                "turn_id": "healthy-turn",
                "last_agent_message": "Should I continue?"
            }
        });
        fs::write(
            &healthy_rollout,
            [serde_json::to_vec(&healthy_event).unwrap(), b"\n".to_vec()].concat(),
        )
        .unwrap();

        let mut ledger = AlertLedger::default();
        ledger.classifier_version = CLASSIFIER_VERSION - 1;
        ledger.alerts = vec![
            AlertRecord {
                id: format!("oversized:{timestamp}"),
                thread_id: "oversized".into(),
                progress_at: timestamp.into(),
                state: "pending".into(),
                outcome: "waiting".into(),
                ..AlertRecord::default()
            },
            AlertRecord {
                id: format!("healthy:{timestamp}"),
                thread_id: "healthy".into(),
                progress_at: timestamp.into(),
                state: "completed".into(),
                outcome: "completed".into(),
                watch_notified: true,
                ..AlertRecord::default()
            },
        ];
        let threads = [
            ThreadInfo {
                id: "oversized".into(),
                cwd: String::new(),
                title: "Oversized".into(),
                rollout: oversized_rollout,
            },
            ThreadInfo {
                id: "healthy".into(),
                cwd: String::new(),
                title: "Healthy".into(),
                rollout: healthy_rollout,
            },
        ];

        assert!(reconcile_classifier_version(&mut ledger, &threads));
        assert_eq!(
            ledger.classifier_retry_threads,
            vec!["oversized".to_string()]
        );
        let oversized = ledger
            .alerts
            .iter()
            .find(|alert| alert.thread_id == "oversized")
            .unwrap();
        assert_eq!(oversized.state, "pending");
        let healthy = ledger
            .alerts
            .iter()
            .find(|alert| alert.thread_id == "healthy")
            .unwrap();
        assert_eq!(healthy.state, "pending");
        assert_eq!(healthy.reason, "text_direct_question");
    }

    #[test]
    fn structured_input_outweighs_plan_and_full_reply_scan_keeps_middle_evidence() {
        let plan_first = classify_completion(None, true, true);
        assert_eq!(plan_first.outcome, "waiting");
        assert_eq!(plan_first.reason, "structured_input_request");

        let long_reply = format!(
            "{}Which option should I use?{}",
            "Work is complete. ".repeat(3_000),
            " The implementation is complete. ".repeat(3_000)
        );
        assert!(long_reply.len() > 64 * 1024);
        let bounded_detail = bounded_string(&long_reply, MAX_TASK_DETAIL_BYTES);
        assert!(bounded_detail.len() <= MAX_TASK_DETAIL_BYTES);
        let result = classify_completion(Some(&long_reply), false, false);
        assert_eq!(result.outcome, "waiting");
        assert_eq!(result.reason, "text_choice_handoff");

        let quoted = format!("{}\"Which option?\"", "Work is complete. ".repeat(5_000));
        assert_eq!(
            classify_completion(Some(&quoted), false, false).outcome,
            "completed"
        );
        let resolved = format!(
            "{}Which option should I use? No choice is required.",
            "Work is complete. ".repeat(5_000)
        );
        assert_eq!(
            classify_completion(Some(&resolved), false, false).outcome,
            "completed"
        );
    }

    #[test]
    fn classifier_matches_existing_python_synthetic_cases() {
        let complete_response_question = format!(
            "هل تريد المتابعة؟\n\n{}",
            "Long operational guidance. ".repeat(200)
        );
        let cases: &[(&str, &str, &str)] = &[
            (
                "可以共享一个手表主密钥，但仍需独立识别 Agent。\n\n首次添加 Agent 时，你希望如何确认？\n\n1. 手表和 Mac 显示相同验证码。\n2. 手表开放六十秒配对窗口。\n3. 只依赖系统蓝牙配对。\n\n我推荐第 1 种，安全性和可理解性最好。",
                "waiting",
                "text_choice_handoff",
            ),
            (
                &complete_response_question,
                "waiting",
                "text_direct_question",
            ),
            ("需要我继续吗？", "waiting", "text_direct_question"),
            ("هل تريد المتابعة؟", "waiting", "text_direct_question"),
            ("¿Desea continuar.", "waiting", "text_direct_question"),
            ("Should I continue?", "waiting", "text_direct_question"),
            ("请确认部署环境。", "waiting", "text_required_action"),
            ("请选择 A 或 B。", "waiting", "text_required_action"),
            ("请提供缺失的令牌。", "waiting", "text_required_action"),
            (
                "Please approve the deployment.",
                "waiting",
                "text_required_action",
            ),
            (
                "Please provide the missing token.",
                "waiting",
                "text_required_action",
            ),
            ("Waiting for your reply.", "waiting", "text_required_action"),
            (
                "确认后我就直接修改并做七个方案的圆屏截图复核。",
                "waiting",
                "text_required_action",
            ),
            (
                "确认后我会处理剩余问题。",
                "waiting",
                "text_required_action",
            ),
            ("确认后我们再继续实现。", "waiting", "text_required_action"),
            ("确认后我将提交修改。", "waiting", "text_required_action"),
            (
                "方案确认后已继续实现，当前工作已经完成。",
                "completed",
                "completion_only",
            ),
            ("确认后已经完成部署。", "completed", "completion_only"),
            ("如需更多帮助请告诉我。", "completed", "completion_only"),
            (
                "Let me know if you would like more help.",
                "completed",
                "completion_only",
            ),
            (
                "为什么构建失败？原因是缓存过期。修复已经完成。",
                "completed",
                "completion_only",
            ),
            (
                "你希望为什么选择方案 1？原因可以在确认后补充。",
                "waiting",
                "text_direct_question",
            ),
            (
                "Opened https://example.test/preview?variant=A. Work is complete.",
                "completed",
                "completion_only",
            ),
            (
                "Implemented `isReady?()` and all tests passed.",
                "completed",
                "completion_only",
            ),
            (
                "Implemented:\n```text\ncontinue?\n```\nAll tests passed.",
                "completed",
                "completion_only",
            ),
            (
                "### Assessment - Ready to merge?\n\nYes. All checks passed.",
                "completed",
                "completion_only",
            ),
            (
                "The earlier log asked “continue?”. The repair is complete.",
                "completed",
                "completion_only",
            ),
            (
                "可用方案：\n\nA. 同码配对\nB. 限时开放\nC. 系统配对\n\n我推荐 A。",
                "waiting",
                "text_choice_handoff",
            ),
            (
                "1. Keep layout\n2. Compact layout\n3. Alerts\n\nReply with 1, 2, or 3.",
                "waiting",
                "text_choice_handoff",
            ),
            (
                "安装步骤：\n\n1. 下载依赖。\n2. 运行测试。\n3. 启动应用。\n\n所有步骤已经执行完成。",
                "completed",
                "completion_only",
            ),
            (
                "请确认部署。无需确认。",
                "completed",
                "text_explicitly_resolved",
            ),
            (
                "你希望使用 A 还是 B？已经按 A 实施，不需要再选择。",
                "completed",
                "text_explicitly_resolved",
            ),
            (
                "Please confirm deployment. No choice is required.",
                "waiting",
                "text_required_action",
            ),
            (
                "无需回复，构建完成。你希望部署到哪个环境？",
                "waiting",
                "text_direct_question",
            ),
            (
                "规格已更新。\n\n请复核后回复。\n\n::git-commit{cwd=\"/workspace/stopwatch\"}",
                "waiting",
                "text_required_action",
            ),
            ("", "completed", "completion_only"),
            ("Build and tests passed.", "completed", "completion_only"),
        ];
        for (message, outcome, reason) in cases {
            let actual = classify_completion(Some(message), false, false);
            assert_eq!(
                (actual.outcome, actual.reason),
                (*outcome, *reason),
                "{message}"
            );
        }
        let invalid = classify_completion(None, false, false);
        assert_eq!(
            (invalid.outcome, invalid.reason),
            ("completed", "completion_only")
        );
        let structured_input = classify_completion(Some("No reply is required."), true, true);
        assert_eq!(
            (structured_input.outcome, structured_input.reason),
            ("waiting", "structured_input_request")
        );
        let structured_plan = classify_completion(Some("No reply is required."), true, false);
        assert_eq!(
            (structured_plan.outcome, structured_plan.reason),
            ("waiting", "structured_unfinished_plan")
        );
    }

    #[test]
    fn trae_workspace_metadata_joins_session_model_catalog_and_generated_title() {
        let dir = tempfile::tempdir().unwrap();
        let root = dir.path().join("Trae CN");
        let storage = root.join("User/workspaceStorage/workspace-a");
        fs::create_dir_all(&storage).unwrap();
        fs::write(
            storage.join("workspace.json"),
            br#"{"folder":"file:///work/CorrectProject"}"#,
        )
        .unwrap();
        let state_db = Connection::open(storage.join("state.vscdb")).unwrap();
        state_db
            .execute_batch("CREATE TABLE ItemTable (key TEXT PRIMARY KEY, value TEXT);")
            .unwrap();
        for (key, value) in [
            (
                "lukeren_ai-chat:sessionRelation:modelMap",
                json!({"session-a":{"solo_coder":"1_-_model-a"}}),
            ),
            (
                "memento/icube-ai-agent-storage",
                json!({"list":[{"sessionId":"session-a"}]}),
            ),
        ] {
            state_db
                .execute(
                    "INSERT INTO ItemTable (key,value) VALUES (?1,?2)",
                    params![key, value.to_string()],
                )
                .unwrap();
        }
        drop(state_db);
        let task = TraeTask {
            session_id: "session-a".into(),
            task_id: "task-a".into(),
            project: "--".into(),
            title: default_trae_title(),
            detail: String::new(),
            state: "running".into(),
            updated_at: 10.0,
            model: String::new(),
            effort: String::new(),
        };
        let catalog = HashMap::from([(
            "model-a".to_string(),
            ("Friendly Model".to_string(), "high".to_string()),
        )]);
        let titles = HashMap::from([("session-a".to_string(), "Build page".to_string())]);

        let observations = trae_real_session_metadata(&root, &[task], &catalog, &titles);
        assert_eq!(observations.len(), 1);
        assert_eq!(observations[0].project_path, "file:///work/CorrectProject");
        assert_eq!(observations[0].selected_model, "Friendly Model");
        assert_eq!(observations[0].selected_effort, "high");
        assert_eq!(observations[0].task_title, "Build page");
    }

    #[test]
    fn trae_sandbox_metadata_uses_file_mtime_for_state_reconciliation() {
        let dir = tempfile::tempdir().unwrap();
        let root = dir.path().join("Trae CN");
        let sandbox = root.join("ModularData/ai-agent/sandbox");
        fs::create_dir_all(&sandbox).unwrap();
        let metadata_path = sandbox.join("current-agent.json");
        fs::write(
            &metadata_path,
            br#"{"session_id":"session-a","task_id":"task-a","state":"completed","selected_model":"Friendly Model"}"#,
        )
        .unwrap();
        let expected = fs::metadata(&metadata_path)
            .unwrap()
            .modified()
            .unwrap()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_secs_f64();

        let metadata = trae_current_agent_metadata(&root);
        assert_eq!(metadata.len(), 1);
        assert_eq!(metadata[0].observed_at, expected);
        let mut tasks = vec![TraeTask {
            session_id: "session-a".into(),
            task_id: "task-a".into(),
            project: "demo".into(),
            title: "Existing task".into(),
            detail: String::new(),
            state: "running".into(),
            updated_at: 1.0,
            model: String::new(),
            effort: String::new(),
        }];
        assert!(enrich_trae_tasks(&mut tasks, &metadata[0], "demo"));
        assert_eq!(tasks[0].state, "completed");
        assert_eq!(tasks[0].updated_at, expected);
    }

    #[test]
    fn trae_current_agent_project_path_is_not_replaced_by_workspace_folder() {
        let dir = tempfile::tempdir().unwrap();
        let root = dir.path().join("Trae CN");
        let workspace = root.join("User/workspaceStorage/workspace-a");
        fs::create_dir_all(&workspace).unwrap();
        fs::write(
            workspace.join("workspace.json"),
            br#"{"folder":"file:///work/WorkspaceProject"}"#,
        )
        .unwrap();
        let state_db = Connection::open(workspace.join("state.vscdb")).unwrap();
        state_db
            .execute_batch("CREATE TABLE ItemTable (key TEXT PRIMARY KEY, value TEXT);")
            .unwrap();
        state_db
            .execute(
                "INSERT INTO ItemTable (key,value) VALUES (?1,?2)",
                params![
                    "trae.currentAgent",
                    json!({
                        "session_id":"session-a",
                        "task_id":"task-a",
                        "project_path":"file:///work/CurrentAgentProject",
                        "state":"running"
                    })
                    .to_string()
                ],
            )
            .unwrap();
        drop(state_db);

        let metadata = trae_current_agent_metadata(&root);
        assert_eq!(metadata.len(), 1);
        assert_eq!(metadata[0].project_path, "file:///work/CurrentAgentProject");
        assert!(metadata[0].observed_at > 0.0);
    }

    #[test]
    fn equal_time_trae_metadata_merges_nonempty_fields() {
        let base = TraeMetadata {
            session_id: "session-a".into(),
            task_id: "task-a".into(),
            project_path: "file:///work/Project%20A".into(),
            observed_at: 12.0,
            ..TraeMetadata::default()
        };
        let supplement = TraeMetadata {
            session_id: "session-a".into(),
            task_id: "task-a".into(),
            selected_model: "Friendly Model".into(),
            task_title: "Build view".into(),
            state: "running".into(),
            observed_at: 12.0,
            ..TraeMetadata::default()
        };
        let mut merged = newest_trae_metadata(vec![base, supplement]);
        assert_eq!(merged.len(), 1);
        assert_eq!(merged[0].project_path, "file:///work/Project%20A");
        assert_eq!(merged[0].selected_model, "Friendly Model");
        assert_eq!(merged[0].task_title, "Build view");
    }

    #[test]
    fn trae_project_names_unquote_uri_paths_and_clean_unicode_other_characters() {
        assert_eq!(project_name("file:///work/Project%20One"), "Project One");
        assert_eq!(project_name("file:///work/%E4%B8%AD%E6%96%87"), "中文");
        assert_eq!(project_name("file:///work/"), "work");
        assert_eq!(
            clean_display("A\u{00ad}B\u{e0001}C\u{e000}D", 16),
            "A B C D"
        );
    }

    #[test]
    fn trae_title_scan_decodes_escaped_generated_titles_from_fixture_logs() {
        let dir = tempfile::tempdir().unwrap();
        let logs = dir.path().join("logs");
        fs::create_dir_all(&logs).unwrap();
        fs::write(
            logs.join("ai-agent.log"),
            br#"generate_session_title: session_id: "session-a", generated_title: "A \"quoted\" task""#,
        )
        .unwrap();

        let fixture_line = fs::read_to_string(logs.join("ai-agent.log")).unwrap();
        assert_eq!(
            parse_quoted_log_field(
                &fixture_line["generate_session_title:".len()..],
                "session_id:"
            ),
            Some("session-a".into())
        );
        assert_eq!(
            parse_quoted_log_field(
                &fixture_line["generate_session_title:".len()..],
                "generated_title:"
            ),
            Some("A \"quoted\" task".into())
        );
        assert_eq!(
            parse_trae_title_line(&fixture_line),
            Some(("session-a".into(), "A \"quoted\" task".into()))
        );
        assert_eq!(trae_log_paths(&logs).len(), 1);

        let requested = HashSet::from(["session-a".into()]);
        let mut cursors = HashMap::new();
        let (titles, finished) =
            trae_session_titles_step(&logs, &requested, &mut cursors, 64 * 1024);
        assert!(finished);
        assert_eq!(
            titles.get("session-a").map(String::as_str),
            Some("A \"quoted\" task")
        );
    }

    #[test]
    fn trae_title_scan_reaches_old_log_content_with_lossy_utf8_and_bounded_steps() {
        let dir = tempfile::tempdir().unwrap();
        let logs = dir.path().join("logs");
        fs::create_dir_all(&logs).unwrap();
        let path = logs.join("ai-agent-older.log");
        let title_line =
            br#"generate_session_title: session_id: "session-old", generated_title: "Older task""#;
        let mut contents = Vec::new();
        contents.push(0xff);
        contents.extend_from_slice(b"\n");
        contents.extend_from_slice(title_line);
        contents.extend_from_slice(b"\n");
        contents.resize(contents.len() + 600 * 1024, b'x');
        contents.push(0xff);
        fs::write(&path, contents).unwrap();

        let requested = HashSet::from(["session-old".into()]);
        let mut cursors = HashMap::new();
        let mut found = HashMap::new();
        let mut finished = false;
        while !finished {
            let (batch, done) =
                trae_session_titles_step(&logs, &requested, &mut cursors, 64 * 1024);
            found.extend(batch);
            finished = done;
        }
        assert_eq!(
            found.get("session-old").map(String::as_str),
            Some("Older task")
        );
    }
}
