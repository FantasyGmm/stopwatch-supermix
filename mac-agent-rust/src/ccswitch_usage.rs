//! Read only CC Switch's imported Codex request statistics, never its providers
//! or settings. Token semantics follow its cache-normalized usage aggregation;
//! prices remain the Agent's public models.dev catalog (no CCS multipliers).

use crate::pricing::PricingCatalog;
use anyhow::{Context, Result, bail};
use rusqlite::{Connection, OpenFlags, params};
use serde_json::{Value, json};
use std::{collections::BTreeMap, path::Path, sync::mpsc, thread, time::{Duration, Instant}};

const MAX_REQUESTS: usize = 250_000;
const QUERY_TIMEOUT: Duration = Duration::from_secs(2);
const MAX_EXACT_INTEGER: u64 = 9_007_199_254_740_991;
const MAX_DETAIL_MODELS: usize = 6;
const MAX_DETAIL_NUMBER: f64 = 1_000_000_000.0;

pub(crate) struct DailyUsage {
    pub tokens: Option<u64>,
    pub cost: Option<f64>,
    pub partial: bool,
    pub cost_partial: bool,
    pub detail: Value,
}

/// Cache-normalized buckets. Output already includes reasoning tokens.
#[derive(Clone, Copy, Default)]
pub(crate) struct TokenBuckets {
    pub input: u64,
    pub output: u64,
    pub read: u64,
    pub write: u64,
}

impl TokenBuckets {
    pub fn input_total(self) -> Option<u64> {
        self.input.checked_add(self.read)?.checked_add(self.write)
    }

    pub fn total(self) -> Option<u64> {
        self.input_total()?.checked_add(self.output)
    }
}

#[derive(Clone, Copy)]
pub(crate) struct RequestCost {
    pub value: Option<f64>,
    pub partial: bool,
}

/// Look up each request's context tier before aggregation. Never use the CCS
/// stored bill/cost or its configurable multipliers as an API-equivalent price.
pub(crate) fn request_cost(
    pricing: &PricingCatalog,
    model: Option<&str>,
    buckets: TokenBuckets,
) -> RequestCost {
    if buckets.total() == Some(0) {
        return RequestCost { value: Some(0.0), partial: false };
    }
    let Some(rates) = buckets.input_total().and_then(|input| model.and_then(|name| pricing.rates(name, input))) else {
        return RequestCost { value: None, partial: true };
    };
    let mut partial = rates.partial;
    let mut priced = false;
    let mut cost = 0.0;
    for (count, rate) in [(buckets.input, Some(rates.input)), (buckets.read, rates.cache_read),
                         (buckets.write, rates.cache_write), (buckets.output, Some(rates.output))] {
        if count == 0 { continue; }
        if let Some(rate) = rate.filter(|rate| rate.is_finite() && *rate >= 0.0) {
            cost += count as f64 * rate / 1_000_000.0;
            priced = true;
        } else {
            partial = true;
        }
    }
    RequestCost { value: (priced && cost.is_finite()).then_some(cost), partial }
}

#[derive(Default)]
struct ModelUsage {
    requests: u64,
    buckets: TokenBuckets,
    tokens: u64,
    exact: bool,
    partial: bool,
    cost: f64,
    priced: bool,
    cost_partial: bool,
    tps_output: u128,
    tps_latency_ms: u128,
    tps_samples: u64,
    first_token_ms: Vec<u64>,
}

/// Shared by the CCS reader and the bounded rollout fallback. This is private
/// collection state; only the compact, bounded projection leaves the worker.
#[derive(Default)]
pub(crate) struct UsageModels {
    models: BTreeMap<String, ModelUsage>,
}

