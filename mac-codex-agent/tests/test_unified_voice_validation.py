import os
import pathlib
import re
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
RUNNER = ROOT / "scripts/run_unified_offline_validation.sh"
README = ROOT / "mac-codex-agent/README.md"
PROBE_DOC = ROOT / "docs/ble-voice-link-probe.md"


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

    def test_stage_contract_has_consistent_numbering_and_final_gates(self):
        lines = self.run_contract()
        stages = []
        for line in lines:
            match = re.fullmatch(r"\[(\d+)/(\d+)\] (.+)", line)
            if match:
                stages.append((int(match.group(1)), int(match.group(2)), match.group(3)))

        self.assertGreater(len(stages), 0)
        total = stages[0][1]
        self.assertEqual(total, 29)
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
        lines = self.run_contract()
        compile_rows = {}
        run_names = []
        for line in lines:
            fields = line.split("\t")
            if fields[0] == "COMPILE":
                self.assertGreaterEqual(len(fields), 4)
                self.assertNotIn(fields[1], compile_rows)
                compile_rows[fields[1]] = fields[2:]
            elif fields[0] == "RUN":
                self.assertEqual(len(fields), 2)
                run_names.append(fields[1])

        expected_sources = {
            "ble_voice_probe_protocol_test": {
                "tests/ble_voice_probe_protocol_test.cpp",
                "main/apps/app_codex_status/ble_voice_probe_protocol.cpp",
            },
            "voice_probe_crypto_test": {
                "tests/voice_probe_crypto_test.cpp",
                "main/apps/app_codex_status/voice_probe_crypto.cpp",
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
            "voice_input_session_test": {
                "tests/voice_input_session_test.cpp",
                "main/apps/app_codex_status/voice_input_session.cpp",
            },
            "voice_capture_pipeline_test": {
                "tests/voice_capture_pipeline_test.cpp",
                "main/apps/app_codex_status/voice_capture_pipeline.cpp",
            },
            "voice_input_controller_test": {
                "tests/voice_input_controller_test.cpp",
                "main/apps/app_codex_status/voice_input_controller.cpp",
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

    def test_user_and_diagnostic_docs_cover_the_production_contract(self):
        readme = README.read_text(encoding="utf-8")
        probe = PROBE_DOC.read_text(encoding="utf-8")
        normalized_readme = " ".join(readme.split())
        normalized_probe = " ".join(probe.split())

        for text in (
            "Voice Input: Driver Required",
            "Voice Input: Restart Required",
            "Voice Input: Accessibility Required",
            "Voice Input: Typeless Not Running",
            "Voice Input: Error",
            "Set Up Watch Voice Input...",
            "90 ms",
            "A+B",
            "wake press",
            "STOPWATCH_VOICE_KEY_FILE",
            "com.stopwatch.codex-agent.voice",
            "account: `StopWatch-Codex`",
            "AES-128-CCM",
            "version 3",
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


if __name__ == "__main__":
    unittest.main()
