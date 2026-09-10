#include "presence_tracker.h"

#include <algorithm>

PresenceTracker::PresenceTracker(std::size_t capacity, int enter_rssi)
    : capacity_(capacity), enter_rssi_(enter_rssi) {
    states_.reserve(capacity);
}

PresenceResult PresenceTracker::observe(
    const std::string& address,
    const std::string& name,
    int rssi,
    std::uint32_t now_ms) {
    PresenceState* state = find(address);
    if (state == nullptr) {
        if (rssi < enter_rssi_ || capacity_ == 0) return {PresenceTransition::None, nullptr};

        if (states_.size() == capacity_) {
            const auto reusable = std::find_if(states_.begin(), states_.end(), [](const PresenceState& candidate) {
                return !candidate.present && !candidate.dirty;
            });
            if (reusable == states_.end()) return {PresenceTransition::None, nullptr};
            *reusable = {address, name, rssi, now_ms, true, true};
            return {PresenceTransition::Entered, &*reusable};
        }

        states_.push_back({address, name, rssi, now_ms, true, true});
        return {PresenceTransition::Entered, &states_.back()};
    }

    state->last_seen_ms = now_ms;
    state->rssi = rssi;
    if (!name.empty()) state->name = name;

    if (!state->present && rssi >= enter_rssi_) {
        state->present = true;
        state->dirty = true;
        return {PresenceTransition::Entered, state};
    }
    return {PresenceTransition::None, state};
}

std::vector<PresenceResult> PresenceTracker::expire(
    std::uint32_t now_ms,
    std::uint32_t timeout_ms) {
    std::vector<PresenceResult> transitions;
    for (auto& state : states_) {
        if (state.present && static_cast<std::uint32_t>(now_ms - state.last_seen_ms) >= timeout_ms) {
            state.present = false;
            state.dirty = true;
            transitions.push_back({PresenceTransition::Exited, &state});
        }
    }
    return transitions;
}

void PresenceTracker::markPublished(const std::string& address) {
    PresenceState* state = find(address);
    if (state != nullptr) state->dirty = false;
}

PresenceState* PresenceTracker::find(const std::string& address) {
    const auto found = std::find_if(states_.begin(), states_.end(), [&](const PresenceState& state) {
        return state.address == address;
    });
    return found == states_.end() ? nullptr : &*found;
}

const std::vector<PresenceState>& PresenceTracker::states() const {
    return states_;
}

std::size_t PresenceTracker::size() const {
    return states_.size();
}
