//! Public models.dev catalog plus explicit USD CC Switch v1 overrides. CC Switch
//! fields are interpreted only when their names state a per-million unit.
//! No account credentials or provider API keys are needed for price updates.
use anyhow::{Context, Result, bail};
use serde_json::{Value, json};
use std::{
    collections::BTreeMap,
    fs,
    io::Read,
    path::{Path, PathBuf},
    sync::{Mutex, RwLock},
    time::{Duration, SystemTime, UNIX_EPOCH},
};

#[cfg(target_os = "macos")]
use std::os::unix::fs::{MetadataExt, OpenOptionsExt};

pub const PRICE_URL: &str = "https://models.dev/api.json";
pub const REFRESH_SECONDS: u64 = 6 * 60 * 60;
pub const MAX_CATALOG_BYTES: usize = 8 * 1024 * 1024;
const STALE_SECONDS: u64 = 7 * 24 * 60 * 60;

#[derive(Clone, Debug)]
pub struct Rates {
    pub input: f64,
    pub cache_read: Option<f64>,
    pub cache_write: Option<f64>,
    pub output: f64,
    pub partial: bool,
}

#[derive(Clone)]
struct Entry {
    rates: Rates,
    tiers: Vec<(u64, Rates)>,
    manual: bool,
}
struct Catalog {
    entries: BTreeMap<String, Entry>,
    checked_at: u64,
}

pub struct PricingCatalog {
    data: RwLock<Catalog>,
    refresh_lock: Mutex<()>,
    cache: PathBuf,
    overrides: Option<PathBuf>,
    client: reqwest::blocking::Client,
}

fn unix_seconds() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs()
}

fn number(value: Option<&Value>) -> Option<f64> {
    let v = value?;
    let n = if let Some(s) = v.as_str() {
        s.parse().ok()?
    } else {
        v.as_f64()?
    };
    (n.is_finite() && (0.0..=1_000_000.0).contains(&n)).then_some(n)
}

fn optional_number(value: &Value, key: &str) -> Option<Option<f64>> {
    match value.get(key) {
        None => Some(None),
        Some(Value::Null) => Some(None),
        Some(value) => number(Some(value)).map(Some),
    }
}

fn raw_rates(cost: &Value) -> Option<Rates> {
    Some(Rates {
        input: number(cost.get("input"))?,
        output: number(cost.get("output"))?,
        cache_read: optional_number(cost, "cache_read")?,
        cache_write: optional_number(cost, "cache_write")?,
        partial: false,
    })
}

fn cc_rates(model: &Value) -> Option<Rates> {
    Some(Rates {
        input: number(model.get("inputCostPerMillion"))?,
        output: number(model.get("outputCostPerMillion"))?,
        cache_read: optional_number(model, "cacheReadCostPerMillion")?,
        cache_write: optional_number(model, "cacheCreationCostPerMillion")?,
        partial: false,
    })
}

fn safe_checked_at(value: Option<&Value>) -> u64 {
    let Some(timestamp) = value.and_then(Value::as_u64) else {
        return 0;
    };
    let now = unix_seconds();
    // A future cache timestamp must never defer refresh or suppress stale
    // marking. Small clock skew is clamped; an implausible future date is stale.
    if timestamp > now.saturating_add(300) {
        0
    } else {
        timestamp.min(now)
    }
}

fn read_bounded(path: &Path) -> Result<Value> {
    let mut options = fs::OpenOptions::new();
    options.read(true);
    #[cfg(target_os = "macos")]
    options.custom_flags(libc::O_NONBLOCK | libc::O_NOFOLLOW | libc::O_CLOEXEC);
    let file = options.open(path)?;
    let opened = file.metadata()?;
    if !opened.file_type().is_file() {
        bail!("price path is not a regular file")
    }
    if opened.len() > MAX_CATALOG_BYTES as u64 {
        bail!("price file exceeds limit")
    }
    #[cfg(target_os = "macos")]
    {
        let named = fs::symlink_metadata(path)?;
        if !named.file_type().is_file()
            || named.dev() != opened.dev()
            || named.ino() != opened.ino()
        {
            bail!("price file identity changed while opening")
        }
    }
    let mut data = Vec::new();
    file.take((MAX_CATALOG_BYTES + 1) as u64)
        .read_to_end(&mut data)?;
    if data.len() > MAX_CATALOG_BYTES {
        bail!("price file exceeds limit")
    }
    Ok(serde_json::from_slice(&data)?)
}

