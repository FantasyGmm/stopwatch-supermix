#pragma once

#include "debug_command_parser.h"

#include <array>
#include <cstddef>

class DebugCommandService {
public:
    void update();

private:
    void consume(char value);
    void dispatchLine();
    void resetLine();

    std::array<char, debug_command_max_line_length + 1> _line{};
    std::size_t _length = 0;
    bool _overflow = false;
};

DebugCommandService& GetDebugCommandService();
