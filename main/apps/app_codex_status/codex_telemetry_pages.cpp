#include "codex_telemetry_pages.h"

#include "codex_status_ui_layout.h"
#include "codex_telemetry_ui_layout.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>


namespace {

using namespace codex_status_ui;
using namespace codex_telemetry_ui;

constexpr int kContentLeft = 48;
constexpr int kContentWidth = 370;

lv_obj_t* make_text(
    lv_obj_t* parent,
    const char* text,
    int x,
    int y,
    int width,
    const lv_font_t* font,
    uint32_t color,
    lv_text_align_t align = LV_TEXT_ALIGN_LEFT)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, align, 0);
    lv_obj_set_width(label, width);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_pos(label, x, y);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    return label;
}

lv_obj_t* make_bar(lv_obj_t* parent, int x, int y, int width)
{
    lv_obj_t* bar = lv_bar_create(parent);
    lv_obj_set_size(bar, width, 7);
    lv_obj_set_pos(bar, x, y);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(bar, 2, 0);
    lv_obj_set_style_radius(bar, 2, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, lv_color_hex(kPanelGraphite), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(kQuotaMint), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    return bar;
}

bool recent_timestamp(int64_t timestamp, int64_t now, uint32_t ttl_seconds)
{
    return timestamp > 0 && now >= timestamp &&
           static_cast<uint64_t>(now - timestamp) <= ttl_seconds;
}

const char* status_text(CodexTelemetryStatus status)
{
    switch (status) {
        case CodexTelemetryStatus::Ok:
            return "LIVE";
        case CodexTelemetryStatus::Stale:
            return "EXPIRED";
        case CodexTelemetryStatus::Error:
            return "ERROR";
        case CodexTelemetryStatus::Unavailable:
        case CodexTelemetryStatus::Unknown:
            return "UNAVAILABLE";
    }
    return "UNAVAILABLE";
}

uint32_t status_color(CodexTelemetryStatus status)
{
    switch (status) {
        case CodexTelemetryStatus::Ok:
            return kQuotaMint;
        case CodexTelemetryStatus::Stale:
            return kAlertVermilion;
        case CodexTelemetryStatus::Error:
            return kAlertVermilion;
        case CodexTelemetryStatus::Unavailable:
        case CodexTelemetryStatus::Unknown:
            return kEtchedGray;
    }
    return kEtchedGray;
}

void set_label_text_if_changed(lv_obj_t* label, const char* text)
{
    if (label == nullptr) {
        return;
    }
    const char* current = lv_label_get_text(label);
    const char* desired = text == nullptr ? "" : text;
    if (current == nullptr || std::strcmp(current, desired) != 0) {
        lv_label_set_text(label, desired);
    }
}

void set_text(
    lv_obj_t* label,
    const char* text,
    uint32_t color,
    TelemetryTextStyleCache& style)
{
    set_label_text_if_changed(label, text);
    if (label != nullptr && (!style.valid || style.text_color != color)) {
        lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
        style.text_color = color;
        style.valid = true;
    }
}

void format_reset(char* output, size_t output_size, int64_t reset_at, int64_t now)
{
    if (output_size == 0) {
        return;
    }
    if (reset_at <= 0 || now <= 0) {
        std::snprintf(output, output_size, "RESET --");
        return;
    }
    const int64_t seconds = reset_at > now ? reset_at - now : 0;
    const int64_t days = seconds / 86400;
    const int64_t hours = (seconds % 86400) / 3600;
    const int64_t minutes = (seconds % 3600) / 60;
    if (days > 0) {
        std::snprintf(
            output,
            output_size,
            "RESET %lldD %lldH",
            static_cast<long long>(days),
            static_cast<long long>(hours));
    } else if (hours > 0) {
        std::snprintf(
            output,
            output_size,
            "RESET %lldH %02lldM",
            static_cast<long long>(hours),
            static_cast<long long>(minutes));
    } else {
        std::snprintf(
            output,
            output_size,
            "RESET %lldM",
            static_cast<long long>(minutes));
    }
}

void format_tokens(
    char* output,
    size_t output_size,
    uint64_t tokens,
    bool lower_bound = false)
{
    if (lower_bound) {
        uint64_t tenths = 0;
        char suffix = '\0';
        if (tokens >= 1000000000000000ULL) {
            tenths = tokens / 100000000000000ULL;
            suffix = 'P';
        } else if (tokens >= 1000000000000ULL) {
            tenths = tokens / 100000000000ULL;
            suffix = 'T';
        } else if (tokens >= 1000000000ULL) {
            tenths = tokens / 100000000ULL;
            suffix = 'B';
        } else if (tokens >= 1000000ULL) {
            tenths = tokens / 100000ULL;
            suffix = 'M';
        } else if (tokens >= 1000ULL) {
            tenths = tokens / 100ULL;
            suffix = 'k';
        } else {
            std::snprintf(
                output,
                output_size,
                "%llu",
                static_cast<unsigned long long>(tokens));
            return;
        }

        if (tenths % 10ULL == 0) {
            std::snprintf(
                output,
                output_size,
                "%llu%c",
                static_cast<unsigned long long>(tenths / 10ULL),
                suffix);
        } else {
            std::snprintf(
                output,
                output_size,
                "%llu.%llu%c",
                static_cast<unsigned long long>(tenths / 10ULL),
                static_cast<unsigned long long>(tenths % 10ULL),
                suffix);
        }
        return;
    }

    if (tokens >= 1000000000000000ULL) {
        std::snprintf(output, output_size, "%.1fP", tokens / 1000000000000000.0);
    } else if (tokens >= 1000000000000ULL) {
        std::snprintf(output, output_size, "%.1fT", tokens / 1000000000000.0);
    } else if (tokens >= 1000000000ULL) {
        std::snprintf(output, output_size, "%.1fB", tokens / 1000000000.0);
    } else if (tokens >= 1000000ULL) {
        std::snprintf(output, output_size, "%.1fM", tokens / 1000000.0);
    } else if (tokens >= 1000ULL) {
        std::snprintf(output, output_size, "%.1fk", tokens / 1000.0);
    } else {
        std::snprintf(
            output,
            output_size,
            "%llu",
            static_cast<unsigned long long>(tokens));
    }
}

void format_percent(char* output, size_t output_size, double value)
{
    std::snprintf(output, output_size, "%.1f%%", value);
}

void format_rate(char* output, size_t output_size, double value)
{
    if (value >= 1000.0) {
        std::snprintf(output, output_size, "%.1f MiB/s", value / 1024.0);
    } else {
        std::snprintf(output, output_size, "%.1f KiB/s", value);
    }
}

void set_visible(lv_obj_t* object, bool visible)
{
    if (object == nullptr || lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN) == !visible) {
        return;
    }
    if (visible) {
        lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
    }
}

