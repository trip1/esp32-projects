#include <Arduino.h>
#if defined(CLOCK_DISPLAY_LCD1602)
#include <Wire.h>
#else
#include <TM1637Display.h>
#endif
#include <WiFi.h>
#include <esp_sntp.h>
#include <time.h>

#include <atomic>
#include <cstring>

#include "ntp_clock_config.h"
#include "ntp_clock_display.h"
#if defined(CLOCK_DISPLAY_LCD1602)
#include "lcd1602_i2c.h"
#endif

#if defined(CLOCK_DISPLAY_LCD1602)
#ifndef CLOCK_SDA_PIN
#error "CLOCK_SDA_PIN is required for LCD1602"
#endif
#ifndef CLOCK_SCL_PIN
#error "CLOCK_SCL_PIN is required for LCD1602"
#endif
#ifndef CLOCK_LCD_ADDRESS
#error "CLOCK_LCD_ADDRESS is required for LCD1602"
#endif
#else
#ifndef CLOCK_CLK_PIN
#error "CLOCK_CLK_PIN is required"
#endif
#ifndef CLOCK_DIO_PIN
#error "CLOCK_DIO_PIN is required"
#endif
#endif
#ifndef SETUP_BUTTON_PIN
#error "SETUP_BUTTON_PIN is required"
#endif

namespace {
#if defined(CLOCK_DISPLAY_LCD1602)
class CheckedLcd1602 {
public:
    bool begin() {
        if (!Wire.begin(CLOCK_SDA_PIN, CLOCK_SCL_PIN)) return false;
        Wire.setTimeOut(10U);
        const uint32_t scan_started = millis();
        bool scan_timed_out = false;
        const auto scan = lcd1602::scanAddresses([&](uint8_t address) {
            const uint32_t elapsed = static_cast<uint32_t>(millis() - scan_started);
            if (elapsed >= 250U) { scan_timed_out = true; return false; }
            const uint32_t remaining = 250U - elapsed;
            Wire.setTimeOut(remaining < 10U ? remaining : 10U);
            Wire.beginTransmission(address);
            const bool found = Wire.endTransmission(true) == 0U;
            if (static_cast<uint32_t>(millis() - scan_started) > 250U) scan_timed_out = true;
            if (found) Serial.printf("Possible PCF8574 I2C responder found at 0x%02X\n", static_cast<unsigned>(address));
            return found;
        }, CLOCK_LCD_ADDRESS);
        if (scan_timed_out || !scan.selected) {
            Serial.printf("LCD1602 address scan %s with %u possible responders; writes require configured address 0x%02X\n",
                          scan_timed_out ? "timed out" : "completed",
                          static_cast<unsigned>(scan.responders), static_cast<unsigned>(CLOCK_LCD_ADDRESS));
            return false;
        }
        address_ = scan.address;
        Wire.setTimeOut(10U);
        for (uint8_t attempt = 0U; attempt < 2U; ++attempt) {
            Wire.beginTransmission(address_);
            if (Wire.endTransmission(true) != 0U) return false;
        }
        Serial.printf("Using LCD1602 I2C address 0x%02X\n", static_cast<unsigned>(address_));
        Wire.setTimeOut(25U);
        const uint32_t started = millis();
        delay(50);
        if (!writeNibble(0x30U, false, started, 250U)) return false;
        delay(5);
        if (!writeNibble(0x30U, false, started, 250U)) return false;
        delayMicroseconds(150);
        if (!writeNibble(0x30U, false, started, 250U)) return false;
        if (!writeNibble(0x20U, false, started, 250U)) return false;
        if (!command(0x28U, started, 250U)) return false;
        if (!command(0x08U, started, 250U)) return false;
        if (!command(0x01U, started, 250U)) return false;
        delay(2);
        return command(0x06U, started, 250U) && command(0x0CU, started, 250U);
    }

    bool show(const char top[17], const char bottom[17]) {
        const uint32_t started = millis();
        return command(0x80U, started, 150U)
            && writeText(top, started, 150U)
            && command(0xC0U, started, 150U)
            && writeText(bottom, started, 150U);
    }

