//! Authenticated BLE transport for the StopWatch Mac Agent.
//!
//! All GATT traffic is bounded and session-owned. A reconnect retires the old
//! notification pump and its replay guards before a new generation is exposed.

use anyhow::{Context, Result, anyhow, bail, ensure};
use btleplug::{
    api::{
        Central, CentralState, CharPropFlags, Manager as _, Peripheral as _, ScanFilter,
        ValueNotification, WriteType,
    },
    platform::{Adapter, Manager, Peripheral},
};
use futures::StreamExt;
use p256::{SecretKey, elliptic_curve::sec1::ToEncodedPoint};
use rand::{RngCore, rngs::OsRng};
use serde_json::{Value, json};
use sha2::{Digest, Sha256};
use std::{
    collections::{HashMap, HashSet, VecDeque},
    sync::{
        Arc,
        atomic::{AtomicBool, AtomicU8, Ordering},
    },
    time::{Duration, Instant, SystemTime, UNIX_EPOCH},
};
use tokio::{
    sync::{mpsc, watch},
    task::JoinHandle,
    time::{interval, timeout},
};
use uuid::Uuid;
use zeroize::Zeroizing;

use crate::{
    agent_inventory::{self, AgentId},
    contracts::{
        Desktop, DeviceCommand, InputAction, MAX_TASK_DETAIL_BYTES, StatusSnapshot,
        TransportConfig, TransportEvent, VoiceMode,
    },
    credentials::{
        self, AgentIdentity, AgentIdentityStore, MacOSKeychain, WatchCredential,
        WatchCredentialStore,
    },
    security::{self},
    wire::{
        self, PairFrame, PairMessageType, PetControlPayload, PetMessageKind, PetSource,
        PetStatusPayload,
    },
};

const STATUS_SERVICE: &str = "7a230001-5474-4f50-5741-544348434f44";
const STATUS_RX: &str = "7a230002-5474-4f50-5741-544348434f44";
const VOICE_CONTROL: &str = "7a230003-5474-4f50-5741-544348434f44";
const VOICE_TX: &str = "7a230004-5474-4f50-5741-544348434f44";
const VOICE_SERVICE: &str = "7a230005-5474-4f50-5741-544348434f44";
const PAIRING_SERVICE: &str = "7a230006-5474-4f50-5741-544348434f44";
const PAIRING_CONTROL: &str = "7a230007-5474-4f50-5741-544348434f44";
const PAIRING_TX: &str = "7a230008-5474-4f50-5741-544348434f44";
const INPUT_CONTROL: &str = "7a230009-5474-4f50-5741-544348434f44";
const INPUT_TX: &str = "7a23000a-5474-4f50-5741-544348434f44";
const PET_SERVICE: &str = "7a23000b-5474-4f50-5741-544348434f44";
const PET_CONTROL: &str = "7a23000c-5474-4f50-5741-544348434f44";
const PET_STATUS: &str = "7a23000d-5474-4f50-5741-544348434f44";

const GATT_TIMEOUT: Duration = Duration::from_secs(5);
const BLE_API_TIMEOUT: Duration = Duration::from_secs(10);
const ADAPTER_RETRY: Duration = Duration::from_secs(2);
const SCAN_RETRY: Duration = Duration::from_millis(500);
const CONNECTION_RETRY: Duration = Duration::from_secs(1);
const PAIRING_TIMEOUT: Duration = Duration::from_secs(65);
const INPUT_QUEUE_CAPACITY: usize = 8;
const VOICE_QUEUE_CAPACITY: usize = 502;
const PET_QUEUE_CAPACITY: usize = 8;
const PAIRING_QUEUE_CAPACITY: usize = 8;
const SESSION_STATUS_PERIOD: Duration = Duration::from_secs(2);
const EPOCH_CHECK_PERIOD: Duration = Duration::from_secs(2);
const STATUS_CODEC_READ_TIMEOUT: Duration = Duration::from_millis(500);
const STATUS_CODEC_READ_RETRY: Duration = Duration::from_millis(100);
const STATUS_CODEC_READ_ATTEMPTS: usize = 3;
const PET_SCHEMA_VERSION: u16 = 1;

#[derive(Clone)]
struct Characteristics {
    status_rx: btleplug::api::Characteristic,
    voice_control: btleplug::api::Characteristic,
    voice_tx: btleplug::api::Characteristic,
    pairing_control: btleplug::api::Characteristic,
    pairing_tx: btleplug::api::Characteristic,
    input_control: btleplug::api::Characteristic,
    input_tx: btleplug::api::Characteristic,
    pet_control: Option<btleplug::api::Characteristic>,
    pet_status: Option<btleplug::api::Characteristic>,
}

struct Ingress {
    pairing: mpsc::Receiver<ValueNotification>,
    input: mpsc::Receiver<ValueNotification>,
    voice: mpsc::Receiver<ValueNotification>,
    pet: Arc<tokio::sync::Mutex<mpsc::Receiver<ValueNotification>>>,
    input_overflow: Arc<AtomicBool>,
    voice_overflow: Arc<AtomicBool>,
    pet_overflow: Arc<AtomicBool>,
    pump: JoinHandle<()>,
}

struct ConnectedSession {
    peripheral: Peripheral,
    peripheral_id: String,
    characteristics: Characteristics,
    ingress: Ingress,
    credential: WatchCredential,
    agent_name: String,
    advertisement_name: String,
    generation: u64,
    input_nonce: [u8; 8],
    input_replay: wire::ActionReplayGuard,
    used_input_nonces: VecDeque<[u8; 8]>,
    voice_nonce: [u8; 8],
    voice_capabilities: u8,
    voice_active: bool,
    voice_failed: bool,
    active_voice: Option<ActiveVoiceSession>,
    detail_nonce: [u8; 8],
    detail_sequence: u32,
    detail_cache: HashMap<String, (u64, [u8; 32])>,
    delivered_notifications: HashSet<String>,
    notification_ack_pending: HashSet<String>,
    status_sequence: u16,
    pet_state: Value,
    pet_operation_active: bool,
    status_capacity: usize,
    status_codec: wire::StatusCodec,
    voice_used_nonces: VecDeque<[u8; 8]>,
    highest_voice_session: u16,
    last_trae_payload: Option<Vec<u8>>,
    last_trae_sent: Option<Instant>,
    status_sync_log: crate::status_sync::StatusSyncLog,
}

struct ActiveVoiceSession {
    session: u16,
    nonce: [u8; 8],
    last_sequence: u32,
}

struct ActiveSession {
    watch_id: String,
    peripheral_id: String,
    commands: mpsc::Sender<DeviceCommand>,
    task: JoinHandle<Result<()>>,
}

#[derive(Clone)]
struct ConnectionProgress(Arc<AtomicU8>);

impl ConnectionProgress {
    const IDLE: u8 = 0;
    const PAIRING: u8 = 1;
    const AWAITING_WATCH_APPROVAL: u8 = 2;
    const COMPLETE: u8 = 3;
    const ERROR: u8 = 4;

    fn new() -> Self {
        Self(Arc::new(AtomicU8::new(Self::IDLE)))
    }

    fn set(&self, value: u8) {
        self.0.store(value, Ordering::Release);
    }

    fn pairing_state(&self) -> &'static str {
        match self.0.load(Ordering::Acquire) {
            Self::PAIRING => "pairing",
            Self::AWAITING_WATCH_APPROVAL => "awaiting_watch_approval",
            Self::COMPLETE => "complete",
            Self::ERROR => "error",
            _ => "idle",
        }
    }

    fn fail_pending_pairing(&self) {
        if matches!(
            self.0.load(Ordering::Acquire),
            Self::PAIRING | Self::AWAITING_WATCH_APPROVAL
        ) {
            self.set(Self::ERROR);
        }
    }
}

enum SessionLoopEvent {
    Shutdown(bool),
    Command(Option<DeviceCommand>),
    PetTask(Option<std::result::Result<Result<Value>, tokio::task::JoinError>>),
    Snapshot(bool),
    StatusTick,
    EpochTick,
    Input(ValueNotification),
    Voice(ValueNotification),
    Pairing,
    IdlePetStatus,
    PumpEnded,
}

