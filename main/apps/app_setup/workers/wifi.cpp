/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "workers.h"
#include "wifi_settings_controller.h"

#include <assets/assets.h>
#include <hal/hal.h>

#include <optional>
#include <string>
#include <vector>

using namespace smooth_ui_toolkit::lvgl_cpp;
using namespace setup_workers;

namespace {

constexpr int kScreenSize = 466;
constexpr int kEdgeInset = 40;
constexpr int kContentWidth = kScreenSize - 2 * kEdgeInset;
constexpr int kStatusWidth = 280;
constexpr int kListTop = 150;
constexpr int kListHeight = 140;
constexpr int kRowHeight = 48;
constexpr int kRowGap = 8;
constexpr int kActionHeight = 42;
constexpr int kPrimaryActionWidth = 300;
constexpr int kDoneActionWidth = 200;
constexpr int kPrimaryActionY = 302;
constexpr int kDoneActionY = 356;

const char* statusText(WifiSettingsState state)
{
    switch (state) {
    case WifiSettingsState::NoSaved:
        return "No saved WiFi";
    case WifiSettingsState::NotConnected:
        return "Not connected";
    case WifiSettingsState::Connecting:
        return "Connecting";
    case WifiSettingsState::Connected:
        return "Connected";
    case WifiSettingsState::Pairing:
        return "Pairing";
    case WifiSettingsState::Busy:
        return "WiFi busy";
    case WifiSettingsState::PairingFailed:
        return "Pairing failed";
    }

    return "WiFi";
}

void applyCompactButtonStyle(Button& button, lv_color_t color)
{
    button.setRadius(8);
    button.setBorderWidth(0);
    button.setShadowWidth(0);
    button.setBgColor(color);
    button.setBgOpa(LV_OPA_COVER);
    button.label().setTextFont(&lv_font_montserrat_18);
    button.label().setTextColor(lv_color_hex(0xFFFFFF));
    button.label().setTextAlign(LV_TEXT_ALIGN_CENTER);
    button.label().align(LV_ALIGN_CENTER, 0, 0);
}

} // namespace

namespace setup_workers {

class WifiSettingsWorker::WifiSettingsView {
public:
    WifiSettingsView()
    {
        _panel = std::make_unique<Container>(lv_screen_active());
        _panel->align(LV_ALIGN_CENTER, 0, 0);
        _panel->setSize(kScreenSize, kScreenSize);
        _panel->setRadius(0);
        _panel->setBorderWidth(0);
        _panel->setPaddingAll(0);
        _panel->setBgColor(lv_color_hex(0x000000));
        _panel->setBgOpa(LV_OPA_COVER);
        _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

        _title = std::make_unique<Label>(_panel->get());
        _title->setSize(kStatusWidth, 28);
        _title->align(LV_ALIGN_TOP_MID, 0, 44);
        _title->setText("WiFi");
        _title->setTextFont(&MontserratSemiBold26);
        _title->setTextColor(lv_color_hex(0xFFFFFF));
        _title->setTextAlign(LV_TEXT_ALIGN_CENTER);

        _status = std::make_unique<Label>(_panel->get());
        _status->setSize(kStatusWidth, 22);
        _status->align(LV_ALIGN_TOP_MID, 0, 78);
        _status->setTextFont(&lv_font_montserrat_16);
        _status->setTextColor(lv_color_hex(0xA8A8A8));
        _status->setTextAlign(LV_TEXT_ALIGN_CENTER);
        lv_label_set_long_mode(_status->get(), LV_LABEL_LONG_MODE_DOTS);

        _pairing_detail = std::make_unique<Label>(_panel->get());
        _pairing_detail->setSize(kStatusWidth, 20);
        _pairing_detail->align(LV_ALIGN_TOP_MID, 0, 106);
        _pairing_detail->setTextFont(&lv_font_montserrat_14);
        _pairing_detail->setTextColor(lv_color_hex(0x70B7FF));
        _pairing_detail->setTextAlign(LV_TEXT_ALIGN_CENTER);
        _pairing_detail->setHidden(true);
        lv_label_set_long_mode(_pairing_detail->get(), LV_LABEL_LONG_MODE_DOTS);

        _saved_list = std::make_unique<Container>(_panel->get());
        _saved_list->setSize(350, kListHeight);
        _saved_list->align(LV_ALIGN_TOP_MID, 0, kListTop);
        _saved_list->setRadius(0);
        _saved_list->setBorderWidth(0);
        _saved_list->setPaddingAll(0);
        _saved_list->setBgOpa(LV_OPA_TRANSP);
        _saved_list->setScrollDir(LV_DIR_VER);
        _saved_list->setScrollbarMode(LV_SCROLLBAR_MODE_ACTIVE);

        _empty_label = std::make_unique<Label>(_saved_list->get());
        _empty_label->setSize(kContentWidth, 28);
        _empty_label->align(LV_ALIGN_TOP_MID, 0, 56);
        _empty_label->setText("No saved WiFi");
        _empty_label->setTextFont(&lv_font_montserrat_18);
        _empty_label->setTextColor(lv_color_hex(0xA8A8A8));
        _empty_label->setTextAlign(LV_TEXT_ALIGN_CENTER);

        _pairing_action = std::make_unique<Button>(_panel->get());
        _pairing_action->setSize(kPrimaryActionWidth, kActionHeight);
        _pairing_action->align(LV_ALIGN_TOP_MID, 0, kPrimaryActionY);
        applyCompactButtonStyle(*_pairing_action, lv_color_hex(0x245C96));
        _pairing_action->onClick().connect([this]() { _pairing_action_requested = true; });

        _done_action = std::make_unique<Button>(_panel->get());
        _done_action->setSize(kDoneActionWidth, kActionHeight);
        _done_action->align(LV_ALIGN_TOP_MID, 0, kDoneActionY);
        applyCompactButtonStyle(*_done_action, lv_color_hex(0x3A3A3A));
        _done_action->label().setText("Done");
        _done_action->onClick().connect([this]() { _done_requested = true; });
    }

