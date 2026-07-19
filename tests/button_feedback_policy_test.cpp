#include "hal/button_feedback_policy.h"

#include <cassert>

int main()
{
    assert(ShouldPlayButtonSfx(true, RuntimeOwner::None));
    assert(!ShouldPlayButtonSfx(false, RuntimeOwner::None));
    assert(!ShouldPlayButtonSfx(true, RuntimeOwner::Assistant));
    assert(!ShouldPlayButtonSfx(true, RuntimeOwner::Codex));
    assert(!ShouldPlayButtonSfx(true, RuntimeOwner::Badge));
    assert(!ShouldPlayButtonSfx(true, RuntimeOwner::System));
    return 0;
}