/// Run the authenticated BLE client and reconnect supervisor. Product callers
/// provide real Keychain-backed stores; protocol tests use only MemoryKeychain.
pub async fn run(
    config: TransportConfig,
    snapshots: watch::Receiver<StatusSnapshot>,
    desktop: Arc<dyn Desktop>,
    events: mpsc::Sender<TransportEvent>,
    mut commands: mpsc::Receiver<DeviceCommand>,
    mut shutdown: watch::Receiver<bool>,
) -> Result<()> {
    let target = parse_watch_id(config.selected_watch_id.as_deref())?;
    let identity_store = Arc::new(if config.agent_name.is_empty() {
        AgentIdentityStore::new(Arc::new(MacOSKeychain))
    } else {
        let configured_name = config.agent_name.clone();
        AgentIdentityStore::with_default_name(Arc::new(MacOSKeychain), move || {
            configured_name.clone()
        })
    });
    let credential_store = Arc::new(WatchCredentialStore::new(Arc::new(MacOSKeychain)));
    let mut last_status_key = None;
    emit_connection_status_if_changed(
        &events,
        &mut last_status_key,
        "initializing",
        "Starting Bluetooth",
        "idle",
        None,
    )
    .await;
    let manager = match timeout(BLE_API_TIMEOUT, Manager::new()).await {
        Ok(Ok(manager)) => manager,
        Ok(Err(error)) => {
            let error = anyhow!(error).context("BLE manager initialization failed");
            emit_transport_failure(
                &events,
                "bluetooth_unavailable",
                "Bluetooth is unavailable",
                "idle",
                &error,
            )
            .await;
            return Err(error).context("BLE manager is unavailable");
        }
        Err(_) => {
            let error = anyhow!("BLE manager initialization timed out after {BLE_API_TIMEOUT:?}");
            emit_transport_failure(
                &events,
                "bluetooth_unavailable",
                "Bluetooth initialization timed out",
                "idle",
                &error,
            )
            .await;
            return Err(error).context("BLE manager is unavailable");
        }
    };
    let mut active: Option<ActiveSession> = None;
    let mut candidate: Option<JoinHandle<Result<ConnectedSession>>> = None;
    let mut candidate_peripheral: Option<Peripheral> = None;
    let mut candidate_expected_target: Option<Option<[u8; 16]>> = None;
    let mut candidate_progress: Option<ConnectionProgress> = None;
    let mut candidate_target = target;
    let mut next_generation = 1u64;
    let mut retry_at = Instant::now();
    let mut retry_delay = CONNECTION_RETRY;
    let mut adapter: Option<Adapter> = None;
    let mut adapter_retry_at = Instant::now();
    let mut last_peripheral_error: Option<String> = None;
    let mut scan = interval(SCAN_RETRY);

    loop {
        if *shutdown.borrow() {
            break;
        }
        if adapter.is_none() && Instant::now() >= adapter_retry_at {
            match timeout(BLE_API_TIMEOUT, manager.adapters()).await {
                Ok(Ok(mut adapters)) if !adapters.is_empty() => {
                    let selected = adapters.remove(0);
                    match timeout(BLE_API_TIMEOUT, selected.adapter_state()).await {
                        Ok(Ok(CentralState::PoweredOn)) => {
                            match timeout(
                                BLE_API_TIMEOUT,
                                selected.start_scan(ScanFilter::default()),
                            )
                            .await
                            {
                                Ok(Ok(())) => {
                                    adapter = Some(selected);
                                    adapter_retry_at = Instant::now();
                                    emit_connection_status_if_changed(
                                        &events,
                                        &mut last_status_key,
                                        "scanning",
                                        "Searching for StopWatch-Codex",
                                        "idle",
                                        None,
                                    )
                                    .await;
                                }
                                Ok(Err(source)) => {
                                    let error = anyhow!(source).context("BLE scan could not start");
                                    emit_transport_failure(
                                        &events,
                                        "bluetooth_unavailable",
                                        "Bluetooth scanning is unavailable",
                                        "idle",
                                        &error,
                                    )
                                    .await;
                                    adapter_retry_at = Instant::now() + ADAPTER_RETRY;
                                }
                                Err(_) => {
                                    let error = anyhow!(
                                        "BLE scan start timed out after {BLE_API_TIMEOUT:?}"
                                    )
                                    .context("BLE scan could not start");
                                    emit_transport_failure(
                                        &events,
                                        "bluetooth_unavailable",
                                        "Bluetooth scanning timed out",
                                        "idle",
                                        &error,
                                    )
                                    .await;
                                    adapter_retry_at = Instant::now() + ADAPTER_RETRY;
                                }
                            }
                        }
                        Ok(Ok(state)) => {
                            let error = anyhow!("Bluetooth adapter is not powered on ({state:?})");
                            emit_transport_failure(
                                &events,
                                "bluetooth_unavailable",
                                "Bluetooth is turned off or unavailable",
                                "idle",
                                &error,
                            )
                            .await;
                            adapter_retry_at = Instant::now() + ADAPTER_RETRY;
                        }
                        Ok(Err(source)) => {
                            let error =
                                anyhow!(source).context("Bluetooth adapter state lookup failed");
                            emit_transport_failure(
                                &events,
                                "bluetooth_unavailable",
                                "Bluetooth adapter state is unavailable",
                                "idle",
                                &error,
                            )
                            .await;
                            adapter_retry_at = Instant::now() + ADAPTER_RETRY;
                        }
                        Err(_) => {
                            let error = anyhow!(
                                "Bluetooth adapter state lookup timed out after {BLE_API_TIMEOUT:?}"
                            );
                            emit_transport_failure(
                                &events,
                                "bluetooth_unavailable",
                                "Bluetooth adapter state lookup timed out",
                                "idle",
                                &error,
                            )
                            .await;
                            adapter_retry_at = Instant::now() + ADAPTER_RETRY;
                        }
                    }
                }
                Ok(Ok(_)) => {
                    let error = anyhow!("no Bluetooth adapter was found");
                    emit_transport_failure(
                        &events,
                        "bluetooth_unavailable",
                        "No Bluetooth adapter was found",
                        "idle",
                        &error,
                    )
                    .await;
                    adapter_retry_at = Instant::now() + ADAPTER_RETRY;
                }
                Ok(Err(source)) => {
                    let error = anyhow!(source).context("Bluetooth adapter discovery failed");
                    emit_transport_failure(
                        &events,
                        "bluetooth_unavailable",
                        "Bluetooth adapter discovery failed",
                        "idle",
                        &error,
                    )
                    .await;
                    adapter_retry_at = Instant::now() + ADAPTER_RETRY;
                }
                Err(_) => {
                    let error =
                        anyhow!("Bluetooth adapter discovery timed out after {BLE_API_TIMEOUT:?}");
                    emit_transport_failure(
                        &events,
                        "bluetooth_unavailable",
                        "Bluetooth adapter discovery timed out",
                        "idle",
                        &error,
                    )
                    .await;
                    adapter_retry_at = Instant::now() + ADAPTER_RETRY;
                }
            }
        }

        tokio::select! {
            changed = shutdown.changed() => {
                if changed.is_err() || *shutdown.borrow() { break; }
            }
            command = commands.recv() => {
                let Some(command) = command else { break; };
                match command {
                    DeviceCommand::Quit => break,
                    DeviceCommand::Reconnect => {
                        if let Some(current) = &active {
                            candidate_target = parse_watch_id(Some(&current.watch_id))?;
                        }
                        stop_active(&mut active).await;
                        retry_at = Instant::now();
                    }
                    DeviceCommand::SelectWatch(value) => match parse_watch_id(Some(&value)) {
                        Ok(selected) => {
                            candidate_target = selected;
                            retry_at = Instant::now();
                        }
                        Err(_) => emit_error(&events, "Selected watch identifier is invalid").await,
                    },
                    DeviceCommand::RenameAgent(value) => {
                        let store = identity_store.clone();
                        let rename = tokio::task::spawn_blocking(move || store.rename(&value)).await;
                        if !matches!(rename, Ok(Ok(_))) {
                            emit_error(&events, "Agent name could not be saved in Keychain").await;
                        } else {
                            if let Some(current) = &active { candidate_target = parse_watch_id(Some(&current.watch_id))?; }
                            retry_at = Instant::now();
                        }
                    }
                    DeviceCommand::ForgetWatch(value) => {
                        match parse_watch_id(Some(&value)) {
                            Ok(Some(watch_id)) => {
                                let store = credential_store.clone();
                                let result = tokio::task::spawn_blocking(move || store.forget(&watch_id)).await;
                                if !matches!(result, Ok(Ok(()))) { emit_error(&events, "Watch credential could not be removed from Keychain").await; }
                                if active.as_ref().is_some_and(|session| session.watch_id == value.to_ascii_lowercase()) {
                                    stop_active(&mut active).await;
                                    candidate_target = Some(watch_id);
                                    retry_at = Instant::now();
                                }
                            }
                            _ => emit_error(&events, "Watch identifier is invalid").await,
                        }
                    }
                    pet_command @ (DeviceCommand::UploadPet(_) | DeviceCommand::UploadPetRequest { .. } | DeviceCommand::CancelPet | DeviceCommand::RestorePet) => {
                        if let Some(current) = &active {
                            if current.commands.try_send(pet_command).is_err() {
                                emit_error(&events, "Pet channel is busy").await;
                            }
                        } else if matches!(pet_command, DeviceCommand::UploadPet(_) | DeviceCommand::UploadPetRequest { .. } | DeviceCommand::RestorePet) {
                            emit_error(&events, "Pet watch is offline").await;
                        }
                    }
                    DeviceCommand::SetPreferences(_) => {}
                }
            }
            _ = tokio::time::sleep(Duration::from_millis(200)), if active.is_some() => {
                if active.as_ref().is_some_and(|current| current.task.is_finished()) {
                    if let Some(old) = active.take() {
                        let failure = match old.task.await {
                            Ok(Ok(())) => None,
                            Ok(Err(error)) => Some(error),
                            Err(error) => Some(anyhow!(error).context("BLE data worker failed")),
                        };
                        if let Some(error) = failure {
                            emit_transport_failure(
                                &events, "error", "Watch data synchronization failed", "complete", &error,
                            ).await;
                        }
                    }
                    emit_connection(&events, "disconnected", json!({})).await;
                    retry_at = Instant::now() + retry_delay;
                    retry_delay = (retry_delay * 2).min(Duration::from_secs(30));
                }
            }
            result = async {
                match candidate.as_mut() {
                    Some(task) => Some(task.await),
                    None => None,
                }
            }, if candidate.is_some() => {
                candidate = None;
                candidate_peripheral = None;
                // The candidate task emits pairing/error/connected transitions directly;
                // let the next scan cycle publish its own connecting/not-found state.
                last_status_key = None;
                let progress = candidate_progress.take().unwrap_or_else(ConnectionProgress::new);
                match result {
                    Some(Ok(Ok(session))) => {
                        let new_target = parse_watch_id(Some(&hex::encode(session.credential.watch_id)))?;
                        let generation_target = candidate_expected_target.take().unwrap_or(candidate_target);
                        if generation_target != candidate_target {
                            retire_connected(session).await;
                            retry_at = Instant::now() + SCAN_RETRY;
                            continue;
                        }
                        if let Some(old) = active.take() { stop_active_value(old).await; }
                        let (tx, rx) = mpsc::channel(16);
                        let watch_id = hex::encode(session.credential.watch_id);
                        let peripheral_id = session.peripheral_id.clone();
                        tracing::info!(state = "connected", "BLE connection state changed");
                        emit_connection(&events, "connected", session_metadata(&session, "connected")).await;
                        let task = tokio::spawn(run_session(session, snapshots.clone(), desktop.clone(), events.clone(), rx, shutdown.clone(), identity_store.clone(), credential_store.clone()));
                        active = Some(ActiveSession { watch_id, peripheral_id, commands: tx, task });
                        retry_delay = CONNECTION_RETRY;
                        candidate_target = new_target;
                    }
                    Some(Ok(Err(error))) => {
                        candidate_expected_target = None;
                        progress.fail_pending_pairing();
                        emit_transport_failure(
                            &events,
                            "error",
                            "Watch connection or authentication failed",
                            progress.pairing_state(),
                            &error,
                        )
                        .await;
                        retry_at = Instant::now() + retry_delay;
                        retry_delay = (retry_delay * 2).min(Duration::from_secs(30));
                    }
                    Some(Err(join_error)) => {
                        candidate_expected_target = None;
                        progress.fail_pending_pairing();
                        let error = anyhow!(join_error).context("BLE connection worker failed");
                        emit_transport_failure(
                            &events,
                            "error",
                            "Watch connection or authentication failed",
                            progress.pairing_state(),
                            &error,
                        )
                        .await;
                        retry_at = Instant::now() + retry_delay;
                        retry_delay = (retry_delay * 2).min(Duration::from_secs(30));
                    }
                    None => {}
                }
            }
            _ = scan.tick() => {
                let Some(adapter_ref) = adapter.as_ref() else { continue; };
                if candidate.is_none() && Instant::now() >= retry_at {
                    let current_physical = active.as_ref().map(|session| session.peripheral_id.as_str());
                    let peripherals = match timeout(BLE_API_TIMEOUT, adapter_ref.peripherals()).await {
                        Ok(Ok(value)) => value,
                        Ok(Err(source)) => {
                            let error = anyhow!(source).context("BLE peripheral discovery failed");
                            adapter = None;
                            adapter_retry_at = Instant::now() + ADAPTER_RETRY;
                            emit_transport_failure(
                                &events,
                                "bluetooth_unavailable",
                                "Bluetooth peripheral discovery failed",
                                "idle",
                                &error,
                            )
                            .await;
                            continue;
                        }
                        Err(_) => {
                            let error = anyhow!("BLE peripheral discovery timed out after {BLE_API_TIMEOUT:?}");
                            adapter = None;
                            adapter_retry_at = Instant::now() + ADAPTER_RETRY;
                            emit_transport_failure(
                                &events,
                                "bluetooth_unavailable",
                                "Bluetooth peripheral discovery timed out",
                                "idle",
                                &error,
                            )
                            .await;
                            continue;
                        }
                    };
                    let mut selected = None;
                    let properties_deadline = Instant::now() + BLE_API_TIMEOUT;
                    for peripheral in peripherals {
                        let id = peripheral.id().to_string();
                        if current_physical == Some(id.as_str()) { continue; }
                        let remaining = properties_deadline.saturating_duration_since(Instant::now());
                        if remaining.is_zero() {
                            let error = anyhow!("BLE peripheral property scan exceeded {BLE_API_TIMEOUT:?}");
                            if last_peripheral_error.as_deref() != Some("scan deadline") {
                                tracing::warn!(error = %format!("{error:#}"), "BLE peripheral metadata lookup stopped");
                                last_peripheral_error = Some("scan deadline".into());
                            }
                            break;
                        }
                        let properties = match timeout(remaining, peripheral.properties()).await {
                            Ok(Ok(value)) => {
                                last_peripheral_error = None;
                                value
                            }
                            Ok(Err(source)) => {
                                let error = anyhow!(source).context("BLE peripheral metadata lookup failed");
                                let chain = format!("{error:#}");
                                if last_peripheral_error.as_deref() != Some(chain.as_str()) {
                                    tracing::warn!(error = %chain, "BLE peripheral metadata lookup failed");
                                    last_peripheral_error = Some(chain);
                                }
                                continue;
                            }
                            Err(_) => {
                                let error = anyhow!("BLE peripheral metadata lookup timed out after {remaining:?}")
                                    .context("BLE peripheral metadata lookup failed");
                                let chain = format!("{error:#}");
                                if last_peripheral_error.as_deref() != Some(chain.as_str()) {
                                    tracing::warn!(error = %chain, "BLE peripheral metadata lookup failed");
                                    last_peripheral_error = Some(chain);
                                }
                                break;
                            }
                        };
                        let name = properties.as_ref().and_then(|p| p.local_name.as_deref().or(p.advertisement_name.as_deref()));
                        let has_service = properties.as_ref().is_some_and(|p| {
                            p.services.iter().any(|service| {
                                service == &uuid_or_nil(STATUS_SERVICE)
                                    || service == &uuid_or_nil(VOICE_SERVICE)
                            })
                        });
                        if name == Some(config.device_name.as_str()) || has_service {
                            selected = Some((peripheral, name.unwrap_or(&config.device_name).to_owned()));
                            break;
                        }
                    }
                    if let Some((peripheral, name)) = selected {
                        let connection_generation = next_generation;
                        next_generation = next_generation.checked_add(1).ok_or_else(|| anyhow!("BLE connection generation exhausted"))?;
                        let target = candidate_target;
                        let identity = identity_store.clone();
                        let credentials = credential_store.clone();
                        let desk = desktop.clone();
                        let progress = ConnectionProgress::new();
                        candidate_progress = Some(progress.clone());
                        emit_connection_status_if_changed(
                            &events,
                            &mut last_status_key,
                            "connecting",
                            "Connecting to the watch",
                            progress.pairing_state(),
                            None,
                        )
                        .await;
                        let connection_events = events.clone();
                        candidate_peripheral = Some(peripheral.clone());
                        candidate = Some(tokio::spawn(async move {
                            connect_and_authorize(peripheral, name, target, connection_generation, identity, credentials, desk, connection_events, progress).await
                        }));
                        candidate_expected_target = Some(target);
                    } else if active.is_none() {
                        emit_connection_status_if_changed(
                            &events,
                            &mut last_status_key,
                            "not_found",
                            "No matching watch was found",
                            "idle",
                            None,
                        )
                        .await;
                    }
                }
            }
        }
    }

    if let Some(task) = candidate.take() {
        task.abort();
        if let Ok(Ok(session)) = task.await {
            session.ingress.pump.abort();
            let _ = session.ingress.pump.await;
        }
    }
    if let Some(peripheral) = candidate_peripheral.take() {
        match timeout(Duration::from_secs(2), peripheral.disconnect()).await {
            Ok(Ok(())) => {}
            Ok(Err(error)) => {
                tracing::warn!(
                    error = %format!("{error:#}"),
                    "Cancelled BLE candidate disconnect failed"
                );
            }
            Err(_) => {
                tracing::warn!(
                    timeout = ?Duration::from_secs(2),
                    "Cancelled BLE candidate disconnect timed out"
                );
            }
        }
    }
    stop_active(&mut active).await;
    if let Some(adapter) = adapter {
        let _ = adapter.stop_scan().await;
    }
    emit_connection(&events, "offline", json!({})).await;
    Ok(())
}

fn uuid(value: &str) -> Result<Uuid> {
    Ok(Uuid::parse_str(value)?)
}
fn is_uuid_v4(value: &str) -> bool {
    Uuid::parse_str(value).is_ok_and(|parsed| {
        let bytes = parsed.as_bytes();
        bytes[6] >> 4 == 4 && bytes[8] & 0xc0 == 0x80
    })
}
fn uuid_or_nil(value: &str) -> Uuid {
    Uuid::parse_str(value).unwrap_or_else(|_| Uuid::nil())
}

fn parse_watch_id(value: Option<&str>) -> Result<Option<[u8; 16]>> {
    let Some(value) = value else {
        return Ok(None);
    };
    if value.len() != 32 || !value.bytes().all(|byte| byte.is_ascii_hexdigit()) {
        bail!("watch identifier must be 32 hexadecimal characters");
    }
    let decoded = hex::decode(value)?;
    let id: [u8; 16] = decoded
        .try_into()
        .map_err(|_| anyhow!("watch identifier is invalid"))?;
    if !id.iter().any(|byte| *byte != 0) {
        bail!("watch identifier cannot be zero");
    }
    Ok(Some(id))
}

