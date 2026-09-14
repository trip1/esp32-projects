#pragma once

#include <cstddef>
#include <cstdint>

namespace lcd1602 {

constexpr std::size_t kCandidateAddressCount = 16U;

constexpr std::uint8_t candidateAddress(std::size_t index) {
    return index < 8U
        ? static_cast<std::uint8_t>(0x20U + index)
        : static_cast<std::uint8_t>(0x38U + (index - 8U));
}

struct ScanResult {
    std::uint8_t address = 0U;
    std::uint8_t responders = 0U;
    bool selected = false;
};

template <typename Probe>
ScanResult scanAddresses(Probe probe, std::uint8_t preferred_address) {
    ScanResult result{};
    bool preferred_found = false;
    for (std::size_t index = 0U; index < kCandidateAddressCount; ++index) {
        const std::uint8_t address = candidateAddress(index);
        if (!probe(address)) continue;
        if (result.responders < 0xffU) ++result.responders;
        if (address == preferred_address) preferred_found = true;
    }
    if (preferred_found) {
        result.address = preferred_address;
        result.selected = true;
    }
    return result;
}

}  // namespace lcd1602
