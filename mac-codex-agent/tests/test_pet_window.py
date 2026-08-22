import pathlib
import sys
import unittest
from dataclasses import dataclass
from enum import Enum


AGENT_ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(AGENT_ROOT))

from pet_converter import ConversionMetadata, ConvertedPet, PetConversionError
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


def make_converted_pet():
    package = encode_package(
        WIDTH,
        HEIGHT,
        (0xF800,),
        1,
        (
            PetFrame(180, bytes([0]) * (WIDTH * HEIGHT)),
            PetFrame(180, bytes([1]) * (WIDTH * HEIGHT)),
        ),
    )
    return ConvertedPet(
        package,
        render_rgba_frames(package),
        ConversionMetadata(6, 2, 360, WIDTH, HEIGHT, len(package.encoded)),
    )


CONVERTED = make_converted_pet()
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
        model = selected.with_progress(PetProgress("request-1", "sending", 96, 192))

        self.assertEqual(model.headline, "Sending to watch")
        self.assertEqual(model.progress_fraction, 0.5)
        self.assertTrue(model.can_cancel)
        self.assertFalse(model.can_send)
        self.assertFalse(model.can_restore)

    def test_transfer_phases_render_without_inventing_success(self):
        selected = PetWindowModel().with_conversion(CONVERTED).with_watch_state(READY)
        expected = {
            "verifying": ("Verifying on watch", True),
            "applying": ("Applying on watch", False),
            "installed": ("Installed on watch", False),
            "error": ("Transfer failed", False),
            "busy": ("Another pet operation is in progress", False),
        }
        for phase, (headline, can_cancel) in expected.items():
            with self.subTest(phase=phase):
                model = selected.with_progress(
                    PetProgress("request-1", phase, 192, 192, "busy" if phase == "busy" else "")
                )
                self.assertEqual(model.headline, headline)
                self.assertEqual(model.can_cancel, can_cancel)
                self.assertFalse(model.can_send if phase != "installed" else False)

    def test_unsupported_watch_disables_send_and_restore(self):
        model = PetWindowModel().with_conversion(CONVERTED).with_watch_state(UNSUPPORTED)

        self.assertEqual(model.headline, "Firmware update required")
        self.assertFalse(model.can_send)
        self.assertFalse(model.can_restore)

    def test_restore_is_available_only_for_authenticated_ready_watch(self):
        self.assertFalse(PetWindowModel().with_watch_state(OFFLINE).can_restore)
        self.assertTrue(PetWindowModel().with_watch_state(READY).can_restore)

    def test_close_and_reopen_preserve_runtime_owned_transfer_snapshot(self):
        # Treating close as cancel would violate the background-transfer
        # contract; a reopened retained window must render the latest callback.
        sending = PetWindowModel().with_conversion(CONVERTED).with_watch_state(READY).with_progress(
            PetProgress("request-1", "sending", 48, 192)
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
        self.assertEqual(late.active_request_id, "request-1")


if __name__ == "__main__":
    unittest.main()
