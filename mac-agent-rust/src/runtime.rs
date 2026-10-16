//! Runtime coordination. Slow work stays outside task updates and BLE callbacks.
use crate::{
    agent_inventory::AgentInventoryCache,
    collector::Collector,
    contracts::{
        AgentPaths, Desktop, DeviceCommand, StatusSnapshot, TransportConfig, TransportEvent,
    },
    pet::PetPackage,
    pet_cache::PetCache,
    pricing::{PricingCatalog, REFRESH_SECONDS},
};
use anyhow::Result;
use serde_json::{Value, json};
use std::{
    collections::BTreeSet,
    path::PathBuf,
    sync::{
        Arc, Mutex,
        atomic::{AtomicBool, Ordering},
        mpsc,
    },
    thread,
    time::{Duration, Instant, SystemTime, UNIX_EPOCH},
};
use std::{
    io::{Read, Write},
    os::unix::fs::OpenOptionsExt,
};
use tokio::sync::{mpsc as async_mpsc, watch};
use unicode_normalization::UnicodeNormalization;

// Successful sends coalesce by durable alert ID. No bounded command queue
// can silently discard a delivery receipt during a slow filesystem operation.
#[derive(Default)]
struct Receipts(Mutex<BTreeSet<String>>);
impl Receipts {
    fn record(&self, ids: Vec<String>) {
        self.0.lock().unwrap_or_else(|e| e.into_inner()).extend(ids);
    }
    fn flush(&self, collector: &mut Collector) {
        let ids: Vec<_> = self
            .0
            .lock()
            .unwrap_or_else(|e| e.into_inner())
            .iter()
            .cloned()
            .collect();
        if ids.is_empty() {
            return;
        }
        match collector.mark_notified(&ids) {
            Ok(()) => {
                let mut pending = self.0.lock().unwrap_or_else(|e| e.into_inner());
                for id in ids {
                    pending.remove(&id);
                }
            }
            Err(e) => tracing::warn!(error=%e,"Delivery receipts retained for persistence retry"),
        }
    }
}
struct SessionEvent {
    epoch: u64,
    event: TransportEvent,
}

async fn retire_transport(
    mut task: tokio::task::JoinHandle<Result<()>>,
    events: &mut async_mpsc::Receiver<SessionEvent>,
    receipts: &Receipts,
) {
    let deadline = tokio::time::sleep(Duration::from_secs(3));
    tokio::pin!(deadline);
    // Keep consuming the bounded relay while awaiting teardown, so delivery
    // receipts cannot be trapped behind UI events in a full channel.
    loop {
        tokio::select! {
            _=&mut task=>break,
            _=&mut deadline=>{task.abort();let _=task.await;break},
            envelope=events.recv()=>{
                if let Some(SessionEvent{event:TransportEvent::NotificationDelivered(ids),..})=envelope{receipts.record(ids);}
            }
        }
    }
}

pub struct RuntimeHandle {
    stop: Arc<AtomicBool>,
    shutdown: watch::Sender<bool>,
    result: mpsc::Receiver<Result<()>>,
    collector_result: mpsc::Receiver<()>,
    receipts: Arc<Receipts>,
}

impl RuntimeHandle {
    pub fn shutdown(self) -> Result<()> {
        let _ = self.shutdown.send(true);
        // The desktop has already cancelled its audio session. Networking is
        // bounded independently; waiting on a stuck HTTP sampler cannot hold UI.
        let result = match self.result.recv_timeout(Duration::from_secs(6)) {
            Ok(result) => result,
            Err(mpsc::RecvTimeoutError::Timeout) => {
                Err(anyhow::anyhow!("Agent shutdown exceeded deadline"))
            }
            Err(_) => Ok(()),
        };
        self.stop.store(true, Ordering::Release);
        self.collector_result
            .recv_timeout(Duration::from_secs(12))
            .map_err(|_| anyhow::anyhow!("Collector shutdown exceeded deadline"))?;
        anyhow::ensure!(
            self.receipts
                .0
                .lock()
                .unwrap_or_else(|e| e.into_inner())
                .is_empty(),
            "Delivered notification receipts could not be persisted; check Agent state-directory permissions"
        );
        result
    }
}

