use std::{fs, path::PathBuf, sync::Arc};

use chrono::Local;
use rusqlite::Connection;
use serde_json::{Value, json};
use stopwatch_codex_agent::{collector::Collector, contracts::AgentPaths, pricing::PricingCatalog};
use tempfile::TempDir;

struct Fixture {
    _dir: TempDir,
    paths: AgentPaths,
    rollout: PathBuf,
}

fn fixture() -> Fixture {
    let dir = tempfile::tempdir().unwrap();
    let codex = dir.path().join("codex");
    let state = dir.path().join("agent-state");
    let sessions = codex.join("sessions");
    fs::create_dir_all(&sessions).unwrap();
    fs::create_dir_all(&state).unwrap();

    let rollout = sessions.join("rollout-thread-a.jsonl");
    let mut existing_history = serde_json::to_vec(&json!({
        "timestamp": "2026-10-13T12:00:00Z",
        "type": "event_msg",
        "payload": {"type": "task_started", "turn_id": "historic-turn"}
    }))
    .unwrap();
    existing_history.push(b'\n');
    fs::write(&rollout, existing_history).unwrap();
    let database = Connection::open(codex.join("state_5.sqlite")).unwrap();
    database
        .execute_batch(
            "CREATE TABLE threads (
                id TEXT PRIMARY KEY,
                cwd TEXT,
                title TEXT,
                rollout_path TEXT,
                model_provider TEXT,
                thread_source TEXT,
                updated_at_ms INTEGER,
                model TEXT,
                reasoning_effort TEXT
            );",
        )
        .unwrap();
    database
        .execute(
            "INSERT INTO threads
                (id,cwd,title,rollout_path,model_provider,thread_source,updated_at_ms,model,reasoning_effort)
             VALUES (?1,?2,?3,?4,'openai',NULL,?5,?6,?7)",
            rusqlite::params![
                "thread-a",
                "/work/demo",
                "Collector fixture",
                rollout.to_string_lossy(),
                1_i64,
                "gpt-test",
                "high"
            ],
        )
        .unwrap();
    drop(database);

    Fixture {
        _dir: dir,
        paths: AgentPaths {
            codex,
            trae: None,
            state,
            pricing_override: None,
        },
        rollout,
    }
}

fn new_collector(paths: &AgentPaths) -> Collector {
    let catalog = PricingCatalog::new(
        paths.state.join("price-cache.json"),
        paths.pricing_override.clone(),
    )
    .unwrap();
    Collector::new(paths.clone(), Arc::new(catalog)).unwrap()
}

fn append_event(rollout: &PathBuf, event: Value) {
    use std::io::Write;
    let mut file = fs::OpenOptions::new().append(true).open(rollout).unwrap();
    serde_json::to_writer(&mut file, &event).unwrap();
    file.write_all(b"\n").unwrap();
}

fn yesterday_at(hour: u32, minute: u32) -> String {
    let date = Local::now().date_naive().pred_opt().unwrap();
    let local = date
        .and_hms_opt(hour, minute, 0)
        .unwrap()
        .and_local_timezone(Local)
        .single()
        .unwrap();
    local.to_rfc3339()
}

fn write_alert_ledger(fixture: &Fixture, alerts: Vec<Value>, revision: u64) {
    fs::write(
        fixture.paths.state.join("codex-alerts.json"),
        serde_json::to_vec(&json!({
            "version": 3,
            "classifier_version": 3,
            "classifier_retry_threads": [],
            "reply_metadata_version": 2,
            "reply_metadata_retry_threads": [],
            "abort_reducer_version": 1,
            "abort_retry_threads": [],
            "initialized_at": yesterday_at(22, 0),
            "last_scan_at": yesterday_at(22, 0),
            "revision": revision,
            "cursors": {
                "thread-a": {
                    "path": fixture.rollout.to_string_lossy(),
                    "offset": fs::metadata(&fixture.rollout).unwrap().len()
                }
            },
            "alerts": alerts,
            "turn_signals": {}
        }))
        .unwrap(),
    )
    .unwrap();
}

