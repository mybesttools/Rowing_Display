#include "ftms_encoder.h"

namespace ftms {

namespace {

// Rower Data (0x2AD1) flags bits, per Bluetooth GATT Specification
// Supplement sec. 3.212.1. Bit 0 means "More Data" - when SET, Stroke Rate
// and Stroke Count are OMITTED from the packet. We always send them, so bit
// 0 must stay clear; getting this backwards (as an earlier version of this
// file did) makes any spec-conformant parser misread every byte after the
// flags field, since it would expect Average Stroke Rate first instead.
constexpr uint16_t kFlagAvgStrokeRate = (1u << 1);
constexpr uint16_t kFlagTotalDistance = (1u << 2);
constexpr uint16_t kFlagInstPace = (1u << 3);
constexpr uint16_t kFlagAvgPace = (1u << 4);
constexpr uint16_t kFlagInstPower = (1u << 5);
constexpr uint16_t kFlagElapsedTime = (1u << 11);

constexpr uint16_t kFlags =
    kFlagAvgStrokeRate | kFlagTotalDistance | kFlagInstPace | kFlagAvgPace | kFlagInstPower | kFlagElapsedTime;

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

size_t encodeIndoorRowerData(const RowerMetrics& m, uint8_t* out, size_t cap) {
    constexpr size_t kSize = 17; // 2+1+2+1+3+2+2+2+2, see field list below.
    if (cap < kSize) {
        return 0;
    }

    uint8_t* p = out;
    putU16(p, kFlags);

    // Stroke Rate (0.5 spm resolution) + Stroke Count - unconditional,
    // since flags bit 0 ("More Data") is clear.
    *p++ = static_cast<uint8_t>(m.strokeRateSpm * 2.0f);
    putU16(p, static_cast<uint16_t>(m.strokeCount & 0xFFFF));

    // Average Stroke Rate: no separate running average tracked yet, so this
    // is the same placeholder simplification as Average Pace below.
    *p++ = static_cast<uint8_t>(m.strokeRateSpm * 2.0f);

    putU24(p, static_cast<uint32_t>(m.distanceMeters));

    const uint16_t pace = static_cast<uint16_t>(m.splitSecPer500m);
    putU16(p, pace); // Instantaneous Pace
    putU16(p, pace); // Average Pace placeholder: same as instantaneous.

    putI16(p, static_cast<int16_t>(m.powerWatts));

    putU16(p, static_cast<uint16_t>(m.elapsedSec));

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
    constexpr uint32_t kElapsedTimeSupported = (1u << 12);
    constexpr uint32_t kPowerMeasurementSupported = (1u << 14);
    constexpr uint32_t kFeatures =
        kCadenceSupported | kTotalDistanceSupported | kPaceSupported | kElapsedTimeSupported |
        kPowerMeasurementSupported;

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

ResultCode resultFor(uint8_t requestOpCode) {
    switch (static_cast<OpCode>(requestOpCode)) {
        case OpCode::RequestControl:
        case OpCode::Reset:
        case OpCode::StartOrResume:
        case OpCode::StopOrPause:
            return ResultCode::Success;
        default:
            return ResultCode::OpCodeNotSupported;
    }
}

} // namespace control_point

} // namespace ftms
