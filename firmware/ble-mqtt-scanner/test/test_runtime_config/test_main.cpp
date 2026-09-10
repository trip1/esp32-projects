#include <unity.h>

#include <string>

#include "scanner_config_logic.h"

void setUp() {}
void tearDown() {}

void test_runtime_configuration_validation() {
    TEST_ASSERT_TRUE(scannerValidWifiSsid("Home WiFi"));
    TEST_ASSERT_FALSE(scannerValidWifiSsid(""));
    TEST_ASSERT_FALSE(scannerValidWifiSsid(std::string(33, 'a')));
    TEST_ASSERT_TRUE(scannerValidWifiPassword(""));
    TEST_ASSERT_TRUE(scannerValidWifiPassword("12345678"));
    TEST_ASSERT_FALSE(scannerValidWifiPassword("short"));
    TEST_ASSERT_TRUE(scannerValidMqttHost("mqtt.home.arpa"));
    TEST_ASSERT_TRUE(scannerValidMqttHost("10.0.0.10"));
    TEST_ASSERT_FALSE(scannerValidMqttHost("bad host"));
    TEST_ASSERT_TRUE(scannerValidCredential("scanner-user", 64));
    TEST_ASSERT_FALSE(scannerValidCredential("bad\nuser", 64));
    TEST_ASSERT_TRUE(scannerValidTopicPrefix("esp32/ble-sightings"));
    TEST_ASSERT_FALSE(scannerValidTopicPrefix("/leading"));
    TEST_ASSERT_FALSE(scannerValidTopicPrefix("home/#"));
}

void test_http_request_parser_is_strict_and_bounded() {
    std::size_t content_length = 0;
    bool has_content_length = false;
    char method[8]{};
    char target[64]{};
    const std::string valid = "POST /save HTTP/1.1\r\nHost: 192.168.4.1\r\ncontent-length: 42\r\n\r\n";
    TEST_ASSERT_TRUE(scannerParseHttpRequest(valid.data(), valid.size(), 1024, method, sizeof(method),
                                             target, sizeof(target), content_length, has_content_length));
    TEST_ASSERT_EQUAL_STRING("POST", method);
    TEST_ASSERT_EQUAL_STRING("/save", target);
    TEST_ASSERT_TRUE(has_content_length);
    TEST_ASSERT_EQUAL_UINT32(42, content_length);

    const std::string duplicate = "POST /save HTTP/1.1\r\nContent-Length: 7\r\ncontent-length: 7\r\n\r\n";
    TEST_ASSERT_FALSE(scannerParseHttpRequest(duplicate.data(), duplicate.size(), 1024, method, sizeof(method),
                                              target, sizeof(target), content_length, has_content_length));
    const std::string chunked = "POST /save HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n";
    TEST_ASSERT_FALSE(scannerParseHttpRequest(chunked.data(), chunked.size(), 1024, method, sizeof(method),
                                              target, sizeof(target), content_length, has_content_length));
    const std::string tab_alias = "POST /clear\tignored HTTP/1.1\r\nContent-Length: 7\r\n\r\n";
    TEST_ASSERT_FALSE(scannerParseHttpRequest(tab_alias.data(), tab_alias.size(), 1024, method, sizeof(method),
                                              target, sizeof(target), content_length, has_content_length));
    const std::string trailing = "POST /save HTTP/1.1\r\nContent-Length: 7junk\r\n\r\n";
    TEST_ASSERT_FALSE(scannerParseHttpRequest(trailing.data(), trailing.size(), 1024, method, sizeof(method),
                                              target, sizeof(target), content_length, has_content_length));
}

void test_provisioning_rejects_dns_rebinding_headers() {
    char method[8]{};
    char target[64]{};
    std::size_t content_length = 0U;
    bool has_content_length = false;
    const std::string valid_get = "GET / HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n";
    TEST_ASSERT_TRUE(scannerParseHttpRequest(valid_get.data(), valid_get.size(), 1024, method, sizeof(method), target, sizeof(target), content_length, has_content_length));
    const std::string valid_post = "POST /save HTTP/1.1\r\nHost: 192.168.4.1\r\nOrigin: http://192.168.4.1\r\nContent-Length: 7\r\n\r\n";
    TEST_ASSERT_TRUE(scannerParseHttpRequest(valid_post.data(), valid_post.size(), 1024, method, sizeof(method), target, sizeof(target), content_length, has_content_length));
    const std::string missing_host = "GET / HTTP/1.1\r\n\r\n";
    TEST_ASSERT_FALSE(scannerParseHttpRequest(missing_host.data(), missing_host.size(), 1024, method, sizeof(method), target, sizeof(target), content_length, has_content_length));
    const std::string hostile_host = "GET / HTTP/1.1\r\nHost: attacker.example\r\n\r\n";
    TEST_ASSERT_FALSE(scannerParseHttpRequest(hostile_host.data(), hostile_host.size(), 1024, method, sizeof(method), target, sizeof(target), content_length, has_content_length));
    const std::string duplicate_host = "GET / HTTP/1.1\r\nHost: 192.168.4.1\r\nHost: 192.168.4.1\r\n\r\n";
    TEST_ASSERT_FALSE(scannerParseHttpRequest(duplicate_host.data(), duplicate_host.size(), 1024, method, sizeof(method), target, sizeof(target), content_length, has_content_length));
    const std::string hostile_origin = "POST /clear HTTP/1.1\r\nHost: 192.168.4.1\r\nOrigin: http://attacker.example\r\nContent-Length: 7\r\n\r\n";
    TEST_ASSERT_FALSE(scannerParseHttpRequest(hostile_origin.data(), hostile_origin.size(), 1024, method, sizeof(method), target, sizeof(target), content_length, has_content_length));
    const std::string absolute_target = "GET http://192.168.4.1/ HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n";
    TEST_ASSERT_FALSE(scannerParseHttpRequest(absolute_target.data(), absolute_target.size(), 1024, method, sizeof(method), target, sizeof(target), content_length, has_content_length));
}

void test_crc32_reference_vector() {
    static constexpr std::uint8_t data[] = "123456789";
    TEST_ASSERT_EQUAL_HEX32(0xcbf43926U, scannerConfigCrc32(data, sizeof(data) - 1U));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_runtime_configuration_validation);
    RUN_TEST(test_http_request_parser_is_strict_and_bounded);
    RUN_TEST(test_provisioning_rejects_dns_rebinding_headers);
    RUN_TEST(test_crc32_reference_vector);
    return UNITY_END();
}