#[derive(Default)]
struct PresentationState {
    connection: String,
    connection_enabled: bool,
    metadata: Value,
    pet: Value,
}

#[derive(Clone)]
struct PendingPet {
    package: PetPackage,
    watch_id: String,
    generation: u64,
    request_id: Option<String>,
    committed: bool,
}

fn render_snapshot(snapshot: &StatusSnapshot, presentation: &PresentationState) -> StatusSnapshot {
    let mut view = snapshot.clone();
    if !view.telemetry.is_object() {
        view.telemetry = json!({});
    }
    view.telemetry["_ui_connection"] = presentation.metadata.clone();
    if !view.telemetry["_ui_connection"].is_object() {
        view.telemetry["_ui_connection"] = json!({});
    }
    view.telemetry["_ui_connection"]["state"] = json!(if presentation.connection.is_empty() {
        "Scanning"
    } else {
        &presentation.connection
    });
    view.telemetry["_ui_connection"]["connection_enabled"] = json!(presentation.connection_enabled);
    view.telemetry["_ui_pet"] = presentation.pet.clone();
    view
}

fn connection_identity(value: &Value) -> (Option<String>, Option<u64>) {
    (
        value
            .get("watch_id")
            .and_then(Value::as_str)
            .map(str::to_owned),
        value.get("generation").and_then(Value::as_u64),
    )
}

fn failed_snapshot() -> StatusSnapshot {
    StatusSnapshot {
        codex: json!({"codex_status_ok":false,"codex_model":"","codex_effort":"","codex_5h_left_pct":null,
            "codex_week_left_pct":null,"codex_alert_pending":0,"codex_alert_running":0,"codex_alert_completed":0,
            "codex_alert_revision":0,"codex_alerts":[]}),
        trae: json!({"trae_online":false,"trae_process_state":"unknown","trae_model":"--","trae_effort":"--",
            "trae_revision":0,"trae_pending_count":0,"trae_running_count":0,"trae_completed_count":0,"trae_tasks":[]}),
        telemetry: json!({"telemetry_v":1,"usage":{"s":"error","q":"error","at":0,"qa":0,
            "h":null,"w":null,"hr":null,"wr":null,"t":null,"c":null,"cs":"unavailable","day":""}}),
        ..StatusSnapshot::default()
    }
}

fn cache_committed(candidate: &PendingPet, cache: &PetCache, desktop: &dyn Desktop) -> Result<()> {
    cache.store(&candidate.watch_id, &candidate.package)?;
    desktop.show_pet_preview(candidate.package.clone())?;
    Ok(())
}

fn sanitize_preferences(input: &Value) -> Value {
    let mut result = json!({});
    if let Some(enabled) = input.get("connectionEnabled").and_then(Value::as_bool) {
        result["connectionEnabled"] = json!(enabled);
    }
    if let Some(id) = input.get("selectedWatchId").and_then(Value::as_str) {
        if id.len() == 32 && id.bytes().all(|b| b.is_ascii_hexdigit()) {
            result["selectedWatchId"] = json!(id.to_ascii_lowercase());
        }
    }
    if let Some(name) = input.get("agentName").and_then(Value::as_str) {
        // The security layer performs authoritative NFC/name normalization and
        // signing. This file holds public preferences only, never credentials.
        if !name.is_empty()
            && name.len() <= 24
            && name.nfc().collect::<String>() == name
            && !name.chars().any(crate::security::is_category_c)
        {
            result["agentName"] = json!(name);
        }
    }
    result
}

fn save_preferences(path: &PathBuf, value: &Value) -> Result<()> {
    let filtered = sanitize_preferences(value);
    let temp = path.with_extension(format!("{}.tmp", uuid::Uuid::new_v4()));
    let mut file = std::fs::OpenOptions::new()
        .write(true)
        .create_new(true)
        .mode(0o600)
        .custom_flags(libc::O_NOFOLLOW)
        .open(&temp)?;
    let result = (|| -> Result<()> {
        file.write_all(&serde_json::to_vec(&filtered)?)?;
        file.sync_all()?;
        std::fs::rename(&temp, path)?;
        Ok(())
    })();
    if result.is_err() {
        let _ = std::fs::remove_file(&temp);
    }
    result?;
    Ok(())
}

