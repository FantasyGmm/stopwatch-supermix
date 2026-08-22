"""Codex Pet window model and the native AppKit presentation."""

from __future__ import annotations

import dataclasses
import pathlib
import threading
import time
from typing import Optional

from pet_converter import ConvertedPet, PetConversionError

import objc
from AppKit import (
    NSBackingStoreBuffered,
    NSBezelStyleRounded,
    NSBitmapFormatAlphaNonpremultiplied,
    NSBitmapImageRep,
    NSButton,
    NSColor,
    NSCompositingOperationSourceOver,
    NSControlSizeSmall,
    NSDeviceRGBColorSpace,
    NSDragOperationCopy,
    NSDragOperationNone,
    NSFont,
    NSFontWeightSemibold,
    NSImage,
    NSImageView,
    NSImageScaleProportionallyUpOrDown,
    NSModalResponseOK,
    NSOpenPanel,
    NSProgressIndicator,
    NSWindow,
    NSWindowController,
    NSWindowStyleMaskClosable,
    NSWindowStyleMaskMiniaturizable,
    NSWindowStyleMaskResizable,
    NSWindowStyleMaskTitled,
    NSView,
    NSTextField,
    NSBezierPath,
    NSFilenamesPboardType,
)
from PyObjCTools import AppHelper
from Foundation import NSTimer


# A Commit has already been sent when the uploader publishes ``applying``;
# protocol rules forbid a later Cancel, so only the pre-Commit phases expose it.
_ACTIVE_TRANSFER_PHASES = frozenset(("sending", "verifying"))
_TERMINAL_TRANSFER_PHASES = frozenset(
    ("installed", "error", "busy", "cancelled")
)
_TRANSFER_HEADLINES = {
    "sending": "Sending to watch",
    "verifying": "Verifying on watch",
    "applying": "Applying on watch",
    "installed": "Installed on watch",
    "error": "Transfer failed",
    "busy": "Another pet operation is in progress",
    "cancelled": "Transfer cancelled",
}
_USE_SELECTED_CONVERSION = object()
_UNSET_PREVIEW = object()


def _capability_value(watch_state) -> str:
    capability = getattr(watch_state, "capability", None)
    return str(getattr(capability, "value", capability or "offline"))


def _format_bytes(value: int) -> str:
    if value < 1024:
        return "{} B".format(value)
    return "{:.1f} KiB".format(value / 1024)