fn parse_cc(value: &Value, manual: bool) -> Result<Catalog> {
    if value.get("version").and_then(Value::as_u64) != Some(1) {
        bail!("unsupported pricing JSON version")
    }
    if value.get("currency").and_then(Value::as_str) != Some("USD") {
        bail!("pricing currency must be explicitly USD")
    }
    let models = value
        .get("models")
        .and_then(Value::as_array)
        .context("models must be an array")?;
    if models.len() > 20000 {
        bail!("too many prices")
    }
    let mut entries = BTreeMap::new();
    for m in models {
        let name = m
            .get("modelId")
            .and_then(Value::as_str)
            .context("missing model ID")?;
        if name.is_empty() || name.len() > 128 {
            bail!("invalid model ID")
        }
        let rates = cc_rates(m).context("invalid price")?;
        let mut tiers = Vec::new();
        if let Some(declared) = m.get("contextTiers") {
            let ts = declared
                .as_array()
                .context("contextTiers must be an array")?;
            for t in ts {
                let threshold = t
                    .get("aboveInputTokens")
                    .and_then(Value::as_u64)
                    .filter(|threshold| *threshold > 0)
                    .context("invalid context threshold")?;
                tiers.push((threshold, cc_rates(t).context("invalid tier price")?));
            }
            tiers.sort_by_key(|t| t.0);
            if tiers.windows(2).any(|pair| pair[0].0 == pair[1].0) {
                bail!("duplicate context tier threshold")
            }
        }
        entries.insert(
            name.to_ascii_lowercase(),
            Entry {
                rates,
                tiers,
                manual,
            },
        );
    }
    Ok(Catalog {
        entries,
        checked_at: safe_checked_at(value.get("fetchedAt")),
    })
}

fn models_dev_tiers(cost: &Value) -> Result<Vec<(u64, Rates)>> {
    let mut tiers = Vec::new();
    if let Some(declared) = cost.get("tiers") {
        let declared = declared
            .as_array()
            .context("context tiers must be an array")?;
        for tier in declared {
            let tier_type = tier
                .pointer("/tier/type")
                .and_then(Value::as_str)
                .context("context tier has no supported type")?;
            if tier_type != "context" {
                bail!("unsupported models.dev tier type: {tier_type}")
            }
            let size = tier
                .pointer("/tier/size")
                .and_then(Value::as_u64)
                .filter(|size| *size > 0)
                .context("invalid context tier threshold")?;
            let rates = raw_rates(tier).context("invalid context tier rates")?;
            tiers.push((size, rates));
        }
    } else if let Some(tier) = cost.get("context_over_200k") {
        let rates = raw_rates(tier).context("invalid legacy context tier rates")?;
        tiers.push((200000, rates));
    }
    tiers.sort_by_key(|tier| tier.0);
    if tiers.windows(2).any(|pair| pair[0].0 == pair[1].0) {
        bail!("duplicate context tier threshold")
    }
    Ok(tiers)
}

fn parse_models_dev(value: &Value) -> Result<Catalog> {
    let providers = value.as_object().context("catalog must be an object")?;
    let mut entries = BTreeMap::new();
    for (provider_id, provider) in providers {
        let Some(models) = provider.get("models").and_then(Value::as_object) else {
            continue;
        };
        for (id, model) in models {
            let Some(cost) = model.get("cost") else {
                continue;
            };
            let Some(rates) = raw_rates(cost) else {
                continue;
            };
            let tiers = models_dev_tiers(cost)?;
            let entry = Entry {
                rates,
                tiers,
                manual: false,
            };
            let qualified = format!("{provider_id}/{id}").to_ascii_lowercase();
            if qualified.len() <= 128 {
                entries.insert(qualified, entry.clone());
            }
            // A bare Codex model identifies OpenAI. Other providers need a
            // namespace, avoiding price collisions between proxy catalogs.
            if provider_id == "openai" && !id.is_empty() && id.len() <= 128 {
                entries.insert(id.to_ascii_lowercase(), entry);
            }
            if entries.len() > 20000 {
                bail!("too many model prices")
            }
        }
    }
    if !entries.keys().any(|k| k.starts_with("openai/")) {
        bail!("catalog has no valid OpenAI prices")
    }
    Ok(Catalog {
        entries,
        checked_at: unix_seconds(),
    })
}

