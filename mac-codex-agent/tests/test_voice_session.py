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
    def __init__(self, fail_start=False, fail_stop=False):
        self.active = False
        self.calls = []
        self.written = []
        self.fail_start = fail_start
        self.fail_stop = fail_stop

    def start(self):
        self.calls.append("start")
        if self.fail_start:
            raise RuntimeError("route start failed")
        self.active = True

    def write(self, samples):
        self.calls.append("write:{}".format(len(samples)))
        self.written.append(list(samples))
        return len(samples)

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

    async def test_authenticated_voice_error_aborts_active_session(self):
        service, route, shortcut = self.make_service()
        await service.handle(begin_packet())

        with self.assertRaises(ValueError):
            await service.handle(
                encrypted_packet(VOICE_ERROR, 1, ERROR_PAYLOAD.pack(4, 9))
            )

        self.assertEqual(route.calls, ["start", "stop"])
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
