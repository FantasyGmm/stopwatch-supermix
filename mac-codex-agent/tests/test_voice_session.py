import asyncio
import dataclasses
import gc
import pathlib
import sys
import unittest
from unittest import mock

from cryptography.exceptions import InvalidTag


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from voice_input_protocol import (  # noqa: E402
    AUDIO,
    BEGIN_BODY,
    ERROR_PAYLOAD,
    IMA_ADPCM,
    PTT_BEGIN,
    PTT_END,
    VOICE_ERROR,
    EndSummary,
    pack_notification as _pack_notification,
)
from voice_session import (  # noqa: E402
    VoiceSessionService as _VoiceSessionService,
    VoiceState,
)


TEST_KEY = bytes.fromhex("000102030405060708090a0b0c0d0e0f")
TEST_NONCE = bytes.fromhex("1011121314151617")
TEST_EPOCH = 7


def pack_notification(
    key,
    kind,
    session,
    sequence,
    watch_ms,
    payload,
    session_nonce,
    *,
    key_epoch=TEST_EPOCH,
):
    return _pack_notification(
        key,
        kind,
        session,
        sequence,
        watch_ms,
        payload,
        session_nonce,
        key_epoch=key_epoch,
    )


def VoiceSessionService(route, shortcut, key, **kwargs):
    kwargs.setdefault("key_epoch", TEST_EPOCH)
    return _VoiceSessionService(route, shortcut, key, **kwargs)


def nonce_for(value):
    return value.to_bytes(8, "little")


def encrypted_packet(kind, sequence, payload, session=7, nonce=TEST_NONCE):
    return pack_notification(
        TEST_KEY,
        kind,
        session,
        sequence,
        (100 + sequence * 20) & 0xFFFFFFFF,
        payload,
        nonce,
    )


def begin_packet(session=7, nonce=TEST_NONCE):
    return encrypted_packet(
        PTT_BEGIN,
        0,
        BEGIN_BODY.pack(IMA_ADPCM, 16000, 320),
        session,
        nonce,
    )


def silence_block():
    return bytes(164)


def audio_packet(sequence, session=7, nonce=TEST_NONCE, payload=None):
    return encrypted_packet(
        AUDIO,
        sequence,
        silence_block() if payload is None else payload,
        session,
        nonce,
    )


def end_packet(sequence, summary, session=7, nonce=TEST_NONCE):
    return encrypted_packet(
        PTT_END,
        sequence,
        summary.pack(),
        session,
        nonce,
    )


class FakeRoute:
    def __init__(
        self,
        fail_start=False,
        fail_stop=False,
        accepted_frames=None,
        write_error=None,
    ):
        self.active = False
        self.calls = []
        self.written = []
        self.fail_start = fail_start
        self.fail_stop = fail_stop
        self.accepted_frames = accepted_frames
        self.write_error = write_error
        self.prepared = False

    def prepare(self):
        if not self.prepared:
            self.calls.append("prepare")
            self.prepared = True

    def start(self):
        self.calls.append("start")
        if self.fail_start:
            raise RuntimeError("route start failed")
        self.active = True

    def write(self, samples):
        self.calls.append("write:{}".format(len(samples)))
        if self.write_error is not None:
            raise self.write_error
        self.written.append(list(samples))
        if self.accepted_frames is None:
            return len(samples)
        return self.accepted_frames

    def stop(self):
        self.calls.append("stop")
        self.active = False
        if self.fail_stop:
            raise RuntimeError("route stop failed")

    def close(self):
        if not self.prepared:
            return
        self.calls.append("close")
        self.prepared = False
        self.active = False


class FakeShortcut:
    def __init__(self, fail_start=False, fail_stop=False):
        self.active = False
        self.calls = []
        self.fail_start = fail_start
        self.fail_stop = fail_stop

    def start(self):
        self.calls.append("start")
        if self.fail_start:
            raise RuntimeError("shortcut start failed")
        self.active = True

    def stop(self):
        self.calls.append("stop")
        self.active = False
        if self.fail_stop:
            raise RuntimeError("shortcut stop failed")


async def no_sleep(_seconds):
    return None


