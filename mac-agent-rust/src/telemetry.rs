//! Bounded, read-only telemetry collectors for Codex usage and the Mac host.
//!
//! Collection is split across persistent workers. A slow quota request cannot
//! delay task reduction or notification delivery, and a stuck sampler never
//! causes another worker to be spawned on the next status poll.

use chrono::{DateTime, Local, NaiveDate, NaiveDateTime, Timelike};
use serde_json::{Map, Value, json};
use std::{
    cmp::Reverse,
    collections::BinaryHeap,
    fs,
    io::{BufRead, Read, Seek, SeekFrom},
    path::{Path, PathBuf},
    sync::{
        Arc, Mutex, RwLock,
        atomic::{AtomicBool, Ordering},
    },
    thread::{self, JoinHandle},
    time::{Duration, Instant, SystemTime, UNIX_EPOCH},
};

use crate::pricing::PricingCatalog;
use crate::ccswitch_usage::{TokenBuckets, UsageModels, request_cost};

// Refresh well before expiry: workers wait this interval after collection,
// so matching the TTL briefly nulled healthy values on every refresh.
const USAGE_INTERVAL: Duration = Duration::from_secs(60);
const USAGE_TTL: Duration = Duration::from_secs(120);
const QUOTA_INTERVAL: Duration = Duration::from_secs(60);
const QUOTA_TTL: Duration = Duration::from_secs(120);
const HOST_INTERVAL: Duration = Duration::from_secs(5);
const HOST_TTL: Duration = Duration::from_secs(15);
const OPTIONAL_PROVIDER_INTERVAL: Duration = Duration::from_secs(120);
const OPTIONAL_PROVIDER_TTL: Duration = Duration::from_secs(120);
const OPTIONAL_IMPORT_MAX_BYTES: usize = 4 * 1024;
const SESSION_FILE_LIMIT: usize = 64;
const SESSION_TAIL_BYTES: u64 = 1024 * 1024;
const SESSION_TRAVERSAL_ENTRY_LIMIT: usize = 100_000;
const SESSION_DIRECTORY_DEPTH_LIMIT: usize = 7;
const QUOTA_FILE_LIMIT: usize = 32;
const QUOTA_TAIL_BYTES: u64 = 256 * 1024;
const MIN_TELEMETRY_EPOCH: i64 = 946_684_800;
const MAX_TELEMETRY_EPOCH: i64 = 4_102_444_800;
const MAX_EXACT_TELEMETRY_INTEGER: u64 = 9_007_199_254_740_991;

#[derive(Clone, Debug)]
struct UsageSample {
    status: String,
    sampled_at: i64,
    tokens: Option<u64>,
    cost: Option<f64>,
    cost_status: String,
    day: String,
    detail: Option<Value>,
}

#[derive(Clone, Debug, Default)]
struct QuotaSample {
    status: String,
    sampled_at: i64,
    five_hour_left: Option<u8>,
    week_left: Option<u8>,
    five_hour_reset: Option<i64>,
    week_reset: Option<i64>,
    plan: String,
}

#[derive(Clone, Debug)]
struct HostSample {
    status: String,
    sampled_at: i64,
    cpu: Option<f64>,
    memory: Option<f64>,
    disk: Option<f64>,
    up: Option<f64>,
    down: Option<f64>,
}

#[derive(Clone, Debug)]
struct ProviderQuotaSample {
    status: String,
    sampled_at: i64,
    five_hour_left: Option<u8>,
    week_left: Option<u8>,
    five_hour_reset: Option<i64>,
    week_reset: Option<i64>,
}

#[derive(Clone, Debug)]
struct CacheEntry<T> {
    value: Option<T>,
    sampled_at: i64,
    updated: Option<Instant>,
    sample_status: String,
    last_attempt_status: String,
}

impl<T> Default for CacheEntry<T> {
    fn default() -> Self {
        Self {
            value: None,
            sampled_at: 0,
            updated: None,
            sample_status: "unavailable".into(),
            last_attempt_status: "unavailable".into(),
        }
    }
}

#[derive(Default)]
struct CacheState {
    usage: CacheEntry<UsageSample>,
    quota: CacheEntry<QuotaSample>,
    host: CacheEntry<HostSample>,
    claude: Option<CacheEntry<ProviderQuotaSample>>,
    opencode_import: Option<CacheEntry<ProviderQuotaSample>>,
}

pub struct TelemetrySampler {
    state: Arc<RwLock<CacheState>>,
    stop: Arc<AtomicBool>,
    workers: Mutex<Vec<JoinHandle<()>>>,
    host_enabled: bool,
}

impl TelemetrySampler {
    pub fn new(codex_root: PathBuf, pricing: Arc<PricingCatalog>) -> Self {
        Self::with_ccswitch_usage(codex_root, pricing, None)
    }

    pub(crate) fn with_ccswitch_usage(
        codex_root: PathBuf,
        pricing: Arc<PricingCatalog>,
        ccswitch_database: Option<PathBuf>,
    ) -> Self {
        Self::with_ccswitch_usage_and_host_enabled(
            codex_root,
            pricing,
            ccswitch_database,
            true,
        )
    }

    pub(crate) fn with_ccswitch_usage_and_host_enabled(
        codex_root: PathBuf,
        pricing: Arc<PricingCatalog>,
        ccswitch_database: Option<PathBuf>,
        host_enabled: bool,
    ) -> Self {
        let state = Arc::new(RwLock::new(CacheState::default()));
        let stop = Arc::new(AtomicBool::new(false));
        let sessions = codex_root.join("sessions");
        let quota_sessions = sessions.clone();
        let auth = codex_root.join("auth.json");
        let mut workers = Vec::with_capacity(if host_enabled { 3 } else { 2 });

        workers.push(spawn_worker(
            Arc::clone(&state),
            Arc::clone(&stop),
            USAGE_INTERVAL,
            |state| &mut state.usage,
            move || collect_preferred_daily_usage(&sessions, ccswitch_database.as_deref(), &pricing),
        ));
        workers.push(spawn_worker(
            Arc::clone(&state),
            Arc::clone(&stop),
            QUOTA_INTERVAL,
            |state| &mut state.quota,
            move || collect_quota(&quota_sessions, &auth),
        ));
        if host_enabled {
            workers.push(spawn_worker(
                Arc::clone(&state),
                Arc::clone(&stop),
                HOST_INTERVAL,
                |state| &mut state.host,
                collect_host,
            ));
        }
        Self {
            state,
            stop,
            workers: Mutex::new(workers),
            host_enabled,
        }
    }

    /// Enables optional provider quotas only when explicit runtime inputs are
    /// supplied. The Claude token remains in a zeroizing worker capture and is
    /// never added to telemetry or persisted. OpenCode quota is imported from
    /// a bounded read-only JSON file; the upstream Go dashboard has no public
    /// usage API.
    pub fn configure_optional_providers(
        &mut self,
        claude_token: Option<String>,
        opencode_import: Option<PathBuf>,
    ) {
        if let Some(token) = claude_token.filter(|token| !token.trim().is_empty()) {
            let token = zeroize::Zeroizing::new(token);
            self.workers
                .lock()
                .unwrap_or_else(|poisoned| poisoned.into_inner())
                .push(spawn_worker(
                    Arc::clone(&self.state),
                    Arc::clone(&self.stop),
                    OPTIONAL_PROVIDER_INTERVAL,
                    |state| state.claude.get_or_insert_with(CacheEntry::default),
                    move || fetch_claude_quota(&token),
                ));
        }
        if let Some(path) = opencode_import {
            self.workers
                .lock()
                .unwrap_or_else(|poisoned| poisoned.into_inner())
                .push(spawn_worker(
                    Arc::clone(&self.state),
                    Arc::clone(&self.stop),
                    OPTIONAL_PROVIDER_INTERVAL,
                    |state| {
                        state
                            .opencode_import
                            .get_or_insert_with(CacheEntry::default)
                    },
                    move || read_opencode_quota_import(&path),
                ));
        }
    }

    pub fn with_optional_providers(
        mut self,
        claude_token: Option<String>,
        opencode_import: Option<PathBuf>,
    ) -> Self {
        self.configure_optional_providers(claude_token, opencode_import);
        self
    }

