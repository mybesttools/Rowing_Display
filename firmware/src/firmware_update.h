#pragma once

// Over-the-air updates from the GitHub Pages site that the release workflow
// publishes (.github/workflows/release.yml): firmware/latest.json names the
// newest version and each build's firmware.bin. Checked from net_time's
// clock sync, while Wi-Fi is up and the clock is right (HTTPS checks the
// certificate dates).
namespace firmware_update {

// This build's version ("1.2.0", or git describe output for a local build)
// and PlatformIO environment (which binary to fetch).
const char* version();
const char* variant();

enum class Result { UpToDate, Installed, Failed, Skipped };

// Progress for the splash screen, e.g. "downloading 1.2.0: 40%".
using StatusFn = void (*)(const char* text);

// Fetches the manifest and, if it names a newer version for this variant,
// downloads and installs it into the other app slot. Blocking, up to a
// minute or so; call with Wi-Fi connected, off the LVGL task. Installed:
// restart to run it.
Result checkAndInstall(StatusFn status);

// Call once the firmware has shown it starts up (the screens are built).
// Until then a freshly installed update is on probation: if it crashes
// before this, the next start goes back to the previous firmware.
void markBootOk();

} // namespace firmware_update
