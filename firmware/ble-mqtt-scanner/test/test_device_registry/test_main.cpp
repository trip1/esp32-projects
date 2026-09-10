#include <unity.h>

#include "device_registry.h"

void test_first_observation_is_publishable() {
    DeviceRegistry registry(4);
    const auto result = registry.observe("aa:bb:cc:dd:ee:ff", "Sensor", -55, 1000, 30000);

    TEST_ASSERT_TRUE(result.should_publish);
    TEST_ASSERT_EQUAL_UINT32(1, result.state->seen_count);
    TEST_ASSERT_EQUAL_UINT32(1000, result.state->first_seen_ms);
}

void test_repeat_is_rate_limited_but_updates_state() {
    DeviceRegistry registry(4);
    registry.observe("aa:bb:cc:dd:ee:ff", "", -70, 1000, 30000);
    const auto result = registry.observe("aa:bb:cc:dd:ee:ff", "Phone", -42, 5000, 30000);

    TEST_ASSERT_FALSE(result.should_publish);
    TEST_ASSERT_EQUAL_UINT32(2, result.state->seen_count);
    TEST_ASSERT_EQUAL_INT(-42, result.state->rssi);
    TEST_ASSERT_EQUAL_STRING("Phone", result.state->name.c_str());
}

void test_repeat_after_interval_is_publishable() {
    DeviceRegistry registry(4);
    registry.observe("aa:bb:cc:dd:ee:ff", "Sensor", -55, 1000, 30000);
    const auto result = registry.observe("aa:bb:cc:dd:ee:ff", "Sensor", -50, 31000, 30000);

    TEST_ASSERT_TRUE(result.should_publish);
    TEST_ASSERT_EQUAL_UINT32(31000, result.state->last_published_ms);
}

void test_capacity_evicts_least_recent_device() {
    DeviceRegistry registry(2);
    registry.observe("first", "", -60, 100, 0);
    registry.observe("second", "", -60, 200, 0);
    registry.observe("third", "", -60, 300, 0);

    TEST_ASSERT_NULL(registry.find("first"));
    TEST_ASSERT_NOT_NULL(registry.find("second"));
    TEST_ASSERT_NOT_NULL(registry.find("third"));
    TEST_ASSERT_EQUAL_UINT32(2, registry.size());
}

void test_capacity_evicts_oldest_across_millis_wraparound() {
    DeviceRegistry registry(2);
    registry.observe("old", "", -60, UINT32_MAX - 10, 0);
    registry.observe("new", "", -60, 5, 0);
    registry.observe("latest", "", -60, 6, 0);

    TEST_ASSERT_NULL(registry.find("old"));
    TEST_ASSERT_NOT_NULL(registry.find("new"));
    TEST_ASSERT_NOT_NULL(registry.find("latest"));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_first_observation_is_publishable);
    RUN_TEST(test_repeat_is_rate_limited_but_updates_state);
    RUN_TEST(test_repeat_after_interval_is_publishable);
    RUN_TEST(test_capacity_evicts_least_recent_device);
    RUN_TEST(test_capacity_evicts_oldest_across_millis_wraparound);
    return UNITY_END();
}