    /// Returns the compact schema consumed by the watch. Credential material is
    /// never copied into this value.
    pub fn snapshot(&self) -> Value {
        let now = epoch_seconds();
        let state = self
            .state
            .read()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        let usage = cache_view(&state.usage, USAGE_TTL);
        let quota = cache_view(&state.quota, QUOTA_TTL);
        let claude = state
            .claude
            .as_ref()
            .map(|entry| optional_provider_view("claude", entry, now));
        let opencode = state
            .opencode_import
            .as_ref()
            .map(|entry| optional_provider_view("opencode", entry, now));

        let usage_sample = usage.value.as_ref();
        let quota_sample = quota.value.as_ref();
        let local_day = Local::now().date_naive().format("%Y-%m-%d").to_string();
        let usage_fresh = usage.age.is_some_and(|age| age <= USAGE_TTL)
            && usage_sample.is_some_and(|sample| sample.day == local_day);
        let quota_fresh = quota.age.is_some_and(|age| age <= QUOTA_TTL)
            && quota_sample.is_some_and(|sample| {
                sample.sampled_at <= now
                    && now.saturating_sub(sample.sampled_at) <= QUOTA_TTL.as_secs() as i64
            });
        let usage_status = if usage_fresh {
            usage.status
        } else {
            status_after_expiry(&usage)
        };
        let quota_status = if quota_fresh {
            quota.status
        } else {
            status_after_expiry(&quota)
        };
        let usage_usable = usage_fresh && matches!(usage_status.as_str(), "ok" | "partial");
        let quota_usable = quota_fresh && matches!(quota_status.as_str(), "ok" | "partial");
        let (tokens, cost, cost_status, day) = match (usage_usable, usage_sample) {
            (true, Some(sample)) => (
                sample.tokens,
                sample.cost,
                sample.cost_status.as_str(),
                sample.day.as_str(),
            ),
            _ => (None, None, "unavailable", local_day.as_str()),
        };

        let mut usage_object = Map::new();
        usage_object.insert("s".into(), json!(wire_status(&usage_status)));
        usage_object.insert("q".into(), json!(wire_status(&quota_status)));
        usage_object.insert(
            "at".into(),
            json!(usage_sample.map_or(now, |s| s.sampled_at())),
        );
        usage_object.insert(
            "qa".into(),
            nullable_epoch(if quota_usable {
                quota_sample.map(|s| s.sampled_at)
            } else {
                None
            }),
        );
        usage_object.insert(
            "h".into(),
            nullable_u8(if quota_usable {
                quota_sample.and_then(|s| s.five_hour_left)
            } else {
                None
            }),
        );
        usage_object.insert(
            "w".into(),
            nullable_u8(if quota_usable {
                quota_sample.and_then(|s| s.week_left)
            } else {
                None
            }),
        );
        usage_object.insert(
            "hr".into(),
            nullable_epoch(if quota_usable {
                quota_sample.and_then(|s| s.five_hour_reset)
            } else {
                None
            }),
        );
        usage_object.insert(
            "wr".into(),
            nullable_epoch(if quota_usable {
                quota_sample.and_then(|s| s.week_reset)
            } else {
                None
            }),
        );
        usage_object.insert(
            "t".into(),
            if usage_usable {
                nullable_u64(tokens)
            } else {
                Value::Null
            },
        );
        usage_object.insert(
            "c".into(),
            if usage_usable {
                nullable_number(cost)
            } else {
                Value::Null
            },
        );
        usage_object.insert("cs".into(), json!(cost_status));
        usage_object.insert("day".into(), json!(day));
        if usage_usable {
            if let Some(detail) = usage_sample.and_then(|sample| sample.detail.as_ref()) {
                usage_object.insert("d".into(), detail.clone());
            }
        }

        let host_object = self.host_enabled.then(|| {
            let host = cache_view(&state.host, HOST_TTL);
            let host_sample = host.value.as_ref();
            let host_fresh = host.age.is_some_and(|age| age <= HOST_TTL);
            let host_status = if host_fresh {
                host.status
            } else {
                status_after_expiry(&host)
            };
            let host_usable = host_fresh && matches!(host_status.as_str(), "ok" | "partial");
            let mut host_object = Map::new();
            host_object.insert("s".into(), json!(wire_status(&host_status)));
            host_object.insert(
                "at".into(),
                json!(host_sample.map_or(now, |s| s.sampled_at)),
            );
            for (key, value) in [
                ("cpu", host_sample.and_then(|s| s.cpu)),
                ("mem", host_sample.and_then(|s| s.memory)),
                ("disk", host_sample.and_then(|s| s.disk)),
                ("up", host_sample.and_then(|s| s.up)),
                ("down", host_sample.and_then(|s| s.down)),
            ] {
                host_object.insert(
                    key.into(),
                    if host_usable {
                        nullable_number(value)
                    } else {
                        Value::Null
                    },
                );
            }
            Value::Object(host_object)
        });

        let mut usage_object = usage_object;
        let providers: Vec<Value> = [claude, opencode].into_iter().flatten().collect();
        if !providers.is_empty() {
            usage_object.insert("p".into(), Value::Array(providers));
        }

        let mut snapshot = Map::new();
        snapshot.insert("telemetry_v".into(), json!(1));
        snapshot.insert("usage".into(), Value::Object(usage_object));
        if let Some(host) = host_object {
            snapshot.insert("host".into(), host);
        }
        Value::Object(snapshot)
    }

    pub fn quota_plan(&self) -> Option<String> {
        let state = self
            .state
            .read()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        let view = cache_view(&state.quota, QUOTA_TTL);
        if view.age.is_some_and(|age| age <= QUOTA_TTL)
            && matches!(view.status.as_str(), "ok" | "partial")
        {
            view.value
                .as_ref()
                .filter(|sample| {
                    let now = epoch_seconds();
                    sample.sampled_at <= now
                        && now.saturating_sub(sample.sampled_at) <= QUOTA_TTL.as_secs() as i64
                })
                .map(|sample| sample.plan.clone())
                .filter(|s| !s.is_empty())
        } else {
            None
        }
    }

    pub fn close(&self) {
        self.stop.store(true, Ordering::Release);
        let mut workers = self
            .workers
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        for worker in workers.drain(..) {
            let _ = worker.join();
        }
    }
}

impl Drop for TelemetrySampler {
    fn drop(&mut self) {
        self.close();
    }
}

struct CacheView<T> {
    value: Option<T>,
    age: Option<Duration>,
    status: String,
}

fn cache_view<T: Clone>(entry: &CacheEntry<T>, ttl: Duration) -> CacheView<T> {
    let age = entry.updated.map(|updated| updated.elapsed());
    let status = match age {
        None => entry.last_attempt_status.clone(),
        Some(age) if age > ttl => status_after_expiry_parts(&entry.last_attempt_status),
        Some(_) if matches!(entry.last_attempt_status.as_str(), "error" | "unavailable") => {
            "stale".into()
        }
        Some(_) => entry.sample_status.clone(),
    };
    CacheView {
        value: entry.value.clone(),
        age,
        status,
    }
}

fn status_after_expiry<T>(view: &CacheView<T>) -> String {
    status_after_expiry_parts(&view.status)
}

fn status_after_expiry_parts(last_status: &str) -> String {
    match last_status {
        "error" | "unavailable" => last_status.into(),
        _ => "stale".into(),
    }
}

fn wire_status(status: &str) -> &str {
    if status == "partial" { "ok" } else { status }
}

fn spawn_worker<T, F, S>(
    state: Arc<RwLock<CacheState>>,
    stop: Arc<AtomicBool>,
    interval: Duration,
    select: S,
    collect: F,
) -> JoinHandle<()>
where
    T: Clone + Send + SampleMeta + 'static,
    F: Fn() -> Result<T, String> + Send + 'static,
    S: for<'a> Fn(&'a mut CacheState) -> &'a mut CacheEntry<T> + Send + 'static,
{
    thread::Builder::new()
        .name("stopwatch-telemetry".into())
        .spawn(move || {
            let mut initial = true;
            while !stop.load(Ordering::Acquire) {
                if !initial {
                    sleep_interruptible(&stop, interval);
                    if stop.load(Ordering::Acquire) {
                        break;
                    }
                }
                initial = false;
                let attempt = collect();
                let attempt_status = match &attempt {
                    Ok(value) => sample_status(value).to_owned(),
                    Err(status) => status.clone(),
                };
                let mut cache = state
                    .write()
                    .unwrap_or_else(|poisoned| poisoned.into_inner());
                let entry = select(&mut cache);
                entry.last_attempt_status = attempt_status;
                if let Ok(value) = attempt {
                    let status = sample_status(&value);
                    if matches!(status, "ok" | "partial") {
                        entry.sampled_at = sample_epoch(&value);
                        entry.updated = Some(Instant::now());
                        entry.sample_status = status.into();
                        entry.value = Some(value);
                    } else if entry.value.is_none() {
                        entry.sample_status = status.into();
                    }
                }
            }
        })
        .expect("telemetry worker spawn failed")
}

trait SampleMeta {
    fn status(&self) -> &str;
    fn sampled_at(&self) -> i64;
}

impl SampleMeta for UsageSample {
    fn status(&self) -> &str {
        &self.status
    }
    fn sampled_at(&self) -> i64 {
        self.sampled_at
    }
}
impl SampleMeta for QuotaSample {
    fn status(&self) -> &str {
        &self.status
    }
    fn sampled_at(&self) -> i64 {
        self.sampled_at
    }
}
impl SampleMeta for HostSample {
    fn status(&self) -> &str {
        &self.status
    }
    fn sampled_at(&self) -> i64 {
        self.sampled_at
    }
}
impl SampleMeta for ProviderQuotaSample {
    fn status(&self) -> &str {
        &self.status
    }
    fn sampled_at(&self) -> i64 {
        self.sampled_at
    }
}

