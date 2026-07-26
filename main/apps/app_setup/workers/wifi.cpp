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
constexpr int kListTop = 150;
constexpr int kListWidth = 350;
constexpr int kListHeight = 140;
constexpr int kRowHeight = 48;
constexpr int kRowGap = 8;
constexpr int kRowHorizontalInset = 20;
constexpr int kDefaultMarkerWidth = 78;
constexpr int kDefaultMarkerRightInset = 16;
constexpr int kSsidMarkerGap = 12;
constexpr int kDefaultSsidLabelWidth =
    kListWidth - kRowHorizontalInset - kDefaultMarkerWidth - kDefaultMarkerRightInset - kSsidMarkerGap;
constexpr int kNonDefaultSsidLabelWidth = kListWidth - 2 * kRowHorizontalInset;
constexpr int kActionHeight = 42;
constexpr int kPrimaryActionWidth = 300;
constexpr int kDoneActionWidth = 374;
constexpr int kDoneActionHeight = 130;
constexpr int kPrimaryActionY = 294;
constexpr int kDoneActionCenterY = 175;

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

void applyConfirmationButtonStyle(Button& button)
{
    button.setRadius(77);
    button.setBorderWidth(0);
    button.setShadowWidth(0);
    button.setBgColor(lv_color_hex(0x4AD78C));
    button.setBgOpa(LV_OPA_COVER);
    button.label().setTextFont(&lv_font_montserrat_28);
    button.label().setTextColor(lv_color_hex(0x0F5831));
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
        _title->setSize(kContentWidth, 28);
        _title->align(LV_ALIGN_TOP_MID, 0, 44);
        _title->setText("WiFi");
        _title->setTextFont(&MontserratSemiBold26);
        _title->setTextColor(lv_color_hex(0xFFFFFF));
        _title->setTextAlign(LV_TEXT_ALIGN_CENTER);

        _status = std::make_unique<Label>(_panel->get());
        _status->setSize(kContentWidth, 20);
        _status->align(LV_ALIGN_TOP_MID, 0, 78);
        _status->setTextFont(&lv_font_montserrat_16);
        _status->setTextColor(lv_color_hex(0xA8A8A8));
        _status->setTextAlign(LV_TEXT_ALIGN_CENTER);
        lv_label_set_long_mode(_status->get(), LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);

        _connection_detail = std::make_unique<Label>(_panel->get());
        _connection_detail->setSize(kContentWidth, 20);
        _connection_detail->align(LV_ALIGN_TOP_MID, 0, 104);
        _connection_detail->setTextFont(&lv_font_montserrat_14);
        _connection_detail->setTextColor(lv_color_hex(0x70B7FF));
        _connection_detail->setTextAlign(LV_TEXT_ALIGN_CENTER);
        _connection_detail->setHidden(true);

        _pairing_ap_ssid_detail = std::make_unique<Label>(_panel->get());
        _pairing_ap_ssid_detail->setSize(kContentWidth, 20);
        _pairing_ap_ssid_detail->align(LV_ALIGN_TOP_MID, 0, 104);
        _pairing_ap_ssid_detail->setTextFont(&lv_font_montserrat_14);
        _pairing_ap_ssid_detail->setTextColor(lv_color_hex(0x70B7FF));
        _pairing_ap_ssid_detail->setTextAlign(LV_TEXT_ALIGN_CENTER);
        _pairing_ap_ssid_detail->setHidden(true);
        lv_label_set_long_mode(_pairing_ap_ssid_detail->get(), LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);

        _pairing_portal_url_detail = std::make_unique<Label>(_panel->get());
        _pairing_portal_url_detail->setSize(kContentWidth, 20);
        _pairing_portal_url_detail->align(LV_ALIGN_TOP_MID, 0, 128);
        _pairing_portal_url_detail->setTextFont(&lv_font_montserrat_14);
        _pairing_portal_url_detail->setTextColor(lv_color_hex(0x70B7FF));
        _pairing_portal_url_detail->setTextAlign(LV_TEXT_ALIGN_CENTER);
        _pairing_portal_url_detail->setHidden(true);
        lv_label_set_long_mode(_pairing_portal_url_detail->get(), LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);

        _saved_list = std::make_unique<Container>(_panel->get());
        _saved_list->setSize(kListWidth, kListHeight);
        _saved_list->align(LV_ALIGN_TOP_MID, 0, kListTop);
        _saved_list->setRadius(0);
        _saved_list->setBorderWidth(0);
        _saved_list->setPaddingAll(0);
        _saved_list->setBgOpa(LV_OPA_TRANSP);
        _saved_list->setScrollDir(LV_DIR_VER);
        _saved_list->setScrollbarMode(LV_SCROLLBAR_MODE_ACTIVE);

        _empty_label = std::make_unique<Label>(_saved_list->get());
        _empty_label->setSize(kListWidth, 28);
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
        _done_action->setSize(kDoneActionWidth, kDoneActionHeight);
        _done_action->align(LV_ALIGN_CENTER, 0, kDoneActionCenterY);
        applyConfirmationButtonStyle(*_done_action);
        _done_action->label().setText("OK");
        _done_action->onClick().connect([this]() { _done_requested = true; });
    }

    void render(const WifiSettingsSnapshot& snapshot)
    {
        const bool connected = snapshot.state == WifiSettingsState::Connected;
        const bool connecting = snapshot.state == WifiSettingsState::Connecting;
        const bool pairing = snapshot.state == WifiSettingsState::Pairing;

        std::string status = statusText(snapshot.state);
        if (connected) {
            status += ": " + snapshot.ssid;
        } else if (connecting) {
            status += ": " + snapshot.ssid;
        }
        _status->setText(status.c_str());

        _connection_detail->setHidden(!connected);
        if (connected) {
            const std::string detail =
                "IP: " + snapshot.ip_address + "  RSSI: " + std::to_string(snapshot.rssi) + " dBm";
            _connection_detail->setText(detail.c_str());
        }

        _pairing_ap_ssid_detail->setHidden(!pairing);
        _pairing_portal_url_detail->setHidden(!pairing);
        if (pairing) {
            const std::string ap_ssid_detail = "AP SSID: " + snapshot.ap_ssid;
            const std::string portal_url_detail = "Portal: " + snapshot.ap_url;
            _pairing_ap_ssid_detail->setText(ap_ssid_detail.c_str());
            _pairing_portal_url_detail->setText(portal_url_detail.c_str());
        }

        _pairing_action->label().setText(pairing ? "Stop Pairing" : "Add WiFi");

        _saved_list->setHidden(pairing);
        _empty_label->setHidden(!snapshot.saved_ssids.empty());

        if (snapshot.saved_ssids != _rendered_saved_ssids) {
            _default_markers.clear();
            _network_rows.clear();

            for (std::size_t index = 0; index < snapshot.saved_ssids.size(); ++index) {
                auto row = std::make_unique<Button>(_saved_list->get());
                row->setSize(kListWidth, kRowHeight);
                row->align(LV_ALIGN_TOP_MID, 0, static_cast<int>(index) * (kRowHeight + kRowGap));
                applyCompactButtonStyle(*row, lv_color_hex(0x252525));
                row->setPaddingAll(0);
                row->label().setText(snapshot.saved_ssids[index]);
                row->label().setWidth(index == 0 ? kDefaultSsidLabelWidth : kNonDefaultSsidLabelWidth);
                row->label().align(LV_ALIGN_LEFT_MID, kRowHorizontalInset, 0);
                row->label().setTextAlign(LV_TEXT_ALIGN_LEFT);
                lv_label_set_long_mode(row->label().get(), LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
                row->onClick().connect([this, index]() { _selected_network_requested = index; });

                if (index == 0) {
                    auto marker = std::make_unique<Label>(row->get());
                    marker->setSize(kDefaultMarkerWidth, 20);
                    marker->align(LV_ALIGN_RIGHT_MID, -kDefaultMarkerRightInset, 0);
                    marker->setText("Default");
                    marker->setTextFont(&lv_font_montserrat_14);
                    marker->setTextColor(lv_color_hex(0xA8D2FF));
                    marker->setTextAlign(LV_TEXT_ALIGN_RIGHT);
                    _default_markers.push_back(std::move(marker));
                }

                _network_rows.push_back(std::move(row));
            }

            _rendered_saved_ssids = snapshot.saved_ssids;
        }

        for (std::size_t index = 0; index < _network_rows.size(); ++index) {
            const bool row_connected = snapshot.state == WifiSettingsState::Connected &&
                                       snapshot.ssid == snapshot.saved_ssids[index];
            _network_rows[index]->setBgColor(
                row_connected ? lv_color_hex(0x1769AA) : lv_color_hex(0x252525));
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
    std::unique_ptr<Label> _connection_detail;
    std::unique_ptr<Label> _pairing_ap_ssid_detail;
    std::unique_ptr<Label> _pairing_portal_url_detail;
    std::unique_ptr<Container> _saved_list;
    std::unique_ptr<Label> _empty_label;
    std::unique_ptr<Button> _pairing_action;
    std::unique_ptr<Button> _done_action;
    std::vector<std::unique_ptr<Button>> _network_rows;
    std::vector<std::unique_ptr<Label>> _default_markers;
    std::vector<std::string> _rendered_saved_ssids;
    bool _pairing_action_requested = false;
    bool _done_requested = false;
    std::optional<std::size_t> _selected_network_requested;
};

WifiSettingsWorker::WifiSettingsWorker()
{
    _view = std::make_unique<WifiSettingsView>();
    _next_poll_tick = 0;
}

WifiSettingsWorker::~WifiSettingsWorker()
{
    releaseRuntime();
    _view.reset();
}

void WifiSettingsWorker::releaseRuntime()
{
    _controller.reset();
}

void WifiSettingsWorker::updateRuntime()
{
    if (_is_done) {
        return;
    }

    if (!_controller) {
        _controller = std::make_unique<WifiSettingsController>();
    }

    if (_pending_selected_network) {
        _controller->selectSaved(*_pending_selected_network);
        _pending_selected_network.reset();
        _next_poll_tick = 0;
    }

    if (_pending_pairing_action) {
        if (_has_snapshot && _last_snapshot.state == WifiSettingsState::Pairing) {
            _controller->stopPairing();
        } else {
            _controller->startPairing();
        }
        _pending_pairing_action = false;
        _next_poll_tick = 0;
    }

    const uint32_t now = GetHAL().millis();
    if (static_cast<int32_t>(now - _next_poll_tick) < 0) {
        return;
    }

    _next_poll_tick = now + 500;
    const WifiSettingsSnapshot snapshot = _controller->poll();
    if (!_has_snapshot || snapshot != _last_snapshot) {
        _pending_snapshot = snapshot;
    }
}

void WifiSettingsWorker::update()
{
    if (!_view) {
        _is_done = true;
        return;
    }

    if (_view->consumeDoneRequested()) {
        _is_done = true;
        return;
    }

    if (const auto selected = _view->consumeSelectedNetworkRequested()) {
        _pending_selected_network = *selected;
    }

    if (_view->consumePairingActionRequested()) {
        _pending_pairing_action = true;
    }

    if (_pending_snapshot) {
        _view->render(*_pending_snapshot);
        _last_snapshot = std::move(*_pending_snapshot);
        _pending_snapshot.reset();
        _has_snapshot = true;
    }
}

} // namespace setup_workers
