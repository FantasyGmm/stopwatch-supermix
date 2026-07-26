import asyncio
import dataclasses
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
    pack_notification,
)
from voice_session import VoiceSessionService, VoiceState  # noqa: E402


TEST_KEY = bytes.fromhex("000102030405060708090a0b0c0d0e0f")
TEST_NONCE = bytes.fromhex("1011121314151617")


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
                self.assertEqual(route.calls, [])
                self.assertEqual(shortcut.calls, [])

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
            "elapsed implausible": dataclasses.replace(valid, elapsed_ms=41),
            "mtu too small": dataclasses.replace(valid, negotiated_mtu=188),
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
        with self.assertRaisesRegex(RuntimeError, "voice error 4"):
            await error_handler

        self.assertEqual(drain_calls, [0.150])
        self.assertEqual(shortcut.calls, ["start", "stop"])
        self.assertEqual(route.calls, ["start", "write:320", "stop"])
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
