#include "wifi_screen.h"

#include <cstdio>
#include <cstring>

#include "net_time.h"
#include "ui_theme.h"

namespace wifi_screen {

namespace {

using namespace ui_theme;

DoneCallback g_onDone = nullptr;
lv_obj_t* g_status = nullptr;
lv_obj_t* g_listPage = nullptr;
lv_obj_t* g_list = nullptr;
lv_obj_t* g_passwordPage = nullptr;
lv_obj_t* g_networkLabel = nullptr;
lv_obj_t* g_password = nullptr;
lv_obj_t* g_showButtonLabel = nullptr;
char g_ssid[33] = "";
bool g_listShown = false;

lv_obj_t* makeButton(lv_obj_t* parent, const char* text, int32_t w, lv_event_cb_t cb, lv_obj_t** labelOut = nullptr) {
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_set_style_bg_color(btn, kColorTile, 0);
    lv_obj_set_style_bg_color(btn, kColorAccent, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, kColorHint, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_size(btn, w, 56);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, kColorValue, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_center(label);
    if (labelOut != nullptr) {
        *labelOut = label;
    }
    return btn;
}

void showPage(bool passwordPage) {
    if (passwordPage) {
        lv_obj_add_flag(g_listPage, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_passwordPage, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(g_passwordPage, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_listPage, LV_OBJ_FLAG_HIDDEN);
    }
}

void onNetwork(lv_event_t* e) {
    const intptr_t i = reinterpret_cast<intptr_t>(lv_event_get_user_data(e));
    snprintf(g_ssid, sizeof(g_ssid), "%s", net_time::networkName(static_cast<int>(i)));
    lv_label_set_text_fmt(g_networkLabel, "Password for %s", g_ssid);
    lv_textarea_set_text(g_password, "");
    showPage(true);
}

void fillList() {
    lv_obj_clean(g_list);
    const int n = net_time::networkCount();
    for (int i = 0; i < n; i++) {
        const int rssi = net_time::networkRssi(i);
        const char* bars = rssi > -60 ? "strong" : (rssi > -72 ? "good" : "weak");
        char text[64];
        snprintf(text, sizeof(text), "%s   (%s)", net_time::networkName(i), bars);
        lv_obj_t* btn = lv_list_add_button(g_list, LV_SYMBOL_WIFI, text);
        lv_obj_set_style_text_font(btn, &lv_font_montserrat_24, 0);
        lv_obj_set_style_bg_color(btn, kColorTile, 0);
        lv_obj_set_style_text_color(btn, kColorValue, 0);
        lv_obj_set_height(btn, 56);
        lv_obj_add_event_cb(btn, onNetwork, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(i)));
    }
    g_listShown = true;
}

// Keeps the status line and the list in step with the background scan.
void onTimer(lv_timer_t*) {
    lv_label_set_text(g_status, net_time::statusText());
    if (!g_listShown && net_time::state() == net_time::State::ScanDone) {
        fillList();
    }
}

void onBack(lv_event_t*) {
    if (!lv_obj_has_flag(g_passwordPage, LV_OBJ_FLAG_HIDDEN)) {
        showPage(false);
        return;
    }
    if (g_onDone) {
        g_onDone(false, "", "");
    }
}

void onScan(lv_event_t*) {
    g_listShown = false;
    lv_obj_clean(g_list);
    net_time::startScan();
}

void onShowPassword(lv_event_t*) {
    const bool hidden = lv_textarea_get_password_mode(g_password);
    lv_textarea_set_password_mode(g_password, !hidden);
    lv_label_set_text(g_showButtonLabel, hidden ? "HIDE" : "SHOW");
}

void onSave(lv_event_t*) {
    if (g_onDone) {
        g_onDone(true, g_ssid, lv_textarea_get_text(g_password));
    }
}

} // namespace

lv_obj_t* build(DoneCallback onDone) {
    g_onDone = onDone;

    lv_obj_t* screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(screen, kColorBg, 0);
    lv_obj_set_style_pad_all(screen, 12, 0);
    lv_obj_set_style_pad_row(screen, 8, 0);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* header = lv_obj_create(screen);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), 60);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 12, 0);
    makeButton(header, LV_SYMBOL_LEFT " BACK", 150, onBack);
    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, "WI-FI");
    lv_obj_set_style_text_color(title, kColorAccent, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
    g_status = lv_label_create(header);
    lv_obj_set_flex_grow(g_status, 1);
    lv_obj_set_style_text_color(g_status, kColorHint, 0);
    lv_obj_set_style_text_font(g_status, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(g_status, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(g_status, "");

    // Page 1: networks.
    g_listPage = lv_obj_create(screen);
    lv_obj_remove_style_all(g_listPage);
    lv_obj_set_width(g_listPage, LV_PCT(100));
    lv_obj_set_flex_grow(g_listPage, 1);
    lv_obj_set_flex_flow(g_listPage, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(g_listPage, 8, 0);
    g_list = lv_list_create(g_listPage);
    lv_obj_set_width(g_list, LV_PCT(100));
    lv_obj_set_flex_grow(g_list, 1);
    lv_obj_set_style_bg_color(g_list, kColorBg, 0);
    lv_obj_set_style_border_width(g_list, 0, 0);
    makeButton(g_listPage, LV_SYMBOL_REFRESH " SCAN AGAIN", 260, onScan);

    // Page 2: password, with the on-screen keyboard.
    g_passwordPage = lv_obj_create(screen);
    lv_obj_remove_style_all(g_passwordPage);
    lv_obj_set_width(g_passwordPage, LV_PCT(100));
    lv_obj_set_flex_grow(g_passwordPage, 1);
    lv_obj_set_flex_flow(g_passwordPage, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(g_passwordPage, 8, 0);
    g_networkLabel = lv_label_create(g_passwordPage);
    lv_obj_set_style_text_color(g_networkLabel, kColorCaption, 0);
    lv_obj_set_style_text_font(g_networkLabel, &lv_font_montserrat_24, 0);

    lv_obj_t* row = lv_obj_create(g_passwordPage);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);
    g_password = lv_textarea_create(row);
    lv_textarea_set_one_line(g_password, true);
    lv_textarea_set_password_mode(g_password, true);
    lv_textarea_set_max_length(g_password, 64);
    lv_obj_set_flex_grow(g_password, 1);
    lv_obj_set_style_text_font(g_password, &lv_font_montserrat_24, 0);
    lv_obj_set_style_bg_color(g_password, kColorTile, 0);
    lv_obj_set_style_text_color(g_password, kColorValue, 0);
    makeButton(row, "SHOW", 110, onShowPassword, &g_showButtonLabel);
    lv_obj_t* save = makeButton(row, LV_SYMBOL_OK " SAVE", 150, onSave);
    lv_obj_set_style_border_color(save, kColorAccent, 0);

    lv_obj_t* keyboard = lv_keyboard_create(g_passwordPage);
    lv_obj_set_width(keyboard, LV_PCT(100));
    lv_obj_set_flex_grow(keyboard, 1);
    lv_keyboard_set_textarea(keyboard, g_password);
    lv_obj_add_event_cb(keyboard, onSave, LV_EVENT_READY, nullptr);

    lv_timer_create(onTimer, 500, nullptr);
    showPage(false);
    return screen;
}

void open(const char* currentSsid) {
    snprintf(g_ssid, sizeof(g_ssid), "%s", currentSsid);
    g_listShown = false;
    lv_obj_clean(g_list);
    showPage(false);
    net_time::startScan();
}

} // namespace wifi_screen
