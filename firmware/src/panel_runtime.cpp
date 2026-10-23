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

bool consumeSessionStartRequested() {
    return false;
}

bool consumeSessionEndRequested() {
    return false;
}

void showSession() {}

void setBanner(const char*) {}

void* ioExpanderBase() {
    return nullptr;
}

void resyncDisplay() {}

} // namespace panel_runtime

#else

#include "esp_display_panel.hpp"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lv_adapter_arduino.h"
#include "pace_boat.h"
#include "rower_scene.h"
#include "sd_backgrounds.h"
#include "settings.h"
#include "settings_screen.h"
#include "ui_theme.h"

#include <cmath>
#include <cstring>

namespace {

using esp_panel::board::Board;
using esp_panel::drivers::BusRGB;

std::shared_ptr<Board> g_board;
esp_panel::drivers::LCD* g_lcd = nullptr;
lv_display_t* g_display = nullptr;

using namespace ui_theme;
// Fallback fill behind the scene where the image doesn't cover.
constexpr lv_color_t kColorWaterDeep = LV_COLOR_MAKE(0x03, 0x08, 0x0C);

constexpr int32_t kPanelW = 800;
constexpr int32_t kPanelH = 480;
constexpr int32_t kSessionPad = 16;
// Metric bands pinned to screen edges so the center stays clear for the
// stern-view rower animation (see rowersession3.mp4 feedback).
constexpr int32_t kTopBarH = 96;
constexpr int32_t kBottomBarH = 88;

constexpr uint32_t kClipFrameMs = 90;

lv_obj_t* g_homeScreen = nullptr;
lv_obj_t* g_homeSubtitle = nullptr;
lv_obj_t* g_settingsScreen = nullptr;
lv_obj_t* g_sessionScreen = nullptr;
lv_obj_t* g_bgImg = nullptr;
lv_draw_buf_t* g_present = nullptr;
lv_timer_t* g_animTimer = nullptr;

// Precomputed stern-view frames: water loop while Idle, stroke poses while rowing.
lv_draw_buf_t* g_idleWaterFrames[rower_scene::kIdleWaterFrameCount] = {};
int g_idleWaterFrameCount = 0;
int g_idleWaterFrameIndex = 0;

lv_draw_buf_t* g_rowerFrames[rower_scene::kStrokeFrameCount] = {};
int g_rowerFrameCount = 0;
RowerPhase g_lastSyncedPhase = RowerPhase::Idle;
// Latest Drive/Recovery cyclePosition, sampled by animTimerCb() at its own
// fixed 90ms cadence rather than redrawn synchronously on every 50ms
// metrics tick - the tighter cadence was enough to underrun the RGB bounce
// buffer (see scratchpad/agents/log.md 2026-08-22 entry for the repro).
float g_cyclePosition = 0.0f;

lv_obj_t* g_splitLabel = nullptr;
lv_obj_t* g_spmLabel = nullptr;
lv_obj_t* g_distanceLabel = nullptr;
lv_obj_t* g_wattsLabel = nullptr;
lv_obj_t* g_timeLabel = nullptr;
lv_obj_t* g_strokesLabel = nullptr;
lv_obj_t* g_avgSplitLabel = nullptr;
lv_obj_t* g_kcalLabel = nullptr;
lv_obj_t* g_banner = nullptr;
lv_obj_t* g_paceLabel = nullptr;

// Pace boat: how far it is ahead of us, set by update() and drawn by
// blitFrame() on the animation timer.
volatile bool g_paceBoatOn = false;
volatile float g_paceBoatGap = 0.0f;
// Its oars swing at a steady 24 spm.
constexpr uint32_t kPaceBoatStrokeMs = 2500;

volatile bool g_sessionStartRequested = false;
volatile bool g_sessionEndRequested = false;

void formatMmSs(char* out, size_t cap, uint32_t totalSec) {
    snprintf(out, cap, "%lu:%02lu", static_cast<unsigned long>(totalSec / 60), static_cast<unsigned long>(totalSec % 60));
}

lv_obj_t* makeTile(lv_obj_t* parent, const char* caption, lv_obj_t** outValueLabel) {
    lv_obj_t* tile = lv_obj_create(parent);
    // Low opacity so the stern scene shows through.
    lv_obj_set_style_bg_color(tile, kColorTile, 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_30, 0);
    lv_obj_set_style_border_width(tile, 0, 0);
    lv_obj_set_style_radius(tile, 12, 0);
    lv_obj_set_style_pad_all(tile, 12, 0);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_flex_grow(tile, 1);
    lv_obj_set_height(tile, LV_PCT(100));
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* captionLabel = lv_label_create(tile);
    lv_label_set_text(captionLabel, caption);
    lv_obj_set_style_text_color(captionLabel, kColorCaption, 0);
    lv_obj_set_style_text_font(captionLabel, &lv_font_montserrat_14, 0);

    lv_obj_t* valueLabel = lv_label_create(tile);
    lv_obj_set_style_text_color(valueLabel, kColorValue, 0);
    lv_obj_set_style_text_font(valueLabel, &lv_font_montserrat_32, 0);
    lv_label_set_text(valueLabel, "--");

    *outValueLabel = valueLabel;
    return tile;
}

lv_obj_t* makeMetricRow(lv_obj_t* parent, int32_t height) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_set_size(row, LV_PCT(100), height);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    // Layout-only container spanning the full width: must not swallow taps
    // meant for the escape button it overlaps.
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
    return row;
}

