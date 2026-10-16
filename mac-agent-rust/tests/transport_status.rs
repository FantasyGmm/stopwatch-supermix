use stopwatch_codex_agent::{contracts::StatusSnapshot, wire::{EVENT_MESSAGE, STATUS_MESSAGE, status_messages}};
use serde_json::{Value, json};

fn snapshot(codex: Value, trae: Value, telemetry: Value) -> StatusSnapshot {
    StatusSnapshot { codex, trae, telemetry, ..StatusSnapshot::default() }
}

#[test]
fn status_projection_is_public_whitelisted_and_keeps_trae_separate() {
    let value = snapshot(
        json!({
            "codex_model": "model🚀".repeat(30),
            "codex_effort": "high",
            "codex_alert_pending": true,
            "codex_alert_revision": 12,
            "_ui_private": "do not send",
            "auth": "private token",
            "codex_alerts": [
                {"route":"0123456789abcdef", "project":"P", "title":"T", "state":"pending", "reply":"private reply"},
                {"route":"ffffffffffffffff", "project":"P", "title":"T", "state":"completed"},
                {"route":"not-a-route", "project":"P", "title":"T", "state":"running"}
            ]
        }),
        json!({
            "trae_online": true,
            "trae_process_state": "live",
            "trae_revision": 99,
            "trae_pending_count": 1,
            "trae_tasks": [{"id":"task-1", "project":"demo", "title":"正在运行", "detail":"安全细节🚀".repeat(60), "time":"12:34", "state":"running", "model":"m", "effort":"h", "session_id":"private-session"}],
            "_ui_private": "not public"
        }),
        json!({
            "telemetry_v": 1,
            "usage": {"s":"partial", "q":"ok", "at": 1791940000, "qa":null, "h":75, "w":null, "hr":null, "wr":null, "t":1234, "c":1.25, "cs":"partial", "day":"2026-10-14", "auth":"private"},
            "host": {"s":"ok", "at":1791940000, "cpu":12.5, "mem":20.0, "disk":30.0, "up":1.0, "down":2.0},
            "private_key":"never send"
        }),
    );

    let messages = status_messages(&value).unwrap();
    assert_eq!(messages.len(), 3);
    assert!(messages.iter().all(|(kind, _)| *kind == STATUS_MESSAGE));
    assert_ne!(EVENT_MESSAGE, messages[0].0);
    for (_, bytes) in &messages { assert!(bytes.len() <= 2048); }
    assert!(messages[2].1.len() <= 1024);

    let codex: Value = serde_json::from_slice(&messages[0].1).unwrap();
    assert_eq!(codex["codex_alert_pending"], 0, "booleans are not integer counters");
    assert_eq!(codex["codex_alerts"].as_array().unwrap().len(), 1);
    assert_eq!(codex["codex_alerts"][0]["route"], "0123456789abcdef");
    assert!(codex.get("_ui_private").is_none());
    assert!(codex.get("auth").is_none());
    assert!(!String::from_utf8_lossy(&messages[0].1).contains("private reply"));

    let telemetry: Value = serde_json::from_slice(&messages[1].1).unwrap();
    assert_eq!(telemetry["telemetry_v"], 1);
    assert_eq!(telemetry["usage"]["s"], "stale");
    assert!(telemetry.get("private_key").is_none());
    assert!(telemetry["usage"].get("auth").is_none());

    let trae: Value = serde_json::from_slice(&messages[2].1).unwrap();
    assert_eq!(trae["type"], "status");
    assert_eq!(trae["source"], "trae");
    assert_eq!(trae["trae_tasks"][0]["state"], "running");
    assert!(!trae["trae_tasks_truncated"].as_bool().unwrap());
    assert!(trae.get("_ui_private").is_none());
    assert!(String::from_utf8_lossy(&messages[2].1).contains("安全细节"));
    assert!(!String::from_utf8_lossy(&messages[2].1).contains("private-session"));
}

#[test]
fn status_messages_compact_large_unicode_collections_to_the_legacy_caps() {
    let alerts = (0..8).map(|n| json!({
        "route": format!("{n:016x}"), "project":"项目🚀".repeat(20),
        "title":"任务标题🚀".repeat(30), "state":"running"
    })).collect::<Vec<_>>();
    let tasks = (0..8).map(|n| json!({
        "id": format!("task-{n}"), "project":"项目🚀".repeat(20),
        "title":"任务标题🚀".repeat(30), "detail":"状态🎙️".repeat(200),
        "time":"12:34", "state":"pending", "model":"模型🚀".repeat(20), "effort":"effort🚀".repeat(20)
    })).collect::<Vec<_>>();
    let value = snapshot(
        json!({"codex_alerts": alerts, "codex_alert_revision": 1}),
        json!({"trae_tasks": tasks, "trae_pending_count": 70000, "trae_running_count": false, "trae_revision": 999999999999_u64}),
        Value::Null,
    );
    let messages = status_messages(&value).unwrap();
    assert!(messages[0].1.len() <= 2048);
    assert!(messages[2].1.len() <= 1024);
    let trae: Value = serde_json::from_slice(&messages[2].1).unwrap();
    assert_eq!(trae["trae_pending_count"], 65535);
    assert_eq!(trae["trae_running_count"], 0);
    assert_eq!(trae["trae_revision"], i32::MAX);
    assert!(trae["trae_tasks"].as_array().unwrap().len() <= 4);
}

#[test]
fn status_fragmentation_keeps_legacy_six_byte_headers_and_byte_bounds() {
    let messages = status_messages(&snapshot(Value::Null, Value::Null, Value::Null)).unwrap();
    for (sequence, (kind, payload)) in messages.iter().enumerate() {
        let fragments = stopwatch_codex_agent::wire::encode_status_message(payload, *kind, sequence as u16 + 1, 20).unwrap();
        assert!(fragments.iter().all(|fragment| fragment.len() <= 20));
        assert!(fragments.iter().all(|fragment| fragment.len() >= 7));
        assert!(fragments.iter().all(|fragment| fragment[0..2] == [1, STATUS_MESSAGE]));
        assert!(fragments.iter().all(|fragment| u16::from_le_bytes([fragment[2], fragment[3]]) == sequence as u16 + 1));
    }
}