async fn stop_active(active: &mut Option<ActiveSession>) {
    if let Some(old) = active.take() {
        stop_active_value(old).await;
    }
}

async fn stop_active_value(mut old: ActiveSession) {
    let _ = old.commands.try_send(DeviceCommand::Quit);
    if timeout(Duration::from_secs(3), &mut old.task)
        .await
        .is_err()
    {
        old.task.abort();
        let _ = old.task.await;
    }
}

async fn emit_connection(events: &mpsc::Sender<TransportEvent>, state: &str, metadata: Value) {
    let _ = timeout(
        Duration::from_secs(2),
        events.send(TransportEvent::Connection {
            state: state.into(),
            metadata,
        }),
    )
    .await;
}

async fn emit_error(events: &mpsc::Sender<TransportEvent>, message: &str) {
    let _ = timeout(
        Duration::from_secs(2),
        events.send(TransportEvent::Error(message.into())),
    )
    .await;
}

const STATUS_DETAIL_MAX_BYTES: usize = 160;
const STATUS_ERROR_MAX_BYTES: usize = 320;

fn bounded_status_text(value: &str, max_bytes: usize) -> String {
    let mut result = String::new();
    for character in value.chars() {
        let character = if character.is_control() {
            ' '
        } else {
            character
        };
        if result.len() + character.len_utf8() > max_bytes {
            break;
        }
        result.push(character);
    }
    result.trim().to_owned()
}

fn status_metadata(detail: &str, pairing_state: &str, error: Option<&str>) -> Value {
    let mut metadata = json!({
        "detail": bounded_status_text(detail, STATUS_DETAIL_MAX_BYTES),
        "pairing_state": pairing_state,
    });
    if let Some(error) = error {
        metadata["error"] = Value::String(bounded_status_text(error, STATUS_ERROR_MAX_BYTES));
    }
    metadata
}

async fn emit_connection_status(
    events: &mpsc::Sender<TransportEvent>,
    state: &str,
    detail: &str,
    pairing_state: &str,
    error: Option<&str>,
) {
    tracing::info!(state, pairing_state, detail, "BLE connection state changed");
    emit_connection(events, state, status_metadata(detail, pairing_state, error)).await;
}

async fn emit_connection_status_if_changed(
    events: &mpsc::Sender<TransportEvent>,
    previous_key: &mut Option<String>,
    state: &str,
    detail: &str,
    pairing_state: &str,
    error: Option<&str>,
) {
    let key = format!(
        "{state}\0{detail}\0{pairing_state}\0{}",
        error.unwrap_or("")
    );
    if previous_key.as_deref() == Some(key.as_str()) {
        return;
    }
    *previous_key = Some(key);
    emit_connection_status(events, state, detail, pairing_state, error).await;
}

async fn emit_transport_failure(
    events: &mpsc::Sender<TransportEvent>,
    state: &str,
    detail: &str,
    pairing_state: &str,
    error: &anyhow::Error,
) {
    let chain = format!("{error:#}");
    tracing::warn!(state, error = %chain, "BLE transport operation failed");
    emit_connection_status(events, state, detail, pairing_state, Some(&chain)).await;
}

fn random_nonzero<const N: usize>() -> [u8; N] {
    loop {
        let mut value = [0u8; N];
        OsRng.fill_bytes(&mut value);
        if value.iter().any(|byte| *byte != 0) {
            return value;
        }
    }
}

fn random_transaction() -> u32 {
    loop {
        let value = OsRng.next_u32();
        if value != 0 {
            return value;
        }
    }
}

async fn connect_and_authorize(
    peripheral: Peripheral,
    advertisement_name: String,
    target_watch: Option<[u8; 16]>,
    generation: u64,
    identity_store: Arc<AgentIdentityStore>,
    credential_store: Arc<WatchCredentialStore>,
    desktop: Arc<dyn Desktop>,
    events: mpsc::Sender<TransportEvent>,
    progress: ConnectionProgress,
) -> Result<ConnectedSession> {
    match timeout(GATT_TIMEOUT, peripheral.connect()).await {
        Ok(Ok(())) => {}
        Ok(Err(source)) => {
            let _ = timeout(GATT_TIMEOUT, peripheral.disconnect()).await;
            return Err(anyhow!(source).context("BLE connection failed"));
        }
        Err(_) => {
            let _ = timeout(GATT_TIMEOUT, peripheral.disconnect()).await;
            bail!("BLE connection timed out after {GATT_TIMEOUT:?}");
        }
    }
    let result = connect_and_authorize_connected(
        peripheral.clone(),
        advertisement_name,
        target_watch,
        generation,
        identity_store,
        credential_store,
        desktop,
        events,
        progress,
    )
    .await;
    if result.is_err() {
        let _ = timeout(GATT_TIMEOUT, peripheral.disconnect()).await;
    }
    result
}

async fn connect_and_authorize_connected(
    peripheral: Peripheral,
    advertisement_name: String,
    target_watch: Option<[u8; 16]>,
    generation: u64,
    identity_store: Arc<AgentIdentityStore>,
    credential_store: Arc<WatchCredentialStore>,
    desktop: Arc<dyn Desktop>,
    events: mpsc::Sender<TransportEvent>,
    progress: ConnectionProgress,
) -> Result<ConnectedSession> {
    timeout(GATT_TIMEOUT, peripheral.discover_services())
        .await
        .context("BLE service discovery timed out")??;
    let services = peripheral.services();
    let status_service = uuid(STATUS_SERVICE)?;
    let pairing_service = uuid(PAIRING_SERVICE)?;
    ensure!(
        services
            .iter()
            .any(|service| service.uuid == status_service),
        "watch status service is missing"
    );
    ensure!(
        services
            .iter()
            .any(|service| service.uuid == pairing_service),
        "watch pairing service is missing"
    );
    let all = peripheral.characteristics();
    let characteristic = |id: &str| -> Result<btleplug::api::Characteristic> {
        let expected = uuid(id)?;
        all.iter()
            .find(|value| value.uuid == expected)
            .cloned()
            .ok_or_else(|| anyhow!("required watch characteristic is missing"))
    };
    let pet_control_uuid = uuid(PET_CONTROL)?;
    let pet_status_uuid = uuid(PET_STATUS)?;
    let pet_service_uuid = uuid(PET_SERVICE)?;
    let pet_control = all
        .iter()
        .find(|value| value.uuid == pet_control_uuid)
        .cloned();
    let pet_status = all
        .iter()
        .find(|value| value.uuid == pet_status_uuid)
        .cloned();
    let pet_available = services
        .iter()
        .any(|service| service.uuid == pet_service_uuid)
        && pet_control.is_some()
        && pet_status.is_some();
    let characteristics = Characteristics {
        status_rx: characteristic(STATUS_RX)?,
        voice_control: characteristic(VOICE_CONTROL)?,
        voice_tx: characteristic(VOICE_TX)?,
        pairing_control: characteristic(PAIRING_CONTROL)?,
        pairing_tx: characteristic(PAIRING_TX)?,
        input_control: characteristic(INPUT_CONTROL)?,
        input_tx: characteristic(INPUT_TX)?,
        pet_control: if pet_available { pet_control } else { None },
        pet_status: if pet_available { pet_status } else { None },
    };
    for item in [
        &characteristics.pairing_tx,
        &characteristics.input_tx,
        &characteristics.voice_tx,
    ] {
        timeout(GATT_TIMEOUT, peripheral.subscribe(item))
            .await
            .context("BLE notification setup timed out")??;
    }
    if let Some(item) = &characteristics.pet_status {
        timeout(GATT_TIMEOUT, peripheral.subscribe(item))
            .await
            .context("pet notification setup timed out")??;
    }

    let mut ingress = notification_pump(&peripheral, &characteristics).await?;
    let authorization = async {
        let store = identity_store.clone();
        let identity = tokio::task::spawn_blocking(move || store.load_or_create())
            .await
            .context("Agent identity worker failed")??;
        let discovery = discover_watch(&peripheral, &characteristics, &mut ingress.pairing).await?;
        if target_watch.is_some_and(|target| target != discovery.watch_id) {
            bail!("selected watch does not match the connected device");
        }
        let credential = {
            let store = credential_store.clone();
            let watch_id = discovery.watch_id;
            tokio::task::spawn_blocking(move || store.load(&watch_id))
                .await
                .context("watch credential worker failed")??
        };
        let credential = match credential {
            Some(credential) => {
                progress.set(ConnectionProgress::COMPLETE);
                emit_connection_status(
                    &events,
                    "connecting",
                    "Authenticating the authorized watch",
                    progress.pairing_state(),
                    None,
                )
                .await;
                if credential.watch_identity_public != discovery.watch_identity_public
                    || credential.fingerprint != identity.fingerprint
                {
                    bail!("watch identity does not match its Keychain credential");
                }
                authenticate_watch(
                    &peripheral,
                    &characteristics,
                    &mut ingress.pairing,
                    &identity,
                    credential,
                    &discovery,
                    &identity.display_name,
                    &credential_store,
                )
                .await?
            }
            None => {
                progress.set(ConnectionProgress::PAIRING);
                emit_connection_status(
                    &events,
                    "pairing",
                    "Starting secure watch pairing",
                    progress.pairing_state(),
                    None,
                )
                .await;
                pair_watch(
                    &peripheral,
                    &characteristics,
                    &mut ingress.pairing,
                    &identity,
                    &discovery,
                    &identity.display_name,
                    &credential_store,
                    &desktop,
                    &events,
                    &progress,
                )
                .await?
            }
        };
        progress.set(ConnectionProgress::COMPLETE);
        ensure!(
            credential.watch_id == discovery.watch_id
                && credential.key_epoch == discovery.key_epoch
                && credential.watch_identity_public == discovery.watch_identity_public
                && credential.fingerprint == identity.fingerprint,
            "watch credential is not current"
        );

        let status_codec = negotiate_status_codec(&peripheral, &characteristics.status_rx).await?;

        let mtu = peripheral.mtu() as usize;
        let advertised = mtu.checked_sub(3);
        let status_capacity = wire::status_write_capacity(advertised);
        tracing::info!(
            codec = ?status_codec,
            frame_version = status_codec.frame_version(),
            write_capacity = status_capacity,
            "BLE status transport negotiated"
        );
        Ok((identity, credential, status_capacity, status_codec))
    }
    .await;
    let (identity, credential, status_capacity, status_codec) = match authorization {
        Ok(value) => value,
        Err(error) => {
            ingress.pump.abort();
            let _ = ingress.pump.await;
            return Err(error);
        }
    };
    Ok(ConnectedSession {
        peripheral: peripheral.clone(),
        peripheral_id: peripheral.id().to_string(),
        characteristics,
        ingress,
        credential,
        agent_name: identity.display_name,
        advertisement_name,
        generation,
        input_nonce: [0; 8],
        input_replay: wire::ActionReplayGuard::default(),
        used_input_nonces: VecDeque::new(),
        voice_nonce: random_nonzero(),
        voice_capabilities: 0,
        voice_active: false,
        voice_failed: false,
        active_voice: None,
        detail_nonce: random_nonzero(),
        detail_sequence: 0,
        detail_cache: HashMap::new(),
        delivered_notifications: HashSet::new(),
        notification_ack_pending: HashSet::new(),
        status_sequence: 0,
        pet_state: initial_pet_state(pet_available, generation),
        pet_operation_active: false,
        status_capacity,
        status_codec,
        voice_used_nonces: VecDeque::new(),
        highest_voice_session: 0,
        last_trae_payload: None,
        last_trae_sent: None,
        status_sync_log: crate::status_sync::StatusSyncLog::default(),
    })
}

async fn negotiate_status_codec(
    peripheral: &Peripheral,
    status_rx: &btleplug::api::Characteristic,
) -> Result<wire::StatusCodec> {
    if !status_rx.properties.contains(CharPropFlags::READ) {
        return Ok(wire::StatusCodec::JsonV1);
    }
    // AUTH_OK can arrive just before firmware promotes the Production session.
    // Bound each async read and the entire retry sequence; never guess a codec
    // after a failed read or an invalid descriptor on READ-capable firmware.
    let mut attempts = 0;
    loop {
        attempts += 1;
        let error = match timeout(STATUS_CODEC_READ_TIMEOUT, peripheral.read(status_rx)).await {
            Ok(Ok(descriptor)) => {
                return wire::StatusCodec::from_capabilities(&descriptor)
                    .context("watch status codec capability descriptor is invalid");
            }
            Ok(Err(error)) => {
                anyhow!(error).context("watch status codec capability read failed")
            }
            Err(_) => {
                anyhow!("watch status codec capability read timed out after {STATUS_CODEC_READ_TIMEOUT:?}")
            }
        };
        if attempts >= STATUS_CODEC_READ_ATTEMPTS {
            return Err(error).context("could not negotiate watch status codec");
        }
        tokio::time::sleep(STATUS_CODEC_READ_RETRY).await;
    }
}

fn initial_pet_state(available: bool, generation: u64) -> Value {
    json!({
        "capability": if available { "authenticating" } else { "unsupported" },
        "source": "builtin", "schema_min": 0, "schema_max": 0,
        "generation": generation, "active_hash": "0000000000000000000000000000000000000000000000000000000000000000",
        "usable": false, "operation_active": false,
    })
}

