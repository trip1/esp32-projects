#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace pico_lab {

enum class SurveyRequestResult {
    Ok,
    BadRequest,
    MethodNotAllowed,
    HostRejected,
};

std::string escapeHtml(std::string_view input, std::size_t max_input = 32);
int signalPercent(int rssi);
std::string_view morseFor(char value);
SurveyRequestResult parseSurveyRequest(std::string_view request);
uint8_t additionalMorseGapUnits(char next_character);

}  // namespace pico_lab
