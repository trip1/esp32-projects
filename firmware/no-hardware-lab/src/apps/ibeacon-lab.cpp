#include <Arduino.h>
#include <NimBLEBeacon.h>
#include <NimBLEDevice.h>

namespace {
constexpr char kBeaconUuid[] = "8c1f7f20-5d3b-4d8a-9a71-6f4453390001";
NimBLEAdvertising* advertising = nullptr;

void startBeacon() {
    NimBLEBeacon beacon;
    beacon.setMajor(9);
    beacon.setMinor(42);
    beacon.setSignalPower(-59);
    beacon.setProximityUUID(NimBLEUUID(kBeaconUuid));

    NimBLEAdvertisementData data;
    data.setFlags(BLE_HS_ADV_F_BREDR_UNSUP);
    data.setManufacturerData(beacon.getData());
    advertising->setAdvertisingInterval(160);
    advertising->setAdvertisementData(data);
    if (advertising->start()) Serial.println("iBeacon advertising: major 9, minor 42");
    else Serial.println("iBeacon start failed; retrying");
}
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200);
    NimBLEDevice::init("DS9-iBeacon-Lab");
    advertising = NimBLEDevice::getAdvertising();
    startBeacon();
}

void loop() {
    if (!advertising->isAdvertising()) startBeacon();
    delay(1000);
}
