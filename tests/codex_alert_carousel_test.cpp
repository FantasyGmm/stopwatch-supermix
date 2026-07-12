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
    mixed.handled_count = 1;
    mixed.alerts = {
        {"Project A", "Pending A", "10:00", true, false},
        {"Project B", "Handled B", "09:59", false, false},
        {"Project C", "Pending C", "09:58", true, true},
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

    CodexStatusSnapshot completed;
    completed.alerts_valid = true;
    completed.alert_revision = 2;
    completed.pending_count = 0;
    completed.handled_count = 2;
    completed.alerts = {
        {"StopWatch", "Status monitor", "10:00", false, false},
        {"webshot", "Improve UI", "09:58", false, false},
    };

    assert(carousel.apply(completed, 7000));
    assert(carousel.heading() == "COMPLETED");
    const std::string fixed_title = carousel.current()->title;
    assert(!carousel.update(10000));
    assert(carousel.current()->title == fixed_title);
    assert(carousel.all_handled());
}
