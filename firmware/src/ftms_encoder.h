#pragma once

#include <cstddef>
#include <cstdint>

#include "metrics_model.h"

namespace ftms {

// Indoor Rower Data (UUID 0x2AD1) goes out as two notifications, so each
// fits the 20-byte payload of the default 23-byte ATT MTU (clients don't
// all negotiate a bigger one). Part 0 has "More Data" set and carries the
// averages, distance, pace and power; part 1 carries stroke rate/count,
// energy and elapsed time. Send part 0 first.
constexpr int kRowerDataParts = 2;

// Returns packet size in bytes, or 0 if cap is too small or part is invalid.
size_t encodeIndoorRowerData(const RowerMetrics& m, int part, uint8_t* out, size_t cap);

// Encodes the Fitness Machine Feature (UUID 0x2ACC) characteristic value:
// two little-endian uint32 fields (Fitness Machine Features, Target Setting
// Features) as required by the FTMS spec. This is a fixed 8-byte value
// matching the fields encodeIndoorRowerData() actually populates — keep the
// two in sync if the Rower Data field set changes.
// Returns packet size in bytes, or 0 if cap is too small.
size_t encodeFitnessMachineFeature(uint8_t* out, size_t cap);

// Fitness Machine Control Point (UUID 0x2AD9) op codes and result codes
// this firmware understands (FTMS spec Table 4.15 / 4.24). Every write is
// answered with an indication (FTMS spec section 4.16); clients that write
// here as part of their connect handshake stall otherwise. Reset, start and
// stop/pause are carried out on the session (main.cpp).
namespace control_point {

enum class OpCode : uint8_t {
    RequestControl = 0x00,
    Reset = 0x01,
    StartOrResume = 0x07,
    StopOrPause = 0x08,
    ResponseCode = 0x80,
};

// Parameter of StopOrPause.
constexpr uint8_t kStop = 0x01;
constexpr uint8_t kPause = 0x02;

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

// Validates a control point write (data[0] is the op code). Success for
// RequestControl, Reset, StartOrResume and StopOrPause with a stop/pause
// parameter; InvalidParameter for a StopOrPause without one;
// OpCodeNotSupported for anything else.
ResultCode resultFor(const uint8_t* data, size_t len);

} // namespace control_point

// Fitness Machine Status (UUID 0x2ADA) notifications for session changes,
// whether the app or the display caused them.
namespace status {

enum class Event : uint8_t {
    Reset = 0x01,
    StoppedOrPausedByUser = 0x02, // parameter: control_point::kStop / kPause
    StartedOrResumedByUser = 0x04,
};

// Returns packet size in bytes (1, or 2 with a parameter), or 0 if cap is
// too small. Pass parameter 0 for events without one.
size_t encode(Event event, uint8_t parameter, uint8_t* out, size_t cap);

} // namespace status

} // namespace ftms
