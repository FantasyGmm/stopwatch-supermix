"""Fail-closed coordination for authenticated watch voice sessions."""

import asyncio
import enum
from collections import deque

from ima_adpcm import decode_ima_adpcm_block
from voice_input_protocol import (
    AUDIO,
    BLOCK_SAMPLES,
    ERROR_PAYLOAD,
    PTT_BEGIN,
    PTT_END,
    VOICE_ERROR,
    EndSummary,
    decode_notification,
)


MAX_GAP_BLOCKS = 5
MAX_SEQUENCE = 0xFFFFFFFF
MIN_AUDIO_MTU = 189
MAX_BLE_MTU = 527
DRAIN_SECONDS = 0.150
RECENT_NONCE_LIMIT = 8
SILENCE = (0,) * BLOCK_SAMPLES


class VoiceState(enum.Enum):
    DRIVER_REQUIRED = "driver_required"
    RESTART_REQUIRED = "restart_required"
    ACCESSIBILITY_REQUIRED = "accessibility_required"
    TYPELESS_NOT_RUNNING = "typeless_not_running"
    READY = "ready"
    RECORDING = "recording"
    ERROR = "error"


READINESS_STATES = frozenset(
    {
        VoiceState.DRIVER_REQUIRED,
        VoiceState.RESTART_REQUIRED,
        VoiceState.ACCESSIBILITY_REQUIRED,
        VoiceState.TYPELESS_NOT_RUNNING,
        VoiceState.READY,
    }
)


