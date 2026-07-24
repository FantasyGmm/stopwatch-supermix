#include "trae_status_page.h"

#include "trae_status_ui_layout.h"

#include <assets/assets.h>

#include <array>


LV_FONT_DECLARE(font_puhui_14_1)

namespace {

using namespace trae_status_ui;


lv_obj_t* make_label(
    lv_obj_t* parent,
    const char* text,
    int x,
    int y,
    int width,
    const lv_font_t* font,
    uint32_t color
)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(label, x, y);
    return label;
}


lv_obj_t* make_center_label(
    lv_obj_t* parent,
    const char* text,
    int y,
    int width,
    const lv_font_t* font,
    uint32_t color
)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, y);
    return label;
}


lv_obj_t* make_line(
    lv_obj_t* parent,
    int x,
    int y,
    int width,
    int height
)
{
    lv_obj_t* line = lv_obj_create(parent);
    lv_obj_set_size(line, width, height);
    lv_obj_set_pos(line, x, y);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_pad_all(line, 0, 0);
    lv_obj_set_style_radius(line, 0, 0);
    lv_obj_set_style_bg_color(line, lv_color_hex(kDimText), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_40, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    return line;
}


lv_obj_t* make_signal_ring(
    lv_obj_t* parent,
    int x,
    int y,
    int size,
    int border_width
)
{
    lv_obj_t* ring = lv_obj_create(parent);
    lv_obj_set_size(ring, size, size);
    lv_obj_set_pos(ring, x, y);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(ring, 0, 0);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ring, border_width, 0);
    lv_obj_set_style_border_color(ring, lv_color_hex(kAccent), 0);
    lv_obj_set_style_border_opa(ring, LV_OPA_70, 0);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
    return ring;
}


void make_signal_mark(lv_obj_t* parent)
{
    make_signal_ring(parent, kSignalX, kSignalY, kSignalSize, 2);
    make_signal_ring(parent, kSignalX + 16, kSignalY + 16, kSignalSize - 32, 2);
    lv_obj_t* core = make_signal_ring(
        parent,
        kSignalX + 34,
        kSignalY + 34,
        kSignalSize - 68,
        0
    );
    lv_obj_set_style_bg_color(core, lv_color_hex(kAccent), 0);
    lv_obj_set_style_bg_opa(core, LV_OPA_COVER, 0);
}


const char* display_or_dash(const char* value)
{
    return value != nullptr && value[0] != '\0' ? value : "--";
}

}  // namespace


void TraeStatusPage::init(lv_obj_t* parent)
{
    shutdown();
    if (parent == nullptr) {
        return;
    }
    _root = parent;

    make_signal_mark(parent);
    make_label(
        parent,
        "Trae",
        kBrandX,
        kBrandY,
        kTelemetryWidth,
        &MontserratSemiBold26,
        kPrimaryText
    );
    _status = make_center_label(
        parent,
        "TRAE WAIT",
        kStatusY,
        200,
        &lv_font_montserrat_14,
        kDimText
    );

    lv_obj_t* model_caption = make_label(
        parent,
        "MODEL",
        kTelemetryX,
        kModelCaptionY,
        kTelemetryWidth,
        &lv_font_montserrat_14,
        kDimText
    );
    lv_obj_set_style_text_letter_space(model_caption, 3, 0);
    _model = make_label(
        parent,
        "--",
        kTelemetryX,
        kModelValueY,
        kTelemetryWidth,
        &MontserratSemiBold26,
        kPrimaryText
    );

    lv_obj_t* effort_caption = make_label(
        parent,
        "EFFORT",
        kTelemetryX,
        kEffortCaptionY,
        kTelemetryWidth,
        &lv_font_montserrat_14,
        kDimText
    );
    lv_obj_set_style_text_letter_space(effort_caption, 3, 0);
    _effort = make_label(
        parent,
        "--",
        kTelemetryX,
        kEffortValueY,
        kTelemetryWidth,
        &MontserratSemiBold26,
        kPrimaryText
    );

    make_line(parent, kDividerX, kDividerY, kDividerWidth, 1);
    make_line(
        parent,
        kColumnDividerX,
        kColumnDividerY,
        1,
        kColumnDividerHeight
    );

    make_label(
        parent,
        "STATE",
        kLeftColumnX,
        kCaptionY,
        kLeftColumnWidth,
        &lv_font_montserrat_14,
        kDimText
    );
    _state = make_label(
        parent,
        "空闲",
        kLeftColumnX,
        kPrimaryValueY,
        kLeftColumnWidth,
        &font_puhui_14_1,
        kDimText
    );
    make_label(
        parent,
        "PROJECT",
        kLeftColumnX,
        kSecondaryCaptionY,
        kLeftColumnWidth,
        &lv_font_montserrat_14,
        kDimText
    );
    _project = make_label(
        parent,
        "--",
        kLeftColumnX,
        kSecondaryValueY,
        kLeftColumnWidth,
        &font_puhui_14_1,
        kAccent
    );

    make_label(
        parent,
        "TASK",
        kRightColumnX,
        kCaptionY,
        kRightColumnWidth,
        &lv_font_montserrat_14,
        kDimText
    );
    _title = make_label(
        parent,
        "--",
        kRightColumnX,
        kPrimaryValueY,
        kRightColumnWidth,
        &font_puhui_14_1,
        kPrimaryText
    );
    _detail = make_label(
        parent,
        "",
        kRightColumnX,
        kSecondaryValueY,
        kRightColumnWidth,
        &font_puhui_14_1,
        kSecondaryText
    );
}


void TraeStatusPage::render(
    const TraeStatusSnapshot& snapshot,
    const TraeTask* task,
    bool stale
)
{
    if (_root == nullptr) {
        return;
    }

    const bool never_received = snapshot.received_at_ms == 0;
    const bool offline = !never_received && !snapshot.online;
    const bool waiting = never_received || stale || task == nullptr;
    const char* status = offline ? "TRAE OFFLINE" : (waiting ? "TRAE WAIT" : "TRAE LIVE");
    const uint32_t status_color =
        offline ? kPending : (waiting ? kDimText : kAccent);
    lv_label_set_text(_status, status);
    lv_obj_set_style_text_color(_status, lv_color_hex(status_color), 0);
    lv_label_set_text(
        _model,
        offline ? "--" : display_or_dash(snapshot.model.data())
    );
    lv_label_set_text(
        _effort,
        offline ? "--" : display_or_dash(snapshot.effort.data())
    );

    if (offline || waiting) {
        lv_label_set_text(_state, "空闲");
        lv_obj_set_style_text_color(_state, lv_color_hex(kDimText), 0);
        lv_label_set_text(_project, "--");
        lv_label_set_text(_title, "--");
        lv_label_set_text(_detail, "");
        return;
    }

    const char* state_text = "已完成";
    uint32_t state_color = kCompleted;
    if (task->state == TraeTaskState::Pending) {
        state_text = "等待指令";
        state_color = kPending;
    } else if (task->state == TraeTaskState::Running) {
        state_text = "正在处理";
        state_color = kRunning;
    }
    lv_label_set_text(_state, state_text);
    lv_obj_set_style_text_color(_state, lv_color_hex(state_color), 0);
    lv_label_set_text(_project, display_or_dash(task->project.data()));
    lv_label_set_text(_title, display_or_dash(task->title.data()));
    lv_label_set_text(_detail, task->detail.data());
}


void TraeStatusPage::shutdown()
{
    _root = nullptr;
    _status = nullptr;
    _model = nullptr;
    _effort = nullptr;
    _state = nullptr;
    _project = nullptr;
    _title = nullptr;
    _detail = nullptr;
}
