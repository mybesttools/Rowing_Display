#pragma once

#include <cstdint>

// Time zones offered under Settings > CLOCK, as POSIX TZ strings (with
// their daylight-saving rules) for setenv("TZ") / configTzTime(). Index 0
// is the default. Append only: settings store the index.
namespace timezones {

struct Zone {
    const char* name;
    const char* posix;
};

constexpr Zone kZones[] = {
    {"UTC", "UTC0"},
    {"London, Lisbon", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Amsterdam, Berlin, Paris", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Helsinki, Athens", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Moscow, Istanbul", "<+03>-3"},
    {"Dubai", "<+04>-4"},
    {"India", "IST-5:30"},
    {"Bangkok, Jakarta", "<+07>-7"},
    {"China, Singapore", "CST-8"},
    {"Japan, Korea", "JST-9"},
    {"Brisbane", "AEST-10"},
    {"Sydney, Melbourne", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
    {"Honolulu", "HST10"},
    {"Los Angeles", "PST8PDT,M3.2.0,M11.1.0"},
    {"Phoenix", "MST7"},
    {"Denver", "MST7MDT,M3.2.0,M11.1.0"},
    {"Chicago", "CST6CDT,M3.2.0,M11.1.0"},
    {"New York", "EST5EDT,M3.2.0,M11.1.0"},
    {"Sao Paulo", "<-03>3"},
};
constexpr uint8_t kCount = sizeof(kZones) / sizeof(kZones[0]);

inline const Zone& zone(uint8_t index) {
    return kZones[index < kCount ? index : 0];
}

} // namespace timezones
