#pragma once

// Clock sync over Wi-Fi. Each job runs on its own task and turns Wi-Fi off
// when it's done: the radio stays off the rest of the time, since RF and
// flash activity are what disturb the RGB panel (see panel_runtime.h). One
// job at a time; a start call while busy is ignored.
namespace net_time {

enum class State { Idle, Scanning, ScanDone, Connecting, Syncing, Synced, Failed };

// Connect, get the time from NTP, write it to the RTC, disconnect.
// `posixTz` is the time zone (timezones.h) to keep in effect.
void startSync(const char* ssid, const char* password, const char* posixTz);

// List nearby networks (strongest first, duplicates merged).
void startScan();

State state();
bool busy();
// One line for the UI, e.g. "synced 10:02" or "wrong password?".
const char* statusText();

constexpr int kMaxNetworks = 12;
int networkCount();
const char* networkName(int i);
int networkRssi(int i);

} // namespace net_time
