import asyncio
import dataclasses
import pathlib
import sys
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from pet_package import HEIGHT, SCHEMA_VERSION, WIDTH, PetFrame, encode_package
from pet_protocol import (
    CancelledStatus,
    ErrorReason,
    ErrorStatus,
    MessageKind,
    PetSource,
    PetStatus,
    ProgressStatus,
    StateStatus,
    CommittedStatus,
    decode_control,
    encode_status,
)
from pet_upload import (
    PetCapability,
    PetCommitAmbiguousError,
    PetOperation,
    PetProgress,
    PetRequest,
    PetUnsupportedError,
    PetUploadError,
    PetUploader,
    PetWatchState,
)


KEY = bytes.fromhex("000102030405060708090a0b0c0d0e0f")


def make_package():
    pixels = WIDTH * HEIGHT
    return encode_package(
        WIDTH,
        HEIGHT,
        (0xF800,),
        1,
        (
            PetFrame(120, bytes([0]) * pixels),
            PetFrame(180, bytes([1]) * pixels),
        ),
    )


PACKAGE = make_package()


class FakePetTransport:
    def __init__(
        self,
        *,
        mtu=247,
        connected=True,
        supported=True,
        drop_first_chunk_progress=False,
        drop_all_chunk_progress=False,
        conflicting_chunk_progress=False,
        drop_commit_status=False,
        drop_restore_status=False,
        drop_query_after_restore=False,
        schema_min=1,
        schema_max=1,
        begin_error=None,
    ):
        self.negotiated_mtu = mtu
        self.connected = connected
        self.supported = supported
        self.drop_first_chunk_progress = drop_first_chunk_progress
        self.drop_all_chunk_progress = drop_all_chunk_progress
        self.conflicting_chunk_progress = conflicting_chunk_progress
        self.drop_commit_status = drop_commit_status
        self.drop_restore_status = drop_restore_status
        self.drop_query_after_restore = drop_query_after_restore
        self.schema_min = schema_min
        self.schema_max = schema_max
        self.begin_error = begin_error
        self.calls = []
        self.writes = []
        self.status_frames = []
        self.delayed_status = None
        self.handler = None
        self.status_sequence = 0
        self.acknowledged = 0
        self.active_hash = bytes(32)
        self.generation = 0
        self._dropped = False
        self._restore_seen = False

    async def enable_pet_notifications(self, handler):
        self.calls.append("subscribe")
        if not self.supported:
            from pet_upload import PetUnsupportedError

            raise PetUnsupportedError("Firmware update required")
        self.handler = handler

    async def disable_pet_notifications(self):
        self.calls.append("unsubscribe")
        self.handler = None

    async def write_pet_control(self, payload):
        self.calls.append("write")
        self.writes.append(bytes(payload))
        control = decode_control(KEY, bytes(payload))
        if self.delayed_status is not None:
            delayed = self.delayed_status
            self.delayed_status = None
            await self.handler(delayed)
        if control.kind in (MessageKind.QUERY, MessageKind.BEGIN, MessageKind.RESTORE):
            self.status_sequence = 0
        self.status_sequence += 1
        if control.kind == MessageKind.QUERY:
            if self._restore_seen and self.drop_query_after_restore:
                return
            status = PetStatus(
                MessageKind.STATE,
                StateStatus(
                    control.transaction_id,
                    PetSource.CUSTOM if any(self.active_hash) else PetSource.BUILT_IN,
                    True,
                    self.schema_min,
                    self.schema_max,
                    self.generation,
                    self.active_hash,
                ),
            )
        elif control.kind == MessageKind.BEGIN:
            if self.begin_error is not None:
                reason, required, available = self.begin_error
                status = PetStatus(
                    MessageKind.ERROR,
                    ErrorStatus(
                        control.transaction_id, reason, required, available
                    ),
                )
                encoded = encode_status(
                    KEY,
                    control.exchange_nonce,
                    self.status_sequence,
                    status,
                )
                self.status_frames.append((status.kind, encoded))
                await self.handler(encoded)
                return
            self.acknowledged = 0
            self.total = control.begin.package_length
            status = PetStatus(
                MessageKind.PROGRESS,
                ProgressStatus(control.transaction_id, 0, self.total),
            )
        elif control.kind == MessageKind.CHUNK:
            end = control.chunk.offset + len(control.chunk.data)
            self.acknowledged = max(self.acknowledged, end)
            if self.drop_first_chunk_progress and not self._dropped:
                self._dropped = True
                return
            if self.drop_all_chunk_progress:
                return
            status = PetStatus(
                MessageKind.PROGRESS,
                ProgressStatus(
                    control.transaction_id,
                    self.acknowledged - 1
                    if self.conflicting_chunk_progress
                    else self.acknowledged,
                    self.total,
                ),
            )
        elif control.kind == MessageKind.COMMIT:
            if self.drop_commit_status:
                return
            self.generation += 1
            self.active_hash = PACKAGE.sha256
            status = PetStatus(
                MessageKind.COMMITTED,
                CommittedStatus(
                    control.transaction_id, self.generation, self.active_hash
                ),
            )
        elif control.kind == MessageKind.RESTORE:
            self._restore_seen = True
            self.generation += 1
            self.active_hash = bytes(32)
            if self.drop_restore_status:
                return
            status = PetStatus(
                MessageKind.STATE,
                StateStatus(
                    control.transaction_id,
                    PetSource.BUILT_IN,
                    True,
                    1,
                    1,
                    self.generation,
                    bytes(32),
                ),
            )
        elif control.kind == MessageKind.CANCEL:
            status = PetStatus(
                MessageKind.CANCELLED,
                CancelledStatus(control.transaction_id),
            )
        else:
            return
        encoded = encode_status(
            KEY,
            control.exchange_nonce,
            self.status_sequence,
            status,
        )
        self.status_frames.append((status.kind, encoded))
        await self.handler(encoded)


class PetUploaderTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self._nonce_value = 0

    def nonce_factory(self):
        self._nonce_value += 1
        return bytes([self._nonce_value]) * 8

    async def test_models_are_exactly_immutable(self):
        state = PetWatchState(
            b"W" * 16,
            PetCapability.READY,
            "built-in",
            1,
            1,
            0,
            bytes(32),
            True,
        )
        progress = PetProgress("request", "sending", 0, 10)
        request = PetRequest(
            "request", PetOperation.RESTORE, None, lambda _progress: None
        )

        with self.assertRaises(dataclasses.FrozenInstanceError):
            state.source = "custom"
        with self.assertRaises(dataclasses.FrozenInstanceError):
            progress.phase = "installed"
        with self.assertRaises(dataclasses.FrozenInstanceError):
            request.operation = PetOperation.CANCEL

    async def test_query_subscribes_before_authenticated_control(self):
        transport = FakePetTransport()
        uploader = PetUploader(
            transport,
            KEY,
            7,
            self.nonce_factory,
            watch_id=b"W" * 16,
        )

        result = await uploader.query()

        self.assertEqual(transport.calls[:2], ["subscribe", "write"])
        self.assertEqual(result.capability, PetCapability.READY)
        self.assertEqual(result.watch_id, b"W" * 16)
        self.assertEqual(result.source, "built-in")

    async def test_upload_counts_acknowledged_bytes_and_retries_exact_ciphertext(self):
        transport = FakePetTransport(drop_first_chunk_progress=True)
        uploader = PetUploader(
            transport,
            KEY,
            7,
            self.nonce_factory,
            watch_id=b"W" * 16,
            acknowledgement_timeout=0.01,
        )
        states = []

        result = await uploader.upload(PACKAGE, states.append)

        chunk_controls = [
            (raw, decode_control(KEY, raw))
            for raw in transport.writes
            if decode_control(KEY, raw).kind == MessageKind.CHUNK
        ]
        self.assertEqual(chunk_controls[0][0], chunk_controls[1][0])
        self.assertLessEqual(
            max(len(control.chunk.data) for _, control in chunk_controls),
            247 - 35,
        )
        self.assertEqual(result.active_hash, PACKAGE.sha256)
        self.assertEqual(states[-1].acknowledged_bytes, len(PACKAGE.encoded))
        self.assertEqual(states[-1].phase, "installed")
        self.assertEqual(
            sum(
                decode_control(KEY, raw).kind == MessageKind.COMMIT
                for raw in transport.writes
            ),
            1,
        )

    async def test_ambiguous_commit_is_never_retried(self):
        transport = FakePetTransport(drop_commit_status=True)
        uploader = PetUploader(
            transport,
            KEY,
            7,
            self.nonce_factory,
            acknowledgement_timeout=0.01,
        )

        with self.assertRaises(PetCommitAmbiguousError):
            await uploader.upload(PACKAGE, lambda _state: None)

        self.assertEqual(
            sum(
                decode_control(KEY, raw).kind == MessageKind.COMMIT
                for raw in transport.writes
            ),
            1,
        )

    async def test_close_after_commit_does_not_send_cancel(self):
        transport = FakePetTransport(drop_commit_status=True)
        uploader = PetUploader(
            transport,
            KEY,
            7,
            self.nonce_factory,
            acknowledgement_timeout=0.02,
        )
        task = asyncio.create_task(
            uploader.upload(PACKAGE, lambda _state: None)
        )
        for _ in range(100):
            if any(
                decode_control(KEY, raw).kind == MessageKind.COMMIT
                for raw in transport.writes
            ):
                break
            await asyncio.sleep(0)

        await uploader.close()
        with self.assertRaises(PetCommitAmbiguousError):
            await task

        kinds = [decode_control(KEY, raw).kind for raw in transport.writes]
        self.assertEqual(kinds.count(MessageKind.COMMIT), 1)
        self.assertNotIn(MessageKind.CANCEL, kinds)

    async def test_conflicting_progress_cancels_without_advancing(self):
        transport = FakePetTransport(conflicting_chunk_progress=True)
        uploader = PetUploader(
            transport,
            KEY,
            7,
            self.nonce_factory,
            acknowledgement_timeout=0.01,
        )
        states = []

        with self.assertRaisesRegex(PetUploadError, "conflicting"):
            await uploader.upload(PACKAGE, states.append)

        self.assertEqual(states[-1].acknowledged_bytes, 0)
        self.assertEqual(
            sum(
                decode_control(KEY, raw).kind == MessageKind.CANCEL
                for raw in transport.writes
            ),
            1,
        )

    async def test_timeout_sends_one_cancel_and_explicit_cancel_is_bounded(self):
        transport = FakePetTransport(drop_all_chunk_progress=True)
        uploader = PetUploader(
            transport,
            KEY,
            7,
            self.nonce_factory,
            acknowledgement_timeout=0.01,
        )

        with self.assertRaises(PetUploadError):
            await uploader.upload(PACKAGE, lambda _state: None)
        self.assertEqual(
            sum(
                decode_control(KEY, raw).kind == MessageKind.CANCEL
                for raw in transport.writes
            ),
            1,
        )

        transport = FakePetTransport(drop_all_chunk_progress=True)
        uploader = PetUploader(
            transport,
            KEY,
            7,
            self.nonce_factory,
            acknowledgement_timeout=1,
        )
        task = asyncio.create_task(
            uploader.upload(PACKAGE, lambda _state: None, request_id="cancel-me")
        )
        for _ in range(100):
            if any(
                decode_control(KEY, raw).kind == MessageKind.CHUNK
                for raw in transport.writes
            ):
                break
            await asyncio.sleep(0)
        cancel = asyncio.create_task(uploader.cancel("cancel-me"))
        with self.assertRaisesRegex(PetUploadError, "cancelled"):
            await task
        await asyncio.wait_for(cancel, 0.1)

    async def test_restore_and_progress_ordering_are_authoritative(self):
        transport = FakePetTransport(mtu=517)
        uploader = PetUploader(
            transport,
            KEY,
            7,
            self.nonce_factory,
            watch_id=b"W" * 16,
        )
        states = []

        installed = await uploader.upload(PACKAGE, states.append)
        restored = await uploader.restore(states.append, request_id="restore")

        self.assertEqual(installed.source, "custom")
        self.assertEqual(restored.source, "built-in")
        self.assertEqual(restored.active_hash, bytes(32))
        upload_phases = [
            state.phase for state in states if state.request_id != "restore"
        ]
        self.assertEqual(upload_phases[0], "sending")
        self.assertEqual(upload_phases[-3:], ["verifying", "applying", "installed"])
        self.assertEqual(
            [state.phase for state in states if state.request_id == "restore"],
            ["applying", "installed"],
        )
        self.assertLessEqual(
            max(
                len(decode_control(KEY, raw).chunk.data)
                for raw in transport.writes
                if decode_control(KEY, raw).kind == MessageKind.CHUNK
            ),
            480,
        )

    async def test_lost_restore_state_requeries_without_retry(self):
        transport = FakePetTransport(drop_restore_status=True)
        uploader = PetUploader(
            transport,
            KEY,
            7,
            self.nonce_factory,
            watch_id=b"W" * 16,
            acknowledgement_timeout=0.01,
        )
        await uploader.upload(PACKAGE, lambda _state: None)
        states = []

        reconciled = await uploader.restore(states.append, request_id="restore")

        kinds = [decode_control(KEY, raw).kind for raw in transport.writes]
        self.assertEqual(kinds.count(MessageKind.RESTORE), 1)
        self.assertEqual(kinds.count(MessageKind.QUERY), 2)
        self.assertEqual(reconciled.source, "built-in")
        self.assertEqual(reconciled.active_hash, bytes(32))
        self.assertEqual(states[-1].phase, "installed")

    async def test_lost_restore_and_query_marks_prior_state_non_authoritative(self):
        transport = FakePetTransport(
            drop_restore_status=True,
            drop_query_after_restore=True,
        )
        uploader = PetUploader(
            transport,
            KEY,
            7,
            self.nonce_factory,
            watch_id=b"W" * 16,
            acknowledgement_timeout=0.01,
        )
        await uploader.upload(PACKAGE, lambda _state: None)

        with self.assertRaises(PetUploadError):
            await uploader.restore(lambda _state: None)

        kinds = [decode_control(KEY, raw).kind for raw in transport.writes]
        self.assertEqual(kinds.count(MessageKind.RESTORE), 1)
        self.assertEqual(kinds.count(MessageKind.QUERY), 2)
        self.assertEqual(
            uploader.watch_state.capability, PetCapability.OFFLINE
        )
        self.assertFalse(uploader.watch_state.usable)

    async def test_offline_and_unsupported_channels_fail_closed(self):
        for transport, message in (
            (FakePetTransport(connected=False), "offline"),
            (FakePetTransport(supported=False), "Firmware update required"),
        ):
            with self.subTest(message=message):
                uploader = PetUploader(
                    transport, KEY, 7, self.nonce_factory
                )
                with self.assertRaisesRegex(PetUploadError, message):
                    await uploader.query()

    async def test_schema_mismatch_rejects_upload_before_begin_but_allows_restore(self):
        transport = FakePetTransport(schema_min=2, schema_max=2)
        uploader = PetUploader(
            transport,
            KEY,
            7,
            self.nonce_factory,
            watch_id=b"W" * 16,
        )

        state = await uploader.query()
        with self.assertRaisesRegex(
            PetUnsupportedError, "Firmware update required"
        ):
            await uploader.upload(PACKAGE, lambda _progress: None)

        kinds = [decode_control(KEY, raw).kind for raw in transport.writes]
        self.assertNotIn(MessageKind.BEGIN, kinds)
        self.assertIs(uploader.watch_state, state)
        restored = await uploader.restore(lambda _progress: None)
        self.assertEqual(restored.source, "built-in")

    async def test_insufficient_storage_preserves_required_and_available_bytes(self):
        transport = FakePetTransport(
            begin_error=(ErrorReason.INSUFFICIENT_STORAGE, 263144, 131071)
        )
        uploader = PetUploader(
            transport, KEY, 7, self.nonce_factory, watch_id=b"W" * 16
        )

        with self.assertRaises(PetUploadError) as raised:
            await uploader.upload(PACKAGE, lambda _progress: None)

        self.assertEqual(raised.exception.error_code, "insufficient_storage")
        self.assertEqual(raised.exception.required_bytes, 263144)
        self.assertEqual(raised.exception.available_bytes, 131071)

    async def test_stale_storage_error_cannot_poison_fresh_upload(self):
        transaction_ids = iter((201, 202))
        transport = FakePetTransport()
        uploader = PetUploader(
            transport,
            KEY,
            7,
            self.nonce_factory,
            watch_id=b"W" * 16,
            transaction_id_factory=lambda: next(transaction_ids),
        )
        await uploader.query()
        stale = PetStatus(
            MessageKind.ERROR,
            ErrorStatus(
                202, ErrorReason.INSUFFICIENT_STORAGE, 999999, 1
            ),
        )
        transport.delayed_status = encode_status(
            KEY, bytes([1]) * 8, 1, stale
        )

        state = await uploader.upload(PACKAGE, lambda _progress: None)

        self.assertEqual(state.source, "custom")

    async def test_delayed_prior_state_is_retired_during_fresh_query(self):
        transaction_ids = iter((101, 101))
        transport = FakePetTransport()
        uploader = PetUploader(
            transport,
            KEY,
            7,
            self.nonce_factory,
            watch_id=b"W" * 16,
            transaction_id_factory=lambda: next(transaction_ids),
        )

        await uploader.query()
        transport.delayed_status = transport.status_frames[-1][1]

        state = await uploader.query()
        self.assertEqual(state.source, "built-in")

    async def test_delayed_prior_progress_is_retired_during_fresh_upload(self):
        transaction_ids = iter((201, 202, 203))
        transport = FakePetTransport(mtu=517)
        uploader = PetUploader(
            transport,
            KEY,
            7,
            self.nonce_factory,
            watch_id=b"W" * 16,
            transaction_id_factory=lambda: next(transaction_ids),
        )

        await uploader.upload(PACKAGE, lambda _progress: None)
        prior_progress = next(
            frame
            for kind, frame in transport.status_frames
            if kind == MessageKind.PROGRESS
        )
        transport.delayed_status = prior_progress

        state = await uploader.upload(PACKAGE, lambda _progress: None)
        self.assertEqual(state.source, "custom")


if __name__ == "__main__":
    unittest.main()
