use std::{fs, path::PathBuf, sync::Arc};

use chrono::{Duration, Utc};
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
    let historic = serde_json::to_vec(&json!({
        "timestamp": "2020-01-01T12:00:00Z",
        "type": "event_msg",
        "payload": {"type": "task_started", "turn_id": "historic"}
    }))
    .unwrap();
    fs::write(&rollout, [historic.as_slice(), b"\n"].concat()).unwrap();

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
                "Collector compatibility fixture",
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

#[test]
fn full_reply_scan_finds_a_middle_question_and_keeps_private_detail_bounded() {
    let fixture = fixture();
    let mut collector = new_collector(&fixture.paths);
    collector.poll().unwrap();
    let started_at = Utc::now();
    let completed_at = started_at + Duration::seconds(1);
    append_event(
        &fixture.rollout,
        json!({
            "timestamp": started_at.to_rfc3339(),
            "type": "event_msg",
            "payload": {"type": "task_started", "turn_id": "long-turn"}
        }),
    );
    let reply = format!(
        "{}Which option should I use?{}",
        "Work is complete. ".repeat(3_000),
        " The implementation is complete. ".repeat(3_000)
    );
    assert!(reply.len() > 64 * 1024);
    append_event(
        &fixture.rollout,
        json!({
            "timestamp": completed_at.to_rfc3339(),
            "type": "event_msg",
            "payload": {
                "type": "task_complete",
                "turn_id": "long-turn",
                "last_agent_message": reply
            }
        }),
    );

    let snapshot = collector.poll().unwrap();
    assert_eq!(snapshot.codex["codex_alert_pending"], 1);
    assert_eq!(snapshot.codex["codex_alerts"][0]["outcome"], "waiting");
    let ledger: Value =
        serde_json::from_slice(&fs::read(fixture.paths.state.join("codex-alerts.json")).unwrap())
            .unwrap();
    assert_eq!(ledger["alerts"][0]["reason"], "text_choice_handoff");
    assert!(
        snapshot
            .details
            .values()
            .all(|detail| detail.reply.len() <= 12 * 1024)
    );
    collector.close();
}

#[test]
fn legacy_trae_state_is_copied_into_agent_state_without_touching_source_files() {
    let fixture = fixture();
    let legacy = fixture._dir.path().join("trae-cn");
    fs::create_dir_all(&legacy).unwrap();
    let cursor_source = legacy.join(".stopwatch_cursor.json");
    let ledger_source = legacy.join(".stopwatch_tasks.json");
    let cursor_bytes = serde_json::to_vec(&json!({
        "version": 2,
        "cursors": {
            "/tmp/trae-agent.log": {
                "path": "/tmp/trae-agent.log",
                "inode": 123,
                "offset": 4567,
                "mtime_ns": 987654321,
                "model": "Friendly Model",
                "effort": "high",
                "model_updated_at": 1234.5
            }
        }
    }))
    .unwrap();
    let ledger_bytes = serde_json::to_vec(&json!({
        "version": 4,
        "model": "Friendly Model",
        "effort": "high",
        "model_updated_at": 1234.5,
        "revision": 77,
        "tasks": [{
            "session_id": "session-a",
            "task_id": "task-a",
            "project": "StopWatch",
            "title": "Legacy task",
            "detail": "Waiting for approval",
            "state": "pending",
            "updated_at": 1234.5,
            "model": "Friendly Model",
            "effort": "high"
        }]
    }))
    .unwrap();
    fs::write(&cursor_source, &cursor_bytes).unwrap();
    fs::write(&ledger_source, &ledger_bytes).unwrap();

    let mut collector =
        new_collector(&fixture.paths).with_legacy_trae_state_dir(Some(legacy.clone()));
    let cursor_copy: Value =
        serde_json::from_slice(&fs::read(fixture.paths.state.join("trae-cursors.json")).unwrap())
            .unwrap();
    let ledger_copy: Value =
        serde_json::from_slice(&fs::read(fixture.paths.state.join("trae-tasks.json")).unwrap())
            .unwrap();
    assert_eq!(cursor_copy["version"], 2);
    assert_eq!(
        cursor_copy["cursors"]["/tmp/trae-agent.log"]["offset"],
        4567
    );
    assert_eq!(ledger_copy["revision"], 77);
    assert_eq!(ledger_copy["tasks"][0]["state"], "pending");
    assert_eq!(fs::read(cursor_source).unwrap(), cursor_bytes);
    assert_eq!(fs::read(ledger_source).unwrap(), ledger_bytes);
    collector.close();
}

#[test]
fn existing_agent_owned_trae_state_wins_over_legacy_state() {
    let fixture = fixture();
    let legacy = fixture._dir.path().join("trae-cn");
    fs::create_dir_all(&legacy).unwrap();
    fs::write(
        legacy.join(".stopwatch_tasks.json"),
        br#"{"version":4,"revision":77,"tasks":[]}"#,
    )
    .unwrap();
    let local_path = fixture.paths.state.join("trae-tasks.json");
    fs::write(&local_path, br#"{"version":4,"revision":12,"tasks":[]}"#).unwrap();

    let mut collector = new_collector(&fixture.paths).with_legacy_trae_state_dir(Some(legacy));
    let local: Value = serde_json::from_slice(&fs::read(local_path).unwrap()).unwrap();
    assert_eq!(local["revision"], 12);
    collector.close();
}

#[test]
fn notification_ack_retry_persists_after_the_first_local_write_fails() {
    let fixture = fixture();
    let mut collector = new_collector(&fixture.paths);
    collector.poll().unwrap();
    let started_at = Utc::now();
    append_event(
        &fixture.rollout,
        json!({
            "timestamp": started_at.to_rfc3339(),
            "type": "event_msg",
            "payload": {"type": "task_started", "turn_id": "ack-turn"}
        }),
    );
    append_event(
        &fixture.rollout,
        json!({
            "timestamp": (started_at + Duration::seconds(1)).to_rfc3339(),
            "type": "event_msg",
            "payload": {
                "type": "task_complete",
                "turn_id": "ack-turn",
                "last_agent_message": "Please confirm deployment."
            }
        }),
    );
    let pending = collector.poll().unwrap();
    assert_eq!(pending.notifications.len(), 1);

    let temporary_path = fixture.paths.state.join("codex-alerts.json.tmp");
    fs::create_dir(&temporary_path).unwrap();
    assert!(collector.mark_notified(&pending.notifications).is_err());
    assert!(collector.poll().unwrap().notifications.is_empty());
    fs::remove_dir(temporary_path).unwrap();
    collector.mark_notified(&pending.notifications).unwrap();
    let ledger: Value =
        serde_json::from_slice(&fs::read(fixture.paths.state.join("codex-alerts.json")).unwrap())
            .unwrap();
    assert_eq!(ledger["alerts"][0]["watch_notified"], true);
    collector.close();
}
