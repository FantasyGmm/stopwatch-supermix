fn main() {
    let target_os = std::env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    if target_os != "macos" {
        return;
    }

    for path in [
        "native/macos_accessibility.h",
        "native/macos_accessibility.mm",
        "native/macos_inventory.h",
        "native/macos_inventory.mm",
        "native/macos_ui.h",
        "native/macos_ui.mm",
        "../mac-codex-agent/native/voice_audio_bridge.h",
        "../mac-codex-agent/native/voice_audio_bridge.mm",
        "../mac-codex-agent/native/voice_audio_ring.h",
        "../mac-codex-agent/native/voice_audio_queue_lifecycle.h",
    ] {
        println!("cargo:rerun-if-changed={path}");
    }

    cc::Build::new()
        .cpp(true)
        .flag("-std=c++17")
        .flag("-xobjective-c++")
        .flag("-fobjc-arc")
        .flag("-fvisibility=hidden")
        .include("native")
        .include("../mac-codex-agent/native")
        .file("native/macos_accessibility.mm")
        .file("native/macos_inventory.mm")
        .file("native/macos_ui.mm")
        // Reuse the existing Core Audio C ABI and ring implementation. The
        // legacy source stays intact as the migration reference.
        .file("../mac-codex-agent/native/voice_audio_bridge.mm")
        .compile("stopwatch_macos_native");

    for framework in [
        "AppKit",
        "ApplicationServices",
        "CoreGraphics",
        "AudioToolbox",
        "CoreAudio",
        "CoreFoundation",
        "CoreServices",
    ] {
        println!("cargo:rustc-link-lib=framework={framework}");
    }
}
