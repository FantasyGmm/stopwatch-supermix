#include "codex_task_focus_view.h"

#include "codex_task_focus_layout.h"
#include "codex_task_focus_text.h"

#include <assets/assets.h>

#include <array>
#include <cstddef>


LV_FONT_DECLARE(font_puhui_14_1)

namespace {

using namespace codex_task_focus_layout;


lv_obj_t* make_layer(lv_obj_t* parent)
{
    lv_obj_t* layer = lv_obj_create(parent);
    lv_obj_set_size(layer, kViewportWidth, kViewportHeight);
    lv_obj_set_pos(layer, 0, 0);
    lv_obj_set_style_radius(layer, 0, 0);
    lv_obj_set_style_border_width(layer, 0, 0);
    lv_obj_set_style_pad_all(layer, 0, 0);
    lv_obj_set_style_bg_opa(layer, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(layer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(layer, LV_OBJ_FLAG_SCROLLABLE);
    return layer;
}


lv_obj_t* make_label(
    lv_obj_t* parent,
    const char* text,
    int x,
    int y,
    int width,
    int height,
    const lv_font_t* font,
    uint32_t color,
    lv_text_align_t alignment,
    lv_label_long_mode_t long_mode
)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, long_mode);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, width, height);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, alignment, 0);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_SCROLLABLE);
    return label;
}


lv_obj_t* make_geometry(lv_obj_t* parent)
{
    lv_obj_t* object = lv_obj_create(parent);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    return object;
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


const lv_font_t* project_font(const char* text)
{
    return has_non_ascii(text) ? &font_puhui_14_1 : &lv_font_montserrat_18;
}


const lv_font_t* title_font(const char* text)
{
    return has_non_ascii(text) ? &font_puhui_14_1 : &MontserratSemiBold26;
}


void set_text_scale(lv_obj_t* label, int scale)
{
    lv_obj_set_style_transform_scale_x(label, scale, 0);
    lv_obj_set_style_transform_scale_y(label, scale, 0);
    lv_obj_set_style_transform_pivot_x(label, 0, 0);
    lv_obj_set_style_transform_pivot_y(label, 0, 0);
}


const char* status_text(CodexTaskState state)
{
    return state == CodexTaskState::Pending ? "等待你的指令" : "正在执行";
}


const char* footer_text(CodexTaskState state)
{
    return state == CodexTaskState::Pending
        ? "按住右键 · 发送下一步"
        : "按住右键 · 引导当前任务";
}

}  // namespace