#[test]
fn pending_reply_is_private_detail_and_notification_is_durable_after_delivery() {
    let fixture = fixture();
    let mut collector = new_collector(&fixture.paths);
    // The first poll establishes a baseline and must not replay pre-existing work.
    let initial = collector.poll().unwrap();
    assert_eq!(initial.codex["codex_alert_pending"], 0);
    append_event(
        &fixture.rollout,
        json!({
            "timestamp": "2026-10-14T06:00:00Z",
            "type": "event_msg",
            "payload": {"type": "task_started", "turn_id": "turn-a"}
        }),
    );
    append_event(
        &fixture.rollout,
        json!({
            "timestamp": "2026-10-14T06:00:01Z",
            "type": "event_msg",
            "payload": {
                "type": "task_complete",
                "turn_id": "turn-a",
                "last_agent_message": "Which option should I use?"
            }
        }),
    );

    let pending = collector.poll().unwrap();
    assert_eq!(pending.codex["codex_alert_pending"], 1);
    assert_eq!(pending.codex["codex_alerts"][0]["state"], "pending");
    assert_eq!(pending.codex["codex_alerts"][0]["outcome"], "waiting");
    let route = pending.codex["codex_alerts"][0]["route"]
        .as_str()
        .unwrap()
        .to_string();
    assert_eq!(route, "01e0a1b2900f0425");
    assert_eq!(pending.details[&route].reply, "Which option should I use?");
    assert!(
        !serde_json::to_string(&pending.codex)
            .unwrap()
            .contains("Which option should I use?")
    );
    assert_eq!(pending.notifications.len(), 1);

    // This method is called by runtime only after a successful transport send.
    collector.mark_notified(&pending.notifications).unwrap();
    assert!(collector.poll().unwrap().notifications.is_empty());
    collector.close();

    let mut restarted = new_collector(&fixture.paths);
    assert!(restarted.poll().unwrap().notifications.is_empty());
    restarted.close();
}

#[test]
fn first_run_imports_python_ledger_without_repeating_delivered_pending_alerts() {
    let fixture = fixture();
    let source = fixture.paths.codex.join(".stopwatch_alerts.json");
    let source_bytes = serde_json::to_vec(&json!({
        "version": 3,
        "classifier_version": 3,
        "classifier_retry_threads": [],
        "reply_metadata_version": 2,
        "reply_metadata_retry_threads": [],
        "abort_reducer_version": 1,
        "abort_retry_threads": [],
        "initialized_at": "2026-10-14T06:00:00Z",
        "last_scan_at": "2026-10-14T06:00:00Z",
        "revision": 77,
        "classifier_version": 3,
        "classifier_retry_threads": [],
        "reply_metadata_version": 2,
        "reply_metadata_retry_threads": [],
        "abort_reducer_version": 1,
        "abort_retry_threads": [],
        "unrelated_ledger_metadata": {"preserve": ["plugin", "data"]},
        "cursors": {
            "thread-a": {
                "path": fixture.rollout.to_string_lossy(),
                "offset": fs::metadata(&fixture.rollout).unwrap().len()
            }
        },
        "alerts": [{
            "id": "thread-a:2026-10-14T05:59:00Z",
            "thread_id": "thread-a",
            "project": "demo",
            "title": "Collector fixture",
            "progress_at": "2026-10-14T05:59:00Z",
            "handled_at": null,
            "state": "pending",
            "watch_notified": true,
            "outcome": "waiting",
            "reason": "text_required_action",
            "turn_id": "turn-old",
            "last_reply": "Approve the deployment?",
            "unrelated_alert_metadata": {"preserve": true}
        }],
        "turn_signals": {}
    }))
    .unwrap();
    fs::write(&source, &source_bytes).unwrap();

    let mut collector = new_collector(&fixture.paths);
    let snapshot = collector.poll().unwrap();
    assert_eq!(snapshot.codex["codex_alert_pending"], 1);
    assert!(snapshot.notifications.is_empty());
    // Classifier v3 -> v4 reconciliation increments the durable revision once,
    // even though this old pending alert remains delivered and unchanged.
    assert_eq!(snapshot.codex["codex_alert_revision"], 78);
    assert_eq!(fs::read(&source).unwrap(), source_bytes);
    let imported: Value =
        serde_json::from_slice(&fs::read(fixture.paths.state.join("codex-alerts.json")).unwrap())
            .unwrap();
    assert_eq!(imported["alerts"][0]["id"], "thread-a:2026-10-14T05:59:00Z");
    assert_eq!(imported["alerts"][0]["watch_notified"], true);
    assert_eq!(imported["classifier_version"], 4);
    assert_eq!(imported["revision"], 78);
    assert_eq!(
        imported["unrelated_ledger_metadata"],
        json!({"preserve": ["plugin", "data"]})
    );
    assert_eq!(
        imported["alerts"][0]["unrelated_alert_metadata"]["preserve"],
        true
    );
    collector.close();
}

