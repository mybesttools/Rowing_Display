#pragma once

#include <cstdint>

struct RowerMetrics {
    float rpm = 0.0f;
    float strokeRateSpm = 0.0f;
    uint32_t strokeCount = 0;
    float powerWatts = 0.0f;
    float splitSecPer500m = 0.0f;
    float distanceMeters = 0.0f;
    uint32_t elapsedSec = 0;
};

class MetricsModel {
public:
    void onPulseMicros(uint32_t nowMicros, uint32_t nowMs);
    void tick(uint32_t nowMs);

    const RowerMetrics& metrics() const { return metrics_; }

private:
    void updateStrokeState(uint32_t nowMs);

    RowerMetrics metrics_{};

    uint32_t lastPulseMicros_ = 0;
    uint32_t lastTickMs_ = 0;
    uint32_t sessionStartMs_ = 0;
    uint32_t lastStrokeMs_ = 0;

    float rpmSmoothed_ = 0.0f;
    bool inStroke_ = false;
};