void format_cost(char* output, size_t size, double value, bool present,
                 CodexCostStatus status)
{
    if (status == CodexCostStatus::Unknown) {
        std::snprintf(output, size, "--");
        return;
    }
    if (!present || status == CodexCostStatus::Unavailable) {
        std::snprintf(output, size, "UNPRICED");
        return;
    }
    char amount[24];
    if (value >= 1000000000.0) {
        std::snprintf(amount, sizeof(amount), "$%.1fB", value / 1000000000.0);
    } else if (value >= 1000000.0) {
        std::snprintf(amount, sizeof(amount), "$%.1fM", value / 1000000.0);
    } else if (value >= 1000.0) {
        std::snprintf(amount, sizeof(amount), "$%.1fk", value / 1000.0);
    } else {
        std::snprintf(amount, sizeof(amount), value > 0.0 && value < 0.01
                          ? "$%.4f" : "$%.2f", value);
    }
    std::snprintf(output, size, "%s%s", amount,
                  status == CodexCostStatus::Partial ? " PARTIAL" : "");
}

lv_obj_t* make_model_arrow(lv_obj_t* parent, int x, const char* glyph)
{
    lv_obj_t* target = lv_obj_create(parent);
    lv_obj_remove_style_all(target);
    lv_obj_set_pos(target, x, kUsageModelY - 10);
    lv_obj_set_size(target, 44, 44);
    lv_obj_add_flag(target, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(target, LV_OBJ_FLAG_SCROLLABLE);
    make_text(target, glyph, 0, 9, 44, &lv_font_montserrat_22,
              kQuotaMint, LV_TEXT_ALIGN_CENTER);
    return target;
}

void format_sample_count(char* output, size_t size, uint64_t samples, uint64_t requests)
{
    char numerator[24];
    char denominator[24];
    if (samples < 1000000ULL) {
        std::snprintf(numerator, sizeof(numerator), "%llu",
                      static_cast<unsigned long long>(samples));
    } else {
        format_tokens(numerator, sizeof(numerator), samples);
    }
    if (requests < 1000000ULL) {
        std::snprintf(denominator, sizeof(denominator), "%llu",
                      static_cast<unsigned long long>(requests));
    } else {
        format_tokens(denominator, sizeof(denominator), requests);
    }
    std::snprintf(output, size, "%s/%s samples", numerator, denominator);
}

void style_page(lv_obj_t* parent)
{
    // The carousel parent owns this root's horizontal position.
    lv_obj_set_size(parent, codex_telemetry_ui::kScreenSize, codex_telemetry_ui::kScreenSize);
    lv_obj_set_style_radius(parent, 0, 0);
    lv_obj_set_style_border_width(parent, 0, 0);
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_set_style_bg_color(parent, lv_color_hex(kInstrumentBlack), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
}

}  // namespace


void CodexUsagePage::init(lv_obj_t* parent)
{
    shutdown();
    if (parent == nullptr) {
        return;
    }
    _root = parent;
    style_page(_root);
    make_text(
        _root, "USAGE", kContentLeft, kHeaderY, kContentWidth,
        &lv_font_montserrat_22, kSignalWhite, LV_TEXT_ALIGN_CENTER);
    _status = make_text(
        _root, "UNAVAILABLE", kContentLeft, kStatusY, kContentWidth,
        &lv_font_montserrat_14, kEtchedGray, LV_TEXT_ALIGN_CENTER);
    make_text(
        _root, "7D LEFT", 51, kUsageWeekY, 95,
        &lv_font_montserrat_18, kEtchedGray);
    _week_quota = {
        make_text(
            _root, "--%", 354, kUsageWeekY - 7, 58,
            &lv_font_montserrat_22, kSignalWhite, LV_TEXT_ALIGN_RIGHT),
        make_bar(_root, 151, kUsageWeekY + 27, 203),
        make_text(
            _root, "RESET --", 151, kUsageWeekY + 4, 195,
            &lv_font_montserrat_14, kEtchedGray, LV_TEXT_ALIGN_CENTER),
    };
    _providers = make_text(
        _root, "", 60, kUsageProvidersY, 346,
        &lv_font_montserrat_12, kEtchedGray, LV_TEXT_ALIGN_CENTER);
    make_text(_root, "TODAY TOKENS", 54, kUsageTodayHeadingY, 174,
              &lv_font_montserrat_12, kEtchedGray, LV_TEXT_ALIGN_CENTER);
    make_text(_root, "API EST. USD", 238, kUsageTodayHeadingY, 174,
              &lv_font_montserrat_12, kEtchedGray, LV_TEXT_ALIGN_CENTER);
    _estimate = make_text(
        _root, "--", 234, kUsageTodayValueY + 2, 182,
        &lv_font_montserrat_18, kSignalWhite, LV_TEXT_ALIGN_CENTER);
    _tokens = make_text(
        _root, "--", 54, kUsageTodayValueY, 174,
        &lv_font_montserrat_22, kSignalWhite, LV_TEXT_ALIGN_CENTER);

    _model_position = make_text(
        _root, "MODEL DETAIL", 58, kUsageModelPositionY, 350,
        &lv_font_montserrat_12, kEtchedGray, LV_TEXT_ALIGN_CENTER);
    _model_name = make_text(
        _root, "DETAILS UNAVAILABLE", 88, kUsageModelY, 290,
        &lv_font_montserrat_20, kSignalWhite, LV_TEXT_ALIGN_CENTER);
    lv_label_set_long_mode(_model_name, LV_LABEL_LONG_MODE_DOTS);
    _previous_model = make_model_arrow(_root, 38, "<");
    _next_model = make_model_arrow(_root, 384, ">");
    lv_obj_add_event_cb(_previous_model, model_arrow_event, LV_EVENT_ALL, this);
    lv_obj_add_event_cb(_next_model, model_arrow_event, LV_EVENT_ALL, this);
    _model_summary = make_text(
        _root, "-- TOKENS  /  -- USD", 54, kUsageModelSummaryY, 358,
        &lv_font_montserrat_18, kEtchedGray, LV_TEXT_ALIGN_CENTER);

    const int bucket_x[] = {54, 238, 54, 238};
    const int bucket_y[] = {kUsageBucketsFirstY, kUsageBucketsFirstY,
                            kUsageBucketsSecondY, kUsageBucketsSecondY};
    const char* bucket_names[] = {"FRESH IN", "OUTPUT", "CACHE R", "CACHE W"};
    for (size_t index = 0; index < _token_buckets.size(); ++index) {
        make_text(_root, bucket_names[index], bucket_x[index], bucket_y[index] + 3,
                  70, &lv_font_montserrat_12, kEtchedGray);
        _token_buckets[index] = make_text(
            _root, "--", bucket_x[index] + 70, bucket_y[index], 104,
            &lv_font_montserrat_18, kEtchedGray, LV_TEXT_ALIGN_RIGHT);
    }
    const char* performance_names[] = {"EST. TPS", "EST. TTFT"};
    for (size_t index = 0; index < _performance_values.size(); ++index) {
        const int x = index == 0 ? 70 : 240;
        make_text(_root, performance_names[index], x, kUsagePerformanceHeadingY, 156,
                  &lv_font_montserrat_12, kEtchedGray, LV_TEXT_ALIGN_CENTER);
        _performance_values[index] = make_text(
            _root, "--", x, kUsagePerformanceValueY, 156,
            &lv_font_montserrat_22, kEtchedGray, LV_TEXT_ALIGN_CENTER);
        _performance_samples[index] = make_text(
            _root, "--/-- samples", x, kUsageSamplesY, 156,
            &lv_font_montserrat_12, kEtchedGray, LV_TEXT_ALIGN_CENTER);
    }
    render({}, 0);
}


void CodexUsagePage::render_quota(
    QuotaRow& row,
    int remaining_pct,
    int64_t reset_epoch_seconds,
    bool has_reset,
    bool available,
    int64_t now_epoch_seconds)
{
    if (row.percent == nullptr || row.bar == nullptr || row.reset == nullptr) {
        return;
    }
    if (!available || remaining_pct < 0 || remaining_pct > 100) {
        set_label_text_if_changed(row.percent, "--%");
        if (!row.bar_value_valid || row.bar_value != 0) {
            lv_bar_set_value(row.bar, 0, LV_ANIM_OFF);
            row.bar_value = 0;
            row.bar_value_valid = true;
        }
        set_label_text_if_changed(row.reset, "RESET --");
        return;
    }

    char percentage[8];
    std::snprintf(percentage, sizeof(percentage), "%d%%", remaining_pct);
    set_label_text_if_changed(row.percent, percentage);
    if (!row.bar_value_valid || row.bar_value != remaining_pct) {
        lv_bar_set_value(row.bar, remaining_pct, LV_ANIM_OFF);
        row.bar_value = remaining_pct;
        row.bar_value_valid = true;
    }

    char reset[32];
    format_reset(
        reset,
        sizeof(reset),
        has_reset ? reset_epoch_seconds : 0,
        now_epoch_seconds);
    set_label_text_if_changed(row.reset, reset);
}


void CodexUsagePage::render(
    const CodexUsageTelemetry& usage,
    int64_t now_epoch_seconds)
{
    if (_root == nullptr) {
        return;
    }
    const bool usage_recent = usage.present &&
        usage.status == CodexTelemetryStatus::Ok &&
        recent_timestamp(
            usage.at_epoch_seconds,
            now_epoch_seconds,
            kCodexUsageTtlSeconds);
    CodexTelemetryStatus visible_status = CodexTelemetryStatus::Unavailable;
    if (usage.present) {
        visible_status = usage.status;
        if (visible_status == CodexTelemetryStatus::Ok && !usage_recent) {
            visible_status = CodexTelemetryStatus::Stale;
        }
    }
    if (usage_recent && usage.quota_status != CodexTelemetryStatus::Ok) {
        if (usage.quota_status == CodexTelemetryStatus::Stale) {
            set_text(_status, "QUOTA EXPIRED", kAlertVermilion, _status_style);
        } else if (usage.quota_status == CodexTelemetryStatus::Error) {
            set_text(_status, "QUOTA ERROR", kAlertVermilion, _status_style);
        } else {
            set_text(_status, "QUOTA UNAVAILABLE", kEtchedGray, _status_style);
        }
    } else {
        set_text(
            _status,
            status_text(visible_status),
            status_color(visible_status),
            _status_style);
    }

    const bool quota_recent = usage.present &&
        usage.quota_status == CodexTelemetryStatus::Ok &&
        usage.has_quota_timestamp &&
        recent_timestamp(
            usage.quota_at_epoch_seconds,
            now_epoch_seconds,
            kCodexQuotaTtlSeconds);
    render_quota(
        _week_quota,
        usage.week_remaining_pct,
        usage.week_reset_epoch_seconds,
        usage.has_week_reset,
        quota_recent,
        now_epoch_seconds);

    if (usage_recent) {
        char estimated[48];
        format_cost(estimated, sizeof(estimated), usage.estimated_api_equivalent_usd,
                    usage.has_estimated_cost, usage.cost_status);
        set_text(_estimate, estimated, usage.has_estimated_cost
                     ? kSignalWhite : kEtchedGray, _estimate_style);
    } else {
        set_text(_estimate, "--", kEtchedGray, _estimate_style);
    }

    if (usage_recent && usage.has_today_total_tokens) {
        char tokens[32];
        char formatted_tokens[20];
        const bool lower_bound = usage.details.present
            ? usage.details.tokens_partial
            : usage.cost_status == CodexCostStatus::Partial;
        format_tokens(
            formatted_tokens,
            sizeof(formatted_tokens),
            usage.today_total_tokens,
            lower_bound);
        std::snprintf(tokens, sizeof(tokens), "%s%s", lower_bound ? ">=" : "",
                      formatted_tokens);
        set_text(_tokens, tokens, kSignalWhite, _tokens_style);
    } else {
        set_text(_tokens, "--", kEtchedGray, _tokens_style);
    }

    char providers[96] = {};
    for (size_t index = 0; quota_recent && index < usage.provider_count; ++index) {
        const CodexProviderQuota& provider = usage.providers[index];
        if (!provider.present) {
            continue;
        }
        const char* name = provider.provider == CodexTelemetryProvider::Claude
                               ? "CLAUDE"
                               : "OPENCODE";
        char value[12];
        if (provider.status == CodexTelemetryStatus::Ok &&
            provider.week_remaining_pct >= 0) {
            std::snprintf(value, sizeof(value), "%d%%", provider.week_remaining_pct);
        } else {
            std::snprintf(value, sizeof(value), "--");
        }
        const size_t used = std::strlen(providers);
        std::snprintf(providers + used, sizeof(providers) - used, "%s%s 7D %s",
                      used == 0 ? "" : "  /  ", name, value);
    }
    set_text(_providers, providers, kEtchedGray, _providers_style);

    _details = usage_recent ? usage.details : CodexUsageDetails{};
    // Retain the selected name through expiry and ordinary task messages. Only
    // pick a replacement when a fresh snapshot actually has different models.
    if (_details.present && _details.model_count != 0) {
        _selected_model_index = 0;
        for (size_t index = 0; index < _details.model_count; ++index) {
            if (std::strcmp(_details.models[index].model.data(),
                            _selected_model.data()) == 0) {
                _selected_model_index = index;
                break;
            }
        }
        _selected_model = _details.models[_selected_model_index].model;
    }
    render_details();
}

void CodexUsagePage::model_arrow_event(lv_event_t* event)
{
    auto* self = static_cast<CodexUsagePage*>(lv_event_get_user_data(event));
    lv_indev_t* input = lv_indev_active();
    if (self == nullptr || input == nullptr) {
        return;
    }
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        lv_indev_get_point(input, &self->_arrow_press_point);
        self->_arrow_pressed = true;
    } else if (code == LV_EVENT_PRESS_LOST) {
        self->_arrow_pressed = false;
    } else if (code == LV_EVENT_CLICKED && self->_arrow_pressed) {
        self->_arrow_pressed = false;
        lv_point_t released;
        lv_indev_get_point(input, &released);
        if (std::abs(released.x - self->_arrow_press_point.x) <= 12 &&
            std::abs(released.y - self->_arrow_press_point.y) <= 12) {
            self->select_model(lv_event_get_target(event) == self->_previous_model
                                   ? -1 : 1);
        }
    }
}

