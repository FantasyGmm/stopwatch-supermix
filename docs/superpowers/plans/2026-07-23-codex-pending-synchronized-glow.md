# Codex Pending Synchronized Glow Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Render all three Codex Pending rings as one smooth, synchronized 1.2-second breath without repeatedly invalidating the full screen.

**Architecture:** Keep Pending state ownership in `AppCodexStatus`, but replace `CodexPendingSoftGlow`'s three LVGL objects with one transparent custom-drawn overlay. Put timing, ring geometry, opacity, and edge-band geometry in host-testable logic; each animation step stores one shared opacity and invalidates only four 20 px edge regions plus the existing heading.

**Tech Stack:** C++17, LVGL 9 custom draw events and animations, Python `unittest` firmware contracts, ESP-IDF 5.5.4, existing unified build/flash scripts.

---

## File Map

- Modify `main/apps/app_codex_status/codex_pending_glow_logic.h`
  - Own pure timing, opacity, ring geometry, and invalidation geometry.
- Modify `tests/codex_pending_glow_logic_test.cpp`
  - Verify the pure synchronized-breath contract on the host.
- Modify `scripts/run_unified_offline_validation.sh`
  - Make the host glow test part of the maintained offline gate.
- Modify `mac-codex-agent/tests/test_codex_pending_glow_firmware_contract.py`
  - Verify the LVGL component uses one custom overlay and bounded invalidation.
- Modify `main/apps/app_codex_status/codex_pending_soft_glow.h`
  - Replace the three object pointers with one overlay and one shared opacity.
- Modify `main/apps/app_codex_status/codex_pending_soft_glow.cpp`
  - Draw all rings in one callback and invalidate only the four edge bands.

### Task 1: Define the host-testable synchronized breath

**Files:**
- Modify: `tests/codex_pending_glow_logic_test.cpp`
- Modify: `main/apps/app_codex_status/codex_pending_glow_logic.h`
- Modify: `scripts/run_unified_offline_validation.sh`

- [ ] **Step 1: Extend the host test before changing production logic**

Add these assertions to `tests/codex_pending_glow_logic_test.cpp` after the
existing transition assertions:

```cpp
    using namespace codex_pending_glow;

    static_assert(kHalfCycleMs == 600);
    static_assert(kFullCycleMs == 1200);
    static_assert(kPanelSize == 466);
    static_assert(kEdgeBandSize == 20);
    static_assert(kRings.size() == 3);
    static_assert(kInvalidationAreas.size() == 4);

    assert(opacity_for_phase(-1) == kMinimumOpacity);
    assert(opacity_for_phase(0) == kMinimumOpacity);
    assert(opacity_for_phase(128) > kMinimumOpacity);
    assert(opacity_for_phase(255) == kMaximumOpacity);
    assert(opacity_for_phase(300) == kMaximumOpacity);

    for (const RingGeometry& ring : kRings) {
        assert(ring.inset >= 0);
        assert(ring.size > 0);
        assert(ring.border_width > 0);
        assert(ring.inset + ring.size <= kPanelSize);
        assert(ring.inset + ring.border_width <= kEdgeBandSize);
    }

    assert((kInvalidationAreas[0] == EdgeArea{0, 0, 465, 19}));
    assert((kInvalidationAreas[1] == EdgeArea{0, 446, 465, 465}));
    assert((kInvalidationAreas[2] == EdgeArea{0, 20, 19, 445}));
    assert((kInvalidationAreas[3] == EdgeArea{446, 20, 465, 445}));
```

- [ ] **Step 2: Run the host test and verify the RED state**

Run:

```bash
mkdir -p /tmp/stopwatch-glow-plan
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/codex_pending_glow_logic_test.cpp \
  -o /tmp/stopwatch-glow-plan/codex_pending_glow_logic_test
```

Expected: compilation fails because `kHalfCycleMs`, `kRings`,
`opacity_for_phase`, `EdgeArea`, and the other new contract members do not yet
exist.

- [ ] **Step 3: Add the minimal pure logic**

Extend `main/apps/app_codex_status/codex_pending_glow_logic.h` with:

```cpp
#include <array>
#include <cstdint>

constexpr int kPanelSize = 466;
constexpr int kHalfCycleMs = 600;
constexpr int kFullCycleMs = kHalfCycleMs * 2;
constexpr int kEdgeBandSize = 20;
constexpr uint8_t kMinimumOpacity = 102;  // LV_OPA_40
constexpr uint8_t kMaximumOpacity = 255;  // LV_OPA_COVER

struct RingGeometry {
    int inset;
    int size;
    int border_width;
};

struct EdgeArea {
    int x1;
    int y1;
    int x2;
    int y2;
};

constexpr bool operator==(EdgeArea lhs, EdgeArea rhs)
{
    return lhs.x1 == rhs.x1 && lhs.y1 == rhs.y1 &&
           lhs.x2 == rhs.x2 && lhs.y2 == rhs.y2;
}

inline constexpr std::array<RingGeometry, 3> kRings{{
    {1, 464, 2},
    {5, 456, 4},
    {11, 444, 6},
}};

inline constexpr std::array<EdgeArea, 4> kInvalidationAreas{{
    {0, 0, 465, 19},
    {0, 446, 465, 465},
    {0, 20, 19, 445},
    {446, 20, 465, 445},
}};

constexpr uint8_t opacity_for_phase(int32_t phase)
{
    const int32_t bounded = phase < 0 ? 0 : (phase > 255 ? 255 : phase);
    return static_cast<uint8_t>(
        kMinimumOpacity +
        bounded * (kMaximumOpacity - kMinimumOpacity) / 255
    );
}
```

Keep `desired_active(...)` and `transition_for(...)` unchanged.

- [ ] **Step 4: Run the host test and verify the GREEN state**

Run:

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/codex_pending_glow_logic_test.cpp \
  -o /tmp/stopwatch-glow-plan/codex_pending_glow_logic_test
/tmp/stopwatch-glow-plan/codex_pending_glow_logic_test
```

Expected: exit code 0 with no compiler warnings or assertion failures.

- [ ] **Step 5: Add the host test to the maintained offline gate**

In `scripts/run_unified_offline_validation.sh`, insert after the Python suite:

```bash
echo "[2/19] Codex pending synchronized glow host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/codex_pending_glow_logic_test.cpp \
    -o "$HOST_TEST_DIR/codex_pending_glow_logic_test"
"$HOST_TEST_DIR/codex_pending_glow_logic_test"
```

Change the original step labels `[2/18]` through `[18/18]` to `[3/19]`
through `[19/19]`. Do not change the commands in those existing steps.

- [ ] **Step 6: Verify the gate contains and runs the focused test**

Run:

```bash
rg -n "Codex pending synchronized glow|codex_pending_glow_logic_test|/19\\]" \
  scripts/run_unified_offline_validation.sh
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/codex_pending_glow_logic_test.cpp \
  -o /tmp/stopwatch-glow-plan/codex_pending_glow_logic_test
/tmp/stopwatch-glow-plan/codex_pending_glow_logic_test
```

Expected: the script contains one compile/run block for the glow test, every
progress label has denominator 19, and the focused binary exits 0.

- [ ] **Step 7: Commit the pure behavior**

```bash
git add \
  main/apps/app_codex_status/codex_pending_glow_logic.h \
  tests/codex_pending_glow_logic_test.cpp \
  scripts/run_unified_offline_validation.sh