async fn notification_pump(
    peripheral: &Peripheral,
    characteristics: &Characteristics,
) -> Result<Ingress> {
    let mut stream = peripheral.notifications().await?;
    let (pair_tx, pairing) = mpsc::channel(PAIRING_QUEUE_CAPACITY);
    let (input_tx, input) = mpsc::channel(INPUT_QUEUE_CAPACITY);
    let (voice_tx, voice) = mpsc::channel(VOICE_QUEUE_CAPACITY);
    let (pet_tx, pet) = mpsc::channel(PET_QUEUE_CAPACITY);
    let input_overflow = Arc::new(AtomicBool::new(false));
    let voice_overflow = Arc::new(AtomicBool::new(false));
    let pet_overflow = Arc::new(AtomicBool::new(false));
    let flags = [
        input_overflow.clone(),
        voice_overflow.clone(),
        pet_overflow.clone(),
    ];
    let uuids = (
        characteristics.pairing_tx.uuid,
        characteristics.input_tx.uuid,
        characteristics.voice_tx.uuid,
        characteristics.pet_status.as_ref().map(|value| value.uuid),
    );
    let pump = tokio::spawn(async move {
        while let Some(notification) = stream.next().await {
            if notification.uuid == uuids.0 {
                if pair_tx.try_send(notification.clone()).is_err() {
                    let _ = pair_tx.try_send(notification);
                }
            } else if notification.uuid == uuids.1 {
                if input_tx.try_send(notification).is_err() {
                    flags[0].store(true, Ordering::Release);
                }
            } else if notification.uuid == uuids.2 {
                if voice_tx.try_send(notification).is_err() {
                    flags[1].store(true, Ordering::Release);
                }
            } else if Some(notification.uuid) == uuids.3 {
                if pet_tx.try_send(notification).is_err() {
                    flags[2].store(true, Ordering::Release);
                }
            }
        }
    });
    Ok(Ingress {
        pairing,
        input,
        voice,
        pet: Arc::new(tokio::sync::Mutex::new(pet)),
        input_overflow,
        voice_overflow,
        pet_overflow,
        pump,
    })
}

async fn pair_exchange(
    peripheral: &Peripheral,
    characteristic: &btleplug::api::Characteristic,
    responses: &mut mpsc::Receiver<ValueNotification>,
    request: &[u8],
    expected: &[PairMessageType],
    wait: Duration,
) -> Result<PairFrame> {
    let decoded = wire::decode_pair_frame(request).context("pairing request is invalid")?;
    write_gatt(peripheral, characteristic, request).await?;
    let deadline = tokio::time::Instant::now() + wait;
    loop {
        let remaining = deadline.saturating_duration_since(tokio::time::Instant::now());
        if remaining.is_zero() {
            bail!("watch pairing response timed out");
        }
        let value = timeout(remaining, responses.recv())
            .await
            .map_err(|_| anyhow!("watch pairing response timed out"))?
            .ok_or_else(|| anyhow!("watch pairing channel closed"))?;
        let frame =
            wire::decode_pair_frame(&value.value).context("watch pairing response is malformed")?;
        if frame.transaction_id != decoded.transaction_id {
            continue;
        }
        if matches!(frame.kind, PairMessageType::Error) {
            bail!("watch rejected secure pairing operation");
        }
        if !expected.contains(&frame.kind) {
            bail!("watch returned an unexpected pairing response");
        }
        return Ok(frame);
    }
}

async fn discover_watch(
    peripheral: &Peripheral,
    characteristics: &Characteristics,
    responses: &mut mpsc::Receiver<ValueNotification>,
) -> Result<wire::DiscoveryResponse> {
    let transaction = random_transaction();
    let request = wire::encode_discovery_request(transaction)?;
    let frame = pair_exchange(
        peripheral,
        &characteristics.pairing_control,
        responses,
        &request,
        &[PairMessageType::DiscoveryResponse],
        GATT_TIMEOUT,
    )
    .await?;
    wire::decode_discovery_response(&frame.payload).context("watch discovery response is invalid")
}

async fn pair_watch(
    peripheral: &Peripheral,
    characteristics: &Characteristics,
    responses: &mut mpsc::Receiver<ValueNotification>,
    identity: &AgentIdentity,
    discovery: &wire::DiscoveryResponse,
    configured_name: &str,
    credentials: &Arc<WatchCredentialStore>,
    desktop: &Arc<dyn Desktop>,
    events: &mpsc::Sender<TransportEvent>,
    progress: &ConnectionProgress,
) -> Result<WatchCredential> {
    let display_name = credentials::normalize_agent_name(configured_name)?;
    let transaction = random_transaction();
    let agent_nonce = random_nonzero::<16>();
    let ephemeral = SecretKey::random(&mut OsRng);
    let ephemeral_scalar = Zeroizing::new(<[u8; 32]>::from(ephemeral.to_bytes()));
    let ephemeral_public = security::public_key_bytes(&ephemeral.public_key());
    let claim = security::encode_pairing_identity_claim(
        transaction,
        &agent_nonce,
        &ephemeral_public,
        &identity.public_key,
        &display_name,
    )?;
    let signature = security::sign_challenge(identity.private_scalar(), &claim)?;
    let request = wire::PairingRequest {
        transaction_id: transaction,
        agent_nonce,
        agent_ephemeral_public: ephemeral_public,
        agent_identity_public: identity.public_key,
        display_name: display_name.clone(),
        identity_signature: signature,
    };
    let frame = wire::encode_pairing_request(&request)?;
    let offer_frame = pair_exchange(
        peripheral,
        &characteristics.pairing_control,
        responses,
        &frame,
        &[PairMessageType::PairingOffer],
        GATT_TIMEOUT,
    )
    .await?;
    let offer = wire::decode_pairing_offer(offer_frame.transaction_id, &offer_frame.payload)?;
    ensure!(
        offer.transaction_id == transaction
            && offer.watch_id == discovery.watch_id
            && offer.watch_identity_public == discovery.watch_identity_public,
        "pairing offer is not bound to discovered watch"
    );
    let transcript = security::encode_pairing_transcript(&security::PairingTranscript {
        transaction_id: transaction,
        watch_id: offer.watch_id,
        watch_nonce: offer.watch_nonce,
        agent_nonce,
        watch_ephemeral_public: offer.watch_ephemeral_public,
        watch_identity_public: offer.watch_identity_public,
        agent_ephemeral_public: ephemeral_public,
        agent_identity_public: identity.public_key,
        agent_name: display_name,
    })?;
    let shared =
        security::derive_shared_secret(&ephemeral_scalar[..], &offer.watch_ephemeral_public)?;
    let keys = security::derive_pairing_keys(&shared[..], &transcript)?;
    let code = security::verification_code(&keys.confirm_key, &transcript)?;
    desktop.pairing_code(&code);
    progress.set(ConnectionProgress::AWAITING_WATCH_APPROVAL);
    emit_connection_status(
        events,
        "pairing",
        "Waiting for approval on the watch",
        progress.pairing_state(),
        None,
    )
    .await;

    let confirmation = wire::encode_pairing_confirmation(transaction, true)?;
    let response = pair_exchange(
        peripheral,
        &characteristics.pairing_control,
        responses,
        &confirmation,
        &[PairMessageType::Credential, PairMessageType::Rejected],
        PAIRING_TIMEOUT,
    )
    .await?;
    if response.kind == PairMessageType::Rejected {
        ensure!(
            response.payload.is_empty(),
            "watch rejection response is malformed"
        );
        bail!("watch rejected pairing on device");
    }
    let envelope = wire::decode_credential_envelope(transaction, &response.payload)?;
    ensure!(
        envelope.purpose == wire::CredentialPurpose::Pairing
            && envelope.transaction_id == transaction,
        "pairing credential envelope is mismatched"
    );
    let aad = security::credential_envelope_aad(envelope.purpose as u8, transaction)?;
    let plaintext =
        security::unwrap_credential(&keys.wrap_key, &envelope.nonce, &envelope.ciphertext, &aad)?;
    let credential = credentials::decode_pairing_credential(&plaintext)?;
    ensure!(
        credential.watch_id == discovery.watch_id
            && credential.fingerprint == identity.fingerprint
            && credential.key_epoch == envelope.key_epoch
            && credential.watch_identity_public == discovery.watch_identity_public,
        "wrapped pairing credential is not bound to the watch identity"
    );
    let store = credentials.clone();
    let persisted = credential.clone();
    tokio::task::spawn_blocking(move || store.replace(&persisted))
        .await
        .context("watch credential worker failed")??;
    let proof = security::credential_ack_proof(&keys.ack_key, transaction, &envelope.ack_nonce)?;
    let acknowledgement = wire::encode_credential_ack(transaction, &proof)?;
    let acknowledged = pair_exchange(
        peripheral,
        &characteristics.pairing_control,
        responses,
        &acknowledgement,
        &[PairMessageType::CredentialAckOk],
        PAIRING_TIMEOUT,
    )
    .await?;
    ensure!(
        acknowledged.payload.is_empty(),
        "pairing acknowledgement response is malformed"
    );
    Ok(credential)
}

async fn authenticate_watch(
    peripheral: &Peripheral,
    characteristics: &Characteristics,
    responses: &mut mpsc::Receiver<ValueNotification>,
    identity: &AgentIdentity,
    credential: WatchCredential,
    discovery: &wire::DiscoveryResponse,
    configured_name: &str,
    store: &Arc<WatchCredentialStore>,
) -> Result<WatchCredential> {
    let display_name = credentials::normalize_agent_name(configured_name)?;
    let transaction = random_transaction();
    let request = wire::AuthenticationRequest {
        transaction_id: transaction,
        fingerprint: identity.fingerprint,
        agent_key_epoch: credential.key_epoch,
        display_name: display_name.clone(),
    };
    let frame = wire::encode_authentication_request(&request)?;
    let challenge_frame = pair_exchange(
        peripheral,
        &characteristics.pairing_control,
        responses,
        &frame,
        &[PairMessageType::AuthenticationChallenge],
        GATT_TIMEOUT,
    )
    .await?;
    let challenge = wire::decode_authentication_challenge(
        challenge_frame.transaction_id,
        &challenge_frame.payload,
    )?;
    ensure!(
        challenge.transaction_id == transaction
            && challenge.watch_id == credential.watch_id
            && challenge.display_name == display_name,
        "watch authentication challenge is mismatched"
    );
    let challenge_bytes = security::authentication_challenge_bytes(
        challenge.transaction_id,
        &challenge.watch_id,
        &challenge.connection_nonce,
        challenge.key_epoch,
        challenge.connection_role,
        &challenge.watch_ephemeral_public,
        &challenge.display_name,
    )?;
    ensure!(
        security::verify_challenge(
            &credential.watch_identity_public,
            &challenge_bytes,
            &challenge.watch_signature
        ),
        "watch identity signature was rejected"
    );
    let proof = security::sign_challenge(identity.private_scalar(), &challenge_bytes)?;
    let auth_frame = wire::encode_authentication_proof(transaction, &proof)?;
    let result = pair_exchange(
        peripheral,
        &characteristics.pairing_control,
        responses,
        &auth_frame,
        &[
            PairMessageType::AuthenticationOk,
            PairMessageType::Credential,
        ],
        GATT_TIMEOUT,
    )
    .await?;
    if result.kind == PairMessageType::AuthenticationOk {
        ensure!(
            result.payload.is_empty(),
            "authentication response is malformed"
        );
        ensure!(
            challenge.key_epoch == credential.key_epoch
                && discovery.key_epoch == credential.key_epoch,
            "watch key epoch changed without a rekey envelope"
        );
        return Ok(credential);
    }
    let envelope = wire::decode_credential_envelope(transaction, &result.payload)?;
    ensure!(
        envelope.purpose == wire::CredentialPurpose::Rekey
            && envelope.transaction_id == transaction
            && envelope.watch_ephemeral_public == challenge.watch_ephemeral_public
            && envelope.key_epoch == challenge.key_epoch
            && envelope.key_epoch > credential.key_epoch,
        "watch rekey envelope is mismatched"
    );
    let shared = security::derive_shared_secret(
        identity.private_scalar(),
        &envelope.watch_ephemeral_public,
    )?;
    let keys = security::derive_pairing_keys(&shared[..], &challenge_bytes)?;
    let aad = security::credential_envelope_aad(envelope.purpose as u8, transaction)?;
    let plaintext =
        security::unwrap_credential(&keys.wrap_key, &envelope.nonce, &envelope.ciphertext, &aad)?;
    let updated = credentials::decode_pairing_credential(&plaintext)?;
    ensure!(
        updated.watch_id == credential.watch_id
            && updated.fingerprint == credential.fingerprint
            && updated.watch_identity_public == credential.watch_identity_public
            && updated.key_epoch == envelope.key_epoch
            && updated.key_epoch == discovery.key_epoch,
        "rekeyed credential is not bound to the active watch"
    );
    let credential_store = store.clone();
    let saved = updated.clone();
    tokio::task::spawn_blocking(move || credential_store.replace(&saved))
        .await
        .context("watch credential worker failed")??;
    let ack = security::credential_ack_proof(&keys.ack_key, transaction, &envelope.ack_nonce)?;
    let frame = wire::encode_credential_ack(transaction, &ack)?;
    let acknowledged = pair_exchange(
        peripheral,
        &characteristics.pairing_control,
        responses,
        &frame,
        &[PairMessageType::CredentialAckOk],
        PAIRING_TIMEOUT,
    )
    .await?;
    ensure!(
        acknowledged.payload.is_empty(),
        "rekey acknowledgement response is malformed"
    );
    Ok(updated)
}

async fn write_gatt(
    peripheral: &Peripheral,
    characteristic: &btleplug::api::Characteristic,
    data: &[u8],
) -> Result<()> {
    timeout(
        GATT_TIMEOUT,
        peripheral.write(characteristic, data, WriteType::WithResponse),
    )
    .await
    .context("BLE write timed out")??;
    Ok(())
}

async fn retire_connected(mut session: ConnectedSession) {
    session.ingress.pump.abort();
    let _ = session.ingress.pump.await;
    let _ = timeout(Duration::from_secs(3), session.peripheral.disconnect()).await;
}

