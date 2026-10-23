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

namespace {

uint16_t u16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

} // namespace

void test_ftms_encoder_packet_shape() {
    RowerMetrics m;
    m.strokeRateSpm = 24.5f;
    m.strokeCount = 123;
    m.avgStrokeRateSpm = 22.0f;
    m.distanceMeters = 456.0f;
    m.splitSecPer500m = 130.0f;
    m.avgSplitSecPer500m = 140.0f;
    m.powerWatts = 210.0f;
    m.energyKcal = 37.6f;
    m.energyPerHourKcal = 1023.0f;
    m.elapsedSec = 789;

    // Both parts fit the 20-byte payload of the default ATT MTU.
    uint8_t packet[20] = {0};

    // Part 0: More Data(bit0) | AvgStrokeRate(1) | TotalDistance(2) |
    // InstPace(3) | AvgPace(4) | InstPower(5) = 0x003F, no stroke rate/count.
    size_t n = ftms::encodeIndoorRowerData(m, 0, packet, sizeof(packet));
    TEST_ASSERT_EQUAL_UINT32(12u, static_cast<uint32_t>(n));
    TEST_ASSERT_EQUAL_UINT16(0x003Fu, u16(packet));
    TEST_ASSERT_EQUAL_UINT8(44u, packet[2]); // Average Stroke Rate, 0.5 spm units
    const uint32_t distance = static_cast<uint32_t>(packet[3]) | (static_cast<uint32_t>(packet[4]) << 8) |
                               (static_cast<uint32_t>(packet[5]) << 16);
    TEST_ASSERT_EQUAL_UINT32(456u, distance);
    TEST_ASSERT_EQUAL_UINT16(130u, u16(packet + 6));  // Instantaneous Pace
    TEST_ASSERT_EQUAL_UINT16(140u, u16(packet + 8));  // Average Pace
    TEST_ASSERT_EQUAL_UINT16(210u, u16(packet + 10)); // Instantaneous Power

    // Part 1: ExpendedEnergy(bit8) | ElapsedTime(bit11) = 0x0900, with
    // Stroke Rate + Stroke Count first since More Data is clear.
    n = ftms::encodeIndoorRowerData(m, 1, packet, sizeof(packet));
    TEST_ASSERT_EQUAL_UINT32(12u, static_cast<uint32_t>(n));
    TEST_ASSERT_EQUAL_UINT16(0x0900u, u16(packet));
    TEST_ASSERT_EQUAL_UINT8(49u, packet[2]);
    TEST_ASSERT_EQUAL_UINT16(123u, u16(packet + 3));
    TEST_ASSERT_EQUAL_UINT16(37u, u16(packet + 5));   // Total Energy
    TEST_ASSERT_EQUAL_UINT16(1023u, u16(packet + 7)); // Energy Per Hour
    TEST_ASSERT_EQUAL_UINT8(17u, packet[9]);          // Energy Per Minute
    TEST_ASSERT_EQUAL_UINT16(789u, u16(packet + 10)); // Elapsed Time

    TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(ftms::encodeIndoorRowerData(m, 2, packet, sizeof(packet))));
}

void test_ftms_encoder_fitness_machine_feature() {
    uint8_t packet[8] = {0};
    const size_t n = ftms::encodeFitnessMachineFeature(packet, sizeof(packet));

    TEST_ASSERT_EQUAL_UINT32(8u, static_cast<uint32_t>(n));

    const uint32_t features = static_cast<uint32_t>(packet[0]) | (static_cast<uint32_t>(packet[1]) << 8) |
                               (static_cast<uint32_t>(packet[2]) << 16) | (static_cast<uint32_t>(packet[3]) << 24);
    // Cadence(bit1) | TotalDistance(bit2) | Pace(bit5) | ExpendedEnergy(bit9) |
    // ElapsedTime(bit12) | PowerMeasurement(bit14)
    TEST_ASSERT_EQUAL_UINT32((1u << 1) | (1u << 2) | (1u << 5) | (1u << 9) | (1u << 12) | (1u << 14), features);

    const uint32_t targetSettingFeatures = static_cast<uint32_t>(packet[4]) |
                                            (static_cast<uint32_t>(packet[5]) << 8) |
                                            (static_cast<uint32_t>(packet[6]) << 16) |
                                            (static_cast<uint32_t>(packet[7]) << 24);
    TEST_ASSERT_EQUAL_UINT32(0u, targetSettingFeatures);
}