git diff --cached --check
git commit -m "Define synchronized Codex glow timing"
```

### Task 2: Replace three full-screen ring objects with one edge overlay

**Files:**
- Modify: `mac-codex-agent/tests/test_codex_pending_glow_firmware_contract.py`
- Modify: `main/apps/app_codex_status/codex_pending_soft_glow.h`
- Modify: `main/apps/app_codex_status/codex_pending_soft_glow.cpp`

- [ ] **Step 1: Rewrite the firmware contract for the chosen architecture**

Replace the first two tests in
`mac-codex-agent/tests/test_codex_pending_glow_firmware_contract.py` with
contracts that assert:

```python
def test_soft_glow_uses_one_custom_overlay_and_one_shared_phase(self):
    header = (
        ROOT / "main/apps/app_codex_status/codex_pending_soft_glow.h"
    ).read_text()
    source = (
        ROOT / "main/apps/app_codex_status/codex_pending_soft_glow.cpp"
    ).read_text()
    logic = (
        ROOT / "main/apps/app_codex_status/codex_pending_glow_logic.h"
    ).read_text()

    self.assertIn("lv_obj_t* _overlay", header)
    self.assertIn("lv_opa_t _opacity", header)
    self.assertNotIn("std::array<lv_obj_t*, 3> _rings", header)
    self.assertIn("static void draw_event_cb", header)
    self.assertIn("void invalidate_edges()", header)
    self.assertIn("LV_EVENT_DRAW_MAIN_BEGIN", source)
    self.assertIn("for (const auto& ring : codex_pending_glow::kRings)", source)
    self.assertIn("lv_draw_rect(layer, &dsc, &area)", source)
    self.assertEqual(source.count("_opacity ="), 1)
    self.assertIn("codex_pending_glow::opacity_for_phase(phase)", source)
    self.assertIn("constexpr int kHalfCycleMs = 600", logic)
    self.assertIn("constexpr int kFullCycleMs = kHalfCycleMs * 2", logic)
    self.assertIn(
        "lv_anim_set_duration(&animation, codex_pending_glow::kHalfCycleMs)",
        source,
    )
    self.assertIn(
        "lv_anim_set_playback_duration("
        "&animation, codex_pending_glow::kHalfCycleMs)",
        source,
    )

def test_animation_invalidates_only_edge_bands_and_restores_heading(self):
    source = (
        ROOT / "main/apps/app_codex_status/codex_pending_soft_glow.cpp"
    ).read_text()

    invalidate_body = source.split(
        "void CodexPendingSoftGlow::invalidate_edges()", 1
    )[1]
    invalidate_body = invalidate_body.split(
        "void CodexPendingSoftGlow::start()", 1
    )[0]
    self.assertIn("codex_pending_glow::kInvalidationAreas", invalidate_body)
    self.assertIn("lv_obj_invalidate_area(_overlay, &area)", invalidate_body)
    self.assertNotIn("lv_obj_invalidate(_overlay)", invalidate_body)

    phase_body = source.split(
        "void CodexPendingSoftGlow::apply_phase", 1
    )[1]
    phase_body = phase_body.split(
        "void CodexPendingSoftGlow::invalidate_edges", 1
    )[0]
    self.assertNotIn("lv_obj_set_style_border_opa", phase_body)
    self.assertIn("invalidate_edges()", phase_body)
    self.assertIn("lv_anim_delete(this, animation_cb)", source)
    self.assertIn(
        "lv_obj_add_flag(_overlay, LV_OBJ_FLAG_HIDDEN)", source
    )
    self.assertIn(
        "lv_obj_set_style_opa(_heading, LV_OPA_COVER, 0)", source
    )
```

Keep the third test that verifies `AppCodexStatus` ownership, z-order, and
Pending lifecycle wiring.

- [ ] **Step 2: Run the contract and verify the RED state**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_codex_pending_glow_firmware_contract.py -v
```

Expected: the new overlay tests fail because the component still owns three
ring objects, uses three style changes, and uses 900 ms half-cycles.

- [ ] **Step 3: Change the component state to one overlay**

Replace the private state in
`main/apps/app_codex_status/codex_pending_soft_glow.h` with:

```cpp
    lv_obj_t* _overlay = nullptr;
    lv_obj_t* _heading = nullptr;
    lv_opa_t _opacity = LV_OPA_40;
    bool _active = false;

    void apply_phase(int32_t phase);
    void invalidate_edges();
    void start();
    void stop();
    static void animation_cb(void* target, int32_t phase);
    static void draw_event_cb(lv_event_t* event);
```

Remove the unused `<array>` include.

- [ ] **Step 4: Create the transparent custom-drawn overlay**

In `CodexPendingSoftGlow::init(...)`, create one child of the supplied parent:

```cpp
    _overlay = lv_obj_create(parent);
    lv_obj_set_size(
        _overlay,
        codex_pending_glow::kPanelSize,
        codex_pending_glow::kPanelSize
    );
    lv_obj_set_pos(_overlay, 0, 0);
    lv_obj_set_style_radius(_overlay, 0, 0);
    lv_obj_set_style_pad_all(_overlay, 0, 0);
    lv_obj_set_style_bg_opa(_overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_overlay, 0, 0);
    lv_obj_clear_flag(_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(_overlay, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(
        _overlay,
        draw_event_cb,
        LV_EVENT_ALL,
        this
    );
```

