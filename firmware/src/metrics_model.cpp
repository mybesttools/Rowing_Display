#include "metrics_model.h"

#include <algorithm>
#include <cmath>

#include "config.h"

namespace {

constexpr float kPi = 3.14159265358979323846f;

// Dead zone around "period unchanged" so near-equal consecutive pulse
// periods don't flip the drive/recovery candidate on noise alone.
constexpr float kPhaseHysteresis = 0.03f;

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

} // namespace

void MetricsModel::reset() {
    const float keepDragFactor = dragFactor_;

    metrics_ = RowerMetrics{};
    metrics_.dragFactor = keepDragFactor;

    phase_ = RowerPhase::Idle;
    phaseStartMs_ = 0;

    lastPulseMicros_ = 0;
    lastPulseMs_ = 0;
    lastDtMicros_ = 0;
    lastStrokeStartMs_ = 0;

    sessionStartMs_ = 0;
    lastTickMs_ = 0;
    paused_ = false;
    pauseStartMs_ = 0;

    lastDrivePhaseDurationMs_ = 0;
    lastRecoveryPhaseDurationMs_ = 0;

    strokeIntervalMs_ = 0;
    strokeSpeedMps_ = 0.0f;

    omegaSmoothed_ = 0.0f;
    dragFactor_ = keepDragFactor;

    recoveryCount_ = 0;
    recoveryStartMicros_ = 0;
}

void MetricsModel::pause(uint32_t nowMs) {
    if (paused_) {
        return;
    }
    paused_ = true;
    pauseStartMs_ = nowMs;

    // Drop live readings and pulse timing, so rowing after resume() starts
    // from rest instead of computing an interval across the pause.
    phase_ = RowerPhase::Idle;
    lastPulseMicros_ = 0;
    lastDtMicros_ = 0;
    lastStrokeStartMs_ = 0;
    strokeIntervalMs_ = 0;
    strokeSpeedMps_ = 0.0f;
    omegaSmoothed_ = 0.0f;
    recoveryCount_ = 0;
    metrics_.phase = RowerPhase::Idle;
    metrics_.phaseProgress = 0.0f;
    metrics_.rpm = 0.0f;
    metrics_.strokeRateSpm = 0.0f;
    metrics_.powerWatts = 0.0f;
    metrics_.splitSecPer500m = 0.0f;
    metrics_.energyPerHourKcal = 0.0f;
    metrics_.paused = true;
}

void MetricsModel::resume(uint32_t nowMs) {
    if (!paused_) {
        return;
    }
    paused_ = false;
    metrics_.paused = false;
    if (sessionStartMs_ != 0) {
        sessionStartMs_ += nowMs - pauseStartMs_;
        lastTickMs_ = nowMs;
    }
}

void MetricsModel::setParams(const ModelParams& params) {
    params_ = params;
    dragFactor_ = 0.0f;
    metrics_.dragFactor = 0.0f;
}

void MetricsModel::onPulseMicros(uint32_t nowMicros, uint32_t nowMs) {
    if (paused_) {
        return;
    }
    if (sessionStartMs_ == 0) {
        sessionStartMs_ = nowMs;
        lastTickMs_ = sessionStartMs_;
    }

    if (params_.mode == cfg::PulseMode::Stroke) {
        onStrokePulse(nowMs);
        return;
    }

    if (lastPulseMicros_ != 0) {
        const uint32_t dtMicros = nowMicros - lastPulseMicros_;
        if (dtMicros < params_.machine.minPulseGapMs * 1000u) {
            // Debounce: ignore this edge and keep waiting from the last
            // accepted pulse, instead of resetting the reference point.
            return;
        }

        const float dtSeconds = static_cast<float>(dtMicros) / 1000000.0f;
        const float omegaInstant = (2.0f * kPi) / (dtSeconds * params_.machine.pulsesPerRev);
        omegaSmoothed_ = lerp(omegaSmoothed_, omegaInstant, cfg::OMEGA_ALPHA);

        updatePhase(nowMicros, nowMs, dtMicros);

        if (phase_ == RowerPhase::Recovery && recoveryCount_ < kMaxRecoveryPoints && omegaInstant > 0.0f) {
            // Raw (unsmoothed) sample for the drag-factor regression: fit
            // the actual physical data, not our own smoothing artifact.
            const float t = static_cast<float>(nowMicros - recoveryStartMicros_) / 1000000.0f;
            recoveryT_[recoveryCount_] = t;
            recoveryInvOmega_[recoveryCount_] = 1.0f / omegaInstant;
            recoveryCount_++;
        }

        lastDtMicros_ = dtMicros;
    } else {
        phaseStartMs_ = nowMs;
    }

    lastPulseMicros_ = nowMicros;
    lastPulseMs_ = nowMs;
}

