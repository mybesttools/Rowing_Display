#include "ble_ftms.h"

#include <NimBLEDevice.h>

#include "ftms_encoder.h"

namespace {

constexpr char FTMS_SERVICE_UUID[] = "1826";
constexpr char FTMS_ROWER_DATA_CHAR_UUID[] = "2AD1";

NimBLECharacteristic* g_rowerChar = nullptr;

} // namespace

void BleFtmsService::begin(const String& deviceName) {
    NimBLEDevice::init(deviceName.c_str());

    NimBLEServer* server = NimBLEDevice::createServer();
    NimBLEService* service = server->createService(FTMS_SERVICE_UUID);

    g_rowerChar = service->createCharacteristic(
        FTMS_ROWER_DATA_CHAR_UUID,
        NIMBLE_PROPERTY::NOTIFY);

    NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
    advertising->addServiceUUID(FTMS_SERVICE_UUID);
    advertising->start();

    ready_ = true;
}

void BleFtmsService::notify(const RowerMetrics& metrics) {
    if (!ready_ || g_rowerChar == nullptr) {
        return;
    }

    uint8_t payload[24] = {0};
    const size_t size = ftms::encodeIndoorRowerData(metrics, payload, sizeof(payload));
    if (size == 0) {
        return;
    }

    g_rowerChar->setValue(payload, size);
    g_rowerChar->notify();
}