void CodexUsagePage::select_model(int direction)
{
    if (!_details.present || _details.model_count < 2) {
        return;
    }
    _selected_model_index = direction < 0
        ? (_selected_model_index + _details.model_count - 1) % _details.model_count
        : (_selected_model_index + 1) % _details.model_count;
    _selected_model = _details.models[_selected_model_index].model;
    render_details();
}

void CodexUsagePage::render_details()
{
    const bool available = _details.present && _details.model_count > 0;
    set_visible(_previous_model, available && _details.model_count > 1);
    set_visible(_next_model, available && _details.model_count > 1);
    if (!available) {
        char position[80] = "MODEL DETAIL";
        if (_details.present && _details.more_models > 0) {
            std::snprintf(position, sizeof(position), "0 SHOWN / +%llu MORE",
                          static_cast<unsigned long long>(_details.more_models));
        } else if (_details.present && _details.tokens_partial) {
            std::snprintf(position, sizeof(position), "MODEL DETAIL / TOKENS PARTIAL");
        }
        set_label_text_if_changed(_model_position, position);
        set_label_text_if_changed(_model_name,
            _details.present && _details.more_models > 0 ? "DETAILS NOT SENT" :
            _details.present && !_details.tokens_partial ? "NO MODEL USAGE" :
            "DETAILS UNAVAILABLE");
        set_text(_model_summary, "-- TOKENS  /  -- USD", kEtchedGray,
                 _model_summary_style);
        for (size_t index = 0; index < _token_buckets.size(); ++index) {
            set_text(_token_buckets[index], "--", kEtchedGray, _bucket_styles[index]);
        }
        for (size_t index = 0; index < _performance_values.size(); ++index) {
            set_text(_performance_values[index], "--", kEtchedGray,
                     _performance_styles[index]);
            set_label_text_if_changed(_performance_samples[index], "--/-- samples");
        }
        return;
    }
    const CodexModelUsage& model = _details.models[_selected_model_index];
    char position[96];
    char more[40] = {};
    if (_details.more_models > 0) {
        std::snprintf(more, sizeof(more), " / +%llu MORE",
                      static_cast<unsigned long long>(_details.more_models));
    }
    std::snprintf(position, sizeof(position), "%s %u/%u%s%s",
                  _details.source == CodexUsageDetailSource::CcsSession
                      ? "CCS" : "ROLLOUT",
                  static_cast<unsigned>(_selected_model_index + 1),
                  static_cast<unsigned>(_details.model_count), more,
                  _details.tokens_partial ? " / PARTIAL TOK" : "");
    set_label_text_if_changed(_model_position, position);
    set_label_text_if_changed(_model_name, model.model.data());

    char tokens[24];
    char cost[48];
    char summary[96];
    format_tokens(tokens, sizeof(tokens), model.total_tokens, model.tokens_partial);
    format_cost(cost, sizeof(cost), model.estimated_api_equivalent_usd,
                model.has_estimated_cost, model.cost_status);
    std::snprintf(summary, sizeof(summary), "%s%s TOK  /  %s",
                  model.tokens_partial ? ">=" : "", tokens, cost);
    set_text(_model_summary, summary, kSignalWhite, _model_summary_style);
    const uint64_t buckets[] = {model.fresh_input_tokens, model.output_tokens,
                                model.cache_read_tokens, model.cache_write_tokens};
    for (size_t index = 0; index < _token_buckets.size(); ++index) {
        format_tokens(tokens, sizeof(tokens), buckets[index], model.tokens_partial);
        set_text(_token_buckets[index], tokens, kSignalWhite, _bucket_styles[index]);
    }

    char tps[24] = "--";
    char ttft[24] = "--";
    if (model.has_estimated_tps) {
        if (model.estimated_tps < 0.01) {
            std::snprintf(tps, sizeof(tps), "<0.01");
        } else {
            std::snprintf(tps, sizeof(tps), model.estimated_tps >= 10000.0
                              ? "%.1fk" : "%.2f",
                          model.estimated_tps >= 10000.0
                              ? model.estimated_tps / 1000.0 : model.estimated_tps);
        }
    }
    if (model.has_estimated_ttft) {
        if (model.estimated_ttft_ms >= 1000.0) {
            std::snprintf(ttft, sizeof(ttft), "%.2f s", model.estimated_ttft_ms / 1000.0);
        } else if (model.estimated_ttft_ms >= 1.0) {
            std::snprintf(ttft, sizeof(ttft), "%.0f ms", model.estimated_ttft_ms);
        } else {
            std::snprintf(ttft, sizeof(ttft), "<1 ms");
        }
    }
    set_text(_performance_values[0], tps, model.has_estimated_tps
                 ? kQuotaMint : kEtchedGray, _performance_styles[0]);
    set_text(_performance_values[1], ttft, model.has_estimated_ttft
                 ? kQuotaMint : kEtchedGray, _performance_styles[1]);
    const uint64_t samples[] = {model.tps_samples, model.ttft_samples};
    for (size_t index = 0; index < _performance_samples.size(); ++index) {
        char coverage[64];
        format_sample_count(coverage, sizeof(coverage), samples[index], model.requests);
        set_label_text_if_changed(_performance_samples[index], coverage);
    }
}

