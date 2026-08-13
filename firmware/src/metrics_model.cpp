#include "metrics_model.h"

#include "config.h"

namespace {

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

} // namespace

void MetricsModel::onPulseMicros(uint32_t nowMicros, uint32_t nowMs) {
    if (sessionStartMs_ == 0) {
        sessionStartMs_ = nowMs;
        lastTickMs_ = sessionStartMs_;
    }

    if (lastPulseMicros_ != 0) {
        const uint32_t dt = nowMicros - lastPulseMicros_;
        if (dt > 0) {
            const float revPerSec = (1000000.0f / static_cast<float>(dt)) / cfg::MAGNETS_PER_REV;
            const float rpmInstant = revPerSec * 60.0f;
            rpmSmoothed_ = lerp(rpmSmoothed_, rpmInstant, cfg::RPM_ALPHA);
            metrics_.rpm = rpmSmoothed_;
        }
    }

    lastPulseMicros_ = nowMicros;
    lastStrokeMs_ = nowMs;
}

void MetricsModel::tick(uint32_t nowMs) {
    if (sessionStartMs_ == 0) {
        return;
    }

    if (lastTickMs_ == 0) {
        lastTickMs_ = nowMs;
        return;
    }

    const uint32_t dtMs = nowMs - lastTickMs_;
    lastTickMs_ = nowMs;

    updateStrokeState(nowMs);

    metrics_.elapsedSec = (nowMs - sessionStartMs_) / 1000;

    // Integrate distance from smoothed rpm.
    const float revPerSec = metrics_.rpm / 60.0f;
    metrics_.distanceMeters += revPerSec * cfg::METERS_PER_REV * (static_cast<float>(dtMs) / 1000.0f);

    // Very rough starter model: map rpm to stroke rate and power.
    metrics_.strokeRateSpm = metrics_.rpm * 0.42f;
    if (metrics_.strokeRateSpm < 5.0f) {
        metrics_.strokeRateSpm = 0.0f;
    }

    metrics_.powerWatts = metrics_.rpm * metrics_.rpm * 0.045f;

    // Split from current speed estimate.
    const float metersPerSec = revPerSec * cfg::METERS_PER_REV;
    if (metersPerSec > 0.05f) {
        metrics_.splitSecPer500m = 500.0f / metersPerSec;
    } else {
        metrics_.splitSecPer500m = 0.0f;
    }
}

void MetricsModel::updateStrokeState(uint32_t nowMs) {
    const bool active = (nowMs - lastStrokeMs_) < cfg::STROKE_IDLE_TIMEOUT_MS;

    if (active && !inStroke_) {
        inStroke_ = true;
        metrics_.strokeCount += 1;
    }

    if (!active) {
        inStroke_ = false;
        metrics_.rpm *= 0.96f;
        if (metrics_.rpm < 1.0f) {
            metrics_.rpm = 0.0f;
            rpmSmoothed_ = 0.0f;
        }
    }
}
