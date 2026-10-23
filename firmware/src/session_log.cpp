#include "session_log.h"

#include <sys/stat.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

#include <Arduino.h>

#include "rtc_clock.h"
#include "sd_card.h"

#if !defined(SIMULATION_MODE)
#include "esp_heap_caps.h"
#endif

namespace session_log {

namespace {

struct Sample {
    uint32_t sec;   // session time
    float meters;
    uint16_t watts;
    uint8_t spm;
};

// Four hours at one sample a second; later samples are dropped.
constexpr size_t kMaxSamples = 4 * 3600;
constexpr char kDir[] = "/sd/sessions";

Sample* g_samples = nullptr;
size_t g_count = 0;
uint32_t g_lastSec = UINT32_MAX;
time_t g_startUtc = 0;

uint8_t clampU8(float v) {
    return v <= 0.0f ? 0 : (v >= 254.0f ? 254 : static_cast<uint8_t>(v + 0.5f));
}

void formatUtc(char* out, size_t cap, time_t t) {
    tm parts{};
    gmtime_r(&t, &parts);
    strftime(out, cap, "%Y-%m-%dT%H:%M:%SZ", &parts);
}

// "2026-10-23_0945.tcx", or "session-NNN.tcx" while the clock isn't set.
void pickFileName(char* out, size_t cap) {
    if (rtc_clock::valid()) {
        tm parts{};
        gmtime_r(&g_startUtc, &parts);
        strftime(out, cap, "%Y-%m-%d_%H%M%S.tcx", &parts);
        return;
    }
    char path[64];
    struct stat st {};
    for (int n = 1; n < 1000; n++) {
        snprintf(out, cap, "session-%03d.tcx", n);
        snprintf(path, sizeof(path), "%s/%s", kDir, out);
        if (stat(path, &st) != 0) {
            return;
        }
    }
}

bool writeTcx(FILE* f, const RowerMetrics& m) {
    char when[32];
    formatUtc(when, sizeof(when), g_startUtc);

    float wattSum = 0.0f;
    for (size_t i = 0; i < g_count; i++) {
        wattSum += g_samples[i].watts;
    }
    const unsigned avgWatts = g_count > 0 ? static_cast<unsigned>(wattSum / static_cast<float>(g_count) + 0.5f) : 0;

    fprintf(f,
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<TrainingCenterDatabase xmlns=\"http://www.garmin.com/xmlschemas/TrainingCenterDatabase/v2\" "
            "xmlns:ns3=\"http://www.garmin.com/xmlschemas/ActivityExtension/v2\">\n"
            " <Activities>\n"
            "  <Activity Sport=\"Other\">\n"
            "   <Id>%s</Id>\n"
            "   <Lap StartTime=\"%s\">\n"
            "    <TotalTimeSeconds>%lu</TotalTimeSeconds>\n"
            "    <DistanceMeters>%.1f</DistanceMeters>\n"
            "    <Calories>%u</Calories>\n"
            "    <Intensity>Active</Intensity>\n"
            "    <Cadence>%u</Cadence>\n"
            "    <TriggerMethod>Manual</TriggerMethod>\n"
            "    <Track>\n",
            when, when, static_cast<unsigned long>(m.elapsedSec), m.distanceMeters,
            static_cast<unsigned>(m.energyKcal + 0.5f), clampU8(m.avgStrokeRateSpm));

    for (size_t i = 0; i < g_count; i++) {
        const Sample& s = g_samples[i];
        formatUtc(when, sizeof(when), g_startUtc + static_cast<time_t>(s.sec));
        fprintf(f,
                "     <Trackpoint><Time>%s</Time><DistanceMeters>%.1f</DistanceMeters><Cadence>%u</Cadence>"
                "<Extensions><ns3:TPX><ns3:Watts>%u</ns3:Watts></ns3:TPX></Extensions></Trackpoint>\n",
                when, s.meters, s.spm, s.watts);
    }

    fprintf(f,
            "    </Track>\n"
            "    <Extensions><ns3:LX><ns3:AvgWatts>%u</ns3:AvgWatts></ns3:LX></Extensions>\n"
            "   </Lap>\n"
            "   <Notes>Indoor rowing (Rowing Display)%s</Notes>\n"
            "  </Activity>\n"
            " </Activities>\n"
            "</TrainingCenterDatabase>\n",
            avgWatts, rtc_clock::valid() ? "" : "; clock not set, times start at 2000-01-01");
    return ferror(f) == 0;
}

} // namespace

void start() {
#if !defined(SIMULATION_MODE)
    if (g_samples == nullptr) {
        g_samples = static_cast<Sample*>(heap_caps_malloc(kMaxSamples * sizeof(Sample), MALLOC_CAP_SPIRAM));
        if (g_samples == nullptr) {
            printf("session: sample buffer alloc failed, no session file\n");
        }
    }
#endif
    g_count = 0;
    g_lastSec = UINT32_MAX;
    g_startUtc = 0;
}

void sample(const RowerMetrics& m) {
    if (m.strokeCount == 0 || m.elapsedSec == g_lastSec) {
        return;
    }
    g_lastSec = m.elapsedSec;
    if (g_count == 0) {
        // Without a set clock, start at 2000-01-01 rather than 1970.
        g_startUtc = (rtc_clock::valid() ? time(nullptr) : 946684800) - static_cast<time_t>(m.elapsedSec);
    }
    if (g_samples == nullptr || g_count >= kMaxSamples) {
        return;
    }
    g_samples[g_count++] = Sample{m.elapsedSec, m.distanceMeters, static_cast<uint16_t>(m.powerWatts > 0 ? m.powerWatts : 0),
                                  clampU8(m.strokeRateSpm)};
}

SaveResult save(const RowerMetrics& finalMetrics, void* expander, char* fileName, size_t cap) {
    if (finalMetrics.strokeCount == 0 || g_count == 0) {
        return SaveResult::Empty;
    }
    SaveResult result = SaveResult::NoCard;
    const uint32_t startMs = millis();
    if (sd_card::mount(expander)) {
        printf("session: card mounted in %lu ms\n", static_cast<unsigned long>(millis() - startMs));
        result = SaveResult::Failed;
        mkdir(kDir, 0775);
        char name[40];
        pickFileName(name, sizeof(name));
        char path[64];
        snprintf(path, sizeof(path), "%s/%s", kDir, name);
        FILE* f = fopen(path, "w");
        if (f != nullptr) {
            static char buffer[4096];
            setvbuf(f, buffer, _IOFBF, sizeof(buffer));
            const bool ok = writeTcx(f, finalMetrics);
            if (fclose(f) == 0 && ok) {
                result = SaveResult::Saved;
                snprintf(fileName, cap, "%s", name);
            }
        }
        printf("session: %s %s (%u samples)\n", result == SaveResult::Saved ? "saved" : "writing failed:", path,
               static_cast<unsigned>(g_count));
    }
    sd_card::unmount();
    printf("session: card access took %lu ms\n", static_cast<unsigned long>(millis() - startMs));
    return result;
}

} // namespace session_log
