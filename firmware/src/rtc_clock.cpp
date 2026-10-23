#include "rtc_clock.h"

#include <sys/time.h>

#include <cstdlib>

#include <cstdint>
#include <cstdio>

#include <Preferences.h>

#if !defined(SIMULATION_MODE)
#include "driver/i2c.h"
#include "esp_lv_adapter_arduino.h"
#endif

namespace rtc_clock {

namespace {

// 2025-01-01T00:00:00Z: anything earlier is the unset clock.
constexpr time_t kEarliestValid = 1735689600;

constexpr char kNamespace[] = "rowing";
constexpr char kKeyLastSet[] = "clockSet"; // UTC seconds of the last set()

time_t g_lastSet = 0;
bool g_batteryProblem = false;
bool g_rtcStopped = false;

time_t loadLastSet() {
    Preferences prefs;
    if (!prefs.begin(kNamespace, true)) {
        return 0;
    }
    const time_t t = static_cast<time_t>(prefs.getULong64(kKeyLastSet, 0));
    prefs.end();
    return t;
}

void storeLastSet(time_t t) {
    Preferences prefs;
    if (prefs.begin(kNamespace, false)) {
        prefs.putULong64(kKeyLastSet, static_cast<uint64_t>(t));
        prefs.end();
    }
}

#if !defined(SIMULATION_MODE)

// PCF85063 (vendor 04_RTC_Test): time registers 0x04..0x0A as BCD seconds,
// minutes, hours, day, weekday, month, year. Seconds bit 7 (OS) is set when
// the oscillator stopped, i.e. the time can't be trusted. Years count from
// 2000 here (the vendor example uses 1970; only this firmware writes it).
constexpr uint8_t kAddress = 0x51;
constexpr uint8_t kRegSeconds = 0x04;
constexpr i2c_port_t kPort = I2C_NUM_0;

// Civil date to days since 1970-01-01 (Howard Hinnant's days_from_civil).
// mktime() would apply the time zone; the RTC holds UTC.
int64_t daysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

uint8_t fromBcd(uint8_t v) {
    return static_cast<uint8_t>((v >> 4) * 10 + (v & 0x0F));
}

uint8_t toBcd(int v) {
    return static_cast<uint8_t>(((v / 10) << 4) | (v % 10));
}

bool readRtc(time_t& out) {
    uint8_t r[7] = {0};
    if (!esp_lv_adapter_i2c_lock(100)) {
        return false;
    }
    const esp_err_t err =
        i2c_master_write_read_device(kPort, kAddress, &kRegSeconds, 1, r, sizeof(r), pdMS_TO_TICKS(50));
    esp_lv_adapter_i2c_unlock();
    if (err != ESP_OK) {
        printf("clock: no RTC (%s)\n", esp_err_to_name(err));
        return false;
    }
    if (r[0] & 0x80) {
        printf("clock: RTC not set (oscillator stopped)\n");
        g_rtcStopped = true;
        return false;
    }
    const int64_t days = daysFromCivil(2000 + fromBcd(r[6]), fromBcd(r[5] & 0x1F), fromBcd(r[3] & 0x3F));
    out = static_cast<time_t>(days * 86400 + fromBcd(r[2] & 0x3F) * 3600 + fromBcd(r[1] & 0x7F) * 60 +
                              fromBcd(r[0] & 0x7F));
    return true;
}

void writeRtc(time_t utc) {
    tm t{};
    gmtime_r(&utc, &t);
    const uint8_t data[8] = {
        kRegSeconds,         toBcd(t.tm_sec),  toBcd(t.tm_min),           toBcd(t.tm_hour),
        toBcd(t.tm_mday),    toBcd(t.tm_wday), toBcd(t.tm_mon + 1),       toBcd(t.tm_year - 100),
    };
    if (!esp_lv_adapter_i2c_lock(100)) {
        return;
    }
    const esp_err_t err = i2c_master_write_to_device(kPort, kAddress, data, sizeof(data), pdMS_TO_TICKS(50));
    esp_lv_adapter_i2c_unlock();
    if (err != ESP_OK) {
        printf("clock: RTC write failed (%s)\n", esp_err_to_name(err));
    }
}

#endif

void setSystem(time_t utc) {
    timeval tv{};
    tv.tv_sec = utc;
    settimeofday(&tv, nullptr);
}

void printNow(const char* what) {
    const time_t now = time(nullptr);
    tm t{};
    localtime_r(&now, &t);
    char buf[40];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", &t);
    printf("clock: %s %s\n", what, buf);
}

} // namespace

void begin() {
    g_lastSet = loadLastSet();
#if !defined(SIMULATION_MODE)
    time_t utc = 0;
    if (readRtc(utc) && utc >= kEarliestValid) {
        setSystem(utc);
        printNow("from RTC");
    }
    if (g_rtcStopped && g_lastSet != 0) {
        g_batteryProblem = true;
        printf("clock: time was lost since it was set: clock battery missing or empty\n");
    }
#endif
}

time_t lastSet() {
    return g_lastSet;
}

void forgetLastSet() {
    g_lastSet = 0;
    storeLastSet(0);
}

bool batteryProblem() {
    return g_batteryProblem;
}

void setTimezone(const char* posix) {
    setenv("TZ", posix, 1);
    tzset();
}

bool valid() {
    return time(nullptr) >= kEarliestValid;
}

void set(time_t utc) {
    if (utc < kEarliestValid) {
        printf("clock: rejected %ld (before 2025)\n", static_cast<long>(utc));
        return;
    }
    setSystem(utc);
#if !defined(SIMULATION_MODE)
    writeRtc(utc);
#endif
    g_lastSet = utc;
    storeLastSet(utc);
    printNow("set to");
}

} // namespace rtc_clock
