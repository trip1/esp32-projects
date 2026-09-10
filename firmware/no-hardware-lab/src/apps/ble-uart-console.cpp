#include <Arduino.h>
#include <NimBLEDevice.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <string>

#include "lab_logic.h"

namespace {
constexpr char kServiceUuid[] = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
constexpr char kRxUuid[] = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
constexpr char kTxUuid[] = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";
struct BleMessage { char text[161]; };
NimBLECharacteristic* tx_characteristic = nullptr;
NimBLEAdvertising* advertising = nullptr;
QueueHandle_t rx_queue = nullptr;
std::string serial_line;
std::atomic<uint32_t> dropped_messages{0};
std::atomic<bool> advertising_requested{true};
uint32_t reported_drops = 0;
uint32_t last_advertising_attempt_ms = 0;

class ServerCallbacks final : public NimBLEServerCallbacks {
    void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override {
        advertising_requested.store(true, std::memory_order_release);
    }
} server_callbacks;

class RxCallbacks final : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo&) override {
        const std::string value = characteristic->getValue();
        BleMessage message{};
        const size_t count = std::min(value.size(), sizeof(message.text) - 1U);
        std::memcpy(message.text, value.data(), count);
        message.text[count] = '\0';
        if (xQueueSend(rx_queue, &message, 0) != pdTRUE) {
            dropped_messages.fetch_add(1, std::memory_order_relaxed);
        }
    }
} rx_callbacks;

void echoMessage(const std::string& input, const char* direction) {
    const std::string message = boundedText(input, 160);
    if (message.empty()) return;
    Serial.printf("%s: %s\n", direction, message.c_str());
    tx_characteristic->setValue(message);
    tx_characteristic->notify();
}

void publishSerialLine() {
    echoMessage(serial_line, "USB -> BLE");
    serial_line.clear();
}

void ensureAdvertising(uint32_t now_ms) {
    if (advertising == nullptr) return;
    if (advertising->isAdvertising()) {
        advertising_requested.store(false, std::memory_order_release);
        return;
    }
    if (!advertising_requested.load(std::memory_order_acquire) ||
        static_cast<uint32_t>(now_ms - last_advertising_attempt_ms) < 1000U) return;
    last_advertising_attempt_ms = now_ms;
    if (advertising->start()) {
        advertising_requested.store(false, std::memory_order_release);
        Serial.println("BLE UART advertising started");
    } else {
        Serial.println("BLE UART advertising failed; retrying");
    }
}
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200);
    rx_queue = xQueueCreate(8, sizeof(BleMessage));
    if (rx_queue == nullptr) abort();

    NimBLEDevice::init("DS9-BLE-UART");
    NimBLEServer* server = NimBLEDevice::createServer();
    server->setCallbacks(&server_callbacks);
    NimBLEService* service = server->createService(kServiceUuid);
    tx_characteristic = service->createCharacteristic(kTxUuid, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ);
    NimBLECharacteristic* rx = service->createCharacteristic(kRxUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    rx->setCallbacks(&rx_callbacks);
    advertising = NimBLEDevice::getAdvertising();
    advertising->addServiceUUID(kServiceUuid);
    advertising->setName("DS9-BLE-UART");
    advertising_requested.store(true, std::memory_order_release);
    Serial.println("BLE UART service ready; send lines at 115200 baud or connect with a Nordic UART client");
}

void loop() {
    ensureAdvertising(millis());
    BleMessage received{};
    while (xQueueReceive(rx_queue, &received, 0) == pdTRUE) {
        echoMessage(received.text, "BLE -> USB");
    }
    while (Serial.available()) {
        const char character = static_cast<char>(Serial.read());
        if (character == '\n' || character == '\r') publishSerialLine();
        else if (serial_line.size() < 160) serial_line.push_back(character);
    }
    const uint32_t drops = dropped_messages.load(std::memory_order_relaxed);
    if (drops != reported_drops) {
        reported_drops = drops;
        Serial.printf("BLE UART receive queue drops: %lu\n", static_cast<unsigned long>(drops));
    }
    delay(5);
}
