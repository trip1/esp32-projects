#include "pico_logic.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace pico_lab {

namespace {

bool asciiCaseEqual(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto l = static_cast<unsigned char>(left[index]);
        const auto r = static_cast<unsigned char>(right[index]);
        if (std::tolower(l) != std::tolower(r)) {
            return false;
        }
    }
    return true;
}

std::string_view trimOws(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
        value.remove_suffix(1);
    }
    return value;
}

bool validHeaderName(std::string_view value) {
    if (value.empty()) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](char character) {
        const auto byte = static_cast<unsigned char>(character);
        return std::isalnum(byte) || character == '-';
    });
}

}  // namespace

std::string escapeHtml(std::string_view input, std::size_t max_input) {
    std::string output;
    const std::size_t length = std::min(input.size(), max_input);
    output.reserve(length);
    for (std::size_t index = 0; index < length; ++index) {
        switch (input[index]) {
            case '&': output += "&amp;"; break;
            case '<': output += "&lt;"; break;
            case '>': output += "&gt;"; break;
            case '"': output += "&quot;"; break;
            case '\'': output += "&#39;"; break;
            default: output += input[index]; break;
        }
    }
    return output;
}

int signalPercent(int rssi) {
    if (rssi <= -100) {
        return 0;
    }
    if (rssi >= -50) {
        return 100;
    }
    return (rssi + 100) * 2;
}

std::string_view morseFor(char value) {
    static constexpr std::array<std::string_view, 26> letters = {
        ".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---",
        "-.-", ".-..", "--", "-.", "---", ".--.", "--.-", ".-.", "...", "-",
        "..-", "...-", ".--", "-..-", "-.--", "--.."
    };
    const unsigned char raw = static_cast<unsigned char>(value);
    const char upper = static_cast<char>(std::toupper(raw));
    if (upper < 'A' || upper > 'Z') {
        return {};
    }
    return letters[static_cast<std::size_t>(upper - 'A')];
}

uint8_t additionalMorseGapUnits(char next_character) {
    return next_character == ' ' ? 6 : 2;
}

SurveyRequestResult parseSurveyRequest(std::string_view request) {
    if (request.empty() || request.size() > 1024) {
        return SurveyRequestResult::BadRequest;
    }
    const std::size_t header_end = request.find("\r\n\r\n");
    if (header_end == std::string_view::npos || header_end + 4 != request.size()) {
        return SurveyRequestResult::BadRequest;
    }
    const std::size_t line_end = request.find("\r\n");
    if (line_end == std::string_view::npos) {
        return SurveyRequestResult::BadRequest;
    }
    const std::string_view request_line = request.substr(0, line_end);
    const std::size_t first_space = request_line.find(' ');
    if (first_space == std::string_view::npos) {
        return SurveyRequestResult::BadRequest;
    }
    if (request_line.substr(0, first_space) != "GET") {
        return SurveyRequestResult::MethodNotAllowed;
    }
    if (request_line != "GET / HTTP/1.1" && request_line != "GET / HTTP/1.0") {
        return SurveyRequestResult::BadRequest;
    }

    std::size_t host_count = 0;
    bool host_approved = false;
    std::size_t cursor = line_end + 2;
    while (cursor < header_end) {
        const std::size_t next = request.find("\r\n", cursor);
        if (next == std::string_view::npos || next > header_end || next == cursor) {
            return SurveyRequestResult::BadRequest;
        }
        const std::string_view line = request.substr(cursor, next - cursor);
        if (line.front() == ' ' || line.front() == '\t') {
            return SurveyRequestResult::BadRequest;
        }
        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos || !validHeaderName(line.substr(0, colon))) {
            return SurveyRequestResult::BadRequest;
        }
        const std::string_view name = line.substr(0, colon);
        const std::string_view value = trimOws(line.substr(colon + 1));
        if (std::any_of(value.begin(), value.end(), [](char character) {
            const auto byte = static_cast<unsigned char>(character);
            return (byte < 0x20 && character != '\t') || byte == 0x7f;
        })) {
            return SurveyRequestResult::BadRequest;
        }
        if (asciiCaseEqual(name, "Host")) {
            ++host_count;
            host_approved = value == "192.168.4.1" || value == "192.168.4.1:80";
        } else if (asciiCaseEqual(name, "Content-Length") || asciiCaseEqual(name, "Transfer-Encoding")) {
            return SurveyRequestResult::BadRequest;
        }
        cursor = next + 2;
    }
    if (host_count != 1 || !host_approved) {
        return SurveyRequestResult::HostRejected;
    }
    return SurveyRequestResult::Ok;
}

}  // namespace pico_lab
