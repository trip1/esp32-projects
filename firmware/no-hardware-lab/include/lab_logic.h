#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

struct PomodoroPhase {
    bool working;
    std::uint32_t remaining_seconds;
};

class PomodoroTimer {
public:
    explicit PomodoroTimer(std::uint32_t now_ms = 0);
    void reset(std::uint32_t now_ms);
    PomodoroPhase advance(std::uint32_t now_ms);

private:
    std::uint32_t last_ms_;
    std::uint32_t cycle_position_ms_ = 0;
};

const char* morsePattern(char character);
std::string encodeMorse(const std::string& text);
std::size_t aliasCount();
const char* aliasAt(std::size_t index);
std::size_t boundedChoice(std::uint32_t random_value, std::size_t choice_count);