@dataclasses.dataclass(frozen=True)
class PetWindowModel:
    """Pure, immutable state for the retained Codex Pet window.

    The model only observes controller-owned watch state and callback progress.
    It deliberately retains no BLE worker, request task, or cache ownership.
    """

    # ``conversion`` belongs to the local chooser.  It may change while an
    # earlier package is still transferring, so it must never carry the live
    # request's identity or presentation.
    conversion: Optional[ConvertedPet] = None
    active_conversion: Optional[ConvertedPet] = None
    cached_preview: object = None
    watch_state: object = None
    selected_path: Optional[pathlib.Path] = None
    selection_phase: str = "initial"
    selection_error_code: str = ""
    selection_detail: str = "Choose one GIF to create the exact watch preview."
    transfer_phase: Optional[str] = None
    transfer_error_code: str = ""
    transfer_detail: str = ""
    acknowledged_bytes: int = 0
    total_bytes: int = 0
    active_request_id: Optional[str] = None

    @property
    def phase(self) -> str:
        """Selection phase kept for the native conversion worker contract."""
        return self.selection_phase

    @property
    def error_code(self) -> str:
        if self.transfer_phase is not None:
            return self.transfer_error_code
        return self.selection_error_code

    @property
    def detail(self) -> str:
        if self.transfer_phase is not None:
            return self.transfer_detail
        if self._cached_preview_source() is not None:
            return "Preview restored from the verified watch package cache."
        return self.selection_detail

    def with_watch_state(
        self, watch_state, cached_preview=_UNSET_PREVIEW
    ) -> "PetWindowModel":
        return dataclasses.replace(
            self,
            watch_state=watch_state,
            cached_preview=(
                self.cached_preview
                if cached_preview is _UNSET_PREVIEW
                else cached_preview
            ),
        )

    def with_snapshot(
        self, snapshot, cached_preview=_UNSET_PREVIEW
    ) -> "PetWindowModel":
        return self.with_watch_state(snapshot, cached_preview)

    def _selection_update(self, **changes) -> "PetWindowModel":
        """Replace local chooser state without retiring a live upload."""
        if self.active_request_id is None:
            changes.update(
                active_conversion=None,
                transfer_phase=None,
                transfer_error_code="",
                transfer_detail="",
                acknowledged_bytes=0,
                total_bytes=0,
            )
        return dataclasses.replace(self, **changes)

    def with_selected_path(self, path: pathlib.Path) -> "PetWindowModel":
        selected = pathlib.Path(path)
        if selected.suffix.lower() != ".gif":
            return self._selection_update(
                conversion=None,
                selected_path=None,
                selection_phase="error",
                selection_error_code="wrong_extension",
                selection_detail="Only one .gif animation can be sent to the watch.",
            )
        return self.with_processing(selected)

    def with_processing(self, path: pathlib.Path) -> "PetWindowModel":
        return self._selection_update(
            conversion=None,
            selected_path=pathlib.Path(path),
            selection_phase="processing",
            selection_error_code="",
            selection_detail="Converting GIF into the watch package…",
        )

    def with_conversion(self, conversion: ConvertedPet) -> "PetWindowModel":
        if not isinstance(conversion, ConvertedPet):
            raise TypeError("conversion must be a ConvertedPet")
        return self._selection_update(
            conversion=conversion,
            selection_phase="ready",
            selection_error_code="",
            selection_detail="Preview is reconstructed from the final watch package.",
        )

    def with_conversion_error(self, error) -> "PetWindowModel":
        if isinstance(error, PetConversionError):
            detail = error.detail
            code = error.code
        else:
            detail = "Could not convert this GIF."
            code = "conversion_failed"
        return self._selection_update(
            conversion=None,
            selection_phase="error",
            selection_error_code=code,
            selection_detail=detail,
        )

    def with_transfer_started(
        self,
        request_id: str,
        total_bytes: int,
        phase: str = "sending",
        transfer_conversion=_USE_SELECTED_CONVERSION,
    ) -> "PetWindowModel":
        if not isinstance(request_id, str) or not request_id:
            raise ValueError("request id is required")
        if self.active_request_id is not None:
            raise RuntimeError("a pet transfer is already active")
        if transfer_conversion is _USE_SELECTED_CONVERSION:
            transfer_conversion = self.conversion
        if transfer_conversion is not None and not isinstance(
            transfer_conversion, ConvertedPet
        ):
            raise TypeError("transfer conversion must be a ConvertedPet")
        return dataclasses.replace(
            self,
            active_conversion=transfer_conversion,
            transfer_phase=phase,
            acknowledged_bytes=0,
            total_bytes=max(0, int(total_bytes)),
            transfer_error_code="",
            transfer_detail="Waiting for the watch to acknowledge progress.",
            active_request_id=request_id,
        )

    def with_progress(self, progress) -> "PetWindowModel":
        request_id = getattr(progress, "request_id", "")
        # A callback is meaningful only while its request is still live.  In
        # particular, no callback may take ownership after a selection change
        # or after a terminal result retires the request id.
        if (
            self.active_request_id is None
            or request_id != self.active_request_id
        ):
            return self
        phase = str(getattr(progress, "phase", "error"))
        total = max(0, int(getattr(progress, "total_bytes", 0)))
        acknowledged = max(0, min(total, int(getattr(progress, "acknowledged_bytes", 0))))
        error_code = str(getattr(progress, "error_code", ""))
        if error_code == "busy":
            phase = "busy"
        elif phase == "error" and error_code == "cancelled":
            phase = "cancelled"
        active_request_id = (
            None if phase in _TERMINAL_TRANSFER_PHASES else self.active_request_id
        )
        detail = self._progress_detail(phase, acknowledged, total, error_code)
        return dataclasses.replace(
            self,
            transfer_phase=phase,
            acknowledged_bytes=acknowledged,
            total_bytes=total,
            transfer_error_code=error_code,
            transfer_detail=detail,
            active_request_id=active_request_id,
        )

    @staticmethod
    def _progress_detail(
        phase: str, acknowledged: int, total: int, error_code: str
    ) -> str:
        if phase == "sending":
            return "{} of {} bytes acknowledged".format(
                _format_bytes(acknowledged), _format_bytes(total)
            )
        if phase == "verifying":
            return "The watch is validating the complete package."
        if phase == "applying":
            return "The watch is applying the new pet."
        if phase == "installed":
            return "The watch acknowledged the installed package."
        if phase == "busy":
            return "Wait for the current pet operation to finish."
        if phase == "cancelled":
            return "The current upload was cancelled before installation."
        if phase == "error":
            return error_code.replace("_", " ") or "The watch did not accept the pet."
        return ""

    def _authoritative_state_matches(self, package) -> bool:
        state = self.watch_state
        return (
            package is not None
            and _capability_value(state) == "ready"
            and getattr(state, "usable", False) is True
            and getattr(state, "source", "built-in") == "custom"
            and getattr(state, "active_hash", None) == package.sha256
        )

    def _cached_preview_source(self):
        preview = self.cached_preview
        package = getattr(preview, "package", None)
        frames = getattr(preview, "rgba_frames", None)
        if (
            self._authoritative_state_matches(package)
            and isinstance(frames, tuple)
            and frames
        ):
            return preview
        return None

    @property
    def _preview_source(self):
        if self.active_request_id is not None and self.active_conversion is not None:
            return self.active_conversion
        cached = self._cached_preview_source()
        if cached is not None:
            return cached
        if self.transfer_phase == "installed":
            if self._authoritative_state_matches(
                getattr(self.active_conversion, "package", None)
            ):
                return self.active_conversion
            return None
        if getattr(self.watch_state, "source", "built-in") == "custom":
            if self._authoritative_state_matches(
                getattr(self.conversion, "package", None)
            ):
                return self.conversion
            return None
        return self.conversion

    @property
    def preview(self):
        source = self._preview_source
        return getattr(source, "preview_rgba", getattr(source, "rgba_frames", None))

    @property
    def preview_durations_ms(self):
        source = self._preview_source
        package = getattr(source, "package", None)
        if package is None:
            return ()
        return tuple(frame.duration_ms for frame in package.frames)

    @property
    def metadata_text(self) -> str:
        source = self._preview_source
        if source is None:
            if getattr(self.watch_state, "source", "built-in") == "custom":
                return "No verified watch preview available"
            return "No converted pet selected"
        metadata = getattr(source, "metadata", None)
        if metadata is not None:
            return "{} frames · {:.2f} s · {} × {} · {}".format(
                metadata.output_frames,
                metadata.loop_duration_ms / 1000,
                metadata.width,
                metadata.height,
                _format_bytes(metadata.package_bytes),
            )
        package = source.package
        return "{} frames · {:.2f} s · {} × {} · {}".format(
            len(package.frames),
            sum(frame.duration_ms for frame in package.frames) / 1000,
            package.width,
            package.height,
            _format_bytes(len(package.encoded)),
        )

    @property
    def progress_fraction(self) -> float:
        if self.total_bytes <= 0:
            return 0.0
        return self.acknowledged_bytes / self.total_bytes

    @property
    def can_send(self) -> bool:
        state = self.watch_state
        return (
            self.conversion is not None
            and self.selection_phase == "ready"
            and self.active_request_id is None
            and self.transfer_phase != "busy"
            and _capability_value(state) == "ready"
            and getattr(state, "schema_min", 0) <= 1 <= getattr(state, "schema_max", 0)
        )

    @property
    def can_cancel(self) -> bool:
        return (
            self.active_request_id is not None
            and self.transfer_phase in _ACTIVE_TRANSFER_PHASES
        )

    @property
    def can_restore(self) -> bool:
        state = self.watch_state
        return (
            self.active_request_id is None
            and self.transfer_phase != "busy"
            and _capability_value(state) == "ready"
            and getattr(state, "usable", False) is True
            and getattr(state, "source", "built-in") == "custom"
        )

    @property
    def headline(self) -> str:
        if self.transfer_phase in _TRANSFER_HEADLINES:
            return _TRANSFER_HEADLINES[self.transfer_phase]
        if self.selection_phase == "processing":
            return "Processing GIF…"
        if (
            self.selection_phase == "error"
            and self.selection_error_code == "wrong_extension"
        ):
            return "Choose a GIF file"
        if (
            self.selection_phase == "error"
            and self.selection_error_code.startswith("conversion")
        ):
            return "Could not process GIF"
        if self.selection_phase == "error" and self.conversion is None:
            return "Could not process GIF"
        if self.watch_state is None:
            return "Choose a GIF to preview"
        capability = _capability_value(self.watch_state)
        if capability == "unsupported":
            return "Firmware update required"
        if capability == "authenticating":
            return "Authenticating watch…"
        if capability != "ready":
            return "Watch offline"
        if self.conversion is not None:
            return "Ready to send to watch"
        if getattr(self.watch_state, "source", "built-in") == "custom":
            return "Custom pet installed on watch"
        return "Choose a GIF to preview"

    @property
    def connection_text(self) -> str:
        capability = _capability_value(self.watch_state)
        if capability == "ready":
            return "Authenticated watch connected"
        if capability == "unsupported":
            return "Firmware update required"
        if capability == "authenticating":
            return "Authenticating watch"
        return "Watch offline — preview is still available"