#[test]
fn invalid_legacy_ledger_is_left_untouched_while_collector_uses_clean_local_state() {
    let fixture = fixture();
    let source = fixture.paths.codex.join(".stopwatch_alerts.json");
    let source_bytes = br#"{"version":99,"alerts":[],"cursors":{}}"#;
    fs::write(&source, source_bytes).unwrap();
    let catalog = PricingCatalog::new(
        fixture.paths.state.join("price-cache.json"),
        fixture.paths.pricing_override.clone(),
    )
    .unwrap();

    let mut collector = Collector::new(fixture.paths.clone(), Arc::new(catalog)).unwrap();
    assert_eq!(collector.poll().unwrap().codex["codex_alert_pending"], 0);
    assert_eq!(fs::read(&source).unwrap(), source_bytes);
    let local: Value =
        serde_json::from_slice(&fs::read(fixture.paths.state.join("codex-alerts.json")).unwrap())
            .unwrap();
    assert_eq!(local["version"], 3);
    collector.close();
}

#[test]
fn legacy_pending_alert_with_completed_outcome_migrates_as_completed_once() {
    let fixture = fixture();
    let source = fixture.paths.codex.join(".stopwatch_alerts.json");
    let progress_at = Local::now().to_rfc3339();
    let source_bytes = serde_json::to_vec(&json!({
        "version": 3,
        "classifier_version": 4,
        "reply_metadata_version": 2,
        "abort_reducer_version": 1,
        "revision": 18,
        "cursors": {
            "thread-a": {
                "path": fixture.rollout.to_string_lossy(),
                "offset": fs::metadata(&fixture.rollout).unwrap().len()
            }
        },
        "alerts": [{
            "id": format!("thread-a:{progress_at}"),
            "thread_id": "thread-a",
            "project": "demo",
            "title": "Completed legacy task",
            "progress_at": progress_at,
            "handled_at": null,
            "state": "pending",
            "watch_notified": true,
            "outcome": "completed",
            "reason": "completion_only",
            "turn_id": "turn-done",
            "last_reply": "The task is complete."
        }],
        "turn_signals": {}
    }))
    .unwrap();
    fs::write(&source, &source_bytes).unwrap();

    let mut collector = new_collector(&fixture.paths);
    let snapshot = collector.poll().unwrap();
    assert_eq!(snapshot.codex["codex_alert_pending"], 0);
    assert_eq!(snapshot.codex["codex_alert_completed"], 1);
    assert!(snapshot.notifications.is_empty());
    assert_eq!(fs::read(&source).unwrap(), source_bytes);
    let local: Value =
        serde_json::from_slice(&fs::read(fixture.paths.state.join("codex-alerts.json")).unwrap())
            .unwrap();
    assert_eq!(local["alerts"][0]["state"], "completed");
    assert_eq!(local["alerts"][0]["outcome"], "completed");
    assert_eq!(local["alerts"][0]["watch_notified"], true);
    collector.close();

    let mut restarted = new_collector(&fixture.paths);
    let after_restart = restarted.poll().unwrap();
    assert!(after_restart.notifications.is_empty());
    restarted.close();
}

