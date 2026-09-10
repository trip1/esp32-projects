#pragma once

#include <Arduino.h>

struct ClockConfig {
    uint32_t magic;
    uint16_t version;
    char wifi_ssid[33];
    char wifi_password[64];
    char timezone[65];
    uint32_t crc32;
};

bool clockConfigValid(const ClockConfig& value);
bool clockLoadConfig(const char* key, ClockConfig& value);
bool clockStoreConfig(const char* key, const ClockConfig& value);
bool clockRemoveConfig(const char* key);
bool clockPromotePending(const ClockConfig& value);
bool clockPendingSuppressed();
bool clockRejectPending();
bool clockRecoveryRequested();
bool clockStartProvisioning();
bool clockProvisioningActive();
void clockHandleProvisioning();
