#pragma once

#include <cstdint>

#include "config.h"

// Drive/recovery stroke phase, exposed on RowerMetrics so the UI can sync
// e.g. animations to the actual stroke rather than guessing off rpm.
enum class RowerPhase { Idle, Drive, Recovery };

struct RowerMetrics {
    float rpm = 0.0f;
    float strokeRateSpm = 0.0f;
    uint32_t strokeCount = 0;
    float powerWatts = 0.0f;
    float splitSecPer500m = 0.0f;
    float distanceMeters = 0.0f;
    uint32_t elapsedSec = 0;
    // Fitted flywheel drag factor (k, kg*m^2). Exposed mainly for
    // calibration/debugging — watch this settle to a stable value across
    // strokes at a given resistance setting once FLYWHEEL_INERTIA_KGM2 is
    // calibrated for real.
    float dragFactor = 0.0f;
    RowerPhase phase = RowerPhase::Idle;
    // Fraction (0..1) through the current Drive/Recovery phase, relative to
    // how long the previous phase of that type took. Lets the UI animate
    // continuously in sync with the actual stroke instead of only knowing
    // which phase we're in. 0 while Idle or before a phase duration has
    // been measured yet.
    float phaseProgress = 0.0f;
};

// Drive/recovery stroke-phase detector and flywheel-physics metrics model.
//
// Inspired by (concepts and formulas only — no code copied; see Hardware.md
// for the full attribution and license notes):
//   - openrowingmonitor (JaapvanEkris fork, GPLv3): the drag-factor-as-
//     linear-regression derivation and the P = k*omega^3 power model.
//   - Abasz/ESPRowingMonitor (no LICENSE file found, treated as all-rights-
//     reserved — used for architecture/approach inspiration only): the
//     general shape of a pulse-timing-driven drive/recovery state machine
//     on ESP32-class hardware.
class MetricsModel {
public:
    explicit MetricsModel(cfg::PulseMode mode = cfg::PULSE_MODE) : mode_(mode) {}

    void onPulseMicros(uint32_t nowMicros, uint32_t nowMs);
    void tick(uint32_t nowMs);

    // Clears session-scoped state (distance, time, stroke count, phase
    // tracking) for a fresh session. Deliberately keeps the fitted drag
    // factor rather than reseeding it back to the placeholder - it
    // represents the machine's physical resistance setting, which doesn't
    // change just because a new session started.
    void reset();

    const RowerMetrics& metrics() const { return metrics_; }

private:
    void updatePhase(uint32_t nowMicros, uint32_t nowMs, uint32_t dtMicros);
    void enterPhase(RowerPhase phase, uint32_t nowMicros, uint32_t nowMs);
    void onRecoveryEnded();
    void decayIfIdle(uint32_t nowMs);
    void recomputeDerivedMetrics(float dtSeconds);
    void updatePhaseProgress(uint32_t nowMs);
    void onStrokePulse(uint32_t nowMs);
    void tickStrokeMode(uint32_t nowMs);

    cfg::PulseMode mode_;

    RowerMetrics metrics_{};

    RowerPhase phase_ = RowerPhase::Idle;
    uint32_t phaseStartMs_ = 0;

    uint32_t lastPulseMicros_ = 0;
    uint32_t lastPulseMs_ = 0;
    uint32_t lastDtMicros_ = 0;
    uint32_t lastStrokeStartMs_ = 0;

    uint32_t sessionStartMs_ = 0;
    uint32_t lastTickMs_ = 0;

    // Last measured duration of a completed Drive/Recovery phase, used as
    // the "expected" duration to normalize the current phase's elapsed time
    // into phaseProgress. 0 until the first phase of that type has
    // completed once this session.
    uint32_t lastDrivePhaseDurationMs_ = 0;
    uint32_t lastRecoveryPhaseDurationMs_ = 0;

    // Stroke mode: last measured catch-to-catch interval (0 until two
    // strokes in a row) and the boat speed it implies.
    uint32_t strokeIntervalMs_ = 0;
    float strokeSpeedMps_ = 0.0f;

    float omegaSmoothed_ = 0.0f; // rad/s
    float dragFactor_ = 0.0f;    // k, kg*m^2

    // Recovery-phase regression buffer: (elapsed seconds since recovery
    // start, 1/omega at that pulse). Fit once recovery ends to estimate k.
    static constexpr int kMaxRecoveryPoints = 48;
    float recoveryT_[kMaxRecoveryPoints] = {};
    float recoveryInvOmega_[kMaxRecoveryPoints] = {};
    int recoveryCount_ = 0;
    uint32_t recoveryStartMicros_ = 0;

    bool fitDragFactor(float& outK, float& outR2) const;
};