void MetricsModel::updatePhase(uint32_t nowMicros, uint32_t nowMs, uint32_t dtMicros) {
    if (lastDtMicros_ == 0) {
        // Only one interval known so far; nothing to compare yet.
        return;
    }

    // Shorter period than last time -> flywheel speeding up -> drive.
    // Longer period -> slowing down -> recovery.
    const float ratio = static_cast<float>(dtMicros) / static_cast<float>(lastDtMicros_);
    RowerPhase candidate = phase_;
    if (ratio < 1.0f - kPhaseHysteresis) {
        candidate = RowerPhase::Drive;
    } else if (ratio > 1.0f + kPhaseHysteresis) {
        candidate = RowerPhase::Recovery;
    }

    if (phase_ == RowerPhase::Idle) {
        // Steady manual pulses (or the first intervals after idle) often sit
        // inside the hysteresis band; treat that as Drive so rowing visibly
        // starts instead of staying on the idle water loop forever.
        if (candidate == RowerPhase::Idle) {
            candidate = RowerPhase::Drive;
        }
        enterPhase(candidate, nowMicros, nowMs);
        return;
    }

    if (candidate == phase_) {
        return;
    }

    // Debounce: require the current phase to have held for a minimum time
    // before accepting a flip, so a single noisy pulse can't flip it.
    const uint32_t minHoldMs = (phase_ == RowerPhase::Drive) ? params_.machine.minDriveMs : params_.machine.minRecoveryMs;
    if (nowMs - phaseStartMs_ < minHoldMs) {
        return;
    }

    // The phase we're leaving just completed - record how long it took as
    // the "expected" duration for the next phase of that type, so
    // phaseProgress has something to normalize against.
    const uint32_t finishedPhaseDurationMs = nowMs - phaseStartMs_;
    if (phase_ == RowerPhase::Drive) {
        lastDrivePhaseDurationMs_ = finishedPhaseDurationMs;
    } else if (phase_ == RowerPhase::Recovery) {
        lastRecoveryPhaseDurationMs_ = finishedPhaseDurationMs;
    }

    if (phase_ == RowerPhase::Recovery) {
        onRecoveryEnded();
    }

    enterPhase(candidate, nowMicros, nowMs);

    if (candidate == RowerPhase::Drive) {
        // A new drive phase starting is the "catch" - one stroke completed.
        metrics_.strokeCount += 1;
        if (lastStrokeStartMs_ != 0) {
            const uint32_t strokeCycleMs = nowMs - lastStrokeStartMs_;
            if (strokeCycleMs > 0) {
                const float spmInstant = 60000.0f / static_cast<float>(strokeCycleMs);
                metrics_.strokeRateSpm = lerp(metrics_.strokeRateSpm, spmInstant, cfg::OMEGA_ALPHA);
            }
        }
        lastStrokeStartMs_ = nowMs;
    }
}

void MetricsModel::enterPhase(RowerPhase phase, uint32_t nowMicros, uint32_t nowMs) {
    phase_ = phase;
    metrics_.phase = phase;
    phaseStartMs_ = nowMs;
    if (phase == RowerPhase::Recovery) {
        recoveryCount_ = 0;
        recoveryStartMicros_ = nowMicros;
    }
}

void MetricsModel::onRecoveryEnded() {
    if (recoveryCount_ < cfg::MIN_REGRESSION_POINTS) {
        return;
    }

    float k = 0.0f;
    float r2 = 0.0f;
    if (!fitDragFactor(k, r2)) {
        return;
    }
    if (r2 < cfg::MIN_FIT_R2) {
        return;
    }
    if (k < params_.machine.dragFactorMin || k > params_.machine.dragFactorMax) {
        return;
    }

    // Drag is physically near-constant stroke to stroke, so blend rather
    // than snap to each new fit.
    dragFactor_ = (dragFactor_ <= 0.0f) ? k : lerp(dragFactor_, k, cfg::DRAG_FACTOR_ALPHA);
}

