#include "apps/app_codex_status/trae_task_carousel.h"

#include <cassert>
#include <cstring>
#include <string>


static TraeTask task(
    const char* id,
    const char* title,
    TraeTaskState state
)
{
    TraeTask result;
    std::strncpy(result.id.data(), id, result.id.size() - 1);
    std::strncpy(result.title.data(), title, result.title.size() - 1);
    result.state = state;
    return result;
}


static TraeStatusSnapshot snapshot_with_pending_and_running()
{
    TraeStatusSnapshot snapshot;
    snapshot.revision = 1;
    snapshot.task_count = 4;
    snapshot.tasks[0] = task("pending-a", "Pending A", TraeTaskState::Pending);
    snapshot.tasks[1] = task("pending-b", "Pending B", TraeTaskState::Pending);
    snapshot.tasks[2] = task("running-a", "Running A", TraeTaskState::Running);
    snapshot.tasks[3] = task("done-a", "Done", TraeTaskState::Completed);
    return snapshot;
}


static TraeStatusSnapshot snapshot_with_same_ids_reordered()
{
    TraeStatusSnapshot snapshot;
    snapshot.task_count = 4;
    snapshot.tasks[0] = task("running-a", "Running A", TraeTaskState::Running);
    snapshot.tasks[1] = task("done-a", "Done", TraeTaskState::Completed);
    snapshot.tasks[2] = task("pending-b", "Pending B", TraeTaskState::Pending);
    snapshot.tasks[3] = task("pending-a", "Pending A", TraeTaskState::Pending);
    return snapshot;
}


int main()
{
    TraeTaskCarousel deck;
    assert(deck.apply(snapshot_with_pending_and_running(), 0));
    assert(deck.task_count() == 3);
    assert(std::string(deck.current()->id.data()) == "pending-a");
    assert(deck.next() != nullptr);
    assert(std::string(deck.next()->id.data()) == "pending-b");
    assert(deck.update(3000));
    assert(std::string(deck.current()->id.data()) == "pending-b");
    assert(std::string(deck.next()->id.data()) == "running-a");
    assert(deck.manual_next(3200));
    assert(deck.manual_mode());
    assert(std::string(deck.current()->id.data()) == "running-a");
    assert(std::string(deck.next()->id.data()) == "pending-a");
    assert(!deck.update(7000));
    assert(deck.update(8200));
    assert(!deck.manual_mode());
    assert(std::string(deck.current()->id.data()) == "pending-a");

    TraeStatusSnapshot reordered = snapshot_with_same_ids_reordered();
    reordered.revision = 2;
    assert(deck.apply(reordered, 8300));
    assert(std::string(deck.current()->id.data()) == "pending-a");

    TraeTaskCarousel retained_priority_card;
    assert(retained_priority_card.apply(snapshot_with_pending_and_running(), 0));
    assert(retained_priority_card.manual_next(10));
    assert(retained_priority_card.update(5010));
    assert(std::string(retained_priority_card.current()->id.data()) == "pending-a");
    assert(retained_priority_card.apply(reordered, 5100));
    assert(retained_priority_card.update(8100));
    assert(std::string(retained_priority_card.current()->id.data()) == "pending-b");

    TraeTaskCarousel newly_pending_deck;
    TraeStatusSnapshot running_only;
    running_only.revision = 1;
    running_only.task_count = 1;
    running_only.tasks[0] = task("running-a", "Running A", TraeTaskState::Running);
    assert(newly_pending_deck.apply(running_only, 0));

    TraeStatusSnapshot pending_added;
    pending_added.revision = 2;
    pending_added.task_count = 2;
    pending_added.tasks[0] = task("pending-a", "Pending A", TraeTaskState::Pending);
    pending_added.tasks[1] = task("running-a", "Running A", TraeTaskState::Running);
    assert(newly_pending_deck.apply(pending_added, 100));
    assert(std::string(newly_pending_deck.current()->id.data()) == "running-a");
    assert(newly_pending_deck.update(3100));
    assert(std::string(newly_pending_deck.current()->id.data()) == "pending-a");

    TraeStatusSnapshot terminal_only;
    terminal_only.revision = 3;
    terminal_only.task_count = 1;
    terminal_only.tasks[0] = task("done", "Done", TraeTaskState::Completed);
    assert(deck.apply(terminal_only, 9000));
    assert(deck.current() == nullptr);
    assert(deck.next() == nullptr);
    assert(deck.task_count() == 0);
    assert(!deck.update(12000));
    assert(!deck.manual_next(12000));

    TraeStatusSnapshot one_running;
    one_running.revision = 4;
    one_running.task_count = 1;
    one_running.tasks[0] = task("running-only", "Running", TraeTaskState::Running);
    assert(deck.apply(one_running, 13000));
    assert(!deck.update(16000));
    assert(!deck.manual_next(16000));
    assert(std::string(deck.current()->id.data()) == "running-only");
    assert(deck.next() == nullptr);

    TraeTaskCarousel constrained_manual_deck;
    assert(constrained_manual_deck.apply(snapshot_with_pending_and_running(), 0));
    assert(constrained_manual_deck.manual_next(10));
    one_running.revision = 2;
    assert(constrained_manual_deck.apply(one_running, 20));
    assert(!constrained_manual_deck.manual_mode());
}
