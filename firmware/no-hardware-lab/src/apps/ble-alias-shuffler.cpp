#include <Arduino.h>
#include <NimBLEDevice.h>
#include <esp_random.h>

#include <string>

#include "lab_logic.h"

namespace {
NimBLEAdvertising* advertising = nullptr;
uint32_t last_change_ms = 0;
std::size_t current_alias = 0;

void advertiseAlias(std::size_t index) {
    advertising->stop();
    std::string name = aliasAt(index);
    if (name.size() > 20) name.resize(20);

    NimBLEAdvertisementData data;
    data.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
    data.setName(name);
    advertising->setAdvertisementData(data);
    if (advertising->start()) {
        Serial.printf("Now advertising as: %s\n", name.c_str());
    } else {
        Serial.println("BLE alias advertising failed; will retry");
    }
}
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200);
    NimBLEDevice::init("");
    advertising = NimBLEDevice::getAdvertising();
    current_alias = boundedChoice(esp_random(), aliasCount());
    advertiseAlias(current_alias);
    last_change_ms = millis();
}

void loop() {
    const uint32_t now = millis();
    if (static_cast<uint32_t>(now - last_change_ms) >= 20000U || !advertising->isAdvertising()) {
        last_change_ms = now;
        current_alias = (current_alias + 1U) % aliasCount();
        advertiseAlias(current_alias);
    }
    delay(250);
}
