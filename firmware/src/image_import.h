#pragma once

// Converts photos dropped in the SD card's /images folder into scene
// backgrounds in /backgrounds (background_convert.h does the maths), so no
// computer is needed. Runs at boot while sd_backgrounds has the card mounted.
//
// - Baseline JPEGs (.jpg / .jpeg) only, decoded with the ESP32-S3's ROM
//   TJpgDec. Progressive JPEGs and PNGs are skipped with a log line: a PNG
//   has to be decoded whole (~4 MB for a 1328x800 photo), more than there is
//   room for. scripts/make_background.py handles those on a computer.
// - The file name becomes the display name ("home_lake.jpg" -> "home lake").
//   A "_h52" suffix sets the waterline to 52% of the photo's height instead
//   of the guess ("home_lake_h52.jpg").
// - A photo is converted once: it's skipped while /backgrounds/<name>.bin
//   exists. Delete that file to convert the photo again.
namespace image_import {

// Call with the card mounted at sd_card::kMountPoint. Returns how many
// photos were converted.
int convertNew();

} // namespace image_import
