#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct DeviceState {
    std::string address;
    std::string name;
    int rssi = 0;
    std::uint32_t first_seen_ms = 0;
    std::uint32_t last_seen_ms = 0;
    std::uint32_t last_published_ms = 0;
    std::uint32_t seen_count = 0;
};

struct ObservationResult {
    DeviceState* state;
    bool should_publish;
};

class DeviceRegistry {
public:
    explicit DeviceRegistry(std::size_t capacity);

    ObservationResult observe(
        const std::string& address,
        const std::string& name,
        int rssi,
        std::uint32_t now_ms,
        std::uint32_t publish_interval_ms);

    DeviceState* find(const std::string& address);
    const DeviceState* find(const std::string& address) const;
    std::size_t size() const;

private:
    std::size_t capacity_;
    std::vector<DeviceState> devices_;
};
