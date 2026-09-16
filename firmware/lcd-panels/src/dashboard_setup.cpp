#include "dashboard_setup.h"

#include <cstdio>
#include <cstring>

namespace {
constexpr std::size_t kMaximumBodyBytes = 3072U;

struct ParsedFields {
    DashboardSetupFields* output = nullptr;
    char order[kDashboardScreenCount][12]{};
    char duration[kDashboardScreenCount][5]{};
    std::uint64_t seen = 0U;
};

bool decode(const char* input, std::size_t length, char* output, std::size_t capacity) {
    if (length >= capacity) return false;
    std::size_t written = 0U;
    auto hex = [](char value) -> int {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        return -1;
    };
    for (std::size_t index = 0U; index < length; ++index) {
        unsigned char value = static_cast<unsigned char>(input[index]);
        if (value == '+') value = ' ';
        else if (value == '%') {
            if (index + 2U >= length) return false;
            const int high = hex(input[++index]);
            const int low = hex(input[++index]);
            if (high < 0 || low < 0) return false;
            value = static_cast<unsigned char>((high << 4) | low);
        }
        if (value < 0x20U || value > 0x7eU || written + 1U >= capacity) return false;
        output[written++] = static_cast<char>(value);
    }
    output[written] = '\0';
    return true;
}

bool assign(ParsedFields& fields, const char* name, std::size_t name_length,
            const char* value, std::size_t value_length) {
    struct Definition { const char* name; std::uint32_t bit; char* output; std::size_t capacity; } definitions[] = {
        {"csrf", 1U << 0, fields.output->csrf, sizeof(fields.output->csrf)},
        {"wifi_ssid", 1U << 1, fields.output->value.sources.wifi_ssid, sizeof(fields.output->value.sources.wifi_ssid)},
        {"wifi_password", 1U << 2, fields.output->value.sources.wifi_password, sizeof(fields.output->value.sources.wifi_password)},
        {"latitude", 1U << 3, fields.output->value.sources.latitude, sizeof(fields.output->value.sources.latitude)},
        {"longitude", 1U << 4, fields.output->value.sources.longitude, sizeof(fields.output->value.sources.longitude)},
        {"timezone", 1U << 5, fields.output->value.timezone, sizeof(fields.output->value.timezone)},
    };
    for (const auto& definition : definitions) {
        if (std::strlen(definition.name) != name_length || std::memcmp(name, definition.name, name_length) != 0) continue;
        if ((fields.seen & definition.bit) != 0U || !decode(value, value_length, definition.output, definition.capacity)) return false;
        fields.seen |= definition.bit;
        return true;
    }
    for (std::size_t index = 0U; index < kDashboardScreenCount; ++index) {
        char order_name[9]{};
        char duration_name[24]{};
        std::snprintf(order_name, sizeof(order_name), "order_%u", static_cast<unsigned>(index + 1U));
        std::snprintf(duration_name, sizeof(duration_name), "duration_%s", dashboardScreenName(static_cast<DashboardScreen>(index)));
        const std::uint64_t order_bit = 1ULL << (6U + index);
        const std::uint64_t duration_bit = 1ULL << (15U + index);
        if (std::strlen(order_name) == name_length && std::memcmp(name, order_name, name_length) == 0) {
            if ((fields.seen & order_bit) != 0U || !decode(value, value_length, fields.order[index], sizeof(fields.order[index]))) return false;
            fields.seen |= order_bit;
            return true;
        }
        if (std::strlen(duration_name) == name_length && std::memcmp(name, duration_name, name_length) == 0) {
            if ((fields.seen & duration_bit) != 0U || !decode(value, value_length, fields.duration[index], sizeof(fields.duration[index]))) return false;
            fields.seen |= duration_bit;
            return true;
        }
    }
    return false;
}
}  // namespace

bool dashboardParseSetupForm(const char* body, std::size_t length, DashboardSetupFields& output) {
    output = {};
    if (body == nullptr || length == 0U || length > kMaximumBodyBytes) return false;
    ParsedFields fields{&output};
    std::size_t start = 0U;
    unsigned count = 0U;
    while (start < length) {
        if (++count > 24U) return false;
        std::size_t end = start;
        while (end < length && body[end] != '&') ++end;
        std::size_t equals = start;
        while (equals < end && body[equals] != '=') ++equals;
        if (equals == start || equals == end
            || !assign(fields, body + start, equals - start, body + equals + 1U, end - equals - 1U)) return false;
        start = end + 1U;
    }
    if (fields.seen != 0xffffffULL) return false;
    for (std::size_t index = 0U; index < kDashboardScreenCount; ++index) {
        if (!dashboardParseScreen(fields.order[index], output.value.schedule.order[index])) return false;
        unsigned duration = 0U;
        if (fields.duration[index][0] == '\0') return false;
        for (const char* digit = fields.duration[index]; *digit != '\0'; ++digit) {
            if (*digit < '0' || *digit > '9') return false;
            duration = duration * 10U + static_cast<unsigned>(*digit - '0');
        }
        output.value.schedule.duration_seconds[index] = static_cast<std::uint16_t>(duration);
    }
    return true;
}
