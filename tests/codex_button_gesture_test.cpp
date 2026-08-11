#include <cassert>
#include <cstdint>

#include "main/apps/app_codex_status/codex_button_gesture.h"

namespace {

void testSingleClickSubmitsAfterDoubleClickWindow() {
    CodexButtonGesture gesture;

    assert(gesture.update(true, false, false, 100U) == CodexButtonAction::None);
    assert(gesture.update(false, false, false, 399U) == CodexButtonAction::None);
    assert(gesture.update(false, false, false, 400U) == CodexButtonAction::Submit);
    assert(gesture.update(false, false, false, 401U) == CodexButtonAction::None);
}

void testDoubleClickForceSteersWithoutSubmittingFirstClick() {
    CodexButtonGesture gesture;

    assert(gesture.update(true, false, false, 100U) == CodexButtonAction::None);
    assert(gesture.update(false, true, false, 200U) == CodexButtonAction::None);
    assert(gesture.update(true, false, false, 201U) == CodexButtonAction::ForceSteer);
    assert(gesture.update(false, false, false, 202U) == CodexButtonAction::None);
}

void testSecondPressHoldCancelsPendingSubmit() {
    CodexButtonGesture gesture;

    assert(gesture.update(true, false, false, 100U) == CodexButtonAction::None);
    assert(gesture.update(false, true, false, 200U) == CodexButtonAction::None);
    assert(gesture.update(false, true, true, 250U) == CodexButtonAction::None);
    assert(gesture.update(false, false, false, 500U) == CodexButtonAction::None);
}

void testSuppressionResetsPendingClick() {
    CodexButtonGesture gesture;

    assert(gesture.update(true, false, false, 100U) == CodexButtonAction::None);
    assert(gesture.update(false, false, false, 150U, true) == CodexButtonAction::None);
    assert(gesture.update(false, false, false, 500U) == CodexButtonAction::None);
}

void testLatchedSecondClickMayReleaseAfterDoubleClickBoundary() {
    CodexButtonGesture gesture;

    assert(gesture.update(true, false, false, 100U) == CodexButtonAction::None);
    assert(gesture.update(false, true, false, 399U) == CodexButtonAction::None);
    assert(gesture.update(true, false, false, 450U) == CodexButtonAction::ForceSteer);
}

void testSingleClickExpiryHandlesUnsignedTimerWrap() {
    CodexButtonGesture gesture;
    constexpr uint32_t first_click_ms = UINT32_MAX - 100U;

    assert(gesture.update(true, false, false, first_click_ms) == CodexButtonAction::None);
    assert(gesture.update(false, false, false, 198U) == CodexButtonAction::None);
    assert(gesture.update(false, false, false, 199U) == CodexButtonAction::Submit);
}

void testResetClearsAllPendingState() {
    CodexButtonGesture gesture;

    assert(gesture.update(true, false, false, 100U) == CodexButtonAction::None);
    assert(gesture.update(false, true, false, 200U) == CodexButtonAction::None);
    gesture.reset();
    assert(gesture.update(false, false, false, 500U) == CodexButtonAction::None);
}

}  // namespace

int main() {
    testSingleClickSubmitsAfterDoubleClickWindow();
    testDoubleClickForceSteersWithoutSubmittingFirstClick();
    testSecondPressHoldCancelsPendingSubmit();
    testSuppressionResetsPendingClick();
    testLatchedSecondClickMayReleaseAfterDoubleClickBoundary();
    testSingleClickExpiryHandlesUnsignedTimerWrap();
    testResetClearsAllPendingState();
    return 0;
}
