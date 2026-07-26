"""Fail-closed coordination for authenticated watch voice sessions."""

import asyncio
import enum
from collections import deque

from ima_adpcm import decode_ima_adpcm_block
from voice_input_protocol import (
    AUDIO,
    BLOCK_SAMPLES,
    PTT_BEGIN,
    PTT_END,
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


class VoiceSessionService:
    def __init__(self, route, shortcut, key: bytes, *, sleep=asyncio.sleep):
        self.route = route
        self.shortcut = shortcut
        self._key = key
        self._sleep = sleep
        self.state = VoiceState.READY
        self._session = None
        self._session_nonce = None
        self._last_sequence = None
        self._route_armed = False
        self._shortcut_armed = False
        self._recent_nonces = deque(maxlen=RECENT_NONCE_LIMIT)
        self._highest_session = 0

    async def prepare(self) -> VoiceState:
        return self.state

    async def handle(self, raw: bytes) -> None:
        try:
            packet = decode_notification(self._key, raw, self._session_nonce)
        except Exception:
            await self._abort()
            raise
        try:
            if packet.kind == PTT_BEGIN:
                await self._begin(packet)
            elif packet.kind == AUDIO:
                await self._audio(packet)
            elif packet.kind == PTT_END:
                await self._end(packet)
            else:
                raise ValueError("voice error notification aborted the session")
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
            self.route.write(SILENCE)
        samples = decode_ima_adpcm_block(packet.payload, BLOCK_SAMPLES)
        self.route.write(samples)
        self._last_sequence = packet.sequence

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
            or not (
                MIN_AUDIO_MTU
                <= summary.negotiated_mtu
                <= MAX_BLE_MTU
            )
        ):
            raise ValueError("PTT_END summary does not match the session")
        await self._sleep(DRAIN_SECONDS)
        await self._cleanup(VoiceState.READY)

    def _require_current_session(self, packet) -> None:
        if self.state != VoiceState.RECORDING or packet.session != self._session:
            raise ValueError("notification is not for the active session")

    async def _abort(self) -> None:
        await self._cleanup(VoiceState.ERROR)

    async def _cleanup(self, final_state: VoiceState) -> None:
        failures = []
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
        self.state = VoiceState.ERROR if failures else final_state
        if failures:
            raise failures[0]