impl UsageModels {
    fn model(&mut self, name: Option<&str>) -> &mut ModelUsage {
        // An explicit unknown bucket avoids silently merging long prefixes or
        // sending control/non-ASCII bytes to the small device model label.
        let name = name.map(str::trim).filter(|name| !name.is_empty() && name.len() <= 48
            && name.bytes().all(|byte| (b' '..=b'~').contains(&byte)))
            .unwrap_or("[unknown model]");
        let model = self.models.entry(name.into()).or_insert_with(|| ModelUsage {
            exact: true, ..ModelUsage::default()
        });
        model.requests += 1;
        model
    }

    pub fn invalidate(&mut self, name: Option<&str>) {
        let model = self.model(name);
        model.partial = true;
        model.cost_partial = true;
    }

    pub fn record(
        &mut self,
        name: Option<&str>,
        buckets: TokenBuckets,
        cost: RequestCost,
        latency_ms: Option<i64>,
        first_token_ms: Option<i64>,
    ) {
        let model = self.model(name);
        for (total, count) in [(&mut model.buckets.input, buckets.input),
                               (&mut model.buckets.output, buckets.output),
                               (&mut model.buckets.read, buckets.read),
                               (&mut model.buckets.write, buckets.write)] {
            if let Some(sum) = total.checked_add(count).filter(|sum| *sum <= MAX_EXACT_INTEGER) {
                *total = sum;
            } else {
                model.exact = false;
            }
        }
        if let Some(sum) = buckets.total().and_then(|tokens| model.tokens.checked_add(tokens))
            .filter(|sum| *sum <= MAX_EXACT_INTEGER) {
            model.tokens = sum;
        } else {
            model.exact = false;
        }
        model.cost_partial |= cost.partial;
        if let Some(cost) = cost.value {
            model.cost += cost;
            model.priced |= buckets.total().is_some_and(|tokens| tokens > 0);
        }
        // CCS session TPS is output / full latency, not streaming duration and
        // not latency minus the importer's estimated first-token time.
        let latency_ms = latency_ms.filter(|latency| *latency > 0 && *latency <= MAX_DETAIL_NUMBER as i64);
        let first_token_ms = first_token_ms.filter(|first| *first > 0 && *first <= MAX_DETAIL_NUMBER as i64);
        if let Some(latency) = latency_ms.filter(|latency| *latency >= 1000) {
            if buckets.output >= 200 {
                model.tps_output += buckets.output as u128;
                model.tps_latency_ms += latency as u128;
                model.tps_samples += 1;
            }
        }
        if let (Some(first), Some(latency)) = (first_token_ms, latency_ms) {
            if first > 0 && first <= latency {
                model.first_token_ms.push(first as u64);
            }
        }
    }

