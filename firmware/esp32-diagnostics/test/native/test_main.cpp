#include "diagnostics_logic.h"

#include <cassert>
#include <cstring>
#include <string>

int main() {
    assert(isValidDiagnosticSsid("Home WiFi"));
    assert(!isValidDiagnosticSsid(""));
    assert(!isValidDiagnosticSsid(std::string(33, 's')));
    assert(isValidDiagnosticPassword(""));
    assert(isValidDiagnosticPassword("12345678"));
    assert(!isValidDiagnosticPassword("short"));
    assert(!isValidDiagnosticPassword(std::string(64, 'p')));

    char ssid[33]{};
    char password[64]{};
    const std::string body = "csrf=abcdef0123456789&ssid=Lab+WiFi&password=test%402024";
    assert(parseDiagnosticSetupForm(body.data(), body.size(), "abcdef0123456789", ssid, sizeof(ssid), password, sizeof(password)));
    assert(std::strcmp(ssid, "Lab WiFi") == 0);
    assert(std::strcmp(password, "test@2024") == 0);
    assert(!parseDiagnosticSetupForm(body.data(), body.size(), "wrong", ssid, sizeof(ssid), password, sizeof(password)));
    const std::string duplicate = "csrf=abcdef0123456789&ssid=A&ssid=B&password=12345678";
    assert(!parseDiagnosticSetupForm(duplicate.data(), duplicate.size(), "abcdef0123456789", ssid, sizeof(ssid), password, sizeof(password)));
    const std::string unknown = "csrf=abcdef0123456789&ssid=A&password=12345678&admin=true";
    assert(!parseDiagnosticSetupForm(unknown.data(), unknown.size(), "abcdef0123456789", ssid, sizeof(ssid), password, sizeof(password)));

    char method[8]{};
    char target[64]{};
    std::size_t content_length = 0;
    bool has_content_length = false;
    const std::string request = "POST /save HTTP/1.1\r\nHost: 192.168.4.1\r\nOrigin: http://192.168.4.1\r\nContent-Length: 7\r\n\r\n";
    assert(parseBoundedHttpRequest(request.data(), request.size(), 1024, method, sizeof(method), target, sizeof(target), content_length, has_content_length));
    assert(std::strcmp(method, "POST") == 0);
    assert(std::strcmp(target, "/save") == 0);
    assert(content_length == 7 && has_content_length);
    const std::string hostile = "POST /save HTTP/1.1\r\nHost: attacker.example\r\nContent-Length: 0\r\n\r\n";
    assert(!parseBoundedHttpRequest(hostile.data(), hostile.size(), 1024, method, sizeof(method), target, sizeof(target), content_length, has_content_length));
    const std::string duplicate_length = "POST /save HTTP/1.1\r\nHost: 192.168.4.1\r\nContent-Length: 1\r\nContent-Length: 1\r\n\r\n";
    assert(!parseBoundedHttpRequest(duplicate_length.data(), duplicate_length.size(), 1024, method, sizeof(method), target, sizeof(target), content_length, has_content_length));

    const std::string lan_request = "POST /api/i2c/scan HTTP/1.1\r\nHost: 10.0.0.44\r\nOrigin: http://10.0.0.44\r\nContent-Length: 22\r\n\r\n";
    assert(parseBoundedLanHttpRequest(lan_request.data(), lan_request.size(), 512, "10.0.0.44", method, sizeof(method), target, sizeof(target), content_length, has_content_length));
    const std::string duplicate_host = "GET / HTTP/1.1\r\nHost: 10.0.0.44\r\nHost: 10.0.0.44\r\n\r\n";
    assert(!parseBoundedLanHttpRequest(duplicate_host.data(), duplicate_host.size(), 512, "10.0.0.44", method, sizeof(method), target, sizeof(target), content_length, has_content_length));
    const std::string foreign_origin = "POST /api/i2c/scan HTTP/1.1\r\nHost: 10.0.0.44\r\nOrigin: http://attacker.example\r\nContent-Length: 0\r\n\r\n";
    assert(!parseBoundedLanHttpRequest(foreign_origin.data(), foreign_origin.size(), 512, "10.0.0.44", method, sizeof(method), target, sizeof(target), content_length, has_content_length));
    const std::string missing_origin = "POST /api/i2c/scan HTTP/1.1\r\nHost: 10.0.0.44\r\nContent-Length: 0\r\n\r\n";
    assert(!parseBoundedLanHttpRequest(missing_origin.data(), missing_origin.size(), 512, "10.0.0.44", method, sizeof(method), target, sizeof(target), content_length, has_content_length));

    assert(extendDiagnosticMillis(1000U, 0xfffffff0U, 0x00000010U) == 1032U);
    assert(diagnosticScanAllowed(1000U, 0U, false, 10000U));
    assert(!diagnosticScanAllowed(10500U, 1000U, true, 10000U));
    assert(diagnosticScanAllowed(11000U, 1000U, true, 10000U));
    assert(diagnosticScanAllowed(0x00000020U, 0xffff0000U, true, 10000U));

    assert(knownI2cDeviceName(0x3c) == std::string("OLED display"));
    assert(knownI2cDeviceName(0x68) == std::string("RTC / IMU"));
    assert(knownI2cDeviceName(0x76) == std::string("BME/BMP environmental sensor"));
    assert(knownI2cDeviceName(0x42).empty());
    return 0;
}
