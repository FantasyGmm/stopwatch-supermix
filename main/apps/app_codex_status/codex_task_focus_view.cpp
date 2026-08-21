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
LV_FONT_DECLARE(lv_font_source_han_sans_sc_16_cjk)
LV_FONT_DECLARE(CodexDetail16)
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


const lv_font_t* title_font(const char* text)
{
    if (!has_non_ascii(text)) {
        return &MontserratSemiBold26;
    }
    static lv_font_t cjk_title_font = [] {
        lv_font_t font = lv_font_source_han_sans_sc_16_cjk;
        font.fallback = &font_puhui_14_1;
        return font;
    }();
    return &cjk_title_font;
}


const lv_font_t* detail_project_font(const char* text)
{
    return has_non_ascii(text) ? &font_puhui_14_1 : &lv_font_montserrat_10;
}


const lv_font_t* detail_title_font(const char* text)
{
    return has_non_ascii(text) ? title_font(text) : &lv_font_montserrat_16;
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


size_t wrapped_line_count(
    const char* text,
    const lv_font_t* font,
    int line_space,
    int logical_width
)
{
    lv_point_t size{};
    lv_text_get_size(
        &size, text == nullptr ? "" : text, font, 0, line_space,
        logical_width, LV_TEXT_FLAG_NONE);
    return std::max<size_t>(
        1,
        static_cast<size_t>(size.y + line_space) /
            static_cast<size_t>(font->line_height + line_space));
}


size_t fit_deck_title(
    const char* source,
    bool cjk,
    const lv_font_t* font,
    int line_space,
    int scale,
    std::string& fitted
)
{
    size_t maximum_scalars = cjk
        ? kDeckTitleCjkMaxScalars
        : kDeckTitleLatinMaxScalars;
    const int logical_width = kTaskTitleWidth * 256 / scale;
    while (maximum_scalars > 1) {
        if (cjk) {
            fitted = elide_utf8(source, maximum_scalars);
        } else {
            const std::string title = source == nullptr ? "" : source;
            fitted = title.size() <= maximum_scalars
                ? title
                : title.substr(0, maximum_scalars > 3
                    ? maximum_scalars - 3
                    : 0) + "...";
        }
        const size_t lines = wrapped_line_count(
            fitted.c_str(), font, line_space, logical_width);
        if (lines <= kTaskTitleMaxLines) {
            return lines;
        }
        --maximum_scalars;
    }
    fitted = cjk ? elide_utf8(source, 1) : ".";
    return 1;
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
    _deck_project_pill = make_geometry(_deck_layer);
    lv_obj_set_size(_deck_project_pill, 83, kProjectHeight);
    lv_obj_set_pos(
        _deck_project_pill, kContentCenterX - 83 / 2, kProjectY);
    lv_obj_set_style_radius(_deck_project_pill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(
        _deck_project_pill, lv_color_hex(kProjectPill), 0);
    _deck_project = make_label(
        _deck_project_pill, "", 0, 0, 83,
        kProjectHeight, &lv_font_montserrat_12, kProjectViolet,
        LV_TEXT_ALIGN_CENTER, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_pad_top(_deck_project, 3, 0);
    _deck_title = make_label(
        _deck_layer, "", kTaskTitleX, kTaskTitleY, kTaskTitleWidth,
        kTaskTitleHeight, &MontserratSemiBold26, kSignalWhite,
        LV_TEXT_ALIGN_CENTER, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_line_space(_deck_title, 3, 0);
    _deck_status = make_label(
        _deck_layer, "", 0, 0, 1, 1, &CodexFocusStatic11,
        kSignalWhite, LV_TEXT_ALIGN_CENTER, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_radius(_deck_status, kStatusRadius, 0);
    lv_obj_set_style_pad_top(_deck_status, 8, 0);
    _deck_target = make_label(
        _deck_layer, "当前语音目标", kContentX, kDeckTargetY,
        kContentWidth, kDeckTargetHeight, &CodexFocusStatic9,
        kProjectViolet, LV_TEXT_ALIGN_CENTER, LV_LABEL_LONG_MODE_CLIP);
    _deck_open = make_label(
        _deck_layer, "点击查看原文", kContentX, kDeckOpenY,
        kContentWidth, kDeckOpenHeight, &CodexFocusStatic16,
        kSecondaryText, LV_TEXT_ALIGN_CENTER, LV_LABEL_LONG_MODE_CLIP);
    _deck_next = make_label(
        _deck_layer, "上下滑动切换　↓", 143, kDeckNextY,
        180, kDeckNextHeight, &CodexFocusStatic16,
        kDeckHint, LV_TEXT_ALIGN_CENTER, LV_LABEL_LONG_MODE_CLIP);
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
    _deck_project_pill = nullptr;
    _deck_project = nullptr;
    _deck_title = nullptr;
    _deck_status = nullptr;
    _deck_target = nullptr;
    _deck_open = nullptr;
    _deck_next = nullptr;
    _deck_empty = nullptr;
    _connection_hint = nullptr;
    _track_layer = nullptr;
    _track_nodes = {};
    _detail_layer = nullptr;
    _detail_project = nullptr;
    _detail_title = nullptr;
    _detail_status = nullptr;
    _detail_divider = nullptr;
    _detail_label = nullptr;
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
    _deck_status_y = 0;
    _deck_title_text.clear();
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
    set_hidden(_deck_project_pill, empty);
    set_hidden(_deck_title, empty);
    set_hidden(_deck_status, empty);
    set_hidden(_deck_target, empty);
    set_hidden(_deck_open, empty);
    set_hidden(_deck_next, empty);
    set_hidden(_deck_empty, !empty);
    if (!empty) {
        lv_label_set_text(_deck_project, task->project.c_str());
        lv_obj_set_style_text_font(
            _deck_project, project_font(task->project.c_str()), 0);
        set_text_scale(_deck_project, 256);
        lv_obj_set_width(_deck_project, LV_SIZE_CONTENT);
        lv_obj_set_height(_deck_project, kProjectHeight);
        lv_obj_update_layout(_deck_project);
        const int project_width = std::clamp(
            static_cast<int>(lv_obj_get_width(_deck_project)) +
                kProjectPillHorizontalPadding,
            52, 220);
        lv_obj_set_size(_deck_project_pill, project_width, kProjectHeight);
        lv_obj_set_pos(_deck_project, 0, 0);
        lv_obj_set_size(_deck_project, project_width, kProjectHeight);
        const bool cjk_title = has_non_ascii(task->title.c_str());
        const lv_font_t* deck_font = title_font(task->title.c_str());
        lv_obj_set_style_text_font(_deck_title, deck_font, 0);
        const int title_scale = cjk_title ? kCjkTitleScale : kLatinTitleScale;
        const int title_line_space = cjk_title ? 0 : 2;
        lv_obj_set_style_text_line_space(
            _deck_title, title_line_space, 0);
        const size_t lines = fit_deck_title(
            task->title.c_str(), cjk_title, deck_font, title_line_space,
            title_scale, _deck_title_text);
        lv_label_set_text(_deck_title, _deck_title_text.c_str());
        const int extra_height = static_cast<int>(lines - 1) *
            kTaskTitleLineHeight;
        const int upper_shift = extra_height / 2;
        const int lower_shift = extra_height - upper_shift;
        lv_obj_set_pos(
            _deck_project_pill,
            kContentCenterX - project_width / 2,
            kProjectY - upper_shift);
        set_scaled_label_box(
            _deck_title, kTaskTitleX, kTaskTitleY - upper_shift,
            kTaskTitleWidth,
            static_cast<int>(lines) * kTaskTitleLineHeight,
            title_scale);
        _deck_status_y = kStatusCenterY - kStatusHeight / 2 - 1 +
            lower_shift;
        lv_obj_set_pos(
            _deck_status,
            kContentCenterX - kStatusPendingWidth / 2,
            _deck_status_y);
        lv_obj_set_size(
            _deck_status, kStatusPendingWidth, kStatusHeight);
        lv_obj_set_pos(
            _deck_target, kContentX, kDeckTargetY + lower_shift);
        lv_obj_set_size(
            _deck_target, kContentWidth, kDeckTargetHeight);
        lv_obj_set_pos(
            _deck_open, kContentX, kDeckOpenY + lower_shift);
        lv_obj_set_size(
            _deck_open, kContentWidth, kDeckOpenHeight);
        lv_label_set_text(
            _deck_target,
            deck.selected_route() == task->route
                ? "当前语音目标"
                : "点击后设为语音目标");
        render_status(task, true);
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
    lv_label_set_text(
        label,
        deck_mode ? status_text(task->state) : detail_status_text(task->state));
    const bool pending = task->state == CodexTaskState::Pending;
    const uint32_t state_color = pending ? kDetailState : kRunningText;
    lv_obj_set_style_text_color(
        label, lv_color_hex(deck_mode && pending ? 0xFFFFFF : state_color),
        0);
    if (!deck_mode) {
        return;
    }
    const int width = pending ? kStatusPendingWidth : kStatusRunningWidth;
    const int height = pending ? kStatusHeight : kStatusRunningHeight;
    lv_obj_set_pos(
        label,
        kContentCenterX - width / 2,
        _deck_status_y - (height - kStatusHeight) / 2);
    lv_obj_set_size(label, width, height);
    lv_obj_set_style_bg_color(label, lv_color_hex(kPendingPill), 0);
    lv_obj_set_style_bg_opa(label, pending ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(label, pending ? 0 : 1, 0);
    lv_obj_set_style_border_color(label, lv_color_hex(kRunningBorder), 0);
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
    lv_obj_set_style_opa(_deck_project_pill, content_opacity, 0);
    lv_obj_set_style_opa(_deck_project, content_opacity, 0);
    lv_obj_set_style_opa(_deck_title, content_opacity, 0);
    lv_obj_set_style_opa(_deck_status, content_opacity, 0);
    lv_obj_set_style_opa(_deck_target, content_opacity, 0);
    lv_obj_set_style_opa(_deck_open, content_opacity, 0);
    lv_obj_set_style_opa(_deck_next, content_opacity, 0);
    lv_obj_set_style_opa(_track_layer, content_opacity, 0);
    lv_obj_set_style_opa(_detail_project, content_opacity, 0);
    lv_obj_set_style_opa(_detail_title, content_opacity, 0);
    lv_obj_set_style_opa(_detail_status, content_opacity, 0);
    lv_obj_set_style_opa(_detail_divider, content_opacity, 0);
    lv_obj_set_style_opa(_detail_label, content_opacity, 0);
    lv_obj_set_style_opa(_detail_body, content_opacity, 0);
    lv_obj_set_style_opa(_detail_footer_label, content_opacity, 0);
    lv_obj_set_style_opa(_detail_footer, content_opacity, 0);
}
