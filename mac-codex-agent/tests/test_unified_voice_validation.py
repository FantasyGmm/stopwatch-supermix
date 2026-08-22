import json
import os
import pathlib
import re
import shlex
import subprocess
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
RUNNER = ROOT / "scripts/run_unified_offline_validation.sh"
README = ROOT / "mac-codex-agent/README.md"
PROJECT_README = ROOT / "README.md"
VALIDATION = ROOT / "VALIDATION.md"
PROBE_DOC = ROOT / "docs/ble-voice-link-probe.md"

EXPECTED_HOST_TESTS = (
    "assistant_state_machine_test",
    "assistant_presentation_model_test",
    "assistant_lifecycle_event_policy_test",
    "assistant_audio_level_test",
    "assistant_close_controller_test",
    "assistant_event_mailbox_test",
    "assistant_update_notice_test",
    "assistant_trace_test",
    "resource_supervisor_test",
    "button_feedback_policy_test",
    "shared_wifi_policy_test",
    "wifi_settings_model_test",
    "wifi_settings_controller_test",
    "debug_command_test",
    "runtime_diagnostics_test",
    "audio_jitter_buffer_policy_test",
    "assistant_output_sync_policy_test",
    "edge_control_logic_test",
    "low_battery_alert_logic_test",
    "status_message_router_test",
    "trae_status_model_test",
    "trae_task_carousel_test",
    "codex_status_model_test",
    "codex_button_gesture_test",
    "codex_task_detail_store_test",
    "codex_task_focus_deck_test",
    "codex_task_focus_layout_test",
    "status_page_swipe_logic_test",
    "trae_card_tap_logic_test",
    "display_protection_config_test",
    "display_protection_logic_test",
    "display_protection_diagnostics_test",
    "display_brightness_arbiter_test",
    "screen_off_runtime_policy_test",
    "external_power_state_test",
    "display_protection_settings_model_test",
    "button_input_gate_test",
    "touch_input_gate_test",
    "assistant_display_activity_test",
    "codex_pending_glow_logic_test",
    "codex_alert_carousel_test",
    "codex_status_ui_layout_test",
    "trae_status_ui_layout_test",
    "ble_voice_probe_protocol_test",
    "voice_probe_crypto_test",
    "secure_pairing_crypto_test",
    "secure_pairing_protocol_test",
    "agent_registry_test",
    "secure_pairing_session_test",
    "secure_pairing_wire_test",
    "agent_connection_router_test",
    "agent_panel_model_test",
    "agent_panel_ui_layout_test",
    "agent_panel_presenter_test",
    "voice_input_crypto_test",
    "codex_task_detail_crypto_test",
    "voice_input_protocol_test",
    "input_action_protocol_test",
    "input_action_controller_test",
    "voice_input_session_test",
    "voice_capture_pipeline_test",
    "codex_runtime_session_test",
    "pinned_microphone_controller_test",
    "voice_input_controller_test",
    "codex_status_priority_test",
    "ble_voice_probe_failure_transition_test",
    "ble_voice_probe_session_test",
    "ble_voice_probe_pcm_buffer_test",
    "ima_adpcm_codec_test",
    "voice_audio_ring_test",
)


