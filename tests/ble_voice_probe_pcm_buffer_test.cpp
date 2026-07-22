#include "main/apps/app_codex_status/ble_voice_probe_controller.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

int main()
{
    ble_voice_probe::detail::MicrophonePcmBuffer buffer;

    std::vector<int16_t> first(700);
    for (size_t index = 0; index < first.size(); ++index) {
        first[index] = static_cast<int16_t>(index);
    }
    buffer.append(first.data(), first.size());

    assert(buffer.complete_blocks() == 2);
    assert(buffer.retained_samples() == 60);

    std::array<int16_t, 320> block = {};
    assert(buffer.take_complete_block(block));
    assert(block.front() == 0);
    assert(block.back() == 319);
    assert(buffer.complete_blocks() == 1);
    assert(buffer.retained_samples() < block.size());

    std::vector<int16_t> second(300);
    for (size_t index = 0; index < second.size(); ++index) {
        second[index] = static_cast<int16_t>(700 + index);
    }
    buffer.append(second.data(), second.size());

    assert(buffer.complete_blocks() == 2);
    assert(buffer.retained_samples() == 40);
    assert(buffer.take_complete_block(block));
    assert(block.front() == 320);
    assert(block.back() == 639);
    assert(buffer.take_complete_block(block));
    assert(block.front() == 640);
    assert(block.back() == 959);
    assert(!buffer.take_complete_block(block));
    assert(buffer.retained_samples() == 40);

    buffer.clear();
    assert(buffer.complete_blocks() == 0);
    assert(buffer.retained_samples() == 0);
    return 0;
}