fn as_cc(catalog: &Catalog) -> Value {
    let models: Vec<_> = catalog
        .entries
        .iter()
        .map(|(id, entry)| {
            let r = &entry.rates;
            let tiers: Vec<_> = entry
                .tiers
                .iter()
                .map(|(threshold, t)| {
                    json!({
            "aboveInputTokens": threshold, "inputCostPerMillion": t.input,
            "outputCostPerMillion": t.output, "cacheReadCostPerMillion": t.cache_read,
            "cacheCreationCostPerMillion": t.cache_write })
                })
                .collect();
            json!({ "modelId": id, "displayName": id, "inputCostPerMillion": r.input,
            "outputCostPerMillion": r.output, "cacheReadCostPerMillion": r.cache_read,
            "cacheCreationCostPerMillion": r.cache_write, "contextTiers": tiers })
        })
        .collect();
    json!({"version":1,"currency":"USD","source":PRICE_URL,"fetchedAt":catalog.checked_at,"models":models,"deletedModelIds":[]})
}

impl PricingCatalog {
    pub fn new(cache: PathBuf, overrides: Option<PathBuf>) -> Result<Self> {
        let seed = serde_json::from_str(include_str!("../model-pricing.json"))?;
        let mut catalog = read_bounded(&cache)
            .and_then(|v| parse_cc(&v, false))
            .unwrap_or(parse_cc(&seed, false)?);
        if let Err(error) = Self::overlay(&mut catalog, overrides.as_deref()) {
            tracing::warn!(error = %error, "Invalid pricing override ignored");
        }
        Ok(Self {
            data: RwLock::new(catalog),
            refresh_lock: Mutex::new(()),
            cache,
            overrides,
            client: reqwest::blocking::Client::builder()
                .timeout(Duration::from_secs(8))
                .connect_timeout(Duration::from_secs(3))
                .build()?,
        })
    }

    fn overlay(catalog: &mut Catalog, path: Option<&Path>) -> Result<()> {
        if let Some(path) = path {
            let value = read_bounded(path).context("cannot load explicit price override")?;
            catalog.entries.extend(parse_cc(&value, true)?.entries);
            if let Some(deleted) = value.get("deletedModelIds").and_then(Value::as_array) {
                for id in deleted {
                    if let Some(id) = id.as_str() {
                        catalog.entries.remove(&id.to_ascii_lowercase());
                    }
                }
            }
        }
        Ok(())
    }

    /// In-memory lookup only. Refresh runs on a separate, bounded worker.
    pub fn rates(&self, model: &str, input_tokens: u64) -> Option<Rates> {
        let catalog = self.data.read().ok()?;
        let entry = catalog.entries.get(&model.trim().to_ascii_lowercase())?;
        let mut rates = entry.rates.clone();
        for (threshold, tier) in &entry.tiers {
            if input_tokens > *threshold {
                rates = tier.clone();
            }
        }
        rates.partial |=
            !entry.manual && unix_seconds().saturating_sub(catalog.checked_at) > STALE_SECONDS;
        Some(rates)
    }

    pub fn checked_at(&self) -> u64 {
        self.data.read().map(|c| c.checked_at).unwrap_or(0)
    }

    /// HTTP and JSON work must be called off the realtime task path. Only one
    /// refresh may be in flight; failures preserve the last complete catalog.
    pub fn refresh(&self) -> Result<()> {
        let Ok(_guard) = self.refresh_lock.try_lock() else {
            return Ok(());
        };
        let mut response = self.client.get(PRICE_URL).send()?.error_for_status()?;
        if response
            .content_length()
            .is_some_and(|n| n > MAX_CATALOG_BYTES as u64)
        {
            bail!("catalog exceeds size limit")
        }
        let mut bytes = Vec::new();
        response
            .by_ref()
            .take((MAX_CATALOG_BYTES + 1) as u64)
            .read_to_end(&mut bytes)?;
        if bytes.len() > MAX_CATALOG_BYTES {
            bail!("catalog exceeds size limit")
        }
        self.install_public_json(&bytes)
    }