# Option A design contract (inferred from the approved browser mock): a dark
# compact surface, black circular watch, light type, blue primary action and
# restrained green connection status.  Native AppKit buttons deliberately keep
# their standard focus behavior and accessible role rather than emulating HTML.
WINDOW_CONTENT_SIZE = (480, 600)
WINDOW_MIN_SIZE = (480, 600)
WINDOW_MAX_SIZE = (560, 680)
WATCH_PREVIEW_SIZE = 220
WINDOW_BACKGROUND = (0.082, 0.094, 0.129, 1.0)
PANEL_BACKGROUND = (0.106, 0.122, 0.164, 1.0)
PANEL_BORDER = (0.227, 0.251, 0.306, 1.0)
PRIMARY_BLUE = (0.204, 0.471, 0.965, 1.0)
MINT = (0.325, 0.859, 0.706, 1.0)
MUTED = (0.573, 0.600, 0.659, 1.0)


def _color(rgba):
    return NSColor.colorWithCalibratedRed_green_blue_alpha_(*rgba)


def _label(frame, value, *, size=12, weight=0.0, color=None):
    label = NSTextField.labelWithString_(value)
    label.setFrame_(frame)
    label.setFont_(NSFont.systemFontOfSize_weight_(size, weight))
    label.setTextColor_(color or NSColor.labelColor())
    label.setLineBreakMode_(5)  # NSLineBreakByTruncatingTail without another import
    label.setAccessibilityElement_(True)
    return label


