#include <unity.h>

#include <cstdint>

#include "presence_tracker.h"

void test_strong_first_observation_emits_enter() {
    PresenceTracker tracker(4, -75);

    const auto result = tracker.observe("aa:bb:cc:dd:ee:ff", "Tag", -60, 1000);

    TEST_ASSERT_EQUAL(PresenceTransition::Entered, result.transition);
    TEST_ASSERT_NOT_NULL(result.state);
    TEST_ASSERT_TRUE(result.state->present);
    TEST_ASSERT_TRUE(result.state->dirty);
}

void test_weak_unknown_device_does_not_enter() {
    PresenceTracker tracker(4, -75);

    const auto result = tracker.observe("aa:bb:cc:dd:ee:ff", "Far tag", -90, 1000);

    TEST_ASSERT_EQUAL(PresenceTransition::None, result.transition);
    TEST_ASSERT_NULL(result.state);
    TEST_ASSERT_EQUAL_UINT32(0, tracker.size());
}

void test_repeat_observation_does_not_duplicate_enter() {
    PresenceTracker tracker(4, -75);
    tracker.observe("aa:bb:cc:dd:ee:ff", "Tag", -60, 1000);
    tracker.markPublished("aa:bb:cc:dd:ee:ff");

    const auto result = tracker.observe("aa:bb:cc:dd:ee:ff", "Tag", -80, 2000);

    TEST_ASSERT_EQUAL(PresenceTransition::None, result.transition);
    TEST_ASSERT_FALSE(result.state->dirty);
    TEST_ASSERT_EQUAL_UINT32(2000, result.state->last_seen_ms);
}

void test_timeout_emits_exit_across_millis_wraparound() {
    PresenceTracker tracker(4, -75);
    tracker.observe("aa:bb:cc:dd:ee:ff", "Tag", -60, UINT32_MAX - 40);
    tracker.markPublished("aa:bb:cc:dd:ee:ff");

    const auto transitions = tracker.expire(60, 100);

    TEST_ASSERT_EQUAL_UINT32(1, transitions.size());
    TEST_ASSERT_EQUAL(PresenceTransition::Exited, transitions[0].transition);
    TEST_ASSERT_FALSE(transitions[0].state->present);
    TEST_ASSERT_TRUE(transitions[0].state->dirty);
}

void test_strong_observation_after_exit_emits_reentry() {
    PresenceTracker tracker(4, -75);
    tracker.observe("aa:bb:cc:dd:ee:ff", "Tag", -60, 1000);
    tracker.expire(2000, 500);
    tracker.markPublished("aa:bb:cc:dd:ee:ff");

    const auto result = tracker.observe("aa:bb:cc:dd:ee:ff", "Tag", -65, 3000);

    TEST_ASSERT_EQUAL(PresenceTransition::Entered, result.transition);
    TEST_ASSERT_TRUE(result.state->present);
    TEST_ASSERT_TRUE(result.state->dirty);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_strong_first_observation_emits_enter);
    RUN_TEST(test_weak_unknown_device_does_not_enter);
    RUN_TEST(test_repeat_observation_does_not_duplicate_enter);
    RUN_TEST(test_timeout_emits_exit_across_millis_wraparound);
    RUN_TEST(test_strong_observation_after_exit_emits_reentry);
    return UNITY_END();
}