Keep the overlay creation in the existing location before the Codex logo is
created so the effect stays below all content.

- [ ] **Step 5: Draw all rings together from one callback**

Add a local helper in `codex_pending_soft_glow.cpp`:

```cpp
void draw_ring(
    lv_layer_t* layer,
    const lv_area_t& overlay_coords,
    const codex_pending_glow::RingGeometry& ring,
    lv_opa_t opacity
)
{
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_opa = LV_OPA_TRANSP;
    dsc.border_color = lv_color_hex(codex_status_ui::kAlertVermilion);
    dsc.border_opa = opacity;
    dsc.border_width = ring.border_width;
    dsc.radius = LV_RADIUS_CIRCLE;

    lv_area_t area = {
        static_cast<lv_coord_t>(overlay_coords.x1 + ring.inset),
        static_cast<lv_coord_t>(overlay_coords.y1 + ring.inset),
        static_cast<lv_coord_t>(
            overlay_coords.x1 + ring.inset + ring.size - 1
        ),
        static_cast<lv_coord_t>(
            overlay_coords.y1 + ring.inset + ring.size - 1
        ),
    };
    lv_draw_rect(layer, &dsc, &area);
}
```

Implement the callback:

```cpp
void CodexPendingSoftGlow::draw_event_cb(lv_event_t* event)
{
    auto* self = static_cast<CodexPendingSoftGlow*>(
        lv_event_get_user_data(event)
    );
    if (self == nullptr) {
        return;
    }

    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_COVER_CHECK) {
        lv_event_set_cover_res(event, LV_COVER_RES_NOT_COVER);
        return;
    }
    if (code != LV_EVENT_DRAW_MAIN_BEGIN || !self->_active) {
        return;
    }

    lv_area_t coords;
    lv_obj_get_coords(lv_event_get_target_obj(event), &coords);
    lv_layer_t* layer = lv_event_get_layer(event);
    for (const auto& ring : codex_pending_glow::kRings) {
        draw_ring(layer, coords, ring, self->_opacity);
    }
}
```

- [ ] **Step 6: Apply one phase and invalidate four edge bands**

Replace the ring style mutations in `apply_phase(...)` with:

```cpp
    _opacity = static_cast<lv_opa_t>(
        codex_pending_glow::opacity_for_phase(phase)
    );
    if (_heading != nullptr) {
        const lv_opa_t heading = interpolate_opa(
            phase,
            LV_OPA_70,
            LV_OPA_COVER
        );
        lv_obj_set_style_opa(_heading, heading, 0);
    }
    invalidate_edges();
```

Implement bounded invalidation:

```cpp
void CodexPendingSoftGlow::invalidate_edges()
{
    if (_overlay == nullptr) {
        return;
    }

    lv_area_t coords;
    lv_obj_get_coords(_overlay, &coords);
    for (const auto& edge : codex_pending_glow::kInvalidationAreas) {
        lv_area_t area = {
            static_cast<lv_coord_t>(coords.x1 + edge.x1),
            static_cast<lv_coord_t>(coords.y1 + edge.y1),
            static_cast<lv_coord_t>(coords.x1 + edge.x2),
            static_cast<lv_coord_t>(coords.y1 + edge.y2),
        };
        lv_obj_invalidate_area(_overlay, &area);
    }
}
```

- [ ] **Step 7: Preserve activation and shutdown semantics**

Update `start()` to unhide `_overlay`, call `apply_phase(0)`, and configure:

```cpp
    lv_anim_set_duration(
        &animation,
        codex_pending_glow::kHalfCycleMs
    );
    lv_anim_set_playback_duration(
        &animation,
        codex_pending_glow::kHalfCycleMs
    );
```

Keep infinite repeat and `lv_anim_path_ease_in_out`.

Update `stop()` to hide `_overlay` and restore the heading. Update
`shutdown()` to remove the event callback, delete `_overlay`, and null the
pointer:

