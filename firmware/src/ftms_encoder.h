#pragma once

#include <cstddef>
#include <cstdint>

#include "metrics_model.h"

namespace ftms {

// Encodes an Indoor Rower Data (UUID 0x2AD1) packet.
// Returns packet size in bytes, or 0 if cap is too small.
size_t encodeIndoorRowerData(const RowerMetrics& m, uint8_t* out, size_t cap);

// Encodes the Fitness Machine Feature (UUID 0x2ACC) characteristic value:
// two little-endian uint32 fields (Fitness Machine Features, Target Setting
// Features) as required by the FTMS spec. This is a fixed 8-byte value
// matching the fields encodeIndoorRowerData() actually populates — keep the
// two in sync if the Rower Data field set changes.
// Returns packet size in bytes, or 0 if cap is too small.
size_t encodeFitnessMachineFeature(uint8_t* out, size_t cap);

// Fitness Machine Control Point (UUID 0x2AD9) op codes and result codes
// this firmware understands (FTMS spec Table 4.15 / 4.24). We don't
// actually implement machine control — every op code is acknowledged with
// an indication so clients that write to this characteristic as part of
// their connect handshake don't stall waiting for a response, per FTMS spec
// section 4.16 (every write must be answered with an indication).
namespace control_point {

enum class OpCode : uint8_t {
    RequestControl = 0x00,
    Reset = 0x01,
    StartOrResume = 0x07,
    StopOrPause = 0x08,
    ResponseCode = 0x80,
};

enum class ResultCode : uint8_t {
    Success = 0x01,
    OpCodeNotSupported = 0x02,
    InvalidParameter = 0x03,
    OperationFailed = 0x04,
    ControlNotPermitted = 0x05,
};

// Builds the [0x80, requestOpCode, resultCode] response indication.
// Returns packet size in bytes, or 0 if cap is too small.
size_t encodeResponse(uint8_t requestOpCode, ResultCode result, uint8_t* out, size_t cap);

// Decides the result for a given incoming request op code. RequestControl,
// Reset, StartOrResume and StopOrPause (the FTMS-mandatory op codes) get
// Success with no side effect; anything else gets OpCodeNotSupported.
ResultCode resultFor(uint8_t requestOpCode);

} // namespace control_point

} // namespace ftms