class VoiceSessionServiceTests(unittest.IsolatedAsyncioTestCase):
    def make_service(self):
        route = FakeRoute()
        shortcut = FakeShortcut()
        service = VoiceSessionService(
            route, shortcut, TEST_KEY, sleep=no_sleep
        )
        return service, route, shortcut

    async def test_prepare_uses_injected_readiness_without_starting_resources(self):
        states = (
            VoiceState.DRIVER_REQUIRED,
            VoiceState.RESTART_REQUIRED,
            VoiceState.ACCESSIBILITY_REQUIRED,
            VoiceState.TYPELESS_NOT_RUNNING,
            VoiceState.READY,
        )
        for expected in states:
            with self.subTest(expected=expected):
                route = FakeRoute()
                shortcut = FakeShortcut()
                service = VoiceSessionService(
                    route,
                    shortcut,
                    TEST_KEY,
                    readiness_checker=lambda expected=expected: expected,
                    sleep=no_sleep,
                )

                self.assertEqual(await service.prepare(), expected)
                self.assertEqual(service.state, expected)
                self.assertEqual(
                    route.calls,
                    ["prepare"] if expected is VoiceState.READY else [],
                )
                self.assertEqual(shortcut.calls, [])

    async def test_disconnect_closes_prepared_route(self):
        service, route, shortcut = self.make_service()

        self.assertIs(await service.prepare(), VoiceState.READY)
        await service.disconnect()

        self.assertEqual(route.calls, ["prepare", "close"])
        self.assertEqual(shortcut.calls, [])

    async def test_bounded_disconnect_closes_prepared_route(self):
        service, route, shortcut = self.make_service()

        self.assertIs(await service.prepare(), VoiceState.READY)
        self.assertTrue(await service.disconnect_bounded(0.1))

        self.assertEqual(route.calls, ["prepare", "close"])
        self.assertEqual(shortcut.calls, [])

    async def test_disconnect_closes_prepared_route_after_stop_failure(self):
        route = FakeRoute(fail_stop=True)
        shortcut = FakeShortcut()
        service = VoiceSessionService(
            route, shortcut, TEST_KEY, sleep=no_sleep
        )

        self.assertIs(await service.prepare(), VoiceState.READY)
        await service.handle(begin_packet())
        with self.assertRaisesRegex(RuntimeError, "route stop failed"):
            await service.disconnect()

        self.assertEqual(
            route.calls, ["prepare", "start", "stop", "close"]
        )
        self.assertEqual(shortcut.calls, ["start", "stop"])

    async def test_prepare_rejects_recording_without_permitting_second_begin(self):
        readiness = mock.Mock(return_value=VoiceState.READY)
        route = FakeRoute()
        shortcut = FakeShortcut()
        service = VoiceSessionService(
            route,
            shortcut,
            TEST_KEY,
            readiness_checker=readiness,
            sleep=no_sleep,
        )
        await service.handle(begin_packet())

        try:
            with self.assertRaisesRegex(ValueError, "session is active"):
                await service.prepare()
            readiness.assert_not_called()
            self.assertEqual(service.state, VoiceState.RECORDING)

            with self.assertRaisesRegex(ValueError, "session is active"):
                await service.handle(
                    begin_packet(session=8, nonce=nonce_for(8))
                )
        finally:
            await service.disconnect()

        self.assertEqual(route.calls.count("start"), 1)
        self.assertEqual(shortcut.calls.count("start"), 1)

    async def test_prepare_rejects_blocked_cleanup_without_overwriting_state(self):
        drain_started = asyncio.Event()
        release_drain = asyncio.Event()
        readiness = mock.Mock(return_value=VoiceState.READY)

        async def blocked_sleep(seconds):
            self.assertEqual(seconds, 0.150)
            drain_started.set()
            await release_drain.wait()

        route = FakeRoute()
        shortcut = FakeShortcut()
        service = VoiceSessionService(
            route,
            shortcut,
            TEST_KEY,
            readiness_checker=readiness,
            sleep=blocked_sleep,
        )
        await service.handle(begin_packet())
        cleanup = asyncio.create_task(service.close())
        await drain_started.wait()

        try:
            with self.assertRaisesRegex(ValueError, "cleanup is in progress"):
                await service.prepare()
            readiness.assert_not_called()
            self.assertEqual(service.state, VoiceState.RECORDING)
            self.assertEqual(route.calls.count("start"), 1)
            self.assertEqual(shortcut.calls.count("start"), 1)
        finally:
            release_drain.set()
            await cleanup

        self.assertEqual(service.state, VoiceState.READY)
        self.assertEqual(route.calls, ["start", "stop"])
        self.assertEqual(shortcut.calls, ["start", "stop"])

    async def test_begin_audio_end_pairs_route_and_shortcut(self):
        route = FakeRoute()
        shortcut = FakeShortcut()
        service = VoiceSessionService(
            route, shortcut, TEST_KEY, sleep=no_sleep
        )

        await service.handle(begin_packet())
        await service.handle(audio_packet(1))
        await service.handle(
            end_packet(2, EndSummary(1, 1, 0, 0, 320, 20, 256))
        )

        self.assertEqual(route.calls, ["start", "write:320", "stop"])
        self.assertEqual(shortcut.calls, ["start", "stop"])
        self.assertEqual(service.state, VoiceState.READY)

    async def test_invalid_tag_cleans_up_before_audio_decode_or_write(self):
        route = FakeRoute()
        shortcut = FakeShortcut()
        service = VoiceSessionService(
            route, shortcut, TEST_KEY, sleep=no_sleep
        )
        await service.handle(begin_packet())
        tampered = bytearray(audio_packet(1))
        tampered[-1] ^= 1

        with mock.patch("voice_session.decode_ima_adpcm_block") as decoder:
            with self.assertRaises(InvalidTag):
                await service.handle(bytes(tampered))

        decoder.assert_not_called()
        self.assertEqual(route.calls, ["start", "stop"])
        self.assertEqual(shortcut.calls, ["start", "stop"])
        self.assertEqual(service.state, VoiceState.ERROR)

    async def test_unauthenticated_idle_traffic_preserves_readiness_without_drain(self):
        drain_calls = []

        async def record_sleep(seconds):
            drain_calls.append(seconds)

        route = FakeRoute()
        shortcut = FakeShortcut()
        service = VoiceSessionService(
            route,
            shortcut,
            TEST_KEY,
            readiness_checker=lambda: VoiceState.TYPELESS_NOT_RUNNING,
            sleep=record_sleep,
        )
        await service.prepare()
        tampered = bytearray(begin_packet())
        tampered[-1] ^= 1

        with self.assertRaises(InvalidTag):
            await service.handle(bytes(tampered))

        self.assertEqual(service.state, VoiceState.TYPELESS_NOT_RUNNING)
        self.assertEqual(drain_calls, [])
        self.assertEqual(route.calls, [])
        self.assertEqual(shortcut.calls, [])

    async def test_duplicate_recent_session_nonce_is_rejected_before_start(self):
        service, route, shortcut = self.make_service()
        await service.handle(begin_packet())
        await service.handle(audio_packet(1))
        await service.handle(
            end_packet(2, EndSummary(1, 1, 0, 0, 320, 20, 256))
        )

        with self.assertRaises(ValueError):
            await service.handle(begin_packet(session=8))

        self.assertEqual(route.calls, ["start", "write:320", "stop"])
        self.assertEqual(shortcut.calls, ["start", "stop"])
        self.assertEqual(service.state, VoiceState.ERROR)

    async def test_forward_audio_gap_inserts_one_silence_block_per_sequence(self):
        service, route, _shortcut = self.make_service()
        await service.handle(begin_packet())

        with mock.patch(
            "voice_session.decode_ima_adpcm_block",
            return_value=[7] * 320,
        ) as decoder:
            await service.handle(audio_packet(1))
            await service.handle(audio_packet(4))

        self.assertEqual(decoder.call_count, 2)
        self.assertEqual(
            route.written,
            [[7] * 320, [0] * 320, [0] * 320, [7] * 320],
        )
        await service.handle(
            end_packet(5, EndSummary(4, 4, 0, 0, 1280, 80, 256))
        )
        self.assertEqual(service.state, VoiceState.READY)

    async def test_short_normal_write_aborts_and_drains_before_cleanup(self):
        drain_calls = []

        async def record_sleep(seconds):
            drain_calls.append(seconds)

        route = FakeRoute(accepted_frames=319)
        shortcut = FakeShortcut()
        service = VoiceSessionService(
            route, shortcut, TEST_KEY, sleep=record_sleep
        )
        await service.handle(begin_packet())

        with self.assertRaisesRegex(RuntimeError, "short audio write"):
            await service.handle(audio_packet(1))

        self.assertEqual(drain_calls, [0.150])
        self.assertEqual(route.calls, ["start", "write:320", "stop"])
        self.assertEqual(shortcut.calls, ["start", "stop"])
        self.assertEqual(service.state, VoiceState.ERROR)

    async def test_short_silence_write_aborts_before_decoding_next_audio(self):
        drain_calls = []

        async def record_sleep(seconds):
            drain_calls.append(seconds)

        route = FakeRoute()
        shortcut = FakeShortcut()
        service = VoiceSessionService(
            route, shortcut, TEST_KEY, sleep=record_sleep
        )
        await service.handle(begin_packet())
        await service.handle(audio_packet(1))
        route.accepted_frames = 319

        with mock.patch("voice_session.decode_ima_adpcm_block") as decoder:
            with self.assertRaisesRegex(RuntimeError, "short audio write"):
                await service.handle(audio_packet(3))

        decoder.assert_not_called()
        self.assertEqual(drain_calls, [0.150])
        self.assertEqual(route.written, [[0] * 320, [0] * 320])
        self.assertEqual(route.calls[-1], "stop")
        self.assertEqual(shortcut.calls[-1], "stop")
        self.assertEqual(service.state, VoiceState.ERROR)

    async def test_stale_session_is_rejected_before_start(self):
        service, route, shortcut = self.make_service()
        nonce = nonce_for(70)
        await service.handle(begin_packet(session=7, nonce=nonce))
        await service.handle(audio_packet(1, session=7, nonce=nonce))
        await service.handle(
            end_packet(
                2,
                EndSummary(1, 1, 0, 0, 320, 20, 256),
                session=7,
                nonce=nonce,
            )
        )

        with self.assertRaises(ValueError):
            await service.handle(begin_packet(session=6, nonce=nonce_for(60)))

        self.assertEqual(route.calls, ["start", "write:320", "stop"])
        self.assertEqual(shortcut.calls, ["start", "stop"])
        self.assertEqual(service.state, VoiceState.ERROR)

    async def test_watch_monotonic_sessions_and_contiguous_error_are_accepted(self):
        service, route, shortcut = self.make_service()
        first_nonce = nonce_for(1)
        await service.handle(begin_packet(session=1, nonce=first_nonce))
        await service.handle(
            end_packet(
                1,
                EndSummary(0, 0, 0, 0, 0, 10, 256),
                session=1,
                nonce=first_nonce,
            )
        )

        second_nonce = nonce_for(2)
        await service.handle(begin_packet(session=2, nonce=second_nonce))
        with self.assertRaisesRegex(RuntimeError, "watch reported voice error"):
            await service.handle(
                encrypted_packet(
                    VOICE_ERROR,
                    1,
                    ERROR_PAYLOAD.pack(5, 0),
                    session=2,
                    nonce=second_nonce,
                )
            )

        self.assertEqual(route.calls, ["start", "stop", "start", "stop"])
        self.assertEqual(shortcut.calls, ["start", "stop", "start", "stop"])
        self.assertEqual(service.state, VoiceState.ERROR)

    async def test_route_start_failure_uses_cleanup_without_paired_stops(self):
        route = FakeRoute(fail_start=True)
        shortcut = FakeShortcut()
        service = VoiceSessionService(
            route, shortcut, TEST_KEY, sleep=no_sleep
        )

        with self.assertRaisesRegex(RuntimeError, "route start failed"):
            await service.handle(begin_packet())

        self.assertEqual(route.calls, ["start"])
        self.assertEqual(shortcut.calls, [])
        self.assertEqual(service.state, VoiceState.ERROR)

    async def test_authenticated_stale_audio_cleans_up_active_session(self):
        service, route, shortcut = self.make_service()
        await service.handle(begin_packet())

        with self.assertRaises(ValueError):
            await service.handle(audio_packet(1, session=8))

        self.assertEqual(route.calls, ["start", "stop"])
        self.assertEqual(shortcut.calls, ["start", "stop"])
        self.assertEqual(service.state, VoiceState.ERROR)

    async def test_cleanup_continues_after_stop_failure_and_is_idempotent(self):
        route = FakeRoute(fail_stop=True)
        shortcut = FakeShortcut(fail_stop=True)
        service = VoiceSessionService(
            route, shortcut, TEST_KEY, sleep=no_sleep
        )
        await service.handle(begin_packet())

        with self.assertRaisesRegex(RuntimeError, "shortcut stop failed"):
            await service.disconnect()

        self.assertEqual(shortcut.calls, ["start", "stop"])
        self.assertEqual(route.calls, ["start", "stop"])
        self.assertEqual(service.state, VoiceState.ERROR)

        await service.disconnect()
        self.assertEqual(shortcut.calls, ["start", "stop"])
        self.assertEqual(route.calls, ["start", "stop"])

    async def test_idle_cleanup_cannot_downgrade_error_before_prepare(self):
        route = FakeRoute(fail_stop=True)
        shortcut = FakeShortcut()
        service = VoiceSessionService(
            route, shortcut, TEST_KEY, sleep=no_sleep
        )
        await service.handle(begin_packet())

        with self.assertRaisesRegex(RuntimeError, "route stop failed"):
            await service.disconnect()
        self.assertEqual(service.state, VoiceState.ERROR)

        await service.disconnect()
        self.assertEqual(service.state, VoiceState.ERROR)
        await service.close()
        self.assertEqual(service.state, VoiceState.ERROR)

        self.assertEqual(await service.prepare(), VoiceState.READY)
        self.assertEqual(service.state, VoiceState.READY)

    async def test_overlapping_begin_aborts_without_second_start(self):
        service, route, shortcut = self.make_service()
        await service.handle(begin_packet())

        with self.assertRaises(ValueError):
            await service.handle(begin_packet(session=8, nonce=nonce_for(80)))

        self.assertEqual(route.calls, ["start", "stop"])
        self.assertEqual(shortcut.calls, ["start", "stop"])
        self.assertEqual(service.state, VoiceState.ERROR)

    async def test_end_rejects_every_summary_or_sequence_mismatch(self):
        valid = EndSummary(1, 1, 0, 0, 320, 20, 256)
        invalid_summaries = {
            "generated": dataclasses.replace(valid, generated_packets=2),
            "accepted": dataclasses.replace(valid, accepted_packets=0),
            "overflow": dataclasses.replace(valid, queue_overflows=1),
            "encode": dataclasses.replace(valid, encode_failures=1),
            "captured": dataclasses.replace(valid, captured_samples=319),
            "elapsed zero": dataclasses.replace(valid, elapsed_ms=0),
            "elapsed implausible": dataclasses.replace(valid, elapsed_ms=1000),
            "mtu too small": dataclasses.replace(valid, negotiated_mtu=192),
            "mtu too large": dataclasses.replace(valid, negotiated_mtu=528),
        }

        for name, summary in invalid_summaries.items():
            with self.subTest(name=name):
                service, route, shortcut = self.make_service()
                await service.handle(begin_packet())
                await service.handle(audio_packet(1))
                with self.assertRaises(ValueError):
                    await service.handle(end_packet(2, summary))
                self.assertEqual(route.calls[-1], "stop")
                self.assertEqual(shortcut.calls[-1], "stop")
                self.assertEqual(service.state, VoiceState.ERROR)

        service, route, shortcut = self.make_service()
        await service.handle(begin_packet())
        await service.handle(audio_packet(1))
        with self.assertRaises(ValueError):
            await service.handle(end_packet(3, valid))
        self.assertEqual(route.calls[-1], "stop")
        self.assertEqual(shortcut.calls[-1], "stop")
        self.assertEqual(service.state, VoiceState.ERROR)

    async def test_end_allows_one_final_block_of_elapsed_quantization(self):
        service, _route, _shortcut = self.make_service()
        await service.handle(begin_packet())
        await service.handle(audio_packet(1))

        await service.handle(
            end_packet(2, EndSummary(1, 1, 0, 0, 320, 40, 256))
        )

        self.assertEqual(service.state, VoiceState.READY)

    async def test_end_allows_bounded_capture_startup_latency(self):
        service, _route, _shortcut = self.make_service()
        await service.handle(begin_packet())
        for sequence in range(1, 282):
            await service.handle(audio_packet(sequence))

        await service.handle(
            end_packet(
                282,
                EndSummary(281, 281, 0, 0, 89920, 5861, 256),
            )
        )

        self.assertEqual(service.state, VoiceState.READY)

    async def test_shortcut_start_failure_cleans_only_armed_route(self):
        route = FakeRoute()
        shortcut = FakeShortcut(fail_start=True)
        service = VoiceSessionService(
            route, shortcut, TEST_KEY, sleep=no_sleep
        )

        with self.assertRaisesRegex(RuntimeError, "shortcut start failed"):
            await service.handle(begin_packet())

        self.assertEqual(route.calls, ["start", "stop"])
        self.assertEqual(shortcut.calls, ["start"])
        self.assertEqual(service.state, VoiceState.ERROR)

    async def test_disconnect_and_close_share_idempotent_cleanup(self):
        service, route, shortcut = self.make_service()
        await service.handle(begin_packet())

        await service.disconnect()
        await service.close()

        self.assertEqual(route.calls, ["start", "stop"])
        self.assertEqual(shortcut.calls, ["start", "stop"])
        self.assertFalse(route.active)
        self.assertFalse(shortcut.active)
        self.assertEqual(service.state, VoiceState.READY)

    async def test_begin_cannot_race_same_tick_idle_cleanup(self):
        for cleanup_method in ("disconnect", "close"):
            with self.subTest(cleanup_method=cleanup_method):
                route = FakeRoute()
                shortcut = FakeShortcut()
                service = VoiceSessionService(
                    route, shortcut, TEST_KEY, sleep=no_sleep
                )
                cleanup = asyncio.create_task(
                    getattr(service, cleanup_method)()
                )
                await asyncio.sleep(0)

                try:
                    with self.assertRaisesRegex(
                        ValueError, "cleanup is in progress"
                    ):
                        await service.handle(begin_packet())
                finally:
                    await cleanup

                self.assertEqual(route.calls, [])
                self.assertEqual(shortcut.calls, [])
                self.assertEqual(service.state, VoiceState.ERROR)

    async def test_sequence_regression_and_oversized_gap_fail_before_decode(self):
        service, route, _shortcut = self.make_service()
        await service.handle(begin_packet())
        await service.handle(audio_packet(1))
        with mock.patch("voice_session.decode_ima_adpcm_block") as decoder:
            with self.assertRaises(ValueError):
                await service.handle(audio_packet(1))
        decoder.assert_not_called()
        self.assertEqual(service.state, VoiceState.ERROR)

        service, route, _shortcut = self.make_service()
        await service.handle(begin_packet())
        with mock.patch("voice_session.decode_ima_adpcm_block") as decoder:
            with self.assertRaises(ValueError):
                await service.handle(audio_packet(7))
        decoder.assert_not_called()
        self.assertEqual(route.written, [])
        self.assertEqual(service.state, VoiceState.ERROR)

    async def test_max_audio_sequence_aborts_before_forcing_end_wrap(self):
        service, route, _shortcut = self.make_service()
        await service.handle(begin_packet())

        with mock.patch("voice_session.decode_ima_adpcm_block") as decoder:
            with self.assertRaisesRegex(ValueError, "wrap"):
                await service.handle(audio_packet(0xFFFFFFFF))

        decoder.assert_not_called()
        self.assertEqual(route.written, [])
        self.assertEqual(service.state, VoiceState.ERROR)

    async def test_voice_error_requires_active_session_and_exact_next_sequence(self):
        service, route, shortcut = self.make_service()
        await service.handle(begin_packet())

        with self.assertRaisesRegex(ValueError, "active session"):
            await service.handle(
                encrypted_packet(
                    VOICE_ERROR,
                    1,
                    ERROR_PAYLOAD.pack(4, 9),
                    session=8,
                )
            )
        self.assertEqual(route.calls, ["start", "stop"])
        self.assertEqual(shortcut.calls, ["start", "stop"])
        self.assertEqual(service.state, VoiceState.ERROR)

        service, _route, _shortcut = self.make_service()
        await service.handle(begin_packet())
        with self.assertRaisesRegex(ValueError, "VOICE_ERROR sequence"):
            await service.handle(
                encrypted_packet(VOICE_ERROR, 2, ERROR_PAYLOAD.pack(4, 9))
            )

        service, _route, _shortcut = self.make_service()
        await service.handle(begin_packet())
        with self.assertRaisesRegex(
            RuntimeError, "watch reported voice error 4.*detail 9"
        ):
            await service.handle(
                encrypted_packet(VOICE_ERROR, 1, ERROR_PAYLOAD.pack(4, 9))
            )

    async def test_handle_preserves_triggering_error_when_cleanup_fails(self):
        cases = (
            (
                "write",
                RuntimeError("route write failed"),
                audio_packet(1),
                "route write failed",
                ["start", "write:320", "stop"],
            ),
            (
                "voice_error",
                None,
                encrypted_packet(
                    VOICE_ERROR, 1, ERROR_PAYLOAD.pack(4, 9)
                ),
                "watch reported voice error 4",
                ["start", "stop"],
            ),
        )
        for name, write_error, raw, message, route_calls in cases:
            with self.subTest(name=name):
                route = FakeRoute(
                    fail_stop=True, write_error=write_error
                )
                shortcut = FakeShortcut()
                service = VoiceSessionService(
                    route, shortcut, TEST_KEY, sleep=no_sleep
                )
                await service.handle(begin_packet())

                with self.assertRaisesRegex(RuntimeError, message) as raised:
                    await service.handle(raw)

                self.assertIsInstance(raised.exception.__cause__, RuntimeError)
                self.assertEqual(
                    str(raised.exception.__cause__), "route stop failed"
                )
                self.assertEqual(route.calls, route_calls)
                self.assertEqual(shortcut.calls, ["start", "stop"])
                self.assertEqual(service.state, VoiceState.ERROR)

    async def test_start_drain_and_stop_order_is_bounded(self):
        events = []

        class OrderedRoute(FakeRoute):
            def start(inner_self):
                events.append("route.start")
                super().start()

            def write(inner_self, samples):
                events.append("route.write")
                return super().write(samples)

            def stop(inner_self):
                events.append("route.stop")
                super().stop()

        class OrderedShortcut(FakeShortcut):
            def start(inner_self):
                events.append("shortcut.start")
                super().start()

            def stop(inner_self):
                events.append("shortcut.stop")
                super().stop()

        async def record_sleep(seconds):
            events.append("sleep:{:.3f}".format(seconds))

        service = VoiceSessionService(
            OrderedRoute(), OrderedShortcut(), TEST_KEY, sleep=record_sleep
        )
        await service.handle(begin_packet())
        await service.handle(audio_packet(1))
        await service.handle(
            end_packet(2, EndSummary(1, 1, 0, 0, 320, 20, 256))
        )

        self.assertEqual(
            events,
            [
                "route.start",
                "shortcut.start",
                "route.write",
                "sleep:0.150",
                "shortcut.stop",
                "route.stop",
            ],
        )

    async def test_cancellation_during_drain_finishes_cleanup_then_propagates(self):
        drain_started = asyncio.Event()
        release_drain = asyncio.Event()

        async def blocked_sleep(seconds):
            self.assertEqual(seconds, 0.150)
            drain_started.set()
            await release_drain.wait()

        route = FakeRoute()
        shortcut = FakeShortcut()
        service = VoiceSessionService(
            route, shortcut, TEST_KEY, sleep=blocked_sleep
        )
        await service.handle(begin_packet())
        await service.handle(audio_packet(1))
        handler = asyncio.create_task(
            service.handle(
                end_packet(2, EndSummary(1, 1, 0, 0, 320, 20, 256))
            )
        )
        await drain_started.wait()

        handler.cancel()
        await asyncio.sleep(0)
        self.assertFalse(handler.done())
        release_drain.set()
        with self.assertRaises(asyncio.CancelledError):
            await handler

        self.assertEqual(shortcut.calls, ["start", "stop"])
        self.assertEqual(route.calls, ["start", "write:320", "stop"])
        self.assertEqual(service.state, VoiceState.READY)

    async def test_repeated_cancellation_cannot_cancel_cleanup(self):
        drain_started = asyncio.Event()
        release_drain = asyncio.Event()

        async def blocked_sleep(seconds):
            self.assertEqual(seconds, 0.150)
            drain_started.set()
            await release_drain.wait()

        route = FakeRoute(fail_stop=True)
        shortcut = FakeShortcut()
        service = VoiceSessionService(
            route, shortcut, TEST_KEY, sleep=blocked_sleep
        )
        await service.handle(begin_packet())
        handler = asyncio.create_task(service.disconnect())
        await drain_started.wait()

        handler.cancel()
        await asyncio.sleep(0)
        handler.cancel()
        await asyncio.sleep(0)
        self.assertFalse(handler.done())
        release_drain.set()

        with self.assertRaises(asyncio.CancelledError):
            await handler
        self.assertEqual(shortcut.calls, ["start", "stop"])
        self.assertEqual(route.calls, ["start", "stop"])
        self.assertEqual(service.state, VoiceState.ERROR)

    async def test_bounded_cleanup_caller_cancellation_dominates_stop_failure(self):
        drain_started = asyncio.Event()
        cleanup_task_cancelled = asyncio.Event()

        async def cancellation_paused_sleep(seconds):
            self.assertEqual(seconds, 0.150)
            drain_started.set()
            try:
                await asyncio.Event().wait()
            except asyncio.CancelledError:
                cleanup_task_cancelled.set()
                await asyncio.Event().wait()
                raise

        route = FakeRoute(fail_stop=True)
        shortcut = FakeShortcut()
        service = VoiceSessionService(
            route,
            shortcut,
            TEST_KEY,
            sleep=cancellation_paused_sleep,
        )
        await service.handle(begin_packet())
        cleanup = asyncio.create_task(service.disconnect_bounded(0.01))
        await drain_started.wait()
        await asyncio.wait_for(cleanup_task_cancelled.wait(), 1)

        cleanup.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await cleanup

        self.assertEqual(shortcut.calls, ["start", "stop"])
        self.assertEqual(route.calls, ["start", "stop"])
        self.assertEqual(service.state, VoiceState.ERROR)

    async def test_bounded_cleanup_preserves_cancel_as_child_finishes(self):
        await self._assert_bounded_cancel_as_child_finishes(fail_stop=False)

    async def test_bounded_cleanup_preserves_cancel_as_failing_child_finishes(self):
        await self._assert_bounded_cancel_as_child_finishes(fail_stop=True)

    async def _assert_bounded_cancel_as_child_finishes(self, fail_stop):
        drain_started = asyncio.Event()
        cleanup_task_cancelled = asyncio.Event()
        finish_cancelled_cleanup = asyncio.Event()

        async def cancellation_paused_sleep(seconds):
            self.assertEqual(seconds, 0.150)
            drain_started.set()
            try:
                await asyncio.Event().wait()
            except asyncio.CancelledError:
                cleanup_task_cancelled.set()
                await finish_cancelled_cleanup.wait()
                raise

        route = FakeRoute(fail_stop=fail_stop)
        shortcut = FakeShortcut()
        service = VoiceSessionService(
            route,
            shortcut,
            TEST_KEY,
            sleep=cancellation_paused_sleep,
        )
        await service.handle(begin_packet())
        cleanup = asyncio.create_task(service.disconnect_bounded(0.01))
        await drain_started.wait()
        await asyncio.wait_for(cleanup_task_cancelled.wait(), 1)

        finish_cancelled_cleanup.set()
        cleanup.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await cleanup

        self.assertEqual(shortcut.calls, ["start", "stop"])
        self.assertEqual(route.calls, ["start", "stop"])
        self.assertEqual(
            service.state,
            VoiceState.ERROR if fail_stop else VoiceState.READY,
        )

    async def test_bounded_cleanup_uses_success_from_cancel_suppressing_child(self):
        await self._assert_cancel_suppressing_child_outcome(
            fail_stop=False,
            cancel_caller=False,
        )

    async def test_bounded_cleanup_uses_failure_from_cancel_suppressing_child(self):
        await self._assert_cancel_suppressing_child_outcome(
            fail_stop=True,
            cancel_caller=False,
        )

    async def test_bounded_cleanup_caller_cancel_dominates_suppressed_success(self):
        await self._assert_cancel_suppressing_child_outcome(
            fail_stop=False,
            cancel_caller=True,
        )

    async def test_bounded_cleanup_caller_cancel_dominates_suppressed_failure(self):
        await self._assert_cancel_suppressing_child_outcome(
            fail_stop=True,
            cancel_caller=True,
        )

    async def test_forced_cleanup_serializes_error_upgrade_for_all_waiters(self):
        await self._assert_forced_cleanup_serializes_waiters(fail_stop=False)

    async def test_forced_cleanup_preserves_operation_error_over_stop_failure(self):
        await self._assert_forced_cleanup_serializes_waiters(fail_stop=True)

    async def test_stale_forced_done_callback_cannot_complete_next_generation(self):
        first_drain_started = asyncio.Event()
        old_callback_deferred = asyncio.Event()
        second_drain_started = asyncio.Event()
        release_second_drain = asyncio.Event()
        deferred = []
        sleep_calls = 0

        async def generation_sleep(seconds):
            nonlocal sleep_calls
            self.assertEqual(seconds, 0.150)
            sleep_calls += 1
            if sleep_calls == 1:
                first_drain_started.set()
                await asyncio.Event().wait()
            second_drain_started.set()
            await release_second_drain.wait()

        route = FakeRoute()
        shortcut = FakeShortcut()
        service = VoiceSessionService(
            route,
            shortcut,
            TEST_KEY,
            sleep=generation_sleep,
        )
        original_done = service._cleanup_worker_done

        def defer_first_done(*args):
            if not deferred:
                deferred.append(lambda: original_done(*args))
                old_callback_deferred.set()
            else:
                original_done(*args)

        second_cleanup = None
        second_task = None
        try:
            with mock.patch.object(
                service,
                "_cleanup_worker_done",
                side_effect=defer_first_done,
            ):
                await service.handle(begin_packet())
                first_cleanup = asyncio.create_task(
                    service.disconnect_bounded(0.01)
                )
                await first_drain_started.wait()
                self.assertFalse(await first_cleanup)
                await asyncio.wait_for(old_callback_deferred.wait(), 1)
                self.assertIsNone(service._cleanup_task)

                route.fail_stop = True
                await service.handle(
                    begin_packet(session=8, nonce=nonce_for(8))
                )
                second_cleanup = asyncio.create_task(service.disconnect())
                await second_drain_started.wait()
                second_task = service._cleanup_task
                second_completion = service._cleanup_completion

                deferred[0]()
                await asyncio.sleep(0)

                self.assertIs(service._cleanup_task, second_task)
                self.assertIs(service._cleanup_completion, second_completion)
                self.assertFalse(second_task.done())
                self.assertFalse(second_completion.done())
                self.assertFalse(second_cleanup.done())
                self.assertEqual(
                    route.calls,
                    ["start", "stop", "start"],
                )

                release_second_drain.set()
                with self.assertRaisesRegex(
                    RuntimeError, "route stop failed"
                ):
                    await second_cleanup

            self.assertEqual(
                shortcut.calls,
                ["start", "stop", "start", "stop"],
            )
            self.assertEqual(
                route.calls,
                ["start", "stop", "start", "stop"],
            )
            self.assertEqual(service.state, VoiceState.ERROR)
            self.assertIsNone(service._cleanup_task)
            self.assertEqual(
                [
                    task
                    for task in asyncio.all_tasks()
                    if task is not asyncio.current_task() and not task.done()
                ],
                [],
            )
        finally:
            release_second_drain.set()
            for task in (second_cleanup, second_task):
                if task is not None and not task.done():
                    task.cancel()
            await asyncio.gather(
                *(
                    task
                    for task in (second_cleanup, second_task)
                    if task is not None
                ),
                return_exceptions=True,
            )

    async def _assert_forced_cleanup_serializes_waiters(self, fail_stop):
        drain_started = asyncio.Event()
        cleanup_task_cancelled = asyncio.Event()
        allow_forced_finish = asyncio.Event()
        unhandled = []
        tasks = []
        loop = asyncio.get_running_loop()
        previous_handler = loop.get_exception_handler()

        def capture_unhandled(_loop, context):
            unhandled.append(context)

        async def cancellation_paused_sleep(seconds):
            self.assertEqual(seconds, 0.150)
            drain_started.set()
            try:
                await asyncio.Event().wait()
            except asyncio.CancelledError:
                cleanup_task_cancelled.set()
                await allow_forced_finish.wait()
                raise

        route = FakeRoute(fail_stop=fail_stop)
        shortcut = FakeShortcut()
        service = VoiceSessionService(
            route,
            shortcut,
            TEST_KEY,
            sleep=cancellation_paused_sleep,
        )
        await service.handle(begin_packet())
        loop.set_exception_handler(capture_unhandled)
        try:
            ready_waiter = asyncio.create_task(service.disconnect())
            tasks.append(ready_waiter)
            await drain_started.wait()
            bounded_waiter = asyncio.create_task(
                service.disconnect_bounded(0.01)
            )
            tasks.append(bounded_waiter)
            await asyncio.wait_for(cleanup_task_cancelled.wait(), 1)
            child = service._cleanup_task

            cancelled_waiter = asyncio.create_task(service.disconnect())
            operation_waiter = asyncio.create_task(
                service.handle(audio_packet(1))
            )
            tasks.extend((cancelled_waiter, operation_waiter))
            await asyncio.sleep(0)
            cancelled_waiter.cancel()
            await asyncio.sleep(0)

            self.assertFalse(ready_waiter.done())
            self.assertFalse(cancelled_waiter.done())
            self.assertFalse(operation_waiter.done())
            self.assertEqual(service._cleanup_final_state, VoiceState.ERROR)

            allow_forced_finish.set()
            results = await asyncio.gather(*tasks, return_exceptions=True)

            if fail_stop:
                self.assertIsInstance(results[0], RuntimeError)
                self.assertEqual(str(results[0]), "route stop failed")
                self.assertIsInstance(results[1], RuntimeError)
                self.assertEqual(str(results[1]), "route stop failed")
            else:
                self.assertIsNone(results[0])
                self.assertFalse(results[1])
            self.assertIsInstance(results[2], asyncio.CancelledError)
            self.assertIsInstance(results[3], ValueError)
            self.assertEqual(str(results[3]), "voice cleanup is in progress")
            if fail_stop:
                self.assertIsInstance(results[3].__cause__, RuntimeError)
                self.assertEqual(
                    str(results[3].__cause__), "route stop failed"
                )
            else:
                self.assertIsNone(results[3].__cause__)

            self.assertEqual(shortcut.calls, ["start", "stop"])
            self.assertEqual(route.calls, ["start", "stop"])
            self.assertEqual(service.state, VoiceState.ERROR)
            self.assertIsNone(service._cleanup_task)
            self.assertTrue(child.done())
            del child
            gc.collect()
            await asyncio.sleep(0)
            await asyncio.sleep(0)
            self.assertEqual(unhandled, [])
        finally:
            allow_forced_finish.set()
            for task in tasks:
                if not task.done():
                    task.cancel()
            if tasks:
                await asyncio.gather(*tasks, return_exceptions=True)
            loop.set_exception_handler(previous_handler)

    async def _assert_cancel_suppressing_child_outcome(
        self, *, fail_stop, cancel_caller
    ):
        drain_started = asyncio.Event()
        cancellation_suppressed = asyncio.Event()
        release_drain = asyncio.Event()
        unhandled = []
        loop = asyncio.get_running_loop()
        previous_handler = loop.get_exception_handler()

        def capture_unhandled(_loop, context):
            unhandled.append(context)

        async def cancellation_suppressing_sleep(seconds):
            self.assertEqual(seconds, 0.150)
            drain_started.set()
            while not release_drain.is_set():
                try:
                    await release_drain.wait()
                except asyncio.CancelledError:
                    cancellation_suppressed.set()

        route = FakeRoute(fail_stop=fail_stop)
        shortcut = FakeShortcut()
        service = VoiceSessionService(
            route,
            shortcut,
            TEST_KEY,
            sleep=cancellation_suppressing_sleep,
        )
        await service.handle(begin_packet())
        cleanup = None
        child = None
        loop.set_exception_handler(capture_unhandled)
        try:
            with mock.patch.object(
                service,
                "_finish_cleanup",
                wraps=service._finish_cleanup,
            ) as finish_cleanup:
                cleanup = asyncio.create_task(
                    service.disconnect_bounded(0.01)
                )
                await drain_started.wait()
                await asyncio.wait_for(cancellation_suppressed.wait(), 1)
                child = service._cleanup_task

                if cancel_caller:
                    cleanup.cancel()
                release_drain.set()

                if cancel_caller:
                    with self.assertRaises(asyncio.CancelledError):
                        await cleanup
                elif fail_stop:
                    with self.assertRaisesRegex(
                        RuntimeError, "route stop failed"
                    ):
                        await cleanup
                else:
                    self.assertFalse(await cleanup)

                self.assertEqual(finish_cleanup.call_count, 1)

            self.assertEqual(shortcut.calls, ["start", "stop"])
            self.assertEqual(route.calls, ["start", "stop"])
            self.assertEqual(
                service.state,
                VoiceState.ERROR if fail_stop else VoiceState.READY,
            )
            self.assertIsNone(service._cleanup_task)
            self.assertTrue(child.done())
            child = None
            cleanup = None
            gc.collect()
            await asyncio.sleep(0)
            await asyncio.sleep(0)
            self.assertEqual(unhandled, [])
        finally:
            release_drain.set()
            for task in (cleanup, child):
                if task is not None and not task.done():
                    task.cancel()
            await asyncio.gather(
                *(
                    task
                    for task in (cleanup, child)
                    if task is not None
                ),
                return_exceptions=True,
            )
            loop.set_exception_handler(previous_handler)

    async def test_concurrent_error_dominates_end_cleanup_final_state(self):
        drain_started = asyncio.Event()
        release_drain = asyncio.Event()
        drain_calls = []

        async def blocked_sleep(seconds):
            drain_calls.append(seconds)
            drain_started.set()
            await release_drain.wait()

        route = FakeRoute()
        shortcut = FakeShortcut()
        service = VoiceSessionService(
            route, shortcut, TEST_KEY, sleep=blocked_sleep
        )
        await service.handle(begin_packet())
        await service.handle(audio_packet(1))
        end_handler = asyncio.create_task(
            service.handle(
                end_packet(2, EndSummary(1, 1, 0, 0, 320, 20, 256))
            )
        )
        await drain_started.wait()
        error_handler = asyncio.create_task(
            service.handle(
                encrypted_packet(VOICE_ERROR, 2, ERROR_PAYLOAD.pack(4, 9))
            )
        )
        await asyncio.sleep(0)
        self.assertFalse(error_handler.done())

        release_drain.set()
        await end_handler
        with self.assertRaisesRegex(ValueError, "cleanup is in progress"):
            await error_handler

        self.assertEqual(drain_calls, [0.150])
        self.assertEqual(shortcut.calls, ["start", "stop"])
        self.assertEqual(route.calls, ["start", "write:320", "stop"])
        self.assertEqual(service.state, VoiceState.ERROR)

    async def test_cleanup_ownership_rejects_all_concurrent_session_traffic(self):
        summary = EndSummary(1, 1, 0, 0, 320, 20, 256)
        traffic = (
            ("audio", audio_packet(2)),
            ("begin", begin_packet(session=8, nonce=nonce_for(8))),
            ("end", end_packet(2, summary)),
            (
                "voice_error",
                encrypted_packet(
                    VOICE_ERROR, 2, ERROR_PAYLOAD.pack(4, 9)
                ),
            ),
        )
        for name, raw in traffic:
            with self.subTest(name=name):
                drain_started = asyncio.Event()
                release_drain = asyncio.Event()
                drain_calls = []

                async def blocked_sleep(seconds):
                    drain_calls.append(seconds)
                    drain_started.set()
                    await release_drain.wait()

                route = FakeRoute()
                shortcut = FakeShortcut()
                service = VoiceSessionService(
                    route, shortcut, TEST_KEY, sleep=blocked_sleep
                )
                await service.handle(begin_packet())
                await service.handle(audio_packet(1))
                cleanup = asyncio.create_task(service.disconnect())
                await drain_started.wait()

                with mock.patch(
                    "voice_session.decode_ima_adpcm_block",
                    return_value=[1] * 320,
                ) as decoder:
                    concurrent = asyncio.create_task(service.handle(raw))
                    try:
                        await asyncio.sleep(0)
                        self.assertFalse(concurrent.done())
                    finally:
                        release_drain.set()
                        await cleanup

                    with self.assertRaisesRegex(
                        ValueError, "cleanup is in progress"
                    ):
                        await concurrent
                    decoder.assert_not_called()

                self.assertEqual(drain_calls, [0.150])
                self.assertEqual(route.written, [[0] * 320])
                self.assertEqual(
                    route.calls, ["start", "write:320", "stop"]
                )
                self.assertEqual(shortcut.calls, ["start", "stop"])
                self.assertEqual(service.state, VoiceState.ERROR)

    async def test_all_active_failure_cleanup_paths_drain_once(self):
        async def exercise(case):
            drain_calls = []

            async def record_sleep(seconds):
                drain_calls.append(seconds)

            route = FakeRoute(
                fail_start=case == "route_start",
                fail_stop=case == "route_stop",
                write_error=(
                    RuntimeError("route write failed")
                    if case == "write"
                    else None
                ),
            )
            shortcut = FakeShortcut(fail_start=case == "shortcut_start")
            service = VoiceSessionService(
                route, shortcut, TEST_KEY, sleep=record_sleep
            )

            if case in ("route_start", "shortcut_start"):
                with self.assertRaises(RuntimeError):
                    await service.handle(begin_packet())
                return drain_calls

            await service.handle(begin_packet())
            if case == "disconnect":
                await service.disconnect()
            elif case == "close":
                await service.close()
            elif case == "auth":
                tampered = bytearray(audio_packet(1))
                tampered[-1] ^= 1
                with self.assertRaises(InvalidTag):
                    await service.handle(bytes(tampered))
            elif case == "malformed":
                with self.assertRaises(ValueError):
                    await service.handle(audio_packet(1)[:-1])
            elif case == "voice_error":
                with self.assertRaises(RuntimeError):
                    await service.handle(
                        encrypted_packet(
                            VOICE_ERROR, 1, ERROR_PAYLOAD.pack(4, 9)
                        )
                    )
            elif case == "write":
                with self.assertRaisesRegex(RuntimeError, "route write failed"):
                    await service.handle(audio_packet(1))
            elif case == "route_stop":
                with self.assertRaisesRegex(RuntimeError, "route stop failed"):
                    await service.disconnect()
            else:
                self.fail("unknown cleanup case {}".format(case))
            return drain_calls

        active_cases = (
            "disconnect",
            "close",
            "auth",
            "malformed",
            "voice_error",
            "write",
            "shortcut_start",
            "route_stop",
        )
        for case in active_cases:
            with self.subTest(case=case):
                self.assertEqual(await exercise(case), [0.150])

        self.assertEqual(await exercise("route_start"), [])

    async def test_recent_nonce_cache_has_a_small_bounded_window(self):
        service, _route, _shortcut = self.make_service()
        summary = EndSummary(1, 1, 0, 0, 320, 20, 256)
        for session in range(1, 10):
            nonce = nonce_for(session)
            await service.handle(begin_packet(session=session, nonce=nonce))
            await service.handle(
                audio_packet(1, session=session, nonce=nonce)
            )
            await service.handle(
                end_packet(2, summary, session=session, nonce=nonce)
            )

        await service.handle(begin_packet(session=10, nonce=nonce_for(1)))
        self.assertEqual(service.state, VoiceState.RECORDING)
        await service.disconnect()


if __name__ == "__main__":
    unittest.main()
