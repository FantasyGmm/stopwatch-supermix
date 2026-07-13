/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "runtime/runtime_diagnostics.h"

#include <cassert>

int main()
{
    RuntimeDiagnosticSnapshot before;
    before.internal_free_heap = 100000;
    before.internal_min_free_heap = 90000;
    before.psram_free = 2000000;
    before.task_count = 20;
    RuntimeDiagnosticSnapshot after = before;
    after.internal_free_heap = 97000;
    after.internal_min_free_heap = 87000;
    after.psram_free = 1999000;

    const auto delta = CompareRuntimeSnapshots(before, after);
    assert(delta.internal_heap_loss == 3000);
    assert(delta.psram_loss == 1000);
    assert(delta.task_delta == 0);
    assert(PassesSingleSessionLeakGate(delta));

    after.internal_free_heap = 95000;
    assert(!PassesSingleSessionLeakGate(CompareRuntimeSnapshots(before, after)));
    return 0;
}
