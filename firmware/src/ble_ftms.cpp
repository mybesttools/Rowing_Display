#include "ble_ftms.h"

#include <NimBLEDevice.h>

#include "ftms_encoder.h"

namespace {

constexpr char FTMS_SERVICE_UUID[] = "1826";
constexpr char FTMS_ROWER_DATA_CHAR_UUID[] = "2AD1";
constexpr char FTMS_FEATURE_CHAR_UUID[] = "2ACC";
constexpr char FTMS_CONTROL_POINT_CHAR_UUID[] = "2AD9";
constexpr char FTMS_STATUS_CHAR_UUID[] = "2ADA";

NimBLECharacteristic* g_rowerChar = nullptr;
NimBLECharacteristic* g_controlPointChar = nullptr;
NimBLECharacteristic* g_statusChar = nullptr;
QueueHandle_t g_commands = nullptr;

BleFtmsService::Command commandFor(const uint8_t* data) {
    using ftms::control_point::OpCode;
    switch (static_cast<OpCode>(data[0])) {
        case OpCode::Reset:
            return BleFtmsService::Command::Reset;
        case OpCode::StartOrResume:
            return BleFtmsService::Command::Start;
        case OpCode::StopOrPause:
            return data[1] == ftms::control_point::kStop ? BleFtmsService::Command::Stop
                                                         : BleFtmsService::Command::Pause;
        default:
            return BleFtmsService::Command::None;
    }
}

// Fitness Machine Control Point handler. Every write must get an indication
// back (FTMS spec sec. 4.16) or clients that write here as part of their
// connect handshake (as EXR does) stall waiting for a response. Accepted
// session commands are queued for the main loop (takeCommand()).
class ControlPointCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& /*connInfo*/) override {
        const NimBLEAttValue value = characteristic->getValue();
        if (value.length() == 0) {
            return;
        }

        const uint8_t requestOpCode = value.data()[0];
        const ftms::control_point::ResultCode result = ftms::control_point::resultFor(value.data(), value.length());
        if (result == ftms::control_point::ResultCode::Success) {
            const BleFtmsService::Command command = commandFor(value.data());
            if (command != BleFtmsService::Command::None && g_commands != nullptr) {
                xQueueSend(g_commands, &command, 0);
            }
        }

        uint8_t response[3] = {0};
        const size_t size = ftms::control_point::encodeResponse(requestOpCode, result, response, sizeof(response));
        if (size == 0) {
            return;
        }

        characteristic->indicate(response, size);
    }
};

ControlPointCallbacks g_controlPointCallbacks;

} // namespace

void BleFtmsService::begin(const String& deviceName) {
    g_commands = xQueueCreate(8, sizeof(Command));
    NimBLEDevice::init(deviceName.c_str());

    NimBLEServer* server = NimBLEDevice::createServer();
    // NimBLE defaults this to off: without it, once any client connects and
    // then disconnects (normal for app backgrounding, dropped connections,
    // ending a workout), the device stops advertising entirely and can
    // never be reconnected to without a power cycle. Confirmed live: a
    // client that connected once made the device permanently
    // undiscoverable on the next scan until this was added.
    server->advertiseOnDisconnect(true);
    NimBLEService* service = server->createService(FTMS_SERVICE_UUID);

    g_rowerChar = service->createCharacteristic(FTMS_ROWER_DATA_CHAR_UUID, NIMBLE_PROPERTY::NOTIFY);

    // Mandatory per FTMS spec — many clients read this during service
    // discovery to decide whether the device is a usable FTMS machine at
    // all before subscribing to Rower Data.
    NimBLECharacteristic* featureChar = service->createCharacteristic(FTMS_FEATURE_CHAR_UUID, NIMBLE_PROPERTY::READ);
    uint8_t featurePayload[8] = {0};
    const size_t featureSize = ftms::encodeFitnessMachineFeature(featurePayload, sizeof(featurePayload));
    featureChar->setValue(featurePayload, featureSize);

    // Control Point: the spec marks this Encryption-required, but requiring
    // bonding/pairing here risks breaking clients that don't expect a
    // pairing prompt for a simple data device. Plain WRITE + INDICATE is a
    // deliberate deviation favoring broad real-world compatibility over
    // spec purity, matching how EXR-verified open-source FTMS rowers do it.
    g_controlPointChar = service->createCharacteristic(
        FTMS_CONTROL_POINT_CHAR_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::INDICATE
    );
    g_controlPointChar->setCallbacks(&g_controlPointCallbacks);

    // Status: mandatory once Control Point is present; reports session
    // reset/start/stop/pause (notifyStatus()).
    g_statusChar = service->createCharacteristic(FTMS_STATUS_CHAR_UUID, NIMBLE_PROPERTY::NOTIFY);

    NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
    advertising->addServiceUUID(FTMS_SERVICE_UUID);

    // Service Data AD: lets scanners identify this as an available Rower
    // before connecting. Byte 0 flags bit 0 = Fitness Machine Available;
    // bytes 1-2 are the Fitness Machine Type bitmask (bit 4 = Rower
    // Supported), little-endian.
    const uint8_t serviceData[] = {0x01, 0x10, 0x00};
    advertising->setServiceData(NimBLEUUID(FTMS_SERVICE_UUID), serviceData, sizeof(serviceData));

    // The advertisement is full (flags, service UUID, service data), so the
    // name goes in the scan response; without it apps list the rower as
    // unnamed.
    NimBLEAdvertisementData scanResponse;
    scanResponse.setName(deviceName.c_str());
    advertising->setScanResponseData(scanResponse);
    advertising->enableScanResponse(true);

    advertising->start();

    ready_ = true;
}

void BleFtmsService::notify(const RowerMetrics& metrics) {
    if (!ready_ || g_rowerChar == nullptr) {
        return;
    }

    uint8_t payload[20] = {0};
    for (int part = 0; part < ftms::kRowerDataParts; part++) {
        const size_t size = ftms::encodeIndoorRowerData(metrics, part, payload, sizeof(payload));
        if (size == 0) {
            return;
        }
        // Sends this buffer directly: setValue() + notify() twice in a row
        // occasionally delivered a packet with a corrupted first byte.
        g_rowerChar->notify(payload, size);
    }
}

BleFtmsService::Command BleFtmsService::takeCommand() {
    Command command = Command::None;
    if (g_commands != nullptr) {
        xQueueReceive(g_commands, &command, 0);
    }
    return command;
}

void BleFtmsService::notifyStatus(ftms::status::Event event, uint8_t parameter) {
    if (!ready_ || g_statusChar == nullptr) {
        return;
    }
    uint8_t payload[2] = {0};
    const size_t size = ftms::status::encode(event, parameter, payload, sizeof(payload));
    if (size == 0) {
        return;
    }
    g_statusChar->notify(payload, size);
}