def _image_from_rgba(rgba: bytes, width: int, height: int):
    if len(rgba) != width * height * 4:
        return None
    bitmap = NSBitmapImageRep.alloc().initWithBitmapDataPlanes_pixelsWide_pixelsHigh_bitsPerSample_samplesPerPixel_hasAlpha_isPlanar_colorSpaceName_bitmapFormat_bytesPerRow_bitsPerPixel_(
        None,
        width,
        height,
        8,
        4,
        True,
        False,
        NSDeviceRGBColorSpace,
        NSBitmapFormatAlphaNonpremultiplied,
        width * 4,
        32,
    )
    if bitmap is None:
        return None
    bitmap.bitmapData()[: len(rgba)] = rgba
    image = NSImage.alloc().initWithSize_((width, height))
    image.addRepresentation_(bitmap)
    return image


class RoundWatchPreviewView(NSView):
    """A round watch enclosure that draws package-derived RGBA frames."""

    def initWithFrame_(self, frame):
        self = objc.super(RoundWatchPreviewView, self).initWithFrame_(frame)
        if self is None:
            return None
        self._preview_image = None
        self.setAccessibilityLabel_("Converted watch pet preview")
        return self

    def setPreviewImage_(self, image):
        self._preview_image = image
        self.setNeedsDisplay_(True)

    def drawRect_(self, _dirty_rect):
        bounds = self.bounds()
        outer = NSBezierPath.bezierPathWithOvalInRect_(((4, 4), (bounds.size.width - 8, bounds.size.height - 8)))
        _color((0.200, 0.220, 0.267, 1.0)).setFill()
        outer.fill()
        _color((0.318, 0.345, 0.400, 1.0)).setStroke()
        outer.setLineWidth_(2.0)
        outer.stroke()
        face = NSBezierPath.bezierPathWithOvalInRect_(((12, 12), (bounds.size.width - 24, bounds.size.height - 24)))
        NSColor.blackColor().setFill()
        face.fill()
        if self._preview_image is not None:
            image_frame = ((38, 32), (144, 156))
            self._preview_image.drawInRect_fromRect_operation_fraction_(
                image_frame,
                ((0, 0), (144, 156)),
                NSCompositingOperationSourceOver,
                1.0,
            )