fn optional_provider_view(name: &str, entry: &CacheEntry<ProviderQuotaSample>, now: i64) -> Value {
    let sample = entry.value.as_ref();
    let within_cache_ttl = entry
        .updated
        .is_some_and(|updated| updated.elapsed() <= OPTIONAL_PROVIDER_TTL);
    let within_sample_ttl = sample.is_some_and(|sample| {
        sample.sampled_at <= now
            && now.saturating_sub(sample.sampled_at) <= OPTIONAL_PROVIDER_TTL.as_secs() as i64
    });
    let fresh = within_cache_ttl && within_sample_ttl;
    let status = match entry.last_attempt_status.as_str() {
        "error" | "unavailable" if fresh => "stale",
        "error" | "unavailable" => entry.last_attempt_status.as_str(),
        "stale" => "stale",
        _ if fresh => sample.map_or("unavailable", |sample| sample.status.as_str()),
        _ => "stale",
    };
    let usable =
        fresh && (status == "ok" || (status == "stale" && entry.last_attempt_status != "stale"));
    json!({
        "n": name,
        "s": status,
        "h": if usable { sample.and_then(|s| s.five_hour_left).map_or(Value::Null, |v| json!(v)) } else { Value::Null },
        "w": if usable { sample.and_then(|s| s.week_left).map_or(Value::Null, |v| json!(v)) } else { Value::Null },
        "hr": if usable { sample.and_then(|s| s.five_hour_reset).map_or(Value::Null, |v| json!(v)) } else { Value::Null },
        "wr": if usable { sample.and_then(|s| s.week_reset).map_or(Value::Null, |v| json!(v)) } else { Value::Null },
    })
}

fn fetch_claude_quota(token: &str) -> Result<ProviderQuotaSample, String> {
    let client = reqwest::blocking::Client::builder()
        .timeout(Duration::from_secs(8))
        .build()
        .map_err(|_| "error".to_string())?;
    // Best-effort GET-only adaptation of CC Island StopWatch bridge's MIT
    // usage mapping; this deliberately omits its credential discovery/refresh.
    let response = client
        .get("https://api.anthropic.com/api/oauth/usage")
        .bearer_auth(token)
        .header("anthropic-beta", "oauth-2025-04-20")
        .header("accept", "application/json")
        .header("user-agent", "claude-code/2.1.121")
        .send()
        .map_err(|_| "error".to_string())?;
    if !response.status().is_success() {
        return Err("error".into());
    }
    const MAX_CLAUDE_RESPONSE: u64 = 64 * 1024;
    let mut bytes = Vec::new();
    response
        .take(MAX_CLAUDE_RESPONSE + 1)
        .read_to_end(&mut bytes)
        .map_err(|_| "error".to_string())?;
    if bytes.len() as u64 > MAX_CLAUDE_RESPONSE {
        return Err("error".into());
    }
    let payload: Value = serde_json::from_slice(&bytes).map_err(|_| "unavailable".to_string())?;
    parse_claude_quota_response(200, &payload, epoch_seconds())
}

fn parse_claude_quota_response(
    http_status: u16,
    payload: &Value,
    now: i64,
) -> Result<ProviderQuotaSample, String> {
    if http_status == 401 || !(200..300).contains(&http_status) {
        return Err("error".into());
    }
    let root = payload
        .as_object()
        .ok_or_else(|| "unavailable".to_string())?;
    let (five_hour_left, five_hour_reset) = parse_claude_window(root.get("five_hour"))?;
    let (week_left, week_reset) = parse_claude_window(root.get("seven_day"))?;
    if five_hour_left.is_none() && week_left.is_none() {
        return Err("unavailable".into());
    }
    Ok(ProviderQuotaSample {
        status: "ok".into(),
        sampled_at: now,
        five_hour_left,
        week_left,
        five_hour_reset,
        week_reset,
    })
}

fn parse_claude_window(value: Option<&Value>) -> Result<(Option<u8>, Option<i64>), String> {
    let Some(value) = value.filter(|value| !value.is_null()) else {
        return Ok((None, None));
    };
    let window = value.as_object().ok_or_else(|| "unavailable".to_string())?;
    let used = window
        .get("utilization")
        .or_else(|| window.get("used_percent"))
        .and_then(Value::as_f64)
        .filter(|value| value.is_finite() && (0.0..=100.0).contains(value))
        .ok_or_else(|| "unavailable".to_string())?;
    let reset = window
        .get("resets_at")
        .and_then(parse_claude_reset)
        .and_then(bounded_telemetry_epoch);
    Ok((Some((100.0 - used).round() as u8), reset))
}

fn parse_claude_reset(value: &Value) -> Option<i64> {
    if let Some(value) = value.as_i64() {
        return Some(value);
    }
    if let Some(value) = value.as_u64() {
        return i64::try_from(value).ok();
    }
    value
        .as_str()
        .and_then(|value| DateTime::parse_from_rfc3339(value).ok())
        .map(|value| value.timestamp())
}

fn read_opencode_quota_import(path: &Path) -> Result<ProviderQuotaSample, String> {
    let mut file = fs::File::open(path).map_err(|error| {
        if error.kind() == std::io::ErrorKind::NotFound {
            "unavailable".to_string()
        } else {
            "error".to_string()
        }
    })?;
    let metadata = file.metadata().map_err(|_| "error".to_string())?;
    if !metadata.is_file() || metadata.len() > OPTIONAL_IMPORT_MAX_BYTES as u64 {
        return Err("error".into());
    }
    let mut bytes = Vec::with_capacity(metadata.len() as usize);
    file.by_ref()
        .take(OPTIONAL_IMPORT_MAX_BYTES as u64 + 1)
        .read_to_end(&mut bytes)
        .map_err(|_| "error".to_string())?;
    if bytes.len() > OPTIONAL_IMPORT_MAX_BYTES {
        return Err("error".into());
    }
    let payload: Value = serde_json::from_slice(&bytes).map_err(|_| "error".to_string())?;
    parse_opencode_quota_import(&payload, epoch_seconds())
}

fn parse_opencode_quota_import(payload: &Value, now: i64) -> Result<ProviderQuotaSample, String> {
    let object = payload.as_object().ok_or_else(|| "error".to_string())?;
    const FIELDS: [&str; 6] = ["at", "s", "h", "w", "hr", "wr"];
    if object.len() != FIELDS.len() || FIELDS.iter().any(|field| !object.contains_key(*field)) {
        return Err("error".into());
    }
    let sampled_at = object
        .get("at")
        .and_then(Value::as_i64)
        .filter(|at| *at > 0 && *at <= now.saturating_add(300))
        .ok_or_else(|| "error".to_string())?;
    let source_status = object
        .get("s")
        .and_then(Value::as_str)
        .filter(|status| matches!(*status, "ok" | "stale" | "error" | "unavailable"))
        .ok_or_else(|| "error".to_string())?;
    let five_hour_left = import_percent(object.get("h"))?;
    let week_left = import_percent(object.get("w"))?;
    let five_hour_reset = import_reset(object.get("hr"))?;
    let week_reset = import_reset(object.get("wr"))?;
    if five_hour_left.is_none() && five_hour_reset.is_some()
        || week_left.is_none() && week_reset.is_some()
    {
        return Err("error".into());
    }
    if source_status != "ok"
        || sampled_at > now
        || now.saturating_sub(sampled_at) > OPTIONAL_PROVIDER_TTL.as_secs() as i64
    {
        return Ok(ProviderQuotaSample {
            status: if source_status == "error" || source_status == "unavailable" {
                source_status.to_string()
            } else {
                "stale".into()
            },
            sampled_at,
            five_hour_left: None,
            week_left: None,
            five_hour_reset: None,
            week_reset: None,
        });
    }
    if five_hour_left.is_none() && week_left.is_none() {
        return Ok(ProviderQuotaSample {
            status: "unavailable".into(),
            sampled_at,
            five_hour_left: None,
            week_left: None,
            five_hour_reset: None,
            week_reset: None,
        });
    }
    Ok(ProviderQuotaSample {
        status: "ok".into(),
        sampled_at,
        five_hour_left,
        week_left,
        five_hour_reset,
        week_reset,
    })
}

fn import_percent(value: Option<&Value>) -> Result<Option<u8>, String> {
    match value {
        None | Some(Value::Null) => Ok(None),
        Some(value) => value
            .as_u64()
            .filter(|value| *value <= 100)
            .and_then(|value| u8::try_from(value).ok())
            .map(Some)
            .ok_or_else(|| "error".to_string()),
    }
}

fn import_reset(value: Option<&Value>) -> Result<Option<i64>, String> {
    match value {
        None | Some(Value::Null) => Ok(None),
        Some(value) => {
            let reset = value.as_i64().ok_or_else(|| "error".to_string())?;
            Ok(bounded_telemetry_epoch(reset))
        }
    }
}

fn sample_status<T: SampleMeta>(value: &T) -> &str {
    value.status()
}
fn sample_epoch<T: SampleMeta>(value: &T) -> i64 {
    value.sampled_at()
}

fn sleep_interruptible(stop: &AtomicBool, interval: Duration) {
    let end = Instant::now() + interval;
    while !stop.load(Ordering::Acquire) {
        let now = Instant::now();
        if now >= end {
            break;
        }
        thread::sleep((end - now).min(Duration::from_millis(250)));
    }
}

fn epoch_seconds() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs() as i64
}

