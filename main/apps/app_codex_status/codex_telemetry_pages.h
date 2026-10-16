#pragma once

#include "codex_telemetry_model.h"

#include <lvgl.h>

#include <array>
#include <cstdint>


struct TelemetryTextStyleCache {
    uint32_t text_color = 0;
    bool valid = false;
};


class CodexUsagePage {
public:
    void init(lv_obj_t* parent);
    void render(const CodexUsageTelemetry& usage, int64_t now_epoch_seconds);
    void shutdown();

private:
    struct QuotaRow {
        lv_obj_t* percent = nullptr;
        lv_obj_t* bar = nullptr;
        lv_obj_t* reset = nullptr;
        int bar_value = 0;
        bool bar_value_valid = false;
    };

    void render_quota(
        QuotaRow& row,
        int remaining_pct,
        int64_t reset_epoch_seconds,
        bool has_reset,
        bool available,
        int64_t now_epoch_seconds);
    static void model_arrow_event(lv_event_t* event);
    void render_details();
    void select_model(int direction);

    lv_obj_t* _root = nullptr;
    lv_obj_t* _status = nullptr;
    QuotaRow _week_quota;
    TelemetryTextStyleCache _status_style;
    TelemetryTextStyleCache _estimate_style;
    TelemetryTextStyleCache _tokens_style;
    TelemetryTextStyleCache _providers_style;
    lv_obj_t* _estimate = nullptr;
    lv_obj_t* _tokens = nullptr;
    lv_obj_t* _providers = nullptr;
    lv_obj_t* _model_position = nullptr;
    lv_obj_t* _model_name = nullptr;
    lv_obj_t* _model_summary = nullptr;
    lv_obj_t* _previous_model = nullptr;
    lv_obj_t* _next_model = nullptr;
    std::array<lv_obj_t*, 4> _token_buckets{};
    std::array<lv_obj_t*, 2> _performance_values{};
    std::array<lv_obj_t*, 2> _performance_samples{};
    TelemetryTextStyleCache _model_summary_style;
    std::array<TelemetryTextStyleCache, 4> _bucket_styles{};
    std::array<TelemetryTextStyleCache, 2> _performance_styles{};
    CodexUsageDetails _details;
    std::array<char, kCodexUsageModelNameBytes + 1> _selected_model{};
    size_t _selected_model_index = 0;
    lv_point_t _arrow_press_point{};
    bool _arrow_pressed = false;
};


class CodexHostPage {
public:
    void init(lv_obj_t* parent);
    void render(const CodexHostTelemetry& host, int64_t now_epoch_seconds);
    void shutdown();

private:
    struct MetricRow {
        lv_obj_t* value = nullptr;
        lv_obj_t* bar = nullptr;
        int bar_value = 0;
        bool bar_value_valid = false;
    };

    void render_percentage(MetricRow& row, double value, bool present, bool available);

    lv_obj_t* _root = nullptr;
    lv_obj_t* _status = nullptr;
    std::array<MetricRow, 3> _percentage_rows{};
    TelemetryTextStyleCache _status_style;
    lv_obj_t* _network_up = nullptr;
    lv_obj_t* _network_down = nullptr;
    TelemetryTextStyleCache _network_up_style;
    TelemetryTextStyleCache _network_down_style;
};
