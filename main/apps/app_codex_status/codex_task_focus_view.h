#pragma once

#include "codex_task_focus_deck.h"
#include "codex_task_focus_preview.h"
#include "codex_task_focus_text.h"

#include <lvgl.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>


enum class CodexTaskFocusViewMode : uint8_t {
    Hidden,
    Deck,
    Detail,
};


class CodexTaskFocusView {
public:
    void init(lv_obj_t* parent);
    void shutdown();
    void show_deck(const CodexTaskFocusDeck& deck, bool connected);
    void show_detail(
        const CodexTaskAlert* task,
        const char* reply,
        int32_t revision,
        bool connected
    );
    void set_recording(bool recording);
    void show_preview(
        codex_task_focus_layout::TaskFocusPreviewScenario scenario
    );
    void set_visible(bool visible);
    void set_vertical_offset(int y);
    void set_deck_scroll_offset(int y);
    int vertical_offset() const;
    bool contains_focus_point(int x, int y) const;
    void detail_scroll_by(int delta_y);
    bool detail_at_top() const;

    CodexTaskFocusViewMode mode() const;
    bool recording() const;
    size_t rendered_track_count() const;

private:
    struct DeckRowWidgets {
        lv_obj_t* root = nullptr;
        lv_obj_t* focus_rule = nullptr;
        lv_obj_t* project = nullptr;
        lv_obj_t* title = nullptr;
        lv_obj_t* status_dot = nullptr;
        lv_obj_t* status = nullptr;
        lv_obj_t* time = nullptr;
    };

    void render_status(const CodexTaskAlert* task, bool deck_mode);
    void render_footer();
    void render_track(const CodexTaskFocusDeck& deck);
    void apply_connection(bool connected);

    lv_obj_t* _root = nullptr;
    lv_obj_t* _deck_layer = nullptr;
    lv_obj_t* _deck_drag_handle = nullptr;
    lv_obj_t* _deck_header = nullptr;
    lv_obj_t* _deck_count = nullptr;
    lv_obj_t* _deck_stream_layer = nullptr;
    std::array<DeckRowWidgets, 3> _deck_rows{};
    lv_obj_t* _deck_separator_top = nullptr;
    lv_obj_t* _deck_separator_bottom = nullptr;
    lv_obj_t* _deck_hint = nullptr;
    lv_obj_t* _deck_empty = nullptr;
    lv_obj_t* _connection_hint = nullptr;
    lv_obj_t* _track_layer = nullptr;
    std::array<lv_obj_t*, 8> _track_nodes{};
    lv_obj_t* _detail_layer = nullptr;
    lv_obj_t* _detail_drag_handle = nullptr;
    lv_obj_t* _detail_project = nullptr;
    lv_obj_t* _detail_title = nullptr;
    lv_obj_t* _detail_status = nullptr;
    lv_obj_t* _detail_divider = nullptr;
    lv_obj_t* _detail_label = nullptr;
    lv_obj_t* _detail_live_dot = nullptr;
    lv_obj_t* _detail_body = nullptr;
    std::array<lv_obj_t*, 16> _detail_paragraphs{};
    lv_obj_t* _detail_footer_label = nullptr;
    lv_obj_t* _detail_footer = nullptr;
    lv_obj_t* _detail_recording_dot = nullptr;
    CodexTaskFocusViewMode _mode = CodexTaskFocusViewMode::Hidden;
    CodexTaskState _detail_state = CodexTaskState::Completed;
    size_t _rendered_track_count = 0;
    bool _visible = true;
    bool _connected = false;
    bool _recording = false;
    std::string _detail_title_text;
    std::array<std::string, 16> _detail_paragraph_text{};
    codex_task_focus_layout::TaskDetailScrollIdentity
        _detail_scroll_identity;
};
