#pragma once

#include <cstddef>

// Version strings and the update manifest's format, kept free of Arduino so
// the native tests cover them (firmware_update.h does the networking).
namespace version_util {

// "1.2.3" (a leading "v" is skipped) into major/minor/patch. Anything after
// the patch number ("-3-gabc123-dirty" from git describe) is ignored. False
// for strings that don't start that way, such as a bare commit hash.
bool parse(const char* s, int out[3]);

// True if `candidate` is a higher version than `current`. False when either
// doesn't parse: a development build is never replaced automatically.
bool isNewer(const char* candidate, const char* current);

// The string value of `"key": "value"` in a flat JSON object (no escapes in
// the value). False if the key is missing or the value doesn't fit.
bool jsonString(const char* json, const char* key, char* out, size_t outSize);

} // namespace version_util
