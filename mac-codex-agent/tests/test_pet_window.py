import dataclasses
import pathlib
import sys
import unittest
from dataclasses import dataclass
from enum import Enum


AGENT_ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(AGENT_ROOT))

from pet_converter import ConversionMetadata, ConvertedPet, PetConversionError
from pet_cache import CachedPreview
from pet_package import HEIGHT, WIDTH, PetFrame, encode_package, render_rgba_frames
from pet_window import PetWindowModel


class PetCapability(Enum):
    OFFLINE = "offline"
    UNSUPPORTED = "unsupported"
    AUTHENTICATING = "authenticating"
    READY = "ready"


@dataclass(frozen=True)
class PetWatchState:
    watch_id: bytes
    capability: PetCapability
    source: str
    schema_min: int
    schema_max: int
    generation: int
    active_hash: bytes
    usable: bool


@dataclass(frozen=True)
class PetProgress:
    request_id: str
    phase: str
    acknowledged_bytes: int
    total_bytes: int
    error_code: str = ""
    required_bytes: int = 0
    available_bytes: int = 0


def make_converted_pet(color=0xF800, frame_value=0):
    package = encode_package(
        WIDTH,
        HEIGHT,
        (color,),
        1,
        (
            PetFrame(180, bytes([frame_value]) * (WIDTH * HEIGHT)),
            PetFrame(180, bytes([frame_value ^ 1]) * (WIDTH * HEIGHT)),
        ),
    )
    return ConvertedPet(
        package,
        render_rgba_frames(package),
        ConversionMetadata(6, 2, 360, WIDTH, HEIGHT, len(package.encoded)),
    )


CONVERTED = make_converted_pet()
ALTERNATE = make_converted_pet(0x001F, 1)
OFFLINE = PetWatchState(
    None, PetCapability.OFFLINE, "built-in", 0, 0, 0, bytes(32), False
)
READY = PetWatchState(
    bytes.fromhex("00112233445566778899aabbccddeeff"),
    PetCapability.READY,
    "built-in",
    1,
    1,
    7,
    bytes(32),
    True,
)
UNSUPPORTED = PetWatchState(
    bytes.fromhex("00112233445566778899aabbccddeeff"),
    PetCapability.UNSUPPORTED,
    "built-in",
    0,
    0,
    7,
    bytes(32),
    True,
)


