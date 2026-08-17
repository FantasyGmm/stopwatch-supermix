#include "apps/app_codex_status/codex_task_focus_deck.h"

#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <string>


namespace {

CodexTaskAlert task(uint64_t route, const char* title, CodexTaskState state)
{
    CodexTaskAlert alert;
    alert.route = route;
    alert.project = "Project";
    alert.title = title;
    alert.state = state;
    return alert;
}


CodexStatusSnapshot snapshot(std::initializer_list<CodexTaskAlert> tasks)
{
    CodexStatusSnapshot value;
    value.alerts_valid = true;
    value.alerts = tasks;
    return value;
}


void assert_pending_first_stable_order_and_completed_filtering()
{
    CodexTaskFocusDeck deck;
    const auto mixed = snapshot({
        task(1, "Running A", CodexTaskState::Running),
        task(2, "Pending A", CodexTaskState::Pending),
        task(0, "Completed", CodexTaskState::Completed),
        task(3, "Running B", CodexTaskState::Running),
        task(4, "Pending B", CodexTaskState::Pending),
    });

    assert(deck.apply(mixed));
    assert(deck.count() == 4);
    assert(deck.tasks()[0].route == 2);
    assert(deck.tasks()[1].route == 4);
    assert(deck.tasks()[2].route == 1);
    assert(deck.tasks()[3].route == 3);
    assert(deck.selected_route() == 2);
    assert(deck.focused_index() == 0);
    assert(deck.focused() == &deck.tasks()[0]);
    assert(deck.selected() == &deck.tasks()[0]);
}


void assert_browse_is_bounded_and_does_not_select()
{
    CodexTaskFocusDeck deck;
    assert(deck.apply(snapshot({
        task(1, "Pending A", CodexTaskState::Pending),
        task(2, "Pending B", CodexTaskState::Pending),
        task(3, "Running", CodexTaskState::Running),
    })));

    assert(deck.browse(-1) == CodexTaskFocusEvent::None);
    assert(deck.focused_index() == 0);
    assert(deck.browse(0) == CodexTaskFocusEvent::None);
    assert(deck.browse(2) == CodexTaskFocusEvent::None);
    assert(deck.browse(1) == CodexTaskFocusEvent::FocusChanged);
    assert(deck.focused_index() == 1);
    assert(deck.selected_route() == 1);
    assert(deck.browse(1) == CodexTaskFocusEvent::StateBoundaryCrossed);
    assert(deck.focused_index() == 2);
    assert(deck.selected_route() == 1);
    assert(deck.browse(1) == CodexTaskFocusEvent::None);
    assert(deck.browse(-1) == CodexTaskFocusEvent::StateBoundaryCrossed);
    assert(deck.browse(-1) == CodexTaskFocusEvent::FocusChanged);
}


void assert_selection_changes_only_explicitly()
{
    CodexTaskFocusDeck deck;
    assert(deck.apply(snapshot({
        task(1, "Pending", CodexTaskState::Pending),
        task(2, "Running", CodexTaskState::Running),
    })));
    assert(deck.select_focused() == CodexTaskFocusEvent::None);
    assert(deck.browse(1) == CodexTaskFocusEvent::StateBoundaryCrossed);
    assert(deck.selected_route() == 1);
    assert(deck.select_focused() == CodexTaskFocusEvent::SelectionChanged);
    assert(deck.selected_route() == 2);
    assert(deck.selected() == deck.focused());
    assert(deck.select_focused() == CodexTaskFocusEvent::None);
}


void assert_refresh_preserves_selection_and_browsed_focus()
{
    CodexTaskFocusDeck deck;
    assert(deck.apply(snapshot({
        task(1, "Pending A", CodexTaskState::Pending),
        task(2, "Pending B", CodexTaskState::Pending),
        task(3, "Running", CodexTaskState::Running),
    })));
    assert(deck.browse(1) == CodexTaskFocusEvent::FocusChanged);
    assert(deck.select_focused() == CodexTaskFocusEvent::SelectionChanged);
    assert(deck.browse(1) == CodexTaskFocusEvent::StateBoundaryCrossed);
    assert(deck.focused()->route == 3);
    assert(deck.selected_route() == 2);

    assert(deck.apply(snapshot({
        task(3, "Running", CodexTaskState::Running),
        task(4, "Pending New", CodexTaskState::Pending),
        task(2, "Pending B", CodexTaskState::Pending),
    })));
    assert(deck.selected_route() == 2);
    assert(deck.focused()->route == 3);
    assert(deck.focused_index() == 2);
}


void assert_missing_selection_falls_back_while_focus_clamps_nearest_index()
{
    CodexTaskFocusDeck deck;
    assert(deck.apply(snapshot({
        task(1, "Pending A", CodexTaskState::Pending),
        task(2, "Pending B", CodexTaskState::Pending),
        task(3, "Running A", CodexTaskState::Running),
        task(4, "Running B", CodexTaskState::Running),
    })));
    assert(deck.browse(1) == CodexTaskFocusEvent::FocusChanged);
    assert(deck.browse(1) == CodexTaskFocusEvent::StateBoundaryCrossed);
    assert(deck.browse(1) == CodexTaskFocusEvent::FocusChanged);
    assert(deck.focused_index() == 3);

    assert(deck.apply(snapshot({
        task(5, "Running New", CodexTaskState::Running),
        task(6, "Pending New", CodexTaskState::Pending),
        task(7, "Running Next", CodexTaskState::Running),
    })));
    assert(deck.selected_route() == 6);
    assert(deck.focused_index() == 2);
    assert(deck.focused()->route == 7);

    assert(deck.apply(snapshot({task(8, "Running only", CodexTaskState::Running)})));
    assert(deck.selected_route() == 8);
    assert(deck.focused_index() == 0);
}


void assert_empty_refresh_clears_state()
{
    CodexTaskFocusDeck deck;
    assert(deck.apply(snapshot({task(1, "Pending", CodexTaskState::Pending)})));
    assert(deck.apply(snapshot({task(0, "Completed", CodexTaskState::Completed)})));
    assert(deck.count() == 0);
    assert(deck.focused() == nullptr);
    assert(deck.selected() == nullptr);
    assert(deck.selected_route() == 0);
    assert(deck.focused_index() == 0);
    assert(deck.browse(1) == CodexTaskFocusEvent::None);
    assert(deck.select_focused() == CodexTaskFocusEvent::None);
}


void assert_invalid_snapshots_fail_without_mutation()
{
    CodexTaskFocusDeck deck;
    assert(deck.apply(snapshot({task(9, "Original", CodexTaskState::Running)})));

    auto invalid_flag = snapshot({task(10, "New", CodexTaskState::Pending)});
    invalid_flag.alerts_valid = false;
    assert(!deck.apply(invalid_flag));
    assert(deck.selected_route() == 9);
    assert(deck.focused()->title == "Original");

    assert(!deck.apply(snapshot({task(0, "Zero", CodexTaskState::Pending)})));
    assert(deck.selected_route() == 9);
    assert(!deck.apply(snapshot({
        task(10, "Duplicate A", CodexTaskState::Pending),
        task(10, "Duplicate B", CodexTaskState::Running),
    })));
    assert(deck.selected_route() == 9);

    CodexStatusSnapshot too_many;
    too_many.alerts_valid = true;
    for (uint64_t route = 1; route <= 9; ++route) {
        too_many.alerts.push_back(task(route, "Task", CodexTaskState::Pending));
    }
    assert(!deck.apply(too_many));
    assert(deck.count() == 1);
    assert(deck.selected_route() == 9);
}

}  // namespace


int main()
{
    assert_pending_first_stable_order_and_completed_filtering();
    assert_browse_is_bounded_and_does_not_select();
    assert_selection_changes_only_explicitly();
    assert_refresh_preserves_selection_and_browsed_focus();
    assert_missing_selection_falls_back_while_focus_clamps_nearest_index();
    assert_empty_refresh_clears_state();
    assert_invalid_snapshots_fail_without_mutation();
}
