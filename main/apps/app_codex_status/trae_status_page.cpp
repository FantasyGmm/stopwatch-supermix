#include "trae_status_page.h"

#include "assets/trae_robot_assets.h"
#include "trae_status_ui_layout.h"

#include <assets/assets.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>


LV_FONT_DECLARE(font_puhui_14_1)
LV_FONT_DECLARE(NotoSansSCBold16Trae)
LV_FONT_DECLARE(CodexTaskTitle21)

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


lv_obj_t* make_segment(
    lv_obj_t* parent,
    int x,
    int y,
    int width,
    int height,
    uint32_t color,
    lv_opa_t opacity
)
{
    lv_obj_t* segment = lv_obj_create(parent);
    lv_obj_set_size(segment, width, height);
    lv_obj_set_pos(segment, x, y);
    lv_obj_set_style_border_width(segment, 0, 0);
    lv_obj_set_style_pad_all(segment, 0, 0);
    lv_obj_set_style_radius(segment, 0, 0);
    lv_obj_set_style_bg_color(segment, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(segment, opacity, 0);
    lv_obj_clear_flag(segment, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(segment, LV_OBJ_FLAG_SCROLLABLE);
    return segment;
}


lv_obj_t* make_spine(lv_obj_t* parent)
{
    return make_segment(
        parent,
        kSpineX,
        kSpineY,
        kSpineWidth,
        kSpineHeight,
        kDimText,
        LV_OPA_50
    );
}


lv_obj_t* make_status_dot(lv_obj_t* parent, int x)
{
    lv_obj_t* dot = make_segment(
        parent,
        x,
        kStatusDotY,
        kHealthDotSize,
        kHealthDotSize,
        kDimText,
        LV_OPA_COVER
    );
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    return dot;
}


lv_obj_t* make_effort_badge(lv_obj_t* parent, lv_obj_t** label_out)
{
    lv_obj_t* badge = lv_obj_create(parent);
    lv_obj_set_size(badge, 48, kEffortBadgeHeight);
    lv_obj_set_style_radius(badge, 7, 0);
    lv_obj_set_style_border_width(badge, 1, 0);
    lv_obj_set_style_border_color(badge, lv_color_hex(0x748197), 0);
    lv_obj_set_style_border_opa(badge, LV_OPA_60, 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(badge, 0, 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* label = make_label(
        badge,
        "--",
        0,
        3,
        48,
        &MontserratBold13Trae,
        kPrimaryText
    );
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_CLIP);
    *label_out = label;
    return badge;
}


const char* display_or_dash(const char* value)
{
    return value != nullptr && value[0] != '\0' ? value : "--";
}


bool is_active_task(const TraeTask* task)
{
    return task != nullptr &&
           (task->state == TraeTaskState::Pending ||
            task->state == TraeTaskState::Running);
}


bool has_non_ascii(const char* value)
{
    if (value == nullptr) {
        return false;
    }
    while (*value != '\0') {
        if (static_cast<unsigned char>(*value) >= 0x80) {
            return true;
        }
        ++value;
    }
    return false;
}


void set_hidden(lv_obj_t* object, bool hidden)
{
    if (object == nullptr) {
        return;
    }
    if (hidden) {
        lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(object, LV_OBJ_FLAG_HIDDEN);
    }
}


void set_channel(
    lv_obj_t* dot,
    lv_obj_t* value,
    const TraeChannelState& state
)
{
    lv_label_set_text(value, display_or_dash(state.text));
    lv_obj_set_style_text_color(value, lv_color_hex(state.text_color), 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(state.dot_color), 0);
}


void uppercase_short(char* output, size_t output_size, const char* value)
{
    if (output == nullptr || output_size == 0) {
        return;
    }
    const char* safe = display_or_dash(value);
    size_t index = 0;
    for (; index + 1 < output_size && safe[index] != '\0'; ++index) {
        output[index] = static_cast<char>(std::toupper(
            static_cast<unsigned char>(safe[index])
        ));
    }
    output[index] = '\0';
}


struct LabelTypography {
    const lv_font_t* font;
    int scale;
};


LabelTypography project_typography(const char* value, bool single)
{
    if (has_non_ascii(value)) {
        return {&font_puhui_14_1, kScale14To17};
    }
    return {
        single ? &MontserratBold17Trae : &MontserratBold13Trae,
        LV_SCALE_NONE,
    };
}


LabelTypography title_typography(const char* value, bool single)
{
    const lv_font_t* font = nullptr;
    int base_font_px = 0;
    if (has_non_ascii(value)) {
        font = single ? &CodexTaskTitle21 : &NotoSansSCBold16Trae;
        base_font_px = single ? 21 : 16;
    } else {
        font = single ? &MontserratBold28Trae : &MontserratBold24Trae;
        base_font_px = single ? 28 : 24;
    }
    lv_point_t raw_size{};
    lv_text_get_size(
        &raw_size,
        value,
        font,
        0,
        0,
        LV_COORD_MAX,
        LV_TEXT_FLAG_NONE
    );
    return {
        font,
        title_font_scale(
            raw_size.x,
            kTaskRowWidth,
            base_font_px,
            single ? kSingleTaskTitleMinFontPx
                   : kMultiTaskTitleMinFontPx,
            single ? kSingleTaskTitleMaxFontPx
                   : kMultiTaskTitleMaxFontPx
        ),
    };
}


LabelTypography model_typography(const char* value)
{
    return has_non_ascii(value)
               ? LabelTypography{&font_puhui_14_1, kScale14To15}
               : LabelTypography{&MontserratMedium15Trae, LV_SCALE_NONE};
}


int visual_text_height(
    const char* text,
    const LabelTypography& typography
)
{
    lv_point_t size{};
    lv_text_get_size(
        &size,
        text,
        typography.font,
        0,
        0,
        LV_COORD_MAX,
        LV_TEXT_FLAG_NONE
    );
    return scaled_visual_height(size.y, typography.scale);
}


void set_text_scale(lv_obj_t* label, int scale, int pivot_x = 0)
{
    lv_obj_set_style_transform_scale_x(label, scale, 0);
    lv_obj_set_style_transform_scale_y(label, scale, 0);
    lv_obj_set_style_transform_pivot_x(label, pivot_x, 0);
    lv_obj_set_style_transform_pivot_y(label, 0, 0);
}


void apply_left_typography(
    lv_obj_t* label,
    const LabelTypography& typography,
    int visual_width
)
{
    lv_obj_set_style_text_font(label, typography.font, 0);
    set_text_scale(label, typography.scale);
    lv_obj_set_width(
        label,
        std::max(1, visual_width * LV_SCALE_NONE / typography.scale)
    );
}


void set_count(lv_obj_t* label, uint16_t count)
{
    char text[8];
    std::snprintf(text, sizeof(text), "%u", static_cast<unsigned>(count));
    lv_label_set_text(label, text);
}

}  // namespace


void TraeStatusPage::init(lv_obj_t* parent)
{
    shutdown();
    if (parent == nullptr) {
        return;
    }

    _root = lv_obj_create(parent);
    lv_obj_set_size(_root, kScreenSize, kScreenSize);
    lv_obj_set_pos(_root, 0, 0);
    lv_obj_set_style_radius(_root, 0, 0);
    lv_obj_set_style_border_width(_root, 0, 0);
    lv_obj_set_style_pad_all(_root, 0, 0);
    lv_obj_set_style_bg_color(_root, lv_color_hex(kBackground), 0);
    lv_obj_set_style_bg_opa(_root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(_root, LV_OBJ_FLAG_SCROLLABLE);

    _brand_icon = lv_image_create(_root);
    lv_image_set_src(_brand_icon, &trae_robot_head);
    lv_image_set_antialias(_brand_icon, false);
    lv_image_set_pivot(_brand_icon, 0, 0);
    lv_image_set_scale_x(_brand_icon, kBrandIconScaleX);
    lv_image_set_scale_y(_brand_icon, kBrandIconScaleY);
    lv_obj_set_pos(_brand_icon, kBrandIconX, kBrandIconRenderY);
    lv_obj_clear_flag(_brand_icon, LV_OBJ_FLAG_CLICKABLE);

    make_label(
        _root,
        "TRAE",
        kBrandX,
        kBrandY,
        kConsoleWidth,
        &MontserratBold29Trae,
        kPrimaryText
    );

    lv_obj_t* agent_caption = make_label(
        _root,
        "AGENT",
        kAgentX,
        kHealthCaptionY,
        kAgentWidth,
        &MontserratMedium13Trae,
        kDimText
    );
    lv_obj_set_style_text_letter_space(agent_caption, 2, 0);
    _agent_dot = make_status_dot(_root, kAgentX);
    _agent_status = make_label(
        _root,
        "--",
        kAgentX + kStatusTextInset,
        kHealthValueY,
        kAgentWidth - kStatusTextInset,
        &MontserratBold15Trae,
        kSecondaryText
    );

    lv_obj_t* voice_caption = make_label(
        _root,
        "VOICE",
        kVoiceX,
        kHealthCaptionY,
        kVoiceWidth,
        &MontserratMedium13Trae,
        kDimText
    );
    lv_obj_set_style_text_letter_space(voice_caption, 2, 0);
    _voice_dot = make_status_dot(_root, kVoiceX);
    _voice_status = make_label(
        _root,
        "--",
        kVoiceX + kStatusTextInset,
        kHealthValueY,
        kVoiceWidth - kStatusTextInset,
        &MontserratBold15Trae,
        kSecondaryText
    );

    make_spine(_root);
    _pending_count = make_label(
        _root,
        "0",
        kPendingCountX,
        kPendingCountY,
        kRailCountWidth,
        &MontserratBold26Digits,
        kPending
    );
    lv_obj_set_style_text_align(_pending_count, LV_TEXT_ALIGN_RIGHT, 0);
    _pending_label = make_label(
        _root,
        "PENDING",
        kPendingCountX,
        kPendingCountY + kRailLabelOffsetY,
        kRailCountWidth,
        &MontserratMedium14Trae,
        kDimText
    );
    lv_obj_set_style_text_align(_pending_label, LV_TEXT_ALIGN_RIGHT, 0);
    _running_count = make_label(
        _root,
        "0",
        kRunningCountX,
        kRunningCountY,
        kRailCountWidth,
        &MontserratBold26Digits,
        kRunning
    );
    lv_obj_set_style_text_align(_running_count, LV_TEXT_ALIGN_RIGHT, 0);
    _running_label = make_label(
        _root,
        "RUNNING",
        kRunningCountX,
        kRunningCountY + kRailLabelOffsetY,
        kRailCountWidth,
        &MontserratMedium14Trae,
        kDimText
    );
    lv_obj_set_style_text_align(_running_label, LV_TEXT_ALIGN_RIGHT, 0);

    _task_group_label = make_label(
        _root,
        "PENDING TASKS • 0",
        kTaskLabelX,
        kTaskLabelY,
        kTaskLabelWidth,
        &MontserratBold14Trae,
        kPending
    );
    lv_obj_set_style_text_letter_space(_task_group_label, 1, 0);

    for (TaskRow& row : _task_rows) {
        row.project = make_label(
            _root,
            "--",
            kTaskProjectX,
            0,
            kTaskRowWidth,
            &MontserratBold13Trae,
            kProjectText
        );
        row.title = make_label(
            _root,
            "--",
            kTaskTitleX,
            0,
            kTaskRowWidth,
            &NotoSansSCBold16Trae,
            kPrimaryText
        );
        row.meta = lv_obj_create(_root);
        lv_obj_set_size(row.meta, kTaskRowWidth, 22);
        lv_obj_set_style_radius(row.meta, 0, 0);
        lv_obj_set_style_border_width(row.meta, 0, 0);
        lv_obj_set_style_pad_all(row.meta, 0, 0);
        lv_obj_set_style_bg_opa(row.meta, LV_OPA_TRANSP, 0);
        lv_obj_clear_flag(row.meta, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(row.meta, LV_OBJ_FLAG_SCROLLABLE);
        row.model = make_label(
            row.meta,
            "--",
            0,
            3,
            kTaskModelMaxWidth,
            &MontserratMedium15Trae,
            kSecondaryText
        );
        row.effort_badge = make_effort_badge(row.meta, &row.effort);
    }

    _task_divider = make_segment(
        _root,
        kTaskLabelX,
        kTaskDividerY,
        kTaskLabelWidth,
        1,
        kDimText,
        LV_OPA_30
    );
    _running_summary = make_label(
        _root,
        "",
        kTaskLabelX,
        kRunningSummaryY,
        kTaskLabelWidth,
        &MontserratMedium13Trae,
        kRunning
    );
    lv_obj_set_style_text_letter_space(_running_summary, 1, 0);
    _empty_state = make_label(
        _root,
        "",
        kTaskLabelX,
        kEmptyStateY,
        kTaskLabelWidth,
        &font_puhui_14_1,
        kSecondaryText
    );
    lv_obj_set_style_text_align(_empty_state, LV_TEXT_ALIGN_CENTER, 0);
    set_text_scale(_empty_state, kScale14To17, kTaskLabelWidth / 2);
    set_hidden(_empty_state, true);
}


void TraeStatusPage::render(
    const TraeStatusSnapshot& snapshot,
    const TraeTask* task,
    const TraeTask* next_task,
    const TraeChannelPresentation& channels,
    bool stale,
    bool bluetooth_connected
)
{
    if (_root == nullptr) {
        return;
    }

    set_channel(_agent_dot, _agent_status, channels.agent);
    set_channel(_voice_dot, _voice_status, channels.voice);
    set_count(_pending_count, snapshot.pending_count);
    set_count(_running_count, snapshot.running_count);

    const bool active_task = is_active_task(task);
    const char* empty_text = empty_state_text(resolve_empty_state(
        bluetooth_connected,
        stale,
        snapshot.process_state,
        snapshot.pending_count,
        snapshot.running_count,
        active_task
    ));
    if (empty_text != nullptr) {
        set_hidden(_task_group_label, true);
        set_task_row_hidden(0, true);
        set_task_row_hidden(1, true);
        set_hidden(_task_divider, true);
        set_hidden(_running_summary, true);
        lv_label_set_text(_empty_state, empty_text);
        set_hidden(_empty_state, false);
        return;
    }

    set_hidden(_empty_state, true);
    const uint16_t group_count = task->state == TraeTaskState::Pending
                                     ? snapshot.pending_count
                                     : snapshot.running_count;
    char group_text[40];
    std::snprintf(
        group_text,
        sizeof(group_text),
        "%s • %u",
        task_group_label(task->state),
        static_cast<unsigned>(group_count)
    );
    lv_label_set_text(_task_group_label, group_text);
    lv_obj_set_style_text_color(
        _task_group_label,
        lv_color_hex(task_group_color(task->state)),
        0
    );
    set_hidden(_task_group_label, false);

    const size_t active_count = static_cast<size_t>(snapshot.pending_count) +
                                static_cast<size_t>(snapshot.running_count);
    const TaskLayout layout = resolve_task_layout(active_count);
    const bool single = layout == TaskLayout::Single;
    TraeTask fallback;
    fallback.model = snapshot.model;
    fallback.effort = snapshot.effort;
    render_task_row(0, task, fallback, single);

    const bool show_second = !single && is_active_task(next_task);
    if (show_second) {
        render_task_row(1, next_task, fallback, false);
    } else {
        set_task_row_hidden(1, true);
    }
    set_hidden(_task_divider, !show_second);

    char summary[40] = {};
    if (!single && task->state == TraeTaskState::Pending &&
        snapshot.running_count > 0) {
        std::snprintf(
            summary,
            sizeof(summary),
            "RUNNING   %u TASKS",
            static_cast<unsigned>(snapshot.running_count)
        );
    } else if (!single && task->state == TraeTaskState::Running &&
               snapshot.pending_count > 0) {
        std::snprintf(
            summary,
            sizeof(summary),
            "PENDING   %u TASKS",
            static_cast<unsigned>(snapshot.pending_count)
        );
    }
    lv_label_set_text(_running_summary, summary);
    set_hidden(_running_summary, summary[0] == '\0');
}


void TraeStatusPage::render_task_row(
    size_t index,
    const TraeTask* task,
    const TraeTask& fallback,
    bool single
)
{
    if (index >= _task_rows.size() || !is_active_task(task)) {
        set_task_row_hidden(index, true);
        return;
    }

    TaskRow& row = _task_rows[index];
    const TaskRowGeometry geometry = single
                                         ? kSingleTaskRow
                                         : kMultiTaskRows[index];
    lv_obj_set_pos(row.project, kTaskProjectX, geometry.project_y);
    lv_obj_set_pos(row.title, kTaskTitleX, geometry.title_y);

    const char* project = display_or_dash(task->project.data());
    const char* title = display_or_dash(task->title.data());
    const char* model = task->model[0] == '\0'
                            ? display_or_dash(fallback.model.data())
                            : task->model.data();
    const char* effort = task->effort[0] == '\0'
                             ? display_or_dash(fallback.effort.data())
                             : task->effort.data();

    lv_label_set_text(row.project, project);
    apply_left_typography(
        row.project,
        project_typography(project, single),
        kTaskRowWidth
    );
    lv_label_set_text(row.title, title);
    const LabelTypography title_style = title_typography(title, single);
    apply_left_typography(row.title, title_style, kTaskRowWidth);
    lv_label_set_long_mode(row.title, LV_LABEL_LONG_MODE_DOTS);
    const int measured_title_height =
        visual_text_height(title, title_style);
    const int title_visual_height = single
                                        ? std::min(
                                              measured_title_height,
                                              kSingleTitleMaxHeight
                                          )
                                        : measured_title_height;
    lv_obj_set_height(
        row.title,
        std::max(
            1,
            title_visual_height * LV_SCALE_NONE / title_style.scale
        )
    );
    lv_obj_set_pos(
        row.meta,
        kTaskMetaX,
        single ? single_task_meta_y(title_visual_height) : geometry.meta_y
    );

    lv_label_set_text(row.model, model);
    const LabelTypography model_style = model_typography(model);
    lv_obj_set_style_text_font(row.model, model_style.font, 0);
    set_text_scale(row.model, model_style.scale);
    lv_point_t model_size{};
    lv_text_get_size(
        &model_size,
        model,
        model_style.font,
        0,
        0,
        LV_COORD_MAX,
        LV_TEXT_FLAG_NONE
    );
    const int model_visual_width = std::max(
        1,
        std::min(
            static_cast<int>(model_size.x) * model_style.scale /
                LV_SCALE_NONE,
            kTaskModelMaxWidth
        )
    );
    lv_obj_set_width(
        row.model,
        std::max(
            1,
            model_visual_width * LV_SCALE_NONE / model_style.scale
        )
    );

    char effort_text[34];
    uppercase_short(effort_text, sizeof(effort_text), effort);
    lv_label_set_text(row.effort, effort_text);
    lv_point_t effort_size{};
    lv_text_get_size(
        &effort_size,
        effort_text,
        &MontserratBold13Trae,
        0,
        0,
        LV_COORD_MAX,
        LV_TEXT_FLAG_NONE
    );
    const int badge_width = std::min(
        kTaskRowWidth - model_visual_width - kTaskMetaGap,
        std::max(38, static_cast<int>(effort_size.x) + 2 * kEffortBadgePadX)
    );
    lv_obj_set_size(row.effort_badge, badge_width, kEffortBadgeHeight);
    lv_obj_set_width(row.effort, badge_width);
    lv_obj_set_pos(
        row.effort_badge,
        model_visual_width + kTaskMetaGap,
        0
    );

    set_task_row_hidden(index, false);
}


void TraeStatusPage::set_task_row_hidden(size_t index, bool hidden)
{
    if (index >= _task_rows.size()) {
        return;
    }
    TaskRow& row = _task_rows[index];
    set_hidden(row.project, hidden);
    set_hidden(row.title, hidden);
    set_hidden(row.meta, hidden);
}


void TraeStatusPage::set_active(bool active)
{
    _active = active;
}


bool TraeStatusPage::contains_task_point(int x, int y) const
{
    return _root != nullptr &&
           x >= kTaskFocusX && x < kTaskFocusX + kTaskFocusWidth &&
           y >= kTaskFocusY && y < kTaskFocusY + kTaskFocusHeight;
}


void TraeStatusPage::shutdown()
{
    if (_root != nullptr) {
        lv_obj_delete(_root);
        _root = nullptr;
    }

    _brand_icon = nullptr;
    _agent_dot = nullptr;
    _agent_status = nullptr;
    _voice_dot = nullptr;
    _voice_status = nullptr;
    _pending_label = nullptr;
    _pending_count = nullptr;
    _running_label = nullptr;
    _running_count = nullptr;
    _task_group_label = nullptr;
    _task_divider = nullptr;
    _running_summary = nullptr;
    _empty_state = nullptr;
    _task_rows = {};
    _active = false;
}
