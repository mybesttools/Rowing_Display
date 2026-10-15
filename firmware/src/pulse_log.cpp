#include "pulse_log.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_partition.h"

namespace pulse_log {

namespace {

constexpr char kHeader[] = "ROWLOG1\n";
constexpr size_t kHeaderLen = sizeof(kHeader) - 1;
// Big enough for ~5 min of rowing, so a session is written once it ends.
constexpr size_t kBufSize = 64 * 1024;
constexpr size_t kForceFlushAt = kBufSize - 4096;
constexpr size_t kIdleFlushAt = 8 * 1024;
constexpr uint32_t kIdleFlushIntervalMs = 60000;

const esp_partition_t* g_part = nullptr;
size_t g_offset = 0;
bool g_full = false;
char* g_buf = nullptr;
size_t g_len = 0;
uint32_t g_lastFlushMs = 0;
bool g_wasQuiet = true;

size_t findAppendOffset() {
    uint8_t chunk[1024];
    for (size_t off = kHeaderLen; off < g_part->size; off += sizeof(chunk)) {
        const size_t n = std::min<size_t>(sizeof(chunk), g_part->size - off);
        if (esp_partition_read(g_part, off, chunk, n) != ESP_OK) {
            return g_part->size;
        }
        for (size_t i = 0; i < n; i++) {
            if (chunk[i] == 0xFF) {
                return off + i;
            }
        }
    }
    return g_part->size;
}

void flush() {
    if (!g_part || g_full || g_len == 0) {
        g_len = 0;
        return;
    }
    if (g_offset + g_len > g_part->size) {
        g_full = true;
        g_len = 0;
        return;
    }
    if (esp_partition_write(g_part, g_offset, g_buf, g_len) == ESP_OK) {
        g_offset += g_len;
    }
    g_len = 0;
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

    char header[kHeaderLen];
    esp_partition_read(g_part, 0, header, kHeaderLen);
    if (memcmp(header, kHeader, kHeaderLen) != 0) {
        ::printf("pulse_log: formatting %lu KB log partition...\n", static_cast<unsigned long>(g_part->size / 1024));
        esp_partition_erase_range(g_part, 0, g_part->size);
        esp_partition_write(g_part, 0, kHeader, kHeaderLen);
    }

    g_offset = findAppendOffset();
    g_full = g_offset >= g_part->size;
    ::printf("pulse_log: %lu/%lu KB used\n", static_cast<unsigned long>(g_offset / 1024),
             static_cast<unsigned long>(g_part->size / 1024));
}

void printf(const char* fmt, ...) {
    if (!g_part || g_full) {
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
    if (g_len + len > kBufSize) {
        flush();
    }
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
    if (!(sessionEnded || idleDue || g_len >= kForceFlushAt)) {
        return false;
    }
    flush();
    g_lastFlushMs = nowMs;
    return true;
}

} // namespace pulse_log
