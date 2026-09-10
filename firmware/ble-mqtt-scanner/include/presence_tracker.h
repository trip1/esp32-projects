#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

enum class PresenceTransition {
    None,
    Entered,
    Exited,
};

struct PresenceState {
    std::string address;
    std::string name;
    int rssi = 0;
    std::uint32_t last_seen_ms = 0;
    bool present = false;
    bool dirty = false;
};

struct PresenceResult {
    PresenceTransition transition;
    PresenceState* state;
};

class PresenceTracker {
public:
    PresenceTracker(std::size_t capacity, int enter_rssi);

    PresenceResult observe(
        const std::string& address,
        const std::string& name,
        int rssi,
        std::uint32_t now_ms);

    std::vector<PresenceResult> expire(std::uint32_t now_ms, std::uint32_t timeout_ms);
    void markPublished(const std::string& address);
    PresenceState* find(const std::string& address);
    const std::vector<PresenceState>& states() const;
    std::size_t size() const;

private:
    std::size_t capacity_;
    int enter_rssi_;
    std::vector<PresenceState> states_;
};