fn session_metadata(session: &ConnectedSession, state: &str) -> Value {
    let mut pet = session.pet_state.clone();
    if let Some(object) = pet.as_object_mut() {
        object.insert(
            "operation_active".into(),
            Value::Bool(session.pet_operation_active),
        );
    }
    json!({
        "watch_id": hex::encode(session.credential.watch_id),
        "name": session.advertisement_name,
        "agent_name": session.agent_name,
        "capability": "ready",
        "generation": session.generation,
        "state": state,
        "pairing_state": "complete",
        "pet": pet,
    })
}

async fn run_session(
    mut session: ConnectedSession,
    mut snapshots: watch::Receiver<StatusSnapshot>,
    desktop: Arc<dyn Desktop>,
    events: mpsc::Sender<TransportEvent>,
    mut commands: mpsc::Receiver<DeviceCommand>,
    mut shutdown: watch::Receiver<bool>,
    identity_store: Arc<AgentIdentityStore>,
    credential_store: Arc<WatchCredentialStore>,
) -> Result<()> {
    let watch_id = hex::encode(session.credential.watch_id);
    let session_generation = session.generation;
    let live = Arc::new(AtomicBool::new(true));
    if session.characteristics.pet_control.is_some() && session.characteristics.pet_status.is_some()
    {
        match query_pet_state(&session).await {
            Ok(state) => session.pet_state = state,
            Err(_) => {
                session.pet_state["capability"] = Value::String("offline".into());
                session.pet_state["usable"] = Value::Bool(false);
                emit_error(&events, "Watch pet state is unavailable").await;
            }
        }
        emit_connection(
            &events,
            "connected",
            session_metadata(&session, "connected"),
        )
        .await;
    }

    let readiness = desktop.readiness();
    let input_available = readiness.get("input_action").and_then(Value::as_str) == Some("ready");
    let (voice_capabilities, unavailable_reason) = voice_capabilities(&readiness);
    session.voice_capabilities = voice_capabilities;
    let voice_control = if voice_capabilities == 0 {
        wire::encode_voice_unavailable(
            &session.credential.master_key.0,
            &session.voice_nonce,
            session.credential.key_epoch,
            unavailable_reason,
        )
    } else {
        wire::encode_voice_ready(
            &session.credential.master_key.0,
            &session.voice_nonce,
            session.credential.key_epoch,
            voice_capabilities,
        )
    };
    match voice_control {
        Ok(payload) => {
            if write_gatt(
                &session.peripheral,
                &session.characteristics.voice_control,
                &payload,
            )
            .await
            .is_err()
            {
                session.voice_failed = true;
            }
        }
        Err(_) => session.voice_failed = true,
    }

    let mut pet_task: Option<JoinHandle<Result<Value>>> = None;
    let mut pet_cancel: Option<watch::Sender<bool>> = None;
    let mut snapshot = snapshots.borrow().clone();
    send_time_sync(&mut session).await?;
    send_status_tick(&mut session, &snapshot, &events).await?;
    let mut status_timer = interval(SESSION_STATUS_PERIOD);
    status_timer.tick().await;
    let mut epoch_timer = interval(EPOCH_CHECK_PERIOD);
    epoch_timer.tick().await;

    let result = loop {
        if *shutdown.borrow() {
            break Ok(());
        }
        let pet_task_active = pet_task.is_some();
        let selected = tokio::select! {
            changed = shutdown.changed() => SessionLoopEvent::Shutdown(changed.is_err() || *shutdown.borrow()),
            command = commands.recv() => SessionLoopEvent::Command(command),
            joined = async {
                match pet_task.as_mut() {
                    Some(task) => Some(task.await),
                    None => std::future::pending::<Option<std::result::Result<Result<Value>, tokio::task::JoinError>>>().await,
                }
            }, if pet_task_active => SessionLoopEvent::PetTask(joined),
            changed = snapshots.changed() => SessionLoopEvent::Snapshot(changed.is_err()),
            _ = status_timer.tick() => SessionLoopEvent::StatusTick,
            _ = epoch_timer.tick() => SessionLoopEvent::EpochTick,
            Some(notification) = session.ingress.input.recv() => SessionLoopEvent::Input(notification),
            Some(notification) = session.ingress.voice.recv() => SessionLoopEvent::Voice(notification),
            Some(_) = session.ingress.pairing.recv() => SessionLoopEvent::Pairing,
            Some(_) = async {
                if !pet_task_active { session.ingress.pet.lock().await.recv().await }
                else { std::future::pending::<Option<ValueNotification>>().await }
            } => SessionLoopEvent::IdlePetStatus,
            _ = &mut session.ingress.pump => SessionLoopEvent::PumpEnded,
        };
        match selected {
            SessionLoopEvent::Shutdown(done) => {
                if done {
                    break Ok(());
                }
            }
            SessionLoopEvent::Command(None) => break Ok(()),
            SessionLoopEvent::Command(Some(DeviceCommand::Quit | DeviceCommand::Reconnect)) => {
                break Ok(());
            }
            SessionLoopEvent::Command(Some(DeviceCommand::CancelPet)) => {
                if let Some(cancel) = &pet_cancel {
                    let _ = cancel.send(true);
                }
            }
            SessionLoopEvent::Command(Some(DeviceCommand::UploadPet(_))) => {
                emit_error(&events, "Pet upload request identifier is missing").await;
            }
            SessionLoopEvent::Command(Some(DeviceCommand::UploadPetRequest {
                package,
                request_id,
            })) => {
                if !is_uuid_v4(&request_id) {
                    emit_error(&events, "Pet upload request identifier is invalid").await;
                    continue;
                }
                if pet_task.is_some() {
                    emit_error(&events, "Pet channel is busy").await;
                    continue;
                }
                let Some(control) = session.characteristics.pet_control.clone() else {
                    emit_error(&events, "Watch does not support pet uploads").await;
                    continue;
                };
                let Some(status) = session.characteristics.pet_status.clone() else {
                    emit_error(&events, "Watch does not support pet uploads").await;
                    continue;
                };
                let package = match canonical_pet_package(package) {
                    Ok(value) => value,
                    Err(_) => {
                        emit_error(&events, "Pet package is invalid").await;
                        continue;
                    }
                };
                if session.pet_state.get("capability").and_then(Value::as_str) != Some("ready") {
                    emit_error(&events, "Watch pet storage is unavailable").await;
                    continue;
                }
                let schema_min = session
                    .pet_state
                    .get("schema_min")
                    .and_then(Value::as_u64)
                    .unwrap_or(0) as u16;
                let schema_max = session
                    .pet_state
                    .get("schema_max")
                    .and_then(Value::as_u64)
                    .unwrap_or(0) as u16;
                if !(schema_min..=schema_max).contains(&PET_SCHEMA_VERSION) {
                    emit_error(&events, "Watch firmware does not support this pet package").await;
                    continue;
                }
                let session_generation = session.generation;
                let initial = pet_progress(
                    &request_id,
                    "sending",
                    0,
                    package.encoded.len(),
                    &watch_id,
                    session_generation,
                    Some(&package),
                    &session.pet_state,
                    None,
                );
                if !emit_pet_progress(&events, &live, initial).await {
                    continue;
                }
                let (cancel_tx, cancel_rx) = watch::channel(false);
                pet_cancel = Some(cancel_tx);
                session.pet_operation_active = true;
                emit_connection(
                    &events,
                    "connected",
                    session_metadata(&session, "connected"),
                )
                .await;
                let peripheral = session.peripheral.clone();
                let statuses = session.ingress.pet.clone();
                let key = session.credential.clone();
                let state = session.pet_state.clone();
                let sink = events.clone();
                let alive = live.clone();
                let request_watch_id = watch_id.clone();
                let kind = PetWork::Upload(package);
                pet_task = Some(tokio::spawn(async move {
                    run_pet_operation(
                        peripheral,
                        control,
                        status,
                        statuses,
                        key,
                        state,
                        kind,
                        request_id,
                        request_watch_id,
                        session_generation,
                        cancel_rx,
                        sink,
                        alive,
                    )
                    .await
                }));
            }
            SessionLoopEvent::Command(Some(DeviceCommand::RestorePet)) => {
                if pet_task.is_some() {
                    emit_error(&events, "Pet channel is busy").await;
                    continue;
                }
                let Some(control) = session.characteristics.pet_control.clone() else {
                    emit_error(&events, "Watch does not support pet restore").await;
                    continue;
                };
                let Some(status) = session.characteristics.pet_status.clone() else {
                    emit_error(&events, "Watch does not support pet restore").await;
                    continue;
                };
                if session.pet_state.get("capability").and_then(Value::as_str) != Some("ready") {
                    emit_error(&events, "Watch pet storage is unavailable").await;
                    continue;
                }
                let request_id = Uuid::new_v4().to_string();
                let session_generation = session.generation;
                let initial = pet_progress(
                    &request_id,
                    "applying",
                    0,
                    0,
                    &watch_id,
                    session_generation,
                    None,
                    &session.pet_state,
                    None,
                );
                if !emit_pet_progress(&events, &live, initial).await {
                    continue;
                }
                let (cancel_tx, cancel_rx) = watch::channel(false);
                pet_cancel = Some(cancel_tx);
                session.pet_operation_active = true;
                emit_connection(
                    &events,
                    "connected",
                    session_metadata(&session, "connected"),
                )
                .await;
                let peripheral = session.peripheral.clone();
                let statuses = session.ingress.pet.clone();
                let key = session.credential.clone();
                let state = session.pet_state.clone();
                let sink = events.clone();
                let alive = live.clone();
                let request_watch_id = watch_id.clone();
                pet_task = Some(tokio::spawn(async move {
                    run_pet_operation(
                        peripheral,
                        control,
                        status,
                        statuses,
                        key,
                        state,
                        PetWork::Restore,
                        request_id,
                        request_watch_id,
                        session_generation,
                        cancel_rx,
                        sink,
                        alive,
                    )
                    .await
                }));
            }
            SessionLoopEvent::Command(Some(
                DeviceCommand::SelectWatch(_)
                | DeviceCommand::RenameAgent(_)
                | DeviceCommand::ForgetWatch(_)
                | DeviceCommand::SetPreferences(_),
            )) => {}
            SessionLoopEvent::PetTask(joined) => {
                pet_task = None;
                pet_cancel = None;
                session.pet_operation_active = false;
                match joined {
                    Some(Ok(Ok(state))) => session.pet_state = state,
                    Some(Ok(Err(_))) | Some(Err(_)) => {
                        emit_error(&events, "Watch pet operation failed").await
                    }
                    None => {}
                }
                emit_connection(
                    &events,
                    "connected",
                    session_metadata(&session, "connected"),
                )
                .await;
            }
            SessionLoopEvent::Snapshot(disconnected) => {
                if disconnected {
                    break Ok(());
                }
                snapshot = snapshots.borrow().clone();
            }
            SessionLoopEvent::StatusTick => {
                if session.ingress.input_overflow.swap(false, Ordering::AcqRel) {
                    emit_error(&events, "Watch input event queue overflowed").await;
                }
                if session.ingress.voice_overflow.swap(false, Ordering::AcqRel) {
                    isolate_voice(&mut session, &desktop).await;
                }
                if session.ingress.pet_overflow.swap(false, Ordering::AcqRel) {
                    emit_error(&events, "Watch pet notification queue overflowed").await;
                }
                send_status_tick(&mut session, &snapshot, &events).await?;
            }
            SessionLoopEvent::EpochTick => {
                if refresh_epoch(&mut session, &identity_store, &credential_store, &events).await? {
                    break Ok(());
                }
            }
            SessionLoopEvent::Input(notification) => {
                handle_input_notification(
                    &mut session,
                    &notification.value,
                    input_available,
                    &desktop,
                    &events,
                )
                .await;
            }
            SessionLoopEvent::Voice(notification) => {
                snapshot = snapshots.borrow().clone();
                if !session.voice_failed
                    && handle_voice_notification(
                        &mut session,
                        &notification.value,
                        &snapshot,
                        &desktop,
                    )
                    .is_err()
                {
                    isolate_voice(&mut session, &desktop).await;
                }
            }
            SessionLoopEvent::Pairing | SessionLoopEvent::IdlePetStatus => {}
            SessionLoopEvent::PumpEnded => break Err(anyhow!("watch notification stream closed")),
        }
    };

    live.store(false, Ordering::Release);
    if let Some(cancel) = pet_cancel {
        let _ = cancel.send(true);
    }
    if let Some(mut task) = pet_task {
        if timeout(Duration::from_secs(3), &mut task).await.is_err() {
            task.abort();
            let _ = task.await;
        }
    }
    if session.voice_active {
        let _ = desktop.voice_stop(true);
        session.voice_active = false;
    }
    session.ingress.pump.abort();
    let _ = session.ingress.pump.await;
    for characteristic in [
        Some(&session.characteristics.pairing_tx),
        Some(&session.characteristics.input_tx),
        Some(&session.characteristics.voice_tx),
        session.characteristics.pet_status.as_ref(),
    ]
    .into_iter()
    .flatten()
    {
        let _ = timeout(GATT_TIMEOUT, session.peripheral.unsubscribe(characteristic)).await;
    }
    let _ = timeout(Duration::from_secs(3), session.peripheral.disconnect()).await;
    result
}

