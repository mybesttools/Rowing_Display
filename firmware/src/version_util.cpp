#include "version_util.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace version_util {

bool parse(const char* s, int out[3]) {
    if (s == nullptr) {
        return false;
    }
    if (*s == 'v' || *s == 'V') {
        s++;
    }
    for (int i = 0; i < 3; i++) {
        if (!isdigit(static_cast<unsigned char>(*s))) {
            return false;
        }
        int n = 0;
        while (isdigit(static_cast<unsigned char>(*s))) {
            n = n * 10 + (*s - '0');
            if (n > 99999) {
                return false;
            }
            s++;
        }
        out[i] = n;
        if (i < 2) {
            if (*s != '.') {
                return false;
            }
            s++;
        }
    }
    // "1.2.3" or "1.2.3-...", not "1.2.3abc" (a hash that happens to start
    // with digits and dots is unlikely, but this keeps it out).
    return *s == '\0' || *s == '-' || *s == '+';
}

bool isNewer(const char* candidate, const char* current) {
    int a[3];
    int b[3];
    if (!parse(candidate, a) || !parse(current, b)) {
        return false;
    }
    for (int i = 0; i < 3; i++) {
        if (a[i] != b[i]) {
            return a[i] > b[i];
        }
    }
    return false;
}

bool jsonString(const char* json, const char* key, char* out, size_t outSize) {
    if (json == nullptr || key == nullptr || out == nullptr || outSize == 0) {
        return false;
    }
    char quoted[72];
    if (snprintf(quoted, sizeof(quoted), "\"%s\"", key) >= static_cast<int>(sizeof(quoted))) {
        return false;
    }
    for (const char* p = strstr(json, quoted); p != nullptr; p = strstr(p + 1, quoted)) {
        const char* v = p + strlen(quoted);
        while (isspace(static_cast<unsigned char>(*v))) {
            v++;
        }
        if (*v != ':') {
            continue; // the key's text appeared as a value
        }
        v++;
        while (isspace(static_cast<unsigned char>(*v))) {
            v++;
        }
        if (*v != '"') {
            return false;
        }
        v++;
        const char* end = strchr(v, '"');
        if (end == nullptr || static_cast<size_t>(end - v) >= outSize) {
            return false;
        }
        memcpy(out, v, end - v);
        out[end - v] = '\0';
        return true;
    }
    return false;
}

} // namespace version_util
