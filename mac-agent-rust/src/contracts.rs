//! Shared contracts. Status/telemetry carry display results, never credentials.
use serde::{Deserialize, Serialize};
use serde_json::Value;
use std::{
    collections::BTreeMap,
    path::PathBuf,
    sync::{Arc, RwLock},
};

pub const MAX_STATUS_BYTES: usize = 2048;
pub const MAX_TASK_DETAIL_BYTES: usize = 12 * 1024;

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct StatusSnapshot {
    pub codex: Value,
    pub trae: Value,
    pub telemetry: Value,
    #[serde(default)]
    pub details: BTreeMap<String, TaskDetail>,
    #[serde(default)]
    pub notifications: Vec<String>,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct TaskDetail {
    pub route: String,
    pub revision: u64,
    pub source: String,
    pub task_id: String,
    pub project: String,
    pub title: String,
    pub reply: String,
}

#[derive(Clone, Debug)]
pub struct AgentPaths {
    pub codex: PathBuf,
    pub trae: Option<PathBuf>,
    pub state: PathBuf,
    pub pricing_override: Option<PathBuf>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum InputAction {
    Submit,
    ForceSteer,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum VoiceMode {
    PushToTalk,
    PinnedMicrophone,
    TaskConversation,
}

pub trait Desktop: Send + Sync {
    fn readiness(&self) -> Value;
    fn submit(&self, action: InputAction) -> anyhow::Result<bool>;
    fn voice_start(&self, mode: VoiceMode, task: Option<TaskDetail>) -> anyhow::Result<()>;
    fn voice_pcm(&self, samples: &[i16]) -> anyhow::Result<()>;
    fn voice_stop(&self, cancelled: bool) -> anyhow::Result<()>;
    fn update_ui(&self, snapshot: &StatusSnapshot, connection: &str);
    fn pairing_code(&self, code: &str);
    fn show_pet_preview(&self, _package: crate::pet::PetPackage) -> anyhow::Result<()> {
        Ok(())
    }
    fn clear_pet_preview(&self) {}
}

pub type SharedSnapshot = Arc<RwLock<StatusSnapshot>>;

#[derive(Clone, Debug)]
pub struct TransportConfig {
    pub device_name: String,
    pub selected_watch_id: Option<String>,
    pub agent_name: String,
    pub state_dir: PathBuf,
}

#[derive(Clone, Debug)]
pub enum DeviceCommand {
    Reconnect,
    SelectWatch(String),
    RenameAgent(String),
    ForgetWatch(String),
    UploadPet(crate::pet::PetPackage),
    UploadPetRequest {
        package: crate::pet::PetPackage,
        request_id: String,
    },
    CancelPet,
    RestorePet,
    SetPreferences(Value),
    Quit,
}

#[derive(Clone, Debug)]
pub enum TransportEvent {
    Connection { state: String, metadata: Value },
    NotificationDelivered(Vec<String>),
    PetProgress(Value),
    Error(String),
}
