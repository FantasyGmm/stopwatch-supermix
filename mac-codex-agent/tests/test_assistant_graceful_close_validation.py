import importlib.util
import inspect
import io
import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT_PATH = ROOT / "scripts" / "validate_assistant_graceful_close.py"


def load_module():
    spec = importlib.util.spec_from_file_location(
        "validate_assistant_graceful_close", SCRIPT_PATH
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


GOOD_TRANSCRIPT = """
I AssistantTrace: ASTRACE sid=4 seq=1 t=100 comp=app event=open state=stopped detail=none result=accepted v0=0 v1=0 v2=0 v3=0
I AssistantTrace: ASTRACE sid=4 seq=2 t=110 comp=runtime event=snapshot state=stopped detail=before-start result=ok v0=100000 v1=200000 v2=18 v3=0
I AssistantTrace: ASTRACE sid=4 seq=8 t=200 comp=runtime event=snapshot state=running detail=first-running result=ok v0=100 v1=200 v2=18 v3=1
I AssistantTrace: ASTRACE sid=4 seq=9 t=300 comp=app event=close-request state=stopping detail=debug-home result=accepted v0=0 v1=0 v2=0 v3=0
I AssistantTrace: ASTRACE sid=4 seq=10 t=301 comp=runtime event=state state=stopping detail=stop result=accepted v0=0 v1=0 v2=0 v3=0
I AssistantTrace: ASTRACE sid=4 seq=11 t=350 comp=callback event=callback state=stopping detail=stopped result=accepted v0=0 v1=0 v2=0 v3=0
I AssistantTrace: ASTRACE sid=4 seq=12 t=360 comp=runtime event=release state=stopped detail=all-resources result=ok v0=0 v1=0 v2=0 v3=0
I AssistantTrace: ASTRACE sid=4 seq=13 t=365 comp=runtime event=snapshot state=stopped detail=stop-boundary result=ok v0=3000 v1=1000 v2=0 v3=0
I AssistantTrace: ASTRACE sid=4 seq=14 t=370 comp=app event=closed state=stopped detail=debug-home result=ok v0=0 v1=0 v2=0 v3=0
I Launcher: on open
I DebugCommand: ASTATUS sid=4 state=stopped bindings=0 subscriptions=0 wifi=0 capture=0 playback=0 ap=0 blocked=0 internal=97000 psram=199000 tasks=18
"""


class AssistantGracefulCloseValidationTests(unittest.TestCase):
    def test_serial_session_synchronizes_device_parser_before_first_command(self):
        module = load_module()

        class FakeTransport:
            def __init__(self):
                self.writes = []
                self.chunks = [b"E DebugCommand: DEBUG ERROR INVALID_COMMAND\n", b""]

            def write(self, payload):
                self.writes.append(payload)

            def read(self, timeout):
                return self.chunks.pop(0) if self.chunks else b""

        output = io.StringIO()
        transport = FakeTransport()
        session = module.SerialLineSession(transport, output)
        session.synchronize(timeout=0.01)

        self.assertEqual(transport.writes, [b"\n"])
        self.assertEqual(session.pending, "")
        self.assertIn("INVALID_COMMAND", output.getvalue())

    def test_serial_session_can_discard_stale_boot_responses_before_first_command(self):
        module = load_module()

        class FakeTransport:
            def __init__(self):
                self.chunks = [b"E DebugCommand: DEBUG ERROR INVALID_COMMAND\n", b""]

            def read(self, timeout):
                return self.chunks.pop(0) if self.chunks else b""

        output = io.StringIO()
        session = module.SerialLineSession(FakeTransport(), output)
        session.drain_input(timeout=0.01)

        self.assertEqual(session.pending, "")
        self.assertIn("INVALID_COMMAND", output.getvalue())

    def test_serial_session_settle_continues_validating_runtime_output(self):
        module = load_module()

        class FakeTransport:
            def __init__(self):
                self.chunks = [b"Guru Meditation Error: panic'ed\n", b""]

            def read(self, timeout):
                return self.chunks.pop(0) if self.chunks else b""

        session = module.SerialLineSession(FakeTransport(), io.StringIO())
        validator = module.AssistantSessionValidator()

        with self.assertRaisesRegex(module.ValidationError, "reset marker"):
            session.settle(validator, 0.01)

    def test_campaign_settles_before_sampling_post_close_memory(self):
        module = load_module()

        self.assertEqual(module.build_parser().parse_args([]).settle, 1.0)
        self.assertIn("session.settle", inspect.getsource(module.run_cycle))

    def test_accepts_complete_ordered_session(self):
        module = load_module()
        validator = module.AssistantSessionValidator()
        for line in GOOD_TRANSCRIPT.splitlines():
            validator.feed(line)
        self.assertEqual(validator.running_session_id, 4)
        validator.require_complete(4)

    def test_rejects_missing_stopped_callback(self):
        module = load_module()
        validator = module.AssistantSessionValidator()
        for line in GOOD_TRANSCRIPT.replace(
            "detail=stopped result=accepted", "detail=unexpected result=accepted"
        ).splitlines():
            validator.feed(line)
        with self.assertRaisesRegex(module.ValidationError, "stopped callback"):
            validator.require_complete(4)

    def test_reconnect_accepts_explicit_offline_release_acknowledgement(self):
        module = load_module()
        transcript = GOOD_TRANSCRIPT.replace(
            "comp=callback event=callback state=stopping detail=stopped result=accepted",
            "comp=runtime event=release state=stopping detail=offline-stop result=accepted",
        )
        validator = module.AssistantSessionValidator()
        for line in transcript.splitlines():
            validator.feed(line)

        with self.assertRaisesRegex(module.ValidationError, "stopped callback"):
            validator.require_complete(4)
        self.assertIn(
            "profile",
            inspect.signature(validator.require_complete).parameters,
        )
        validator.require_complete(4, profile="reconnect")

    def test_rejects_reset_and_dirty_resources(self):
        module = load_module()
        validator = module.AssistantSessionValidator()
        with self.assertRaisesRegex(module.ValidationError, "reset marker"):
            validator.feed("rst:0xc (SW_CPU_RESET),boot:0x8")

        validator = module.AssistantSessionValidator()
        for line in GOOD_TRANSCRIPT.replace("playback=0", "playback=1").splitlines():
            validator.feed(line)
        with self.assertRaisesRegex(module.ValidationError, "resource status"):
            validator.require_complete(4)

    def test_extracts_session_leak_and_stop_metrics(self):
        module = load_module()
        validator = module.AssistantSessionValidator()
        for line in GOOD_TRANSCRIPT.splitlines():
            validator.feed(line)

        self.assertEqual(
            validator.session_metrics(4),
            {
                "session_id": 4,
                "before_internal": 100000,
                "before_psram": 200000,
                "after_internal": 97000,
                "after_psram": 199000,
                "internal_heap_loss": 3000,
                "psram_loss": 1000,
                "task_delta": 0,
                "stop_ms": 70,
            },
        )

    def test_warmup_records_one_time_shared_wifi_initialization_separately(self):
        module = load_module()
        transcript = GOOD_TRANSCRIPT.replace(
            "result=ok v0=3000 v1=1000 v2=0 v3=0",
            "result=failed v0=35000 v1=9000 v2=3 v3=0",
        ).replace("internal=97000", "internal=65000").replace("tasks=18", "tasks=21")
        validator = module.AssistantSessionValidator()
        for line in transcript.splitlines():
            validator.feed(line)

        with self.assertRaisesRegex(module.ValidationError, "stop-boundary leak gate"):
            validator.session_metrics(4)

        self.assertIn(
            "require_leak_gate",
            inspect.signature(validator.session_metrics).parameters,
        )
        warmup = validator.session_metrics(4, require_leak_gate=False)
        self.assertEqual(warmup["internal_heap_loss"], 35000)
        self.assertEqual(warmup["task_delta"], 3)
        self.assertEqual(module.validate_warmup_metrics(warmup)["session_id"], 4)

    def test_campaign_rejects_single_session_and_cumulative_leaks(self):
        module = load_module()
        campaign = module.AssistantCampaignValidator()
        good = {
            "session_id": 1,
            "before_internal": 100000,
            "before_psram": 200000,
            "after_internal": 97000,
            "after_psram": 199000,
            "internal_heap_loss": 3000,
            "psram_loss": 1000,
            "task_delta": 0,
            "stop_ms": 1200,
        }
        campaign.add(good)

        with self.assertRaisesRegex(module.ValidationError, "single-session internal heap"):
            campaign.add({**good, "session_id": 2, "internal_heap_loss": 4097})

        cumulative = module.AssistantCampaignValidator()
        cumulative.add(good)
        cumulative.add(
            {
                **good,
                "session_id": 2,
                "before_internal": 97000,
                "after_internal": 91000,
                "internal_heap_loss": 3000,
            }
        )
        with self.assertRaisesRegex(module.ValidationError, "cumulative internal heap"):
            cumulative.require_complete(expected_cycles=2)

    def test_campaign_reports_required_release_metrics(self):
        module = load_module()
        campaign = module.AssistantCampaignValidator(baseline_internal=97000)
        campaign.add(
            {
                "session_id": 7,
                "before_internal": 100000,
                "before_psram": 200000,
                "after_internal": 97000,
                "after_psram": 199000,
                "internal_heap_loss": 3000,
                "psram_loss": 1000,
                "task_delta": 0,
                "stop_ms": 1200,
            }
        )
        self.assertEqual(
            campaign.require_complete(expected_cycles=1),
            {
                "cycles": 1,
                "peak_internal_heap_loss": 3000,
                "peak_psram_loss": 1000,
                "maximum_stop_ms": 1200,
                "cumulative_internal_heap_loss": 0,
                "sessions": [7],
                "profiles": {"listening": 1},
            },
        )

    def test_campaign_compares_equivalent_launcher_boundaries(self):
        module = load_module()
        campaign = module.AssistantCampaignValidator(baseline_internal=100000)
        campaign.add(
            {
                "session_id": 1,
                "before_internal": 103000,
                "before_psram": 200000,
                "after_internal": 99000,
                "after_psram": 199000,
                "internal_heap_loss": 3000,
                "psram_loss": 1000,
                "task_delta": 0,
                "stop_ms": 1200,
            }
        )

        metrics = campaign.require_complete(expected_cycles=1)

        self.assertEqual(metrics["cumulative_internal_heap_loss"], 1000)

    def test_reads_launcher_baseline_from_runtime_status(self):
        module = load_module()

        class FakeSession:
            def command(self, action, validator=None, timeout=None):
                self.action = action
                validator.feed(
                    "I DebugCommand: ASTATUS sid=9 state=stopped bindings=0 "
                    "subscriptions=0 wifi=0 capture=0 playback=0 ap=0 blocked=0 "
                    "internal=123456 psram=654321 tasks=16"
                )

        session = FakeSession()

        self.assertEqual(module.read_launcher_internal_baseline(session, 5.0), 123456)
        self.assertEqual(session.action, "assistant-status")

    def test_mixed_campaign_balances_all_required_exit_profiles(self):
        module = load_module()
        profiles = module.build_profile_schedule(100, "mixed")
        self.assertEqual(len(profiles), 100)
        self.assertEqual(
            {name: profiles.count(name) for name in set(profiles)},
            {
                "listening": 20,
                "recording": 20,
                "tts": 20,
                "reconnect": 20,
                "conversation": 20,
            },
        )

    def test_profile_readiness_uses_observable_runtime_events(self):
        module = load_module()
        validator = module.AssistantSessionValidator()
        for line in GOOD_TRANSCRIPT.splitlines():
            validator.feed(line)
        self.assertTrue(validator.profile_ready(4, "listening"))
        self.assertFalse(validator.profile_ready(4, "recording"))

        validator.feed(
            "I AssistantTrace: ASTRACE sid=4 seq=15 t=380 comp=resource "
            "event=conversation state=running detail=listening result=accepted "
            "v0=1 v1=0 v2=0 v3=0"
        )
        self.assertTrue(validator.profile_ready(4, "recording"))

        validator.feed(
            "I AssistantTrace: ASTRACE sid=4 seq=16 t=390 comp=callback "
            "event=conversation state=running detail=start result=accepted "
            "v0=4 v1=0 v2=0 v3=0"
        )
        validator.feed(
            "I AssistantTrace: ASTRACE sid=4 seq=17 t=400 comp=callback "
            "event=conversation state=running detail=speaking result=accepted "
            "v0=1 v1=0 v2=0 v3=0"
        )
        self.assertTrue(validator.profile_ready(4, "tts"))
        self.assertFalse(validator.profile_ready(4, "conversation"))

        validator.feed(
            "I AssistantTrace: ASTRACE sid=4 seq=18 t=405 comp=callback "
            "event=conversation state=running detail=running result=accepted "
            "v0=5 v1=0 v2=0 v3=0"
        )
        validator.feed(
            "I AssistantTrace: ASTRACE sid=4 seq=19 t=408 comp=callback "
            "event=conversation state=running detail=speaking result=accepted "
            "v0=0 v1=0 v2=0 v3=0"
        )
        self.assertTrue(validator.profile_ready(4, "conversation"))

        validator.feed(
            "I AssistantTrace: ASTRACE sid=4 seq=20 t=410 comp=debug "
            "event=resource-decision state=running detail=wifi result=accepted "
            "v0=0 v1=0 v2=0 v3=0"
        )
        self.assertTrue(validator.profile_ready(4, "reconnect"))


if __name__ == "__main__":
    unittest.main()
