use std::{
    fs,
    path::Path,
    time::{SystemTime, UNIX_EPOCH},
};

#[cfg(target_os = "macos")]
use std::{sync::mpsc, time::Duration};

use serde_json::{Value, json};
use stopwatch_codex_agent::pricing::PricingCatalog;

fn fixtures() -> Value {
    serde_json::from_str(include_str!("fixtures/pricing-compat.json")).unwrap()
}

fn write_fixture(path: &Path, fixture: &Value) {
    fs::write(path, serde_json::to_vec(fixture).unwrap()).unwrap();
}

#[test]
fn cc_switch_requires_explicit_usd_and_unknown_models_remain_unpriced() {
    for fixture_name in ["missing_currency_override", "unknown_currency_override"] {
        let dir = tempfile::tempdir().unwrap();
        let override_path = dir.path().join("override.json");
        write_fixture(&override_path, &fixtures()[fixture_name]);

        let catalog =
            PricingCatalog::new(dir.path().join("cache.json"), Some(override_path)).unwrap();
        assert!(
            catalog
                .rates("pricing-compat-invalid-currency", 1)
                .is_none()
        );
        assert!(
            catalog
                .rates("unknown-model-without-source-price", 1)
                .is_none()
        );
        assert!(catalog.rates("gpt-5", 1).is_some());
    }

    let dir = tempfile::tempdir().unwrap();
    let cache = dir.path().join("cache.json");
    write_fixture(&cache, &fixtures()["unknown_currency_cache"]);
    let catalog = PricingCatalog::new(cache, None).unwrap();
    assert!(
        catalog
            .rates("pricing-compat-invalid-currency", 1)
            .is_none()
    );
    assert!(catalog.rates("gpt-5", 1).is_some());

    let dir = tempfile::tempdir().unwrap();
    let override_path = dir.path().join("override.json");
    write_fixture(&override_path, &fixtures()["usd_override"]);
    let catalog = PricingCatalog::new(dir.path().join("cache.json"), Some(override_path)).unwrap();
    assert_eq!(
        catalog
            .rates("pricing-compat-valid-override", 1)
            .unwrap()
            .input,
        9.0
    );
}

#[test]
fn malformed_declared_tiers_reject_refresh_and_keep_last_good_cache() {
    let dir = tempfile::tempdir().unwrap();
    let cache = dir.path().join("cache.json");
    let catalog = PricingCatalog::new(cache.clone(), None).unwrap();
    catalog
        .install_public_json(&serde_json::to_vec(&fixtures()["valid_public"]).unwrap())
        .unwrap();
    assert_eq!(
        catalog
            .rates("pricing-compat-lastgood", 272001)
            .unwrap()
            .input,
        4.0
    );

    for fixture_name in ["invalid_context_type", "invalid_context_data"] {
        let invalid = serde_json::to_vec(&fixtures()[fixture_name]).unwrap();
        assert!(catalog.install_public_json(&invalid).is_err());
        assert_eq!(
            catalog
                .rates("pricing-compat-lastgood", 272001)
                .unwrap()
                .input,
            4.0
        );
        assert_eq!(
            PricingCatalog::new(cache.clone(), None)
                .unwrap()
                .rates("pricing-compat-lastgood", 272001)
                .unwrap()
                .input,
            4.0
        );
    }
}

#[test]
fn future_cache_timestamp_is_stale_and_does_not_extend_refresh_window() {
    let dir = tempfile::tempdir().unwrap();
    let cache = dir.path().join("cache.json");
    write_fixture(&cache, &fixtures()["future_cache"]);

    let catalog = PricingCatalog::new(cache, None).unwrap();
    let now = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap()
        .as_secs();
    assert!(catalog.checked_at() <= now);
    assert!(
        catalog
            .rates("pricing-compat-future-cache", 1)
            .unwrap()
            .partial
    );
}

#[cfg(target_os = "macos")]
#[test]
fn fifo_cache_and_override_do_not_block_startup() {
    use std::{ffi::CString, os::unix::ffi::OsStrExt, path::PathBuf, thread};

    fn create_fifo(path: &Path) {
        let path = CString::new(path.as_os_str().as_bytes()).unwrap();
        assert_eq!(unsafe { libc::mkfifo(path.as_ptr(), 0o600) }, 0);
    }

    let dir = tempfile::tempdir().unwrap();
    let cache_fifo = dir.path().join("pricing-cache-fifo");
    let override_fifo = dir.path().join("pricing-override-fifo");
    create_fifo(&cache_fifo);
    create_fifo(&override_fifo);

    let (sender, receiver) = mpsc::channel();
    let cache_fifo: PathBuf = cache_fifo;
    let override_fifo: PathBuf = override_fifo;
    thread::spawn(move || {
        let started = PricingCatalog::new(cache_fifo, Some(override_fifo)).is_ok();
        let _ = sender.send(started);
    });
    assert!(receiver.recv_timeout(Duration::from_secs(2)).unwrap());
}

#[test]
fn public_refresh_without_context_tiers_keeps_the_declared_model_price() {
    let public =
        json!({"openai":{"models":{"pricing-compat-no-tier":{"cost":{"input":2,"output":8}}}}});
    let dir = tempfile::tempdir().unwrap();
    let catalog = PricingCatalog::new(dir.path().join("cache.json"), None).unwrap();
    catalog
        .install_public_json(&serde_json::to_vec(&public).unwrap())
        .unwrap();
    assert_eq!(
        catalog.rates("pricing-compat-no-tier", 1).unwrap().input,
        2.0
    );
}
