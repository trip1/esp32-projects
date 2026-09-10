#include "device_registry.h"

#include <algorithm>

DeviceRegistry::DeviceRegistry(std::size_t capacity) : capacity_(capacity) {
    devices_.reserve(capacity);
}

ObservationResult DeviceRegistry::observe(
    const std::string& address,
    const std::string& name,
    int rssi,
    std::uint32_t now_ms,
    std::uint32_t publish_interval_ms) {
    DeviceState* state = find(address);
    if (state == nullptr) {
        if (capacity_ == 0) {
            return {nullptr, false};
        }
        if (devices_.size() == capacity_) {
            const auto oldest = std::max_element(
                devices_.begin(), devices_.end(),
                [now_ms](const DeviceState& left, const DeviceState& right) {
                    const auto left_age = static_cast<std::uint32_t>(now_ms - left.last_seen_ms);
                    const auto right_age = static_cast<std::uint32_t>(now_ms - right.last_seen_ms);
                    return left_age < right_age;
                });
            devices_.erase(oldest);
        }
        devices_.push_back({address, name, rssi, now_ms, now_ms, now_ms, 1});
        return {&devices_.back(), true};
    }

    state->last_seen_ms = now_ms;
    state->rssi = rssi;
    state->seen_count += 1;
    if (!name.empty()) {
        state->name = name;
    }

    const bool due = static_cast<std::uint32_t>(now_ms - state->last_published_ms) >= publish_interval_ms;
    if (due) {
        state->last_published_ms = now_ms;
    }
    return {state, due};
}

DeviceState* DeviceRegistry::find(const std::string& address) {
    const auto found = std::find_if(devices_.begin(), devices_.end(), [&](const DeviceState& state) {
        return state.address == address;
    });
    return found == devices_.end() ? nullptr : &*found;
}

const DeviceState* DeviceRegistry::find(const std::string& address) const {
    const auto found = std::find_if(devices_.begin(), devices_.end(), [&](const DeviceState& state) {
        return state.address == address;
    });
    return found == devices_.end() ? nullptr : &*found;
}

std::size_t DeviceRegistry::size() const {
    return devices_.size();
}