class GifDropTargetView(NSView):
    """Native file drop target limited to exactly one GIF path."""

    def initWithFrame_handler_(self, frame, handler):
        self = objc.super(GifDropTargetView, self).initWithFrame_(frame)
        if self is None:
            return None
        self._handler = handler
        self._highlighted = False
        self.registerForDraggedTypes_([NSFilenamesPboardType])
        self.setAccessibilityLabel_("Drop one GIF file here")
        return self

    @objc.python_method
    def _paths(self, sender):
        values = sender.draggingPasteboard().propertyListForType_(NSFilenamesPboardType)
        return tuple(pathlib.Path(value) for value in values or () if isinstance(value, str))

    def draggingEntered_(self, sender):
        paths = self._paths(sender)
        accepted = len(paths) == 1 and paths[0].suffix.lower() == ".gif"
        self._highlighted = accepted
        self.setNeedsDisplay_(True)
        return NSDragOperationCopy if accepted else NSDragOperationNone

    def draggingExited_(self, _sender):
        self._highlighted = False
        self.setNeedsDisplay_(True)

    def performDragOperation_(self, sender):
        paths = self._paths(sender)
        self._highlighted = False
        self.setNeedsDisplay_(True)
        if len(paths) != 1 or paths[0].suffix.lower() != ".gif":
            return False
        self._handler(paths[0])
        return True

    def drawRect_(self, _dirty_rect):
        bounds = self.bounds()
        border = NSBezierPath.bezierPathWithRoundedRect_xRadius_yRadius_(bounds, 10, 10)
        _color(PANEL_BACKGROUND).setFill()
        border.fill()
        _color(PRIMARY_BLUE if self._highlighted else PANEL_BORDER).setStroke()
        border.setLineWidth_(2.0 if self._highlighted else 1.0)
        border.stroke()