fn voice_capabilities(readiness: &Value) -> (u8, u8) {
    let state = readiness
        .get("voice")
        .and_then(|value| value.get("state"))
        .and_then(Value::as_str)
        .unwrap_or("unavailable");
    match state {
        "ready" => (wire::ALL_VOICE_CAPABILITIES, 0),
        "accessibility_required" => (
            wire::EXTERNAL_MICROPHONE_CAPABILITY,
            wire::VOICE_ACCESSIBILITY_REQUIRED,
        ),
        "typeless_not_running" => (
            wire::EXTERNAL_MICROPHONE_CAPABILITY,
            wire::VOICE_TYPELESS_NOT_RUNNING,
        ),
        "driver_required" => (0, wire::VOICE_DRIVER_REQUIRED),
        "restart_required" => (0, wire::VOICE_RESTART_REQUIRED),
        _ => (0, wire::VOICE_INITIALIZATION_FAILED),
    }
}

async fn send_time_sync(session: &mut ConnectedSession) -> Result<()> {
    let unix_ms = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_millis()
        .min(u64::MAX as u128) as u64;
    let offset = chrono::Local::now().offset().local_minus_utc() / 60;
    let payload = wire::encode_status_value(
        &json!({
            "type": "time_sync", "unix_ms": unix_ms,
            "utc_offset_minutes": offset,
        }),
        session.status_codec,
    )?;
    send_status_payload(session, wire::EVENT_MESSAGE, &payload).await
}

async fn send_status_tick(
    session: &mut ConnectedSession,
    snapshot: &StatusSnapshot,
    events: &mpsc::Sender<TransportEvent>,
) -> Result<()> {
    let started = Instant::now();
    let messages = wire::status_messages_with_codec(snapshot, session.status_codec)?;
    for (message_type, payload) in messages {
        if wire::is_trae_status_payload_with_codec(&payload, session.status_codec) {
            let due = session
                .last_trae_sent
                .is_none_or(|last| last.elapsed() >= Duration::from_secs(6));
            if session.last_trae_payload.as_deref() == Some(payload.as_slice()) && !due {
                continue;
            }
            session.last_trae_payload = Some(payload.clone());
            session.last_trae_sent = Some(Instant::now());
        }
        send_status_payload(session, message_type, &payload).await?;
    }
    session.status_sync_log.record("sent", snapshot, started.elapsed());
    send_task_details(session, snapshot).await?;
    send_notification_chime(session, snapshot, events).await?;
    Ok(())
}

async fn send_status_payload(
    session: &mut ConnectedSession,
    message_type: u8,
    payload: &[u8],
) -> Result<()> {
    let sequence = session.status_sequence;
    session.status_sequence = session.status_sequence.wrapping_add(1);
    let frames = wire::encode_status_message_with_codec(
        payload,
        message_type,
        sequence,
        session.status_capacity,
        session.status_codec,
    )?;
    for (index, frame) in frames.iter().enumerate() {
        write_gatt(
            &session.peripheral,
            &session.characteristics.status_rx,
            frame,
        )
        .await?;
        if (index + 1) % wire::STATUS_FRAME_BURST == 0 && index + 1 < frames.len() {
            tokio::time::sleep(Duration::from_millis(wire::STATUS_BURST_PAUSE_MS)).await;
        }
    }
    Ok(())
}

async fn send_task_details(
    session: &mut ConnectedSession,
    snapshot: &StatusSnapshot,
) -> Result<()> {
    if !agent_inventory::allows(&snapshot.telemetry, AgentId::Codex)? {
        session.detail_cache.clear();
        return Ok(());
    }
    let allowed: HashSet<&str> = snapshot
        .details
        .keys()
        .filter_map(|route| snapshot_task_detail(snapshot, route, false).map(|_| route.as_str()))
        .collect();
    session
        .detail_cache
        .retain(|route, _| allowed.contains(route.as_str()));
    for route in snapshot.details.keys() {
        let Some(detail) = snapshot_task_detail(snapshot, route, false) else { continue; };
        let Some(text) = bounded_task_reply(&detail.reply) else {
            continue;
        };
        let revision = detail.revision;
        let fingerprint: [u8; 32] = Sha256::digest(text.as_bytes()).into();
        if session.detail_cache.get(&detail.route) == Some(&(revision, fingerprint)) {
            continue;
        }
        let chunks = task_detail_chunks(&text, wire::MAX_TASK_DETAIL_CHUNK_BYTES);
        if chunks.is_empty() || chunks.len() > wire::MAX_TASK_DETAIL_CHUNKS as usize {
            continue;
        }
        for (index, chunk) in chunks.iter().enumerate() {
            session.detail_sequence = session
                .detail_sequence
                .checked_add(1)
                .ok_or_else(|| anyhow!("task detail sequence exhausted"))?;
            let envelope = wire::encrypt_task_detail(
                &session.credential.master_key.0,
                session.credential.key_epoch,
                session.detail_nonce,
                session.detail_sequence,
                &detail.route,
                revision,
                index as u64,
                chunks.len() as u64,
                chunk,
            )?;
            let payload = wire::encode_task_detail(&envelope, session.status_codec)?;
            send_status_payload(session, wire::STATUS_MESSAGE, &payload).await?;
        }
        session
            .detail_cache
            .insert(detail.route.clone(), (revision, fingerprint));
    }
    Ok(())
}

fn snapshot_task_detail<'a>(
    snapshot: &'a StatusSnapshot,
    route: &str,
    allow_completed: bool,
) -> Option<&'a crate::contracts::TaskDetail> {
    if route.len() != 16
        || route == "0000000000000000"
        || !route
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
    {
        return None;
    }
    let revision = snapshot
        .codex
        .get("codex_alert_revision")
        .and_then(Value::as_u64)
        .filter(|value| *value <= wire::MAX_TASK_DETAIL_REVISION)?;
    let detail = snapshot.details.get(route)?;
    if detail.route != route || detail.source != "codex" || detail.revision != revision {
        return None;
    }
    let alerts = snapshot.codex.get("codex_alerts")?.as_array()?;
    let mut matching = alerts
        .iter()
        .filter(|alert| alert.get("route").and_then(Value::as_str) == Some(route));
    let alert = matching.next()?;
    if matching.next().is_some() {
        return None;
    }
    let state = alert.get("state").and_then(Value::as_str)?;
    if !matches!(state, "pending" | "running")
        && !(allow_completed && state == "completed")
    {
        return None;
    }
    if alert.get("thread_id").and_then(Value::as_str) != Some(detail.task_id.as_str())
        || detail.task_id.is_empty()
    {
        return None;
    }
    if state == "running"
        && alert
            .get("turn_id")
            .and_then(Value::as_str)
            .filter(|turn_id| !turn_id.is_empty())
            .is_none()
    {
        return None;
    }
    Some(detail)
}

fn bounded_task_reply(value: &str) -> Option<String> {
    const MARKER: &str = "\n\n[内容过长，手表仅显示前 12 KiB]";
    if value.len() <= MAX_TASK_DETAIL_BYTES {
        return Some(value.to_owned());
    }
    let mut prefix = String::new();
    for character in value.chars() {
        if prefix.len() + character.len_utf8() > MAX_TASK_DETAIL_BYTES {
            break;
        }
        prefix.push(character);
    }
    prefix.push_str(MARKER);
    Some(prefix)
}

fn task_detail_chunks(text: &str, byte_limit: usize) -> Vec<String> {
    if byte_limit == 0 {
        return Vec::new();
    }
    let mut chunks = Vec::new();
    let mut current = String::new();
    for character in text.chars() {
        if current.len() + character.len_utf8() > byte_limit && !current.is_empty() {
            chunks.push(std::mem::take(&mut current));
        }
        if character.len_utf8() > byte_limit {
            return Vec::new();
        }
        current.push(character);
    }
    if !current.is_empty() || text.is_empty() {
        chunks.push(current);
    }
    chunks
}

async fn send_notification_chime(
    session: &mut ConnectedSession,
    snapshot: &StatusSnapshot,
    events: &mpsc::Sender<TransportEvent>,
) -> Result<()> {
    let pending: Vec<String> = if agent_inventory::allows(&snapshot.telemetry, AgentId::Codex)? {
        snapshot
            .notifications
            .iter()
            .filter(|id| !session.delivered_notifications.contains(*id))
            .cloned()
            .collect()
    } else {
        Vec::new()
    };
    if !pending.is_empty() {
        let payload = wire::encode_status_value(
            &json!({"type": "chime", "agent": "codex"}),
            session.status_codec,
        )?;
        if send_status_payload(session, wire::EVENT_MESSAGE, &payload)
            .await
            .is_ok()
        {
            for id in pending {
                session.delivered_notifications.insert(id.clone());
                session.notification_ack_pending.insert(id);
            }
            if session.delivered_notifications.len() > 4096 {
                session.delivered_notifications.clear();
            }
        }
    }
    if !session.notification_ack_pending.is_empty() {
        let ids = session
            .notification_ack_pending
            .iter()
            .cloned()
            .collect::<Vec<_>>();
        let sent = timeout(
            Duration::from_secs(2),
            events.send(TransportEvent::NotificationDelivered(ids.clone())),
        )
        .await
        .is_ok_and(|result| result.is_ok());
        if sent {
            for id in ids {
                session.notification_ack_pending.remove(&id);
            }
        }
    }
    Ok(())
}

async fn handle_input_notification(
    session: &mut ConnectedSession,
    raw: &[u8],
    available: bool,
    desktop: &Arc<dyn Desktop>,
    events: &mpsc::Sender<TransportEvent>,
) {
    let key = &session.credential.master_key.0;
    if let Ok(nonce) = wire::decode_input_ready(key, raw) {
        if session.used_input_nonces.contains(&nonce) {
            return;
        }
        session.used_input_nonces.push_back(nonce);
        if session.used_input_nonces.len() > 16 {
            session.used_input_nonces.pop_front();
        }
        if session.input_replay.set_active_nonce(nonce).is_err() {
            return;
        }
        session.input_nonce = nonce;
        if available {
            if let Ok(ready) = wire::encode_input_ready(key, &nonce) {
                if write_gatt(
                    &session.peripheral,
                    &session.characteristics.input_control,
                    &ready,
                )
                .await
                .is_err()
                {
                    emit_error(events, "Watch input channel could not be enabled").await;
                }
            }
        }
        return;
    }
    if !available || session.input_nonce == [0; 8] {
        return;
    }
    let Ok(action) = wire::decode_input_action(key, raw, &session.input_nonce) else {
        return;
    };
    if !session
        .input_replay
        .accept(&session.input_nonce, action.sequence)
    {
        return;
    }
    let action = match action.action {
        wire::INPUT_ACTION_SUBMIT => InputAction::Submit,
        wire::INPUT_ACTION_FORCE_STEER => InputAction::ForceSteer,
        _ => return,
    };
    if !matches!(desktop.submit(action), Ok(true)) {
        emit_error(events, "Desktop input action failed").await;
    }
}

fn handle_voice_notification(
    session: &mut ConnectedSession,
    raw: &[u8],
    snapshot: &StatusSnapshot,
    desktop: &Arc<dyn Desktop>,
) -> Result<()> {
    let key = &session.credential.master_key.0;
    let epoch = session.credential.key_epoch;
    if raw.len() < 2 {
        bail!("voice notification is truncated");
    }
    if raw[1] == wire::PTT_BEGIN {
        let packet = wire::decode_voice_notification(key, raw, None, epoch)?;
        let begin = packet
            .begin
            .ok_or_else(|| anyhow!("voice begin is missing"))?;
        let nonce = packet
            .session_nonce
            .ok_or_else(|| anyhow!("voice session nonce is missing"))?;
        if packet.session <= session.highest_voice_session
            || session.voice_used_nonces.contains(&nonce)
        {
            bail!("voice session is stale");
        }
        let required = if begin.mode == wire::PUSH_TO_TALK {
            wire::PUSH_TO_TALK_CAPABILITY
        } else {
            wire::EXTERNAL_MICROPHONE_CAPABILITY
        };
        if session.voice_capabilities & required == 0 {
            bail!("watch selected an unavailable voice mode");
        }
        let (mode, task) = if begin.mode == wire::PUSH_TO_TALK && begin.route != 0 {
            let route = format!("{:016x}", begin.route);
            let detail = snapshot_task_detail(snapshot, &route, true)
                .cloned()
                .ok_or_else(|| anyhow!("voice task route is no longer available"))?;
            (VoiceMode::TaskConversation, Some(detail))
        } else if begin.mode == wire::PUSH_TO_TALK {
            (VoiceMode::PushToTalk, None)
        } else {
            (VoiceMode::PinnedMicrophone, None)
        };
        session.highest_voice_session = packet.session;
        session.voice_used_nonces.push_back(nonce);
        if session.voice_used_nonces.len() > 16 {
            session.voice_used_nonces.pop_front();
        }
        desktop.voice_start(mode, task.clone())?;
        session.voice_active = true;
        session.active_voice = Some(ActiveVoiceSession {
            session: packet.session,
            nonce,
            last_sequence: 0,
        });
        return Ok(());
    }
    let active = session
        .active_voice
        .as_mut()
        .ok_or_else(|| anyhow!("voice notification has no active session"))?;
    let packet = wire::decode_voice_notification(key, raw, Some(&active.nonce), epoch)?;
    if packet.session != active.session {
        bail!("voice notification belongs to another session");
    }
    match packet.kind {
        wire::AUDIO => {
            if packet.sequence <= active.last_sequence || packet.sequence == u32::MAX {
                bail!("voice audio sequence is stale");
            }
            let missing = packet.sequence - active.last_sequence - 1;
            if missing > 5 {
                bail!("voice audio sequence gap exceeds limit");
            }
            for _ in 0..missing {
                desktop.voice_pcm(&[0; wire::VOICE_BLOCK_SAMPLES as usize])?;
            }
            let samples =
                wire::decode_ima_adpcm_block(&packet.payload, wire::VOICE_BLOCK_SAMPLES as usize)?;
            desktop.voice_pcm(&samples)?;
            active.last_sequence = packet.sequence;
        }
        wire::PTT_END => {
            if packet.sequence != active.last_sequence.saturating_add(1) {
                bail!("voice end sequence is invalid");
            }
            let summary = wire::decode_voice_end(&packet.payload)?;
            let packets = packet.sequence.saturating_sub(1);
            let sample_count = packets.saturating_mul(wire::VOICE_BLOCK_SAMPLES as u32);
            let expected_samples = summary.elapsed_ms.saturating_mul(16);
            let tolerance = expected_samples / 100 + wire::VOICE_BLOCK_SAMPLES as u32 + 160 * 16;
            let delta = summary.captured_samples.abs_diff(expected_samples);
            if summary.generated_packets != packets
                || summary.accepted_packets != packets
                || summary.queue_overflows != 0
                || summary.encode_failures != 0
                || summary.captured_samples != sample_count
                || summary.elapsed_ms == 0
                || delta > tolerance
                || !(23..=515).contains(&summary.negotiated_mtu)
            {
                bail!("voice end summary does not match the session");
            }
            session.active_voice = None;
            session.voice_active = false;
            desktop.voice_stop(false)?;
        }
        wire::VOICE_ERROR => {
            if packet.sequence != active.last_sequence.saturating_add(1) {
                bail!("voice error sequence is invalid");
            }
            bail!("watch reported a voice error");
        }
        _ => bail!("voice notification kind is unsupported"),
    }
    Ok(())
}