#[test]
fn pending_survives_midnight_but_a_later_yesterday_completion_does_not_resurrect_it() {
    let fixture = fixture();
    let pending_time = yesterday_at(23, 58);
    let completion_time = yesterday_at(23, 59);
    let pending = json!({
        "id": format!("thread-a:{pending_time}"),
        "thread_id": "thread-a",
        "project": "demo",
        "title": "Waiting task",
        "progress_at": pending_time,
        "handled_at": null,
        "state": "pending",
        "watch_notified": true,
        "outcome": "waiting",
        "reason": "text_required_action",
        "turn_id": "turn-a",
        "last_reply": "Please confirm."
    });
    write_alert_ledger(&fixture, vec![pending.clone()], 9);
    let mut collector = new_collector(&fixture.paths);
    let across_midnight = collector.poll().unwrap();
    assert_eq!(across_midnight.codex["codex_alert_pending"], 1);
    assert_eq!(across_midnight.notifications, Vec::<String>::new());
    collector.close();

    let mut completed = pending;
    completed["id"] = json!(format!("thread-a:{completion_time}"));
    completed["progress_at"] = json!(completion_time);
    completed["handled_at"] = json!(completion_time);
    completed["state"] = json!("completed");
    completed["outcome"] = json!("completed");
    completed["watch_notified"] = json!(true);
    write_alert_ledger(
        &fixture,
        vec![
            json!({
                "id": format!("thread-a:{pending_time}"),
                "thread_id": "thread-a",
                "project": "demo",
                "title": "Waiting task",
                "progress_at": pending_time,
                "handled_at": null,
                "state": "pending",
                "watch_notified": true,
                "outcome": "waiting",
                "reason": "text_required_action",
                "turn_id": "turn-a",
                "last_reply": "Please confirm."
            }),
            completed,
        ],
        10,
    );

    let mut restarted = new_collector(&fixture.paths);
    let next_day = restarted.poll().unwrap();
    assert_eq!(next_day.codex["codex_alert_pending"], 0);
    assert_eq!(next_day.codex["codex_alert_completed"], 0);
    assert!(
        next_day.codex["codex_alerts"]
            .as_array()
            .unwrap()
            .is_empty()
    );
    restarted.close();
}

#[test]
fn running_alert_survives_midnight_until_its_turn_finishes() {
    let fixture = fixture();
    let progress = yesterday_at(23, 58);
    write_alert_ledger(
        &fixture,
        vec![json!({
            "id": format!("thread-a:{progress}"),
            "thread_id": "thread-a",
            "project": "demo",
            "title": "Long-running task",
            "progress_at": progress,
            "handled_at": null,
            "state": "running",
            "watch_notified": true,
            "outcome": "running",
            "reason": "",
            "turn_id": "turn-a",
            "last_reply": "Working."
        })],
        12,
    );

    let mut collector = new_collector(&fixture.paths);
    let snapshot = collector.poll().unwrap();
    assert_eq!(snapshot.codex["codex_alert_running"], 1);
    assert_eq!(snapshot.codex["codex_alerts"][0]["state"], "running");
    assert!(snapshot.notifications.is_empty());
    collector.close();
}

#[test]
fn corrupt_alert_ledger_is_preserved_before_a_fresh_baseline_is_written() {
    let fixture = fixture();
    let ledger = fixture.paths.state.join("codex-alerts.json");
    fs::write(&ledger, b"{broken json").unwrap();

    let mut collector = new_collector(&fixture.paths);
    assert_eq!(collector.poll().unwrap().codex["codex_alert_pending"], 0);
    collector.close();

    let preserved = fs::read_dir(&fixture.paths.state)
        .unwrap()
        .flatten()
        .any(|entry| {
            entry
                .file_name()
                .to_string_lossy()
                .starts_with("codex-alerts.json.corrupt-")
        });
    assert!(
        preserved,
        "the invalid ledger should be retained for recovery"
    );
    assert!(serde_json::from_slice::<Value>(&fs::read(ledger).unwrap()).is_ok());
}