fn nullable_epoch(value: Option<i64>) -> Value {
    value.map_or(Value::Null, |v| json!(v))
}
fn nullable_u8(value: Option<u8>) -> Value {
    value.map_or(Value::Null, |v| json!(v))
}
fn nullable_u64(value: Option<u64>) -> Value {
    value.map_or(Value::Null, |v| json!(v))
}
fn nullable_number(value: Option<f64>) -> Value {
    value
        .filter(|v| v.is_finite() && *v >= 0.0)
        .map_or(Value::Null, |v| json!(v))
}

fn local_day_bounds(now: DateTime<Local>) -> (NaiveDate, i64) {
    let date = now.date_naive();
    let midnight = date
        .and_hms_opt(0, 0, 0)
        .expect("valid midnight")
        .and_local_timezone(Local)
        .single();
    let epoch = midnight
        .map(|value| value.timestamp())
        .unwrap_or(now.timestamp() - now.time().num_seconds_from_midnight() as i64);
    (date, epoch)
}

fn collect_preferred_daily_usage(
    sessions: &Path,
    ccswitch_database: Option<&Path>,
    pricing: &PricingCatalog,
) -> Result<UsageSample, String> {
    let database = match ccswitch_database {
        Some(database) => match database.try_exists() {
            Ok(true) => Some(database),
            Ok(false) => None,
            Err(error) => {
                tracing::warn!(error = %error, "Cannot inspect CC Switch usage database; not switching sources");
                return Err("error".into());
            }
        },
        None => None,
    };
    if let Some(database) = database {
        let now = Local::now();
        let (today, midnight) = local_day_bounds(now);
        let sample = crate::ccswitch_usage::read_daily(database, midnight, now.timestamp(), pricing)
            .map_err(|error| {
                tracing::warn!(error = %error, "CC Switch usage collection failed; retaining stale cache, not mixing sources");
                "error".to_string()
            })?;
        return Ok(UsageSample {
            status: if sample.partial { "partial" } else { "ok" }.into(),
            sampled_at: epoch_seconds(),
            tokens: sample.tokens,
            cost: sample.cost,
            cost_status: if sample.cost.is_none() { "unavailable" }
                else if sample.cost_partial { "partial" } else { "ok" }.into(),
            day: today.format("%Y-%m-%d").to_string(),
            detail: Some(sample.detail),
        });
    }
    collect_daily_usage(sessions, pricing)
}

fn collect_daily_usage(sessions: &Path, pricing: &PricingCatalog) -> Result<UsageSample, String> {
    let now = Local::now();
    let (today, midnight) = local_day_bounds(now);
    let day = today.format("%Y-%m-%d").to_string();
    if !sessions.is_dir() {
        return Ok(UsageSample {
            status: "unavailable".into(),
            sampled_at: epoch_seconds(),
            tokens: None,
            cost: None,
            cost_status: "unavailable".into(),
            day,
            detail: None,
        });
    }
    let listing = list_session_files(
        sessions,
        "rollout-",
        SESSION_FILE_LIMIT,
        Some(midnight.saturating_sub(86_400)),
    )
    .map_err(|_| "error".to_string())?;
    let mut files = listing.files;
    files.sort_by(|a, b| b.1.cmp(&a.1).then_with(|| a.0.cmp(&b.0)));
    let mut truncated = listing.incomplete || listing.more_files;
    let mut total_tokens = 0u64;
    let mut estimated_cost = 0.0f64;
    let mut saw_usage = false;
    let mut saw_priced = false;
    let mut saw_unpriced = false;
    let mut token_precision_lost = false;
    let mut read_error = false;
    let mut models = UsageModels::default();

    for (path, _modified, size) in files {
        let offset = size.saturating_sub(SESSION_TAIL_BYTES);
        truncated |= offset > 0;
        let file = match fs::File::open(&path) {
            Ok(file) => file,
            Err(_) => {
                read_error = true;
                continue;
            }
        };
        let mut reader = std::io::BufReader::new(file);
        if offset > 0 {
            if reader.seek(SeekFrom::Start(offset)).is_err() {
                read_error = true;
                continue;
            }
            let mut discard = Vec::new();
            if reader.read_until(b'\n', &mut discard).is_err() {
                read_error = true;
                continue;
            }
        }
        let mut model: Option<String> = None;
        let mut previous_total: Option<Vec<u64>> = None;
        let mut line = Vec::new();
        loop {
            line.clear();
            match reader.read_until(b'\n', &mut line) {
                Ok(0) => break,
                Ok(_) => {}
                Err(_) => {
                    read_error = true;
                    break;
                }
            }
            let Ok(event) = serde_json::from_slice::<Value>(&line) else {
                continue;
            };
            let Some(payload) = event.get("payload").and_then(Value::as_object) else {
                continue;
            };
            match event.get("type").and_then(Value::as_str) {
                Some("turn_context") => {
                    if let Some(candidate) = payload
                        .get("model")
                        .and_then(Value::as_str)
                        .filter(|s| !s.trim().is_empty())
                    {
                        model = Some(candidate.trim().to_string());
                    }
                    continue;
                }
                Some("event_msg")
                    if payload.get("type").and_then(Value::as_str) == Some("token_count") => {}
                _ => continue,
            }
            let Some(event_at) = event.get("timestamp").and_then(parse_epoch) else {
                truncated = true;
                continue;
            };
            if event_at < midnight || event_at > now.timestamp() {
                continue;
            }
            let Some(info) = payload.get("info").and_then(Value::as_object) else {
                truncated = true;
                continue;
            };
            let Some(last) = info.get("last_token_usage").and_then(Value::as_object) else {
                continue;
            };
            if let Some(total) = info.get("total_token_usage").and_then(Value::as_object) {
                let keys = [
                    "input_tokens",
                    "cached_input_tokens",
                    "cache_write_input_tokens",
                    "output_tokens",
                    "reasoning_output_tokens",
                    "total_tokens",
                ];
                let parsed: Option<Vec<u64>> = keys
                    .iter()
                    .map(|key| strict_u64(total.get(*key).unwrap_or(&Value::from(0))))
                    .collect();
                if let Some(parsed) = parsed {
                    if previous_total.as_ref() == Some(&parsed) {
                        continue;
                    }
                    previous_total = Some(parsed);
                } else {
                    truncated = true;
                }
            }
            let Some(input) = strict_u64(last.get("input_tokens").unwrap_or(&Value::from(0)))
            else {
                truncated = true;
                models.invalidate(model.as_deref());
                continue;
            };
            let Some(cached) =
                strict_u64(last.get("cached_input_tokens").unwrap_or(&Value::from(0)))
            else {
                truncated = true;
                models.invalidate(model.as_deref());
                continue;
            };
            let Some(cache_write) = strict_u64(
                last.get("cache_write_input_tokens")
                    .unwrap_or(&Value::from(0)),
            ) else {
                truncated = true;
                models.invalidate(model.as_deref());
                continue;
            };
            let Some(output) = strict_u64(last.get("output_tokens").unwrap_or(&Value::from(0)))
            else {
                truncated = true;
                models.invalidate(model.as_deref());
                continue;
            };
            let cached_and_write = cached.checked_add(cache_write).unwrap_or_else(|| {
                token_precision_lost = true;
                u64::MAX
            });
            let input_total = input.max(cached_and_write);
            let event_tokens = input_total.checked_add(output).unwrap_or_else(|| {
                token_precision_lost = true;
                u64::MAX
            });
            let buckets = TokenBuckets {
                input: input_total.saturating_sub(cached_and_write),
                output,
                read: cached,
                write: cache_write,
            };
            let estimate = request_cost(pricing, model.as_deref(), buckets);
            models.record(model.as_deref(), buckets, estimate, None, None);
            if event_tokens == 0 {
                continue;
            }
            saw_usage = true;
            total_tokens = total_tokens.checked_add(event_tokens).unwrap_or_else(|| {
                token_precision_lost = true;
                u64::MAX
            });
            token_precision_lost |= total_tokens > MAX_EXACT_TELEMETRY_INTEGER;
            if let Some(cost) = estimate.value {
                estimated_cost += cost;
                saw_priced = true;
            }
            saw_unpriced |= estimate.partial || estimate.value.is_none();
        }
    }

    let visible_tokens = (!token_precision_lost).then_some(total_tokens);
    let (status, tokens, cost, cost_status) = if read_error {
        if saw_usage {
            (
                "partial",
                visible_tokens,
                if saw_priced {
                    Some(round_cost(estimated_cost))
                } else {
                    None
                },
                if token_precision_lost || saw_priced {
                    "partial"
                } else {
                    "unavailable"
                },
            )
        } else {
            ("error", None, None, "unavailable")
        }
    } else if !saw_usage {
        if truncated {
            ("partial", None, None, "unavailable")
        } else {
            ("ok", Some(0), Some(0.0), "ok")
        }
    } else if truncated || saw_unpriced || token_precision_lost {
        (
            if truncated || token_precision_lost {
                "partial"
            } else {
                "ok"
            },
            visible_tokens,
            if saw_priced {
                Some(round_cost(estimated_cost))
            } else {
                None
            },
            if token_precision_lost || saw_priced {
                "partial"
            } else {
                "unavailable"
            },
        )
    } else {
        ("ok", visible_tokens, Some(round_cost(estimated_cost)), "ok")
    };
    let bounded_cost = cost.filter(|value| value.is_finite() && *value <= 1_000_000_000.0);
    let cost_status = if cost.is_some() && bounded_cost.is_none() { "unavailable" } else { cost_status };
    let detail = models.finish("rollout", truncated || read_error || token_precision_lost, None)
        .map_err(|_| "error".to_string())?;
    Ok(UsageSample {
        status: status.into(),
        sampled_at: epoch_seconds(),
        tokens,
        cost: bounded_cost,
        cost_status: cost_status.into(),
        day,
        detail: Some(detail),
    })
}

