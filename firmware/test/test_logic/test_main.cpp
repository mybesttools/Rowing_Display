#include <unity.h>

#include "ftms_encoder.h"
#include "metrics_model.h"

void test_metrics_model_updates_from_pulses() {
    MetricsModel model;

    model.onPulseMicros(1000000u, 1000u);
    model.onPulseMicros(1200000u, 1200u);
    model.tick(1250u);

    const RowerMetrics &m = model.metrics();
    TEST_ASSERT_FLOAT_WITHIN(0.2f, 60.0f, m.rpm);
    TEST_ASSERT_EQUAL_UINT32(1u, m.strokeCount);
    TEST_ASSERT_TRUE(m.distanceMeters > 0.0f);
    TEST_ASSERT_TRUE(m.splitSecPer500m > 0.0f);
    TEST_ASSERT_TRUE(m.powerWatts > 0.0f);
}

void test_metrics_model_decay_after_idle() {
    MetricsModel model;

    model.onPulseMicros(1000000u, 1000u);
    model.onPulseMicros(1200000u, 1200u);
    model.tick(1250u);

    const float before = model.metrics().rpm;

    // Exceed idle timeout to trigger decay path.
    model.tick(5000u);

    const RowerMetrics &m = model.metrics();
    TEST_ASSERT_TRUE(m.rpm < before);
    TEST_ASSERT_EQUAL_UINT32(1u, m.strokeCount);
}

void test_ftms_encoder_packet_shape() {
    RowerMetrics m;
    m.strokeRateSpm = 24.5f;
    m.strokeCount = 123;
    m.distanceMeters = 456.0f;
    m.splitSecPer500m = 130.0f;
    m.powerWatts = 210.0f;

    uint8_t packet[32] = {0};
    const size_t n = ftms::encodeIndoorRowerData(m, packet, sizeof(packet));

    TEST_ASSERT_EQUAL_UINT32(15u, static_cast<uint32_t>(n));
    TEST_ASSERT_EQUAL_HEX8(0x3Fu, packet[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, packet[1]);
    TEST_ASSERT_EQUAL_UINT8(49u, packet[2]);
    TEST_ASSERT_EQUAL_UINT16(123u, static_cast<uint16_t>(packet[4] | (packet[5] << 8)));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_metrics_model_updates_from_pulses);
    RUN_TEST(test_metrics_model_decay_after_idle);
    RUN_TEST(test_ftms_encoder_packet_shape);
    return UNITY_END();
}