    void render(const WifiSettingsSnapshot& snapshot)
    {
        _status->setText(statusText(snapshot.state));

        const bool pairing = snapshot.state == WifiSettingsState::Pairing;
        _pairing_detail->setHidden(!pairing);
        if (pairing) {
            const std::string detail = "AP: " + snapshot.ap_ssid + "  " + snapshot.ap_url;
            _pairing_detail->setText(detail.c_str());
        }

        _pairing_action->label().setText(pairing ? "Stop Pairing" : "Add WiFi");

        _default_markers.clear();
        _network_rows.clear();
        _empty_label->setHidden(!snapshot.saved_ssids.empty());

        for (std::size_t index = 0; index < snapshot.saved_ssids.size(); ++index) {
            auto row = std::make_unique<Button>(_saved_list->get());
            row->setSize(350, kRowHeight);
            row->align(LV_ALIGN_TOP_MID, 0, static_cast<int>(index) * (kRowHeight + kRowGap));

            const bool connected = snapshot.state == WifiSettingsState::Connected &&
                                   snapshot.ssid == snapshot.saved_ssids[index];
            applyCompactButtonStyle(*row, connected ? lv_color_hex(0x1769AA) : lv_color_hex(0x252525));
            row->label().setText(snapshot.saved_ssids[index]);
            row->label().setWidth(240);
            row->label().align(LV_ALIGN_LEFT_MID, 20, 0);
            row->label().setTextAlign(LV_TEXT_ALIGN_LEFT);
            lv_label_set_long_mode(row->label().get(), LV_LABEL_LONG_MODE_DOTS);
            row->onClick().connect([this, index]() { _selected_network_requested = index; });

            if (index == 0) {
                auto marker = std::make_unique<Label>(row->get());
                marker->setSize(78, 20);
                marker->align(LV_ALIGN_RIGHT_MID, -16, 0);
                marker->setText("Default");
                marker->setTextFont(&lv_font_montserrat_14);
                marker->setTextColor(lv_color_hex(0xA8D2FF));
                marker->setTextAlign(LV_TEXT_ALIGN_RIGHT);
                _default_markers.push_back(std::move(marker));
            }

            _network_rows.push_back(std::move(row));
        }
    }

    bool consumePairingActionRequested()
    {
        const bool requested = _pairing_action_requested;
        _pairing_action_requested = false;
        return requested;
    }

    std::optional<std::size_t> consumeSelectedNetworkRequested()
    {
        const auto requested = _selected_network_requested;
        _selected_network_requested.reset();
        return requested;
    }

    bool consumeDoneRequested()
    {
        const bool requested = _done_requested;
        _done_requested = false;
        return requested;
    }

private:
    std::unique_ptr<Container> _panel;
    std::unique_ptr<Label> _title;
    std::unique_ptr<Label> _status;
    std::unique_ptr<Label> _pairing_detail;
    std::unique_ptr<Container> _saved_list;
    std::unique_ptr<Label> _empty_label;
    std::unique_ptr<Button> _pairing_action;
    std::unique_ptr<Button> _done_action;
    std::vector<std::unique_ptr<Button>> _network_rows;
    std::vector<std::unique_ptr<Label>> _default_markers;
    bool _pairing_action_requested = false;
    bool _done_requested = false;
    std::optional<std::size_t> _selected_network_requested;
};

WifiSettingsWorker::WifiSettingsWorker()
{
    _controller = std::make_unique<WifiSettingsController>();
    _view = std::make_unique<WifiSettingsView>();
    _next_poll_tick = 0;
}

WifiSettingsWorker::~WifiSettingsWorker()
{
    _controller.reset();
    _view.reset();
}

void WifiSettingsWorker::update()
{
    if (!_view || !_controller) {
        _is_done = true;
        return;
    }

    if (_view->consumeDoneRequested()) {
        _is_done = true;
        return;
    }

    if (const auto selected = _view->consumeSelectedNetworkRequested()) {
        _controller->selectSaved(*selected);
        _next_poll_tick = 0;
    }

    if (_view->consumePairingActionRequested()) {
        if (_has_snapshot && _last_snapshot.state == WifiSettingsState::Pairing) {
            _controller->stopPairing();
        } else {
            _controller->startPairing();
        }
        _next_poll_tick = 0;
    }

    const uint32_t now = GetHAL().millis();
    if (static_cast<int32_t>(now - _next_poll_tick) < 0) {
        return;
    }

    _next_poll_tick = now + 500;
    const WifiSettingsSnapshot snapshot = _controller->poll();
    if (!_has_snapshot || snapshot != _last_snapshot) {
        _view->render(snapshot);
        _last_snapshot = snapshot;
        _has_snapshot = true;
    }
}

} // namespace setup_workers