// "Interactive" background: procedural stern-view scene (see rower_scene.cpp
// and scratchpad/watergen/gen_stroke_phases.py). Frames are drawn once at
// boot into PSRAM, then presented with a cheap integer 2× blit into an
// 800×480 canvas on a fixed 90ms timer - no live lv_gif on the RGB scan.

void up2xRgb565(const lv_draw_buf_t* src, lv_draw_buf_t* dst) {
    const int sw = static_cast<int>(src->header.w);
    const int sh = static_cast<int>(src->header.h);
    if (src->data == nullptr || dst->data == nullptr || dst->header.w < sw * 2 || dst->header.h < sh * 2) {
        return;
    }
    for (int y = 0; y < sh; y++) {
        const auto* srow = reinterpret_cast<const uint16_t*>(src->data + static_cast<uint32_t>(y) * src->header.stride);
        auto* d0 = reinterpret_cast<uint16_t*>(dst->data + static_cast<uint32_t>(y * 2) * dst->header.stride);
        auto* d1 = reinterpret_cast<uint16_t*>(dst->data + static_cast<uint32_t>(y * 2 + 1) * dst->header.stride);
        for (int x = 0; x < sw; x++) {
            const uint16_t p = srow[x];
            d0[x * 2] = p;
            d0[x * 2 + 1] = p;
            d1[x * 2] = p;
            d1[x * 2 + 1] = p;
        }
    }
}

void blitFrame(const lv_draw_buf_t* frame) {
    if (frame == nullptr || g_present == nullptr) {
        return;
    }
    lv_draw_buf_invalidate_cache(frame, nullptr);
    up2xRgb565(frame, g_present);
    if (g_paceBoatOn) {
        const float phase = static_cast<float>(millis() % kPaceBoatStrokeMs) / kPaceBoatStrokeMs;
        pace_boat::draw(g_present, g_paceBoatGap, phase);
    }
    lv_draw_buf_flush_cache(g_present, nullptr);
    if (g_bgImg != nullptr) {
        lv_obj_invalidate(g_bgImg);
    }
}

// 0=catch, 0.5=finish, 1=catch again.
float driveForCyclePosition(float cyclePosition) {
    if (cyclePosition <= 0.5f) {
        return cyclePosition / 0.5f;
    }
    return 1.0f - (cyclePosition - 0.5f) / 0.5f;
}

