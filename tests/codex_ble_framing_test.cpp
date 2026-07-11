#include "main/apps/app_codex_status/codex_ble_framing.h"

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>


static std::vector<uint8_t> frame(uint16_t sequence, uint8_t index, uint8_t count, const char* payload)
{
    std::vector<uint8_t> result = {
        1,
        1,
        static_cast<uint8_t>(sequence & 0xff),
        static_cast<uint8_t>((sequence >> 8) & 0xff),
        index,
        count,
    };
    result.insert(result.end(), payload, payload + std::char_traits<char>::length(payload));
    return result;
}


int main()
{
    CodexBleReassembler reassembler;
    auto first = frame(7, 0, 3, "{\"codex_");
    auto second = frame(7, 1, 3, "model\":\"");
    auto third = frame(7, 2, 3, "gpt-5\"}");

    assert(!reassembler.push(first.data(), first.size(), 100).has_value());
    assert(!reassembler.push(second.data(), second.size(), 200).has_value());
    auto complete = reassembler.push(third.data(), third.size(), 300);
    assert(complete.has_value());
    assert(complete->type == 1);
    assert(complete->payload == "{\"codex_model\":\"gpt-5\"}");

    auto out_of_order = frame(8, 1, 2, "bad");
    assert(!reassembler.push(out_of_order.data(), out_of_order.size(), 400).has_value());

    auto expired_first = frame(9, 0, 2, "old");
    auto expired_second = frame(9, 1, 2, "data");
    assert(!reassembler.push(expired_first.data(), expired_first.size(), 500).has_value());
    assert(!reassembler.push(expired_second.data(), expired_second.size(), 4000).has_value());
}