class CodexPetWindowController(NSWindowController):
    """One retained AppKit window observing runtime-owned pet transfer state."""

    def initWithAgentController_(self, agent_controller):
        style = (
            NSWindowStyleMaskTitled
            | NSWindowStyleMaskClosable
            | NSWindowStyleMaskMiniaturizable
            | NSWindowStyleMaskResizable
        )
        window = NSWindow.alloc().initWithContentRect_styleMask_backing_defer_(
            ((0, 0), WINDOW_CONTENT_SIZE), style, NSBackingStoreBuffered, False
        )
        self = objc.super(CodexPetWindowController, self).initWithWindow_(window)
        if self is None:
            return None
        self.agent_controller = agent_controller
        self._model = PetWindowModel().with_snapshot(
            agent_controller.pet_snapshot(), agent_controller.pet_preview()
        )
        self._preview_frames = None
        self._preview_frame_index = 0
        self._preview_timer = None
        self._conversion_generation = 0
        self._conversion_worker = None
        self._call_after = AppHelper.callAfter
        window.setTitle_("Codex Pet")
        window.setBackgroundColor_(_color(WINDOW_BACKGROUND))
        window.setContentMinSize_(WINDOW_MIN_SIZE)
        window.setContentMaxSize_(WINDOW_MAX_SIZE)
        window.setReleasedWhenClosed_(False)
        window.setDelegate_(self)
        self._build_window()
        self._render_model(self._model)
        return self

    @objc.python_method
    def _build_window(self):
        content = self.window().contentView()

        self.title_label = _label(
            ((24, 554), (300, 26)), "Codex Pet", size=20, weight=NSFontWeightSemibold
        )
        self.subtitle_label = _label(
            ((24, 534), (432, 18)),
            "Preview the exact animation package sent to your watch.",
            size=12,
            color=_color(MUTED),
        )
        content.addSubview_(self.title_label)
        content.addSubview_(self.subtitle_label)

        self.watch_preview = RoundWatchPreviewView.alloc().initWithFrame_(
            ((20, 294), (WATCH_PREVIEW_SIZE, WATCH_PREVIEW_SIZE))
        )
        content.addSubview_(self.watch_preview)

        self.headline_label = _label(
            ((260, 464), (196, 40)), "Choose a GIF to preview", size=16, weight=NSFontWeightSemibold
        )
        self.headline_label.setLineBreakMode_(0)
        self.metadata_label = _label(
            ((260, 412), (196, 44)), "No converted pet selected", size=12, color=_color(MUTED)
        )
        self.metadata_label.setLineBreakMode_(0)
        self.detail_label = _label(
            ((260, 342), (196, 60)), "", size=12, color=_color(MUTED)
        )
        self.detail_label.setLineBreakMode_(0)
        content.addSubview_(self.headline_label)
        content.addSubview_(self.metadata_label)
        content.addSubview_(self.detail_label)

        self.drop_target = GifDropTargetView.alloc().initWithFrame_handler_(
            ((24, 210), (432, 66)), self._begin_conversion
        )
        self.drop_label = _label(
            ((14, 33), (245, 20)), "Drop one GIF here", size=13, weight=NSFontWeightSemibold
        )
        self.drop_hint_label = _label(
            ((14, 14), (245, 16)), "Transparent background is preserved.", size=11, color=_color(MUTED)
        )
        self.choose_button = NSButton.alloc().initWithFrame_(((288, 18), (126, 30)))
        self.choose_button.setTitle_("Choose GIF…")
        self.choose_button.setBezelStyle_(NSBezelStyleRounded)
        self.choose_button.setControlSize_(NSControlSizeSmall)
        self.choose_button.setTarget_(self)
        self.choose_button.setAction_("chooseGif:")
        self.choose_button.setAccessibilityLabel_("Choose GIF file")
        self.drop_target.addSubview_(self.drop_label)
        self.drop_target.addSubview_(self.drop_hint_label)
        self.drop_target.addSubview_(self.choose_button)
        content.addSubview_(self.drop_target)

        self.connection_label = _label(
            ((24, 178), (432, 20)), "Watch offline — preview is still available", size=12, color=_color(MINT)
        )
        content.addSubview_(self.connection_label)
        self.progress_indicator = NSProgressIndicator.alloc().initWithFrame_(((24, 146), (432, 8)))
        self.progress_indicator.setIndeterminate_(False)
        self.progress_indicator.setMinValue_(0.0)
        self.progress_indicator.setMaxValue_(1.0)
        self.progress_indicator.setDoubleValue_(0.0)
        self.progress_indicator.setAccessibilityLabel_("Pet transfer progress")
        content.addSubview_(self.progress_indicator)
        self.progress_label = _label(
            ((24, 120), (432, 18)), "No transfer in progress", size=11, color=_color(MUTED)
        )
        content.addSubview_(self.progress_label)

        self.restore_button = NSButton.alloc().initWithFrame_(((24, 54), (128, 32)))
        self.restore_button.setTitle_("Restore Default")
        self.restore_button.setBezelStyle_(NSBezelStyleRounded)
        self.restore_button.setTarget_(self)
        self.restore_button.setAction_("restoreDefault:")
        self.restore_button.setAccessibilityLabel_("Restore built-in pet")
        content.addSubview_(self.restore_button)
        self.cancel_button = NSButton.alloc().initWithFrame_(((24, 54), (128, 32)))
        self.cancel_button.setTitle_("Cancel")
        self.cancel_button.setBezelStyle_(NSBezelStyleRounded)
        self.cancel_button.setTarget_(self)
        self.cancel_button.setAction_("cancelTransfer:")
        self.cancel_button.setAccessibilityLabel_("Cancel pet transfer")
        content.addSubview_(self.cancel_button)
        self.send_button = NSButton.alloc().initWithFrame_(((306, 54), (150, 32)))
        self.send_button.setTitle_("Send to Watch")
        self.send_button.setBezelStyle_(NSBezelStyleRounded)
        self.send_button.setTarget_(self)
        self.send_button.setAction_("sendToWatch:")
        self.send_button.setKeyEquivalent_("\r")
        self.send_button.setAccessibilityLabel_("Send converted pet to watch")
        content.addSubview_(self.send_button)

    def showWindow_(self, sender):
        objc.super(CodexPetWindowController, self).showWindow_(sender)
        self.window().makeKeyAndOrderFront_(sender)
        self._start_preview_timer_if_visible()

    def windowDidBecomeKey_(self, _notification):
        self._start_preview_timer_if_visible()

    def windowDidMiniaturize_(self, _notification):
        self.stopPreviewTimer()

    def windowDidDeminiaturize_(self, _notification):
        self._start_preview_timer_if_visible()

    def windowWillClose_(self, _notification):
        # Closing this retained view never cancels its runtime-owned request.
        self.stopPreviewTimer()

    def chooseGif_(self, _sender):
        panel = NSOpenPanel.openPanel()
        panel.setCanChooseFiles_(True)
        panel.setCanChooseDirectories_(False)
        panel.setAllowsMultipleSelection_(False)
        panel.setAllowedFileTypes_(["gif"])

        def completed(result):
            if result != NSModalResponseOK:
                return
            url = panel.URL()
            if url is not None:
                self._begin_conversion(pathlib.Path(str(url.path())))

        panel.beginSheetModalForWindow_completionHandler_(self.window(), completed)

    def sendToWatch_(self, _sender):
        self._submit_selected_package()

    def cancelTransfer_(self, _sender):
        self._cancel_active_request()

    def restoreDefault_(self, _sender):
        self._submit_restore()

    def applySnapshot_(self, snapshot):
        self._call_after(self._apply_snapshot_on_main, snapshot, _UNSET_PREVIEW)

    @objc.python_method
    def applySnapshotAndPreview(self, snapshot, preview):
        """Accept one authoritative snapshot/cache-preview pair on main."""
        self._call_after(self._apply_snapshot_on_main, snapshot, preview)

    @objc.python_method
    def _apply_snapshot_on_main(self, snapshot, cached_preview):
        self._model = self._model.with_snapshot(snapshot, cached_preview)
        self._render_model(self._model)

    @objc.python_method
    def _begin_conversion(self, path: pathlib.Path):
        self._conversion_generation += 1
        generation = self._conversion_generation
        self._model = self._model.with_selected_path(path)
        self._render_model(self._model)
        if self._model.phase != "processing":
            return
        worker = threading.Thread(
            target=self._convert_on_worker,
            args=(generation, pathlib.Path(path)),
            name="codex-pet-converter",
            daemon=True,
        )
        self._conversion_worker = worker
        worker.start()

    @objc.python_method
    def _convert_on_worker(self, generation: int, path: pathlib.Path):
        from pet_converter import convert_gif

        try:
            converted = convert_gif(path)
            error = None
        except PetConversionError as caught:
            converted = None
            error = caught
        except Exception:
            converted = None
            error = PetConversionError("conversion_failed", "Could not convert this GIF.")
        self._call_after(self._finish_conversion_on_main, generation, converted, error)

    @objc.python_method
    def _finish_conversion_on_main(self, generation: int, converted, error):
        if generation != self._conversion_generation:
            return
        self._conversion_worker = None
        self._model = (
            self._model.with_conversion_error(error)
            if error is not None
            else self._model.with_conversion(converted)
        )
        self._render_model(self._model)

    @objc.python_method
    def _submit_selected_package(self):
        if not self._model.can_send:
            self._render_model(self._model)
            return
        package = self._model.conversion.package
        request_id = self.agent_controller.upload_pet(package, self._receive_progress)
        self._model = self._model.with_transfer_started(request_id, len(package.encoded))
        self._render_model(self._model)

    @objc.python_method
    def _cancel_active_request(self):
        if not self._model.can_cancel:
            return
        if not self.agent_controller.cancel_pet(self._model.active_request_id):
            failed = type(
                "PetProgressFailure",
                (),
                {
                    "request_id": self._model.active_request_id,
                    "phase": "error",
                    "acknowledged_bytes": self._model.acknowledged_bytes,
                    "total_bytes": self._model.total_bytes,
                    "error_code": "cancel_failed",
                },
            )()
            self._model = self._model.with_progress(failed)
            self._render_model(self._model)

    @objc.python_method
    def _submit_restore(self):
        if not self._model.can_restore:
            self._render_model(self._model)
            return
        request_id = self.agent_controller.restore_default(self._receive_progress)
        self._model = self._model.with_transfer_started(
            request_id, 0, "applying", transfer_conversion=None
        )
        self._render_model(self._model)

    @objc.python_method
    def _receive_progress(self, progress):
        self._call_after(self._apply_progress_on_main, progress)

    @objc.python_method
    def _apply_progress_on_main(self, progress):
        self._model = self._model.with_progress(progress)
        self._render_model(self._model)

    @objc.python_method
    def _render_model(self, model: PetWindowModel):
        self.headline_label.setStringValue_(model.headline)
        self.metadata_label.setStringValue_(model.metadata_text)
        self.detail_label.setStringValue_(model.detail)
        self.connection_label.setStringValue_(model.connection_text)
        self.progress_indicator.setDoubleValue_(model.progress_fraction)
        self.progress_label.setStringValue_(
            model.detail
            if model.transfer_phase in _TRANSFER_HEADLINES
            else "No transfer in progress"
        )
        self.send_button.setEnabled_(model.can_send)
        self.restore_button.setEnabled_(model.can_restore)
        self.cancel_button.setEnabled_(model.can_cancel)
        self.cancel_button.setHidden_(not model.can_cancel)
        self.restore_button.setHidden_(model.can_cancel)
        if model.preview is not self._preview_frames:
            self._preview_frames = model.preview
            self._preview_frame_index = 0
            self._show_preview_frame()
            self.stopPreviewTimer()
        self._start_preview_timer_if_visible()

    @objc.python_method
    def _show_preview_frame(self):
        if not self._preview_frames:
            self.watch_preview.setPreviewImage_(None)
            return
        frame = self._preview_frames[self._preview_frame_index]
        self.watch_preview.setPreviewImage_(_image_from_rgba(frame, 144, 156))

    @objc.python_method
    def _start_preview_timer_if_visible(self):
        if (
            self._preview_timer is not None
            or not self._preview_frames
            or len(self._preview_frames) < 2
            or not self.window().isVisible()
            or self.window().isMiniaturized()
        ):
            return
        durations = self._model.preview_durations_ms
        if self._preview_frame_index >= len(durations):
            self._preview_frame_index = 0
        duration = durations[self._preview_frame_index] / 1000.0
        self._preview_timer = NSTimer.scheduledTimerWithTimeInterval_target_selector_userInfo_repeats_(
            duration, self, "advancePreview:", None, False
        )

    def advancePreview_(self, _timer):
        self._preview_timer = None
        if not self.window().isVisible() or not self._preview_frames:
            return
        self._preview_frame_index = (self._preview_frame_index + 1) % len(self._preview_frames)
        self._show_preview_frame()
        self._start_preview_timer_if_visible()

    @objc.python_method
    def stopPreviewTimer(self):
        if self._preview_timer is not None:
            self._preview_timer.invalidate()
            self._preview_timer = None
