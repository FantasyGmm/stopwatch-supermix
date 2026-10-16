//! Bounded inventory of locally installed task agents.
//!
//! LaunchServices lookup runs on its own background thread. Until one lookup
//! succeeds the inventory is unknown; failed refreshes retain the last result.

use anyhow::{Result, bail};
use serde_json::{Value, json};
use std::{
    sync::{
        Arc, RwLock,
        atomic::{AtomicBool, Ordering},
    },
    thread,
    time::Duration,
};

pub const INVENTORY_FIELD: &str = "agent_inventory";
pub const INVENTORY_VERSION: u64 = 1;
pub const MAX_AGENTS: usize = 8;
pub const MAX_AGENT_ID_BYTES: usize = 16;
const REFRESH_INTERVAL: Duration = Duration::from_secs(45);

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum AgentId {
    Codex,
    Trae,
}

impl AgentId {
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::Codex => "codex",
            Self::Trae => "trae",
        }
    }

    fn from_str(value: &str) -> Result<Option<Self>> {
        if value.is_empty() || value.len() > MAX_AGENT_ID_BYTES
            || !value.bytes().all(|byte| (0x21..=0x7e).contains(&byte)) {
            bail!("agent inventory identifier is invalid");
        }
        match value {
            "codex" => Ok(Some(Self::Codex)),
            "trae" => Ok(Some(Self::Trae)),
            _ => Ok(None),
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct AgentInventory {
    pub agents: Vec<AgentId>,
}

impl AgentInventory {
    fn from_mask(mask: u8) -> Result<Self> {
        if mask & !0b11 != 0 {
            bail!("native agent inventory returned unknown flags");
        }
        let mut agents = Vec::with_capacity(2);
        if mask & 0b01 != 0 {
            agents.push(AgentId::Codex);
        }
        if mask & 0b10 != 0 {
            agents.push(AgentId::Trae);
        }
        Ok(Self { agents })
    }

    pub fn to_value(&self) -> Value {
        json!({
            "v": INVENTORY_VERSION,
            "agents": self.agents.iter().map(|agent| agent.as_str()).collect::<Vec<_>>(),
        })
    }

    pub fn from_telemetry(telemetry: &Value) -> Result<Option<Self>> {
        let Some(value) = telemetry.get(INVENTORY_FIELD) else {
            return Ok(None);
        };
        let object = value
            .as_object()
            .ok_or_else(|| anyhow::anyhow!("agent inventory is not an object"))?;
        if object.get("v").and_then(Value::as_u64) != Some(INVENTORY_VERSION) {
            bail!("agent inventory version is invalid");
        }
        let raw_agents = object
            .get("agents")
            .and_then(Value::as_array)
            .ok_or_else(|| anyhow::anyhow!("agent inventory list is invalid"))?;
        if raw_agents.len() > MAX_AGENTS {
            bail!("agent inventory list is too large");
        }
        let mut agents = Vec::with_capacity(raw_agents.len());
        let mut seen = Vec::with_capacity(raw_agents.len());
        for raw in raw_agents {
            let raw_id = raw
                .as_str()
                .ok_or_else(|| anyhow::anyhow!("agent inventory identifier is invalid"))?;
            let id = AgentId::from_str(raw_id)?;
            if seen.contains(&raw_id) {
                bail!("agent inventory contains a duplicate identifier");
            }
            seen.push(raw_id);
            if let Some(id) = id {
                agents.push(id);
            }
        }
        Ok(Some(Self { agents }))
    }

    pub fn contains(&self, agent: AgentId) -> bool {
        self.agents.contains(&agent)
    }
}

/// `None` means an old snapshot without an inventory; retain its historical
/// behavior. A present inventory is authoritative, including an empty list.
pub fn allows(telemetry: &Value, agent: AgentId) -> Result<bool> {
    Ok(AgentInventory::from_telemetry(telemetry)?
        .is_none_or(|inventory| inventory.contains(agent)))
}

#[derive(Clone, Default)]
pub struct AgentInventoryCache(Arc<RwLock<Option<AgentInventory>>>);

impl AgentInventoryCache {
    pub fn current(&self) -> Option<AgentInventory> {
        self.0
            .read()
            .unwrap_or_else(|error| error.into_inner())
            .clone()
    }

    pub fn inject(&self, snapshot: &mut crate::contracts::StatusSnapshot) {
        let Some(inventory) = self.current() else {
            return;
        };
        if !snapshot.telemetry.is_object() {
            snapshot.telemetry = json!({});
        }
        snapshot.telemetry[INVENTORY_FIELD] = inventory.to_value();
    }

    /// Start a refresh loop that never blocks task collection or UI updates.
    pub fn start_monitor(&self, stop: Arc<AtomicBool>) -> std::io::Result<()> {
        let cache = self.clone();
        thread::Builder::new()
            .name("installed-agent-inventory".into())
            .spawn(move || {
                while !stop.load(Ordering::Acquire) {
                    match detect_installed_agents() {
                        Ok(inventory) => {
                            *cache
                                .0
                                .write()
                                .unwrap_or_else(|error| error.into_inner()) = Some(inventory);
                        }
                        Err(error) => {
                            tracing::warn!(error = %error, "Installed-agent lookup failed; retaining the last inventory");
                        }
                    }
                    let slices = REFRESH_INTERVAL.as_millis() / 250;
                    for _ in 0..slices {
                        if stop.load(Ordering::Acquire) {
                            return;
                        }
                        thread::sleep(Duration::from_millis(250));
                    }
                }
            })?;
        Ok(())
    }
}

#[cfg(target_os = "macos")]
unsafe extern "C" {
    fn sw_detect_installed_agents(mask: *mut u8) -> i32;
}

/// Check only conventional standalone CLI locations. `metadata` follows a
/// symlink, then the target must be a regular file with a Unix execute bit.
/// This deliberately does not invoke `codex`, inspect app bundles, or read
/// shell configuration/authentication files.
#[cfg(target_os = "macos")]
fn codex_cli_is_installed() -> bool {
    use std::os::unix::fs::PermissionsExt;

    let mut candidates = Vec::new();
    if let Some(path) = std::env::var_os("PATH") {
        candidates.extend(
            std::env::split_paths(&path).map(|directory| directory.join("codex")),
        );
    }
    candidates.extend([
        std::path::PathBuf::from("/opt/homebrew/bin/codex"),
        std::path::PathBuf::from("/usr/local/bin/codex"),
    ]);
    if let Some(home) = std::env::var_os("HOME") {
        candidates.push(std::path::PathBuf::from(&home).join(".local/bin/codex"));
        candidates.push(std::path::PathBuf::from(home).join(".cargo/bin/codex"));
    }

    candidates.into_iter().any(|path| {
        let Ok(metadata) = std::fs::metadata(&path) else {
            return false;
        };
        if !metadata.is_file() || metadata.permissions().mode() & 0o111 == 0 {
            return false;
        }
        let Ok(resolved) = std::fs::canonicalize(path) else {
            return false;
        };
        !resolved.components().any(|component| {
            std::path::Path::new(component.as_os_str())
                .extension()
                .is_some_and(|extension| {
                    extension.to_string_lossy().eq_ignore_ascii_case("app")
                })
        })
    })
}

pub fn detect_installed_agents() -> Result<AgentInventory> {
    #[cfg(target_os = "macos")]
    {
        let mut mask = 0_u8;
        // SAFETY: the native function only writes one byte and returns before
        // this stack-owned output pointer leaves scope.
        let result = unsafe { sw_detect_installed_agents(&mut mask) };
        if result != 1 {
            bail!("LaunchServices could not complete the installed-agent lookup");
        }
        // Preserve CLI-only installations that the LaunchServices app lookup
        // cannot see. Never search ChatGPT app internals or execute the CLI.
        if mask & 0b01 == 0 && codex_cli_is_installed() {
            mask |= 0b01;
        }
        AgentInventory::from_mask(mask)
    }

    #[cfg(not(target_os = "macos"))]
    {
        bail!("installed-agent discovery is available only on macOS")
    }
}