```cpp
    if (_overlay != nullptr) {
        lv_obj_remove_event_cb_with_user_data(
            _overlay,
            draw_event_cb,
            this
        );
        lv_obj_delete(_overlay);
        _overlay = nullptr;
    }
```

- [ ] **Step 8: Run focused tests and verify the GREEN state**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_codex_pending_glow_firmware_contract.py -v
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/codex_pending_glow_logic_test.cpp \
  -o /tmp/stopwatch-glow-plan/codex_pending_glow_logic_test
/tmp/stopwatch-glow-plan/codex_pending_glow_logic_test
git diff --check
```

Expected: three Python contract tests pass, the host binary exits 0, and the
whitespace check produces no errors.

- [ ] **Step 9: Commit the rendering change**

```bash
git add \
  mac-codex-agent/tests/test_codex_pending_glow_firmware_contract.py \
  main/apps/app_codex_status/codex_pending_soft_glow.h \
  main/apps/app_codex_status/codex_pending_soft_glow.cpp
git diff --cached --check
git commit -m "Render Codex pending glow in one edge overlay"
```

### Task 3: Build, flash, and validate the physical effect

**Files:**
- Verify: `build-unified/StopWatch-UserDemo.bin`
- Verify: `build-unified/generated_assets.bin`
- Verify: `build-unified/unified-runtime-metrics.json`
- Preserve: all unrelated tracked and untracked workspace changes

- [ ] **Step 1: Run the complete offline validation**

Run:

```bash
./scripts/run_unified_offline_validation.sh
```

Expected:

- all Python tests pass;
- all 16 existing host test groups plus the new glow host test pass;
- the clean unified ESP-IDF build exits 0;
- capacity and whitespace gates pass.

- [ ] **Step 2: Record the exact candidate identity**

Run:

```bash
shasum -a 256 \
  build-unified/StopWatch-UserDemo.bin \
  build-unified/generated_assets.bin
git rev-parse HEAD
git status --short
```

Expected: both images have stable SHA-256 values, the implementation commits
are at `HEAD`, and unrelated pre-existing workspace changes remain visible but
unstaged.

- [ ] **Step 3: Verify the connected hardware and preserve the existing backup**

Run:

```bash
ls -l /dev/cu.usbmodem*
test -f hardware-evidence/firmware-backups/2026-07-23-before-pending-soft-glow.bin
shasum -a 256 \
  hardware-evidence/firmware-backups/2026-07-23-before-pending-soft-glow.bin
```

Expected: one usable StopWatch USB serial port is present and the existing
complete 16 MiB backup remains readable. Do not overwrite that backup.

- [ ] **Step 4: Flash the exact clean candidate**

Run with the detected port:

```bash
./scripts/flash_unified_candidate.sh --port /dev/cu.usbmodem1101
```

If the detected suffix changed, substitute only the actual current port.

Expected: bootloader, partition table, app, and assets writes verify
successfully.

- [ ] **Step 5: Verify boot and application startup**

Run:

```bash
python3 scripts/validate_watch_boot.py \
  --port /dev/cu.usbmodem1101 \
  --log hardware-evidence/pending-synchronized-glow-boot.log
python3 scripts/watch_debug.py \
  --port /dev/cu.usbmodem1101 \
  app open Codex
```

Expected: boot validation passes, Launcher starts, and the debug command
acknowledges opening Codex. Use the current detected port if it changed after
reset.

- [ ] **Step 6: Verify BLE status delivery before visual acceptance**

Run:

```bash
tail -n 30 "$HOME/Library/Logs/stopwatch-codex-agent.log"
```

Expected: the latest state is `connected` with no subsequent BLE disconnect or
write error.

- [ ] **Step 7: Perform the real Pending visual gate**

Finish the implementation response with an explicit request for visual
confirmation while the Codex page remains open. The Mac Agent will classify
that completed response as Waiting/Pending and deliver it on the next
two-second status interval.

Observe at least five complete 1.2-second cycles. Accept only when the user
confirms:

- all three rings brighten and dim together;
- the transition looks continuous rather than ring-by-ring;
- the page center does not visibly repaint;
- replying to the Pending task stops the effect.

If any visual condition fails, do not claim completion. Capture the exact
symptom and return to the rendering/invalidation diagnosis before making
another change.
