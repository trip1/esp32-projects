#include "lab_logic.h"

#include <cctype>

namespace {
constexpr std::uint32_t kWorkSeconds = 25U * 60U;
constexpr std::uint32_t kBreakSeconds = 5U * 60U;
constexpr std::uint32_t kCycleSeconds = kWorkSeconds + kBreakSeconds;
constexpr std::uint32_t kCycleMs = kCycleSeconds * 1000U;

constexpr const char* kAliases[] = {
    "Definitely Not A Toaster",
    "Free WiFi Maybe",
    "The Bluetooth Goblin",
    "Government Pigeon 7",
    "Kevin's Smart Fridge",
    "Totally Normal Beacon",
    "Haunted Firmware",
    "Pair With Regret",
};
}  // namespace

PomodoroTimer::PomodoroTimer(std::uint32_t now_ms) : last_ms_(now_ms) {}

void PomodoroTimer::reset(std::uint32_t now_ms) {
    last_ms_ = now_ms;
    cycle_position_ms_ = 0;
}

PomodoroPhase PomodoroTimer::advance(std::uint32_t now_ms) {
    const std::uint32_t delta_ms = static_cast<std::uint32_t>(now_ms - last_ms_);
    last_ms_ = now_ms;
    cycle_position_ms_ = (cycle_position_ms_ + delta_ms % kCycleMs) % kCycleMs;
    if (cycle_position_ms_ < kWorkSeconds * 1000U) {
        const std::uint32_t remaining_ms = kWorkSeconds * 1000U - cycle_position_ms_;
        return {true, (remaining_ms + 999U) / 1000U};
    }
    const std::uint32_t remaining_ms = kCycleMs - cycle_position_ms_;
    return {false, (remaining_ms + 999U) / 1000U};
}

const char* morsePattern(char character) {
    static constexpr const char* letters[] = {
        ".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---", "-.-", ".-..", "--",
        "-.", "---", ".--.", "--.-", ".-.", "...", "-", "..-", "...-", ".--", "-..-", "-.--", "--..",
    };
    static constexpr const char* digits[] = {
        "-----", ".----", "..---", "...--", "....-", ".....", "-....", "--...", "---..", "----.",
    };
    const unsigned char raw = static_cast<unsigned char>(character);
    const char upper = static_cast<char>(std::toupper(raw));
    if (upper >= 'A' && upper <= 'Z') return letters[upper - 'A'];
    if (upper >= '0' && upper <= '9') return digits[upper - '0'];
    return "";
}

std::string encodeMorse(const std::string& text) {
    std::string encoded;
    for (const char character : text) {
        if (character == ' ') {
            if (!encoded.empty() && encoded.back() != ' ') encoded += " /";
            continue;
        }
        const char* pattern = morsePattern(character);
        if (pattern[0] == '\0') continue;
        if (!encoded.empty() && encoded.back() != '/') encoded.push_back(' ');
        encoded += pattern;
    }
    return encoded;
}

std::size_t aliasCount() {
    return sizeof(kAliases) / sizeof(kAliases[0]);
}

const char* aliasAt(std::size_t index) {
    return index < aliasCount() ? kAliases[index] : nullptr;
}

std::size_t boundedChoice(std::uint32_t random_value, std::size_t choice_count) {
    return choice_count == 0 ? 0 : random_value % choice_count;
}