pub fn start(
    paths: AgentPaths,
    pricing: Arc<PricingCatalog>,
    desktop: Arc<dyn Desktop>,
    mut commands: async_mpsc::Receiver<DeviceCommand>,
    preferences: Value,
    claude_token: Option<String>,
    opencode_import: Option<PathBuf>,
    legacy_trae: Option<PathBuf>,
) -> Result<RuntimeHandle> {
    std::fs::create_dir_all(&paths.state)?;
    let preferences_path = paths.state.join("preferences.json");
    let mut prefs = sanitize_preferences(&preferences);
    // Native NSUserDefaults remains authoritative for the original toggle.
    // JSON supplies added public preferences; it contains no account secrets.
    if let Ok(file) = std::fs::OpenOptions::new()
        .read(true)
        .custom_flags(libc::O_NOFOLLOW | libc::O_NONBLOCK)
        .open(&preferences_path)
    {
        if file
            .metadata()
            .is_ok_and(|m| m.is_file() && m.len() <= 4096)
        {
            let mut bytes = Vec::new();
            if file.take(4097).read_to_end(&mut bytes).is_ok() && bytes.len() <= 4096 {
                if let Ok(saved) = serde_json::from_slice::<Value>(&bytes) {
                    let saved = sanitize_preferences(&saved);
                    for key in ["selectedWatchId", "agentName"] {
                        if saved.get(key).is_some() {
                            prefs[key] = saved[key].clone();
                        }
                    }
                }
            }
        }
    }
    let connection_enabled = prefs
        .get("connectionEnabled")
        .and_then(Value::as_bool)
        .unwrap_or(true);
    let mut config = TransportConfig {
        device_name: "StopWatch-Codex".into(),
        selected_watch_id: prefs
            .get("selectedWatchId")
            .and_then(Value::as_str)
            .map(str::to_owned),
        agent_name: prefs
            .get("agentName")
            .and_then(Value::as_str)
            .unwrap_or("")
            .to_owned(),
        state_dir: paths.state.clone(),
    };
    let stop = Arc::new(AtomicBool::new(false));
    let inventory_cache = AgentInventoryCache::default();
    inventory_cache.start_monitor(stop.clone())?;
    let (shutdown_tx, mut shutdown_rx) = watch::channel(false);
    let (snapshot_tx, snapshot_rx) = watch::channel(StatusSnapshot::default());
    let receipts = Arc::new(Receipts::default());
    let (collector_result_tx, collector_result_rx) = mpsc::sync_channel(1);
    let presentation = Arc::new(Mutex::new(PresentationState {
        connection: if connection_enabled { "initializing" } else { "disabled" }.into(),
        connection_enabled,
        ..PresentationState::default()
    }));
    if let Ok(p) = presentation.lock() {
        desktop.update_ui(&render_snapshot(&StatusSnapshot::default(), &p), &p.connection);
    }
    tracing::info!(connection_enabled, "Watch connection preference loaded");
    let cache = Arc::new(PetCache::new(paths.state.join("Pets")));

    let price_stop = stop.clone();
    let catalog = pricing.clone();
    thread::Builder::new().name("models-dev-prices".into()).spawn(move || {
        while !price_stop.load(Ordering::Acquire) {
            let now = SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default().as_secs();
            if catalog.checked_at()>now || now.saturating_sub(catalog.checked_at()) >= REFRESH_SECONDS {
                if let Err(e) = catalog.refresh() { tracing::warn!(error=%e, "Public pricing update failed; retaining validated cache"); }
            }
            // Retry failures no more than once per minute. Successful catalogs
            // remain fresh for six hours. No HTTP work occurs in task polling.
            for _ in 0..240 { if price_stop.load(Ordering::Acquire) { return } thread::sleep(Duration::from_millis(250)); }
        }
    })?;

    let worker_paths = paths.clone();
    let collector_stop = stop.clone();
    let collector_desktop = desktop.clone();
    let collector_presentation = presentation.clone();
    let collector_receipts = receipts.clone();
    let collector_inventory = inventory_cache.clone();
    let ccswitch_database = std::env::var_os("HOME")
        .map(|home| PathBuf::from(home).join(".cc-switch/cc-switch.db"));
    thread::Builder::new()
        .name("codex-realtime-state".into())
        .spawn(move || {
            let token = claude_token.map(zeroize::Zeroizing::new);
            let mut collector: Option<Collector> = None;
            let mut sync_log = crate::status_sync::StatusSyncLog::default();
            while !collector_stop.load(Ordering::Acquire) {
                let started = Instant::now();
                if collector.is_none() {
                    match Collector::with_ccswitch_usage_and_host_enabled(
                        worker_paths.clone(),
                        pricing.clone(),
                        ccswitch_database.clone(),
                        false,
                    ) {
                        Ok(c) => {
                            collector = Some(
                                c.with_app_server_enabled(true)
                                    .with_optional_providers(
                                        token.as_ref().map(|t| t.to_string()),
                                        opencode_import.clone(),
                                    )
                                    .with_legacy_trae_state_dir(legacy_trae.clone()),
                            )
                        }
                        Err(e) => tracing::error!(error=%e,"Collector startup failed; retrying"),
                    }
                }
                let mut snapshot = if let Some(c) = collector.as_mut() {
                    collector_receipts.flush(c);
                    match c.poll() {
                        Ok(snapshot) => snapshot,
                        Err(e) => {
                            tracing::warn!(error=%e,"Task collection failed; invalidating status");
                            failed_snapshot()
                        }
                    }
                } else {
                    failed_snapshot()
                };
                // The bounded inventory refresh runs separately from this
                // two-second task collector. Inject it before either UI or BLE
                // consumers receive the snapshot; unknown stays absent.
                collector_inventory.inject(&mut snapshot);
                sync_log.record("collected", &snapshot, started.elapsed());
                if let Ok(p) = collector_presentation.lock() {
                    collector_desktop.update_ui(&render_snapshot(&snapshot, &p), &p.connection);
                }
                snapshot_tx.send_replace(snapshot);
                let until = started + Duration::from_secs(2);
                while !collector_stop.load(Ordering::Acquire) && Instant::now() < until {
                    if let Some(c) = collector.as_mut() {
                        collector_receipts.flush(c);
                    }
                    thread::sleep(Duration::from_millis(50));
                }
            }
            if let Some(mut c) = collector {
                collector_receipts.flush(&mut c);
                c.close();
            }
            let _ = collector_result_tx.send(());
        })?;

    let (result_tx, result_rx) = mpsc::sync_channel(1);
    let runtime_stop = stop.clone();
    let runtime_receipts = receipts.clone();
    thread::Builder::new().name("stopwatch-ble-runtime".into()).spawn(move || {
        let result = (|| -> Result<()> {
            let runtime = tokio::runtime::Builder::new_multi_thread().worker_threads(2).enable_all().build()?;
            runtime.block_on(async move {
                let (events_tx, mut events_rx) = async_mpsc::channel::<SessionEvent>(32);
                let mut transport = None;
                let mut device_commands = None;
                let mut session_shutdown = None;
                let mut enabled = connection_enabled;
                let mut pending_pet: Option<PendingPet> = None;
                let mut retry_pet: Option<PendingPet> = None;
                let mut epoch=0_u64;
                let mut restart_after=Instant::now();
                let mut preview_identity:Option<(String,String)>=None;
                loop {
                    if runtime_stop.load(Ordering::Acquire) || *shutdown_rx.borrow() { break }
                    if enabled && transport.is_none() && Instant::now()>=restart_after {
                        if let Ok(mut p) = presentation.lock() {
                            p.connection = "initializing".into();
                            p.metadata = json!({});
                            desktop.update_ui(&render_snapshot(&snapshot_rx.borrow(), &p), &p.connection);
                        }
                        epoch=epoch.checked_add(1).expect("session epoch exhausted");
                        let task_epoch=epoch;
                        let (tx, rx) = async_mpsc::channel(16);
                        let (stop_tx, stop_rx) = watch::channel(false);
                        let task_config = config.clone(); let task_snapshots = snapshot_rx.clone();
                        let task_desktop = desktop.clone(); let task_events = events_tx.clone();
                        let task_receipts=runtime_receipts.clone();
                        transport = Some(tokio::spawn(async move {
                            let (raw_tx,mut raw_rx)=async_mpsc::channel(32);
                            let relay=async move{
                                while let Some(event)=raw_rx.recv().await{
                                    if let TransportEvent::NotificationDelivered(ids)=event{task_receipts.record(ids);continue}
                                    if task_events.send(SessionEvent{epoch:task_epoch,event}).await.is_err(){break}
                                }
                            };
                            let (result,())=tokio::join!(crate::transport::run(task_config,task_snapshots,task_desktop,raw_tx,rx,stop_rx),relay);
                            result
                        }));
                        device_commands = Some(tx); session_shutdown = Some(stop_tx);
                    }
                    tokio::select! {
                        _ = shutdown_rx.changed() => { if *shutdown_rx.borrow() { break } },
                        command = commands.recv() => {
                            let Some(command) = command else { break };
                            match command {
                                DeviceCommand::Quit => break,
                                DeviceCommand::SetPreferences(value) => {
                                    let filtered = sanitize_preferences(&value);
                                    if let Some(e) = filtered.get("connectionEnabled").and_then(Value::as_bool) { enabled = e; prefs["connectionEnabled"] = json!(e); }
                                    for key in ["selectedWatchId", "agentName"] { if filtered.get(key).is_some() { prefs[key] = filtered[key].clone(); } }
                                    if let Err(error) = save_preferences(&preferences_path, &prefs) {
                                        tracing::warn!(%error, "Could not save public Agent preferences");
                                    }
                                    if let Ok(mut p) = presentation.lock() {
                                        p.connection_enabled = enabled;
                                        if !enabled || transport.is_none() {
                                            p.connection = if enabled { "initializing" } else { "disabled" }.into();
                                            p.metadata = json!({});
                                        }
                                        desktop.update_ui(&render_snapshot(&snapshot_rx.borrow(), &p), &p.connection);
                                    }
                                    tracing::info!(connection_enabled=enabled, "Watch connection preference applied");
                                    if enabled { restart_after = Instant::now(); }
                                    if !enabled {
                                        epoch+=1;
                                        if let Some(tx) = session_shutdown.take() { let _ = tx.send(true); }
                                        if let Some(task)=transport.take(){retire_transport(task,&mut events_rx,&runtime_receipts).await;}
                                        device_commands = None; pending_pet = None;retry_pet=None;preview_identity=None;
                                        let _ = desktop.voice_stop(true);desktop.clear_pet_preview();desktop.pairing_code("");
                                        if let Ok(mut p)=presentation.lock(){p.connection="disabled".into();p.metadata=json!({});p.pet=json!({});desktop.update_ui(&render_snapshot(&snapshot_rx.borrow(),&p),&p.connection);}
                                    }
                                }
                                DeviceCommand::Reconnect => {
                                    enabled = true;
                                    prefs["connectionEnabled"] = json!(true);
                                    if let Err(error) = save_preferences(&preferences_path, &prefs) {
                                        tracing::warn!(%error, "Could not save reconnect preference");
                                    }
                                    if let Ok(mut p) = presentation.lock() {
                                        if let Some(id) = p.metadata.get("watch_id").and_then(Value::as_str) {
                                            config.selected_watch_id = Some(id.to_owned());
                                        }
                                        p.connection_enabled = true;
                                        p.connection = "initializing".into();
                                        p.metadata = json!({});
                                        p.pet = json!({});
                                        desktop.update_ui(&render_snapshot(&snapshot_rx.borrow(), &p), &p.connection);
                                    }
                                    tracing::info!("Reconnect requested; restarting watch discovery");
                                    epoch += 1;
                                    if let Some(tx) = session_shutdown.take() { let _ = tx.send(true); }
                                    if let Some(task) = transport.take() {
                                        retire_transport(task, &mut events_rx, &runtime_receipts).await;
                                    }
                                    device_commands = None;
                                    pending_pet = None;
                                    retry_pet = None;
                                    preview_identity = None;
                                    desktop.pairing_code("");
                                    let _ = desktop.voice_stop(true);
                                    desktop.clear_pet_preview();
                                    restart_after = Instant::now();
                                }
                                DeviceCommand::SelectWatch(id) => {
                                    let filtered=sanitize_preferences(&json!({"selectedWatchId":id}));
                                    let Some(id)=filtered.get("selectedWatchId").and_then(Value::as_str).map(str::to_owned)else{continue};
                                    config.selected_watch_id = Some(id.clone()); prefs["selectedWatchId"] = json!(id);
                                    let _ = save_preferences(&preferences_path, &prefs);
                                    pending_pet = None;retry_pet=None;preview_identity=None;
                                    desktop.clear_pet_preview();
                                    if let Some(tx) = &device_commands { let _ = tx.try_send(DeviceCommand::SelectWatch(config.selected_watch_id.clone().unwrap())); }
                                }
                                DeviceCommand::RenameAgent(name) => {
                                    if sanitize_preferences(&json!({"agentName":name})).get("agentName").is_none(){continue}
                                    config.agent_name = name.clone(); prefs["agentName"] = json!(name);
                                    let _ = save_preferences(&preferences_path, &prefs);
                                    if let Some(tx) = &device_commands { let _ = tx.try_send(DeviceCommand::RenameAgent(config.agent_name.clone())); }
                                }
                                DeviceCommand::UploadPet(package) => {
                                    // One active candidate prevents ambiguous completion/progress
                                    // from being applied to a later local upload request.
                                    if pending_pet.is_some() { continue }
                                    let Ok(canonical)=crate::pet::parse_package(&package.encoded) else{continue};
                                    if canonical.digest!=package.digest{continue}
                                    let identity = presentation.lock().ok().map(|p| connection_identity(&p.metadata)).unwrap_or_default();
                                    if let (Some(tx),(Some(watch_id),Some(generation))) = (&device_commands,identity) {
                                        let request_id=uuid::Uuid::new_v4().to_string();
                                        if tx.try_send(DeviceCommand::UploadPetRequest{package:canonical.clone(),request_id:request_id.clone()}).is_ok() {
                                            pending_pet=Some(PendingPet{package:canonical,watch_id,generation,request_id:Some(request_id),committed:false});
                                        }
                                    }
                                }
                                DeviceCommand::CancelPet|DeviceCommand::RestorePet=>{
                                    if let Some(tx)=&device_commands{
                                        if tx.try_send(command).is_ok(){pending_pet=None;}
                                    }
                                }
                                DeviceCommand::ForgetWatch(id) => {
                                    if let Some(tx) = &device_commands { let _ = tx.try_send(DeviceCommand::ForgetWatch(id.clone())); }
                                    let _ = cache.remove_watch(&id); pending_pet = None;retry_pet=None;preview_identity=None;desktop.clear_pet_preview();
                                }
                                command => { if let Some(tx) = &device_commands { let _ = tx.try_send(command); } },
                            }
                        },
                        event = events_rx.recv() => {
                            if let Some(SessionEvent{epoch:event_epoch,event}) = event {
                                if let TransportEvent::NotificationDelivered(ids)=event{
                                    runtime_receipts.record(ids);continue
                                }
                                if event_epoch!=epoch{continue}
                                match event {
                                    TransportEvent::NotificationDelivered(_) => unreachable!(),
                                    TransportEvent::Connection {state,metadata} => {
                                        let identity=connection_identity(&metadata);
                                        if presentation.lock().ok().is_some_and(|p|connection_identity(&p.metadata).0!=identity.0){desktop.clear_pet_preview();preview_identity=None;}
                                        let ready=metadata.pointer("/pet/capability").and_then(Value::as_str)==Some("ready");
                                        let active_hash=metadata.pointer("/pet/active_hash").and_then(Value::as_str);
                                        if let Some(candidate)=&mut pending_pet{
                                            if identity==(Some(candidate.watch_id.clone()),Some(candidate.generation))&&ready{
                                                candidate.committed=active_hash==Some(hex::encode(candidate.package.digest).as_str());
                                            }
                                        }
                                        if pending_pet.as_ref().is_some_and(|c|!c.committed&&c.request_id.is_some())
                                            &&ready&&metadata.pointer("/pet/operation_active").and_then(Value::as_bool)==Some(false){
                                            pending_pet=None;
                                        }
                                        if pending_pet.as_ref().is_some_and(|c|c.committed){
                                            let candidate=pending_pet.take().unwrap();
                                            if let Err(e)=cache_committed(&candidate,&cache,desktop.as_ref()){tracing::warn!(error=%e,"Committed pet cache publication will retry");retry_pet=Some(candidate);}
                                        }
                                        if let (Some(watch),true,Some(hash))=(&identity.0,ready,active_hash){
                                            let current=(watch.clone(),hash.to_owned());
                                            if preview_identity.as_ref()!=Some(&current){
                                                if let Ok(Some(package))=cache.load(watch,hash){
                                                    if desktop.show_pet_preview(package).is_ok(){preview_identity=Some(current);}
                                                }
                                                else if hash.bytes().all(|b|b==b'0'){desktop.clear_pet_preview();preview_identity=Some(current);}
                                            }
                                        }
                                        if let Ok(mut p) = presentation.lock() {
                                            let old = connection_identity(&p.metadata); let new = connection_identity(&metadata);
                                            if old != new || !matches!(state.as_str(),"connected"|"live"|"ready"|"BLE LIVE") { pending_pet = None; }
                                            if new.0.is_none(){preview_identity=None;desktop.clear_pet_preview();}
                                            if p.connection != state {
                                                tracing::info!(state=%state, "Watch connection state changed");
                                            }
                                            p.connection = state; p.metadata = metadata;
                                            desktop.update_ui(&render_snapshot(&snapshot_rx.borrow(), &p), &p.connection);
                                        }
                                    }
                                    TransportEvent::PetProgress(value) => {
                                        if let Some(candidate) = &mut pending_pet {
                                            let hash = hex::encode(candidate.package.digest);
                                            let supplied_hash = value.get("digest").and_then(Value::as_str);
                                            let id = value.get("request_id").and_then(Value::as_str);
                                            let watch = value.get("watch_id").and_then(Value::as_str);
                                            let generation = value.get("generation").and_then(Value::as_u64);
                                            let identity_ok = Some(candidate.watch_id.as_str())==watch&&Some(candidate.generation)==generation;
                                            let request_ok = candidate.request_id.as_deref().is_some_and(|r|Some(r)==id);
                                            if identity_ok && request_ok && supplied_hash==Some(hash.as_str()) {
                                                match value.get("phase").and_then(Value::as_str) {
                                                    Some("complete"|"reconciled") if value.get("active_hash").and_then(Value::as_str)==Some(hash.as_str()) => {
                                                        candidate.committed=true;
                                                        let candidate=pending_pet.take().unwrap();
                                                        if let Err(e)=cache_committed(&candidate,&cache,desktop.as_ref()){tracing::warn!(error=%e,"Committed pet preview will retry");retry_pet=Some(candidate);}
                                                    },
                                                    Some("error"|"cancelled") => pending_pet=None,
                                                    // A following authoritative query can still establish
                                                    // commit; no upload or submit is automatically replayed.
                                                    Some("ambiguous") => (),
                                                    _ => (),
                                                }
                                            }
                                        }
                                        if let Ok(mut p) = presentation.lock() { p.pet=value; desktop.update_ui(&render_snapshot(&snapshot_rx.borrow(),&p),&p.connection); }
                                    }
                                    TransportEvent::Error(message) => {
                                        tracing::warn!(error=%message,"Watch operation failed");
                                        if let Ok(mut p) = presentation.lock() {
                                            if !p.metadata.is_object() { p.metadata = json!({}); }
                                            p.metadata["error"] = json!(message);
                                            desktop.update_ui(&render_snapshot(&snapshot_rx.borrow(), &p), &p.connection);
                                        }
                                    },
                                }
                            }
                        },
                        _ = tokio::time::sleep(Duration::from_millis(250)) => {
                            if let Some(candidate)=&retry_pet{
                                if cache.store(&candidate.watch_id,&candidate.package).is_ok(){
                                    if presentation.lock().ok().is_some_and(|p|p.metadata.pointer("/pet/active_hash").and_then(Value::as_str)==Some(hex::encode(candidate.package.digest).as_str())&&connection_identity(&p.metadata).0.as_deref()==Some(candidate.watch_id.as_str())){
                                        let _=desktop.show_pet_preview(candidate.package.clone());
                                    }
                                    retry_pet=None;
                                }
                            }
                            if transport.as_ref().is_some_and(|t|t.is_finished()) {
                                let failure = if let Some(task) = transport.take() {
                                    match task.await {
                                        Ok(Ok(())) => "Bluetooth session ended".to_owned(),
                                        Ok(Err(error)) => format!("{error:#}"),
                                        Err(error) => format!("Bluetooth task stopped: {error}"),
                                    }
                                } else { "Bluetooth session ended".to_owned() };
                                tracing::warn!(error=%failure,"BLE runtime failed; retry scheduled");
                                if let Ok(mut p) = presentation.lock() {
                                    p.connection = "error".into();
                                    p.metadata = json!({"error":failure,"detail":"Bluetooth retry scheduled"});
                                    desktop.update_ui(&render_snapshot(&snapshot_rx.borrow(),&p),&p.connection);
                                }
                                desktop.pairing_code("");
                                epoch+=1;device_commands=None; session_shutdown=None;pending_pet=None;
                                preview_identity=None;desktop.clear_pet_preview();
                                restart_after=Instant::now()+Duration::from_secs(2);
                            }
                        }
                    }
                }
                if let Some(tx)=session_shutdown { let _=tx.send(true); }
                if let Some(task)=transport{retire_transport(task,&mut events_rx,&runtime_receipts).await;}
                while let Ok(envelope)=events_rx.try_recv(){
                    if let TransportEvent::NotificationDelivered(ids)=envelope.event{runtime_receipts.record(ids);}
                }
                runtime_stop.store(true,Ordering::Release);
                let _=desktop.voice_stop(true); Ok(())
            })
        })();
        if let Err(error) = &result { tracing::error!(error=%format!("{error:#}"), "BLE supervisor stopped"); }
        let _ = result_tx.send(result);
    })?;
    Ok(RuntimeHandle {
        stop,
        shutdown: shutdown_tx,
        result: result_rx,
        collector_result: collector_result_rx,
        receipts,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn public_preferences_filter_secret_and_invalid_identity() {
        let input = json!({"connectionEnabled":false,"selectedWatchId":"../keys","agentName":"Mac","oauth_token":"fixture-secret","cookie":"fixture-cookie"});
        assert_eq!(
            sanitize_preferences(&input),
            json!({"connectionEnabled":false,"agentName":"Mac"})
        );
        assert!(!sanitize_preferences(&input).to_string().contains("fixture"));
    }
    #[test]
    fn delivery_receipts_coalesce_without_overflow() {
        let receipts = Receipts::default();
        for _ in 0..100 {
            receipts.record(vec!["delivered".into()]);
        }
        assert_eq!(receipts.0.lock().unwrap().len(), 1);
    }
    #[test]
    fn collection_failure_exposes_no_actionable_stale_tasks() {
        let snapshot = failed_snapshot();
        assert!(snapshot.details.is_empty() && snapshot.notifications.is_empty());
        assert!(snapshot.telemetry["usage"]["h"].is_null());
        assert_eq!(snapshot.telemetry["usage"]["q"], "error");
        assert_eq!(snapshot.codex["codex_alerts"], json!([]));
    }
    #[tokio::test]
    async fn retirement_drains_backpressure_and_preserves_delivery_receipts() {
        let (tx, mut rx) = async_mpsc::channel(32);
        let task = tokio::spawn(async move {
            for _ in 0..64 {
                tx.send(SessionEvent {
                    epoch: 1,
                    event: TransportEvent::Connection {
                        state: "old".into(),
                        metadata: json!({}),
                    },
                })
                .await
                .unwrap();
            }
            tx.send(SessionEvent {
                epoch: 1,
                event: TransportEvent::NotificationDelivered(vec!["delivered".into()]),
            })
            .await
            .unwrap();
            Ok(())
        });
        let receipts = Receipts::default();
        retire_transport(task, &mut rx, &receipts).await;
        // A final event can race task completion; normal shutdown drains it too.
        while let Ok(envelope) = rx.try_recv() {
            if let TransportEvent::NotificationDelivered(ids) = envelope.event {
                receipts.record(ids);
            }
        }
        assert!(receipts.0.lock().unwrap().contains("delivered"));
    }
}