async fn isolate_voice(session: &mut ConnectedSession, desktop: &Arc<dyn Desktop>) {
    if session.voice_failed {
        return;
    }
    session.voice_failed = true;
    if session.voice_active {
        let _ = desktop.voice_stop(true);
        session.voice_active = false;
        session.active_voice = None;
    }
    if let Ok(payload) = wire::encode_voice_unavailable(
        &session.credential.master_key.0,
        &session.voice_nonce,
        session.credential.key_epoch,
        wire::VOICE_INITIALIZATION_FAILED,
    ) {
        let _ = write_gatt(
            &session.peripheral,
            &session.characteristics.voice_control,
            &payload,
        )
        .await;
    }
}

async fn refresh_epoch(
    session: &mut ConnectedSession,
    identity_store: &Arc<AgentIdentityStore>,
    credential_store: &Arc<WatchCredentialStore>,
    events: &mpsc::Sender<TransportEvent>,
) -> Result<bool> {
    let discovery = discover_watch(
        &session.peripheral,
        &session.characteristics,
        &mut session.ingress.pairing,
    )
    .await?;
    if discovery.watch_id != session.credential.watch_id
        || discovery.watch_identity_public != session.credential.watch_identity_public
    {
        bail!("active watch identity changed");
    }
    if discovery.key_epoch == session.credential.key_epoch {
        return Ok(false);
    }
    if discovery.key_epoch < session.credential.key_epoch {
        bail!("watch key epoch rolled back");
    }
    let identity_store = identity_store.clone();
    let identity = tokio::task::spawn_blocking(move || identity_store.load_or_create())
        .await
        .context("Agent identity worker failed")??;
    let store = credential_store.clone();
    session.credential = authenticate_watch(
        &session.peripheral,
        &session.characteristics,
        &mut session.ingress.pairing,
        &identity,
        session.credential.clone(),
        &discovery,
        &session.agent_name,
        &store,
    )
    .await?;
    emit_error(events, "Watch key epoch changed; reconnecting securely").await;
    Ok(true)
}

async fn query_pet_state(session: &ConnectedSession) -> Result<Value> {
    let control = session
        .characteristics
        .pet_control
        .as_ref()
        .ok_or_else(|| anyhow!("pet service is unavailable"))?;
    let _status = session
        .characteristics
        .pet_status
        .as_ref()
        .ok_or_else(|| anyhow!("pet service is unavailable"))?;
    let transaction = random_transaction();
    let nonce = random_nonzero::<8>();
    let mut replay = wire::PetStatusReplayGuard::default();
    replay.set_active_nonce(nonce)?;
    let frame = wire::encode_pet_control(
        &session.credential.master_key.0,
        &nonce,
        1,
        PetMessageKind::Query,
        PetControlPayload::Query {
            transaction_id: transaction,
        },
    )?;
    write_gatt(&session.peripheral, control, &frame).await?;
    let response = wait_pet_status(
        &session.peripheral,
        &session.ingress.pet,
        &session.credential.master_key,
        transaction,
        nonce,
        &mut replay,
        GATT_TIMEOUT,
    )
    .await?;
    match response.payload {
        PetStatusPayload::State {
            transaction_id,
            source,
            usable,
            schema_min,
            schema_max,
            generation,
            active_sha256,
        } => {
            ensure!(
                transaction_id == transaction,
                "pet State transaction does not match"
            );
            Ok(pet_state_value(
                source,
                usable,
                schema_min,
                schema_max,
                generation,
                active_sha256,
                session.generation,
                false,
            ))
        }
        _ => bail!("watch returned conflicting pet state"),
    }
}

fn pet_state_value(
    source: PetSource,
    usable: bool,
    schema_min: u16,
    schema_max: u16,
    generation: u32,
    active_sha256: [u8; 32],
    session_generation: u64,
    operation_active: bool,
) -> Value {
    let schema_supported = schema_min <= PET_SCHEMA_VERSION && PET_SCHEMA_VERSION <= schema_max;
    json!({
        "capability": if schema_supported { "ready" } else { "unsupported" },
        "source": if source == PetSource::Custom { "custom" } else { "builtin" },
        "schema_min": schema_min, "schema_max": schema_max,
        "generation": generation, "session_generation": session_generation,
        "active_hash": hex::encode(active_sha256), "usable": usable,
        "operation_active": operation_active,
    })
}

async fn wait_pet_status(
    peripheral: &Peripheral,
    receiver: &Arc<tokio::sync::Mutex<mpsc::Receiver<ValueNotification>>>,
    key: &security::Secret16,
    transaction: u32,
    nonce: [u8; 8],
    replay: &mut wire::PetStatusReplayGuard,
    wait: Duration,
) -> Result<wire::PetStatus> {
    let deadline = tokio::time::Instant::now() + wait;
    loop {
        let remaining = deadline.saturating_duration_since(tokio::time::Instant::now());
        if remaining.is_zero() {
            return Err(anyhow::Error::new(PetTimeout));
        }
        let value = {
            let mut queue = receiver.lock().await;
            timeout(remaining, queue.recv())
                .await
                .map_err(|_| anyhow::Error::new(PetTimeout))?
                .ok_or_else(|| anyhow!("pet notification channel closed"))?
        };
        let Ok(status) = wire::decode_pet_status(&key.0, &value.value) else {
            continue;
        };
        if status.exchange_nonce != nonce || pet_status_transaction(&status.payload) != transaction
        {
            continue;
        }
        if !replay.accept(&status)? {
            continue;
        }
        if let PetStatusPayload::Error {
            reason,
            required_bytes,
            available_bytes,
            ..
        } = &status.payload
        {
            return Err(anyhow::Error::new(PetProtocolFailure {
                code: pet_error_code(*reason),
                required_bytes: *required_bytes,
                available_bytes: *available_bytes,
            }));
        }
        let connected = peripheral.is_connected().await.unwrap_or(false);
        if !connected {
            bail!("watch disconnected during pet operation");
        }
        return Ok(status);
    }
}

fn pet_status_transaction(payload: &PetStatusPayload) -> u32 {
    match payload {
        PetStatusPayload::State { transaction_id, .. }
        | PetStatusPayload::Progress { transaction_id, .. }
        | PetStatusPayload::Committed { transaction_id, .. }
        | PetStatusPayload::Cancelled { transaction_id }
        | PetStatusPayload::Error { transaction_id, .. } => *transaction_id,
    }
}

fn pet_error_code(reason: wire::PetErrorReason) -> &'static str {
    match reason {
        wire::PetErrorReason::InvalidMessage => "invalid_message",
        wire::PetErrorReason::Unauthorized => "unauthorized",
        wire::PetErrorReason::Busy => "busy",
        wire::PetErrorReason::UnsupportedSchema => "unsupported_schema",
        wire::PetErrorReason::InvalidMetadata => "invalid_metadata",
        wire::PetErrorReason::InvalidOffset => "invalid_offset",
        wire::PetErrorReason::Replay => "replay",
        wire::PetErrorReason::InsufficientStorage => "insufficient_storage",
        wire::PetErrorReason::HashMismatch => "hash_mismatch",
        wire::PetErrorReason::IoFailure => "io_failure",
        wire::PetErrorReason::Timeout => "timeout",
        wire::PetErrorReason::UnusablePackage => "unusable_package",
        wire::PetErrorReason::TooLarge => "too_large",
        wire::PetErrorReason::Internal => "internal",
    }
}

#[derive(Debug)]
struct PetTimeout;
impl std::fmt::Display for PetTimeout {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str("pet acknowledgement timed out")
    }
}
impl std::error::Error for PetTimeout {}

#[derive(Debug)]
struct PetProtocolFailure {
    code: &'static str,
    required_bytes: u32,
    available_bytes: u32,
}
impl std::fmt::Display for PetProtocolFailure {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "watch rejected pet operation ({})", self.code)
    }
}
impl std::error::Error for PetProtocolFailure {}

enum PetWork {
    Upload(crate::pet::PetPackage),
    Restore,
}

fn canonical_pet_package(package: crate::pet::PetPackage) -> Result<crate::pet::PetPackage> {
    ensure!(
        package.encoded.len() <= wire::PET_MAX_PACKAGE_BYTES,
        "pet package exceeds protocol capacity"
    );
    let decoded = crate::pet::parse_package(&package.encoded)?;
    ensure!(
        decoded.digest == package.digest
            && decoded.width == package.width
            && decoded.height == package.height
            && decoded.frames == package.frames
            && decoded.palette == package.palette
            && decoded.transparent_index == package.transparent_index,
        "pet package is not canonical"
    );
    Ok(decoded)
}

fn pet_progress(
    request_id: &str,
    phase: &str,
    acknowledged_bytes: usize,
    total_bytes: usize,
    watch_id: &str,
    session_generation: u64,
    package: Option<&crate::pet::PetPackage>,
    current_state: &Value,
    error: Option<(&str, u32, u32)>,
) -> Value {
    let mut result = json!({
        "request_id": request_id, "phase": phase,
        "acknowledged_bytes": acknowledged_bytes, "total_bytes": total_bytes,
        "error_code": error.map(|item| item.0),
        "required_bytes": error.map(|item| item.1).unwrap_or(0),
        "available_bytes": error.map(|item| item.2).unwrap_or(0),
        "watch_id": watch_id, "session_generation": session_generation,
        "generation": session_generation,
        "active_hash": current_state.get("active_hash").and_then(Value::as_str).unwrap_or(""),
        "source": current_state.get("source").and_then(Value::as_str).unwrap_or("builtin"),
    });
    if let Some(package) = package {
        result["package_hash"] = Value::String(hex::encode(Sha256::digest(&package.encoded)));
        result["digest"] = Value::String(hex::encode(package.digest));
    }
    result
}

async fn emit_pet_progress(
    events: &mpsc::Sender<TransportEvent>,
    live: &AtomicBool,
    value: Value,
) -> bool {
    if !live.load(Ordering::Acquire) {
        return false;
    }
    timeout(
        Duration::from_secs(2),
        events.send(TransportEvent::PetProgress(value)),
    )
    .await
    .is_ok_and(|result| result.is_ok())
}

async fn run_pet_operation(
    peripheral: Peripheral,
    control: btleplug::api::Characteristic,
    status_characteristic: btleplug::api::Characteristic,
    statuses: Arc<tokio::sync::Mutex<mpsc::Receiver<ValueNotification>>>,
    credential: WatchCredential,
    previous_state: Value,
    work: PetWork,
    request_id: String,
    watch_id: String,
    session_generation: u64,
    mut cancel: watch::Receiver<bool>,
    events: mpsc::Sender<TransportEvent>,
    live: Arc<AtomicBool>,
) -> Result<Value> {
    let package = match &work {
        PetWork::Upload(package) => Some(package.clone()),
        PetWork::Restore => None,
    };
    let result = match work {
        PetWork::Upload(package) => {
            run_pet_upload(
                &peripheral,
                &control,
                &status_characteristic,
                &statuses,
                &credential,
                &previous_state,
                &package,
                &request_id,
                &watch_id,
                session_generation,
                &mut cancel,
                &events,
                &live,
            )
            .await
        }
        PetWork::Restore => {
            run_pet_restore(
                &peripheral,
                &control,
                &status_characteristic,
                &statuses,
                &credential,
                &previous_state,
                &request_id,
                &watch_id,
                session_generation,
                &mut cancel,
                &events,
                &live,
            )
            .await
        }
    };
    if let Err(error) = &result {
        let failure = error.downcast_ref::<PetProtocolFailure>();
        let payload = pet_progress(
            &request_id,
            "error",
            0,
            0,
            &watch_id,
            session_generation,
            package.as_ref(),
            &previous_state,
            failure
                .map(|item| (item.code, item.required_bytes, item.available_bytes))
                .or(Some(("operation_failed", 0, 0))),
        );
        let _ = emit_pet_progress(&events, &live, payload).await;
    }
    result
}

