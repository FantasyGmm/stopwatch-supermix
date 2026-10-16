use anyhow::{Context, Result, bail};
use std::{path::PathBuf, sync::Arc};
use stopwatch_codex_agent::{
    contracts::{AgentPaths, Desktop, StatusSnapshot},
    pet::{PetFrame, encode_package, parse_package},
    pricing::PricingCatalog,
};

#[cfg(target_os = "macos")]
mod logging;

fn option(args: &[String], name: &str) -> Option<PathBuf> {
    args.iter()
        .position(|a| a == name)
        .and_then(|i| args.get(i + 1))
        .map(PathBuf::from)
}

fn self_test() -> Result<()> {
    // No HOME lookup, Keychain, Bluetooth, Accessibility, audio, or Codex process.
    let package = encode_package(
        vec![0],
        vec![PetFrame {
            duration_ms: 120,
            indices: vec![0; 144 * 156],
        }],
    )?;
    anyhow::ensure!(
        parse_package(&package.encoded)?.digest == package.digest,
        "pet format self-test failed"
    );
    let snapshot = StatusSnapshot {
        codex: serde_json::json!({"codex_model":"fixture-model","codex_effort":"high","codex_5h_left_pct":null,"codex_week_left_pct":null,"codex_alerts":[]}),
        telemetry: serde_json::json!({"telemetry_v":1,"usage":{"s":"unavailable","q":"unavailable","at":0,"qa":0,"h":null,"w":null,"hr":null,"wr":null,"t":null,"c":null,"cs":"unavailable","day":"2026-10-14"}}),
        ..StatusSnapshot::default()
    };
    for (_, bytes) in stopwatch_codex_agent::wire::status_messages(&snapshot)? {
        anyhow::ensure!(bytes.len() <= 2048, "status size self-test failed");
    }
    println!("StopWatch Codex Agent Rust self-test: OK");
    Ok(())
}

fn main() -> Result<()> {
    let args: Vec<_> = std::env::args().skip(1).collect();
    if args.iter().any(|a| a == "--self-test") {
        return self_test();
    }
    if args.iter().any(|a| a == "--version") {
        println!("{}", include_str!("../../mac-codex-agent/VERSION").trim());
        return Ok(());
    }
    if args.iter().any(|a| a == "--help") {
        println!(
            "StopWatch Codex Agent (Rust)\n  --self-test\n  --version\n  --collect-once --codex-root DIR --state-dir DIR [--trae-root DIR]\n  --convert-pet INPUT.gif OUTPUT.cxp\n  No arguments: manually open the native macOS menu bar Agent."
        );
        return Ok(());
    }
    if let Some(i) = args.iter().position(|a| a == "--convert-pet") {
        let input = args.get(i + 1).context("missing GIF input")?;
        let output = args.get(i + 2).context("missing pet output")?;
        let package = stopwatch_codex_agent::pet::convert_gif(&PathBuf::from(input))?;
        std::fs::write(output, &package.encoded)?;
        println!(
            "{} bytes sha256 {}",
            package.encoded.len(),
            hex::encode(package.digest)
        );
        return Ok(());
    }
    if args.iter().any(|a| a == "--collect-once") {
        // Diagnostic collection requires explicit paths and cannot accidentally
        // fall through into the user's real credentials, settings, or UI.
        let codex =
            option(&args, "--codex-root").context("--collect-once requires --codex-root")?;
        let state = option(&args, "--state-dir").context("--collect-once requires --state-dir")?;
        let paths = AgentPaths {
            codex,
            trae: option(&args, "--trae-root"),
            state: state.clone(),
            pricing_override: None,
        };
        let pricing = Arc::new(PricingCatalog::new(
            state.join("models-dev-cache.json"),
            None,
        )?);
        let mut collector = stopwatch_codex_agent::collector::Collector::new(paths, pricing)?;
        let snapshot = collector.poll()?;
        collector.close();
        let messages = stopwatch_codex_agent::wire::status_messages(&snapshot)?;
        let values: Vec<serde_json::Value> = messages
            .iter()
            .map(|(_, bytes)| serde_json::from_slice(bytes))
            .collect::<std::result::Result<_, _>>()?;
        println!("{}", serde_json::to_string_pretty(&values)?);
        return Ok(());
    }
    if !args.is_empty() {
        bail!("unknown arguments; use --help")
    }
    #[cfg(target_os = "macos")]
    {
        // Initialize persistent logging before touching production paths or
        // starting any native/runtime components. CLI and fixture modes above
        // deliberately never resolve HOME or open the production log.
        logging::initialize()
            .context("could not initialize ~/Library/Logs/stopwatch-codex-agent.log")?;
        tracing_subscriber::fmt()
            .with_env_filter(
                tracing_subscriber::EnvFilter::try_from_default_env()
                    .unwrap_or_else(|_| "info".into()),
            )
            .with_ansi(false)
            .with_writer(std::io::stderr)
            .try_init()
            .map_err(|error| {
                anyhow::anyhow!("could not initialize persistent tracing subscriber: {error}")
            })?;
        let version = include_str!("../../mac-codex-agent/VERSION").trim();
        tracing::info!(version = version, stage = "launch", "StopWatch Codex Agent starting");
        std::panic::set_hook(Box::new(|_| {
            tracing::error!(
                stage = "panic",
                outcome = "failure",
                "StopWatch Codex Agent panicked"
            );
        }));
        let result = run_macos();
        match result {
            Ok(()) => {
                tracing::info!(stage = "exit", outcome = "success", "StopWatch Codex Agent exited");
                Ok(())
            }
            Err(error) => {
                let error_chain = format!("{error:#}");
                tracing::error!(
                    stage = "exit",
                    outcome = "failure",
                    error = %error_chain,
                    "StopWatch Codex Agent failed"
                );
                Err(error).context("StopWatch Codex Agent failed; see persistent log")
            }
        }
    }
    #[cfg(not(target_os = "macos"))]
    {
        bail!("Native Agent UI requires macOS; use --self-test or explicit fixture collection")
    }
}