    pub fn finish(self, source: &str, incomplete: bool, started: Option<Instant>) -> Result<Value> {
        let mut partial = incomplete;
        let mut more = 0_usize;
        let mut visible: Vec<(String, ModelUsage)> = Vec::with_capacity(MAX_DETAIL_MODELS + 1);
        for (name, model) in self.models {
            if started.is_some_and(|started| started.elapsed() >= QUERY_TIMEOUT) {
                bail!("CC Switch usage query timed out");
            }
            partial |= model.partial || !model.exact || model.requests > MAX_EXACT_INTEGER;
            if !model.exact || model.requests > MAX_EXACT_INTEGER {
                more += 1;
                continue;
            }
            visible.push((name, model));
            visible.sort_by(|a, b| b.1.tokens.cmp(&a.1.tokens).then_with(|| a.0.cmp(&b.0)));
            if visible.len() > MAX_DETAIL_MODELS {
                visible.pop();
                more += 1;
            }
        }
        // Only the six selected rows need JSON allocation and median work.
        let rows = visible.into_iter().map(|(name, mut model)| {
            let cost = ((model.priced || (model.tokens == 0 && !model.partial))
                && model.cost.is_finite() && model.cost <= MAX_DETAIL_NUMBER)
                .then(|| (model.cost * 1_000_000.0).round() / 1_000_000.0);
            let cost_status = if cost.is_none() { "unavailable" }
                else if incomplete || model.partial || model.cost_partial { "partial" } else { "ok" };
            let tps = (model.tps_samples > 0)
                .then(|| model.tps_output as f64 * 1000.0 / model.tps_latency_ms as f64)
                .filter(|value| value.is_finite() && *value > 0.0 && *value <= MAX_DETAIL_NUMBER);
            let tps_samples = tps.map_or(0, |_| model.tps_samples);
            let first_samples = model.first_token_ms.len();
            let first = if first_samples == 0 { None } else {
                let middle = first_samples / 2;
                let (lower, upper, _) = model.first_token_ms.select_nth_unstable(middle);
                let median = if first_samples % 2 == 0 {
                    (*lower.iter().max().expect("even median has a lower half") as f64 + *upper as f64) / 2.0
                } else { *upper as f64 };
                (median > 0.0 && median <= MAX_DETAIL_NUMBER).then_some(median)
            };
            let first_samples = first.map_or(0, |_| first_samples);
            json!({
                "m": name, "n": model.requests, "t": model.tokens,
                "i": model.buckets.input, "o": model.buckets.output,
                "r": model.buckets.read, "w": model.buckets.write,
                "c": cost, "cs": cost_status, "p": incomplete || model.partial,
                "ts": tps, "ft": first, "tn": tps_samples, "fn": first_samples,
            })
        }).collect::<Vec<_>>();
        if started.is_some_and(|started| started.elapsed() >= QUERY_TIMEOUT) {
            bail!("CC Switch usage query timed out");
        }
        Ok(json!({"v": 1, "src": source, "p": partial, "m": rows, "more": more}))
    }
}

pub(crate) fn read_daily(
    database: &Path,
    midnight: i64,
    now: i64,
    pricing: &PricingCatalog,
) -> Result<DailyUsage> {
    let connection = Connection::open_with_flags(
        database,
        OpenFlags::SQLITE_OPEN_READ_ONLY | OpenFlags::SQLITE_OPEN_NO_MUTEX,
    ).context("cannot open CC Switch usage database read-only")?;
    connection.busy_timeout(Duration::from_millis(500))?;

    // A query can spend time inside SQLite before yielding a row. Interrupt it
    // independently of row iteration so collection and shutdown stay bounded.
    let started = Instant::now();
    let interrupt = connection.get_interrupt_handle();
    let (done_tx, done_rx) = mpsc::channel();
    let timer = thread::Builder::new().name("ccswitch-query-deadline".into())
        .spawn(move || {
            if matches!(done_rx.recv_timeout(QUERY_TIMEOUT), Err(mpsc::RecvTimeoutError::Timeout)) {
                interrupt.interrupt();
            }
        })?;
    let result = query_daily(&connection, midnight, now, pricing, started);
    let _ = done_tx.send(());
    let _ = timer.join();
    result
}