    uint8_t detectedAddress() const { return address_; }

private:
    static constexpr uint8_t kEnable = 0x04U;
    static constexpr uint8_t kRegisterSelect = 0x01U;
    static constexpr uint8_t kBacklight = 0x08U;

    bool transmit(uint8_t value, uint32_t started, uint32_t timeout_ms) {
        const uint32_t elapsed = static_cast<uint32_t>(millis() - started);
        if (elapsed >= timeout_ms) return false;
        const uint32_t remaining = timeout_ms - elapsed;
        Wire.setTimeOut(remaining < 25U ? remaining : 25U);
        Wire.beginTransmission(address_);
        if (Wire.write(value) != 1U) return false;
        return Wire.endTransmission(true) == 0U;
    }

    bool writeNibble(uint8_t nibble, bool data, uint32_t started, uint32_t timeout_ms) {
        const uint8_t value = static_cast<uint8_t>((nibble & 0xF0U) | kBacklight | (data ? kRegisterSelect : 0U));
        return transmit(value, started, timeout_ms)
            && transmit(static_cast<uint8_t>(value | kEnable), started, timeout_ms)
            && transmit(value, started, timeout_ms);
    }

    bool writeByte(uint8_t value, bool data, uint32_t started, uint32_t timeout_ms) {
        return writeNibble(value, data, started, timeout_ms)
            && writeNibble(static_cast<uint8_t>(value << 4U), data, started, timeout_ms);
    }

    bool command(uint8_t value, uint32_t started, uint32_t timeout_ms) {
        return writeByte(value, false, started, timeout_ms);
    }

    bool writeText(const char value[17], uint32_t started, uint32_t timeout_ms) {
        for (uint8_t index = 0U; index < 16U; ++index) {
            if (!writeByte(static_cast<uint8_t>(value[index]), true, started, timeout_ms)) return false;
        }
        return true;
    }

    uint8_t address_ = CLOCK_LCD_ADDRESS;
};
#endif

ClockConfig active_config{};
bool active_ready = false;
#if defined(CLOCK_DISPLAY_LCD1602)
CheckedLcd1602 display;
#else
TM1637Display display(CLOCK_CLK_PIN, CLOCK_DIO_PIN);
#endif
bool display_ready = false;
uint32_t last_wifi_attempt_ms = 0U;
uint32_t last_display_ms = 0U;
uint32_t last_display_probe_ms = 0U;
std::atomic<bool> fresh_ntp_sync{false};

void onNtpSync(struct timeval*) {
    fresh_ntp_sync.store(true, std::memory_order_release);
}

bool connectAndSynchronize(const ClockConfig& value, uint32_t timeout_ms) {
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.begin(value.wifi_ssid, value.wifi_password);
    const uint32_t started = millis();
    while (WiFi.status() != WL_CONNECTED && static_cast<uint32_t>(millis() - started) < timeout_ms) delay(100);
    if (WiFi.status() != WL_CONNECTED) return false;
    esp_sntp_stop();
    fresh_ntp_sync.store(false, std::memory_order_release);
    sntp_set_time_sync_notification_cb(onNtpSync);
    configTzTime(value.timezone, "pool.ntp.org", "time.nist.gov");
    const uint32_t sync_started = millis();
    while (!fresh_ntp_sync.load(std::memory_order_acquire) && static_cast<uint32_t>(millis() - sync_started) < 10000U) delay(50);
    struct tm local{};
    return fresh_ntp_sync.load(std::memory_order_acquire) && getLocalTime(&local, 50U) && local.tm_year >= 120;
}

bool initializeDisplay() {
#if defined(CLOCK_DISPLAY_LCD1602)
    if (!display.begin()) {
        Serial.printf("LCD1602 initialization failed at configured I2C address 0x%02X on SDA GPIO%d / SCL GPIO%d\n", static_cast<unsigned>(CLOCK_LCD_ADDRESS), CLOCK_SDA_PIN, CLOCK_SCL_PIN);
        return false;
    }
    display_ready = true;
#else
    display.setBrightness(4U, true);
    display_ready = true;
#endif
    return true;
}

void showUnavailable() {
    if (!display_ready) return;
#if defined(CLOCK_DISPLAY_LCD1602)
    char top[17]{};
    char bottom[17]{};
    formatLcd1602Unavailable(top, bottom);
    if (!display.show(top, bottom)) display_ready = false;
#else
    static const uint8_t dashes[] = {0x40U, 0x40U, 0x40U, 0x40U};
    display.setSegments(dashes);
#endif
}

void showTime(const struct tm& local) {
    if (!display_ready) return;
#if defined(CLOCK_DISPLAY_LCD1602)
    char top[17]{};
    char bottom[17]{};
    formatLcd1602Clock(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec, top, bottom);
    if (!display.show(top, bottom)) display_ready = false;
#else
    const int digits = local.tm_hour * 100 + local.tm_min;
    display.showNumberDecEx(digits, (local.tm_sec % 2 == 0) ? 0x40U : 0x00U, true, 4U, 0U);
#endif
}

void initializeClock() {
    configTzTime(active_config.timezone, "pool.ntp.org", "time.nist.gov");
#if defined(CLOCK_DISPLAY_LCD1602)
    Serial.printf("Clock ready for LCD1602 at I2C 0x%02X on SDA GPIO%d / SCL GPIO%d; timezone %s\n", static_cast<unsigned>(display.detectedAddress()), CLOCK_SDA_PIN, CLOCK_SCL_PIN, active_config.timezone);
#else
    Serial.printf("Clock ready on CLK GPIO%d / DIO GPIO%d; timezone %s\n", CLOCK_CLK_PIN, CLOCK_DIO_PIN, active_config.timezone);
#endif
}
}  // namespace

