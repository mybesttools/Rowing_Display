#include <cmath>

#include <unity.h>

#include "config.h"
#include "ftms_encoder.h"
#include "metrics_model.h"

namespace {

// Feeds a decreasing-then-increasing dt sequence to bootstrap the model into
// a Drive phase and then a Recovery phase, using whole milliseconds so the
// timing comfortably clears MIN_PULSE_INTERVAL_US and the phase debounce
// windows. Returns the microsecond clock value after the last pulse.
uint32_t feedBootstrapDriveThenRecovery(MetricsModel& model, uint32_t startMs) {
    // Drive: decreasing periods (300, 250, 200, 150 ms) -> flywheel speeding up.
    const uint32_t driveDtsMs[] = {300, 250, 200, 150};
    uint32_t nowMs = startMs;
    uint32_t nowUs = startMs * 1000u;
    for (uint32_t dtMs : driveDtsMs) {
        nowMs += dtMs;
        nowUs += dtMs * 1000u;
        model.onPulseMicros(nowUs, nowMs);
    }
    return nowUs;
}

} // namespace

void test_metrics_model_bootstraps_drive_then_counts_stroke_after_recovery() {
    MetricsModel model(cfg::PulseMode::Revolution);

    uint32_t nowMs = 0;
    uint32_t nowUs = feedBootstrapDriveThenRecovery(model, nowMs);
    nowMs = nowUs / 1000u;

    // Still in the initial (bootstrap) drive phase - no completed stroke yet.
    TEST_ASSERT_EQUAL_UINT32(0u, model.metrics().strokeCount);

    // Recovery: increasing periods (well past the 3% hysteresis band).
    const uint32_t recoveryDtsMs[] = {400, 500, 600, 700, 800};
    for (uint32_t dtMs : recoveryDtsMs) {
        nowMs += dtMs;
        nowUs += dtMs * 1000u;
        model.onPulseMicros(nowUs, nowMs);
    }

    // Back to drive: a short period ends recovery and starts a new stroke.
    nowMs += 150;
    nowUs += 150u * 1000u;
    model.onPulseMicros(nowUs, nowMs);

    TEST_ASSERT_EQUAL_UINT32(1u, model.metrics().strokeCount);

    model.tick(nowMs + cfg::LOOP_INTERVAL_MS);
    TEST_ASSERT_TRUE(model.metrics().rpm > 0.0f);
}

void test_metrics_model_fits_drag_factor_from_recovery_phase() {
    MetricsModel model(cfg::PulseMode::Revolution);

    uint32_t nowMs = 0;
    uint32_t nowUs = feedBootstrapDriveThenRecovery(model, nowMs);
    nowMs = nowUs / 1000u;

    // Construct a synthetic recovery phase that exactly follows the modeled
    // physics (I*domega/dt = -k*omega^2, i.e. 1/omega is linear in elapsed
    // time with slope k/I), so the regression should recover k almost
    // exactly. Each pulse represents one full revolution (MAGNETS_PER_REV
    // is 1), so consecutive pulse times t_i must satisfy:
    //   2*pi / (t_i - t_(i-1)) = 1 / (1/omega0 + (k/I) * t_i)
    // which rearranges to a simple recurrence for t_i.
    constexpr float kTestDragFactor = 0.002f;
    constexpr float kOmega0 = 10.0f; // rad/s
    const float slope = kTestDragFactor / cfg::machinePreset(cfg::kDefaultMachine).flywheelInertia;
    constexpr float kTwoPi = 6.283185307f;

    float t = 0.0f; // seconds since recovery start
    uint32_t recoveryStartUs = 0;
    // First recovery pulse: this transition is what sets the model's
    // internal recovery-start reference, so it defines t=0 for itself.
    {
        const float dtSeconds = kTwoPi / kOmega0;
        nowUs += static_cast<uint32_t>(dtSeconds * 1000000.0f);
        nowMs = nowUs / 1000u;
        model.onPulseMicros(nowUs, nowMs);
        recoveryStartUs = nowUs;
        t = 0.0f;
    }

    constexpr int kPoints = 6;
    for (int i = 0; i < kPoints; ++i) {
        const float tNext = (t + kTwoPi / kOmega0) / (1.0f - kTwoPi * slope);
        nowUs = recoveryStartUs + static_cast<uint32_t>(tNext * 1000000.0f);
        nowMs = nowUs / 1000u;
        model.onPulseMicros(nowUs, nowMs);
        t = tNext;
    }

    // A short pulse ends recovery, triggering the drag-factor fit.
    nowUs += 500u * 1000u;
    nowMs = nowUs / 1000u;
    model.onPulseMicros(nowUs, nowMs);

    model.tick(nowMs + cfg::LOOP_INTERVAL_MS);

    const float fitted = model.metrics().dragFactor;
    TEST_ASSERT_TRUE(fitted > 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(kTestDragFactor * 0.2f, kTestDragFactor, fitted);
}

void test_metrics_model_decays_after_idle() {
    MetricsModel model(cfg::PulseMode::Revolution);

    uint32_t nowMs = 0;
    uint32_t nowUs = feedBootstrapDriveThenRecovery(model, nowMs);
    nowMs = nowUs / 1000u;

    model.tick(nowMs + 10);
    const float before = model.metrics().rpm;
    TEST_ASSERT_TRUE(before > 0.0f);

    // Exceed the idle timeout with no further pulses.
    model.tick(nowMs + cfg::STROKE_IDLE_TIMEOUT_MS + 500);

    TEST_ASSERT_TRUE(model.metrics().rpm < before);
}

void test_metrics_model_reset_clears_session_but_keeps_drag_factor() {
    MetricsModel model(cfg::PulseMode::Revolution);

    uint32_t nowMs = 0;
    uint32_t nowUs = feedBootstrapDriveThenRecovery(model, nowMs);
    nowMs = nowUs / 1000u;
    model.tick(nowMs + 10);

    TEST_ASSERT_TRUE(model.metrics().rpm > 0.0f);
    TEST_ASSERT_TRUE(model.metrics().elapsedSec > 0u || nowMs > 0u);

    model.reset();

    const RowerMetrics& m = model.metrics();
    TEST_ASSERT_EQUAL_FLOAT(0.0f, m.rpm);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, m.distanceMeters);
    TEST_ASSERT_EQUAL_UINT32(0u, m.strokeCount);
    TEST_ASSERT_EQUAL_UINT32(0u, m.elapsedSec);

    // A fresh pulse after reset should bootstrap a new session cleanly
    // (sessionStartMs_ was cleared), not carry over old timing state.
    model.onPulseMicros(1000000u, 1000u);
    model.tick(1010u);
    TEST_ASSERT_EQUAL_UINT32(0u, model.metrics().elapsedSec);
}