fn round_cost(value: f64) -> f64 {
    (value * 1_000_000.0).round() / 1_000_000.0
}

fn strict_u64(value: &Value) -> Option<u64> {
    value.as_u64()
}

fn json_truthy(value: &Value) -> bool {
    match value {
        Value::Null => false,
        Value::Bool(value) => *value,
        Value::Number(value) => value.as_f64().is_some_and(|value| value != 0.0),
        Value::String(value) => !value.is_empty(),
        Value::Array(value) => !value.is_empty(),
        Value::Object(value) => !value.is_empty(),
    }
}

fn parse_epoch(value: &Value) -> Option<i64> {
    if let Some(number) = value.as_i64() {
        return Some(number);
    }
    if let Some(number) = value.as_u64() {
        return i64::try_from(number).ok();
    }
    let text = value.as_str()?;
    DateTime::parse_from_rfc3339(text)
        .ok()
        .map(|time| time.timestamp())
        .or_else(|| {
            NaiveDateTime::parse_from_str(text, "%Y-%m-%dT%H:%M:%S")
                .ok()
                .and_then(|dt| dt.and_local_timezone(Local).single())
                .map(|dt| dt.timestamp())
        })
}

fn bounded_telemetry_epoch(value: i64) -> Option<i64> {
    (MIN_TELEMETRY_EPOCH..=MAX_TELEMETRY_EPOCH)
        .contains(&value)
        .then_some(value)
}

fn telemetry_reset(value: Option<&Value>) -> Option<i64> {
    value
        .and_then(parse_epoch)
        .and_then(bounded_telemetry_epoch)
}

struct SessionFileListing {
    files: Vec<(PathBuf, i64, u64)>,
    more_files: bool,
    incomplete: bool,
}

fn list_session_files(
    root: &Path,
    prefix: &str,
    limit: usize,
    minimum_modified: Option<i64>,
) -> std::io::Result<SessionFileListing> {
    let mut pending = vec![(root.to_path_buf(), 0usize)];
    let mut newest = BinaryHeap::<Reverse<(i64, PathBuf, u64)>>::with_capacity(limit);
    let mut candidate_count = 0usize;
    let mut visited_entries = 0usize;
    let mut incomplete = false;
    let mut traversal_limited = false;
    while let Some((directory, depth)) = pending.pop() {
        if depth > SESSION_DIRECTORY_DEPTH_LIMIT {
            incomplete = true;
            continue;
        }
        let entries = match fs::read_dir(directory) {
            Ok(entries) => entries,
            Err(error) if depth == 0 => return Err(error),
            Err(_) => {
                incomplete = true;
                continue;
            }
        };
        for entry in entries {
            if visited_entries >= SESSION_TRAVERSAL_ENTRY_LIMIT {
                incomplete = true;
                traversal_limited = true;
                break;
            }
            visited_entries += 1;
            let entry = match entry {
                Ok(entry) => entry,
                Err(_) => {
                    incomplete = true;
                    continue;
                }
            };
            let path = entry.path();
            let metadata = match entry.metadata() {
                Ok(metadata) => metadata,
                Err(_) => {
                    incomplete = true;
                    continue;
                }
            };
            if metadata.is_dir() {
                if depth >= SESSION_DIRECTORY_DEPTH_LIMIT {
                    incomplete = true;
                } else {
                    pending.push((path, depth + 1));
                }
                continue;
            }
            let name = entry.file_name();
            let name = name.to_string_lossy();
            if name.ends_with(".jsonl") && name.starts_with(prefix) {
                let modified = metadata
                    .modified()
                    .ok()
                    .and_then(|time| time.duration_since(UNIX_EPOCH).ok())
                    .map(|duration| duration.as_secs() as i64)
                    .unwrap_or_else(|| {
                        incomplete = true;
                        i64::MAX
                    });
                if minimum_modified.is_some_and(|minimum| modified < minimum) {
                    continue;
                }
                candidate_count = candidate_count.saturating_add(1);
                if limit == 0 {
                    continue;
                }
                let candidate = Reverse((modified, path, metadata.len()));
                if newest.len() < limit {
                    newest.push(candidate);
                } else if newest.peek().is_some_and(|oldest| candidate.0 > oldest.0) {
                    newest.pop();
                    newest.push(candidate);
                }
            }
        }
        if traversal_limited {
            break;
        }
    }
    let mut files: Vec<_> = newest
        .into_vec()
        .into_iter()
        .map(|Reverse((modified, path, size))| (path, modified, size))
        .collect();
    files.sort_by(|a, b| b.1.cmp(&a.1).then_with(|| a.0.cmp(&b.0)));
    Ok(SessionFileListing {
        files,
        more_files: candidate_count > limit,
        incomplete,
    })
}

fn collect_quota(sessions: &Path, auth: &Path) -> Result<QuotaSample, String> {
    if sessions.is_dir() {
        if let Ok(Some(sample)) = session_quota(sessions) {
            return Ok(sample);
        }
    }
    fetch_http_quota(auth).or_else(|status| Err(status))
}

fn session_quota(sessions: &Path) -> Result<Option<QuotaSample>, String> {
    let listing = list_session_files(sessions, "", QUOTA_FILE_LIMIT, None)
        .map_err(|_| "error".to_string())?;
    let mut partial = listing.incomplete || listing.more_files;
    let files = listing.files;
    let mut best: Option<(String, i64, QuotaSample)> = None;
    let mut saw_error = false;
    let now = epoch_seconds();
    for (path, _, size) in files {
        let offset = size.saturating_sub(QUOTA_TAIL_BYTES);
        let file = match fs::File::open(&path) {
            Ok(file) => file,
            Err(_) => {
                saw_error = true;
                continue;
            }
        };
        let mut reader = std::io::BufReader::new(file);
        if offset > 0 {
            if reader.seek(SeekFrom::Start(offset)).is_err() {
                saw_error = true;
                continue;
            }
            let mut discard = Vec::new();
            if reader.read_until(b'\n', &mut discard).is_err() {
                saw_error = true;
                continue;
            }
        }
        let mut line = Vec::new();
        loop {
            line.clear();
            match reader.read_until(b'\n', &mut line) {
                Ok(0) => break,
                Ok(_) => {}
                Err(_) => {
                    saw_error = true;
                    break;
                }
            }
            let Ok(event) = serde_json::from_slice::<Value>(&line) else {
                continue;
            };
            if event.get("type").and_then(Value::as_str) != Some("event_msg") {
                continue;
            }
            let Some(payload) = event.get("payload").and_then(Value::as_object) else {
                continue;
            };
            if payload.get("type").and_then(Value::as_str) != Some("token_count") {
                continue;
            }
            let Some(limits) = payload.get("rate_limits").and_then(Value::as_object) else {
                continue;
            };
            if limits.get("limit_name").is_some_and(json_truthy) {
                continue;
            }
            let mut sample = quota_from_windows(
                limits.get("primary"),
                limits.get("secondary"),
                limits
                    .get("plan_type")
                    .and_then(Value::as_str)
                    .unwrap_or(""),
            );
            if sample.five_hour_left.is_none() && sample.week_left.is_none() {
                continue;
            }
            let Some(event_at) = event.get("timestamp").and_then(parse_epoch) else {
                continue;
            };
            if event_at > now {
                partial = true;
                continue;
            }
            let reset = sample
                .five_hour_reset
                .into_iter()
                .chain(sample.week_reset)
                .max()
                .unwrap_or(-1);
            let key = (
                event
                    .get("timestamp")
                    .and_then(Value::as_str)
                    .unwrap_or("")
                    .to_string(),
                reset,
            );
            if best
                .as_ref()
                .is_some_and(|(old, old_reset, _)| (&key.0, key.1) <= (old, *old_reset))
            {
                continue;
            }
            sample.status = if partial { "partial" } else { "ok" }.into();
            sample.sampled_at = event_at;
            best = Some((key.0, key.1, sample));
        }
    }
    if let Some((_, _, mut sample)) = best {
        if sample.sampled_at > now
            || now.saturating_sub(sample.sampled_at) > QUOTA_TTL.as_secs() as i64
        {
            Ok(None)
        } else {
            sample.status = if partial || saw_error {
                "partial"
            } else {
                "ok"
            }
            .into();
            Ok(Some(sample))
        }
    } else if saw_error {
        Err("error".into())
    } else {
        Ok(None)
    }
}

