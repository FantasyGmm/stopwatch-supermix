#include "codex_task_focus_view.h"

#include "codex_task_focus_layout.h"
#include "codex_task_focus_text.h"

#include <assets/assets.h>

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>


LV_FONT_DECLARE(font_puhui_14_1)
LV_FONT_DECLARE(CodexDetail16)
LV_FONT_DECLARE(CodexTaskTitle21)
LV_FONT_DECLARE(CodexFocusStatic9)
LV_FONT_DECLARE(CodexFocusStatic10)
LV_FONT_DECLARE(CodexFocusStatic11)
LV_FONT_DECLARE(CodexFocusStatic16)

namespace {

using namespace codex_task_focus_layout;

constexpr int kCjk10Scale = 183;


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


lv_obj_t* make_fading_separator(lv_obj_t* parent, int y)
{
    lv_obj_t* separator = make_layer(parent);
    lv_obj_set_pos(separator, kStreamSeparatorX, y);
    lv_obj_set_size(separator, kStreamSeparatorWidth, 1);

    lv_obj_t* left = make_geometry(separator);
    lv_obj_set_pos(left, 0, 0);
    lv_obj_set_size(left, kStreamSeparatorFadeWidth, 1);
    lv_obj_set_style_bg_color(left, lv_color_hex(kTaskRule), 0);
    lv_obj_set_style_bg_opa(left, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_grad_color(left, lv_color_hex(kTaskRule), 0);
    lv_obj_set_style_bg_grad_opa(left, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_dir(left, LV_GRAD_DIR_HOR, 0);

    lv_obj_t* center = make_geometry(separator);
    lv_obj_set_pos(center, kStreamSeparatorFadeWidth, 0);
    lv_obj_set_size(center, kStreamSeparatorCenterWidth, 1);
    lv_obj_set_style_bg_color(center, lv_color_hex(kTaskRule), 0);

    lv_obj_t* right = make_geometry(separator);
    lv_obj_set_pos(
        right,
        kStreamSeparatorFadeWidth + kStreamSeparatorCenterWidth,
        0);
    lv_obj_set_size(right, kStreamSeparatorFadeWidth, 1);
    lv_obj_set_style_bg_color(right, lv_color_hex(kTaskRule), 0);
    lv_obj_set_style_bg_opa(right, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_color(right, lv_color_hex(kTaskRule), 0);
    lv_obj_set_style_bg_grad_opa(right, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_grad_dir(right, LV_GRAD_DIR_HOR, 0);
    return separator;
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
    return has_non_ascii(text) ? &font_puhui_14_1 : &lv_font_montserrat_12;
}


const lv_font_t* detail_project_font(const char* text)
{
    return has_non_ascii(text) ? &font_puhui_14_1 : &lv_font_montserrat_10;
}


const lv_font_t* detail_title_font(const char* text)
{
    return has_non_ascii(text) ? &CodexDetail16 : &lv_font_montserrat_16;
}


const lv_font_t* detail_body_font()
{
    return &CodexDetail16;
}


void set_text_scale(lv_obj_t* label, int scale)
{
    lv_obj_set_style_transform_scale_x(label, scale, 0);
    lv_obj_set_style_transform_scale_y(label, scale, 0);
    lv_obj_set_style_transform_pivot_x(label, 0, 0);
    lv_obj_set_style_transform_pivot_y(label, 0, 0);
}


void set_scaled_label_box(
    lv_obj_t* label,
    int x,
    int y,
    int width,
    int height,
    int scale
)
{
    set_text_scale(label, scale);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(
        label,
        std::max(1, width * 256 / scale),
        std::max(1, height * 256 / scale));
}


size_t split_detail_reply(
    const char* reply,
    std::array<std::string, kDetailParagraphLimit>& paragraphs
)
{
    for (std::string& paragraph : paragraphs) {
        paragraph.clear();
    }
    const std::string text =
        reply == nullptr || reply[0] == '\0' ? "暂无完整回复" : reply;
    size_t count = 0;
    size_t begin = 0;
    while (count + 1 < paragraphs.size()) {
        const size_t separator = text.find("\n\n", begin);
        if (separator == std::string::npos) {
            break;
        }
        paragraphs[count++] = text.substr(begin, separator - begin);
        begin = separator + 2;
    }
    paragraphs[count++] = text.substr(begin);
    return count;
}


const char* status_text(CodexTaskState state)
{
    return state == CodexTaskState::Pending ? "等待你的指令" : "正在执行";
}


const char* detail_status_text(CodexTaskState state)
{
    return state == CodexTaskState::Pending ? "待回复" : "运行中";
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
    _deck_drag_handle = make_geometry(_deck_layer);
    lv_obj_set_pos(
        _deck_drag_handle, kDeckDragHandleX, kDeckDragHandleY);
    lv_obj_set_size(
        _deck_drag_handle, kDeckDragHandleWidth, kDeckDragHandleHeight);
    lv_obj_set_style_radius(_deck_drag_handle, 2, 0);
    lv_obj_set_style_bg_color(
        _deck_drag_handle, lv_color_hex(kTaskHandle), 0);
    lv_obj_set_style_bg_opa(
        _deck_drag_handle,
        static_cast<lv_opa_t>(kDeckHandleOpacity),
        0);
    _deck_header = make_label(
        _deck_layer, "任务", kDeckHeaderX, 49,
        kDeckHeaderWidth, 24,
        &CodexFocusStatic16, kTaskSignalWhite,
        LV_TEXT_ALIGN_CENTER, LV_LABEL_LONG_MODE_CLIP);
    _deck_count = make_label(
        _deck_layer, "", kDeckCountX, 53,
        kDeckCountWidth, 18,
        &lv_font_montserrat_12, kTaskCount,
        LV_TEXT_ALIGN_RIGHT, LV_LABEL_LONG_MODE_CLIP);
    _deck_stream_layer = make_layer(_deck_layer);
    const TaskStreamSlots stream_geometry = make_stream_slots(3, 1);
    for (size_t index = 0; index < _deck_rows.size(); ++index) {
        DeckRowWidgets& row = _deck_rows[index];
        const TaskStreamSlot& slot = stream_geometry.items[index];
        row.root = lv_obj_create(_deck_stream_layer);
        lv_obj_set_pos(row.root, slot.x, slot.y);
        lv_obj_set_size(row.root, slot.width, slot.height);
        lv_obj_set_style_radius(row.root, 0, 0);
        lv_obj_set_style_border_width(row.root, 0, 0);
        lv_obj_set_style_pad_all(row.root, 0, 0);
        lv_obj_set_style_bg_opa(row.root, LV_OPA_TRANSP, 0);
        lv_obj_clear_flag(row.root, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(row.root, LV_OBJ_FLAG_SCROLLABLE);
        row.focus_rule = make_geometry(row.root);
        row.project = make_label(
            row.root, "", 12, 0, slot.width - 24, 18,
            &font_puhui_14_1, kTaskProject,
            LV_TEXT_ALIGN_LEFT, LV_LABEL_LONG_MODE_DOTS);
        row.title = make_label(
            row.root, "", 12, 20, slot.width - 24, 43,
            &CodexTaskTitle21, kTaskSignalWhite,
            LV_TEXT_ALIGN_LEFT, LV_LABEL_LONG_MODE_WRAP);
        row.status_dot = make_geometry(row.root);
        lv_obj_set_size(row.status_dot, 6, 6);
        lv_obj_set_style_radius(row.status_dot, LV_RADIUS_CIRCLE, 0);
        row.status = make_label(
            row.root, "", 24, 50, 116, 17,
            &CodexFocusStatic11, kTaskMeta,
            LV_TEXT_ALIGN_LEFT, LV_LABEL_LONG_MODE_CLIP);
        row.time = make_label(
            row.root, "", slot.width - 112, 50, 100, 17,
            &font_puhui_14_1, kTaskMeta,
            LV_TEXT_ALIGN_RIGHT, LV_LABEL_LONG_MODE_DOTS);
    }
    _deck_separator_top =
        make_fading_separator(_deck_layer, kStreamSeparatorTopY);
    _deck_separator_bottom =
        make_fading_separator(_deck_layer, kStreamSeparatorBottomY);
    _deck_hint = make_label(
        _deck_layer, "上下滑动切换　↓", 143, 418,
        180, 23, &CodexFocusStatic16,
        kTaskHint, LV_TEXT_ALIGN_CENTER, LV_LABEL_LONG_MODE_CLIP);
    _deck_empty = make_label(
        _deck_layer, "暂无进行中的任务", kContentX, 203,
        kContentWidth, 44, &font_puhui_14_1, kEtchedGray,
        LV_TEXT_ALIGN_CENTER, LV_LABEL_LONG_MODE_CLIP);
    _connection_hint = make_label(
        _deck_layer, "连接已断开 · 仅显示缓存", kContentX,
        kConnectionHintY, kContentWidth, kConnectionHintHeight,
        &font_puhui_14_1, kEtchedGray, LV_TEXT_ALIGN_CENTER,
        LV_LABEL_LONG_MODE_CLIP);
    lv_obj_move_foreground(_deck_drag_handle);

    _track_layer = make_layer(_deck_layer);
    for (size_t index = 0; index < _track_nodes.size(); ++index) {
        _track_nodes[index] = lv_obj_create(_track_layer);
        lv_obj_set_style_pad_all(_track_nodes[index], 0, 0);
        lv_obj_set_style_border_width(_track_nodes[index], 0, 0);
        lv_obj_set_style_bg_opa(_track_nodes[index], LV_OPA_COVER, 0);
        lv_obj_clear_flag(_track_nodes[index], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(_track_nodes[index], LV_OBJ_FLAG_SCROLLABLE);
    }

    _detail_layer = make_layer(_root);
    _detail_drag_handle = make_geometry(_detail_layer);
    lv_obj_set_pos(
        _detail_drag_handle, kDetailDragHandleX, kDetailDragHandleY);
    lv_obj_set_size(
        _detail_drag_handle,
        kDetailDragHandleWidth,
        kDetailDragHandleHeight);
    lv_obj_set_style_radius(_detail_drag_handle, 2, 0);
    lv_obj_set_style_bg_color(
        _detail_drag_handle, lv_color_hex(kEtchedGray), 0);
    lv_obj_set_style_bg_opa(
        _detail_drag_handle,
        static_cast<lv_opa_t>(kDetailHandleOpacity),
        0);
    _detail_project = make_label(
        _detail_layer, "", kDetailHeaderX, kDetailProjectY,
        kDetailHeaderWidth, kDetailProjectHeight, &lv_font_montserrat_10,
        kProjectViolet,
        LV_TEXT_ALIGN_CENTER, LV_LABEL_LONG_MODE_DOTS);
    _detail_title = make_label(
        _detail_layer, "", kDetailHeaderX, kDetailTitleY,
        kDetailHeaderWidth, kDetailTitleHeight, &lv_font_montserrat_16,
        kSignalWhite,
        LV_TEXT_ALIGN_CENTER, LV_LABEL_LONG_MODE_DOTS);
    _detail_status = make_label(
        _detail_layer, "", kDetailHeaderX, kDetailStatusY,
        kDetailHeaderWidth, kDetailStatusHeight, &CodexFocusStatic9,
        kDetailState,
        LV_TEXT_ALIGN_CENTER, LV_LABEL_LONG_MODE_CLIP);
    _detail_divider = make_geometry(_detail_layer);
    lv_obj_set_pos(_detail_divider, kDetailLabelX, kDetailLabelY);
    lv_obj_set_size(_detail_divider, kDetailLabelWidth, 1);
    lv_obj_set_style_bg_color(
        _detail_divider, lv_color_hex(kGraphite), 0);
    _detail_label = make_label(
        _detail_layer, "Agent 最后一条完整回复",
        kDetailLabelX, kDetailLabelTextY,
        kDetailLabelWidth, 14, &CodexFocusStatic10, kDetailLabel,
        LV_TEXT_ALIGN_CENTER, LV_LABEL_LONG_MODE_CLIP);
    _detail_live_dot = make_geometry(_detail_layer);
    lv_obj_set_pos(_detail_live_dot, kDetailLiveDotX, kDetailLiveDotY);
    lv_obj_set_size(_detail_live_dot, kDetailLiveDotSize, kDetailLiveDotSize);
    lv_obj_set_style_radius(_detail_live_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(
        _detail_live_dot, lv_color_hex(kRunningCyan), 0);

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
        _detail_body, lv_color_hex(kScrollbar), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(_detail_body, LV_OPA_COVER, LV_PART_SCROLLBAR);
    for (lv_obj_t*& paragraph : _detail_paragraphs) {
        paragraph = make_label(
            _detail_body, "", 0, 0, kDetailBodyWidth - 12,
            LV_SIZE_CONTENT, detail_body_font(), kSignalWhite,
            LV_TEXT_ALIGN_LEFT, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_text_line_space(
            paragraph, kDetailReplyLineSpace, 0);
        set_hidden(paragraph, true);
    }

    _detail_footer_label = make_label(
        _detail_layer, "当前语音目标", kDetailFooterX, kDetailFooterY,
        kDetailFooterWidth, 13, &CodexFocusStatic9, kProjectViolet,
        LV_TEXT_ALIGN_CENTER, LV_LABEL_LONG_MODE_CLIP);
    _detail_footer = make_label(
        _detail_layer, "", kDetailFooterX, kDetailFooterY + 15,
        kDetailFooterWidth, 15, &CodexFocusStatic11,
        kSignalWhite, LV_TEXT_ALIGN_CENTER, LV_LABEL_LONG_MODE_CLIP);
    _detail_recording_dot = make_geometry(_detail_layer);
    lv_obj_set_size(_detail_recording_dot, 7, 7);
    lv_obj_set_pos(_detail_recording_dot, 126, kDetailFooterY + 19);
    lv_obj_set_style_radius(_detail_recording_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(
        _detail_recording_dot, lv_color_hex(kPendingVermilion), 0);

    set_hidden(_deck_layer, true);
    set_hidden(_detail_layer, true);
    set_hidden(_deck_empty, true);
    set_hidden(_connection_hint, true);
    set_hidden(_detail_live_dot, true);
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
    _deck_drag_handle = nullptr;
    _deck_header = nullptr;
    _deck_count = nullptr;
    _deck_stream_layer = nullptr;
    _deck_rows = {};
    _deck_separator_top = nullptr;
    _deck_separator_bottom = nullptr;
    _deck_hint = nullptr;
    _deck_empty = nullptr;
    _connection_hint = nullptr;
    _track_layer = nullptr;
    _track_nodes = {};
    _detail_layer = nullptr;
    _detail_drag_handle = nullptr;
    _detail_project = nullptr;
    _detail_title = nullptr;
    _detail_status = nullptr;
    _detail_divider = nullptr;
    _detail_label = nullptr;
    _detail_live_dot = nullptr;
    _detail_body = nullptr;
    _detail_paragraphs.fill(nullptr);
    for (std::string& paragraph : _detail_paragraph_text) {
        paragraph.clear();
    }
    _detail_footer_label = nullptr;
    _detail_footer = nullptr;
    _detail_recording_dot = nullptr;
    _mode = CodexTaskFocusViewMode::Hidden;
    _detail_state = CodexTaskState::Completed;
    _rendered_track_count = 0;
    _visible = false;
    _connected = false;
    _recording = false;
    _detail_title_text.clear();
    _detail_scroll_identity.reset();
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
    _detail_scroll_identity.reset();
    set_hidden(_deck_layer, false);
    set_hidden(_detail_layer, true);

    const CodexTaskAlert* task = deck.focused();
    const bool empty = task == nullptr;
    set_hidden(_deck_header, empty);
    set_hidden(_deck_count, empty);
    set_hidden(_deck_stream_layer, empty);
    set_hidden(_deck_separator_top, empty);
    set_hidden(_deck_separator_bottom, empty);
    set_hidden(_deck_hint, empty);
    set_hidden(_deck_empty, !empty);
    if (!empty) {
        const std::string count_text =
            std::to_string(deck.focused_index() + 1) + " / " +
            std::to_string(deck.count());
        lv_label_set_text(_deck_count, count_text.c_str());
        const TaskStreamSlots slots = make_stream_slots(
            deck.count(), deck.focused_index());
        for (size_t index = 0; index < _deck_rows.size(); ++index) {
            DeckRowWidgets& row = _deck_rows[index];
            const TaskStreamSlot& slot = slots.items[index];
            set_hidden(row.root, !slot.visible);
            if (!slot.visible) {
                continue;
            }
            const CodexTaskAlert& row_task = deck.tasks()[slot.task_index];
            const bool focused = slot.role == TaskStreamRole::Focused;
            const bool cjk = has_non_ascii(row_task.title.c_str());
            const lv_font_t* title_font = focused
                ? (cjk ? &CodexTaskTitle21 : &MontserratSemiBold26)
                : (cjk ? &font_puhui_14_1 : &lv_font_montserrat_16);
            lv_obj_set_pos(row.root, slot.x, slot.y);
            lv_obj_set_size(row.root, slot.width, slot.height);
            lv_obj_set_style_opa(row.root, slot.opacity, 0);
            set_hidden(row.focus_rule, !focused);
            lv_obj_set_pos(row.focus_rule, 0, focused ? 2 : 0);
            lv_obj_set_size(
                row.focus_rule, 3, focused ? slot.height - 4 : 0);
            lv_obj_set_style_radius(row.focus_rule, 2, 0);
            lv_obj_set_style_bg_color(
                row.focus_rule, lv_color_hex(kTaskTrackFocused), 0);
            const int content_x = focused ? 18 : 12;
            const int content_right = focused ? 28 : 12;
            const int content_width =
                slot.width - content_x - content_right;
            lv_obj_set_pos(
                row.project, content_x, focused ? kFocusedProjectY : 0);
            lv_obj_set_size(
                row.project, content_width, kStreamProjectHeight);
            lv_label_set_text(row.project, row_task.project.c_str());
            lv_obj_set_style_text_font(
                row.project, project_font(row_task.project.c_str()), 0);
            set_text_scale(row.project, 256);
            const std::string title = elide_utf8(
                row_task.title.c_str(), focused ? 34 : 22);
            lv_label_set_text(row.title, title.c_str());
            lv_label_set_long_mode(
                row.title,
                focused ? LV_LABEL_LONG_MODE_WRAP : LV_LABEL_LONG_MODE_DOTS);
            lv_obj_set_style_text_font(row.title, title_font, 0);
            lv_obj_set_style_text_line_space(row.title, focused ? 4 : 0, 0);
            lv_point_t measured_title{};
            lv_text_get_size(
                &measured_title,
                title.c_str(),
                title_font,
                0,
                focused ? 4 : 0,
                content_width,
                LV_TEXT_FLAG_NONE);
            const TaskStreamTextLayout text_layout =
                make_stream_text_layout(
                    focused,
                    measured_title.y,
                    title_font->line_height);
            lv_obj_set_pos(
                row.title, content_x, text_layout.title_y);
            lv_obj_set_size(
                row.title, content_width, text_layout.title_height);
            set_text_scale(
                row.title,
                focused ? kStreamFocusedCjkScale
                        : kStreamNeighborCjkScale);
            const int metadata_y = text_layout.metadata_y;
            lv_obj_set_pos(
                row.status_dot, content_x, metadata_y + 5);
            lv_obj_set_style_bg_color(
                row.status_dot,
                lv_color_hex(
                    row_task.state == CodexTaskState::Pending
                        ? kTaskPending
                        : kTaskRunning),
                0);
            lv_obj_set_pos(row.status, content_x + 12, metadata_y);
            lv_obj_set_size(row.status, 116, 17);
            lv_label_set_text(row.status, status_text(row_task.state));
            lv_obj_set_style_text_color(
                row.status,
                lv_color_hex(
                    kTaskMeta),
                0);
            lv_obj_set_pos(
                row.time,
                slot.width - content_right - 100,
                metadata_y);
            lv_obj_set_size(row.time, 100, 17);
            lv_label_set_text(row.time, row_task.time.c_str());
        }
        set_deck_scroll_offset(0);
    }
    render_track(deck);
    apply_connection(connected);
    set_visible(_visible);
}


void CodexTaskFocusView::show_detail(
    const CodexTaskAlert* task,
    const char* reply,
    int32_t revision,
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
        _detail_project, detail_project_font(task->project.c_str()), 0);
    const int project_scale = has_non_ascii(task->project.c_str())
        ? kCjk10Scale
        : 256;
    set_scaled_label_box(
        _detail_project, kDetailHeaderX, kDetailProjectY,
        kDetailHeaderWidth, kDetailProjectHeight, project_scale);
    _detail_title_text = elide_utf8(
        task->title.c_str(), kDetailTitleMaxScalars);
    lv_label_set_text(_detail_title, _detail_title_text.c_str());
    lv_obj_set_style_text_font(
        _detail_title, detail_title_font(task->title.c_str()), 0);
    set_scaled_label_box(
        _detail_title, kDetailHeaderX, kDetailTitleY,
        kDetailHeaderWidth, kDetailTitleHeight, 256);
    lv_obj_set_pos(_detail_status, kDetailHeaderX, kDetailStatusY);
    lv_obj_set_size(
        _detail_status, kDetailHeaderWidth, kDetailStatusHeight);
    render_status(task, false);
    const bool live = task->state == CodexTaskState::Running;
    lv_label_set_text(
        _detail_label,
        live ? "Agent 实时进展" : "Agent 最后一条完整回复");
    set_hidden(_detail_live_dot, !live);
    const bool reset_scroll =
        _detail_scroll_identity.update(task->route, revision);
    const std::string display_reply = detail_display_text(reply);
    const size_t paragraph_count = split_detail_reply(
        display_reply.empty() ? nullptr : display_reply.c_str(),
        _detail_paragraph_text);
    int paragraph_y = 0;
    for (size_t index = 0; index < _detail_paragraphs.size(); ++index) {
        lv_obj_t* paragraph = _detail_paragraphs[index];
        if (index >= paragraph_count) {
            set_hidden(paragraph, true);
            continue;
        }
        lv_label_set_text(
            paragraph, _detail_paragraph_text[index].c_str());
        lv_obj_set_pos(paragraph, 0, paragraph_y);
        set_hidden(paragraph, false);
        lv_obj_update_layout(paragraph);
        const int logical_line_height =
            detail_body_font()->line_height + kDetailReplyLineSpace;
        const int line_count = std::max(
            1,
            (static_cast<int>(lv_obj_get_height(paragraph)) +
             kDetailReplyLineSpace) / logical_line_height);
        paragraph_y +=
            line_count * kDetailReplyLineHeight + kDetailParagraphGap;
    }
    if (reset_scroll) {
        lv_obj_scroll_to_y(_detail_body, 0, LV_ANIM_OFF);
    }
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


void CodexTaskFocusView::set_vertical_offset(int y)
{
    if (_root != nullptr) {
        lv_obj_set_y(_root, y);
    }
}


void CodexTaskFocusView::set_deck_scroll_offset(int y)
{
    if (_deck_stream_layer != nullptr &&
        _mode == CodexTaskFocusViewMode::Deck) {
        lv_obj_set_y(_deck_stream_layer, y);
    }
}


int CodexTaskFocusView::vertical_offset() const
{
    return _root == nullptr ? 0 : lv_obj_get_y(_root);
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
        show_detail(&task, data.reply, 0, data.connected);
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
        x >= kFocusedRowX && x < kFocusedRowX + kFocusedRowWidth &&
        y >= kFocusedRowY && y < kFocusedRowY + kFocusedRowHeight;
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
    if (deck_mode) {
        return;
    }
    lv_obj_t* label = _detail_status;
    lv_label_set_text(
        label,
        detail_status_text(task->state));
    const bool pending = task->state == CodexTaskState::Pending;
    const uint32_t state_color = pending ? kDetailState : kRunningText;
    lv_obj_set_style_text_color(
        label, lv_color_hex(state_color),
        0);
}


void CodexTaskFocusView::render_footer()
{
    if (_detail_footer == nullptr) {
        return;
    }
    _recording = _recording && _connected;
    set_hidden(_detail_recording_dot, !_recording);
    if (_recording) {
        lv_label_set_text(_detail_footer_label, "正在录音");
        lv_label_set_text(_detail_footer, "松开右键发送");
        lv_obj_set_style_text_color(
            _detail_footer, lv_color_hex(kSignalWhite), 0);
    } else if (!_connected) {
        lv_label_set_text(_detail_footer_label, "当前语音目标");
        lv_label_set_text(_detail_footer, "连接已断开 · 语音不可用");
        lv_obj_set_style_text_color(
            _detail_footer, lv_color_hex(kEtchedGray), 0);
    } else {
        lv_label_set_text(_detail_footer_label, "当前语音目标");
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
            lv_color_hex(node.color),
            0);
        lv_obj_set_style_radius(
            _track_nodes[index], LV_RADIUS_CIRCLE, 0);
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
    lv_obj_set_style_opa(_deck_header, content_opacity, 0);
    lv_obj_set_style_opa(_deck_count, content_opacity, 0);
    for (size_t index = 0; index < _deck_rows.size(); ++index) {
        const uint8_t role_opacity = index == 1 ? 255 : 153;
        lv_obj_set_style_opa(
            _deck_rows[index].root,
            connected
                ? role_opacity
                : static_cast<uint8_t>(role_opacity * 3 / 5),
            0);
    }
    lv_obj_set_style_opa(_deck_separator_top, content_opacity, 0);
    lv_obj_set_style_opa(_deck_separator_bottom, content_opacity, 0);
    lv_obj_set_style_opa(_deck_hint, content_opacity, 0);
    lv_obj_set_style_opa(_track_layer, content_opacity, 0);
    lv_obj_set_style_opa(_detail_project, content_opacity, 0);
    lv_obj_set_style_opa(_detail_title, content_opacity, 0);
    lv_obj_set_style_opa(_detail_status, content_opacity, 0);
    lv_obj_set_style_opa(_detail_divider, content_opacity, 0);
    lv_obj_set_style_opa(_detail_label, content_opacity, 0);
    lv_obj_set_style_opa(_detail_live_dot, content_opacity, 0);
    lv_obj_set_style_opa(_detail_body, content_opacity, 0);
    lv_obj_set_style_opa(_detail_footer_label, content_opacity, 0);
    lv_obj_set_style_opa(_detail_footer, content_opacity, 0);
}