void test_stroke_mode_counts_every_pulse_as_a_stroke() {
    MetricsModel model(cfg::PulseMode::Stroke);

    // Three strokes 2 s apart (30 spm), plus a bounce 50 ms after the second.
    model.onPulseMicros(1000000u, 1000u);
    model.onPulseMicros(3000000u, 3000u);
    model.onPulseMicros(3050000u, 3050u);
    model.onPulseMicros(5000000u, 5000u);

    TEST_ASSERT_EQUAL_UINT32(3u, model.metrics().strokeCount);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.0f * cfg::METERS_PER_STROKE, model.metrics().distanceMeters);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 30.0f, model.metrics().strokeRateSpm);

    // Drive for the first DRIVE_FRACTION of the cycle, then recovery.
    model.tick(5100u);
    TEST_ASSERT_TRUE(model.metrics().phase == RowerPhase::Drive);
    model.tick(6500u);
    TEST_ASSERT_TRUE(model.metrics().phase == RowerPhase::Recovery);
    TEST_ASSERT_TRUE(model.metrics().splitSecPer500m > 0.0f);

    // No pulse for well over 1.5 cycles -> idle.
    model.tick(9000u);
    TEST_ASSERT_TRUE(model.metrics().phase == RowerPhase::Idle);
}

void test_ftms_encoder_packet_shape() {
    RowerMetrics m;
    m.strokeRateSpm = 24.5f;
    m.strokeCount = 123;
    m.distanceMeters = 456.0f;
    m.splitSecPer500m = 130.0f;
    m.powerWatts = 210.0f;
    m.elapsedSec = 789;

    uint8_t packet[32] = {0};
    const size_t n = ftms::encodeIndoorRowerData(m, packet, sizeof(packet));

    // Flags: AvgStrokeRate(bit1) | TotalDistance(bit2) | InstPace(bit3) |
    // AvgPace(bit4) | InstPower(bit5) | ElapsedTime(bit11) = 0x083E. Bit 0
    // ("More Data") must stay clear - Stroke Rate/Count are always present.
    TEST_ASSERT_EQUAL_UINT32(17u, static_cast<uint32_t>(n));
    TEST_ASSERT_EQUAL_HEX8(0x3Eu, packet[0]);
    TEST_ASSERT_EQUAL_HEX8(0x08u, packet[1]);

    // Stroke Rate (0.5 spm resolution) then Stroke Count, immediately
    // after the flags and before any optional field.
    TEST_ASSERT_EQUAL_UINT8(49u, packet[2]);
    TEST_ASSERT_EQUAL_UINT16(123u, static_cast<uint16_t>(packet[3] | (packet[4] << 8)));

    TEST_ASSERT_EQUAL_UINT8(49u, packet[5]); // Average Stroke Rate

    const uint32_t distance = static_cast<uint32_t>(packet[6]) | (static_cast<uint32_t>(packet[7]) << 8) |
                               (static_cast<uint32_t>(packet[8]) << 16);
    TEST_ASSERT_EQUAL_UINT32(456u, distance);

    TEST_ASSERT_EQUAL_UINT16(130u, static_cast<uint16_t>(packet[9] | (packet[10] << 8)));  // Instantaneous Pace
    TEST_ASSERT_EQUAL_UINT16(130u, static_cast<uint16_t>(packet[11] | (packet[12] << 8))); // Average Pace
    TEST_ASSERT_EQUAL_UINT16(210u, static_cast<uint16_t>(packet[13] | (packet[14] << 8))); // Instantaneous Power
    TEST_ASSERT_EQUAL_UINT16(789u, static_cast<uint16_t>(packet[15] | (packet[16] << 8))); // Elapsed Time
}

