#include <cassert>
#include <cstdint>
#include <string>

#include "sensor_config_logic.h"

int main() {
    assert(isValidWifiSsid("Home WiFi"));
    assert(!isValidWifiSsid(""));
    assert(!isValidWifiSsid(std::string(33, 'a')));

    assert(isValidWifiPassword(""));
    assert(isValidWifiPassword("12345678"));
    assert(isValidWifiPassword(std::string(63, 'p')));
    assert(!isValidWifiPassword("short"));
    assert(!isValidWifiPassword(std::string(64, 'p')));

    assert(isValidMqttHost("mqtt.home.arpa"));
    assert(isValidMqttHost("10.0.0.10"));
    assert(!isValidMqttHost(""));
    assert(!isValidMqttHost("bad host"));
    assert(!isValidMqttHost("mqtt/#"));

    assert(isValidOptionalCredential("", 64));
    assert(isValidOptionalCredential("sensor-user", 64));
    assert(!isValidOptionalCredential(std::string(65, 'u'), 64));
    assert(!isValidOptionalCredential("bad\nvalue", 64));

    assert(isValidTopicPrefix("home/environment"));
    assert(isValidTopicPrefix("sensors_room-1"));
    assert(!isValidTopicPrefix(""));
    assert(!isValidTopicPrefix("/leading"));
    assert(!isValidTopicPrefix("trailing/"));
    assert(!isValidTopicPrefix("home/+/temperature"));
    assert(!isValidTopicPrefix("home/#"));
    assert(!isValidTopicPrefix(std::string(97, 'a')));

    assert(isValidSleepMinutes(1));
    assert(isValidSleepMinutes(5));
    assert(isValidSleepMinutes(1440));
    assert(!isValidSleepMinutes(0));
    assert(!isValidSleepMinutes(1441));

    assert(isValidBme280Reading(22.5F, 48.0F, 1013.2F));
    assert(isValidBme280Reading(-40.0F, 0.0F, 300.0F));
    assert(isValidBme280Reading(85.0F, 100.0F, 1100.0F));
    assert(!isValidBme280Reading(-40.1F, 50.0F, 1000.0F));
    assert(!isValidBme280Reading(20.0F, 100.1F, 1000.0F));
    assert(!isValidBme280Reading(20.0F, 50.0F, 299.9F));

    std::size_t content_length = 0;
    bool has_content_length = false;
    const std::string valid_headers = "POST /save HTTP/1.1\r\nHost: 192.168.4.1\r\nContent-Length: 42\r\n\r\n";
    assert(parseBoundedHttpHeaders(valid_headers.data(), valid_headers.size(), 1024, content_length, has_content_length));
    assert(has_content_length && content_length == 42);
    const std::string lowercase = "POST / HTTP/1.1\r\ncontent-length: 7\r\n\r\n";
    assert(parseBoundedHttpHeaders(lowercase.data(), lowercase.size(), 1024, content_length, has_content_length));
    assert(has_content_length && content_length == 7);
    const std::string get_headers = "GET / HTTP/1.1\r\nHost: device\r\n\r\n";
    assert(parseBoundedHttpHeaders(get_headers.data(), get_headers.size(), 1024, content_length, has_content_length));
    assert(!has_content_length && content_length == 0);
    const std::string duplicate = "POST / HTTP/1.1\r\nContent-Length: 7\r\ncontent-length: 7\r\n\r\n";
    assert(!parseBoundedHttpHeaders(duplicate.data(), duplicate.size(), 1024, content_length, has_content_length));
    const std::string trailing = "POST / HTTP/1.1\r\nContent-Length: 7junk\r\n\r\n";
    assert(!parseBoundedHttpHeaders(trailing.data(), trailing.size(), 1024, content_length, has_content_length));
    const std::string smuggled = "GET /Content-Length:99 HTTP/1.1\r\nX-Test: Content-Length: 9\r\n\r\n";
    assert(parseBoundedHttpHeaders(smuggled.data(), smuggled.size(), 1024, content_length, has_content_length));
    assert(!has_content_length);
    const std::string chunked = "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n";
    assert(!parseBoundedHttpHeaders(chunked.data(), chunked.size(), 1024, content_length, has_content_length));
    std::string nul_header = "GET / HTTP/1.1\r\nHost: bad\r\n\r\n";
    nul_header[20] = '\0';
    assert(!parseBoundedHttpHeaders(nul_header.data(), nul_header.size(), 1024, content_length, has_content_length));

    char method[8]{};
    char target[64]{};
    assert(parseBoundedHttpRequest(valid_headers.data(), valid_headers.size(), 1024,
                                   method, sizeof(method), target, sizeof(target),
                                   content_length, has_content_length));
    assert(std::string(method) == "POST" && std::string(target) == "/save");
    const std::string tab_alias = "POST /clear\tignored HTTP/1.1\r\nContent-Length: 7\r\n\r\n";
    assert(!parseBoundedHttpRequest(tab_alias.data(), tab_alias.size(), 1024,
                                    method, sizeof(method), target, sizeof(target),
                                    content_length, has_content_length));
    const std::string lone_lf = "POST /clear\nignored HTTP/1.1\r\nContent-Length: 7\r\n\r\n";
    assert(!parseBoundedHttpRequest(lone_lf.data(), lone_lf.size(), 1024,
                                    method, sizeof(method), target, sizeof(target),
                                    content_length, has_content_length));
    const std::string overlong_target = "GET /" + std::string(64, 'a') + " HTTP/1.1\r\n\r\n";
    assert(!parseBoundedHttpRequest(overlong_target.data(), overlong_target.size(), 1024,
                                    method, sizeof(method), target, sizeof(target),
                                    content_length, has_content_length));

    assert(shouldProcessPending(true, false));
    assert(!shouldProcessPending(true, true));
    assert(!shouldProcessPending(false, false));

    assert(sleepMinutesAfterPendingAttempt(false, true, true, true, 30, 5, 5) == 30);
    assert(sleepMinutesAfterPendingAttempt(true, true, true, true, 30, 5, 5) == 30);
    assert(sleepMinutesAfterPendingAttempt(true, true, false, true, 1440, 5, 5) == 5);
    assert(sleepMinutesAfterPendingAttempt(true, false, false, true, 1440, 10, 5) == 10);
    assert(sleepMinutesAfterPendingAttempt(true, true, false, false, 1440, 0, 5) == 5);

    static constexpr std::uint8_t test[] = "123456789";
    assert(configurationCrc32(test, sizeof(test) - 1) == 0xcbf43926U);
    return 0;
}
