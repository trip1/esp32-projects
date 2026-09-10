#pragma once

#include <Arduino.h>

struct ScannerRuntimeConfig {
    uint32_t magic;
    uint16_t version;
    uint16_t mqtt_port;
    char wifi_ssid[33];
    char wifi_password[64];
    char mqtt_host[129];
    char mqtt_username[65];
    char mqtt_password[129];
    char topic_prefix[97];
    uint32_t crc32;
};

bool scannerConfigIsValid(const ScannerRuntimeConfig& value);
bool scannerLoadConfig(const char* key, ScannerRuntimeConfig& value);
bool scannerStoreConfig(const char* key, const ScannerRuntimeConfig& value);
bool scannerRemoveConfig(const char* key);
bool scannerClearConfigs();
bool scannerPromotePending(const ScannerRuntimeConfig& pending);
bool scannerPendingSuppressed();
bool scannerRejectPending();
bool scannerRecoveryRequested();
bool scannerStartProvisioning();
bool scannerProvisioningActive();
void scannerHandleProvisioning();