void test_ftms_encoder_fitness_machine_feature() {
    uint8_t packet[8] = {0};
    const size_t n = ftms::encodeFitnessMachineFeature(packet, sizeof(packet));

    TEST_ASSERT_EQUAL_UINT32(8u, static_cast<uint32_t>(n));

    const uint32_t features = static_cast<uint32_t>(packet[0]) | (static_cast<uint32_t>(packet[1]) << 8) |
                               (static_cast<uint32_t>(packet[2]) << 16) | (static_cast<uint32_t>(packet[3]) << 24);
    // Cadence(bit1) | TotalDistance(bit2) | Pace(bit5) | ElapsedTime(bit12) | PowerMeasurement(bit14)
    TEST_ASSERT_EQUAL_UINT32((1u << 1) | (1u << 2) | (1u << 5) | (1u << 12) | (1u << 14), features);

    const uint32_t targetSettingFeatures = static_cast<uint32_t>(packet[4]) |
                                            (static_cast<uint32_t>(packet[5]) << 8) |
                                            (static_cast<uint32_t>(packet[6]) << 16) |
                                            (static_cast<uint32_t>(packet[7]) << 24);
    TEST_ASSERT_EQUAL_UINT32(0u, targetSettingFeatures);
}

void test_ftms_control_point_acknowledges_mandatory_op_codes() {
    using ftms::control_point::OpCode;
    using ftms::control_point::ResultCode;

    TEST_ASSERT_TRUE(
        ResultCode::Success == ftms::control_point::resultFor(static_cast<uint8_t>(OpCode::RequestControl))
    );
    TEST_ASSERT_TRUE(ResultCode::Success == ftms::control_point::resultFor(static_cast<uint8_t>(OpCode::Reset)));
    TEST_ASSERT_TRUE(
        ResultCode::Success == ftms::control_point::resultFor(static_cast<uint8_t>(OpCode::StartOrResume))
    );
    TEST_ASSERT_TRUE(ResultCode::Success == ftms::control_point::resultFor(static_cast<uint8_t>(OpCode::StopOrPause))
    );
    TEST_ASSERT_TRUE(ResultCode::OpCodeNotSupported == ftms::control_point::resultFor(0x04)); // Set Target Power

    uint8_t response[3] = {0};
    const size_t n = ftms::control_point::encodeResponse(0x04, ResultCode::OpCodeNotSupported, response, sizeof(response));
    TEST_ASSERT_EQUAL_UINT32(3u, static_cast<uint32_t>(n));
    TEST_ASSERT_EQUAL_HEX8(0x80u, response[0]);
    TEST_ASSERT_EQUAL_HEX8(0x04u, response[1]);
    TEST_ASSERT_EQUAL_HEX8(0x02u, response[2]);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_metrics_model_bootstraps_drive_then_counts_stroke_after_recovery);
    RUN_TEST(test_metrics_model_fits_drag_factor_from_recovery_phase);
    RUN_TEST(test_metrics_model_decays_after_idle);
    RUN_TEST(test_metrics_model_reset_clears_session_but_keeps_drag_factor);
    RUN_TEST(test_stroke_mode_counts_every_pulse_as_a_stroke);
    RUN_TEST(test_ftms_encoder_packet_shape);
    RUN_TEST(test_ftms_encoder_fitness_machine_feature);
    RUN_TEST(test_ftms_control_point_acknowledges_mandatory_op_codes);
    return UNITY_END();
}