async fn run_pet_upload(
    peripheral: &Peripheral,
    control: &btleplug::api::Characteristic,
    _status_characteristic: &btleplug::api::Characteristic,
    statuses: &Arc<tokio::sync::Mutex<mpsc::Receiver<ValueNotification>>>,
    credential: &WatchCredential,
    previous_state: &Value,
    package: &crate::pet::PetPackage,
    request_id: &str,
    watch_id: &str,
    session_generation: u64,
    cancel: &mut watch::Receiver<bool>,
    events: &mpsc::Sender<TransportEvent>,
    live: &AtomicBool,
) -> Result<Value> {
    let tx = random_transaction();
    let nonce = random_nonzero::<8>();
    let mut status_replay = wire::PetStatusReplayGuard::default();
    status_replay.set_active_nonce(nonce)?;
    let mut sequence = 0u32;
    let mut begun = false;
    let mut commit_sent = false;
    let total = u32::try_from(package.encoded.len())?;
    let send_progress = |phase: &str, acknowledged: usize, state: &Value| {
        pet_progress(
            request_id,
            phase,
            acknowledged,
            package.encoded.len(),
            watch_id,
            session_generation,
            Some(package),
            state,
            None,
        )
    };
    let outcome = async {
        if *cancel.borrow() {
            let _ = emit_pet_progress(events, live, send_progress("cancelled", 0, previous_state)).await;
            return Ok(previous_state.clone());
        }
        sequence += 1;
        let begin = wire::encode_pet_control(&credential.master_key.0, &nonce, sequence,
            PetMessageKind::Begin, PetControlPayload::Begin {
                transaction_id: tx, schema: PET_SCHEMA_VERSION, width: package.width,
                height: package.height, frame_count: package.frames.len() as u16,
                package_length: total, sha256: package.digest,
            })?;
        write_gatt(peripheral, control, &begin).await?;
        begun = true;
        let initial = match await_pet_or_cancel(peripheral, statuses, &credential.master_key, tx, nonce,
            &mut status_replay, cancel, GATT_TIMEOUT).await? {
            Some(status) => status,
            None => {
                send_pet_cancel(peripheral, control, credential, tx, nonce, &mut sequence).await;
                let _ = emit_pet_progress(events, live, send_progress("cancelled", 0, previous_state)).await;
                return Ok(previous_state.clone());
            }
        };
        ensure!(matches!(initial.payload, PetStatusPayload::Progress { transaction_id, acknowledged_bytes: 0, total_bytes: length } if transaction_id == tx && length == total), "watch returned conflicting pet Begin progress");
        let _ = emit_pet_progress(events, live, send_progress("sending", 0, previous_state)).await;
        let mtu = usize::from(peripheral.mtu().max(23));
        let chunk_limit = mtu.saturating_sub(3 + wire::PET_PREFIX_BYTES + 10 + security::TAG_BYTES).min(wire::PET_MAX_CHUNK_BYTES);
        ensure!(chunk_limit > 0, "negotiated BLE MTU is too small for pet chunks");
        let mut offset = 0usize;
        while offset < package.encoded.len() {
            if *cancel.borrow() {
                send_pet_cancel(peripheral, control, credential, tx, nonce, &mut sequence).await;
                let payload = pet_progress(request_id, "cancelled", offset, package.encoded.len(),
                    watch_id, session_generation, Some(package), previous_state, None);
                let _ = emit_pet_progress(events, live, payload).await;
                return Ok(previous_state.clone());
            }
            let end = (offset + chunk_limit).min(package.encoded.len());
            let data = package.encoded[offset..end].to_vec();
            sequence = sequence.checked_add(1).ok_or_else(|| anyhow!("pet control sequence exhausted"))?;
            let frame = wire::encode_pet_control(&credential.master_key.0, &nonce, sequence,
                PetMessageKind::Chunk, PetControlPayload::Chunk { transaction_id: tx, offset: offset as u32, data })?;
            let expected = end as u32;
            let mut attempts = 0;
            loop {
                write_gatt(peripheral, control, &frame).await?;
                match await_pet_or_cancel(peripheral, statuses, &credential.master_key, tx, nonce,
                    &mut status_replay, cancel, GATT_TIMEOUT).await {
                    Ok(Some(status)) => {
                        status_result_progress(&status, tx, expected, total)?;
                        break;
                    }
                    Ok(None) => {
                        send_pet_cancel(peripheral, control, credential, tx, nonce, &mut sequence).await;
                        let payload = pet_progress(request_id, "cancelled", offset, package.encoded.len(),
                            watch_id, session_generation, Some(package), previous_state, None);
                        let _ = emit_pet_progress(events, live, payload).await;
                        return Ok(previous_state.clone());
                    }
                    Err(error) if error.downcast_ref::<PetTimeout>().is_some() && attempts == 0 => {
                        attempts += 1;
                        // A timeout leaves acknowledgement ambiguous. Retransmit the identical
                        // authenticated frame once; firmware's nonce/sequence replay check makes
                        // the duplicate idempotent without advancing the transfer offset.
                    }
                    Err(error) => return Err(error),
                }
            };
            offset = end;
            let _ = emit_pet_progress(events, live, send_progress("sending", offset, previous_state)).await;
        }
        let payload = pet_progress(request_id, "verifying", package.encoded.len(), package.encoded.len(),
            watch_id, session_generation, Some(package), previous_state, None);
        let _ = emit_pet_progress(events, live, payload).await;
        sequence = sequence.checked_add(1).ok_or_else(|| anyhow!("pet control sequence exhausted"))?;
        let commit = wire::encode_pet_control(&credential.master_key.0, &nonce, sequence,
            PetMessageKind::Commit, PetControlPayload::Transaction { transaction_id: tx })?;
        commit_sent = true;
        let committed = async {
            write_gatt(peripheral, control, &commit).await?;
            let result = wait_pet_status(peripheral, statuses, &credential.master_key, tx, nonce,
                &mut status_replay, GATT_TIMEOUT).await?;
            match result.payload {
                PetStatusPayload::Committed { transaction_id, generation, active_sha256 }
                    if transaction_id == tx && active_sha256 == package.digest =>
                    Ok(pet_state_value(PetSource::Custom, true, 1, 1, generation, active_sha256, session_generation, false)),
                _ => bail!("watch returned conflicting pet Commit result"),
            }
        }.await;
        if let Ok(state) = committed {
            let payload = pet_progress(request_id, "complete", package.encoded.len(), package.encoded.len(),
                watch_id, session_generation, Some(package), &state, None);
            let _ = emit_pet_progress(events, live, payload).await;
            return Ok(state);
        }
        let reconciled = query_pet_state_from_parts(peripheral, control, statuses, credential, session_generation).await;
        if let Ok(state) = reconciled {
            let expected_hash = hex::encode(package.digest);
            let matches = state.get("source").and_then(Value::as_str) == Some("custom")
                && state.get("active_hash").and_then(Value::as_str) == Some(expected_hash.as_str());
            let phase = if matches { "reconciled" } else { "ambiguous" };
            let payload = pet_progress(request_id, phase, package.encoded.len(), package.encoded.len(),
                watch_id, session_generation, Some(package), &state, Some(("commit_ambiguous", 0, 0)));
            let _ = emit_pet_progress(events, live, payload).await;
            return Ok(state);
        }
        let payload = pet_progress(request_id, "ambiguous", package.encoded.len(), package.encoded.len(),
            watch_id, session_generation, Some(package), previous_state, Some(("commit_ambiguous", 0, 0)));
        let _ = emit_pet_progress(events, live, payload).await;
        Ok(previous_state.clone())
    }.await;
    if outcome.is_err() && begun && !commit_sent {
        send_pet_cancel(peripheral, control, credential, tx, nonce, &mut sequence).await;
    }
    outcome
}

fn status_result_progress(
    status: &wire::PetStatus,
    transaction: u32,
    acknowledged: u32,
    total: u32,
) -> Result<()> {
    ensure!(
        matches!(status.payload, PetStatusPayload::Progress { transaction_id, acknowledged_bytes, total_bytes }
        if transaction_id == transaction && acknowledged_bytes == acknowledged && total_bytes == total),
        "watch returned conflicting pet progress"
    );
    Ok(())
}

async fn send_pet_cancel(
    peripheral: &Peripheral,
    control: &btleplug::api::Characteristic,
    credential: &WatchCredential,
    transaction: u32,
    nonce: [u8; 8],
    sequence: &mut u32,
) {
    let Some(next) = sequence.checked_add(1) else {
        return;
    };
    *sequence = next;
    if let Ok(frame) = wire::encode_pet_control(
        &credential.master_key.0,
        &nonce,
        next,
        PetMessageKind::Cancel,
        PetControlPayload::Transaction {
            transaction_id: transaction,
        },
    ) {
        let _ = write_gatt(peripheral, control, &frame).await;
    }
}

async fn await_pet_or_cancel(
    peripheral: &Peripheral,
    statuses: &Arc<tokio::sync::Mutex<mpsc::Receiver<ValueNotification>>>,
    key: &security::Secret16,
    transaction: u32,
    nonce: [u8; 8],
    replay: &mut wire::PetStatusReplayGuard,
    cancel: &mut watch::Receiver<bool>,
    wait: Duration,
) -> Result<Option<wire::PetStatus>> {
    if *cancel.borrow() {
        return Ok(None);
    }
    tokio::select! {
        result = wait_pet_status(peripheral, statuses, key, transaction, nonce, replay, wait) => result.map(Some),
        changed = cancel.changed() => {
            let _ = changed;
            if *cancel.borrow() { Ok(None) } else { bail!("pet cancellation channel closed") }
        }
    }
}

async fn query_pet_state_from_parts(
    peripheral: &Peripheral,
    control: &btleplug::api::Characteristic,
    statuses: &Arc<tokio::sync::Mutex<mpsc::Receiver<ValueNotification>>>,
    credential: &WatchCredential,
    session_generation: u64,
) -> Result<Value> {
    let transaction = random_transaction();
    let nonce = random_nonzero::<8>();
    let mut replay = wire::PetStatusReplayGuard::default();
    replay.set_active_nonce(nonce)?;
    let frame = wire::encode_pet_control(
        &credential.master_key.0,
        &nonce,
        1,
        PetMessageKind::Query,
        PetControlPayload::Query {
            transaction_id: transaction,
        },
    )?;
    write_gatt(peripheral, control, &frame).await?;
    let state = wait_pet_status(
        peripheral,
        statuses,
        &credential.master_key,
        transaction,
        nonce,
        &mut replay,
        GATT_TIMEOUT,
    )
    .await?;
    match state.payload {
        PetStatusPayload::State {
            transaction_id,
            source,
            usable,
            schema_min,
            schema_max,
            generation,
            active_sha256,
        } => {
            ensure!(
                transaction_id == transaction,
                "pet State transaction does not match"
            );
            Ok(pet_state_value(
                source,
                usable,
                schema_min,
                schema_max,
                generation,
                active_sha256,
                session_generation,
                false,
            ))
        }
        _ => bail!("watch returned conflicting pet State"),
    }
}

async fn run_pet_restore(
    peripheral: &Peripheral,
    control: &btleplug::api::Characteristic,
    _status_characteristic: &btleplug::api::Characteristic,
    statuses: &Arc<tokio::sync::Mutex<mpsc::Receiver<ValueNotification>>>,
    credential: &WatchCredential,
    previous_state: &Value,
    request_id: &str,
    watch_id: &str,
    session_generation: u64,
    cancel: &mut watch::Receiver<bool>,
    events: &mpsc::Sender<TransportEvent>,
    live: &AtomicBool,
) -> Result<Value> {
    if *cancel.borrow() {
        let _ = emit_pet_progress(
            events,
            live,
            pet_progress(
                request_id,
                "cancelled",
                0,
                0,
                watch_id,
                session_generation,
                None,
                previous_state,
                None,
            ),
        )
        .await;
        return Ok(previous_state.clone());
    }
    let transaction = random_transaction();
    let nonce = random_nonzero::<8>();
    let mut replay = wire::PetStatusReplayGuard::default();
    replay.set_active_nonce(nonce)?;
    let frame = wire::encode_pet_control(
        &credential.master_key.0,
        &nonce,
        1,
        PetMessageKind::Restore,
        PetControlPayload::Transaction {
            transaction_id: transaction,
        },
    )?;
    let sent = write_gatt(peripheral, control, &frame).await;
    let state_result = if sent.is_ok() {
        wait_pet_status(
            peripheral,
            statuses,
            &credential.master_key,
            transaction,
            nonce,
            &mut replay,
            GATT_TIMEOUT,
        )
        .await
    } else {
        sent.map(|_| unreachable!())
    };
    let state = match state_result {
        Ok(status) => state_from_restore_status(status, transaction, session_generation)?,
        Err(_) => match query_pet_state_from_parts(
            peripheral,
            control,
            statuses,
            credential,
            session_generation,
        )
        .await
        {
            Ok(state) => {
                let reconciled = state.get("source").and_then(Value::as_str) == Some("builtin")
                    && state
                        .get("active_hash")
                        .and_then(Value::as_str)
                        .is_some_and(|hash| hash.bytes().all(|byte| byte == b'0'));
                let phase = if reconciled {
                    "reconciled"
                } else {
                    "ambiguous"
                };
                let _ = emit_pet_progress(
                    events,
                    live,
                    pet_progress(
                        request_id,
                        phase,
                        0,
                        0,
                        watch_id,
                        session_generation,
                        None,
                        &state,
                        Some(("restore_ambiguous", 0, 0)),
                    ),
                )
                .await;
                return Ok(state);
            }
            Err(error) => return Err(error),
        },
    };
    let payload = pet_progress(
        request_id,
        "complete",
        0,
        0,
        watch_id,
        session_generation,
        None,
        &state,
        None,
    );
    let _ = emit_pet_progress(events, live, payload).await;
    Ok(state)
}

fn state_from_restore_status(
    status: wire::PetStatus,
    transaction: u32,
    session_generation: u64,
) -> Result<Value> {
    match status.payload {
        PetStatusPayload::State {
            transaction_id,
            source,
            usable,
            schema_min,
            schema_max,
            generation,
            active_sha256,
        } if transaction_id == transaction
            && source == PetSource::BuiltIn
            && active_sha256 == [0; 32] =>
        {
            Ok(pet_state_value(
                source,
                usable,
                schema_min,
                schema_max,
                generation,
                active_sha256,
                session_generation,
                false,
            ))
        }
        _ => bail!("watch returned conflicting pet restore state"),
    }
}