    pub fn install_public_json(&self, bytes: &[u8]) -> Result<()> {
        if bytes.len() > MAX_CATALOG_BYTES {
            bail!("catalog exceeds size limit")
        }
        let mut catalog = parse_models_dev(&serde_json::from_slice(bytes)?)?;
        // Persist only public prices. Manual overrides remain independently
        // editable and are never overwritten by a network refresh.
        let public = as_cc(&catalog);
        if let Err(error) = Self::overlay(&mut catalog, self.overrides.as_deref()) {
            tracing::warn!(error = %error, "Invalid pricing override ignored");
        }
        if let Some(parent) = self.cache.parent() {
            fs::create_dir_all(parent)?;
        }
        let temp = self.cache.with_extension("json.tmp");
        let mut file = fs::File::create(&temp)?;
        use std::io::Write;
        file.write_all(&serde_json::to_vec(&public)?)?;
        file.sync_all()?;
        fs::rename(temp, &self.cache)?;
        *self
            .data
            .write()
            .map_err(|_| anyhow::anyhow!("pricing lock poisoned"))? = catalog;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn invalid_rates_and_versions_fail_closed() {
        assert!(number(Some(&json!(true))).is_none());
        assert!(number(Some(&json!("NaN"))).is_none());
        assert!(number(Some(&json!(-1))).is_none());
        assert!(parse_cc(&json!({"version":2,"models":[]}), false).is_err());
        assert!(parse_cc(&json!({"version":1,"currency":"CNY","models":[]}), false).is_err());
    }
    #[test]
    fn models_dev_context_tier_and_offline_cache() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("catalog.json");
        let catalog = PricingCatalog::new(path.clone(), None).unwrap();
        let input = json!({"openai":{"models":{"test":{"cost":{"input":2,"output":10,"cache_read":0.1,"cache_write":2.5,"tiers":[{"input":4,"output":15,"cache_read":0.2,"cache_write":5,"tier":{"type":"context","size":272000}}]}}}}});
        catalog
            .install_public_json(&serde_json::to_vec(&input).unwrap())
            .unwrap();
        assert_eq!(catalog.rates("test", 272000).unwrap().input, 2.0);
        assert_eq!(catalog.rates("test", 272001).unwrap().input, 4.0);
        assert_eq!(
            catalog.rates("openai/test", 1).unwrap().cache_read,
            Some(0.1)
        );
        assert!(catalog.rates("another/test", 1).is_none());
        assert!(catalog.install_public_json(b"{}").is_err());
        assert_eq!(catalog.rates("test", 1).unwrap().input, 2.0);
        assert_eq!(
            PricingCatalog::new(path, None)
                .unwrap()
                .rates("test", 1)
                .unwrap()
                .input,
            2.0
        );
    }
    #[test]
    fn cc_switch_override_is_preserved_with_tombstones() {
        let dir = tempfile::tempdir().unwrap();
        let override_path = dir.path().join("override.json");
        fs::write(&override_path, serde_json::to_vec(&json!({"version":1,"currency":"USD","models":[{"modelId":"test","displayName":"Test","inputCostPerMillion":"9","outputCostPerMillion":"18","cacheReadCostPerMillion":"1","cacheCreationCostPerMillion":"10"}],"deletedModelIds":["removed"]})).unwrap()).unwrap();
        let catalog =
            PricingCatalog::new(dir.path().join("cache.json"), Some(override_path.clone()))
                .unwrap();
        catalog.install_public_json(&serde_json::to_vec(&json!({"openai":{"models":{"test":{"cost":{"input":2,"output":10}},"removed":{"cost":{"input":3,"output":9}}}}})).unwrap()).unwrap();
        assert_eq!(catalog.rates("test", 1).unwrap().input, 9.0);
        assert!(catalog.rates("removed", 1).is_none());
        assert!(
            read_bounded(&override_path)
                .unwrap()
                .get("models")
                .is_some()
        );
    }
}
