#include "trae_status_page.h"

#include "assets/trae_robot_assets.h"
#include "trae_status_ui_layout.h"

#include <assets/assets.h>

#include <algorithm>
#include <cstdio>
#include <cstring>


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


lv_obj_t* make_line(lv_obj_t* parent, int x, int y, int width)
{
    lv_obj_t* line = lv_obj_create(parent);
    lv_obj_set_size(line, width, 1);
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


lv_obj_t* make_status_dot(
    lv_obj_t* parent,
    int x,
    uint32_t color
)
{
    lv_obj_t* dot = lv_obj_create(parent);
    lv_obj_set_size(dot, kHealthDotSize, kHealthDotSize);
    lv_obj_set_pos(dot, x, kHealthDotY);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(dot, 0, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    return dot;
}


lv_obj_t* make_card(
    lv_obj_t* parent,
    int y_offset,
    lv_opa_t opacity,
    int scale
)
{
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_size(card, kCardWidth, kCardHeight);
    lv_obj_set_pos(card, kDeckX, kDeckY + y_offset);
    lv_obj_set_style_radius(card, kCardRadius, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(kCardBackground), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(kAccent), 0);
    lv_obj_set_style_border_opa(card, opacity, 0);
    lv_obj_set_style_opa(card, opacity, 0);
    lv_obj_set_style_transform_scale(card, scale, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
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


bool same_task_id(const TraeTask& left, const TraeTask& right)
{
    if (left.id[0] != '\0' && right.id[0] != '\0') {
        return std::strncmp(
                   left.id.data(),
                   right.id.data(),
                   left.id.size()
               ) == 0;
    }
    return std::strncmp(
               left.project.data(),
               right.project.data(),
               left.project.size()
           ) == 0 &&
           std::strncmp(
               left.title.data(),
               right.title.data(),
               left.title.size()
           ) == 0;
}


size_t active_ordinal(
    const TraeStatusSnapshot& snapshot,
    const TraeTask& task
)
{
    size_t ordinal = 0;
    const size_t task_count = std::min(
        snapshot.task_count,
        snapshot.tasks.size()
    );
    for (size_t index = 0; index < task_count; ++index) {
        const TraeTask& candidate = snapshot.tasks[index];
        if (!is_active_task(&candidate)) {
            continue;
        }
        ++ordinal;
        if (same_task_id(candidate, task)) {
            return ordinal;
        }
    }
    return 1;
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
        kTelemetryWidth,
        &MontserratSemiBold26,
        kPrimaryText
    );

    _bt_dot = make_status_dot(_root, kBluetoothDotX, kDimText);
    _bt_status = make_label(
        _root,
        "BT OFF",
        kBluetoothTextX,
        kHealthLabelY,
        kBluetoothWidth,
        &lv_font_montserrat_10,
        kDimText
    );
    lv_obj_set_style_text_letter_space(_bt_status, 1, 0);
    _process_dot = make_status_dot(_root, kProcessDotX, kDimText);
    _process_status = make_label(
        _root,
        "TRAE --",
        kProcessTextX,
        kHealthLabelY,
        kProcessWidth,
        &lv_font_montserrat_10,
        kDimText
    );
    lv_obj_set_style_text_letter_space(_process_status, 1, 0);

    _robot = lv_image_create(_root);
    lv_image_set_src(_robot, &trae_robot_idle);
    lv_image_set_antialias(_robot, false);
    lv_image_set_pivot(_robot, 0, 0);
    lv_image_set_scale(_robot, kRobotScale);
    lv_obj_set_pos(_robot, kRobotX, kRobotY);
    lv_obj_clear_flag(_robot, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* model_caption = make_label(
        _root,
        "MODEL",
        kTelemetryX,
        kModelCaptionY,
        kTelemetryWidth,
        &lv_font_montserrat_14,
        kDimText
    );
    lv_obj_set_style_text_letter_space(model_caption, 3, 0);
    _model = make_label(
        _root,
        "--",
        kTelemetryX,
        kModelValueY,
        kTelemetryWidth,
        &MontserratSemiBold26,
        kPrimaryText
    );

    lv_obj_t* effort_caption = make_label(
        _root,
        "EFFORT",
        kTelemetryX,
        kEffortCaptionY,
        kTelemetryWidth,
        &lv_font_montserrat_14,
        kDimText
    );
    lv_obj_set_style_text_letter_space(effort_caption, 3, 0);
    _effort = make_label(
        _root,
        "--",
        kTelemetryX,
        kEffortValueY,
        kTelemetryWidth,
        &MontserratSemiBold26,
        kPrimaryText
    );

    make_line(_root, kDividerX, kDividerY, kDividerWidth);
    make_label(
        _root,
        "待确认",
        kPendingSummaryX,
        kSummaryY,
        58,
        &font_puhui_14_1,
        kPending
    );
    _pending_count = make_label(
        _root,
        "0",
        kPendingCountX,
        kSummaryY,
        32,
        &lv_font_montserrat_14,
        kPending
    );
    make_label(
        _root,
        "运行中",
        kRunningSummaryX,
        kSummaryY,
        58,
        &font_puhui_14_1,
        kRunning
    );
    _running_count = make_label(
        _root,
        "0",
        kRunningCountX,
        kSummaryY,
        32,
        &lv_font_montserrat_14,
        kRunning
    );

    _cards[0] = make_card(
        _root,
        kDeepCardOffset,
        LV_OPA_30,
        kDeepCardScale
    );
    _cards[1] = make_card(
        _root,
        kBackCardOffset,
        LV_OPA_60,
        kBackCardScale
    );
    _cards[2] = make_card(_root, 0, LV_OPA_COVER, 256);
    set_hidden(_cards[0], true);
    set_hidden(_cards[1], true);

    _card_state = make_label(
        _cards[2],
        "",
        kCardTextX,
        kCardStateY,
        92,
        &lv_font_montserrat_10,
        kPending
    );
    lv_obj_set_style_text_letter_space(_card_state, 1, 0);
    _card_index = make_label(
        _cards[2],
        "",
        kCardIndexX,
        kCardStateY,
        kCardIndexWidth,
        &lv_font_montserrat_10,
        kPending
    );
    lv_obj_set_style_text_letter_space(_card_index, 1, 0);
    lv_obj_set_style_text_align(_card_index, LV_TEXT_ALIGN_RIGHT, 0);
    _card_project = make_label(
        _cards[2],
        "--",
        kCardTextX,
        kCardProjectY,
        kCardTextWidth,
        &font_puhui_14_1,
        kProjectText
    );
    _card_title = make_label(
        _cards[2],
        "--",
        kCardTextX,
        kCardTitleY,
        kCardTextWidth,
        &font_puhui_14_1,
        kCardTitleText
    );
    _card_detail = make_label(
        _cards[2],
        "",
        kCardTextX,
        kCardDetailY,
        kCardTextWidth,
        &font_puhui_14_1,
        kSecondaryText
    );

    _deck_hint = make_label(
        _root,
        "点击最上层卡片切换 · Pending 始终优先",
        0,
        kDeckHintY,
        kScreenSize,
        &font_puhui_14_1,
        kHintText
    );
    lv_obj_set_style_text_align(_deck_hint, LV_TEXT_ALIGN_CENTER, 0);

    _robot_timer = lv_timer_create(
        TraeStatusPage::robot_timer_cb,
        kRobotIdleDurationMs,
        this
    );
    lv_timer_pause(_robot_timer);
}


void TraeStatusPage::render(
    const TraeStatusSnapshot& snapshot,
    const TraeTask* task,
    bool stale,
    bool bluetooth_connected,
    size_t visible_task_count
)
{
    if (_root == nullptr) {
        return;
    }

    const HealthState health = resolve_health_state(
        bluetooth_connected,
        stale,
        snapshot.process_state
    );
    lv_label_set_text(_bt_status, bluetooth_health_text(health));
    lv_obj_set_style_text_color(
        _bt_status,
        lv_color_hex(health.bluetooth_live ? kRunning : kDimText),
        0
    );
    lv_obj_set_style_bg_color(
        _bt_dot,
        lv_color_hex(health.bluetooth_live ? kRunning : kDimText),
        0
    );

    const char* process_text = process_health_text(health.process);
    uint32_t process_color = kDimText;
    if (health.process == ProcessHealth::Live) {
        process_color = kProcessLive;
    }
    lv_label_set_text(_process_status, process_text);
    lv_obj_set_style_text_color(
        _process_status,
        lv_color_hex(process_color),
        0
    );
    lv_obj_set_style_bg_color(_process_dot, lv_color_hex(process_color), 0);

    const bool active_task = is_active_task(task);
    const char* model = snapshot.model.data();
    const char* effort = snapshot.effort.data();
    if (active_task && task->model[0] != '\0') {
        model = task->model.data();
    }
    if (active_task && task->effort[0] != '\0') {
        effort = task->effort.data();
    }
    lv_label_set_text(_model, display_or_dash(model));
    lv_label_set_text(_effort, display_or_dash(effort));

    char count_text[8];
    std::snprintf(
        count_text,
        sizeof(count_text),
        "%u",
        static_cast<unsigned>(snapshot.pending_count)
    );
    lv_label_set_text(_pending_count, count_text);
    std::snprintf(
        count_text,
        sizeof(count_text),
        "%u",
        static_cast<unsigned>(snapshot.running_count)
    );
    lv_label_set_text(_running_count, count_text);

    const char* empty_text = empty_state_text(resolve_empty_state(
        bluetooth_connected,
        stale,
        snapshot.process_state,
        snapshot.pending_count,
        snapshot.running_count,
        active_task
    ));

    if (empty_text != nullptr) {
        set_hidden(_cards[0], true);
        set_hidden(_cards[1], true);
        set_hidden(_card_state, true);
        set_hidden(_card_index, true);
        set_hidden(_card_project, true);
        set_hidden(_card_detail, true);
        set_hidden(_card_title, false);
        lv_label_set_text(_card_title, empty_text);
        lv_obj_set_pos(_card_title, kCardTextX, 40);
        lv_obj_set_style_text_align(_card_title, LV_TEXT_ALIGN_CENTER, 0);
        _last_task_id = {};
        return;
    }

    set_hidden(_card_state, false);
    set_hidden(_card_index, false);
    set_hidden(_card_project, false);
    set_hidden(_card_title, false);
    set_hidden(_card_detail, false);
    lv_obj_set_pos(_card_title, kCardTextX, kCardTitleY);
    lv_obj_set_style_text_align(_card_title, LV_TEXT_ALIGN_LEFT, 0);

    const DeckLayers layers = resolve_deck_layers(visible_task_count);
    set_hidden(_cards[1], !layers.back);
    set_hidden(_cards[0], !layers.deep);

    const bool pending = task->state == TraeTaskState::Pending;
    lv_label_set_text(_card_state, pending ? "PENDING" : "RUNNING");
    lv_obj_set_style_text_color(
        _card_state,
        lv_color_hex(pending ? kPending : kRunning),
        0
    );
    lv_obj_set_style_text_color(
        _card_index,
        lv_color_hex(pending ? kPending : kRunning),
        0
    );
    lv_obj_set_style_border_color(
        _cards[2],
        lv_color_hex(pending ? kPendingBorder : kRunningBorder),
        0
    );

    const size_t visible_count = std::max<size_t>(1, visible_task_count);
    const size_t ordinal = active_ordinal(snapshot, *task);
    const size_t aggregate_count =
        static_cast<size_t>(snapshot.pending_count) +
        static_cast<size_t>(snapshot.running_count);
    const size_t overflow =
        aggregate_count > visible_count ? aggregate_count - visible_count : 0;
    char index_text[24];
    if (overflow != 0) {
        std::snprintf(
            index_text,
            sizeof(index_text),
            "%zu/%zu +%zu",
            ordinal,
            visible_count,
            overflow
        );
    } else {
        std::snprintf(
            index_text,
            sizeof(index_text),
            "%zu/%zu",
            ordinal,
            visible_count
        );
    }
    lv_label_set_text(_card_index, index_text);
    lv_label_set_text(
        _card_project,
        display_or_dash(task->project.data())
    );
    const bool ascii_project = std::all_of(
        task->project.begin(),
        task->project.end(),
        [](char character) {
            return character == '\0' ||
                   static_cast<unsigned char>(character) < 0x80;
        }
    );
    lv_obj_set_style_text_font(
        _card_project,
        ascii_project ? &lv_font_montserrat_18 : &font_puhui_14_1,
        0
    );
    lv_label_set_text(_card_title, display_or_dash(task->title.data()));
    lv_label_set_text(_card_detail, display_or_dash(task->detail.data()));

    const bool task_changed = should_trigger_task_action(
        _active,
        _last_task_id.data(),
        task->id.data()
    );
    _last_task_id = task->id;
    if (task_changed && _robot_timer != nullptr) {
        set_robot_action(true);
        lv_timer_set_period(_robot_timer, kRobotActionDurationMs);
        lv_timer_reset(_robot_timer);
    }
}


void TraeStatusPage::set_active(bool active)
{
    if (_active == active) {
        return;
    }
    _active = active;
    if (_robot_timer == nullptr) {
        return;
    }

    const RobotActivation activation = resolve_robot_activation(active);
    set_robot_action(activation.action);
    lv_timer_set_period(_robot_timer, activation.period_ms);
    if (activation.paused) {
        lv_timer_pause(_robot_timer);
        lv_timer_reset(_robot_timer);
    } else {
        lv_timer_reset(_robot_timer);
        lv_timer_resume(_robot_timer);
    }
}


bool TraeStatusPage::contains_deck_point(int x, int y) const
{
    return _root != nullptr &&
           x >= kDeckX && x < kDeckX + kDeckWidth &&
           y >= kDeckY && y < kDeckY + kDeckHeight;
}


void TraeStatusPage::robot_timer_cb(lv_timer_t* timer)
{
    auto* self = static_cast<TraeStatusPage*>(
        lv_timer_get_user_data(timer)
    );
    if (self == nullptr || !self->_active || self->_robot == nullptr) {
        return;
    }

    const RobotTimerStep step = resolve_robot_timer_step(
        self->_robot_action
    );
    self->set_robot_action(step.action);
    lv_timer_set_period(timer, step.period_ms);
}


void TraeStatusPage::set_robot_action(bool action)
{
    _robot_action = action;
    if (_robot != nullptr) {
        lv_image_set_src(
            _robot,
            action ? &trae_robot_action : &trae_robot_idle
        );
    }
}


void TraeStatusPage::shutdown()
{
    if (_robot_timer != nullptr) {
        lv_timer_delete(_robot_timer);
        _robot_timer = nullptr;
    }
    if (_root != nullptr) {
        lv_obj_delete(_root);
        _root = nullptr;
    }

    _brand_icon = nullptr;
    _bt_dot = nullptr;
    _bt_status = nullptr;
    _process_dot = nullptr;
    _process_status = nullptr;
    _robot = nullptr;
    _model = nullptr;
    _effort = nullptr;
    _pending_count = nullptr;
    _running_count = nullptr;
    _cards = {};
    _card_state = nullptr;
    _card_index = nullptr;
    _card_project = nullptr;
    _card_title = nullptr;
    _card_detail = nullptr;
    _deck_hint = nullptr;
    _last_task_id = {};
    _active = false;
    _robot_action = false;
}
