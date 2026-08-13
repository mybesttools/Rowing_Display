#include "ftms_encoder.h"

namespace ftms {

size_t encodeIndoorRowerData(const RowerMetrics& m, uint8_t* out, size_t cap) {
    if (cap < 18) {
        return 0;
    }

    // Bitfield based on FTMS Indoor Rower Data optional fields.
    // This starter includes stroke rate, stroke count, average stroke rate,
    // total distance, instantaneous pace, average pace, and instantaneous power.
    uint16_t flags = 0;
    flags |= (1u << 0);  // More data is false, reserved usage in some stacks.
    flags |= (1u << 1);  // Avg Stroke Rate present
    flags |= (1u << 2);  // Total Distance present
    flags |= (1u << 3);  // Instantaneous Pace present
    flags |= (1u << 4);  // Average Pace present
    flags |= (1u << 5);  // Instantaneous Power present

    uint8_t* p = out;
    *p++ = static_cast<uint8_t>(flags & 0xFF);
    *p++ = static_cast<uint8_t>((flags >> 8) & 0xFF);

    const uint8_t spm = static_cast<uint8_t>(m.strokeRateSpm * 2.0f); // resolution 0.5 spm
    const uint8_t avgSpm = spm;
    *p++ = spm;
    *p++ = avgSpm;

    const uint16_t strokeCount = static_cast<uint16_t>(m.strokeCount & 0xFFFF);
    *p++ = static_cast<uint8_t>(strokeCount & 0xFF);
    *p++ = static_cast<uint8_t>((strokeCount >> 8) & 0xFF);

    const uint32_t dist = static_cast<uint32_t>(m.distanceMeters);
    *p++ = static_cast<uint8_t>(dist & 0xFF);
    *p++ = static_cast<uint8_t>((dist >> 8) & 0xFF);
    *p++ = static_cast<uint8_t>((dist >> 16) & 0xFF);

    const uint16_t pace = static_cast<uint16_t>(m.splitSecPer500m);
    *p++ = static_cast<uint8_t>(pace & 0xFF);
    *p++ = static_cast<uint8_t>((pace >> 8) & 0xFF);

    // Average pace placeholder: equal to current pace.
    *p++ = static_cast<uint8_t>(pace & 0xFF);
    *p++ = static_cast<uint8_t>((pace >> 8) & 0xFF);

    const int16_t watts = static_cast<int16_t>(m.powerWatts);
    *p++ = static_cast<uint8_t>(watts & 0xFF);
    *p++ = static_cast<uint8_t>((watts >> 8) & 0xFF);

    return static_cast<size_t>(p - out);
}

} // namespace ftms