fn query_daily(
    connection: &Connection,
    midnight: i64,
    now: i64,
    pricing: &PricingCatalog,
    started: Instant,
) -> Result<DailyUsage> {
    if started.elapsed() >= QUERY_TIMEOUT { bail!("CC Switch usage query timed out"); }
    let version: i64 = connection.query_row("PRAGMA user_version", [], |row| row.get(0))?;
    if version != 20 {
        bail!("unsupported CC Switch usage schema {version}; expected 20");
    }
    // Today is still in request detail, not historical rollups. Only use the
    // imported session source: adding proxy rows could count one request twice.
    // Keep epoch comparisons indexable on (app_type, created_at).
    let mut statement = connection.prepare(
        "SELECT input_token_semantics, model, pricing_model, input_tokens, \
         output_tokens, cache_read_tokens, cache_creation_tokens, latency_ms, first_token_ms \
         FROM proxy_request_logs \
         WHERE app_type = 'codex' AND data_source = 'codex_session' \
         AND created_at >= ?1 AND created_at <= ?2 LIMIT ?3",
    ).context("CC Switch request statistics schema is incompatible")?;
    let mut rows = statement.query(params![midnight, now, (MAX_REQUESTS + 1) as i64])?;
    let mut tokens = 0_u64;
    let mut cost = 0.0_f64;
    let mut partial = false;
    let mut cost_partial = false;
    let mut priced = false;
    let mut valid_rows = 0_usize;
    let mut seen_rows = 0_usize;
    let mut models = UsageModels::default();
    loop {
        if started.elapsed() >= QUERY_TIMEOUT { bail!("CC Switch usage query timed out"); }
        let Some(row) = rows.next()? else { break; };
        seen_rows += 1;
        if seen_rows > MAX_REQUESTS {
            partial = true;
            break;
        }
        // Timing fields are independent: an absent/malformed estimate must not
        // discard otherwise valid token and pricing data.
        let model = row.get::<_, Option<String>>(1).ok().flatten();
        let pricing_model = row.get::<_, Option<String>>(2).ok().flatten();
        let latency = row.get::<_, Option<i64>>(7).ok().flatten();
        let first_token = row.get::<_, Option<i64>>(8).ok().flatten();
        let parsed = (|| -> rusqlite::Result<_> {
            Ok((row.get::<_, i64>(0)?, row.get::<_, i64>(3)?,
                row.get::<_, i64>(4)?, row.get::<_, i64>(5)?, row.get::<_, i64>(6)?))
        })();
        let Ok((semantics, input, output, read, write)) = parsed else {
            partial = true;
            models.invalidate(model.as_deref());
            continue;
        };
        if [input, output, read, write].iter().any(|count| *count < 0) {
            partial = true;
            models.invalidate(model.as_deref());
            continue;
        }
        let (input, output, read, write) = (input as u64, output as u64, read as u64, write as u64);
        // 0: legacy input includes cache reads; 1: input includes reads+writes;
        // 2: fresh input. Reasoning tokens are already in output, not an extra
        // bucket. Unknown semantics cannot be treated as zero consumption.
        let fresh = match semantics {
            0 if input >= read => input - read,
            1 if read.checked_add(write).is_some_and(|cache| input >= cache) => input - read - write,
            2 => input,
            _ => { partial = true; models.invalidate(model.as_deref()); continue; }
        };
        let Some(input_total) = fresh.checked_add(read).and_then(|v| v.checked_add(write)) else {
            partial = true;
            models.invalidate(model.as_deref());
            continue;
        };
        let Some(event_tokens) = input_total.checked_add(output) else {
            partial = true;
            models.invalidate(model.as_deref());
            continue;
        };
        valid_rows += 1;
        tokens = tokens.checked_add(event_tokens).unwrap_or(u64::MAX);
        let price_name = pricing_model.as_deref().filter(|name| !name.trim().is_empty())
            .or(model.as_deref());
        let buckets = TokenBuckets { input: fresh, output, read, write };
        let estimate = request_cost(pricing, price_name, buckets);
        models.record(model.as_deref(), buckets, estimate, latency, first_token);
        cost_partial |= estimate.partial;
        if let Some(estimate) = estimate.value {
            cost += estimate;
            priced |= event_tokens > 0;
        }
    }
    let exact = tokens <= MAX_EXACT_INTEGER;
    partial |= !exact;
    let tokens = (exact && (valid_rows > 0 || seen_rows == 0)).then_some(tokens);
    let cost = if seen_rows == 0 || (tokens == Some(0) && !partial) { Some(0.0) } else if priced && cost.is_finite() && cost <= MAX_DETAIL_NUMBER {
        Some((cost * 1_000_000.0).round() / 1_000_000.0)
    } else { None };
    let detail = models.finish("ccs_session", partial, Some(started))?;
    if started.elapsed() >= QUERY_TIMEOUT { bail!("CC Switch usage query timed out"); }
    Ok(DailyUsage { tokens, cost, partial, cost_partial: partial || cost_partial, detail })
}
