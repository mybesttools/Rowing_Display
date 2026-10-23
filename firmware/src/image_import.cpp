#include "image_import.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if !defined(SIMULATION_MODE)
#include <dirent.h>
#include <sys/stat.h>

#include "background_convert.h"
#include "esp_heap_caps.h"
#include "rom/tjpgd.h"
#include "sd_backgrounds.h"
#endif

namespace image_import {

#if defined(SIMULATION_MODE)

int convertNew() {
    return 0;
}

#else

namespace {

constexpr char kImagesDir[] = "/sd/images";
constexpr char kBackgroundsDir[] = "/sd/backgrounds";
constexpr float kGlint = 0.6f; // make_background.py's default
constexpr size_t kWorkSize = 4096; // TJpgDec needs ~3.1 KB

bool isJpeg(const char* name) {
    const char* dot = strrchr(name, '.');
    return dot != nullptr && (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0);
}

bool isOtherImage(const char* name) {
    const char* dot = strrchr(name, '.');
    return dot != nullptr && (strcasecmp(dot, ".png") == 0 || strcasecmp(dot, ".heic") == 0 ||
                              strcasecmp(dot, ".webp") == 0 || strcasecmp(dot, ".bmp") == 0);
}

struct Names {
    char display[sizeof(sd_backgrounds::FileHeader::name)];
    char file[sd_backgrounds::kFileNameLen]; // "<slug>.bin"
    float horizon;                           // < 0: guess it
};

// "Home Lake_h52.jpg" -> display "Home Lake", file "home_lake.bin", horizon 0.52.
Names namesFor(const char* fileName) {
    Names n{};
    n.horizon = -1.0f;
    char stem[64];
    snprintf(stem, sizeof(stem), "%s", fileName);
    char* dot = strrchr(stem, '.');
    if (dot != nullptr) {
        *dot = '\0';
    }
    const size_t len = strlen(stem);
    // Waterline override: "_h" plus one or two digits at the end.
    for (size_t digits = 2; digits >= 1; digits--) {
        if (len > digits + 2 && stem[len - digits - 2] == '_' && tolower(stem[len - digits - 1]) == 'h') {
            bool allDigits = true;
            for (size_t i = len - digits; i < len; i++) {
                allDigits = allDigits && isdigit(static_cast<unsigned char>(stem[i]));
            }
            if (allDigits) {
                n.horizon = static_cast<float>(atoi(stem + len - digits)) / 100.0f;
                stem[len - digits - 2] = '\0';
                break;
            }
        }
    }

    size_t d = 0;
    for (const char* p = stem; *p != '\0' && d + 1 < sizeof(n.display); p++) {
        n.display[d++] = (*p == '_' || *p == '-') ? ' ' : *p;
    }
    n.display[d] = '\0';

    char slug[sd_backgrounds::kFileNameLen - 4] = "";
    size_t s = 0;
    for (const char* p = stem; *p != '\0' && s + 1 < sizeof(slug); p++) {
        const unsigned char c = static_cast<unsigned char>(*p);
        if (isalnum(c)) {
            slug[s++] = static_cast<char>(tolower(c));
        } else if (s > 0 && slug[s - 1] != '_') {
            slug[s++] = '_';
        }
    }
    while (s > 0 && slug[s - 1] == '_') {
        s--;
    }
    slug[s] = '\0';
    snprintf(n.file, sizeof(n.file), "%s.bin", s > 0 ? slug : "photo");
    return n;
}

struct Decode {
    FILE* file;
    uint8_t* rgb;
    int w;
    int h;
};

UINT readInput(JDEC* jd, BYTE* buf, UINT n) {
    auto* d = static_cast<Decode*>(jd->device);
    if (buf == nullptr) {
        return fseek(d->file, n, SEEK_CUR) == 0 ? n : 0;
    }
    return static_cast<UINT>(fread(buf, 1, n, d->file));
}

UINT writeOutput(JDEC* jd, void* bitmap, JRECT* r) {
    auto* d = static_cast<Decode*>(jd->device);
    const auto* src = static_cast<const uint8_t*>(bitmap);
    for (int y = r->top; y <= r->bottom; y++) {
        for (int x = r->left; x <= r->right; x++, src += 3) {
            if (y < d->h && x < d->w) {
                memcpy(d->rgb + (static_cast<size_t>(y) * d->w + x) * 3, src, 3);
            }
        }
    }
    return 1;
}

// Decodes `path` at the coarsest 1/2^n scale that still leaves at least
// 640x384 pixels (plenty for a 400x240 crop) and fits in PSRAM. On success
// the caller frees d.rgb.
bool decodeJpeg(const char* path, Decode& d) {
    d.file = fopen(path, "rb");
    if (d.file == nullptr) {
        return false;
    }
    void* work = malloc(kWorkSize);
    JDEC jd{};
    bool ok = false;
    JRESULT res = work != nullptr ? jd_prepare(&jd, readInput, work, kWorkSize, &d) : JDR_MEM1;
    if (res == JDR_OK) {
        int scale = 3;
        while (scale > 0 && ((static_cast<int>(jd.width) >> scale) < 640 || (static_cast<int>(jd.height) >> scale) < 384)) {
            scale--;
        }
        for (; scale <= 3 && !ok; scale++) {
            d.w = static_cast<int>(jd.width) >> scale;
            d.h = static_cast<int>(jd.height) >> scale;
            d.rgb = static_cast<uint8_t*>(heap_caps_malloc(static_cast<size_t>(d.w) * d.h * 3, MALLOC_CAP_SPIRAM));
            if (d.rgb == nullptr) {
                continue; // try the next smaller scale
            }
            res = jd_decomp(&jd, writeOutput, static_cast<BYTE>(scale));
            ok = res == JDR_OK;
            if (!ok) {
                heap_caps_free(d.rgb);
                d.rgb = nullptr;
                break;
            }
            printf("photos: %s decoded at 1/%d: %dx%d\n", path, 1 << scale, d.w, d.h);
        }
    }
    if (!ok) {
        printf("photos: can't decode %s (TJpgDec %d%s)\n", path, static_cast<int>(res),
               res == JDR_FMT3 ? ": progressive JPEG? save it as baseline" : "");
    }
    free(work);
    fclose(d.file);
    return ok;
}

bool convertOne(const char* name) {
    const Names n = namesFor(name);
    char src[sizeof(kImagesDir) + 64];
    char dst[sizeof(kBackgroundsDir) + sd_backgrounds::kFileNameLen + 1];
    snprintf(src, sizeof(src), "%s/%s", kImagesDir, name);
    snprintf(dst, sizeof(dst), "%s/%s", kBackgroundsDir, n.file);
    struct stat st {};
    if (stat(dst, &st) == 0) {
        return false; // converted before
    }

    Decode d{};
    if (!decodeJpeg(src, d)) {
        return false;
    }
    const background_convert::Image img{d.rgb, d.w, d.h};
    const float horizon = n.horizon >= 0.05f && n.horizon <= 0.95f ? n.horizon : background_convert::estimateHorizon(img);
    const float sunX = background_convert::estimateSunX(img, horizon);

    constexpr size_t kPixels = background_convert::kOutW * background_convert::kOutH;
    auto* scratch = static_cast<uint8_t*>(heap_caps_malloc(kPixels * 3, MALLOC_CAP_SPIRAM));
    auto* pixels = static_cast<uint16_t*>(heap_caps_malloc(kPixels * sizeof(uint16_t), MALLOC_CAP_SPIRAM));
    bool ok = false;
    if (scratch != nullptr && pixels != nullptr) {
        const background_convert::Result r = background_convert::convert(img, horizon, sunX, scratch, pixels);
        sd_backgrounds::FileHeader h{};
        memcpy(h.magic, "RBG1", 4);
        h.width = background_convert::kOutW;
        h.height = background_convert::kOutH;
        h.sunX = r.sunX;
        h.glint = kGlint;
        h.sunColor = r.sunColor;
        h.rippleColor = r.rippleColor;
        snprintf(h.name, sizeof(h.name), "%s", n.display);
        FILE* f = fopen(dst, "wb");
        if (f != nullptr) {
            ok = fwrite(&h, sizeof(h), 1, f) == 1 && fwrite(pixels, sizeof(uint16_t), kPixels, f) == kPixels;
            ok = fclose(f) == 0 && ok;
            if (!ok) {
                remove(dst); // don't leave half a file that blocks a retry
            }
        }
        printf("photos: %s -> %s \"%s\", waterline %.2f (%s), %s\n", name, n.file, n.display, horizon,
               n.horizon >= 0.0f ? "from name" : "guessed", ok ? "saved" : "write failed");
    }
    heap_caps_free(scratch);
    heap_caps_free(pixels);
    heap_caps_free(d.rgb);
    return ok;
}

} // namespace

int convertNew() {
    DIR* dir = opendir(kImagesDir);
    if (dir == nullptr) {
        return 0;
    }
    mkdir(kBackgroundsDir, 0775);
    // Collect first: writing to the card while iterating a directory on it
    // isn't something FatFs promises to handle.
    constexpr int kMaxPhotos = 16;
    char names[kMaxPhotos][64];
    int count = 0;
    for (dirent* e = readdir(dir); e != nullptr && count < kMaxPhotos; e = readdir(dir)) {
        if (e->d_type == DT_DIR || e->d_name[0] == '.') {
            continue;
        }
        if (isJpeg(e->d_name) && strlen(e->d_name) < sizeof(names[0])) {
            snprintf(names[count++], sizeof(names[0]), "%s", e->d_name);
        } else if (isOtherImage(e->d_name)) {
            printf("photos: skipping %s (only JPEG is converted on the display)\n", e->d_name);
        }
    }
    closedir(dir);

    int converted = 0;
    for (int i = 0; i < count; i++) {
        converted += convertOne(names[i]) ? 1 : 0;
    }
    if (converted > 0) {
        printf("photos: %d new background(s)\n", converted);
    }
    return converted;
}

#endif

} // namespace image_import
