#include "background_convert.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace background_convert {

namespace {

float luma(const uint8_t* p) {
    return 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2];
}

// Luminance, box-filtered down to 200 pixels wide (background_tools resizes
// to 200 wide before both estimates).
std::vector<float> smallLuma(const Image& img, int& w, int& h) {
    w = 200;
    h = std::max(1, static_cast<int>(std::lround(200.0 * img.h / img.w)));
    std::vector<float> out(static_cast<size_t>(w) * h);
    for (int y = 0; y < h; y++) {
        const int y0 = y * img.h / h;
        const int y1 = std::max(y0 + 1, (y + 1) * img.h / h);
        for (int x = 0; x < w; x++) {
            const int x0 = x * img.w / w;
            const int x1 = std::max(x0 + 1, (x + 1) * img.w / w);
            float sum = 0.0f;
            for (int sy = y0; sy < y1; sy++) {
                const uint8_t* row = img.rgb + (static_cast<size_t>(sy) * img.w + x0) * 3;
                for (int sx = x0; sx < x1; sx++, row += 3) {
                    sum += luma(row);
                }
            }
            out[static_cast<size_t>(y) * w + x] = sum / static_cast<float>((y1 - y0) * (x1 - x0));
        }
    }
    return out;
}

// numpy.percentile's default (linear interpolation between closest ranks).
float percentile(std::vector<float>& v, float p) {
    if (v.empty()) {
        return 0.0f;
    }
    const float pos = p / 100.0f * static_cast<float>(v.size() - 1);
    const size_t lo = static_cast<size_t>(pos);
    const size_t hi = std::min(lo + 1, v.size() - 1);
    std::nth_element(v.begin(), v.begin() + static_cast<long>(lo), v.end());
    const float a = v[lo];
    std::nth_element(v.begin(), v.begin() + static_cast<long>(hi), v.end());
    const float b = v[hi];
    return a + (b - a) * (pos - static_cast<float>(lo));
}

uint32_t packHex(const float* c) {
    uint32_t out = 0;
    for (int i = 0; i < 3; i++) {
        const long v = std::lround(std::min(255.0f, std::max(0.0f, c[i])));
        out = (out << 8) | static_cast<uint32_t>(v);
    }
    return out;
}

} // namespace

float estimateHorizon(const Image& img) {
    int w = 0;
    int h = 0;
    const std::vector<float> a = smallLuma(img, w, h);
    const int k = std::max(2, static_cast<int>(h * 0.12f));
    float bestScore = INFINITY;
    int bestY = h / 2;
    std::vector<float> upMean(w);
    std::vector<float> downMean(w);
    for (int y = std::max(k, static_cast<int>(h * 0.25f)); y < std::min(h - k - 1, static_cast<int>(h * 0.75f)); y++) {
        // Row i above the line pairs with row i below it.
        std::fill(upMean.begin(), upMean.end(), 0.0f);
        std::fill(downMean.begin(), downMean.end(), 0.0f);
        float mirror = 0.0f;
        for (int i = 0; i < k; i++) {
            const float* up = &a[static_cast<size_t>(y - 1 - i) * w];
            const float* down = &a[static_cast<size_t>(y + 1 + i) * w];
            for (int x = 0; x < w; x++) {
                mirror += std::fabs(up[x] - down[x]);
                upMean[x] += up[x];
                downMean[x] += down[x];
            }
        }
        float detail = 0.0f;
        for (int i = 0; i < k; i++) {
            const float* up = &a[static_cast<size_t>(y - 1 - i) * w];
            const float* down = &a[static_cast<size_t>(y + 1 + i) * w];
            for (int x = 0; x < w; x++) {
                detail += std::fabs(up[x] - upMean[x] / k) + std::fabs(down[x] - downMean[x] / k);
            }
        }
        const float n = static_cast<float>(k * w);
        const float score = (mirror / n) / (detail / n + 10.0f);
        if (score < bestScore) {
            bestScore = score;
            bestY = y;
        }
    }
    return static_cast<float>(bestY) / static_cast<float>(h);
}

float estimateSunX(const Image& img, float horizon) {
    int w = 0;
    int h = 0;
    const std::vector<float> a = smallLuma(img, w, h);
    const int skyRows = std::max(1, static_cast<int>(horizon * h) - 2);
    std::vector<float> column(skyRows);
    std::vector<float> bright(w);
    for (int x = 0; x < w; x++) {
        for (int y = 0; y < skyRows; y++) {
            column[y] = a[static_cast<size_t>(y) * w + x];
        }
        bright[x] = percentile(column, 90.0f);
    }
    // numpy.convolve(bright, ones(15) / 15, mode="same"): zero beyond the ends.
    int best = 0;
    float bestValue = -1.0f;
    for (int x = 0; x < w; x++) {
        float sum = 0.0f;
        for (int j = -7; j <= 7; j++) {
            if (x + j >= 0 && x + j < w) {
                sum += bright[x + j];
            }
        }
        if (sum / 15.0f > bestValue) {
            bestValue = sum / 15.0f;
            best = x;
        }
    }
    return static_cast<float>(best) / static_cast<float>(w);
}