void CodexUsagePage::shutdown()
{
    _root = nullptr;
    _status = nullptr;
    _week_quota = {};
    _status_style = {};
    _estimate_style = {};
    _tokens_style = {};
    _providers_style = {};
    _estimate = nullptr;
    _tokens = nullptr;
    _providers = nullptr;
    _model_position = nullptr;
    _model_name = nullptr;
    _model_summary = nullptr;
    _previous_model = nullptr;
    _next_model = nullptr;
    _token_buckets = {};
    _performance_values = {};
    _performance_samples = {};
    _model_summary_style = {};
    _bucket_styles = {};
    _performance_styles = {};
    _details = {};
    _selected_model = {};
    _selected_model_index = 0;
    _arrow_press_point = {};
    _arrow_pressed = false;
}


void CodexHostPage::init(lv_obj_t* parent)
{
    shutdown();
    if (parent == nullptr) {
        return;
    }
    _root = parent;
    style_page(_root);
    make_text(
        _root, "HOST", kContentLeft, kHeaderY, kContentWidth,
        &lv_font_montserrat_22, kSignalWhite, LV_TEXT_ALIGN_CENTER);
    _status = make_text(
        _root, "UNAVAILABLE", kContentLeft, kStatusY, kContentWidth,
        &lv_font_montserrat_14, kEtchedGray, LV_TEXT_ALIGN_CENTER);

    constexpr int kLabelX = 54;
    constexpr int kBarX = 112;
    constexpr int kBarWidth = 245;
    constexpr int kValueX = 363;
    constexpr int kValueWidth = 50;
    const int row_y[] = {kHostCpuY, kHostMemoryY, kHostDiskY};
    const char* row_names[] = {"CPU", "MEM", "DISK"};
    for (size_t index = 0; index < _percentage_rows.size(); ++index) {
        make_text(
            _root,
            row_names[index],
            kLabelX,
            row_y[index],
            kBarX - kLabelX - 8,
            &lv_font_montserrat_14,
            kEtchedGray);
        _percentage_rows[index] = {
            make_text(
                _root,
                "--",
                kValueX,
                row_y[index] - 4,
                kValueWidth,
                &lv_font_montserrat_18,
                kSignalWhite,
                LV_TEXT_ALIGN_RIGHT),
            make_bar(_root, kBarX, row_y[index] + 27, kBarWidth),
        };
    }
    make_text(
        _root,
        "NETWORK",
        72,
        kHostNetworkY,
        322,
        &lv_font_montserrat_14,
        kEtchedGray,
        LV_TEXT_ALIGN_CENTER);
    _network_up = make_text(
        _root,
        "UP --",
        69,
        kHostNetworkY + 24,
        328,
        &lv_font_montserrat_14,
        kEtchedGray,
        LV_TEXT_ALIGN_CENTER);
    _network_down = make_text(
        _root,
        "DOWN --",
        69,
        kHostNetworkY + 43,
        328,
        &lv_font_montserrat_14,
        kEtchedGray,
        LV_TEXT_ALIGN_CENTER);
    render({}, 0);
}


