#pragma once

#include "codex_task_focus_deck.h"
#include "codex_task_focus_preview.h"

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
        bool connected
    );
    void set_recording(bool recording);
    void show_preview(
        codex_task_focus_layout::TaskFocusPreviewScenario scenario
    );
    void set_visible(bool visible);
    bool contains_focus_point(int x, int y) const;
    void detail_scroll_by(int delta_y);
    bool detail_at_top() const;

    CodexTaskFocusViewMode mode() const;
    bool recording() const;
    size_t rendered_track_count() const;

private:
    void render_status(const CodexTaskAlert* task, bool deck_mode);
    void render_footer();
    void render_track(const CodexTaskFocusDeck& deck);
    void apply_connection(bool connected);

    lv_obj_t* _root = nullptr;
    lv_obj_t* _deck_layer = nullptr;
    lv_obj_t* _deck_project = nullptr;
    lv_obj_t* _deck_title = nullptr;
    lv_obj_t* _deck_status = nullptr;
    lv_obj_t* _deck_empty = nullptr;
    lv_obj_t* _connection_hint = nullptr;
    lv_obj_t* _track_layer = nullptr;
    lv_obj_t* _track_arc = nullptr;
    std::array<lv_obj_t*, 8> _track_nodes{};
    lv_obj_t* _detail_layer = nullptr;
    lv_obj_t* _detail_project = nullptr;
    lv_obj_t* _detail_title = nullptr;
    lv_obj_t* _detail_status = nullptr;
    lv_obj_t* _detail_body = nullptr;
    lv_obj_t* _detail_reply = nullptr;
    lv_obj_t* _detail_footer = nullptr;
    lv_obj_t* _detail_recording_dot = nullptr;
    CodexTaskFocusViewMode _mode = CodexTaskFocusViewMode::Hidden;
    CodexTaskState _detail_state = CodexTaskState::Completed;
    size_t _rendered_track_count = 0;
    bool _visible = true;
    bool _connected = false;
    bool _recording = false;
    std::string _deck_title_text;
    std::string _detail_title_text;
};