void test_ftms_control_point_acknowledges_mandatory_op_codes() {
    using ftms::control_point::OpCode;
    using ftms::control_point::ResultCode;
    using ftms::control_point::resultFor;

    const uint8_t requestControl[] = {static_cast<uint8_t>(OpCode::RequestControl)};
    const uint8_t reset[] = {static_cast<uint8_t>(OpCode::Reset)};
    const uint8_t start[] = {static_cast<uint8_t>(OpCode::StartOrResume)};
    const uint8_t stop[] = {static_cast<uint8_t>(OpCode::StopOrPause), ftms::control_point::kStop};
    const uint8_t pause[] = {static_cast<uint8_t>(OpCode::StopOrPause), ftms::control_point::kPause};
    const uint8_t stopNoParam[] = {static_cast<uint8_t>(OpCode::StopOrPause)};
    const uint8_t setTargetPower[] = {0x04, 0x64, 0x00};

    TEST_ASSERT_TRUE(ResultCode::Success == resultFor(requestControl, sizeof(requestControl)));
    TEST_ASSERT_TRUE(ResultCode::Success == resultFor(reset, sizeof(reset)));
    TEST_ASSERT_TRUE(ResultCode::Success == resultFor(start, sizeof(start)));
    TEST_ASSERT_TRUE(ResultCode::Success == resultFor(stop, sizeof(stop)));
    TEST_ASSERT_TRUE(ResultCode::Success == resultFor(pause, sizeof(pause)));
    TEST_ASSERT_TRUE(ResultCode::InvalidParameter == resultFor(stopNoParam, sizeof(stopNoParam)));
    TEST_ASSERT_TRUE(ResultCode::OpCodeNotSupported == resultFor(setTargetPower, sizeof(setTargetPower)));

    uint8_t response[3] = {0};
    const size_t n = ftms::control_point::encodeResponse(0x04, ResultCode::OpCodeNotSupported, response, sizeof(response));
    TEST_ASSERT_EQUAL_UINT32(3u, static_cast<uint32_t>(n));
    TEST_ASSERT_EQUAL_HEX8(0x80u, response[0]);
    TEST_ASSERT_EQUAL_HEX8(0x04u, response[1]);
    TEST_ASSERT_EQUAL_HEX8(0x02u, response[2]);
}

void test_ftms_status_encoding() {
    using ftms::status::Event;
    uint8_t out[2] = {0};
    TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(ftms::status::encode(Event::Reset, 0, out, sizeof(out))));
    TEST_ASSERT_EQUAL_HEX8(0x01u, out[0]);
    TEST_ASSERT_EQUAL_UINT32(2u, static_cast<uint32_t>(ftms::status::encode(Event::StoppedOrPausedByUser,
                                                                            ftms::control_point::kPause, out, sizeof(out))));
    TEST_ASSERT_EQUAL_HEX8(0x02u, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x02u, out[1]);
    TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(ftms::status::encode(Event::StartedOrResumedByUser, 0, out, sizeof(out))));
    TEST_ASSERT_EQUAL_HEX8(0x04u, out[0]);
}

void test_session_averages_and_energy() {
    MetricsModel model(cfg::PulseMode::Stroke);

    // 11 strokes 2.5 s apart (24 spm), ticking every 50 ms like the main loop.
    uint32_t nowMs = 1000;
    for (int i = 0; i <= 10; i++) {
        model.onPulseMicros(nowMs * 1000u, nowMs);
        for (int t = 0; t < 50; t++) {
            nowMs += 50;
            model.tick(nowMs);
        }
    }
    const RowerMetrics& m = model.metrics();
    const float elapsed = static_cast<float>(nowMs - 1000u) / 1000.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 11.0f * 60.0f / elapsed, m.avgStrokeRateSpm);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 500.0f * elapsed / m.distanceMeters, m.avgSplitSecPer500m);
    // Rowing the whole time: at least the 300 kcal/h base rate.
    TEST_ASSERT_TRUE(m.energyKcal >= 300.0f * (elapsed - 0.1f) / 3600.0f);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 4.0f * 0.8604f * m.powerWatts + 300.0f, m.energyPerHourKcal);
}

void test_pause_freezes_session_time_and_ignores_pulses() {
    MetricsModel model(cfg::PulseMode::Stroke);
    model.onPulseMicros(1000000u, 1000u);
    model.tick(1050u);
    model.onPulseMicros(3000000u, 3000u);
    model.tick(4000u);
    TEST_ASSERT_EQUAL_UINT32(3u, model.metrics().elapsedSec);

    model.pause(4000u);
    TEST_ASSERT_TRUE(model.metrics().paused);
    TEST_ASSERT_TRUE(model.metrics().phase == RowerPhase::Idle);
    model.onPulseMicros(5000000u, 5000u); // ignored
    model.tick(60000u);
    TEST_ASSERT_EQUAL_UINT32(2u, model.metrics().strokeCount);
    TEST_ASSERT_EQUAL_UINT32(3u, model.metrics().elapsedSec);

    // 56 s paused; after resume the clock carries on from 3 s.
    model.resume(60000u);
    TEST_ASSERT_FALSE(model.metrics().paused);
    model.onPulseMicros(61000000u, 61000u);
    model.tick(62000u);
    TEST_ASSERT_EQUAL_UINT32(3u, model.metrics().strokeCount);
    TEST_ASSERT_EQUAL_UINT32(5u, model.metrics().elapsedSec);
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
    RUN_TEST(test_ftms_status_encoding);
    RUN_TEST(test_session_averages_and_energy);
    RUN_TEST(test_pause_freezes_session_time_and_ignores_pulses);
    return UNITY_END();
}