fn quota_from_windows(
    primary: Option<&Value>,
    secondary: Option<&Value>,
    plan: &str,
) -> QuotaSample {
    let mut output = QuotaSample {
        status: "unavailable".into(),
        plan: plan.to_string(),
        ..QuotaSample::default()
    };
    let mut unclassified: Vec<(bool, u8, Option<i64>)> = Vec::new();
    for (is_primary, window) in [(true, primary), (false, secondary)] {
        let Some(window) = window.and_then(Value::as_object) else {
            continue;
        };
        let Some(used) = window
            .get("used_percent")
            .and_then(Value::as_f64)
            .filter(|v| v.is_finite())
        else {
            continue;
        };
        let used = used.clamp(0.0, 100.0) as u8;
        let duration = window
            .get("window_minutes")
            .and_then(Value::as_i64)
            .or_else(|| {
                window
                    .get("limit_window_seconds")
                    .and_then(Value::as_i64)
                    .map(|seconds| seconds / 60)
            });
        let reset = telemetry_reset(window.get("resets_at").or_else(|| window.get("reset_at")));
        match duration {
            Some(300) => {
                output.five_hour_left = Some(100 - used);
                output.five_hour_reset = reset;
            }
            Some(10080) => {
                output.week_left = Some(100 - used);
                output.week_reset = reset;
            }
            _ => unclassified.push((is_primary, used, reset)),
        }
    }
    for (is_primary, used, reset) in unclassified {
        let target = if is_primary {
            &mut output.five_hour_left
        } else {
            &mut output.week_left
        };
        if target.is_none() {
            *target = Some(100 - used);
            if is_primary {
                output.five_hour_reset = reset;
            } else {
                output.week_reset = reset;
            }
        }
    }
    if output.five_hour_left.is_some() || output.week_left.is_some() {
        output.status = "ok".into();
    }
    output
}

fn fetch_http_quota(auth_path: &Path) -> Result<QuotaSample, String> {
    let auth: Value = fs::read(auth_path)
        .ok()
        .and_then(|bytes| serde_json::from_slice(&bytes).ok())
        .ok_or_else(|| "unavailable".to_string())?;
    let token = auth
        .get("tokens")
        .and_then(|v| v.get("access_token"))
        .and_then(Value::as_str)
        .filter(|s| !s.is_empty())
        .ok_or_else(|| "unavailable".to_string())?;
    let client = reqwest::blocking::Client::builder()
        .timeout(Duration::from_secs(8))
        .build()
        .map_err(|_| "error".to_string())?;
    let response = client
        .get("https://chatgpt.com/backend-api/wham/usage")
        .bearer_auth(token)
        .send()
        .map_err(|_| "error".to_string())?;
    if !response.status().is_success() {
        return Err("error".into());
    }
    let payload: Value = response.json().map_err(|_| "error".to_string())?;
    let limits = payload.get("rate_limit");
    let mut quota = quota_from_windows(
        limits.and_then(|v| v.get("primary_window")),
        limits.and_then(|v| v.get("secondary_window")),
        payload
            .get("plan_type")
            .and_then(Value::as_str)
            .unwrap_or(""),
    );
    if quota.status != "ok" {
        return Err("unavailable".into());
    }
    quota.sampled_at = epoch_seconds();
    Ok(quota)
}

fn collect_host() -> Result<HostSample, String> {
    use sysinfo::{Disks, Networks, System};
    let mut system = System::new_all();
    system.refresh_memory();
    system.refresh_cpu_all();
    thread::sleep(Duration::from_millis(250));
    system.refresh_cpu_all();
    let cpu = finite_clamp(system.global_cpu_usage() as f64, 0.0, 100.0);
    let total_memory = system.total_memory();
    let memory = if total_memory > 0 {
        finite_clamp(
            system.used_memory() as f64 * 100.0 / total_memory as f64,
            0.0,
            100.0,
        )
    } else {
        None
    };
    let disks = Disks::new_with_refreshed_list();
    let total_disk: u64 = disks.iter().map(|disk| disk.total_space()).sum();
    let free_disk: u64 = disks.iter().map(|disk| disk.available_space()).sum();
    let disk = (total_disk > 0)
        .then(|| (total_disk.saturating_sub(free_disk)) as f64 * 100.0 / total_disk as f64)
        .and_then(|value| finite_clamp(value, 0.0, 100.0));
    let mut networks = Networks::new_with_refreshed_list();
    networks.refresh(true);
    let before_rx: u64 = networks.iter().map(|(_, data)| data.total_received()).sum();
    let before_tx: u64 = networks
        .iter()
        .map(|(_, data)| data.total_transmitted())
        .sum();
    thread::sleep(Duration::from_millis(500));
    networks.refresh(true);
    let after_rx: u64 = networks.iter().map(|(_, data)| data.total_received()).sum();
    let after_tx: u64 = networks
        .iter()
        .map(|(_, data)| data.total_transmitted())
        .sum();
    let down = Some(after_rx.saturating_sub(before_rx) as f64 / 1024.0 / 0.5);
    let up = Some(after_tx.saturating_sub(before_tx) as f64 / 1024.0 / 0.5);
    let metrics = [cpu, memory, disk, up, down];
    let available = metrics.iter().filter(|value| value.is_some()).count();
    let status = if available == 0 {
        "error"
    } else if available == metrics.len() {
        "ok"
    } else {
        "partial"
    };
    Ok(HostSample {
        status: status.into(),
        sampled_at: epoch_seconds(),
        cpu,
        memory,
        disk,
        up,
        down,
    })
}

