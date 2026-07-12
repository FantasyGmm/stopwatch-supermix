#include "codex_alert_carousel.h"

#include <cassert>
#include <string>


int main()
{
    CodexAlertCarousel carousel;

    CodexStatusSnapshot mixed;
    mixed.alerts_valid = true;
    mixed.alert_revision = 1;
    mixed.pending_count = 2;
    mixed.running_count = 1;
    mixed.completed_count = 1;
    mixed.alerts = {
        {"Project A", "Pending A", "10:00", CodexTaskState::Pending},
        {"Project B", "Running B", "09:59", CodexTaskState::Running},
        {"Project C", "Pending C", "09:58", CodexTaskState::Pending},
        {"Project D", "Completed D", "09:57", CodexTaskState::Completed},
    };

    assert(carousel.apply(mixed, 0));
    assert(carousel.current() != nullptr);
    assert(carousel.current()->title == "Pending A");
    assert(carousel.heading() == "PENDING 1/2");
    assert(carousel.update(3000));
    assert(carousel.current()->title == "Pending C");
    assert(carousel.heading() == "PENDING 2/2");
    assert(carousel.update(6000));
    assert(carousel.current()->title == "Pending A");

    CodexStatusSnapshot running;
    running.alerts_valid = true;
    running.alert_revision = 2;
    running.pending_count = 0;
    running.running_count = 2;
    running.completed_count = 1;
    running.alerts = {
        {"Project A", "Running A", "10:01", CodexTaskState::Running},
        {"Project B", "Completed B", "09:59", CodexTaskState::Completed},
        {"Project C", "Running C", "09:58", CodexTaskState::Running},
    };

    assert(carousel.apply(running, 7000));
    assert(carousel.current()->title == "Running A");
    assert(carousel.heading() == "RUNNING 1/2");
    assert(carousel.update(10000));
    assert(carousel.current()->title == "Running C");
    assert(carousel.heading() == "RUNNING 2/2");

    CodexStatusSnapshot completed;
    completed.alerts_valid = true;
    completed.alert_revision = 3;
    completed.pending_count = 0;
    completed.running_count = 0;
    completed.completed_count = 2;
    completed.alerts = {
        {"StopWatch", "Status monitor", "10:00", CodexTaskState::Completed},
        {"webshot", "Improve UI", "09:58", CodexTaskState::Completed},
    };

    assert(carousel.apply(completed, 11000));
    assert(carousel.heading() == "COMPLETED");
    const std::string fixed_title = carousel.current()->title;
    assert(!carousel.update(14000));
    assert(carousel.current()->title == fixed_title);
    assert(carousel.completed());
}
