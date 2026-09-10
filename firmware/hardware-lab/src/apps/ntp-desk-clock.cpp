#include <Arduino.h>
#include <TM1637Display.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <time.h>

#include <atomic>
#include <cstring>

#include "ntp_clock_config.h"

#ifndef CLOCK_CLK_PIN
#error "CLOCK_CLK_PIN is required"
#endif
#ifndef CLOCK_DIO_PIN
#error "CLOCK_DIO_PIN is required"
#endif
#ifndef SETUP_BUTTON_PIN
#error "SETUP_BUTTON_PIN is required"
#endif

namespace {
ClockConfig active_config{};
bool active_ready = false;
TM1637Display display(CLOCK_CLK_PIN, CLOCK_DIO_PIN);
uint32_t last_wifi_attempt_ms = 0U;
uint32_t last_display_ms = 0U;
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

void showUnavailable() {
    static const uint8_t dashes[] = {0x40U, 0x40U, 0x40U, 0x40U};
    display.setSegments(dashes);
}

void initializeClock() {
    configTzTime(active_config.timezone, "pool.ntp.org", "time.nist.gov");
    Serial.printf("Clock ready on CLK GPIO%d / DIO GPIO%d; timezone %s\n", CLOCK_CLK_PIN, CLOCK_DIO_PIN, active_config.timezone);
}
}  // namespace

void setup() {
    Serial.begin(115200);
    display.setBrightness(4U, true);
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
        struct tm local{};
        if (getLocalTime(&local, 50U) && local.tm_year >= 120) {
            const int digits = local.tm_hour * 100 + local.tm_min;
            display.showNumberDecEx(digits, (local.tm_sec % 2 == 0) ? 0x40U : 0x00U, true, 4U, 0U);
        } else showUnavailable();
    }
    delay(5);
}