fn finite_clamp(value: f64, min: f64, max: f64) -> Option<f64> {
    (value.is_finite()).then(|| value.clamp(min, max))
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;

    fn sampler_with_state(state: CacheState) -> TelemetrySampler {
        TelemetrySampler {
            state: Arc::new(RwLock::new(state)),
            stop: Arc::new(AtomicBool::new(true)),
            workers: Mutex::new(Vec::new()),
            host_enabled: true,
        }
    }

    fn quota_event(
        timestamp: String,
        limit_name: Value,
        used_percent: u64,
        reset_at: i64,
    ) -> Value {
        json!({
            "timestamp": timestamp,
            "type": "event_msg",
            "payload": {
                "type": "token_count",
                "rate_limits": {
                    "limit_name": limit_name,
                    "primary": {
                        "used_percent": used_percent,
                        "window_minutes": 300,
                        "resets_at": reset_at
                    }
                }
            }
        })
    }

    fn append_json_line(path: &Path, value: &Value) {
        let mut file = fs::OpenOptions::new()
            .create(true)
            .append(true)
            .open(path)
            .unwrap();
        serde_json::to_writer(&mut file, value).unwrap();
        file.write_all(b"\n").unwrap();
    }

    #[test]
    fn quota_windows_classify_by_duration_and_keep_unknown_absent() {
        let primary = json!({"used_percent": 25, "window_minutes": 300, "resets_at": 1900000000});
        let secondary =
            json!({"used_percent": 80, "window_minutes": 10080, "resets_at": 1900000001});
        let quota = quota_from_windows(Some(&primary), Some(&secondary), "plus");
        assert_eq!(quota.five_hour_left, Some(75));
        assert_eq!(quota.week_left, Some(20));
        assert_eq!(quota.status, "ok");
        let unknown = quota_from_windows(None, None, "");
        assert_eq!(unknown.five_hour_left, None);
        assert_eq!(unknown.week_left, None);
    }

    #[test]
    fn reset_epochs_outside_device_range_are_nulled_without_dropping_percentages() {
        let primary = json!({
            "used_percent": 25,
            "window_minutes": 300,
            "resets_at": MIN_TELEMETRY_EPOCH - 1
        });
        let secondary = json!({
            "used_percent": 80,
            "window_minutes": 10080,
            "resets_at": MAX_TELEMETRY_EPOCH + 1
        });
        let quota = quota_from_windows(Some(&primary), Some(&secondary), "plus");
        assert_eq!(quota.five_hour_left, Some(75));
        assert_eq!(quota.week_left, Some(20));
        assert_eq!(quota.five_hour_reset, None);
        assert_eq!(quota.week_reset, None);

        assert_eq!(
            bounded_telemetry_epoch(MIN_TELEMETRY_EPOCH),
            Some(MIN_TELEMETRY_EPOCH)
        );
        assert_eq!(
            bounded_telemetry_epoch(MAX_TELEMETRY_EPOCH),
            Some(MAX_TELEMETRY_EPOCH)
        );

        let claude = parse_claude_quota_response(
            200,
            &json!({
                "five_hour": {
                    "utilization": 25,
                    "resets_at": MIN_TELEMETRY_EPOCH - 1
                },
                "seven_day": {
                    "utilization": 80,
                    "resets_at": MAX_TELEMETRY_EPOCH + 1
                }
            }),
            epoch_seconds(),
        )
        .unwrap();
        assert_eq!(claude.five_hour_left, Some(75));
        assert_eq!(claude.week_left, Some(20));
        assert_eq!(claude.five_hour_reset, None);
        assert_eq!(claude.week_reset, None);

        let now = epoch_seconds();
        let opencode = parse_opencode_quota_import(
            &json!({
                "at": now,
                "s": "ok",
                "h": 45,
                "w": 55,
                "hr": MIN_TELEMETRY_EPOCH - 1,
                "wr": MAX_TELEMETRY_EPOCH + 1
            }),
            now,
        )
        .unwrap();
        assert_eq!(opencode.status, "ok");
        assert_eq!(opencode.five_hour_left, Some(45));
        assert_eq!(opencode.week_left, Some(55));
        assert_eq!(opencode.five_hour_reset, None);
        assert_eq!(opencode.week_reset, None);
    }

    #[test]
    fn token_counts_above_exact_json_integer_limit_are_not_serialized() {
        let sample_for = |input: u64, output: u64| {
            let dir = tempfile::tempdir().unwrap();
            let sessions = dir.path().join("sessions");
            fs::create_dir_all(&sessions).unwrap();
            let path = sessions.join("rollout-large-count.jsonl");
            let timestamp = Local::now().to_rfc3339();
            append_json_line(
                &path,
                &json!({"type": "turn_context", "payload": {"model": "unpriced"}}),
            );
            append_json_line(
                &path,
                &json!({
                    "timestamp": timestamp,
                    "type": "event_msg",
                    "payload": {
                        "type": "token_count",
                        "info": {
                            "last_token_usage": {
                                "input_tokens": input,
                                "cached_input_tokens": 0,
                                "cache_write_input_tokens": 0,
                                "output_tokens": output
                            }
                        }
                    }
                }),
            );
            let pricing = PricingCatalog::new(dir.path().join("prices.json"), None).unwrap();
            collect_daily_usage(&sessions, &pricing).unwrap()
        };

        let exact_max = sample_for(MAX_EXACT_TELEMETRY_INTEGER, 0);
        assert_eq!(exact_max.tokens, Some(MAX_EXACT_TELEMETRY_INTEGER));

        let over_limit = sample_for(MAX_EXACT_TELEMETRY_INTEGER + 1, 0);
        assert_eq!(over_limit.status, "partial");
        assert_eq!(over_limit.tokens, None);
        assert_eq!(over_limit.cost_status, "partial");

        let overflow = sample_for(u64::MAX, 1);
        assert_eq!(overflow.status, "partial");
        assert_eq!(overflow.tokens, None);
        assert_eq!(overflow.cost_status, "partial");
    }

    #[test]
    fn usage_cache_from_previous_local_day_does_not_leak_into_snapshot() {
        let now = epoch_seconds();
        let today = Local::now().date_naive();
        let yesterday = today.pred_opt().unwrap().format("%Y-%m-%d").to_string();
        let mut usage = CacheEntry::default();
        usage.value = Some(UsageSample {
            status: "ok".into(),
            sampled_at: now,
            tokens: Some(900),
            cost: Some(0.25),
            cost_status: "ok".into(),
            day: yesterday,
            detail: None,
        });
        usage.updated = Some(Instant::now());
        usage.sample_status = "ok".into();
        usage.last_attempt_status = "ok".into();

        let sampler = sampler_with_state(CacheState {
            usage,
            ..CacheState::default()
        });
        let value = sampler.snapshot();

        assert_eq!(value["usage"]["s"], "stale");
        assert!(value["usage"]["t"].is_null());
        assert!(value["usage"]["c"].is_null());
        assert_eq!(value["usage"]["day"], today.format("%Y-%m-%d").to_string());
    }

    #[test]
    fn future_codex_and_opencode_quota_timestamps_are_not_fresh() {
        let now = epoch_seconds();
        let future = now + 60;
        let import = parse_opencode_quota_import(
            &json!({
                "at": future,
                "s": "ok",
                "h": 40,
                "w": 70,
                "hr": future + 300,
                "wr": future + 600
            }),
            now,
        )
        .unwrap();
        assert_eq!(import.status, "stale");
        assert_eq!(import.five_hour_left, None);
        assert_eq!(import.week_left, None);

        let mut quota = CacheEntry::default();
        quota.value = Some(QuotaSample {
            status: "ok".into(),
            sampled_at: future,
            five_hour_left: Some(40),
            week_left: Some(70),
            five_hour_reset: Some(future + 300),
            week_reset: Some(future + 600),
            plan: "plus".into(),
        });
        quota.updated = Some(Instant::now());
        quota.sample_status = "ok".into();
        quota.last_attempt_status = "ok".into();
        let sampler = sampler_with_state(CacheState {
            quota,
            ..CacheState::default()
        });
        let snapshot = sampler.snapshot();
        assert_eq!(snapshot["usage"]["q"], "stale");
        assert!(snapshot["usage"]["h"].is_null());
        assert!(sampler.quota_plan().is_none());

        let dir = tempfile::tempdir().unwrap();
        let sessions = dir.path().join("sessions");
        fs::create_dir_all(&sessions).unwrap();
        append_json_line(
            &sessions.join("rollout-future.jsonl"),
            &quota_event(
                DateTime::<chrono::Utc>::from_timestamp(future, 0)
                    .unwrap()
                    .to_rfc3339(),
                Value::Null,
                60,
                future + 300,
            ),
        );
        assert!(session_quota(&sessions).unwrap().is_none());
    }

    #[test]
    fn quota_limit_name_null_and_empty_keep_python_truthy_compatibility() {
        let dir = tempfile::tempdir().unwrap();
        let sessions = dir.path().join("sessions");
        fs::create_dir_all(&sessions).unwrap();
        let path = sessions.join("rollout-quota.jsonl");
        let timestamp = Local::now().to_rfc3339();
        let now = epoch_seconds();

        append_json_line(
            &path,
            &quota_event(timestamp.clone(), Value::Null, 25, now + 300),
        );
        let null_sample = session_quota(&sessions).unwrap().unwrap();
        assert_eq!(null_sample.five_hour_left, Some(75));

        append_json_line(&path, &quota_event(timestamp, json!(""), 40, now + 600));
        let empty_sample = session_quota(&sessions).unwrap().unwrap();
        assert_eq!(empty_sample.five_hour_left, Some(60));
    }

    #[test]
    fn quota_scan_finds_latest_session_beyond_the_old_traversal_prefix() {
        let dir = tempfile::tempdir().unwrap();
        let sessions = dir.path().join("sessions");
        fs::create_dir_all(&sessions).unwrap();
        for index in 0..134 {
            fs::write(sessions.join(format!("rollout-{index:03}.jsonl")), b"{}\n").unwrap();
        }
        let paths: Vec<_> = fs::read_dir(&sessions)
            .unwrap()
            .map(|entry| entry.unwrap().path())
            .collect();
        assert_eq!(paths.len(), 134);
        let latest_path = paths.last().unwrap();
        let timestamp = Local::now().to_rfc3339();
        let now = epoch_seconds();
        fs::write(
            latest_path,
            format!("{}\n", quota_event(timestamp, Value::Null, 25, now + 300)),
        )
        .unwrap();
        fs::File::options()
            .write(true)
            .open(latest_path)
            .unwrap()
            .set_times(
                fs::FileTimes::new().set_modified(SystemTime::now() + Duration::from_secs(10)),
            )
            .unwrap();

        let scan_order: Vec<_> = fs::read_dir(&sessions)
            .unwrap()
            .map(|entry| entry.unwrap().path())
            .collect();
        assert_eq!(scan_order.last(), Some(latest_path));
        let sample = session_quota(&sessions).unwrap().unwrap();
        assert_eq!(sample.five_hour_left, Some(75));
        assert_eq!(sample.status, "partial");
    }

    #[test]
    fn fresh_partial_usage_and_host_samples_keep_known_values() {
        let now = epoch_seconds();
        let day = Local::now().date_naive().format("%Y-%m-%d").to_string();
        let mut usage = CacheEntry::default();
        usage.value = Some(UsageSample {
            status: "partial".into(),
            sampled_at: now,
            tokens: Some(123),
            cost: Some(0.0123),
            cost_status: "partial".into(),
            day,
            detail: None,
        });
        usage.updated = Some(Instant::now());
        usage.sample_status = "partial".into();
        usage.last_attempt_status = "partial".into();
        let mut host = CacheEntry::default();
        host.value = Some(HostSample {
            status: "partial".into(),
            sampled_at: now,
            cpu: Some(42.0),
            memory: None,
            disk: Some(63.0),
            up: Some(2.5),
            down: None,
        });
        host.updated = Some(Instant::now());
        host.sample_status = "partial".into();
        host.last_attempt_status = "partial".into();

        let sampler = sampler_with_state(CacheState {
            usage,
            host,
            ..CacheState::default()
        });
        let value = sampler.snapshot();

        assert_eq!(value["usage"]["s"], "ok");
        assert_eq!(value["usage"]["t"], 123);
        assert_eq!(value["usage"]["c"], 0.0123);
        assert_eq!(value["usage"]["cs"], "partial");
        assert_eq!(value["host"]["s"], "ok");
        assert_eq!(value["host"]["cpu"], 42.0);
        assert_eq!(value["host"]["disk"], 63.0);
        assert!(value["host"]["mem"].is_null());
        assert!(value["host"]["down"].is_null());
    }

    #[test]
    fn claude_provider_parser_is_read_only_fail_closed_and_converts_used_to_remaining() {
        let now = 2_000_000_000;
        let payload = json!({
            "five_hour": {"utilization": 67.4, "resets_at": now + 1800},
            "seven_day": {"utilization": 12.0, "resets_at": now + 86400}
        });
        let sample = parse_claude_quota_response(200, &payload, now).unwrap();
        assert_eq!(sample.status, "ok");
        assert_eq!(sample.five_hour_left, Some(33));
        assert_eq!(sample.week_left, Some(88));
        assert_eq!(sample.five_hour_reset, Some(now + 1800));

        assert_eq!(
            parse_claude_quota_response(401, &payload, now).unwrap_err(),
            "error"
        );
        assert_eq!(
            parse_claude_quota_response(200, &json!({"usage": {"quota": 50}}), now).unwrap_err(),
            "unavailable"
        );
        assert_eq!(
            parse_claude_quota_response(200, &json!({"five_hour": {"utilization": 101}}), now)
                .unwrap_err(),
            "unavailable"
        );
    }

    #[test]
    fn opencode_quota_import_is_read_only_bounded_and_expires_after_120_seconds() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("opencode-quota.json");
        let now = epoch_seconds();
        let encoded = serde_json::to_vec(&json!({
            "at": now,
            "s": "ok",
            "h": 44,
            "w": null,
            "hr": now + 3600,
            "wr": null
        }))
        .unwrap();
        fs::write(&path, &encoded).unwrap();
        let sample = read_opencode_quota_import(&path).unwrap();
        assert_eq!(sample.five_hour_left, Some(44));
        assert_eq!(sample.week_left, None);
        assert_eq!(fs::read(&path).unwrap(), encoded);

        let stale = parse_opencode_quota_import(
            &json!({
                "at": now - 121,
                "s": "ok",
                "h": 44,
                "w": 70,
                "hr": now + 3600,
                "wr": now + 7200
            }),
            now,
        )
        .unwrap();
        assert_eq!(stale.status, "stale");
        assert_eq!(stale.five_hour_left, None);
        assert_eq!(stale.week_reset, None);
        assert!(
            parse_opencode_quota_import(
                &json!({"at": now, "s": "ok", "h": 101, "w": null, "hr": null, "wr": null}),
                now
            )
            .is_err()
        );
        assert!(parse_opencode_quota_import(
            &json!({"at": now, "s": "ok", "h": 20, "w": null, "hr": null, "wr": null, "extra": 1}),
            now
        )
        .is_err());

        fs::write(&path, vec![b'x'; OPTIONAL_IMPORT_MAX_BYTES + 1]).unwrap();
        assert_eq!(read_opencode_quota_import(&path).unwrap_err(), "error");
    }

    #[test]
    fn optional_provider_snapshot_is_whitelisted_and_omitted_by_default() {
        let now = epoch_seconds();
        let mut claude = CacheEntry::default();
        claude.value = Some(ProviderQuotaSample {
            status: "ok".into(),
            sampled_at: now,
            five_hour_left: Some(31),
            week_left: Some(79),
            five_hour_reset: Some(now + 500),
            week_reset: None,
        });
        claude.updated = Some(Instant::now());
        claude.sample_status = "ok".into();
        claude.last_attempt_status = "ok".into();
        let sample = TelemetrySampler {
            state: Arc::new(RwLock::new(CacheState {
                claude: Some(claude),
                ..CacheState::default()
            })),
            stop: Arc::new(AtomicBool::new(true)),
            workers: Mutex::new(Vec::new()),
            host_enabled: true,
        };
        let value = sample.snapshot();
        let provider = value["usage"]["p"][0].as_object().unwrap();
        let fields: std::collections::BTreeSet<_> = provider.keys().map(String::as_str).collect();
        assert_eq!(fields, ["h", "hr", "n", "s", "w", "wr"].into());
        assert_eq!(provider["n"], "claude");
        assert_eq!(provider["h"], 31);
        assert_eq!(provider["w"], 79);
        assert!(value.to_string().find("Bearer").is_none());

        let default_sampler = TelemetrySampler {
            state: Arc::new(RwLock::new(CacheState::default())),
            stop: Arc::new(AtomicBool::new(true)),
            workers: Mutex::new(Vec::new()),
            host_enabled: true,
        };
        assert!(default_sampler.snapshot()["usage"].get("p").is_none());
    }

    #[test]
    fn optional_provider_expiry_nulls_old_quota() {
        let now = epoch_seconds();
        let mut entry = CacheEntry::default();
        entry.value = Some(ProviderQuotaSample {
            status: "ok".into(),
            sampled_at: now - 121,
            five_hour_left: Some(20),
            week_left: Some(40),
            five_hour_reset: Some(now + 100),
            week_reset: Some(now + 200),
        });
        entry.updated = Some(Instant::now());
        entry.sample_status = "ok".into();
        entry.last_attempt_status = "ok".into();
        let view = optional_provider_view("opencode", &entry, now);
        assert_eq!(view["s"], "stale");
        assert!(view["h"].is_null());
        assert!(view["w"].is_null());
        assert!(view["hr"].is_null());
        assert!(view["wr"].is_null());
    }

    #[test]
    fn status_snapshot_does_not_wait_for_a_blocked_quota_worker() {
        let state = Arc::new(RwLock::new(CacheState::default()));
        let stop = Arc::new(AtomicBool::new(false));
        let (started_tx, started_rx) = std::sync::mpsc::channel();
        let (release_tx, release_rx) = std::sync::mpsc::channel();
        let worker = spawn_worker(
            Arc::clone(&state),
            Arc::clone(&stop),
            Duration::from_secs(60),
            |state| &mut state.quota,
            move || {
                started_tx.send(()).unwrap();
                let _ = release_rx.recv_timeout(Duration::from_secs(2));
                Err("error".into())
            },
        );
        started_rx.recv_timeout(Duration::from_secs(1)).unwrap();
        let sampler = TelemetrySampler {
            state,
            stop,
            workers: Mutex::new(vec![worker]),
            host_enabled: true,
        };

        let before = Instant::now();
        let snapshot = sampler.snapshot();
        assert!(before.elapsed() < Duration::from_millis(150));
        assert_eq!(snapshot["usage"]["q"], "unavailable");
        release_tx.send(()).unwrap();
        sampler.close();
    }

    #[test]
    fn daily_usage_counts_changed_total_snapshots_once_and_prices_cache_separately() {
        let dir = tempfile::tempdir().unwrap();
        let sessions = dir.path().join("sessions");
        fs::create_dir_all(&sessions).unwrap();
        let pricing = PricingCatalog::new(dir.path().join("prices.json"), None).unwrap();
        pricing
            .install_public_json(
                br#"{"openai":{"models":{"test":{"cost":{"input":2,"output":10,"cache_read":0.5,"cache_write":1}}}}}"#,
            )
            .unwrap();
        let timestamp = Local::now().to_rfc3339();
        let event = |total, last| {
            json!({
                "timestamp": timestamp,
                "type": "event_msg",
                "payload": {
                    "type": "token_count",
                    "info": {
                        "last_token_usage": last,
                        "total_token_usage": total
                    }
                }
            })
        };
        let first_total = json!({
            "input_tokens": 15,
            "cached_input_tokens": 5,
            "cache_write_input_tokens": 2,
            "output_tokens": 3,
            "reasoning_output_tokens": 4,
            "total_tokens": 18
        });
        let first_last = json!({
            "input_tokens": 10,
            "cached_input_tokens": 5,
            "cache_write_input_tokens": 2,
            "output_tokens": 3
        });
        let second_total = json!({
            "input_tokens": 19,
            "cached_input_tokens": 7,
            "cache_write_input_tokens": 3,
            "output_tokens": 5,
            "reasoning_output_tokens": 5,
            "total_tokens": 24
        });
        let second_last = json!({
            "input_tokens": 4,
            "cached_input_tokens": 2,
            "cache_write_input_tokens": 1,
            "output_tokens": 2
        });
        let mut file = fs::File::create(sessions.join("rollout-usage.jsonl")).unwrap();
        serde_json::to_writer(
            &mut file,
            &json!({"type":"turn_context","payload":{"model":"test"}}),
        )
        .unwrap();
        use std::io::Write;
        file.write_all(b"\n").unwrap();
        for value in [
            event(first_total.clone(), first_last),
            event(first_total, json!({"input_tokens": 10, "output_tokens": 3})),
            event(second_total, second_last),
        ] {
            serde_json::to_writer(&mut file, &value).unwrap();
            file.write_all(b"\n").unwrap();
        }

        let sample = collect_daily_usage(&sessions, &pricing).unwrap();
        assert_eq!(sample.status, "ok");
        assert_eq!(sample.tokens, Some(19));
        assert_eq!(sample.cost_status, "ok");
        assert_eq!(sample.cost, Some(0.000065));
    }

    #[test]
    fn quota_nulls_and_compact_schema_are_present_without_real_paths() {
        let sample = TelemetrySampler {
            state: Arc::new(RwLock::new(CacheState::default())),
            stop: Arc::new(AtomicBool::new(true)),
            workers: Mutex::new(Vec::new()),
            host_enabled: true,
        };
        let value = sample.snapshot();
        assert_eq!(value["telemetry_v"], 1);
        assert_eq!(value["usage"]["q"], "unavailable");
        assert!(value["usage"]["h"].is_null());
        assert!(value["usage"].get("p").is_none());
        assert!(value["host"]["cpu"].is_null());
    }
}