bool MetricsModel::fitDragFactor(float& outK, float& outR2) const {
    const int n = recoveryCount_;
    if (n < 2) {
        return false;
    }

    // Ordinary least squares: fit 1/omega (y) against elapsed recovery time
    // (x). The physics (I*domega/dt = -k*omega^2, integrated) predicts this
    // is exactly linear, with slope = k / I.
    float sumX = 0.0f;
    float sumY = 0.0f;
    float sumXY = 0.0f;
    float sumXX = 0.0f;
    for (int i = 0; i < n; ++i) {
        sumX += recoveryT_[i];
        sumY += recoveryInvOmega_[i];
        sumXY += recoveryT_[i] * recoveryInvOmega_[i];
        sumXX += recoveryT_[i] * recoveryT_[i];
    }

    const float nf = static_cast<float>(n);
    const float denom = nf * sumXX - sumX * sumX;
    if (fabsf(denom) < 1e-9f) {
        return false;
    }

    const float slope = (nf * sumXY - sumX * sumY) / denom;
    const float intercept = (sumY - slope * sumX) / nf;

    const float meanY = sumY / nf;
    float ssRes = 0.0f;
    float ssTot = 0.0f;
    for (int i = 0; i < n; ++i) {
        const float predicted = slope * recoveryT_[i] + intercept;
        const float residual = recoveryInvOmega_[i] - predicted;
        ssRes += residual * residual;
        const float diffFromMean = recoveryInvOmega_[i] - meanY;
        ssTot += diffFromMean * diffFromMean;
    }

    outR2 = (ssTot > 1e-9f) ? (1.0f - ssRes / ssTot) : 0.0f;
    outK = slope * params_.machine.flywheelInertia;
    return true;
}

void MetricsModel::decayIfIdle(uint32_t nowMs) {
    if (lastPulseMs_ == 0) {
        return;
    }
    if (nowMs - lastPulseMs_ < cfg::STROKE_IDLE_TIMEOUT_MS) {
        return;
    }

    if (phase_ != RowerPhase::Idle) {
        phase_ = RowerPhase::Idle;
        metrics_.phase = RowerPhase::Idle;
        metrics_.phaseProgress = 0.0f;
        recoveryCount_ = 0;
    }

    omegaSmoothed_ *= 0.9f;
    if (omegaSmoothed_ < 0.05f) {
        omegaSmoothed_ = 0.0f;
    }

    metrics_.strokeRateSpm *= 0.9f;
    if (metrics_.strokeRateSpm < 0.5f) {
        metrics_.strokeRateSpm = 0.0f;
    }
}

void MetricsModel::recomputeDerivedMetrics(float dtSeconds) {
    metrics_.rpm = omegaSmoothed_ * 60.0f / (2.0f * kPi);
    metrics_.dragFactor = dragFactor_;

    // Use the seeded placeholder until the first real fit lands, so power
    // and distance aren't pinned at zero for the whole first stroke.
    const float k = (dragFactor_ > 0.0f) ? dragFactor_ : params_.machine.initialDragFactor;

    // P = k * omega^3 (see metrics_model.h doc comment for derivation and
    // attribution).
    const float omega3 = omegaSmoothed_ * omegaSmoothed_ * omegaSmoothed_;
    metrics_.powerWatts = k * omega3;

    // v = (k / C)^(1/3) * omega — cube-root scaling from flywheel drag to a
    // reference "boat" speed, same family Concept2-style ergometers use.
    const float metersPerSec = cbrtf(k / params_.machine.boatConstant) * omegaSmoothed_;
    metrics_.distanceMeters += metersPerSec * dtSeconds;

    metrics_.splitSecPer500m = (metersPerSec > 0.05f) ? (500.0f / metersPerSec) : 0.0f;
}

void MetricsModel::updatePhaseProgress(uint32_t nowMs) {
    uint32_t expectedMs = 0;
    if (phase_ == RowerPhase::Drive) {
        expectedMs = lastDrivePhaseDurationMs_;
    } else if (phase_ == RowerPhase::Recovery) {
        expectedMs = lastRecoveryPhaseDurationMs_;
    }

    if (phase_ == RowerPhase::Idle || expectedMs == 0) {
        metrics_.phaseProgress = 0.0f;
        return;
    }

    const uint32_t elapsedMs = nowMs - phaseStartMs_;
    float progress = static_cast<float>(elapsedMs) / static_cast<float>(expectedMs);
    if (progress > 1.0f) {
        progress = 1.0f;
    }
    metrics_.phaseProgress = progress;
}

void MetricsModel::onStrokePulse(uint32_t nowMs) {
    if (lastStrokeStartMs_ != 0) {
        const uint32_t intervalMs = nowMs - lastStrokeStartMs_;
        if (intervalMs < cfg::MIN_STROKE_INTERVAL_MS) {
            return;
        }
        // Only a back-to-back stroke gives a meaningful rate; the first
        // pulse after a pause just restarts rowing.
        if (phase_ != RowerPhase::Idle) {
            strokeIntervalMs_ = intervalMs;
            const float spmInstant = 60000.0f / static_cast<float>(intervalMs);
            metrics_.strokeRateSpm = (metrics_.strokeRateSpm <= 0.0f) ? spmInstant : lerp(metrics_.strokeRateSpm, spmInstant, 0.5f);
            strokeSpeedMps_ = std::min(cfg::METERS_PER_STROKE * 1000.0f / static_cast<float>(intervalMs), cfg::MAX_BOAT_SPEED_MPS);
        }
    }

    metrics_.strokeCount += 1;
    const float strokeMeters = strokeIntervalMs_ != 0
                                   ? std::min(cfg::METERS_PER_STROKE, cfg::MAX_BOAT_SPEED_MPS * static_cast<float>(strokeIntervalMs_) / 1000.0f)
                                   : cfg::METERS_PER_STROKE;
    metrics_.distanceMeters += strokeMeters;
    lastStrokeStartMs_ = nowMs;
    lastPulseMs_ = nowMs;
    enterPhase(RowerPhase::Drive, 0, nowMs);
}