void CodexHostPage::render_percentage(
    MetricRow& row,
    double value,
    bool present,
    bool available)
{
    if (row.value == nullptr || row.bar == nullptr) {
        return;
    }
    if (!available || !present || !std::isfinite(value) || value < 0.0 || value > 100.0) {
        set_label_text_if_changed(row.value, "--");
        if (!row.bar_value_valid || row.bar_value != 0) {
            lv_bar_set_value(row.bar, 0, LV_ANIM_OFF);
            row.bar_value = 0;
            row.bar_value_valid = true;
        }
        return;
    }
    char formatted[12];
    format_percent(formatted, sizeof(formatted), value);
    set_label_text_if_changed(row.value, formatted);
    const int bar_value = static_cast<int>(std::lround(value));
    if (!row.bar_value_valid || row.bar_value != bar_value) {
        lv_bar_set_value(row.bar, bar_value, LV_ANIM_OFF);
        row.bar_value = bar_value;
        row.bar_value_valid = true;
    }
}


void CodexHostPage::render(const CodexHostTelemetry& host, int64_t now_epoch_seconds)
{
    if (_root == nullptr) {
        return;
    }
    const bool host_recent = host.present &&
        host.status == CodexTelemetryStatus::Ok &&
        recent_timestamp(
            host.at_epoch_seconds,
            now_epoch_seconds,
            kCodexHostTtlSeconds);
    CodexTelemetryStatus visible_status = CodexTelemetryStatus::Unavailable;
    if (host.present) {
        visible_status = host.status;
        if (visible_status == CodexTelemetryStatus::Ok && !host_recent) {
            visible_status = CodexTelemetryStatus::Stale;
        }
    }
    set_text(
        _status,
        status_text(visible_status),
        status_color(visible_status),
        _status_style);

    render_percentage(_percentage_rows[0], host.cpu_pct, host.has_cpu, host_recent);
    render_percentage(
        _percentage_rows[1], host.memory_pct, host.has_memory, host_recent);
    render_percentage(_percentage_rows[2], host.disk_pct, host.has_disk, host_recent);

    if (host_recent && host.has_network_up) {
        char rate[24];
        char text[40];
        format_rate(rate, sizeof(rate), host.network_up_kib_s);
        std::snprintf(text, sizeof(text), "UP  %s", rate);
        set_text(_network_up, text, kSignalWhite, _network_up_style);
    } else {
        set_text(_network_up, "UP  --", kEtchedGray, _network_up_style);
    }
    if (host_recent && host.has_network_down) {
        char rate[24];
        char text[40];
        format_rate(rate, sizeof(rate), host.network_down_kib_s);
        std::snprintf(text, sizeof(text), "DOWN  %s", rate);
        set_text(_network_down, text, kSignalWhite, _network_down_style);
    } else {
        set_text(_network_down, "DOWN  --", kEtchedGray, _network_down_style);
    }
}


void CodexHostPage::shutdown()
{
    _root = nullptr;
    _status = nullptr;
    _percentage_rows = {};
    _status_style = {};
    _network_up = nullptr;
    _network_down = nullptr;
    _network_up_style = {};
    _network_down_style = {};
}
