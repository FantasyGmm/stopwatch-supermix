#pragma once

#include <cstdint>

class TraeCardTapLogic {
public:
    void press(int x, int y, std::uint32_t timestamp_ms, bool inside_deck);
    void move(int x, int y);
    bool release(std::uint32_t timestamp_ms);
    void cancel();

private:
    int start_x_ = 0;
    int start_y_ = 0;
    std::uint32_t start_timestamp_ms_ = 0;
    bool candidate_ = false;
    bool moved_too_far_ = false;
};
