import os
import pathlib
import shlex
import subprocess
import tempfile
import textwrap
import time
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
BLACKHOLE_URL = (
    "https://existential.audio/downloads/BlackHole2ch-0.7.1.pkg"
)
BLACKHOLE_SHA256 = (
    "57b540f27a3e29c37e310e01bee0fdfab76733087e47f997ef9dccf851400dcf"
)
INSTALL_SCRIPT = ROOT / "install_launch_agent.sh"


class InstallScriptTests(unittest.TestCase):
    def read_script(self):
        return INSTALL_SCRIPT.read_text(encoding="utf-8")

    def run_installer_function(self, body, *, env=None):
        script = self.read_script()
        self.assertIn("STOPWATCH_INSTALLER_SOURCE_ONLY", script)
        command = textwrap.dedent(
            f"""
            export STOPWATCH_INSTALLER_SOURCE_ONLY=1
            source {shlex.quote(str(INSTALL_SCRIPT))}
            {body}
            """
        )
        return subprocess.run(
            ["/bin/bash", "-c", command],
            capture_output=True,
            text=True,
            check=False,
            env={**os.environ, **(env or {})},
        )

    def test_installer_does_not_change_agent_or_connection_state(self):
        script = self.read_script()

        for forbidden in (
            "launchctl",
            "Library/LaunchAgents",
            "<key>KeepAlive</key>",
            "<key>RunAtLoad</key>",
            "open -W",
            "open \"$APP_DIR\"",
            "defaults write",
            "connectionEnabled",
        ):
            self.assertNotIn(forbidden, script)

    def test_installer_packages_the_menu_application_modules(self):
        script = self.read_script()

        for module in (
            "sitecustomize.py",
            "menu_app.py",
            "runtime.py",
            "app_state.py",
            "agent.py",
            "agent_identity.py",
            "codex_adapter.py",
            "trae_tasks.py",
            "trae_adapter.py",
            "combined_collector.py",
            "completion_classifier.py",
            "codex_turn_evidence.py",
            "task_alerts.py",
            "protocol.py",
            "pairing_client.py",
            "secure_pairing_crypto.py",
            "secure_pairing_protocol.py",
            "secure_pairing_wire.py",
            "watch_credentials.py",
            "trae_tasks.py",
            "trae_adapter.py",
            "combined_collector.py",
        ):
            self.assertIn(module, script)
        self.assertIn("STOPWATCH_CODEX_APP", script)
        self.assertIn("import Security", script)
        self.assertNotIn('open -W -n "$APP_DIR"', script)

    def test_packaging_preserves_existing_product_identity(self):
        script = (ROOT / "install_launch_agent.sh").read_text(encoding="utf-8")
        agent = (ROOT / "agent.py").read_text(encoding="utf-8")

        self.assertIn('LABEL="com.stopwatch.codex-agent"', script)
        self.assertIn("StopWatch Codex Agent.app", script)
        self.assertIn("<string>StopWatch Codex Agent</string>", script)
        self.assertIn('DEVICE_NAME = "StopWatch-Codex"', agent)
        self.assertIn(
            'SERVICE_UUID = "7a230001-5474-4f50-5741-544348434f44"',
            agent,
        )

    def test_installer_packages_every_production_voice_module(self):
        script = self.read_script()

        for module in (
            "voice_driver.py",
            "voice_shortcut.py",
            "voice_audio.py",
            "voice_input_crypto.py",
            "voice_input_protocol.py",
            "voice_session.py",
            "typeless_transcripts.py",
            "codex_app_server.py",
            "task_voice_submitter.py",
            "task_routes.py",
            "ima_adpcm.py",
        ):
            self.assertIn(module, script)

    def test_installer_builds_and_packages_the_native_audio_bridge(self):
        script = self.read_script()

        self.assertIn('NATIVE_BUILD_DIR="$ROOT_DIR/.native"', script)
        self.assertIn(
            'NATIVE_LIBRARY="$NATIVE_BUILD_DIR/libstopwatch_voice_audio.dylib"',
            script,
        )
        self.assertIn('mkdir -p "$NATIVE_BUILD_DIR"', script)
        self.assertIn("xcrun clang++ -std=c++17 -O2 -dynamiclib", script)
        self.assertIn("-arch arm64", script)
        self.assertIn("-arch x86_64", script)
        self.assertIn(
            "-install_name '@rpath/libstopwatch_voice_audio.dylib'", script
        )
        self.assertIn("-fvisibility=hidden", script)
        self.assertIn("-framework AudioToolbox", script)
        self.assertIn("-framework CoreAudio", script)
        self.assertIn("-framework CoreFoundation", script)
        self.assertIn('"$ROOT_DIR/native/voice_audio_bridge.mm"', script)
        self.assertIn('-o "$NATIVE_LIBRARY"', script)
        self.assertIn('mkdir -p "$STAGING_RESOURCES/native"', script)
        self.assertIn(
            'cp "$NATIVE_LIBRARY" '
            '"$STAGING_RESOURCES/native/libstopwatch_voice_audio.dylib"',
            script,
        )
        self.assertIn('validate_universal_macho_tree "$app_path" || return', script)
        self.assertIn('lipo -archs "$macho_path"', script)

    def test_installer_rebuilds_pillow_as_universal2(self):
        script = self.read_script()

        self.assertIn(
            '\'import importlib.metadata; '
            'print(importlib.metadata.version("Pillow"))\'',
            script,
        )
        self.assertIn('ARCHFLAGS="-arch arm64 -arch x86_64"', script)
        self.assertIn("--no-binary=Pillow", script)
        self.assertIn('"Pillow==$PILLOW_VERSION"', script)

    def test_installer_caches_and_packages_the_pinned_driver_without_installing_it(self):
        script = self.read_script()

        self.assertIn('DRIVER_CACHE="$ROOT_DIR/.driver-cache"', script)
        self.assertIn(
            'DRIVER_PACKAGE="$DRIVER_CACHE/BlackHole2ch-0.7.1.pkg"',
            script,
        )
        self.assertIn(BLACKHOLE_URL, script)
        self.assertIn(BLACKHOLE_SHA256, script)
        self.assertIn("curl --fail --location --proto '=https' --tlsv1.2", script)
        self.assertIn("Contents/Resources/Drivers", script)
        self.assertIn('DRIVER_LOCK="$DRIVER_CACHE/.blackhole-package.lock"', script)
        self.assertIn('DRIVER_TEMP="$(mktemp', script)
        self.assertIn("acquire_driver_lock", script)
        self.assertIn("release_driver_lock", script)
        self.assertIn("cleanup_driver_cache_state", script)
        self.assertIn("handle_signal", script)
        self.assertIn('rm -f "$DRIVER_PACKAGE"', script)

        verify_before_copy = script.index('ensure_driver_package')
        copy_to_bundle = script.index(
            'cp "$DRIVER_PACKAGE" "$STAGING_DRIVERS/"'
        )
        verify_bundle = script.index(
            'verify_driver_package "$STAGING_DRIVERS/$DRIVER_PACKAGE_NAME"'
        )
        self.assertLess(verify_before_copy, copy_to_bundle)
        self.assertLess(copy_to_bundle, verify_bundle)

        for forbidden in (
            "brew",
            "/usr/sbin/installer",
            "installer -pkg",
            "osascript",
            "sudo",
        ):
            self.assertNotIn(forbidden, script)

    def test_installer_never_provisions_keys_and_verifies_code_signature(self):
        script = self.read_script()

        self.assertIn(
            "Production voice keys are created and rotated by the watch registry",
            script,
        )
        for forbidden in (
            "openssl rand",
            "/dev/urandom",
            "security add-generic-password",
            "voice_input_crypto.h",
            "xxd -i",
            "STOPWATCH_VOICE_KEY_FILE",
            "voice_input_device_key.h",
        ):
            self.assertNotIn(forbidden, script)
        self.assertIn(
            'codesign --verify --deep --strict --verbose=2 "$STAGING_APP"',
            script,
        )
        self.assertIn(
            'CODE_REQUIREMENT=\'=designated => identifier "com.stopwatch.codex-agent"\'',
            script,
        )
        self.assertIn(
            'codesign --force --deep --sign - "$STAGING_APP"',
            script,
        )
        self.assertIn(
            'codesign --force --sign - --requirements \\\n'
            '        "$CODE_REQUIREMENT" "$STAGING_APP"',
            script,
        )

    def test_installer_builds_and_validates_a_clean_staging_app_before_publish(self):
        script = self.read_script()

        self.assertIn(
            'BUILD_ROOT="$(mktemp -d "$APP_PARENT/.stopwatch-agent-build.XXXXXX")"',
            script,
        )
        self.assertIn('STAGING_APP="$BUILD_ROOT/StopWatch Codex Agent.app"', script)
        self.assertIn('BACKUP_ROOT="$(mktemp -d "$APP_PARENT/.stopwatch-agent-backup.XXXXXX")"', script)
        self.assertIn('validate_staged_app "$STAGING_APP"', script)
        self.assertIn(
            'publish_validated_app "$STAGING_APP" "$APP_DIR" "$BACKUP_ROOT"',
            script,
        )
        self.assertNotIn('mkdir -p "$APP_DIR/', script)
        self.assertNotIn('cp "$NATIVE_LIBRARY" "$APP_RESOURCES/', script)

        validate_index = script.rindex('validate_staged_app "$STAGING_APP"')
        publish_index = script.rindex(
            'publish_validated_app "$STAGING_APP" "$APP_DIR" "$BACKUP_ROOT"'
        )
        self.assertLess(validate_index, publish_index)

    def test_atomic_publish_replaces_the_whole_bundle_without_stale_resources(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            app = root / "StopWatch Codex Agent.app"
            staging = root / "staging.app"
            backup = root / "backup"
            (app / "Contents" / "Resources").mkdir(parents=True)
            (app / "Contents" / "Resources" / "stale-key.h").write_text(
                "stale", encoding="utf-8"
            )
            (staging / "Contents" / "Resources").mkdir(parents=True)
            (staging / "Contents" / "Resources" / "fresh.txt").write_text(
                "fresh", encoding="utf-8"
            )

            result = self.run_installer_function(
                f"""
                validate_staged_app() {{ return 0; }}
                validate_published_app() {{ return 0; }}
                publish_validated_app \
                    {shlex.quote(str(staging))} \
                    {shlex.quote(str(app))} \
                    {shlex.quote(str(backup))}
                """
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue((app / "Contents" / "Resources" / "fresh.txt").is_file())
            self.assertFalse((app / "Contents" / "Resources" / "stale-key.h").exists())
            self.assertFalse(backup.exists())

    def test_injected_pre_publish_validation_failure_leaves_prior_app_intact(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            app = root / "StopWatch Codex Agent.app"
            staging = root / "staging.app"
            backup = root / "backup"
            app.mkdir()
            staging.mkdir()
            (app / "prior.txt").write_text("prior", encoding="utf-8")
            (staging / "new.txt").write_text("new", encoding="utf-8")

            result = self.run_installer_function(
                f"""
                validate_staged_app() {{ return 73; }}
                validate_published_app() {{ return 0; }}
                if publish_validated_app \
                    {shlex.quote(str(staging))} \
                    {shlex.quote(str(app))} \
                    {shlex.quote(str(backup))}; then
                    exit 99
                fi
                """
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((app / "prior.txt").read_text(encoding="utf-8"), "prior")
            self.assertTrue((staging / "new.txt").is_file())
            self.assertFalse(backup.exists())

    def test_failed_final_validation_rolls_back_prior_app(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            app = root / "StopWatch Codex Agent.app"
            staging = root / "staging.app"
            backup = root / "backup"
            app.mkdir()
            staging.mkdir()
            (app / "prior.txt").write_text("prior", encoding="utf-8")
            (staging / "new.txt").write_text("new", encoding="utf-8")

            result = self.run_installer_function(
                f"""
                validate_staged_app() {{ return 0; }}
                validate_published_app() {{ return 74; }}
                if publish_validated_app \
                    {shlex.quote(str(staging))} \
                    {shlex.quote(str(app))} \
                    {shlex.quote(str(backup))}; then
                    exit 99
                fi
                """
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((app / "prior.txt").read_text(encoding="utf-8"), "prior")
            self.assertFalse((app / "new.txt").exists())
            self.assertFalse(backup.exists())

    def test_signal_after_moving_prior_app_restores_it_before_cleanup(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            app = root / "StopWatch Codex Agent.app"
            staging = root / "staging.app"
            backup = root / "backup"
            app.mkdir()
            staging.mkdir()
            (app / "prior.txt").write_text("prior", encoding="utf-8")
            (staging / "new.txt").write_text("new", encoding="utf-8")

            result = self.run_installer_function(
                f"""
                validate_staged_app() {{ return 0; }}
                validate_published_app() {{ return 0; }}
                mv() {{
                    command mv "$@"
                    if [[ "$1" == {shlex.quote(str(app))} ]] &&
                        [[ "$2" == {shlex.quote(str(backup / app.name))} ]]; then
                        kill -TERM $$
                    fi
                }}
                BACKUP_ROOT={shlex.quote(str(backup))}
                trap cleanup EXIT
                trap 'handle_signal 143' TERM
                publish_validated_app \
                    {shlex.quote(str(staging))} \
                    {shlex.quote(str(app))} \
                    {shlex.quote(str(backup))}
                """
            )

            self.assertEqual(result.returncode, 143, result.stderr)
            self.assertEqual((app / "prior.txt").read_text(encoding="utf-8"), "prior")
            self.assertFalse((app / "new.txt").exists())
            self.assertFalse(backup.exists())

    def test_concurrent_publish_transactions_serialize_and_failed_one_rolls_back(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            app = root / "StopWatch Codex Agent.app"
            first_staging = root / "first-staging.app"
            second_staging = root / "second-staging.app"
            first_backup = root / "first-backup"
            second_backup = root / "second-backup"
            first_validating = root / "first-validating"
            second_validating = root / "second-validating"
            release_first = root / "release-first"
            publish_lock = root / "publish.lock"
            app.mkdir()
            first_staging.mkdir()
            second_staging.mkdir()
            (app / "version.txt").write_text("original", encoding="utf-8")
            (first_staging / "version.txt").write_text("first", encoding="utf-8")
            (second_staging / "version.txt").write_text("second", encoding="utf-8")

            first_command = textwrap.dedent(
                f"""
                export STOPWATCH_INSTALLER_SOURCE_ONLY=1
                source {shlex.quote(str(INSTALL_SCRIPT))}
                PUBLISH_LOCK={shlex.quote(str(publish_lock))}
                BACKUP_ROOT={shlex.quote(str(first_backup))}
                validate_staged_app() {{ return 0; }}
                validate_published_app() {{
                    [[ "$(cat "$1/version.txt")" == first ]] || return 75
                    touch {shlex.quote(str(first_validating))}
                    while [[ ! -e {shlex.quote(str(release_first))} ]]; do
                        sleep 0.02
                    done
                    [[ "$(cat "$1/version.txt")" == first ]]
                }}
                trap cleanup EXIT
                publish_validated_app \
                    {shlex.quote(str(first_staging))} \
                    {shlex.quote(str(app))} \
                    {shlex.quote(str(first_backup))}
                """
            )
            second_command = textwrap.dedent(
                f"""
                export STOPWATCH_INSTALLER_SOURCE_ONLY=1
                source {shlex.quote(str(INSTALL_SCRIPT))}
                PUBLISH_LOCK={shlex.quote(str(publish_lock))}
                BACKUP_ROOT={shlex.quote(str(second_backup))}
                validate_staged_app() {{ return 0; }}
                validate_published_app() {{
                    touch {shlex.quote(str(second_validating))}
                    return 76
                }}
                trap cleanup EXIT
                if publish_validated_app \
                    {shlex.quote(str(second_staging))} \
                    {shlex.quote(str(app))} \
                    {shlex.quote(str(second_backup))}; then
                    exit 99
                fi
                """
            )

            first = subprocess.Popen(
                ["/bin/bash", "-c", first_command],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            deadline = time.monotonic() + 5
            while not first_validating.exists() and time.monotonic() < deadline:
                time.sleep(0.02)
            self.assertTrue(first_validating.exists(), "first publisher did not pause")

            second = subprocess.Popen(
                ["/bin/bash", "-c", second_command],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            time.sleep(0.3)
            was_serialized = second.poll() is None and not second_validating.exists()
            release_first.touch()
            first_output = first.communicate(timeout=10)
            second_output = second.communicate(timeout=10)

            self.assertTrue(was_serialized, second_output)
            self.assertEqual(first.returncode, 0, first_output)
            self.assertEqual(second.returncode, 0, second_output)
            self.assertEqual((app / "version.txt").read_text(encoding="utf-8"), "first")
            self.assertEqual(list(app.rglob("*.app")), [])
            self.assertFalse(first_backup.exists())
            self.assertFalse(second_backup.exists())

    def test_key_scan_allows_public_ca_bundle_but_rejects_private_key_marker(self):
        with tempfile.TemporaryDirectory() as temporary:
            app = pathlib.Path(temporary) / "Agent.app"
            (app / "Contents" / "Resources" / "app").mkdir(parents=True)
            certifi = app / "Contents" / "Resources" / "site-packages" / "certifi"
            certifi.mkdir(parents=True)
            (app / "Contents" / "MacOS").mkdir(parents=True)
            (app / "Contents" / "Info.plist").write_text(
                "<plist></plist>", encoding="utf-8"
            )
            (certifi / "cacert.pem").write_text(
                "-----BEGIN CERTIFICATE-----\npublic-ca\n"
                "-----END CERTIFICATE-----\n",
                encoding="utf-8",
            )
            (certifi / "font-data.py").write_text(
                "YAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAD/\n",
                encoding="utf-8",
            )

            allowed = self.run_installer_function(
                f"validate_no_key_material {shlex.quote(str(app))}"
            )
            self.assertEqual(allowed.returncode, 0, allowed.stderr)

            (app / "Contents" / "Resources" / "app" / "bad.txt").write_text(
                "000102030405060708090a0b0c0d0e0f\n",
                encoding="utf-8",
            )
            rejected = self.run_installer_function(
                f"validate_no_key_material {shlex.quote(str(app))}"
            )
            self.assertNotEqual(rejected.returncode, 0)

    def test_key_scan_ignores_vendor_base64_and_url_hashes_but_rejects_hex_key(self):
        with tempfile.TemporaryDirectory() as temporary:
            app = pathlib.Path(temporary) / "Agent.app"
            resources = app / "Contents" / "Resources"
            (resources / "app").mkdir(parents=True)
            (app / "Contents" / "MacOS").mkdir(parents=True)
            (app / "Contents" / "Info.plist").write_text(
                "<plist></plist>", encoding="utf-8"
            )
            (resources / "vendor.py").write_text(
                "embedded_font = 'YAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAD/'\n"
                "# https://gist.github.com/example/f51579ae8d93c8657a5564aefc2ffbca\n",
                encoding="utf-8",
            )

            allowed = self.run_installer_function(
                f"validate_no_key_material {shlex.quote(str(app))}"
            )
            self.assertEqual(allowed.returncode, 0, allowed.stderr)

            (resources / "app" / "bad.txt").write_text(
                "voice_key=00112233445566778899aabbccddeeff\n",
                encoding="utf-8",
            )
            rejected = self.run_installer_function(
                f"validate_no_key_material {shlex.quote(str(app))}"
            )
            self.assertNotEqual(rejected.returncode, 0)

    def test_validation_stops_immediately_after_a_failed_check(self):
        with tempfile.TemporaryDirectory() as temporary:
            marker = pathlib.Path(temporary) / "continued"
            result = self.run_installer_function(
                f"""
                validate_app_contents() {{ return 72; }}
                validate_universal_macho_tree() {{ touch {shlex.quote(str(marker))}; }}
                codesign() {{ touch {shlex.quote(str(marker))}; }}
                STAGING_APP=/tmp/Agent.app
                if validate_staged_app /tmp/Agent.app; then
                    exit 99
                fi
                """
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse(marker.exists())

    def test_corrupt_cache_is_redownloaded_once_under_a_concurrent_lock(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            cache = root / "cache"
            cache.mkdir()
            package = cache / "BlackHole2ch-0.7.1.pkg"
            package.write_text("corrupt\n", encoding="utf-8")
            count = root / "downloads.txt"

            body = f"""
                DRIVER_CACHE={shlex.quote(str(cache))}
                DRIVER_PACKAGE_NAME=BlackHole2ch-0.7.1.pkg
                DRIVER_PACKAGE="$DRIVER_CACHE/$DRIVER_PACKAGE_NAME"
                DRIVER_LOCK="$DRIVER_CACHE/.blackhole-package.lock"
                DRIVER_TEMP=
                DRIVER_LOCK_OWNED=0
                verify_driver_package() {{
                    grep -qx 'valid-package' "$1"
                }}
                curl() {{
                    local output=
                    while [[ $# -gt 0 ]]; do
                        if [[ "$1" == "--output" ]]; then
                            output="$2"
                            shift 2
                        else
                            shift
                        fi
                    done
                    printf 'download\n' >> {shlex.quote(str(count))}
                    sleep 0.25
                    printf 'valid-package\n' > "$output"
                }}
                ensure_driver_package
            """
            command = textwrap.dedent(
                f"""
                export STOPWATCH_INSTALLER_SOURCE_ONLY=1
                source {shlex.quote(str(INSTALL_SCRIPT))}
                {body}
                """
            )
            self.assertIn("STOPWATCH_INSTALLER_SOURCE_ONLY", self.read_script())
            first = subprocess.Popen(
                ["/bin/bash", "-c", command],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            second = subprocess.Popen(
                ["/bin/bash", "-c", command],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            first_output = first.communicate(timeout=10)
            second_output = second.communicate(timeout=10)

            self.assertEqual(first.returncode, 0, first_output)
            self.assertEqual(second.returncode, 0, second_output)
            self.assertEqual(package.read_text(encoding="utf-8"), "valid-package\n")
            self.assertEqual(count.read_text(encoding="utf-8").splitlines(), ["download"])
            self.assertFalse((cache / ".blackhole-package.lock").exists())
            self.assertEqual(list(cache.glob(".BlackHole2ch-0.7.1.pkg.*")), [])

    def test_runtime_is_self_contained_and_smoked_with_the_packaged_executable(self):
        script = self.read_script()

        self.assertIn(
            'STAGING_EXECUTABLE="$STAGING_APP/Contents/MacOS/'
            'StopWatchCodexAgent"',
            script,
        )
        self.assertIn(
            'cp "$PYTHON_APP_EXECUTABLE" "$STAGING_EXECUTABLE"', script
        )
        self.assertIn("install_name_tool", script)
        self.assertIn("'@executable_path/../../../../Python3'", script)
        self.assertIn(
            "'@executable_path/../Frameworks/Python3.framework/Versions/'"
            '"$PYTHON_VERSION"\'/Python3\'',
            script,
        )
        self.assertNotIn("write_launcher()", script)
        self.assertIn(
            'STAGING_PYTHON_HOME="$STAGING_PYTHON_FRAMEWORK/Versions/'
            '$PYTHON_VERSION"',
            script,
        )
        self.assertIn(
            'PYTHON_FRAMEWORK_DIR="$(cd "$PYTHON_FRAMEWORK_ROOT/../.." && pwd)"',
            script,
        )
        self.assertIn(
            'STAGING_PYTHON_FRAMEWORK="$STAGING_APP/Contents/Frameworks/'
            'Python3.framework"',
            script,
        )
        self.assertIn(
            'ditto "$PYTHON_FRAMEWORK_DIR/" "$STAGING_PYTHON_FRAMEWORK/"',
            script,
        )
        self.assertIn(
            'rm -rf "$STAGING_PYTHON_HOME/lib/python$PYTHON_VERSION/site-packages"',
            script,
        )
        self.assertIn(
            'STAGING_SITE_PACKAGES="$STAGING_RESOURCES/site-packages"', script
        )
        self.assertIn('ditto "$SITE_PACKAGES/" "$STAGING_SITE_PACKAGES/"', script)
        self.assertIn("--no-binary=cffi", script)
        self.assertIn('find "$STAGING_APP" -name \'*.pth\'', script)
        self.assertIn("validate_no_external_symlinks", script)
        self.assertIn("env -i", script)
        self.assertIn("PYTHONNOUSERSITE=1", script)
        self.assertIn("<key>PYTHONNOUSERSITE</key>", script)
        self.assertIn('STOPWATCH_CODEX_APP=0', script)
        self.assertIn(
            'resolved = pathlib.Path(entry or os.getcwd()).resolve()', script
        )
        self.assertNotIn("if not entry:\n        continue", script)
        for dependency in (
            "AppKit",
            "ApplicationServices",
            "Quartz",
            "bleak",
            "certifi",
            "cryptography",
        ):
            self.assertIn(f"import {dependency}", script)

        self.assertIn(
            '<string>$APP_DIR/Contents/Frameworks/Python3.framework/'
            'Versions/$PYTHON_VERSION</string>',
            script,
        )
        self.assertIn(
            '<string>$APP_DIR/Contents/Resources/app:'
            '$APP_DIR/Contents/Resources/site-packages</string>',
            script,
        )
        self.assertNotIn('<string>$PYTHON_FRAMEWORK_ROOT</string>', script)
        self.assertNotIn('<string>$SITE_PACKAGES</string>', script)

        for validation in (
            'validate_no_external_symlinks "$app_path" || return',
            'validate_no_key_material "$app_path" || return',
            'validate_info_plist_paths "$app_path" || return',
            'smoke_test_packaged_runtime "$app_path" || return',
            'validate_app_contents "$app_path" || return',
            'validate_universal_macho_tree "$app_path" || return',
        ):
            self.assertIn(validation, script)

    def test_architecture_scan_is_limited_to_macho_candidates_and_not_duplicated(self):
        script = self.read_script()

        self.assertIn(
            'find "$app_path" -type f \\( -perm -111 -o -name \'*.so\' -o '
            '-name \'*.dylib\' -o -name \'*.bundle\' \\) -print0',
            script,
        )
        build_body = script.split("build_staged_app() {", 1)[1].split(
            "\n}\n\nmain()", 1
        )[0]
        self.assertNotIn("validate_universal_macho_tree", build_body)
        self.assertNotIn("validate_no_key_material", build_body)
        self.assertNotIn("smoke_test_packaged_runtime", build_body)

if __name__ == "__main__":
    unittest.main()
