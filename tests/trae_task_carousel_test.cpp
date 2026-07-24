#include "apps/app_codex_status/trae_task_carousel.h"

#include <cassert>
#include <cstring>
#include <string>


static TraeTask task(const char* title, TraeTaskState state)
{
    TraeTask result;
    std::strncpy(result.title.data(), title, result.title.size() - 1);
    result.state = state;
    return result;
}


int main()
{
    TraeStatusSnapshot snapshot;
    snapshot.revision = 1;
    snapshot.task_count = 4;
    snapshot.tasks[0] = task("Completed", TraeTaskState::Completed);
    snapshot.tasks[1] = task("Pending A", TraeTaskState::Pending);
    snapshot.tasks[2] = task("Running", TraeTaskState::Running);
    snapshot.tasks[3] = task("Pending B", TraeTaskState::Pending);

    TraeTaskCarousel carousel;
    assert(carousel.apply(snapshot, 0));
    assert(carousel.display_state() == TraeTaskState::Pending);
    assert(std::string(carousel.current()->title.data()) == "Pending A");
    assert(carousel.update(3000));
    assert(std::string(carousel.current()->title.data()) == "Pending B");
    assert(carousel.update(6000));
    assert(std::string(carousel.current()->title.data()) == "Pending A");

    snapshot.revision = 2;
    snapshot.task_count = 3;
    snapshot.tasks[0] = task("Completed A", TraeTaskState::Completed);
    snapshot.tasks[1] = task("Running A", TraeTaskState::Running);
    snapshot.tasks[2] = task("Running B", TraeTaskState::Running);
    assert(carousel.apply(snapshot, 7000));
    assert(carousel.display_state() == TraeTaskState::Running);
    assert(std::string(carousel.current()->title.data()) == "Running A");
    assert(carousel.update(10000));
    assert(std::string(carousel.current()->title.data()) == "Running B");

    snapshot.revision = 3;
    snapshot.task_count = 2;
    snapshot.tasks[0] = task("Completed A", TraeTaskState::Completed);
    snapshot.tasks[1] = task("Completed B", TraeTaskState::Completed);
    assert(carousel.apply(snapshot, 11000));
    assert(carousel.display_state() == TraeTaskState::Completed);
    assert(carousel.update(14000));
    assert(std::string(carousel.current()->title.data()) == "Completed B");

    carousel.reset();
    assert(carousel.current() == nullptr);
}
