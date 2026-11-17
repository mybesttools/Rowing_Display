#include "pulse_log.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_partition.h"

namespace pulse_log {

namespace {

// The partition is a ring of 4 KB sectors. Each starts with a header naming
// the log it belongs to (seq) and its place in that log (index), so the
// fetch script can put the logs back together wherever they wrapped.
constexpr char kMagic[4] = {'R', 'L', 'G', '2'};
struct SectorHeader {
    char magic[4];
    uint32_t seq;
    uint32_t index;
};
constexpr size_t kSectorSize = 4096;
constexpr uint32_t kFree = 0xFFFFFFFF;
// Oldest logs are erased once there are more than this many.
constexpr uint32_t kKeepLogs = 7;

// Only written to flash between sessions: a flash write while rowing makes
// the RGB panel drift and resyncDisplay() does not always bring it back
// (2026-10-29: forced flush 8.8 min into a session, frame stayed shifted).
// Full detail for ~4 min of rowing, then only pulses and session events
// (~27 B/s) until full, ~20 min more; what does not fit is dropped.
constexpr size_t kBufSize = 64 * 1024;
constexpr size_t kPulsesOnlyAt = kBufSize / 2;
constexpr size_t kIdleFlushAt = 8 * 1024;
constexpr uint32_t kIdleFlushIntervalMs = 60000;

const esp_partition_t* g_part = nullptr;
size_t g_sectorCount = 0;
uint32_t* g_sectorSeq = nullptr; // per sector: the log it holds, or kFree
uint32_t g_lastSeq = 0;
size_t g_nextSector = 0;

// This boot's log; 0 until it first has rowing in it, so boots without any
// don't push real sessions out.
uint32_t g_seq = 0;
uint32_t g_index = 0;
size_t g_sector = 0;
size_t g_fill = kSectorSize;
bool g_hasRowing = false;

char* g_buf = nullptr;
size_t g_len = 0;
uint32_t g_lastFlushMs = 0;
bool g_wasQuiet = true;
uint32_t g_dropped = 0;

void eraseLog(uint32_t seq) {
    for (size_t s = 0; s < g_sectorCount; s++) {
        if (g_sectorSeq[s] == seq) {
            esp_partition_erase_range(g_part, s * kSectorSize, kSectorSize);
            g_sectorSeq[s] = kFree;
        }
    }
}

uint32_t oldestLog() {
    uint32_t oldest = kFree;
    for (size_t s = 0; s < g_sectorCount; s++) {
        oldest = std::min(oldest, g_sectorSeq[s]);
    }
    return oldest;
}

uint32_t logCount() {
    uint32_t count = 0;
    for (size_t s = 0; s < g_sectorCount; s++) {
        if (g_sectorSeq[s] == kFree) {
            continue;
        }
        bool seen = false;
        for (size_t t = 0; t < s && !seen; t++) {
            seen = g_sectorSeq[t] == g_sectorSeq[s];
        }
        count += seen ? 0 : 1;
    }
    return count;
}

void startSector(size_t s) {
    // The ring caught up with the oldest log: drop all of it, not just this
    // sector. A log that fills the whole partition overwrites its own start.
    if (g_sectorSeq[s] != kFree && g_sectorSeq[s] != g_seq) {
        eraseLog(g_sectorSeq[s]);
    }
    esp_partition_erase_range(g_part, s * kSectorSize, kSectorSize);
    SectorHeader header;
    memcpy(header.magic, kMagic, sizeof(kMagic));
    header.seq = g_seq;
    header.index = g_index++;
    esp_partition_write(g_part, s * kSectorSize, &header, sizeof(header));
    g_sectorSeq[s] = g_seq;
    g_sector = s;
    g_fill = sizeof(header);
    g_nextSector = (s + 1) % g_sectorCount;
}

void openLog() {
    while (logCount() >= kKeepLogs) {
        eraseLog(oldestLog());
    }
    g_seq = ++g_lastSeq;
    g_index = 0;
    startSector(g_nextSector);
}

bool flush() {
    if (!g_sectorSeq || g_len == 0) {
        return false;
    }
    if (g_seq == 0) {
        if (!g_hasRowing) {
            return false; // keep the boot lines until there is rowing to log
        }
        openLog();
    }
    size_t off = 0;
    while (off < g_len) {
        if (g_fill == kSectorSize) {
            startSector(g_nextSector);
        }
        const size_t n = std::min(g_len - off, kSectorSize - g_fill);
        esp_partition_write(g_part, g_sector * kSectorSize + g_fill, g_buf + off, n);
        g_fill += n;
        off += n;
    }
    g_len = 0;
    return true;
}

} // namespace

void begin() {
    g_buf = static_cast<char*>(heap_caps_malloc(kBufSize, MALLOC_CAP_SPIRAM));
    if (!g_buf) {
        ::printf("pulse_log: no buffer, logging disabled\n");
        return;
    }
    g_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
    if (!g_part) {
        ::printf("pulse_log: no spiffs partition, logging disabled\n");
        return;
    }
    g_sectorCount = g_part->size / kSectorSize;
    g_sectorSeq = static_cast<uint32_t*>(malloc(g_sectorCount * sizeof(uint32_t)));
    if (!g_sectorSeq) {
        ::printf("pulse_log: no sector table, logging disabled\n");
        return;
    }

    bool foreign = false;
    for (size_t s = 0; s < g_sectorCount; s++) {
        SectorHeader header;
        esp_partition_read(g_part, s * kSectorSize, &header, sizeof(header));
        if (memcmp(header.magic, kMagic, sizeof(kMagic)) == 0) {
            g_sectorSeq[s] = header.seq;
        } else {
            g_sectorSeq[s] = kFree;
            const auto* bytes = reinterpret_cast<const uint8_t*>(&header);
            foreign |= std::any_of(bytes, bytes + sizeof(header), [](uint8_t b) { return b != 0xFF; });
        }
    }
    if (foreign) {
        // Older single-log format, or something else entirely.
        ::printf("pulse_log: formatting %lu KB log partition...\n", static_cast<unsigned long>(g_part->size / 1024));
        esp_partition_erase_range(g_part, 0, g_part->size);
        std::fill(g_sectorSeq, g_sectorSeq + g_sectorCount, kFree);
    }

    // Carry on after the newest log's last sector.
    uint32_t newestIndex = 0;
    for (size_t s = 0; s < g_sectorCount; s++) {
        if (g_sectorSeq[s] == kFree || g_sectorSeq[s] < g_lastSeq) {
            continue;
        }
        SectorHeader header;
        esp_partition_read(g_part, s * kSectorSize, &header, sizeof(header));
        if (g_sectorSeq[s] > g_lastSeq || header.index >= newestIndex) {
            g_lastSeq = g_sectorSeq[s];
            newestIndex = header.index;
            g_nextSector = (s + 1) % g_sectorCount;
        }
    }

    size_t used = 0;
    for (size_t s = 0; s < g_sectorCount; s++) {
        used += g_sectorSeq[s] == kFree ? 0 : 1;
    }
    ::printf("pulse_log: %lu log(s), %lu/%lu KB used\n", static_cast<unsigned long>(logCount()),
             static_cast<unsigned long>(used * kSectorSize / 1024), static_cast<unsigned long>(g_part->size / 1024));
}

void printf(const char* fmt, ...) {
    if (!g_sectorSeq) {
        return;
    }
    char line[160];
    va_list args;
    va_start(args, fmt);
    const int n = vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    if (n <= 0) {
        return;
    }
    const size_t len = std::min(static_cast<size_t>(n), sizeof(line) - 1);
    const bool event = line[0] == 'P' || line[0] == 'R' || line[0] == 'B' || line[0] == 'C';
    if (g_len + len > kBufSize || (g_len >= kPulsesOnlyAt && !event)) {
        g_dropped++;
        return;
    }
    g_hasRowing |= line[0] == 'P';
    memcpy(g_buf + g_len, line, len);
    g_len += len;
}

bool poll(uint32_t nowMs, bool quiet) {
    const bool sessionEnded = quiet && !g_wasQuiet;
    g_wasQuiet = quiet;
    if (g_len == 0) {
        return false;
    }
    const bool idleDue = quiet && (g_len >= kIdleFlushAt || nowMs - g_lastFlushMs >= kIdleFlushIntervalMs);
    if (!(sessionEnded || idleDue)) {
        return false;
    }
    if (g_dropped > 0 && g_len + 32 <= kBufSize) {
        g_len += snprintf(g_buf + g_len, 32, "D,%lu,%lu\n", static_cast<unsigned long>(nowMs),
                          static_cast<unsigned long>(g_dropped));
        g_dropped = 0;
    }
    if (!flush()) {
        return false;
    }
    g_lastFlushMs = nowMs;
    return true;
}

} // namespace pulse_log