class VoiceSessionService:
    def __init__(
        self,
        route,
        shortcut,
        key: bytes,
        *,
        readiness_checker=lambda: VoiceState.READY,
        sleep=asyncio.sleep,
    ):
        self.route = route
        self.shortcut = shortcut
        self._key = key
        self._readiness_checker = readiness_checker
        self._sleep = sleep
        self.state = VoiceState.READY
        self._session = None
        self._session_nonce = None
        self._last_sequence = None
        self._route_armed = False
        self._shortcut_armed = False
        self._recent_nonces = deque(maxlen=RECENT_NONCE_LIMIT)
        self._highest_session = 0
        self._cleanup_task = None
        self._cleanup_final_state = None

    async def prepare(self) -> VoiceState:
        state = self._readiness_checker()
        if state not in READINESS_STATES:
            self.state = VoiceState.ERROR
            raise ValueError("readiness checker returned an invalid voice state")
        self.state = state
        return self.state

    async def handle(self, raw: bytes) -> None:
        try:
            packet = decode_notification(self._key, raw, self._session_nonce)
        except Exception:
            if self._has_active_resources():
                await self._abort()
            raise
        try:
            if packet.kind == PTT_BEGIN:
                await self._begin(packet)
            elif packet.kind == AUDIO:
                await self._audio(packet)
            elif packet.kind == PTT_END:
                await self._end(packet)
            elif packet.kind == VOICE_ERROR:
                await self._voice_error(packet)
            else:
                raise ValueError("unknown voice notification kind")
        except Exception:
            await self._abort()
            raise

    async def disconnect(self) -> None:
        await self._cleanup(VoiceState.READY)

    async def close(self) -> None:
        await self._cleanup(VoiceState.READY)

    async def _begin(self, packet) -> None:
        if self.state != VoiceState.READY:
            raise ValueError("voice session is not ready to begin")
        if packet.session_nonce in self._recent_nonces:
            raise ValueError("session nonce was recently used")
        if packet.session <= self._highest_session:
            raise ValueError("session is stale")
        self._recent_nonces.append(packet.session_nonce)
        self._highest_session = packet.session
        self.route.start()
        self._route_armed = True
        self.shortcut.start()
        self._shortcut_armed = True
        self._session = packet.session
        self._session_nonce = packet.session_nonce
        self._last_sequence = 0
        self.state = VoiceState.RECORDING

    async def _audio(self, packet) -> None:
        self._require_current_session(packet)
        if packet.sequence <= self._last_sequence:
            raise ValueError("AUDIO sequence regressed")
        if packet.sequence == MAX_SEQUENCE:
            raise ValueError("AUDIO sequence would force PTT_END to wrap")
        missing_blocks = packet.sequence - self._last_sequence - 1
        if missing_blocks > MAX_GAP_BLOCKS:
            raise ValueError("AUDIO sequence gap is too large")
        for _missing in range(missing_blocks):
            self._write_audio(SILENCE)
        samples = decode_ima_adpcm_block(packet.payload, BLOCK_SAMPLES)
        self._write_audio(samples)
        self._last_sequence = packet.sequence

    def _write_audio(self, samples) -> None:
        accepted_frames = self.route.write(samples)
        if accepted_frames != len(samples):
            raise RuntimeError(
                "short audio write: accepted {} of {} input frames".format(
                    accepted_frames, len(samples)
                )
            )

    async def _end(self, packet) -> None:
        self._require_current_session(packet)
        if packet.sequence != self._last_sequence + 1:
            raise ValueError("unexpected PTT_END sequence")
        summary = EndSummary.from_payload(packet.payload)
        packet_count = packet.sequence - 1
        if (
            summary.generated_packets != packet_count
            or summary.accepted_packets != packet_count
            or summary.queue_overflows != 0
            or summary.encode_failures != 0
            or summary.captured_samples != packet_count * BLOCK_SAMPLES
            or not self._elapsed_matches_capture(summary)
            or not (
                MIN_AUDIO_MTU
                <= summary.negotiated_mtu
                <= MAX_BLE_MTU
            )
        ):
            raise ValueError("PTT_END summary does not match the session")
        await self._cleanup(VoiceState.READY)

    async def _voice_error(self, packet) -> None:
        self._require_current_session(packet)
        if packet.sequence != self._last_sequence + 1:
            raise ValueError("unexpected VOICE_ERROR sequence")
        code, detail = ERROR_PAYLOAD.unpack(packet.payload)
        raise RuntimeError(
            "watch reported voice error {} (detail {})".format(code, detail)
        )

    @staticmethod
    def _elapsed_matches_capture(summary: EndSummary) -> bool:
        if summary.elapsed_ms == 0:
            return False
        expected_samples = summary.elapsed_ms * 16
        sample_delta = abs(summary.captured_samples - expected_samples)
        # The probe gate allows 1% clock error plus one final 320-sample block.
        return sample_delta * 100 <= expected_samples + 32000

    def _require_current_session(self, packet) -> None:
        if self.state != VoiceState.RECORDING or packet.session != self._session:
            raise ValueError("notification is not for the active session")

    def _has_active_resources(self) -> bool:
        return self._shortcut_armed or self._route_armed

    async def _abort(self) -> None:
        await self._cleanup(VoiceState.ERROR)

    async def _cleanup(self, final_state: VoiceState) -> None:
        if self._cleanup_task is None:
            self._cleanup_final_state = final_state
            self._cleanup_task = asyncio.create_task(
                self._perform_cleanup()
            )
        elif final_state == VoiceState.ERROR:
            self._cleanup_final_state = VoiceState.ERROR
            if self._cleanup_task.done():
                self.state = VoiceState.ERROR
        cleanup_task = self._cleanup_task
        cancelled = False
        try:
            while True:
                try:
                    await asyncio.shield(cleanup_task)
                    break
                except asyncio.CancelledError:
                    cancelled = True
                    if cleanup_task.done():
                        break
                except Exception:
                    if not cancelled:
                        raise
                    break
            if not cancelled:
                cleanup_task.result()
                return
            try:
                cleanup_task.result()
            except Exception:
                pass
            raise asyncio.CancelledError
        finally:
            if cleanup_task.done() and self._cleanup_task is cleanup_task:
                self._cleanup_task = None
                self._cleanup_final_state = None

    async def _perform_cleanup(self) -> None:
        failures = []
        if self._shortcut_armed or self._route_armed:
            await self._sleep(DRAIN_SECONDS)
        if self._shortcut_armed:
            self._shortcut_armed = False
            try:
                self.shortcut.stop()
            except Exception as error:
                failures.append(error)
        if self._route_armed:
            self._route_armed = False
            try:
                self.route.stop()
            except Exception as error:
                failures.append(error)
        self._session = None
        self._session_nonce = None
        self._last_sequence = None
        self.state = (
            VoiceState.ERROR if failures else self._cleanup_final_state
        )
        if failures:
            raise failures[0]