#[cfg(target_os = "macos")]
fn run_macos() -> Result<()> {
    tracing::info!(stage = "paths", "Resolving production paths");
    let home = PathBuf::from(std::env::var_os("HOME").context("HOME unavailable")?);
    let state = home.join("Library/Application Support/StopWatch Codex Agent");
    let override_path = std::env::var_os("STOPWATCH_PRICING_JSON").map(PathBuf::from);
    let paths = AgentPaths {
        codex: home.join(".codex"),
        trae: Some(home.join("Library/Application Support/Trae")),
        state: state.clone(),
        pricing_override: override_path.clone(),
    };
    tracing::info!(stage = "pricing", "Initializing pricing catalog");
    let pricing = Arc::new(PricingCatalog::new(
        state.join("models-dev-cache.json"),
        override_path,
    )?);
    let (commands_tx, commands_rx) = tokio::sync::mpsc::channel(32);
    tracing::info!(stage = "desktop", "Initializing native desktop integration");
    let desktop = Arc::new(stopwatch_codex_agent::desktop::MacDesktop::new(
        commands_tx,
        paths.clone(),
    )?);
    tracing::info!(stage = "preferences", "Loading native preferences");
    let preferences = desktop.preferences();
    tracing::info!(stage = "runtime", "Starting agent runtime");
    let handle = stopwatch_codex_agent::runtime::start(
        paths,
        pricing,
        desktop.clone(),
        commands_rx,
        preferences,
        std::env::var("STOPWATCH_CLAUDE_USAGE_TOKEN").ok(),
        std::env::var_os("STOPWATCH_OPENCODE_USAGE_JSON").map(PathBuf::from),
        Some(home.join(".trae-cn")),
    )?;
    tracing::info!(stage = "ui", "Starting native menu bar UI");
    let ui_result = desktop.run_ui();
    if ui_result.is_ok() {
        tracing::info!(stage = "ui", outcome = "success", "Native menu bar UI stopped");
    } else {
        tracing::error!(stage = "ui", outcome = "failure", "Native menu bar UI failed");
    }
    tracing::info!(stage = "shutdown", "Stopping agent runtime");
    let _ = desktop.voice_stop(true);
    let shutdown_result = handle.shutdown();
    if shutdown_result.is_ok() {
        tracing::info!(stage = "shutdown", outcome = "success", "Agent runtime stopped");
    } else {
        tracing::error!(stage = "shutdown", outcome = "failure", "Agent runtime shutdown failed");
    }
    ui_result?;
    shutdown_result?;
    Ok(())
}
