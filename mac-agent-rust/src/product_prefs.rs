//! Read-only composer settings. Return only the two input-policy enums.
//! These names/defaults and legacy fallback were checked against the installed
//! com.openai.codex app's settings registry and keyboard handler on 2026-10-14.
use serde_json::{Value, json};
use std::{fs::File, io::Read, path::Path};

const MAX_CONFIG_BYTES: u64 = 4 * 1024 * 1024;

fn bounded_text(path: &Path) -> Option<String> {
    let f = File::open(path).ok()?;
    if f.metadata().ok()?.len() > MAX_CONFIG_BYTES { return None }
    let mut s = String::new(); f.take(MAX_CONFIG_BYTES + 1).read_to_string(&mut s).ok()?;
    (s.len() as u64 <= MAX_CONFIG_BYTES).then_some(s)
}

fn valid_enter(s: &str) -> Option<&str> { matches!(s, "enter" | "cmdIfMultiline" | "cmdAlways").then_some(s) }
fn valid_follow_up(s: &str) -> Option<&str> { matches!(s, "queue" | "steer" | "interrupt").then_some(s) }

pub fn read(codex_root: &Path) -> Value {
    let path = codex_root.join("config.toml");
    let mut enter = None;
    let mut follow = None;
    if path.exists() {
        let Some(text) = bounded_text(&path) else { return json!({"composer_enter_behavior":null,"follow_up_queue_mode":null}) };
        let Ok(config) = text.parse::<toml::Value>() else { return json!({"composer_enter_behavior":null,"follow_up_queue_mode":null}) };
        if let Some(desktop) = config.get("desktop").and_then(toml::Value::as_table) {
            enter = desktop.get("composerEnterBehavior").and_then(toml::Value::as_str).and_then(valid_enter).map(str::to_owned);
            follow = desktop.get("followUpQueueMode").and_then(toml::Value::as_str).and_then(valid_follow_up).map(str::to_owned);
            // Explicit invalid values must not silently acquire defaults.
            if desktop.contains_key("composerEnterBehavior") && enter.is_none() { return json!({"composer_enter_behavior":null,"follow_up_queue_mode":null}) }
            if desktop.contains_key("followUpQueueMode") && follow.is_none() { return json!({"composer_enter_behavior":null,"follow_up_queue_mode":null}) }
        }
    }
    if enter.is_none() || follow.is_none() {
        let global_path = codex_root.join(".codex-global-state.json");
        if let Some(text) = bounded_text(&global_path) {
            if let Ok(global) = serde_json::from_str::<Value>(&text) {
                if enter.is_none() {
                    enter = global.pointer("/electron-persisted-atom-state/enter-behavior").and_then(Value::as_str).and_then(valid_enter).map(str::to_owned);
                }
                if follow.is_none() {
                    follow = global.get("electron-initial-follow-up-queue-mode").and_then(Value::as_str).and_then(valid_follow_up).map(str::to_owned);
                }
            }
        }
    }
    json!({"composer_enter_behavior":enter.unwrap_or_else(||"enter".into()),"follow_up_queue_mode":follow.unwrap_or_else(||"steer".into())})
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn read_only_precedence_legacy_and_secret_filtering() {
        let d = tempfile::tempdir().unwrap();
        let config = b"api_key='fixture-secret'\n[desktop]\ncomposerEnterBehavior='cmdAlways'\nfollowUpQueueMode='queue'\n";
        std::fs::write(d.path().join("config.toml"), config).unwrap();
        std::fs::write(d.path().join(".codex-global-state.json"), br#"{"electron-persisted-atom-state":{"enter-behavior":"enter"},"cookie":"fixture-cookie"}"#).unwrap();
        assert_eq!(read(d.path()), json!({"composer_enter_behavior":"cmdAlways","follow_up_queue_mode":"queue"}));
        assert_eq!(std::fs::read(d.path().join("config.toml")).unwrap(), config);
        assert!(!read(d.path()).to_string().contains("fixture"));
        std::fs::write(d.path().join("config.toml"), "[desktop]\ncomposerEnterBehavior='unsupported'").unwrap();
        assert!(read(d.path())["composer_enter_behavior"].is_null());
    }
}