#[test]
fn structured_plan_and_input_calls_reduce_to_waiting_without_exposing_reply() {
    let fixture = fixture();
    let mut collector = new_collector(&fixture.paths);
    collector.poll().unwrap();
    append_event(
        &fixture.rollout,
        json!({
            "timestamp": "2026-10-14T06:10:00Z",
            "type": "event_msg",
            "payload": {"type":"task_started","turn_id":"turn-plan"}
        }),
    );
    append_event(
        &fixture.rollout,
        json!({
            "timestamp": "2026-10-14T06:10:00.100Z",
            "type":"response_item",
            "payload": {
                "type":"function_call",
                "name":"update_plan",
                "arguments":"{\"plan\":[{\"status\":\"in_progress\"}]}"
            }
        }),
    );
    append_event(
        &fixture.rollout,
        json!({
            "timestamp": "2026-10-14T06:10:01Z",
            "type":"event_msg",
            "payload": {
                "type":"task_complete",
                "turn_id":"turn-plan",
                "last_agent_message":"No reply is required."
            }
        }),
    );
    append_event(
        &fixture.rollout,
        json!({
            "timestamp": "2026-10-14T06:11:00Z",
            "type": "event_msg",
            "payload": {"type":"task_started","turn_id":"turn-input"}
        }),
    );
    append_event(
        &fixture.rollout,
        json!({
            "timestamp": "2026-10-14T06:11:00.100Z",
            "type":"response_item",
            "payload": {
                "type":"function_call",
                "name":"request_user_input",
                "call_id":"input-call"
            }
        }),
    );
    append_event(
        &fixture.rollout,
        json!({
            "timestamp": "2026-10-14T06:11:01Z",
            "type":"event_msg",
            "payload": {
                "type":"task_complete",
                "turn_id":"turn-input",
                "last_agent_message":"No reply is required."
            }
        }),
    );

    let snapshot = collector.poll().unwrap();
    assert_eq!(snapshot.codex["codex_alert_pending"], 1);
    assert_eq!(snapshot.codex["codex_alerts"][0]["outcome"], "waiting");
    assert_eq!(snapshot.codex["codex_alerts"][0]["turn_id"], "turn-input");
    let ledger: Value =
        serde_json::from_slice(&fs::read(fixture.paths.state.join("codex-alerts.json")).unwrap())
            .unwrap();
    let reasons: Vec<&str> = ledger["alerts"]
        .as_array()
        .unwrap()
        .iter()
        .filter_map(|alert| alert["reason"].as_str())
        .collect();
    assert!(reasons.contains(&"structured_unfinished_plan"));
    assert!(reasons.contains(&"structured_input_request"));
    assert!(
        !serde_json::to_string(&snapshot.codex)
            .unwrap()
            .contains("No reply is required.")
    );
    collector.close();
}

#[test]
fn late_abort_cannot_retire_a_newer_turn_and_abort_is_durable() {
    let fixture = fixture();
    let mut collector = new_collector(&fixture.paths);
    collector.poll().unwrap();
    for event in [
        json!({"timestamp":"2026-10-14T06:20:00Z","type":"event_msg","payload":{"type":"task_started","turn_id":"turn-a"}}),
        json!({"timestamp":"2026-10-14T06:21:00Z","type":"event_msg","payload":{"type":"task_started","turn_id":"turn-b"}}),
        json!({"timestamp":"2026-10-14T06:21:30Z","type":"event_msg","payload":{"type":"turn_aborted","turn_id":"turn-a"}}),
    ] {
        append_event(&fixture.rollout, event);
    }

    let newer = collector.poll().unwrap();
    assert_eq!(newer.codex["codex_alert_running"], 1);
    assert_eq!(newer.codex["codex_alerts"][0]["turn_id"], "turn-b");
    append_event(
        &fixture.rollout,
        json!({
            "timestamp":"2026-10-14T06:22:00Z",
            "type":"event_msg",
            "payload":{"type":"turn_aborted","turn_id":"turn-b"}
        }),
    );
    let completed = collector.poll().unwrap();
    assert_eq!(completed.codex["codex_alert_running"], 0);
    assert_eq!(completed.codex["codex_alert_completed"], 1);
    assert!(completed.notifications.is_empty());
    collector.close();

    let mut restarted = new_collector(&fixture.paths);
    let after_restart = restarted.poll().unwrap();
    assert_eq!(after_restart.codex["codex_alert_completed"], 1);
    assert_eq!(
        after_restart.codex["codex_alert_revision"],
        completed.codex["codex_alert_revision"]
    );
    restarted.close();
}
