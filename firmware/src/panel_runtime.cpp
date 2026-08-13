#include "panel_runtime.h"

#include <Arduino.h>
#include <cstdio>
#ifdef SIMULATION_MODE

namespace panel_runtime {

bool begin() {
    printf("panel(sim): mock panel runtime enabled\n");
    return true;
}

void update(const RowerMetrics& metrics) {
    static uint32_t lastLogMs = 0;
    const uint32_t now = millis();
    if (now - lastLogMs >= 1000) {
        lastLogMs = now;
        printf("panel(sim): rpm=%.1f spm=%.1f watts=%.0f\n", metrics.rpm, metrics.strokeRateSpm, metrics.powerWatts);
    }
}

} // namespace panel_runtime

#else

#include <memory>
#include <vector>

#include "esp_display_panel.hpp"

namespace {

using esp_panel::board::Board;
using esp_panel::drivers::TouchPoint;

std::shared_ptr<Board> g_board;
esp_panel::drivers::LCD* g_lcd = nullptr;
esp_panel::drivers::Touch* g_touch = nullptr;

constexpr int kCanvasW = 320;
constexpr int kCanvasH = 120;
// This board's RGB bus is configured for RGB565 (ESP_PANEL_BOARD_LCD_RGB_PIXEL_BITS
// in the board profile) — esp_lcd_panel_draw_bitmap() memcpy's this buffer straight
// into the framebuffer with no conversion, so it must already be 2 bytes/pixel in
// that exact format, not RGB888.
std::vector<uint16_t> g_rgb565Canvas;

uint16_t toRgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

void setPixelRgb565(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if (x < 0 || y < 0 || x >= kCanvasW || y >= kCanvasH) {
        return;
    }
    g_rgb565Canvas[static_cast<size_t>(y * kCanvasW + x)] = toRgb565(r, g, b);
}

void fillRect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    for (int yy = y; yy < y + h; ++yy) {
        for (int xx = x; xx < x + w; ++xx) {
            setPixelRgb565(xx, yy, r, g, b);
        }
    }
}

int clampBar(float value, float maxValue) {
    if (value < 0.0f) {
        value = 0.0f;
    }
    if (value > maxValue) {
        value = maxValue;
    }
    return static_cast<int>((value / maxValue) * 280.0f);
}

void drawBars(const RowerMetrics& m) {
    fillRect(0, 0, kCanvasW, kCanvasH, 10, 12, 18);

    // Three quick-read bars: RPM (cyan), SPM (green), WATTS (orange)
    const int rpmLen = clampBar(m.rpm, 160.0f);
    const int spmLen = clampBar(m.strokeRateSpm, 60.0f);
    const int wLen = clampBar(m.powerWatts, 600.0f);

    fillRect(20, 20, 280, 20, 30, 35, 48);
    fillRect(20, 50, 280, 20, 30, 35, 48);
    fillRect(20, 80, 280, 20, 30, 35, 48);

    fillRect(20, 20, rpmLen, 20, 40, 180, 220);
    fillRect(20, 50, spmLen, 20, 90, 220, 90);
    fillRect(20, 80, wLen, 20, 235, 140, 50);
}

void pollTouch() {
    if (g_touch == nullptr) {
        return;
    }

    TouchPoint points[5];
    const int count = g_touch->readPoints(points, 5, 0);
    if (count > 0) {
        printf("touch=%d p0(%d,%d)\n", count, points[0].x, points[0].y);
    }
}

} // namespace

namespace panel_runtime {

bool begin() {
    g_board = std::make_shared<Board>();
    if (!g_board) {
        printf("panel: board alloc failed\n");
        return false;
    }

    if (!g_board->init()) {
        printf("panel: board init failed\n");
        return false;
    }
    if (!g_board->begin()) {
        printf("panel: board begin failed\n");
        return false;
    }

    g_lcd = g_board->getLCD();
    g_touch = g_board->getTouch();

    if (g_lcd == nullptr) {
        printf("panel: lcd not available\n");
        return false;
    }

    // First-frame sanity check from library.
    g_lcd->colorBarTest();

    g_rgb565Canvas.resize(static_cast<size_t>(kCanvasW * kCanvasH));
    printf("panel: ready\n");
    return true;
}

void update(const RowerMetrics& metrics) {
    if (g_lcd == nullptr) {
        return;
    }

    drawBars(metrics);
    g_lcd->drawBitmap(0, 0, kCanvasW, kCanvasH, reinterpret_cast<const uint8_t*>(g_rgb565Canvas.data()), 0);
    pollTouch();
}

} // namespace panel_runtime

#endif