Result convert(const Image& img, float horizon, float sunX, uint8_t* scratch, uint16_t* out) {
    // Largest 5:3 crop that puts the horizon at kHorizon.
    const float sw = static_cast<float>(img.w);
    const float sh = static_cast<float>(img.h);
    const float hy = horizon * sh;
    const float cropH = std::min({hy / kHorizon, (sh - hy) / (1.0f - kHorizon), sw * kOutH / kOutW});
    const float cropW = cropH * kOutW / kOutH;
    const float top = hy - kHorizon * cropH;
    const float left = std::min(std::max(0.5f * sw - cropW / 2.0f, 0.0f), sw - cropW);

    // Box-filter each output pixel's footprint in the source.
    const float stepX = cropW / kOutW;
    const float stepY = cropH / kOutH;
    for (int oy = 0; oy < kOutH; oy++) {
        const float fy = top + oy * stepY;
        int y0 = std::max(0, static_cast<int>(std::floor(fy)));
        int y1 = std::min(img.h, std::max(y0 + 1, static_cast<int>(std::ceil(fy + stepY))));
        y0 = std::min(y0, img.h - 1);
        for (int ox = 0; ox < kOutW; ox++) {
            const float fx = left + ox * stepX;
            int x0 = std::max(0, static_cast<int>(std::floor(fx)));
            int x1 = std::min(img.w, std::max(x0 + 1, static_cast<int>(std::ceil(fx + stepX))));
            x0 = std::min(x0, img.w - 1);
            uint32_t sum[3] = {0, 0, 0};
            for (int sy = y0; sy < y1; sy++) {
                const uint8_t* p = img.rgb + (static_cast<size_t>(sy) * img.w + x0) * 3;
                for (int sx = x0; sx < x1; sx++, p += 3) {
                    sum[0] += p[0];
                    sum[1] += p[1];
                    sum[2] += p[2];
                }
            }
            const uint32_t n = static_cast<uint32_t>((y1 - y0) * (x1 - x0));
            uint8_t* d = scratch + (static_cast<size_t>(oy) * kOutW + ox) * 3;
            for (int c = 0; c < 3; c++) {
                d[c] = static_cast<uint8_t>((sum[c] + n / 2) / n);
            }
        }
    }

    Result result{};
    result.sunX = (sunX * sw - left) / cropW * kOutW;

    // Sky colours: the brightest tenth of the sky band is the glow the water
    // would reflect (skyline hills and trees are darker and drop out).
    const int skyRows = static_cast<int>(kHorizon * kOutH) - 4;
    std::vector<float> lums(static_cast<size_t>(skyRows) * kOutW);
    for (size_t i = 0; i < lums.size(); i++) {
        lums[i] = luma(scratch + i * 3);
    }
    std::vector<float> sorted = lums;
    const float threshold = percentile(sorted, 90.0f);
    float glow[3] = {0, 0, 0}; // summed; only the ratios matter below
    for (size_t i = 0; i < lums.size(); i++) {
        if (lums[i] >= threshold) {
            for (int c = 0; c < 3; c++) {
                glow[c] += scratch[i * 3 + c];
            }
        }
    }
    float ripple[3];
    float sun[3];
    const float glowMax = std::max({glow[0], glow[1], glow[2], 1.0f});
    for (int c = 0; c < 3; c++) {
        ripple[c] = glow[c] * 242.0f / glowMax;
        sun[c] = 0.6f * ripple[c] + 0.4f * 255.0f;
    }
    const float sunMax = std::max({sun[0], sun[1], sun[2]});
    for (int c = 0; c < 3; c++) {
        sun[c] *= 255.0f / sunMax;
    }
    result.rippleColor = packHex(ripple);
    result.sunColor = packHex(sun);

    // Floyd-Steinberg to RGB565: hides banding in the sky gradient.
    static const float kLevels[3] = {31.0f, 63.0f, 31.0f};
    std::vector<float> errCur(kOutW * 3 + 6, 0.0f);
    std::vector<float> errNext(kOutW * 3 + 6, 0.0f);
    for (int y = 0; y < kOutH; y++) {
        std::fill(errNext.begin(), errNext.end(), 0.0f);
        for (int x = 0; x < kOutW; x++) {
            const uint8_t* p = scratch + (static_cast<size_t>(y) * kOutW + x) * 3;
            int q[3];
            for (int c = 0; c < 3; c++) {
                const float old = std::min(255.0f, std::max(0.0f, p[c] + errCur[(x + 1) * 3 + c]));
                q[c] = static_cast<int>(std::lround(old / 255.0f * kLevels[c]));
                const float err = old - q[c] / kLevels[c] * 255.0f;
                errCur[(x + 2) * 3 + c] += err * 7.0f / 16.0f;
                errNext[x * 3 + c] += err * 3.0f / 16.0f;
                errNext[(x + 1) * 3 + c] += err * 5.0f / 16.0f;
                errNext[(x + 2) * 3 + c] += err * 1.0f / 16.0f;
            }
            out[static_cast<size_t>(y) * kOutW + x] = static_cast<uint16_t>((q[0] << 11) | (q[1] << 5) | q[2]);
        }
        std::swap(errCur, errNext);
    }
    return result;
}

} // namespace background_convert