void MetricsModel::tickStrokeMode(uint32_t nowMs) {
    if (lastStrokeStartMs_ == 0) {
        return;
    }

    // Replay the stroke cycle between pulses: drive for DRIVE_FRACTION of
    // the last interval, then recovery, holding the catch pose at the end
    // until the next pulse. Go idle once a stroke is clearly overdue.
    const uint32_t cycleMs = strokeIntervalMs_ != 0 ? strokeIntervalMs_ : cfg::FIRST_STROKE_INTERVAL_MS;
    const uint32_t driveMs = static_cast<uint32_t>(static_cast<float>(cycleMs) * cfg::DRIVE_FRACTION);
    const uint32_t idleAfterMs = std::max(cfg::STROKE_IDLE_TIMEOUT_MS, cycleMs * 3 / 2);
    const uint32_t sinceMs = nowMs - lastStrokeStartMs_;

    if (sinceMs >= idleAfterMs) {
        phase_ = RowerPhase::Idle;
        metrics_.phaseProgress = 0.0f;
        strokeIntervalMs_ = 0;
        metrics_.strokeRateSpm *= 0.9f;
        if (metrics_.strokeRateSpm < 0.5f) {
            metrics_.strokeRateSpm = 0.0f;
        }
        strokeSpeedMps_ *= 0.9f;
        if (strokeSpeedMps_ < 0.05f) {
            strokeSpeedMps_ = 0.0f;
        }
    } else if (sinceMs < driveMs) {
        phase_ = RowerPhase::Drive;
        metrics_.phaseProgress = static_cast<float>(sinceMs) / static_cast<float>(driveMs);
    } else {
        phase_ = RowerPhase::Recovery;
        metrics_.phaseProgress = std::min(1.0f, static_cast<float>(sinceMs - driveMs) / static_cast<float>(cycleMs - driveMs));
    }
    metrics_.phase = phase_;

    // Concept2's pace/power relation: P = 2.80 * v^3.
    metrics_.rpm = 0.0f;
    metrics_.dragFactor = 0.0f;
    metrics_.powerWatts = 2.80f * strokeSpeedMps_ * strokeSpeedMps_ * strokeSpeedMps_;
    metrics_.splitSecPer500m = (strokeSpeedMps_ > 0.05f) ? (500.0f / strokeSpeedMps_) : 0.0f;
}

void MetricsModel::updateSessionTotals(uint32_t nowMs, float dtSeconds) {
    const float elapsedSec = static_cast<float>(nowMs - sessionStartMs_) / 1000.0f;
    if (elapsedSec > 0.0f) {
        metrics_.avgStrokeRateSpm = static_cast<float>(metrics_.strokeCount) * 60.0f / elapsedSec;
        metrics_.avgSplitSecPer500m = metrics_.distanceMeters > 1.0f ? 500.0f * elapsedSec / metrics_.distanceMeters : 0.0f;
    }

    if (phase_ == RowerPhase::Idle) {
        metrics_.energyPerHourKcal = 0.0f;
        return;
    }
    metrics_.energyPerHourKcal = 4.0f * 0.8604f * metrics_.powerWatts + 300.0f;
    metrics_.energyKcal += metrics_.energyPerHourKcal * dtSeconds / 3600.0f;
}

void MetricsModel::tick(uint32_t nowMs) {
    if (sessionStartMs_ == 0 || paused_) {
        return;
    }

    if (lastTickMs_ == 0) {
        lastTickMs_ = nowMs;
        return;
    }

    const uint32_t dtMs = nowMs - lastTickMs_;
    lastTickMs_ = nowMs;

    if (params_.mode == cfg::PulseMode::Stroke) {
        metrics_.elapsedSec = (nowMs - sessionStartMs_) / 1000;
        tickStrokeMode(nowMs);
        updateSessionTotals(nowMs, static_cast<float>(dtMs) / 1000.0f);
        return;
    }

    decayIfIdle(nowMs);
    updatePhaseProgress(nowMs);

    metrics_.elapsedSec = (nowMs - sessionStartMs_) / 1000;

    recomputeDerivedMetrics(static_cast<float>(dtMs) / 1000.0f);
    updateSessionTotals(nowMs, static_cast<float>(dtMs) / 1000.0f);
}
