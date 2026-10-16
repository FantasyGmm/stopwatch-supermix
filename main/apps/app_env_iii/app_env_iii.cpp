/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_env_iii.h"

#include <assets/assets.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <runtime/display/global_display_protection_service.h>
#include <array>
#include <cstdint>

namespace {

constexpr int kIconSize = 200;
constexpr int kIconSamplesPerAxis = 2;
constexpr int kIconStripeHeight = 8;
constexpr std::size_t kIconBytes = static_cast<std::size_t>(kIconSize * kIconSize * 2);
using IconPixels = std::array<std::uint8_t, kIconBytes>;

constexpr std::uint16_t rgb565(std::uint8_t red, std::uint8_t green, std::uint8_t blue)
{
    return static_cast<std::uint16_t>(((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3));
}

constexpr bool in_rounded_rect(int x, int y, int left, int top, int right, int bottom, int radius)
{
    int nearest_x = x < left + radius ? left + radius : (x > right - radius ? right - radius : x);
    int nearest_y = y < top + radius ? top + radius : (y > bottom - radius ? bottom - radius : y);
    int dx = x - nearest_x;
    int dy = y - nearest_y;
    return dx * dx + dy * dy <= radius * radius;
}

constexpr bool in_circle(int x, int y, int center_x, int center_y, int radius)
{
    int dx = x - center_x;
    int dy = y - center_y;
    return dx * dx + dy * dy <= radius * radius;
}

constexpr bool in_triangle(int x, int y, int ax, int ay, int bx, int by, int cx, int cy)
{
    const int ab = (bx - ax) * (y - ay) - (by - ay) * (x - ax);
    const int bc = (cx - bx) * (y - by) - (cy - by) * (x - bx);
    const int ca = (ax - cx) * (y - cy) - (ay - cy) * (x - cx);
    return (ab >= 0 && bc >= 0 && ca >= 0) || (ab <= 0 && bc <= 0 && ca <= 0);
}

constexpr std::uint16_t icon_sample_color(int x8, int y8)
{
    constexpr int kUnit = 8;
    constexpr int kCenter = 100 * kUnit;
    constexpr int kOuterRadius = 89 * kUnit;
    constexpr int kInnerRadius = 86 * kUnit;
    constexpr std::uint16_t kBlack = rgb565(0x00, 0x00, 0x00);
    constexpr std::uint16_t kPanel = rgb565(0x0D, 0x1A, 0x20);
    constexpr std::uint16_t kRing = rgb565(0x68, 0xB7, 0xAC);
    constexpr std::uint16_t kOutline = rgb565(0xE8, 0xF3, 0xF3);
    constexpr std::uint16_t kFluid = rgb565(0xF2, 0x9A, 0x72);
    constexpr std::uint16_t kWater = rgb565(0x72, 0xD5, 0xDF);
    constexpr std::uint16_t kHighlight = rgb565(0xC3, 0xF0, 0xED);

    const int dx = x8 - kCenter;
    const int dy = y8 - kCenter;
    const int distance_squared = dx * dx + dy * dy;
    if (distance_squared > kOuterRadius * kOuterRadius) {
        return kBlack;
    }
    if (distance_squared >= kInnerRadius * kInnerRadius) {
        return kRing;
    }

    std::uint16_t color = kPanel;
    const bool thermometer_shell =
        in_rounded_rect(x8, y8, 70 * kUnit, 45 * kUnit, 86 * kUnit, 113 * kUnit, 8 * kUnit) ||
        in_circle(x8, y8, 78 * kUnit, 114 * kUnit, 16 * kUnit);
    if (thermometer_shell) {
        color = kOutline;
    }
    const bool thermometer_inner =
        in_rounded_rect(x8, y8, 74 * kUnit, 50 * kUnit, 82 * kUnit, 111 * kUnit, 4 * kUnit) ||
        in_circle(x8, y8, 78 * kUnit, 114 * kUnit, 11 * kUnit);
    if (thermometer_inner) {
        color = kPanel;
    }
    if (in_rounded_rect(x8, y8, 75 * kUnit, 80 * kUnit, 81 * kUnit, 114 * kUnit, 3 * kUnit) ||
        in_circle(x8, y8, 78 * kUnit, 114 * kUnit, 7 * kUnit)) {
        color = kFluid;
    }

    // A clean water-drop silhouette balances the thermometer without adding text.
    const bool water_drop = in_circle(x8, y8, 128 * kUnit, 103 * kUnit, 18 * kUnit) ||
                            in_triangle(
                                x8, y8, 128 * kUnit, 55 * kUnit, 109 * kUnit, 101 * kUnit, 147 * kUnit, 101 * kUnit);
    if (water_drop) {
        color = kWater;
    }
    if (in_rounded_rect(x8, y8, 120 * kUnit, 87 * kUnit, 124 * kUnit, 98 * kUnit, 2 * kUnit)) {
        color = kHighlight;
    }
    return color;
}

template <int StartY, int StripeHeight>
constexpr std::array<std::uint8_t, StripeHeight * kIconSize * 2> make_icon_stripe()
{
    std::array<std::uint8_t, StripeHeight * kIconSize * 2> pixels{};
    for (int row = 0; row < StripeHeight; ++row) {
        const int y = StartY + row;
        for (int x = 0; x < kIconSize; ++x) {
            std::uint32_t red = 0;
            std::uint32_t green = 0;
            std::uint32_t blue = 0;
            for (int sample_y = 0; sample_y < kIconSamplesPerAxis; ++sample_y) {
                for (int sample_x = 0; sample_x < kIconSamplesPerAxis; ++sample_x) {
                    const int x8 = x * 8 + 2 + sample_x * 4;
                    const int y8 = y * 8 + 2 + sample_y * 4;
                    const std::uint16_t sample = icon_sample_color(x8, y8);
                    red += ((sample >> 11) & 0x1F) * 255 / 31;
                    green += ((sample >> 5) & 0x3F) * 255 / 63;
                    blue += (sample & 0x1F) * 255 / 31;
                }
            }
            const int sample_count = kIconSamplesPerAxis * kIconSamplesPerAxis;
            const std::uint16_t color = rgb565(
                static_cast<std::uint8_t>(red / sample_count),
                static_cast<std::uint8_t>(green / sample_count),
                static_cast<std::uint8_t>(blue / sample_count));
            const std::size_t offset = static_cast<std::size_t>((row * kIconSize + x) * 2);
            pixels[offset] = static_cast<std::uint8_t>(color & 0xFF);
            pixels[offset + 1] = static_cast<std::uint8_t>(color >> 8);
        }
    }
    return pixels;
}

template <std::size_t StripeBytes>
constexpr void append_icon_stripe(
    IconPixels& pixels,
    std::size_t& offset,
    const std::array<std::uint8_t, StripeBytes>& stripe)
{
    for (const std::uint8_t byte : stripe) {
        pixels[offset++] = byte;
    }
}

// Keep each expensive 2x-supersampled raster pass in its own constant expression.
// GCC limits constexpr work per expression, while the final concatenation only copies bytes.
constexpr auto kEnvIconStripe000 = make_icon_stripe<0, kIconStripeHeight>();
constexpr auto kEnvIconStripe008 = make_icon_stripe<8, kIconStripeHeight>();
constexpr auto kEnvIconStripe016 = make_icon_stripe<16, kIconStripeHeight>();
constexpr auto kEnvIconStripe024 = make_icon_stripe<24, kIconStripeHeight>();
constexpr auto kEnvIconStripe032 = make_icon_stripe<32, kIconStripeHeight>();
constexpr auto kEnvIconStripe040 = make_icon_stripe<40, kIconStripeHeight>();
constexpr auto kEnvIconStripe048 = make_icon_stripe<48, kIconStripeHeight>();
constexpr auto kEnvIconStripe056 = make_icon_stripe<56, kIconStripeHeight>();
constexpr auto kEnvIconStripe064 = make_icon_stripe<64, kIconStripeHeight>();
constexpr auto kEnvIconStripe072 = make_icon_stripe<72, kIconStripeHeight>();
constexpr auto kEnvIconStripe080 = make_icon_stripe<80, kIconStripeHeight>();
constexpr auto kEnvIconStripe088 = make_icon_stripe<88, kIconStripeHeight>();
constexpr auto kEnvIconStripe096 = make_icon_stripe<96, kIconStripeHeight>();
constexpr auto kEnvIconStripe104 = make_icon_stripe<104, kIconStripeHeight>();
constexpr auto kEnvIconStripe112 = make_icon_stripe<112, kIconStripeHeight>();
constexpr auto kEnvIconStripe120 = make_icon_stripe<120, kIconStripeHeight>();
constexpr auto kEnvIconStripe128 = make_icon_stripe<128, kIconStripeHeight>();
constexpr auto kEnvIconStripe136 = make_icon_stripe<136, kIconStripeHeight>();
constexpr auto kEnvIconStripe144 = make_icon_stripe<144, kIconStripeHeight>();
constexpr auto kEnvIconStripe152 = make_icon_stripe<152, kIconStripeHeight>();
constexpr auto kEnvIconStripe160 = make_icon_stripe<160, kIconStripeHeight>();
constexpr auto kEnvIconStripe168 = make_icon_stripe<168, kIconStripeHeight>();
constexpr auto kEnvIconStripe176 = make_icon_stripe<176, kIconStripeHeight>();
constexpr auto kEnvIconStripe184 = make_icon_stripe<184, kIconStripeHeight>();
constexpr auto kEnvIconStripe192 = make_icon_stripe<192, kIconStripeHeight>();

constexpr IconPixels assemble_icon_pixels()
{
    IconPixels pixels{};
    std::size_t offset = 0;
    append_icon_stripe(pixels, offset, kEnvIconStripe000);
    append_icon_stripe(pixels, offset, kEnvIconStripe008);
    append_icon_stripe(pixels, offset, kEnvIconStripe016);
    append_icon_stripe(pixels, offset, kEnvIconStripe024);
    append_icon_stripe(pixels, offset, kEnvIconStripe032);
    append_icon_stripe(pixels, offset, kEnvIconStripe040);
    append_icon_stripe(pixels, offset, kEnvIconStripe048);
    append_icon_stripe(pixels, offset, kEnvIconStripe056);
    append_icon_stripe(pixels, offset, kEnvIconStripe064);
    append_icon_stripe(pixels, offset, kEnvIconStripe072);
    append_icon_stripe(pixels, offset, kEnvIconStripe080);
    append_icon_stripe(pixels, offset, kEnvIconStripe088);
    append_icon_stripe(pixels, offset, kEnvIconStripe096);
    append_icon_stripe(pixels, offset, kEnvIconStripe104);
    append_icon_stripe(pixels, offset, kEnvIconStripe112);
    append_icon_stripe(pixels, offset, kEnvIconStripe120);
    append_icon_stripe(pixels, offset, kEnvIconStripe128);
    append_icon_stripe(pixels, offset, kEnvIconStripe136);
    append_icon_stripe(pixels, offset, kEnvIconStripe144);
    append_icon_stripe(pixels, offset, kEnvIconStripe152);
    append_icon_stripe(pixels, offset, kEnvIconStripe160);
    append_icon_stripe(pixels, offset, kEnvIconStripe168);
    append_icon_stripe(pixels, offset, kEnvIconStripe176);
    append_icon_stripe(pixels, offset, kEnvIconStripe184);
    append_icon_stripe(pixels, offset, kEnvIconStripe192);
    return pixels;
}

constexpr IconPixels kEnvIconPixels = assemble_icon_pixels();

lv_image_dsc_t make_env_icon_descriptor()
{
    lv_image_dsc_t descriptor{};
    descriptor.header.cf = LV_COLOR_FORMAT_RGB565;
    descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
    descriptor.header.flags = 0;
    descriptor.header.w = kIconSize;
    descriptor.header.h = kIconSize;
    descriptor.header.stride = kIconSize * 2;
    descriptor.data_size = kEnvIconPixels.size();
    descriptor.data = kEnvIconPixels.data();
    return descriptor;
}

lv_image_dsc_t kEnvIcon = make_env_icon_descriptor();

}  // namespace

AppEnvIII::AppEnvIII()
{
    setAppInfo().name = "环境监测";
    setAppInfo().icon = &kEnvIcon;
}

void AppEnvIII::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppEnvIII::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");
    _key_manager = std::make_unique<input::KeyManager>();
    _screen_off_paused =
        GetGlobalDisplayProtectionService().state() ==
        DisplayProtectionState::ScreenOff;
    const bool monitoring_requested = !_screen_off_paused;
    GetHAL().setEnvMonitoring(monitoring_requested);
    const EnvSnapshot snapshot = GetHAL().getEnvSnapshot();
    const std::uint32_t now = GetHAL().millis();

    LvglLockGuard lock;
    _view = std::make_unique<view::EnvIIIView>();
    _view->init(lv_screen_active());
    _view->update(snapshot, now);
}

void AppEnvIII::onRunning()
{
    if (_key_manager && _key_manager->update() == input::KeyEvent::GoHome) {
        close();
        return;
    }

    const bool screen_off =
        GetGlobalDisplayProtectionService().state() ==
        DisplayProtectionState::ScreenOff;
    if (screen_off != _screen_off_paused) {
        _screen_off_paused = screen_off;
        const bool monitoring_requested = !screen_off;
        GetHAL().setEnvMonitoring(monitoring_requested);
    }
    if (screen_off) {
        return;
    }

    const EnvSnapshot snapshot = GetHAL().getEnvSnapshot();
    const std::uint32_t now = GetHAL().millis();

    LvglLockGuard lock;
    if (_view) {
        _view->update(snapshot, now);
    }
}

void AppEnvIII::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
    GetHAL().setEnvMonitoring(false);
    _screen_off_paused = false;
    _key_manager.reset();

    LvglLockGuard lock;
    _view.reset();
}
