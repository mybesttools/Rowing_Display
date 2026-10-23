#include "ftms_encoder.h"

namespace ftms {

namespace {

// Rower Data (0x2AD1) flags bits, per Bluetooth GATT Specification
// Supplement sec. 3.212.1. Bit 0 means "More Data" - when SET, Stroke Rate
// and Stroke Count are OMITTED from the packet (getting this backwards, as
// an earlier version of this file did, makes any spec-conformant parser
// misread every byte after the flags field). Part 0 sets it, part 1 clears
// it and carries them.
constexpr uint16_t kFlagMoreData = (1u << 0);
constexpr uint16_t kFlagAvgStrokeRate = (1u << 1);
constexpr uint16_t kFlagTotalDistance = (1u << 2);
constexpr uint16_t kFlagInstPace = (1u << 3);
constexpr uint16_t kFlagAvgPace = (1u << 4);
constexpr uint16_t kFlagInstPower = (1u << 5);
constexpr uint16_t kFlagExpendedEnergy = (1u << 8);
constexpr uint16_t kFlagElapsedTime = (1u << 11);

constexpr uint16_t kPart0Flags =
    kFlagMoreData | kFlagAvgStrokeRate | kFlagTotalDistance | kFlagInstPace | kFlagAvgPace | kFlagInstPower;
constexpr uint16_t kPart1Flags = kFlagExpendedEnergy | kFlagElapsedTime;

uint16_t clampU16(float v) {
    return v <= 0.0f ? 0 : (v >= 65535.0f ? 65535 : static_cast<uint16_t>(v));
}

uint8_t clampU8(float v) {
    return v <= 0.0f ? 0 : (v >= 255.0f ? 255 : static_cast<uint8_t>(v));
}

void putU16(uint8_t*& p, uint16_t v) {
    *p++ = static_cast<uint8_t>(v & 0xFF);
    *p++ = static_cast<uint8_t>((v >> 8) & 0xFF);
}

void putI16(uint8_t*& p, int16_t v) {
    putU16(p, static_cast<uint16_t>(v));
}

void putU24(uint8_t*& p, uint32_t v) {
    *p++ = static_cast<uint8_t>(v & 0xFF);
    *p++ = static_cast<uint8_t>((v >> 8) & 0xFF);
    *p++ = static_cast<uint8_t>((v >> 16) & 0xFF);
}

void putU32(uint8_t*& p, uint32_t v) {
    *p++ = static_cast<uint8_t>(v & 0xFF);
    *p++ = static_cast<uint8_t>((v >> 8) & 0xFF);
    *p++ = static_cast<uint8_t>((v >> 16) & 0xFF);
    *p++ = static_cast<uint8_t>((v >> 24) & 0xFF);
}

} // namespace

size_t encodeIndoorRowerData(const RowerMetrics& m, int part, uint8_t* out, size_t cap) {
    constexpr size_t kSize = 12; // both parts, see the field lists below
    if (cap < kSize || part < 0 || part >= kRowerDataParts) {
        return 0;
    }

    uint8_t* p = out;
    if (part == 0) {
        putU16(p, kPart0Flags);
        *p++ = clampU8(m.avgStrokeRateSpm * 2.0f); // 0.5 spm resolution
        putU24(p, static_cast<uint32_t>(m.distanceMeters));
        putU16(p, clampU16(m.splitSecPer500m));    // Instantaneous Pace
        putU16(p, clampU16(m.avgSplitSecPer500m)); // Average Pace
        putI16(p, static_cast<int16_t>(m.powerWatts));
    } else {
        putU16(p, kPart1Flags);
        // Stroke Rate (0.5 spm resolution) + Stroke Count: present because
        // "More Data" is clear.
        *p++ = clampU8(m.strokeRateSpm * 2.0f);
        putU16(p, static_cast<uint16_t>(m.strokeCount & 0xFFFF));
        putU16(p, clampU16(m.energyKcal));               // Total Energy
        putU16(p, clampU16(m.energyPerHourKcal));        // Energy Per Hour
        *p++ = clampU8(m.energyPerHourKcal / 60.0f);     // Energy Per Minute
        putU16(p, static_cast<uint16_t>(m.elapsedSec));
    }
    return static_cast<size_t>(p - out);
}

size_t encodeFitnessMachineFeature(uint8_t* out, size_t cap) {
    constexpr size_t kSize = 8;
    if (cap < kSize) {
        return 0;
    }

    // Fitness Machine Features bits (FTMS spec Table 4.2), matching what
    // encodeIndoorRowerData() actually populates above.
    constexpr uint32_t kCadenceSupported = (1u << 1);
    constexpr uint32_t kTotalDistanceSupported = (1u << 2);
    constexpr uint32_t kPaceSupported = (1u << 5);
    constexpr uint32_t kExpendedEnergySupported = (1u << 9);
    constexpr uint32_t kElapsedTimeSupported = (1u << 12);
    constexpr uint32_t kPowerMeasurementSupported = (1u << 14);
    constexpr uint32_t kFeatures =
        kCadenceSupported | kTotalDistanceSupported | kPaceSupported | kExpendedEnergySupported |
        kElapsedTimeSupported | kPowerMeasurementSupported;

    // Target Setting Features: left at 0 since we don't implement any
    // Control Point "Set Target" op codes. Setting a bit here without
    // implementing the matching op code would make a client attempt a
    // control action that then fails, which is worse than not offering it.
    constexpr uint32_t kTargetSettingFeatures = 0;

    uint8_t* p = out;
    putU32(p, kFeatures);
    putU32(p, kTargetSettingFeatures);
    return static_cast<size_t>(p - out);
}

namespace control_point {

size_t encodeResponse(uint8_t requestOpCode, ResultCode result, uint8_t* out, size_t cap) {
    constexpr size_t kSize = 3;
    if (cap < kSize) {
        return 0;
    }
    out[0] = static_cast<uint8_t>(OpCode::ResponseCode);
    out[1] = requestOpCode;
    out[2] = static_cast<uint8_t>(result);
    return kSize;
}

ResultCode resultFor(const uint8_t* data, size_t len) {
    if (data == nullptr || len == 0) {
        return ResultCode::InvalidParameter;
    }
    switch (static_cast<OpCode>(data[0])) {
        case OpCode::RequestControl:
        case OpCode::Reset:
        case OpCode::StartOrResume:
            return ResultCode::Success;
        case OpCode::StopOrPause:
            return len >= 2 && (data[1] == kStop || data[1] == kPause) ? ResultCode::Success
                                                                        : ResultCode::InvalidParameter;
        default:
            return ResultCode::OpCodeNotSupported;
    }
}

} // namespace control_point

namespace status {

size_t encode(Event event, uint8_t parameter, uint8_t* out, size_t cap) {
    const size_t size = event == Event::StoppedOrPausedByUser ? 2 : 1;
    if (cap < size) {
        return 0;
    }
    out[0] = static_cast<uint8_t>(event);
    if (size == 2) {
        out[1] = parameter;
    }
    return size;
}

} // namespace status

} // namespace ftms
