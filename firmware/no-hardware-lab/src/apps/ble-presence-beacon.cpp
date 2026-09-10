#include <Arduino.h>
#include <NimBLEDevice.h>

namespace {
NimBLEAdvertising* advertising = nullptr;

void startBeacon() {
    NimBLEAdvertisementData data;
    data.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
    data.setName("DS9-Presence-Beacon");
    const std::string manufacturer{"D9P1", 4};
    data.setManufacturerData(manufacturer);
    advertising->setAdvertisementData(data);
    if (!advertising->start()) {
        Serial.println("BLE advertising failed; retrying");
    } else {
        Serial.println("Advertising as DS9-Presence-Beacon");
    }
}
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200);
    NimBLEDevice::init("DS9-Presence-Beacon");
    advertising = NimBLEDevice::getAdvertising();
    startBeacon();
}

void loop() {
    if (!advertising->isAdvertising()) {
        startBeacon();
    }
    delay(1000);
}