void setup() {
    Serial.begin(115200);
    initializeDisplay();
    showUnavailable();
    pinMode(SETUP_BUTTON_PIN, INPUT_PULLUP);
    active_ready = clockLoadConfig("active", active_config);
    ClockConfig pending{};
    const bool pending_valid = clockLoadConfig("pending", pending);
    const bool pending_matches_active = pending_valid && active_ready &&
        std::memcmp(&pending, &active_config, sizeof(pending)) == 0;
    const bool pending_ready = pending_valid && !pending_matches_active && !clockPendingSuppressed();
    if (!pending_valid) clockRemoveConfig("pending");
    else if (pending_matches_active && !clockRemoveConfig("pending")) {
        Serial.println("Already-promoted pending record could not be removed; active clock configuration remains authoritative");
    }
    if ((active_ready || pending_ready) && clockRecoveryRequested()) {
        if (!clockStartProvisioning()) { delay(30000); ESP.restart(); }
        return;
    }
    if (pending_ready) {
        if (connectAndSynchronize(pending, 15000U) && clockPromotePending(pending)) {
            active_config = pending;
            active_ready = true;
            Serial.println("Pending Wi-Fi/timezone settings verified and promoted");
        } else {
            clockRejectPending();
            WiFi.disconnect(true, false);
            Serial.println("Pending clock settings rejected; active settings preserved");
        }
    }
    if (!active_ready) {
        if (!clockStartProvisioning()) { delay(30000); ESP.restart(); }
        return;
    }
    if (WiFi.status() != WL_CONNECTED) connectAndSynchronize(active_config, 15000U);
    initializeClock();
}

void loop() {
    if (clockProvisioningActive()) {
        clockHandleProvisioning();
        delay(2);
        return;
    }
    const uint32_t now = millis();
    if (WiFi.status() != WL_CONNECTED && static_cast<uint32_t>(now - last_wifi_attempt_ms) >= 30000U) {
        last_wifi_attempt_ms = now;
        WiFi.begin(active_config.wifi_ssid, active_config.wifi_password);
    }
    if (static_cast<uint32_t>(now - last_display_ms) >= 500U) {
        last_display_ms = now;
#if defined(CLOCK_DISPLAY_LCD1602)
        if (!display_ready && static_cast<uint32_t>(now - last_display_probe_ms) >= 5000U) {
            last_display_probe_ms = now;
            if (initializeDisplay()) showUnavailable();
        }
#endif
        struct tm local{};
        if (getLocalTime(&local, 50U) && local.tm_year >= 120) showTime(local);
        else showUnavailable();
    }
    delay(5);
}