void CodexTaskFocusView::init(lv_obj_t* parent)
{
    shutdown();
    if (parent == nullptr) {
        return;
    }

    _root = lv_obj_create(parent);
    lv_obj_set_size(_root, kViewportWidth, kViewportHeight);
    lv_obj_set_pos(_root, 0, 0);
    lv_obj_set_style_radius(_root, 0, 0);
    lv_obj_set_style_border_width(_root, 0, 0);
    lv_obj_set_style_pad_all(_root, 0, 0);
    lv_obj_set_style_bg_color(_root, lv_color_hex(kBackground), 0);
    lv_obj_set_style_bg_opa(_root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(_root, LV_OBJ_FLAG_SCROLLABLE);

    _deck_layer = make_layer(_root);
    _deck_project = make_label(
        _deck_layer, "", kContentX, kProjectY, kContentWidth,
        kProjectHeight, &lv_font_montserrat_18, kBeaconViolet,
        LV_TEXT_ALIGN_CENTER, LV_LABEL_LONG_MODE_DOTS);
    _deck_title = make_label(
        _deck_layer, "", kContentX, kTaskTitleY, kContentWidth,
        kTaskTitleHeight, &MontserratSemiBold26, kSignalWhite,
        LV_TEXT_ALIGN_CENTER, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_line_space(_deck_title, 4, 0);
    _deck_status = make_label(
        _deck_layer, "", 0, 0, 1, 1, &font_puhui_14_1,
        kSignalWhite, LV_TEXT_ALIGN_CENTER, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_radius(_deck_status, kStatusRadius, 0);
    lv_obj_set_style_pad_top(_deck_status, 7, 0);
    _deck_empty = make_label(
        _deck_layer, "暂无进行中的任务", kContentX, 203,
        kContentWidth, 44, &font_puhui_14_1, kEtchedGray,
        LV_TEXT_ALIGN_CENTER, LV_LABEL_LONG_MODE_CLIP);
    _connection_hint = make_label(
        _deck_layer, "连接已断开 · 仅显示缓存", kContentX,
        kConnectionHintY, kContentWidth, kConnectionHintHeight,
        &font_puhui_14_1, kEtchedGray, LV_TEXT_ALIGN_CENTER,
        LV_LABEL_LONG_MODE_CLIP);

    _track_layer = make_layer(_deck_layer);
    _track_arc = lv_arc_create(_track_layer);
    lv_obj_set_size(_track_arc, kTrackRadius * 2, kTrackRadius * 2);
    lv_obj_set_pos(
        _track_arc, kTrackCenterX - kTrackRadius,
        kTrackCenterY - kTrackRadius);
    lv_arc_set_bg_angles(
        _track_arc, kTrackArcStartAngleDegrees, kTrackArcEndAngleDegrees);
    lv_arc_set_angles(
        _track_arc, kTrackArcStartAngleDegrees, kTrackArcStartAngleDegrees);
    lv_obj_set_style_arc_width(_track_arc, 2, LV_PART_MAIN);
    lv_obj_set_style_arc_color(
        _track_arc, lv_color_hex(kGraphite), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(_track_arc, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(_track_arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(_track_arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_clear_flag(_track_arc, LV_OBJ_FLAG_CLICKABLE);
    for (size_t index = 0; index < _track_nodes.size(); ++index) {
        _track_nodes[index] = lv_obj_create(_track_layer);
        lv_obj_set_style_pad_all(_track_nodes[index], 0, 0);
        lv_obj_set_style_border_width(_track_nodes[index], 0, 0);
        lv_obj_set_style_bg_opa(_track_nodes[index], LV_OPA_COVER, 0);
        lv_obj_clear_flag(_track_nodes[index], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(_track_nodes[index], LV_OBJ_FLAG_SCROLLABLE);
    }

    _detail_layer = make_layer(_root);
    _detail_project = make_label(
        _detail_layer, "", kContentX, kDetailProjectY, kContentWidth,
        kDetailProjectHeight, &lv_font_montserrat_18, kBeaconViolet,
        LV_TEXT_ALIGN_CENTER, LV_LABEL_LONG_MODE_DOTS);
    _detail_title = make_label(
        _detail_layer, "", kContentX, kDetailTitleY, kContentWidth,
        kDetailTitleHeight, &MontserratSemiBold26, kSignalWhite,
        LV_TEXT_ALIGN_CENTER, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_line_space(_detail_title, 2, 0);
    _detail_status = make_label(
        _detail_layer, "", kContentX, kDetailStatusY, kContentWidth,
        kDetailStatusHeight, &font_puhui_14_1, kPendingVermilion,
        LV_TEXT_ALIGN_CENTER, LV_LABEL_LONG_MODE_CLIP);

    _detail_body = lv_obj_create(_detail_layer);
    lv_obj_set_pos(_detail_body, kDetailBodyX, kDetailBodyY);
    lv_obj_set_size(_detail_body, kDetailBodyWidth, kDetailBodyHeight);
    lv_obj_set_style_radius(_detail_body, 0, 0);
    lv_obj_set_style_border_width(_detail_body, 0, 0);
    lv_obj_set_style_pad_all(_detail_body, 0, 0);
    lv_obj_set_style_pad_right(_detail_body, 6, 0);
    lv_obj_set_style_bg_opa(_detail_body, LV_OPA_TRANSP, 0);
    lv_obj_set_scroll_dir(_detail_body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(_detail_body, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_bg_color(
        _detail_body, lv_color_hex(kGraphite), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(_detail_body, LV_OPA_COVER, LV_PART_SCROLLBAR);
    _detail_reply = make_label(
        _detail_body, "暂无完整回复", 0, 0, kDetailBodyWidth - 12,
        LV_SIZE_CONTENT, &font_puhui_14_1, kSignalWhite,
        LV_TEXT_ALIGN_LEFT, LV_LABEL_LONG_MODE_WRAP);
    set_text_scale(_detail_reply, kCjkBodyScale);
    lv_obj_set_width(
        _detail_reply,
        (kDetailBodyWidth - 12) * 256 / kCjkBodyScale);
    lv_obj_set_style_text_line_space(_detail_reply, 7, 0);

    _detail_footer = make_label(
        _detail_layer, "", kDetailFooterX, kDetailFooterY,
        kDetailFooterWidth, kDetailFooterHeight, &font_puhui_14_1,
        kSignalWhite, LV_TEXT_ALIGN_CENTER, LV_LABEL_LONG_MODE_CLIP);
    _detail_recording_dot = make_geometry(_detail_layer);
    lv_obj_set_size(_detail_recording_dot, 10, 10);
    lv_obj_set_pos(_detail_recording_dot, 124, kDetailFooterY + 6);
    lv_obj_set_style_radius(_detail_recording_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(
        _detail_recording_dot, lv_color_hex(kPendingVermilion), 0);

    set_hidden(_deck_layer, true);
    set_hidden(_detail_layer, true);
    set_hidden(_deck_empty, true);
    set_hidden(_connection_hint, true);
    set_hidden(_detail_recording_dot, true);
    for (size_t index = 0; index < _track_nodes.size(); ++index) {
        set_hidden(_track_nodes[index], true);
    }
    _mode = CodexTaskFocusViewMode::Hidden;
    set_visible(false);
}


void CodexTaskFocusView::shutdown()
{
    if (_root != nullptr) {
        lv_obj_delete(_root);
    }
    _root = nullptr;
    _deck_layer = nullptr;
    _deck_project = nullptr;
    _deck_title = nullptr;
    _deck_status = nullptr;
    _deck_empty = nullptr;
    _connection_hint = nullptr;
    _track_layer = nullptr;
    _track_arc = nullptr;
    _track_nodes = {};
    _detail_layer = nullptr;
    _detail_project = nullptr;
    _detail_title = nullptr;
    _detail_status = nullptr;
    _detail_body = nullptr;
    _detail_reply = nullptr;
    _detail_footer = nullptr;
    _detail_recording_dot = nullptr;
    _mode = CodexTaskFocusViewMode::Hidden;
    _detail_state = CodexTaskState::Completed;
    _rendered_track_count = 0;
    _visible = false;
    _connected = false;
    _recording = false;
    _deck_title_text.clear();
    _detail_title_text.clear();
}


void CodexTaskFocusView::show_deck(
    const CodexTaskFocusDeck& deck,
    bool connected
)
{
    if (_root == nullptr) {
        return;
    }
    _mode = CodexTaskFocusViewMode::Deck;
    _recording = false;
    set_hidden(_deck_layer, false);
    set_hidden(_detail_layer, true);

    const CodexTaskAlert* task = deck.focused();
    const bool empty = task == nullptr;
    set_hidden(_deck_project, empty);
    set_hidden(_deck_title, empty);
    set_hidden(_deck_status, empty);
    set_hidden(_deck_empty, !empty);
    if (!empty) {
        lv_label_set_text(_deck_project, task->project.c_str());
        lv_obj_set_style_text_font(
            _deck_project, project_font(task->project.c_str()), 0);
        const int project_scale = has_non_ascii(task->project.c_str())
            ? kCjkBodyScale
            : 256;
        set_text_scale(_deck_project, project_scale);
        lv_obj_set_size(
            _deck_project, kContentWidth * 256 / project_scale,
            kProjectHeight * 256 / project_scale);
        const bool cjk_title = has_non_ascii(task->title.c_str());
        _deck_title_text = elide_utf8(
            task->title.c_str(),
            cjk_title ? kDeckTitleCjkMaxScalars
                      : kDeckTitleLatinMaxScalars);
        lv_label_set_text(_deck_title, _deck_title_text.c_str());
        lv_obj_set_style_text_font(
            _deck_title, title_font(task->title.c_str()), 0);
        const int title_scale = cjk_title ? kCjkTitleScale : kLatinTitleScale;
        set_text_scale(_deck_title, title_scale);
        lv_obj_set_style_text_line_space(_deck_title, cjk_title ? 4 : 2, 0);
        lv_obj_set_size(
            _deck_title, kContentWidth * 256 / title_scale,
            kTaskTitleHeight * 256 / title_scale);
        render_status(task, true);
    }
    render_track(deck);
    apply_connection(connected);
    set_visible(_visible);
}


void CodexTaskFocusView::show_detail(
    const CodexTaskAlert* task,
    const char* reply,
    bool connected
)
{
    if (_root == nullptr || task == nullptr) {
        return;
    }
    _mode = CodexTaskFocusViewMode::Detail;
    _detail_state = task->state;
    set_hidden(_deck_layer, true);
    set_hidden(_detail_layer, false);
    lv_label_set_text(_detail_project, task->project.c_str());
    lv_obj_set_style_text_font(
        _detail_project, project_font(task->project.c_str()), 0);
    const int project_scale = has_non_ascii(task->project.c_str())
        ? kCjkBodyScale
        : 256;
    set_text_scale(_detail_project, project_scale);
    lv_obj_set_size(
        _detail_project, kContentWidth * 256 / project_scale,
        kDetailProjectHeight * 256 / project_scale);
    _detail_title_text = elide_utf8(
        task->title.c_str(), kDetailTitleMaxScalars);
    lv_label_set_text(_detail_title, _detail_title_text.c_str());
    lv_obj_set_style_text_font(
        _detail_title, title_font(task->title.c_str()), 0);
    const int detail_title_scale = has_non_ascii(task->title.c_str())
        ? kCjkBodyScale
        : 256;
    set_text_scale(_detail_title, detail_title_scale);
    lv_obj_set_size(
        _detail_title, kContentWidth * 256 / detail_title_scale,
        kDetailTitleHeight * 256 / detail_title_scale);
    render_status(task, false);
    if (reply == nullptr || reply[0] == '\0') {
        lv_label_set_text(_detail_reply, "暂无完整回复");
    } else {
        lv_label_set_text(_detail_reply, reply);
    }
    lv_obj_scroll_to_y(_detail_body, 0, LV_ANIM_OFF);
    apply_connection(connected);
    render_footer();
    set_visible(_visible);
}


void CodexTaskFocusView::set_recording(bool recording)
{
    _recording = recording && _connected &&
        _mode == CodexTaskFocusViewMode::Detail;
    render_footer();
}


void CodexTaskFocusView::show_preview(TaskFocusPreviewScenario scenario)
{
    const TaskFocusPreviewData data = task_focus_preview(scenario);
    CodexStatusSnapshot snapshot;
    snapshot.alerts_valid = true;
    for (size_t index = 0; index < data.count; ++index) {
        CodexTaskAlert task;
        task.route = index + 1;
        task.project = data.project;
        task.title = data.title;
        task.state = data.states[index] == TaskTrackState::Pending
            ? CodexTaskState::Pending
            : CodexTaskState::Running;
        snapshot.alerts.push_back(task);
    }
    CodexTaskFocusDeck deck;
    deck.apply(snapshot);
    for (size_t index = 0; index < data.focused_index; ++index) {
        deck.browse(1);
    }
    show_deck(deck, data.connected);
    set_visible(true);
    if (data.detail) {
        CodexTaskAlert task;
        task.route = 1;
        task.project = data.project;
        task.title = data.title;
        task.state = data.states[data.focused_index] == TaskTrackState::Pending
            ? CodexTaskState::Pending
            : CodexTaskState::Running;
        show_detail(&task, data.reply, data.connected);
        set_recording(data.recording);
    }
}


void CodexTaskFocusView::set_visible(bool visible)
{
    _visible = visible;
    set_hidden(_root, !visible);
}


bool CodexTaskFocusView::contains_focus_point(int x, int y) const
{
    return _visible && _mode == CodexTaskFocusViewMode::Deck &&
        x >= kFocusTargetX && x < kFocusTargetX + kFocusTargetWidth &&
        y >= kFocusTargetY && y < kFocusTargetY + kFocusTargetHeight;
}


void CodexTaskFocusView::detail_scroll_by(int delta_y)
{
    if (_detail_body != nullptr && _mode == CodexTaskFocusViewMode::Detail) {
        lv_obj_scroll_by_bounded(_detail_body, 0, -delta_y, LV_ANIM_OFF);
    }
}


bool CodexTaskFocusView::detail_at_top() const
{
    return _detail_body == nullptr || lv_obj_get_scroll_y(_detail_body) <= 0;
}


CodexTaskFocusViewMode CodexTaskFocusView::mode() const
{
    return _mode;
}


bool CodexTaskFocusView::recording() const
{
    return _recording;
}


size_t CodexTaskFocusView::rendered_track_count() const
{
    return _rendered_track_count;
}


void CodexTaskFocusView::render_status(
    const CodexTaskAlert* task,
    bool deck_mode
)
{
    if (task == nullptr) {
        return;
    }
    lv_obj_t* label = deck_mode ? _deck_status : _detail_status;
    lv_label_set_text(label, status_text(task->state));
    const bool pending = task->state == CodexTaskState::Pending;
    const uint32_t state_color = pending ? kPendingVermilion : kRunningCyan;
    lv_obj_set_style_text_color(
        label, lv_color_hex(deck_mode && pending ? kSignalWhite : state_color),
        0);
    if (!deck_mode) {
        return;
    }
    const int width = pending ? kStatusPendingWidth : kStatusRunningWidth;
    lv_obj_set_size(label, width, kStatusHeight);
    lv_obj_set_pos(
        label, kContentCenterX - width / 2,
        kStatusCenterY - kStatusHeight / 2);
    lv_obj_set_style_bg_color(label, lv_color_hex(kPendingVermilion), 0);
    lv_obj_set_style_bg_opa(label, pending ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(label, pending ? 0 : 1, 0);
    lv_obj_set_style_border_color(label, lv_color_hex(kRunningCyan), 0);
}


void CodexTaskFocusView::render_footer()
{
    if (_detail_footer == nullptr) {
        return;
    }
    _recording = _recording && _connected;
    set_hidden(_detail_recording_dot, !_recording);
    if (_recording) {
        lv_label_set_text(_detail_footer, "正在录音 · 松开右键发送");
        lv_obj_set_style_text_color(
            _detail_footer, lv_color_hex(kSignalWhite), 0);
    } else if (!_connected) {
        lv_label_set_text(_detail_footer, "连接已断开 · 语音不可用");
        lv_obj_set_style_text_color(
            _detail_footer, lv_color_hex(kEtchedGray), 0);
    } else {
        lv_label_set_text(_detail_footer, footer_text(_detail_state));
        lv_obj_set_style_text_color(
            _detail_footer, lv_color_hex(kSignalWhite), 0);
    }
}


void CodexTaskFocusView::render_track(const CodexTaskFocusDeck& deck)
{
    std::array<TaskTrackState, kMaxTrackNodes> states{};
    for (size_t index = 0; index < deck.count(); ++index) {
        states[index] = deck.tasks()[index].state == CodexTaskState::Pending
            ? TaskTrackState::Pending
            : TaskTrackState::Running;
    }
    const TaskTrackNodes geometry = make_track_nodes(
        states.data(), deck.count(), deck.focused_index());
    _rendered_track_count = geometry.count;
    for (size_t index = 0; index < _track_nodes.size(); ++index) {
        const bool hidden = index >= geometry.count;
        set_hidden(_track_nodes[index], hidden);
        if (hidden) {
            continue;
        }
        const TaskTrackNode& node = geometry.items[index];
        lv_obj_set_size(_track_nodes[index], node.width, node.height);
        lv_obj_set_pos(
            _track_nodes[index], node.center_x - node.width / 2,
            node.center_y - node.height / 2);
        lv_obj_set_style_bg_color(
            _track_nodes[index],
            lv_color_hex(
                node.shape == TaskTrackShape::FocusedCapsule
                    ? kBeaconViolet
                    : node.color),
            0);
        lv_obj_set_style_radius(
            _track_nodes[index],
            node.shape == TaskTrackShape::RunningCircle ||
                    node.shape == TaskTrackShape::FocusedCapsule
                ? LV_RADIUS_CIRCLE
                : 1,
            0);
        lv_obj_set_style_transform_rotation(
            _track_nodes[index],
            node.shape == TaskTrackShape::PendingDiamond ? 450 : 0,
            0);
    }
}


void CodexTaskFocusView::apply_connection(bool connected)
{
    _connected = connected;
    if (!connected) {
        _recording = false;
    }
    set_hidden(
        _connection_hint,
        connected || _mode != CodexTaskFocusViewMode::Deck);
    const lv_opa_t content_opacity = connected ? LV_OPA_COVER : LV_OPA_60;
    lv_obj_set_style_opa(_deck_project, content_opacity, 0);
    lv_obj_set_style_opa(_deck_title, content_opacity, 0);
    lv_obj_set_style_opa(_deck_status, content_opacity, 0);
    lv_obj_set_style_opa(_track_layer, content_opacity, 0);
    lv_obj_set_style_opa(_detail_project, content_opacity, 0);
    lv_obj_set_style_opa(_detail_title, content_opacity, 0);
    lv_obj_set_style_opa(_detail_status, content_opacity, 0);
    lv_obj_set_style_opa(_detail_body, content_opacity, 0);
}