// 0=catch, 0.5=finish, 1=catch again - phaseProgress only tells us how far
// through the *current* phase we are, so Drive/Recovery each own one half
// of the cycle.
float cyclePositionForMetrics(const RowerMetrics& metrics) {
    switch (metrics.phase) {
        case RowerPhase::Drive:
            return 0.5f * metrics.phaseProgress;
        case RowerPhase::Recovery:
            return 0.5f + 0.5f * metrics.phaseProgress;
        case RowerPhase::Idle:
        default:
            return 0.0f;
    }
}

void drawRowerFrame() {
    if (g_rowerFrameCount <= 0) {
        return;
    }
    int idx = static_cast<int>(g_cyclePosition * static_cast<float>(g_rowerFrameCount - 1) + 0.5f);
    if (idx < 0) {
        idx = 0;
    } else if (idx >= g_rowerFrameCount) {
        idx = g_rowerFrameCount - 1;
    }
    blitFrame(g_rowerFrames[idx]);
}

void blitIdleWaterFrame() {
    if (g_idleWaterFrameCount <= 0) {
        return;
    }
    if (g_idleWaterFrameIndex < 0 || g_idleWaterFrameIndex >= g_idleWaterFrameCount) {
        g_idleWaterFrameIndex = 0;
    }
    blitFrame(g_idleWaterFrames[g_idleWaterFrameIndex]);
}