class PetWindowModelTests(unittest.TestCase):
    def test_initial_and_explicit_offline_states_have_distinct_guidance(self):
        # Collapsing initial and disconnected states hides whether the user can
        # safely begin a local conversion before a watch becomes available.
        initial = PetWindowModel()
        offline = initial.with_watch_state(OFFLINE)

        self.assertEqual(initial.headline, "Choose a GIF to preview")
        self.assertEqual(offline.headline, "Watch offline")
        self.assertFalse(offline.can_send)

    def test_send_enablement_requires_package_and_authenticated_capability(self):
        # Replacing capability gating with local preview-only gating would send
        # to an offline or unsupported watch.
        model = PetWindowModel().with_conversion(CONVERTED)
        self.assertFalse(model.can_send)

        model = model.with_watch_state(READY)

        self.assertTrue(model.can_send)
        self.assertEqual(model.headline, "Ready to send to watch")

    def test_unknown_remote_hash_does_not_reuse_wrong_preview(self):
        # Reusing the selected conversion for a hash owned by another Agent
        # would misrepresent the watch's authoritative custom pet.
        remote = PetWatchState(
            READY.watch_id,
            PetCapability.READY,
            "custom",
            1,
            1,
            8,
            bytes.fromhex("ff" * 32),
            True,
        )

        model = PetWindowModel().with_watch_state(remote)

        self.assertEqual(model.headline, "Custom pet installed on watch")
        self.assertIsNone(model.preview)

    def test_selected_conversion_is_previewed_only_when_its_hash_matches_watch(self):
        matching = PetWatchState(
            READY.watch_id,
            PetCapability.READY,
            "custom",
            1,
            1,
            8,
            CONVERTED.package.sha256,
            True,
        )

        model = PetWindowModel().with_conversion(CONVERTED).with_watch_state(matching)

        self.assertEqual(model.preview, CONVERTED.preview_rgba)
        self.assertEqual(model.metadata_text, "2 frames · 0.36 s · 144 × 156 · {} B".format(len(CONVERTED.package.encoded)))

    def test_processing_and_converter_error_are_visible_without_enabling_send(self):
        processing = PetWindowModel().with_processing(pathlib.Path("new-pet.gif"))
        self.assertEqual(processing.headline, "Processing GIF…")
        self.assertFalse(processing.can_send)

        failed = processing.with_conversion_error(
            PetConversionError("canvas_too_large", "GIF logical canvas exceeds 1024 x 1024")
        )
        self.assertEqual(failed.headline, "Could not process GIF")
        self.assertEqual(failed.detail, "GIF logical canvas exceeds 1024 x 1024")
        self.assertFalse(failed.can_send)

    def test_non_gif_selection_is_rejected_before_converter_runs(self):
        # Passing arbitrary files to the converter would make the chooser/drop
        # target promise broader input than the product supports.
        model = PetWindowModel().with_selected_path(pathlib.Path("pet.png"))

        self.assertEqual(model.headline, "Choose a GIF file")
        self.assertEqual(model.detail, "Only one .gif animation can be sent to the watch.")
        self.assertIsNone(model.conversion)

    def test_acknowledged_progress_is_determinate_and_exposes_cancel(self):
        selected = PetWindowModel().with_conversion(CONVERTED).with_watch_state(READY)
        model = selected.with_transfer_started("request-1", 192).with_progress(
            PetProgress("request-1", "sending", 96, 192)
        )

        self.assertEqual(model.headline, "Sending to watch")
        self.assertEqual(model.progress_fraction, 0.5)
        self.assertTrue(model.can_cancel)
        self.assertFalse(model.can_send)
        self.assertFalse(model.can_restore)

    def test_transfer_phases_render_without_inventing_success(self):
        selected = PetWindowModel().with_conversion(CONVERTED).with_watch_state(READY)
        expected = {
            "verifying": ("Verifying on watch", True, False),
            "applying": ("Applying on watch", False, False),
            "installed": ("Installed on watch", False, True),
            "error": ("Transfer failed", False, True),
            "busy": ("Another pet operation is in progress", False, False),
        }
        for phase, (headline, can_cancel, can_send) in expected.items():
            with self.subTest(phase=phase):
                model = selected.with_transfer_started("request-1", 192).with_progress(
                    PetProgress("request-1", phase, 192, 192, "busy" if phase == "busy" else "")
                )
                self.assertEqual(model.headline, headline)
                self.assertEqual(model.can_cancel, can_cancel)
                self.assertEqual(model.can_send, can_send)

    def test_unsupported_watch_disables_send_and_restore(self):
        model = PetWindowModel().with_conversion(CONVERTED).with_watch_state(UNSUPPORTED)

        self.assertEqual(model.headline, "Firmware update required")
        self.assertFalse(model.can_send)
        self.assertFalse(model.can_restore)

    def test_insufficient_storage_error_shows_exact_watch_bytes(self):
        model = (
            PetWindowModel()
            .with_conversion(CONVERTED)
            .with_watch_state(READY)
            .with_transfer_started("request-1", 263144)
            .with_progress(
                PetProgress(
                    "request-1",
                    "error",
                    0,
                    263144,
                    "insufficient_storage",
                    263144,
                    131071,
                )
            )
        )

        self.assertEqual(model.headline, "Transfer failed")
        self.assertEqual(
            model.detail,
            "Watch storage requires 263144 bytes; 131071 bytes are available.",
        )

        stale = model.with_progress(
            PetProgress(
                "request-old",
                "error",
                0,
                1,
                "insufficient_storage",
                1,
                0,
            )
        )
        self.assertEqual(stale.detail, model.detail)

    def test_restore_requires_a_usable_custom_pet_on_an_authenticated_watch(self):
        self.assertFalse(PetWindowModel().with_watch_state(OFFLINE).can_restore)
        self.assertFalse(PetWindowModel().with_watch_state(READY).can_restore)
        custom = dataclasses.replace(
            READY, source="custom", active_hash=CONVERTED.package.sha256
        )
        self.assertTrue(PetWindowModel().with_watch_state(custom).can_restore)

    def test_close_and_reopen_preserve_runtime_owned_transfer_snapshot(self):
        # Treating close as cancel would violate the background-transfer
        # contract; a reopened retained window must render the latest callback.
        sending = (
            PetWindowModel()
            .with_conversion(CONVERTED)
            .with_watch_state(READY)
            .with_transfer_started("request-1", 192)
            .with_progress(PetProgress("request-1", "sending", 48, 192))
        )

        reopened = sending.with_progress(PetProgress("request-1", "applying", 192, 192))

        self.assertEqual(reopened.headline, "Applying on watch")
        self.assertFalse(reopened.can_cancel)
        self.assertEqual(reopened.progress_fraction, 1.0)

    def test_late_callback_cannot_replace_terminal_request_presentation(self):
        # Clearing the request identity at Installed would let a stale callback
        # from another runtime generation overwrite the acknowledged outcome.
        selected = PetWindowModel().with_conversion(CONVERTED).with_watch_state(READY)
        installed = selected.with_transfer_started("request-1", 192).with_progress(
            PetProgress("request-1", "installed", 192, 192)
        )

        late = installed.with_progress(PetProgress("request-2", "error", 0, 192, "offline"))

        self.assertEqual(late.headline, "Installed on watch")
        self.assertIsNone(late.active_request_id)

    def test_progress_without_a_live_request_is_ignored(self):
        initial = PetWindowModel()

        self.assertEqual(
            initial.with_progress(PetProgress(None, "error", 0, 0, "offline")),
            initial,
        )

    def test_selecting_b_during_a_keeps_a_as_the_only_live_transfer(self):
        """A later local conversion cannot claim A's callback or Cancel slot."""
        active_a = (
            PetWindowModel()
            .with_conversion(CONVERTED)
            .with_watch_state(READY)
            .with_transfer_started("request-a", len(CONVERTED.package.encoded))
        )

        selected_b = active_a.with_conversion(ALTERNATE)

        self.assertEqual(selected_b.active_request_id, "request-a")
        self.assertIs(selected_b.conversion, ALTERNATE)
        self.assertEqual(selected_b.preview, CONVERTED.preview_rgba)
        self.assertTrue(selected_b.can_cancel)
        self.assertFalse(selected_b.can_send)

        rejected_b = selected_b.with_progress(
            PetProgress("request-b", "error", 0, 0, "cancelled")
        )
        self.assertEqual(rejected_b, selected_b)

        installed = selected_b.with_progress(
            PetProgress(
                "request-a", "installed", len(CONVERTED.package.encoded), len(CONVERTED.package.encoded)
            )
        )
        custom_a = dataclasses.replace(
            READY, source="custom", active_hash=CONVERTED.package.sha256
        )
        installed = installed.with_watch_state(custom_a)

        self.assertIsNone(installed.active_request_id)
        self.assertEqual(installed.preview, CONVERTED.preview_rgba)
        self.assertNotEqual(installed.preview, ALTERNATE.preview_rgba)
        self.assertTrue(installed.can_send)

        late_a = installed.with_progress(
            PetProgress("request-a", "cancelled", 0, 0, "cancelled")
        )
        self.assertEqual(late_a, installed)

    def test_restore_matrix_uses_authoritative_source_not_presentation_phase(self):
        custom = dataclasses.replace(
            READY, source="custom", active_hash=CONVERTED.package.sha256
        )
        installed = (
            PetWindowModel()
            .with_watch_state(custom)
            .with_transfer_started("request-a", 0, "applying")
            .with_progress(PetProgress("request-a", "installed", 0, 0))
        )
        cancelled = (
            PetWindowModel()
            .with_watch_state(custom)
            .with_transfer_started("request-a", 0)
            .with_progress(PetProgress("request-a", "error", 0, 0, "cancelled"))
        )
        failed = (
            PetWindowModel()
            .with_watch_state(custom)
            .with_transfer_started("request-a", 0)
            .with_progress(PetProgress("request-a", "error", 0, 0, "offline"))
        )
        busy = (
            PetWindowModel()
            .with_watch_state(custom)
            .with_transfer_started("request-a", 0)
            .with_progress(PetProgress("request-a", "error", 0, 0, "busy"))
        )
        active = PetWindowModel().with_watch_state(custom).with_transfer_started(
            "request-a", 0
        )

        self.assertTrue(installed.can_restore)
        self.assertTrue(cancelled.can_restore)
        self.assertTrue(failed.can_restore)
        self.assertFalse(busy.can_restore)
        self.assertFalse(active.can_restore)
        self.assertFalse(PetWindowModel().with_watch_state(OFFLINE).can_restore)
        self.assertFalse(PetWindowModel().with_watch_state(UNSUPPORTED).can_restore)
        self.assertFalse(PetWindowModel().with_watch_state(READY).can_restore)

    def test_cancelled_error_uses_cancelled_copy(self):
        model = (
            PetWindowModel()
            .with_conversion(CONVERTED)
            .with_watch_state(READY)
            .with_transfer_started("request-a", len(CONVERTED.package.encoded))
            .with_progress(PetProgress("request-a", "error", 0, 0, "cancelled"))
        )

        self.assertEqual(model.headline, "Transfer cancelled")
        self.assertEqual(
            model.detail, "The current upload was cancelled before installation."
        )

    def test_authoritative_cached_preview_requires_the_current_watch_hash(self):
        cached = CachedPreview(
            CONVERTED.package,
            CONVERTED.preview_rgba,
            tuple(frame.duration_ms for frame in CONVERTED.package.frames),
        )
        matching = dataclasses.replace(
            READY, source="custom", active_hash=CONVERTED.package.sha256
        )
        mismatched = dataclasses.replace(
            READY, source="custom", active_hash=ALTERNATE.package.sha256
        )

        reopened = PetWindowModel().with_snapshot(matching, cached)
        unknown = PetWindowModel().with_snapshot(mismatched, cached)

        self.assertEqual(reopened.preview, CONVERTED.preview_rgba)
        self.assertEqual(
            reopened.detail,
            "Preview restored from the verified watch package cache.",
        )
        self.assertEqual(reopened.metadata_text, "2 frames · 0.36 s · 144 × 156 · {} B".format(len(CONVERTED.package.encoded)))
        self.assertIsNone(unknown.preview)
        self.assertEqual(unknown.metadata_text, "No verified watch preview available")


if __name__ == "__main__":
    unittest.main()