class UnifiedVoiceValidationTests(unittest.TestCase):
    def run_contract(self):
        with tempfile.TemporaryDirectory() as temporary:
            sentinel_key = pathlib.Path(temporary) / "must-not-be-read.hex"
            result = subprocess.run(
                [str(RUNNER), "--contract"],
                capture_output=True,
                text=True,
                check=False,
                env={
                    **os.environ,
                    "PYTHON": "/definitely/missing/python",
                    "STOPWATCH_VOICE_KEY_FILE": str(sentinel_key),
                },
            )
            self.assertFalse(sentinel_key.exists())
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout.splitlines()

    def contract_host_rows(self):
        compile_rows = []
        run_names = []
        for line in self.run_contract():
            fields = line.split("\t")
            if fields[0] == "COMPILE":
                self.assertGreaterEqual(len(fields), 4)
                compile_rows.append((fields[1], fields[2:]))
            elif fields[0] == "RUN":
                self.assertEqual(len(fields), 2)
                run_names.append(fields[1])
        return compile_rows, run_names

    def run_behavioral_runner(self, runner=RUNNER):
        with tempfile.TemporaryDirectory() as temporary:
            temp = pathlib.Path(temporary)
            compiler_log = temp / "compiler.log"
            executable_log = temp / "executables.log"
            trace = temp / "runner.trace"
            fake_cxx = temp / "fake-cxx.py"
            fake_bin = temp / "bin"
            fake_bin.mkdir()
            fake_cmake = fake_bin / "cmake"
            fake_cmake.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
            fake_cmake.chmod(0o700)
            fake_cxx.write_text(
                """#!/usr/bin/env python3
import json
import os
import pathlib
import shlex
import sys

arguments = sys.argv[1:]
with pathlib.Path(os.environ[\"FAKE_CXX_LOG\"]).open(\"a\", encoding=\"utf-8\") as log:
    log.write(json.dumps(arguments) + \"\\n\")
output = pathlib.Path(arguments[arguments.index(\"-o\") + 1])
command = \"printf '%s\\n' \" + shlex.quote(output.name) + \" >> \" + shlex.quote(os.environ[\"FAKE_EXECUTABLE_LOG\"])
output.write_text(\"#!/bin/sh\\n\" + command + \"\\n\", encoding=\"utf-8\")
output.chmod(0o700)
""",
                encoding="utf-8",
            )
            fake_cxx.chmod(0o700)
            result = subprocess.run(
                [str(runner), "--host-tests-only"],
                capture_output=True,
                text=True,
                check=False,
                env={
                    **os.environ,
                    "PATH": f"{fake_bin}{os.pathsep}{os.environ['PATH']}",
                    "TMPDIR": str(temp),
                    "CXX": str(fake_cxx),
                    "PYTHON": sys.executable,
                    "IDF_PATH": str(temp / "fake-idf"),
                    "FAKE_CXX_LOG": str(compiler_log),
                    "FAKE_EXECUTABLE_LOG": str(executable_log),
                    "STOPWATCH_VALIDATION_TRACE_FILE": str(trace),
                },
            )
            trace_lines = trace.read_text(encoding="utf-8").splitlines() if trace.exists() else []
            compiler_lines = compiler_log.read_text(encoding="utf-8").splitlines() if compiler_log.exists() else []
            executable_lines = executable_log.read_text(encoding="utf-8").splitlines() if executable_log.exists() else []
            trace_mode = trace.stat().st_mode & 0o777 if trace.exists() else None
            temporary_host_dirs = list(temp.glob("stopwatch-unified-validation.*"))
        return result, trace_lines, compiler_lines, executable_lines, trace_mode, temporary_host_dirs

    def assert_behavioral_host_execution(self, runner=RUNNER):
        (
            result,
            trace_lines,
            compiler_lines,
            executable_lines,
            trace_mode,
            temporary_host_dirs,
        ) = self.run_behavioral_runner(runner)
        self.assertEqual(result.returncode, 0, result.stderr)
        expected_trace = [
            row
            for name in EXPECTED_HOST_TESTS
            for row in (f"COMPILED\t{name}", f"RAN\t{name}")
        ]
        self.assertEqual(trace_lines, expected_trace)
        self.assertEqual(
            [
                pathlib.Path(arguments[arguments.index("-o") + 1]).name
                for arguments in map(json.loads, compiler_lines)
            ],
            list(EXPECTED_HOST_TESTS),
        )
        self.assertEqual(executable_lines, list(EXPECTED_HOST_TESTS))
        self.assertEqual(trace_mode, 0o600)
        self.assertEqual(temporary_host_dirs, [])

    def test_stage_contract_has_consistent_numbering_and_final_gates(self):
        lines = self.run_contract()
        stages = []
        for line in lines:
            match = re.fullmatch(r"\[(\d+)/(\d+)\] (.+)", line)
            if match:
                stages.append((int(match.group(1)), int(match.group(2)), match.group(3)))

        self.assertGreater(len(stages), 0)
        total = stages[0][1]
        runner = RUNNER.read_text(encoding="utf-8")
        declared_total = re.search(r"^TOTAL_STAGES=(\d+)$", runner, re.MULTILINE)
        self.assertIsNotNone(declared_total)
        self.assertEqual(total, int(declared_total.group(1)))
        self.assertEqual(total, 63)
        self.assertEqual(len(stages), total)
        self.assertEqual([stage[0] for stage in stages], list(range(1, total + 1)))
        self.assertEqual({stage[1] for stage in stages}, {total})
        self.assertEqual(
            [stage[2] for stage in stages[-2:]],
            [
                "Isolated unified candidate clean build and capacity gates",
                "Whitespace error check",
            ],
        )

    def test_contract_executes_exact_voice_compile_and_run_pairs(self):
        compile_rows_list, run_names = self.contract_host_rows()
        compile_names = [name for name, _ in compile_rows_list]
        compile_rows = dict(compile_rows_list)
        self.assertEqual(compile_names, list(EXPECTED_HOST_TESTS))
        self.assertEqual(run_names, list(EXPECTED_HOST_TESTS))

        expected_sources = {
            "ble_voice_probe_protocol_test": {
                "tests/ble_voice_probe_protocol_test.cpp",
                "main/apps/app_codex_status/ble_voice_probe_protocol.cpp",
            },
            "voice_probe_crypto_test": {
                "tests/voice_probe_crypto_test.cpp",
                "main/apps/app_codex_status/voice_probe_crypto.cpp",
            },
            "secure_pairing_crypto_test": {
                "tests/secure_pairing_crypto_test.cpp",
                "main/apps/app_codex_status/secure_pairing_crypto.cpp",
            },
            "secure_pairing_protocol_test": {
                "tests/secure_pairing_protocol_test.cpp",
                "main/apps/app_codex_status/secure_pairing_protocol.cpp",
                "main/apps/app_codex_status/secure_pairing_crypto.cpp",
            },
            "agent_registry_test": {
                "tests/agent_registry_test.cpp",
                "main/apps/app_codex_status/agent_registry.cpp",
                "main/apps/app_codex_status/agent_registry_store.cpp",
                "main/apps/app_codex_status/secure_pairing_crypto.cpp",
            },
            "secure_pairing_session_test": {
                "tests/secure_pairing_session_test.cpp",
                "main/apps/app_codex_status/secure_pairing_session.cpp",
                "main/apps/app_codex_status/secure_pairing_protocol.cpp",
                "main/apps/app_codex_status/secure_pairing_crypto.cpp",
                "main/apps/app_codex_status/agent_registry.cpp",
                "main/apps/app_codex_status/agent_registry_store.cpp",
            },
            "secure_pairing_wire_test": {
                "tests/secure_pairing_wire_test.cpp",
                "main/apps/app_codex_status/secure_pairing_wire.cpp",
            },
            "agent_connection_router_test": {
                "tests/agent_connection_router_test.cpp",
                "main/apps/app_codex_status/agent_connection_router.cpp",
            },
            "agent_panel_model_test": {
                "tests/agent_panel_model_test.cpp",
                "main/apps/app_codex_status/agent_panel_model.cpp",
                "main/apps/app_codex_status/agent_registry.cpp",
                "main/apps/app_codex_status/secure_pairing_crypto.cpp",
            },
            "voice_input_crypto_test": {
                "tests/voice_input_crypto_test.cpp",
                "main/apps/app_codex_status/voice_input_crypto.cpp",
            },
            "voice_input_protocol_test": {
                "tests/voice_input_protocol_test.cpp",
                "main/apps/app_codex_status/voice_input_protocol.cpp",
                "main/apps/app_codex_status/voice_input_crypto.cpp",
            },
            "codex_button_gesture_test": {
                "tests/codex_button_gesture_test.cpp",
                "main/apps/app_codex_status/codex_button_gesture.cpp",
            },
            "input_action_protocol_test": {
                "tests/input_action_protocol_test.cpp",
                "main/apps/app_codex_status/input_action_protocol.cpp",
                "main/apps/app_codex_status/voice_input_crypto.cpp",
            },
            "input_action_controller_test": {
                "tests/input_action_controller_test.cpp",
                "main/apps/app_codex_status/input_action_controller.cpp",
                "main/apps/app_codex_status/input_action_protocol.cpp",
                "main/apps/app_codex_status/voice_input_crypto.cpp",
            },
            "voice_input_session_test": {
                "tests/voice_input_session_test.cpp",
                "main/apps/app_codex_status/voice_input_session.cpp",
            },
            "voice_capture_pipeline_test": {
                "tests/voice_capture_pipeline_test.cpp",
                "main/apps/app_codex_status/voice_capture_pipeline.cpp",
            },
            "codex_runtime_session_test": {
                "tests/codex_runtime_session_test.cpp",
            },
            "pinned_microphone_controller_test": {
                "tests/pinned_microphone_controller_test.cpp",
                "main/apps/app_codex_status/pinned_microphone_controller.cpp",
            },
            "voice_input_controller_test": {
                "tests/voice_input_controller_test.cpp",
                "main/apps/app_codex_status/voice_input_controller.cpp",
                "main/apps/app_codex_status/codex_button_gesture.cpp",
                "main/apps/app_codex_status/voice_capture_pipeline.cpp",
                "main/apps/app_codex_status/voice_input_session.cpp",
                "main/apps/app_codex_status/voice_input_protocol.cpp",
                "main/apps/app_codex_status/voice_input_crypto.cpp",
            },
            "codex_status_priority_test": {
                "tests/codex_status_priority_test.cpp",
            },
            "ble_voice_probe_failure_transition_test": {
                "tests/ble_voice_probe_failure_transition_test.cpp",
                "main/apps/app_codex_status/ble_voice_probe_failure_transition.cpp",
                "main/apps/app_codex_status/ble_voice_probe_session.cpp",
                "main/apps/app_codex_status/ble_voice_probe_protocol.cpp",
            },
            "ble_voice_probe_session_test": {
                "tests/ble_voice_probe_session_test.cpp",
                "main/apps/app_codex_status/ble_voice_probe_session.cpp",
            },
            "ble_voice_probe_pcm_buffer_test": {
                "tests/ble_voice_probe_pcm_buffer_test.cpp",
            },
            "ima_adpcm_codec_test": {
                "tests/ima_adpcm_codec_test.cpp",
                "main/apps/app_codex_status/ima_adpcm_codec.cpp",
            },
            "voice_audio_ring_test": {
                "mac-codex-agent/native/voice_audio_ring_test.cpp",
            },
        }

        for name, sources in expected_sources.items():
            self.assertIn(name, compile_rows)
            arguments = compile_rows[name]
            self.assertIn("-std=c++17", arguments)
            self.assertIn("-Wall", arguments)
            self.assertIn("-Wextra", arguments)
            self.assertIn("-Werror", arguments)
            self.assertEqual({argument for argument in arguments if argument.endswith(".cpp")}, sources)
            self.assertEqual(run_names.count(name), 1)

    def test_normal_host_path_records_actual_compile_and_run_success(self):
        self.assert_behavioral_host_execution()

    def test_echo_only_contract_output_cannot_satisfy_behavioral_execution(self):
        with tempfile.TemporaryDirectory() as temporary:
            fake_runner = pathlib.Path(temporary) / "echo-only-runner"
            rows = []
            for name in EXPECTED_HOST_TESTS:
                rows.extend((f"COMPILE\\t{name}\\t-std=c++17\\ttests/{name}.cpp", f"RUN\\t{name}"))
            fake_runner.write_text(
                "#!/bin/sh\nprintf '%b\\n' " + shlex.quote("\n".join(rows)) + "\n",
                encoding="utf-8",
            )
            fake_runner.chmod(0o700)
            with self.assertRaises(AssertionError):
                self.assert_behavioral_host_execution(fake_runner)

    def test_contract_mode_never_creates_an_execution_trace(self):
        with tempfile.TemporaryDirectory() as temporary:
            trace = pathlib.Path(temporary) / "must-not-exist.trace"
            result = subprocess.run(
                [str(RUNNER), "--contract"],
                capture_output=True,
                text=True,
                check=False,
                env={**os.environ, "STOPWATCH_VALIDATION_TRACE_FILE": str(trace)},
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse(trace.exists())

    def test_user_and_diagnostic_docs_cover_the_production_contract(self):
        readme = README.read_text(encoding="utf-8")
        project_readme = PROJECT_README.read_text(encoding="utf-8")
        validation = VALIDATION.read_text(encoding="utf-8")
        probe = PROBE_DOC.read_text(encoding="utf-8")
        normalized_readme = " ".join(readme.split())
        normalized_project_readme = " ".join(project_readme.split())
        normalized_validation = " ".join(validation.split())
        normalized_probe = " ".join(probe.split())

        for text in (
            "Voice Input: Driver Required",
            "Voice Input: Restart Required",
            "Voice Input: Accessibility Required",
            "Voice Input: Typeless Not Running",
            "Voice Input: Error",
            "Set Up Watch Voice Input...",
            "90 ms",
            "300 ms",
            "A+B",
            "wake press",
            "six-digit",
            "eight Agents",
            "remove-legacy-voice-key",
            "com.stopwatch.codex-agent.voice",
            "account `StopWatch-Codex`",
            "AES-128-CCM",
            "version 6",
            "version 2",
            "audio.existential.BlackHole2ch",
            "/Library/Audio/Plug-Ins/HAL/BlackHole2ch.driver",
            "sudo pkgutil --forget audio.existential.BlackHole2ch",
        ):
            self.assertIn(text, normalized_readme)

        self.assertIn("does not contain an uninstaller", normalized_readme)
        self.assertIn("does not start the application", normalized_readme)
        self.assertIn("does not start when you log in", normalized_readme)
        self.assertIn("previous default input", normalized_readme)
        self.assertIn("status and time synchronization continue", normalized_readme)
        self.assertIn("diagnostic-only", normalized_probe)
        self.assertIn("fully quit", normalized_probe.casefold())
        self.assertIn("normal `StopWatch-Codex` identity", normalized_probe)
        self.assertIn("physical acceptance", normalized_probe)
        self.assertIn("--host-tests-only", normalized_probe)

        button_contract = (
            "Inside the watch Codex app, an upper-right single click submits after "
            "the 300 ms double-click window. A fast double click steers the running "
            "turn, or sends once when idle. Holding for 300 ms still controls "
            "Typeless."
        )
        for document in (
            normalized_project_readme,
            normalized_readme,
            normalized_validation,
        ):
            self.assertIn(button_contract, document)
            self.assertIn("com.openai.codex", document)
            self.assertIn("A+B Home", document)
            self.assertIn("does not activate Codex", document)
            self.assertIn("independent of voice readiness", document)
            self.assertIn("固定麦克风", document)
            self.assertIn("display-off", document)

        for safety_fact in (
            "AXPress",
            "Command+Return",
            "CGEventPostToPid",
            "English and Simplified Chinese descriptions from 16 composer "
            "resource IDs",
            "same-process foreground race",
        ):
            self.assertIn(safety_fact, normalized_validation)
        self.assertIn("export IDF_PATH=/path/to/esp-idf", normalized_validation)
        self.assertNotIn("/Users/luke/esp", normalized_validation)
        self.assertIn(
            "Full Mac Agent discovery: 904 tests passed",
            normalized_validation,
        )
        self.assertIn(
            "Focused input protocol and desktop submission: 37 tests passed",
            normalized_validation,
        )


if __name__ == "__main__":
    unittest.main()