void precomputeIdleWaterFrames() {
    // Recovery held-pose from gen_stroke_phases.py ("recovery" clip).
    constexpr float kIdleDrive = 0.38f;
    for (int i = 0; i < rower_scene::kIdleWaterFrameCount; i++) {
        lv_draw_buf_t* frame = lv_draw_buf_create(rower_scene::kWidth, rower_scene::kHeight, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
        if (frame == nullptr) {
            printf("panel: idle water frame %d alloc failed\n", i);
            break;
        }
        const float waterTime = static_cast<float>(i) / static_cast<float>(rower_scene::kIdleWaterFrameCount);
        rower_scene::draw(frame, kIdleDrive, waterTime, true);
        g_idleWaterFrames[g_idleWaterFrameCount++] = frame;
    }
    printf("panel: idle water precomputed %d/%d frames\n", g_idleWaterFrameCount, rower_scene::kIdleWaterFrameCount);
}

void precomputeRowerFrames() {
    // Bake water phase into each stroke pose (was waterTime=0 → static water
    // while oars moved). Two water cycles per stroke so ripples stay visible
    // across Drive/Recovery without extra PSRAM frames or live redraw.
    for (int i = 0; i < rower_scene::kStrokeFrameCount; i++) {
        lv_draw_buf_t* frame = lv_draw_buf_create(rower_scene::kWidth, rower_scene::kHeight, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
        if (frame == nullptr) {
            printf("panel: rower frame %d alloc failed\n", i);
            break;
        }
        const float cyclePos = static_cast<float>(i) / static_cast<float>(rower_scene::kStrokeFrameCount - 1);
        const float drive = driveForCyclePosition(cyclePos);
        const bool recovering = cyclePos > 0.5f;
        const float waterTime = std::fmod(cyclePos * 2.0f, 1.0f);
        rower_scene::draw(frame, drive, waterTime, recovering);
        g_rowerFrames[g_rowerFrameCount++] = frame;
    }
    printf("panel: rower precomputed %d/%d frames\n", g_rowerFrameCount, rower_scene::kStrokeFrameCount);
}

void animTimerCb(lv_timer_t* /*t*/) {
    if (g_lastSyncedPhase != RowerPhase::Idle) {
        drawRowerFrame();
        return;
    }
    if (g_idleWaterFrameCount <= 0) {
        return;
    }
    g_idleWaterFrameIndex = (g_idleWaterFrameIndex + 1) % g_idleWaterFrameCount;
    blitIdleWaterFrame();
}

void syncBackgroundToPhase(RowerPhase phase) {
    g_lastSyncedPhase = phase;
    if (phase == RowerPhase::Idle) {
        g_idleWaterFrameIndex = 0;
        blitIdleWaterFrame();
    } else {
        drawRowerFrame();
    }
}

void startScene() {
    if (g_bgImg != nullptr) {
        lv_obj_clear_flag(g_bgImg, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_background(g_bgImg);
    }
    syncBackgroundToPhase(RowerPhase::Idle);
    if (g_animTimer != nullptr) {
        lv_timer_resume(g_animTimer);
    }
}

// Off the session screen the 90 ms frame blits only cost render time.
void stopScene() {
    if (g_animTimer != nullptr) {
        lv_timer_pause(g_animTimer);
    }
}

void showScreen(lv_obj_t* screen) {
    // Plain load, no fade transition: LV_SCR_LOAD_ANIM_FADE_IN combined
    // with this board's RGB full-refresh (framebuffer-switching) render
    // mode produced a stray glyph on first use - untested combination,
    // dropped rather than chased further.
    lv_screen_load(screen);
}

void onStartClicked(lv_event_t* /*e*/) {
    startScene();
    g_sessionStartRequested = true;
    showScreen(g_sessionScreen);
}

void onEscapeClicked(lv_event_t* /*e*/) {
    g_sessionEndRequested = true;
    stopScene();
    showScreen(g_homeScreen);
}

void setHomeSubtitle(const settings::Settings& s) {
    lv_label_set_text_fmt(g_homeSubtitle, "%s  |  %s", settings::machineName(s.machine), settings::sensorName(s.sensor));
}

void onSettingsDone(const settings::Settings& inEffect) {
    setHomeSubtitle(inEffect);
    if (strcmp(inEffect.background, settings::current().background) != 0) {
        // The main loop saves, then restarts to load it (main.cpp).
        lv_label_set_text(g_homeSubtitle, "Restarting to load the background...");
    }
    showScreen(g_homeScreen);
}

void onSettingsClicked(lv_event_t* /*e*/) {
    settings_screen::load();
    showScreen(g_settingsScreen);
}

lv_obj_t* makeMenuButton(lv_obj_t* parent, const char* text, lv_event_cb_t onClick) {
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_set_style_bg_color(btn, kColorTile, 0);
    lv_obj_set_style_bg_color(btn, kColorAccent, LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 16, 0);
    lv_obj_set_size(btn, LV_PCT(80), 90);
    lv_obj_add_event_cb(btn, onClick, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, kColorValue, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_center(label);

    return btn;
}

void buildHomeScreen() {
    g_homeScreen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(g_homeScreen, kColorBg, 0);
    lv_obj_set_flex_flow(g_homeScreen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_homeScreen, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(g_homeScreen, 24, 0);
    lv_obj_clear_flag(g_homeScreen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(g_homeScreen);
    lv_label_set_text(title, "Let's get rowing!");
    lv_obj_set_style_text_color(title, kColorAccent, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_48, 0);

    g_homeSubtitle = lv_label_create(g_homeScreen);
    lv_obj_set_style_text_color(g_homeSubtitle, kColorHint, 0);
    lv_obj_set_style_text_font(g_homeSubtitle, &lv_font_montserrat_24, 0);
    setHomeSubtitle(settings::current());

    makeMenuButton(g_homeScreen, "START SESSION", onStartClicked);
    makeMenuButton(g_homeScreen, "SETTINGS", onSettingsClicked);
}

void buildSessionScreen() {
    g_sessionScreen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(g_sessionScreen, kColorWaterDeep, 0);
    lv_obj_set_style_pad_all(g_sessionScreen, kSessionPad, 0);
    lv_obj_clear_flag(g_sessionScreen, LV_OBJ_FLAG_SCROLLABLE);

    g_present = lv_draw_buf_create(kPanelW, kPanelH, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
    if (g_present == nullptr) {
        printf("panel: 800x480 present alloc failed\n");
    }
    // An SD card background is read into g_present: that buffer is only used
    // once a session starts, by which time the frames are rendered and the
    // background pixels aren't needed any more. PSRAM has no room to spare.
    static scene_background::Background sdBackground;
    constexpr size_t kBackgroundBytes = sizeof(uint16_t) * scene_background::kWidth * scene_background::kHeight;
    uint16_t* sdPixels = g_present != nullptr && g_present->data_size >= kBackgroundBytes
                             ? reinterpret_cast<uint16_t*>(g_present->data)
                             : nullptr;
    // Also lists the card's backgrounds for Settings, whatever is selected.
    if (sd_backgrounds::begin(panel_runtime::ioExpanderBase(), settings::current().background, sdPixels, sdBackground)) {
        rower_scene::setBackground(sdBackground);
    } else {
        rower_scene::setBackground(scene_background::kDefault);
    }

    const uint32_t precomputeStartMs = millis();
    precomputeIdleWaterFrames();
    precomputeRowerFrames();
    rower_scene::releaseCanvas();
    printf("panel: scene frames rendered in %lu ms\n", static_cast<unsigned long>(millis() - precomputeStartMs));

    g_bgImg = lv_image_create(g_sessionScreen);
    if (g_present != nullptr) {
        lv_image_set_src(g_bgImg, g_present);
    }
    lv_obj_set_size(g_bgImg, kPanelW, kPanelH);
    lv_obj_add_flag(g_bgImg, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(g_bgImg, LV_OBJ_FLAG_FLOATING);
    lv_obj_clear_flag(g_bgImg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(g_bgImg, -kSessionPad, -kSessionPad);
    lv_obj_move_background(g_bgImg);
    lv_obj_add_flag(g_bgImg, LV_OBJ_FLAG_HIDDEN);
    g_animTimer = lv_timer_create(animTimerCb, kClipFrameMs, nullptr);
    lv_timer_pause(g_animTimer);

    // Escape button: always visible, returns to the home screen. Kept
    // large for a first pass at touch since this is the first touch UI in
    // this project - worth erring toward "easy to hit" over "compact".
    lv_obj_t* escapeBtn = lv_button_create(g_sessionScreen);
    lv_obj_remove_style_all(escapeBtn);
    lv_obj_set_style_bg_color(escapeBtn, kColorTile, 0);
    lv_obj_set_style_bg_opa(escapeBtn, LV_OPA_80, 0);
    lv_obj_set_style_radius(escapeBtn, 8, 0);
    lv_obj_set_size(escapeBtn, 56, 56);
    lv_obj_align(escapeBtn, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_add_flag(escapeBtn, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_event_cb(escapeBtn, onEscapeClicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_set_ext_click_area(escapeBtn, 16);

    lv_obj_t* escapeLabel = lv_label_create(escapeBtn);
    lv_label_set_text(escapeLabel, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(escapeLabel, kColorValue, 0);
    lv_obj_center(escapeLabel);

    // Top band: split (primary) + cadence + power. Pinned to the top edge
    // so tiles never sit over the rower's torso in interactive mode.
    lv_obj_t* topBar = makeMetricRow(g_sessionScreen, kTopBarH);
    lv_obj_align(topBar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_add_flag(topBar, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_pad_right(topBar, 64, 0);

    lv_obj_t* splitBlock = lv_obj_create(topBar);
    lv_obj_set_style_bg_opa(splitBlock, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(splitBlock, 0, 0);
    lv_obj_set_style_pad_all(splitBlock, 0, 0);
    lv_obj_set_flex_flow(splitBlock, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_grow(splitBlock, 1);
    lv_obj_clear_flag(splitBlock, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* splitCaption = lv_label_create(splitBlock);
    lv_label_set_text(splitCaption, "SPLIT /500m");
    lv_obj_set_style_text_color(splitCaption, kColorCaption, 0);
    lv_obj_set_style_text_font(splitCaption, &lv_font_montserrat_14, 0);

    g_splitLabel = lv_label_create(splitBlock);
    lv_obj_set_style_text_color(g_splitLabel, kColorAccent, 0);
    lv_obj_set_style_text_font(g_splitLabel, &lv_font_montserrat_48, 0);
    lv_label_set_text(g_splitLabel, "-:--");

    g_avgSplitLabel = lv_label_create(splitBlock);
    lv_obj_set_style_text_color(g_avgSplitLabel, kColorCaption, 0);
    lv_obj_set_style_text_font(g_avgSplitLabel, &lv_font_montserrat_14, 0);
    lv_label_set_text(g_avgSplitLabel, "avg -:--");

    makeTile(topBar, "SPM", &g_spmLabel);
    makeTile(topBar, "WATTS", &g_wattsLabel);

    // Bottom band: session time, stroke count, distance.
    lv_obj_t* bottomBar = makeMetricRow(g_sessionScreen, kBottomBarH);
    lv_obj_align(bottomBar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(bottomBar, LV_OBJ_FLAG_IGNORE_LAYOUT);

    makeTile(bottomBar, "TIME", &g_timeLabel);
    makeTile(bottomBar, "STROKES", &g_strokesLabel);
    makeTile(bottomBar, "METERS", &g_distanceLabel);
    makeTile(bottomBar, "KCAL", &g_kcalLabel);

    g_banner = lv_label_create(g_sessionScreen);
    lv_obj_add_flag(g_banner, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_text_font(g_banner, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_align(g_banner, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(g_banner, kColorValue, 0);
    lv_obj_set_style_bg_color(g_banner, kColorTile, 0);
    lv_obj_set_style_bg_opa(g_banner, LV_OPA_80, 0);
    lv_obj_set_style_radius(g_banner, 12, 0);
    lv_obj_set_style_pad_hor(g_banner, 24, 0);
    lv_obj_set_style_pad_ver(g_banner, 8, 0);
    lv_obj_align(g_banner, LV_ALIGN_CENTER, 0, -40);
    lv_obj_add_flag(g_banner, LV_OBJ_FLAG_HIDDEN);

    g_paceLabel = lv_label_create(g_sessionScreen);
    lv_obj_add_flag(g_paceLabel, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_text_font(g_paceLabel, &lv_font_montserrat_24, 0);
    lv_obj_set_style_bg_color(g_paceLabel, kColorTile, 0);
    lv_obj_set_style_bg_opa(g_paceLabel, LV_OPA_60, 0);
    lv_obj_set_style_radius(g_paceLabel, 8, 0);
    lv_obj_set_style_pad_hor(g_paceLabel, 10, 0);
    lv_obj_set_style_pad_ver(g_paceLabel, 4, 0);
    lv_obj_align(g_paceLabel, LV_ALIGN_TOP_RIGHT, 0, kTopBarH + 8);
    lv_obj_add_flag(g_paceLabel, LV_OBJ_FLAG_HIDDEN);

    // The top bar is created after the escape button and overlaps it; keep
    // the button on top so it gets the touch.
    lv_obj_move_foreground(escapeBtn);
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

    g_lcd = g_board->getLCD();
    if (g_lcd == nullptr) {
        printf("panel: lcd not available\n");
        return false;
    }

    // Double-buffer the RGB bus and give it a bounce buffer before begin();
    // this is what avoids the screen-tear/drift this board's own docs warn
    // about with a naive single-buffer RGB setup, and is required for
    // esp_lv_adapter_arduino's full-refresh (framebuffer-switching) path.
    if (g_lcd->getBus()->getBasicAttributes().type == ESP_PANEL_BUS_TYPE_RGB) {
        g_lcd->configFrameBufferNumber(2);
        // Vendor docs use width*20 when LVGL work contends with the RGB
        // scan; width*10 was underrunning once a GIF was being decoded
        // (scanline garbage). Still double-buffered full-refresh.
        static_cast<BusRGB*>(g_lcd->getBus())->configRGB_BounceBufferSize(g_lcd->getFrameWidth() * 20);
    }

    if (!g_board->begin()) {
        printf("panel: board begin failed\n");
        return false;
    }

    esp_lv_adapter_config_t adapterConfig = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    adapterConfig.task_stack_size = 12 * 1024;
    adapterConfig.task_priority = 2;
    adapterConfig.task_core_id = ARDUINO_RUNNING_CORE;
    if (esp_lv_adapter_init(&adapterConfig) != ESP_OK) {
        printf("panel: lvgl adapter init failed\n");
        return false;
    }

    esp_lv_adapter_display_config_t dispConfig = ESP_LV_ADAPTER_DISPLAY_RGB_DEFAULT_CONFIG(
        g_lcd, g_lcd->getFrameWidth(), g_lcd->getFrameHeight(), ESP_LV_ADAPTER_ROTATE_0
    );
    dispConfig.profile.use_psram = true;
    g_display = esp_lv_adapter_register_display(&dispConfig);
    if (g_display == nullptr) {
        printf("panel: lvgl display register failed\n");
        return false;
    }

    esp_panel::drivers::Touch* touch = g_board->getTouch();
    if (touch != nullptr) {
        esp_lv_adapter_touch_config_t touchConfig = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(g_display, touch);
        esp_lv_adapter_register_touch(&touchConfig);
    }

    if (esp_lv_adapter_start() != ESP_OK) {
        printf("panel: lvgl adapter start failed\n");
        return false;
    }

    esp_lv_adapter_lock(-1);
    buildHomeScreen();
    buildSessionScreen();
    g_settingsScreen = settings_screen::build(onSettingsDone);
    lv_screen_load(g_homeScreen);
    esp_lv_adapter_unlock();

    printf("panel: ready\n");
    return true;
}

void update(const RowerMetrics& metrics) {
    if (g_display == nullptr) {
        return;
    }

    // Sample rowing phase for animTimerCb() outside the LVGL lock. The 90ms
    // animation timer runs inside lv_timer_handler() and can hold the mutex
    // for a full 800×480 blit; a non-blocking lock(0) here meant metrics and
    // phase never reached the UI while water animation still ran.
    if (metrics.phase == RowerPhase::Idle) {
        if (metrics.phase != g_lastSyncedPhase) {
            g_lastSyncedPhase = RowerPhase::Idle;
            g_idleWaterFrameIndex = 0;
        }
    } else {
        g_lastSyncedPhase = metrics.phase;
        g_cyclePosition = cyclePositionForMetrics(metrics);
    }

    // Pace boat position: session time at the target split, interpolated
    // between whole seconds so it glides instead of jumping once a second.
    static uint32_t lastElapsedSec = UINT32_MAX;
    static uint32_t elapsedChangedMs = 0;
    const uint16_t paceSplitSec = settings::current().paceBoatSplitSec;
    float paceDelta = 0.0f; // metres we're ahead of the pace boat
    if (paceSplitSec > 0) {
        const uint32_t now = millis();
        if (metrics.elapsedSec != lastElapsedSec) {
            lastElapsedSec = metrics.elapsedSec;
            elapsedChangedMs = now;
        }
        float sec = static_cast<float>(metrics.elapsedSec);
        if (!metrics.paused && metrics.elapsedSec > 0) {
            sec += std::min(1.0f, static_cast<float>(now - elapsedChangedMs) / 1000.0f);
        }
        const float paceMeters = sec * 500.0f / static_cast<float>(paceSplitSec);
        paceDelta = metrics.distanceMeters - paceMeters;
        g_paceBoatGap = -paceDelta;
    }
    g_paceBoatOn = paceSplitSec > 0;

    char buf[16];

    if (esp_lv_adapter_lock(10) != ESP_OK) {
        // Labels can wait briefly; animation state above is already synced.
        return;
    }

    if (metrics.splitSecPer500m > 0.0f) {
        formatMmSs(buf, sizeof(buf), static_cast<uint32_t>(metrics.splitSecPer500m));
    } else {
        snprintf(buf, sizeof(buf), "-:--");
    }
    lv_label_set_text(g_splitLabel, buf);

    snprintf(buf, sizeof(buf), "%.0f", metrics.strokeRateSpm);
    lv_label_set_text(g_spmLabel, buf);

    snprintf(buf, sizeof(buf), "%.0f", metrics.distanceMeters);
    lv_label_set_text(g_distanceLabel, buf);

    snprintf(buf, sizeof(buf), "%.0f", metrics.powerWatts);
    lv_label_set_text(g_wattsLabel, buf);

    formatMmSs(buf, sizeof(buf), metrics.elapsedSec);
    lv_label_set_text(g_timeLabel, buf);

    snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(metrics.strokeCount));
    lv_label_set_text(g_strokesLabel, buf);

    snprintf(buf, sizeof(buf), "%.0f", metrics.energyKcal);
    lv_label_set_text(g_kcalLabel, buf);

    if (paceSplitSec > 0) {
        char pace[12];
        formatMmSs(pace, sizeof(pace), paceSplitSec);
        lv_label_set_text_fmt(g_paceLabel, "PACE %s  %+.0f m", pace, paceDelta);
        lv_obj_set_style_text_color(g_paceLabel, paceDelta >= 0.0f ? kColorAccent : lv_color_hex(0xFF9F43), 0);
        lv_obj_clear_flag(g_paceLabel, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(g_paceLabel, LV_OBJ_FLAG_HIDDEN);
    }

    if (metrics.avgSplitSecPer500m > 0.0f) {
        char pace[12];
        formatMmSs(pace, sizeof(pace), static_cast<uint32_t>(metrics.avgSplitSecPer500m));
        lv_label_set_text_fmt(g_avgSplitLabel, "avg %s  |  %.0f spm", pace, metrics.avgStrokeRateSpm);
    } else {
        lv_label_set_text(g_avgSplitLabel, "avg -:--");
    }

    esp_lv_adapter_unlock();
}

bool consumeSessionEndRequested() {
    if (!g_sessionEndRequested) {
        return false;
    }
    g_sessionEndRequested = false;
    return true;
}

void showSession() {
    if (g_display == nullptr || esp_lv_adapter_lock(-1) != ESP_OK) {
        return;
    }
    if (lv_screen_active() != g_sessionScreen) {
        startScene();
        showScreen(g_sessionScreen);
    }
    esp_lv_adapter_unlock();
}

void setBanner(const char* text) {
    if (g_banner == nullptr || esp_lv_adapter_lock(-1) != ESP_OK) {
        return;
    }
    if (text == nullptr) {
        lv_obj_add_flag(g_banner, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(g_banner, text);
        lv_obj_clear_flag(g_banner, LV_OBJ_FLAG_HIDDEN);
    }
    esp_lv_adapter_unlock();
}

bool consumeSessionStartRequested() {
    if (!g_sessionStartRequested) {
        return false;
    }
    g_sessionStartRequested = false;
    return true;
}

void* ioExpanderBase() {
    if (!g_board) {
        return nullptr;
    }
    auto* expander = g_board->getIO_Expander();
    return expander ? expander->getBase() : nullptr;
}

void resyncDisplay() {
    if (g_lcd != nullptr && g_lcd->getBus()->getBasicAttributes().type == ESP_PANEL_BUS_TYPE_RGB) {
        esp_lcd_rgb_panel_restart(g_lcd->getHandle());
    }
}

} // namespace panel_runtime

#endif
