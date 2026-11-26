#include "aero/render/ZeroCopyGpuStream.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>

#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define AERO_SW_SIMD_SSE2 1
#elif defined(__ARM_NEON) || defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#define AERO_SW_SIMD_NEON 1
#endif

#include "aero/asset/GradientBrush.hpp"
#include "aero/asset/MtsdfPathBaker.hpp"
#include "aero/core/UIResourceStore.hpp"
#include "aero/render/FontAtlas.hpp"
#include "aero/render/ImageAtlas.hpp"

#include "RenderInternal.hpp"

namespace aero {

namespace {

using aero::render::detail::ComputeNodeRangeScreenBounds;
using aero::render::detail::ComputeSpanBoxRadius;
using aero::render::detail::ComputeSpanBoxRadiusFloat;
using aero::render::detail::ComputeSpanPaddingPx;
using aero::render::detail::SafeCeilToInt;
using aero::render::detail::SafeFloorToInt;
using aero::render::detail::SpanNeedsEffectPass;

thread_local int gRasterBandX0 = 0;
thread_local int gRasterBandX1 = 0x3FFFFFFF;
thread_local int gRasterBandY0 = 0;
thread_local int gRasterBandY1 = 0x3FFFFFFF;
thread_local bool gInScanlineBand = false;

class ScanlineWorkerPool {
public:
    static constexpr std::size_t kNumWorkers = 3u; // +1 caller thread = 4 parallel horizontal bands

    static ScanlineWorkerPool& Instance() {
        static ScanlineWorkerPool pool;
        return pool;
    }

    template <typename Fn>
    void ParallelBands4(int totalHeight, Fn&& bandFn) {
        if (totalHeight < 64 || gInScanlineBand) {
            bandFn(0, totalHeight);
            return;
        }
        std::unique_lock<std::mutex> dispatchLock(dispatchMtx_, std::try_to_lock);
        if (!dispatchLock.owns_lock()) {
            bandFn(0, totalHeight);
            return;
        }
        const int step = (totalHeight + 3) / 4;
        using FnT = std::remove_reference_t<Fn>;
        auto invokeBand = +[](void* rawCtx, int bandIdx, int totalH, int stepH) {
            const int y0 = bandIdx * stepH;
            const int y1 = std::min(totalH, y0 + stepH);
            if (y0 < y1) {
                (*static_cast<FnT*>(rawCtx))(y0, y1);
            }
        };
        void* const ctxPtr = const_cast<void*>(static_cast<const void*>(&bandFn));
        {
            std::lock_guard<std::mutex> lk(mtx_);
            currentCtx_ = ctxPtr;
            currentFn_ = invokeBand;
            currentTotalH_ = totalHeight;
            currentStep_ = step;
            remaining_.store(static_cast<int>(kNumWorkers), std::memory_order_release);
            ++epoch_;
        }
        cvStart_.notify_all();

        // Band 0 on calling thread
        gInScanlineBand = true;
        try {
            invokeBand(ctxPtr, 0, totalHeight, step);
        } catch (...) {
            gInScanlineBand = false;
            std::unique_lock<std::mutex> lk(mtx_);
            cvDone_.wait(lk, [&]() { return remaining_.load(std::memory_order_acquire) == 0; });
            currentFn_ = nullptr;
            currentCtx_ = nullptr;
            throw;
        }
        gInScanlineBand = false;

        // Wait for bands 1..3
        {
            std::unique_lock<std::mutex> lk(mtx_);
            cvDone_.wait(lk, [&]() { return remaining_.load(std::memory_order_acquire) == 0; });
            currentFn_ = nullptr;
            currentCtx_ = nullptr;
        }
    }

private:
    ScanlineWorkerPool() {
        for (std::size_t i = 0u; i < kNumWorkers; ++i) {
            const int bandIdx = static_cast<int>(i + 1u);
            workers_[i] = std::thread([this, bandIdx]() {
                std::uint64_t seenEpoch = 0u;
                for (;;) {
                    void (*fn)(void*, int, int, int) = nullptr;
                    void* ctx = nullptr;
                    int totalH = 0;
                    int stepH = 0;
                    {
                        std::unique_lock<std::mutex> lk(mtx_);
                        cvStart_.wait(lk, [&]() { return stop_ || epoch_ != seenEpoch; });
                        if (stop_) return;
                        seenEpoch = epoch_;
                        fn = currentFn_;
                        ctx = currentCtx_;
                        totalH = currentTotalH_;
                        stepH = currentStep_;
                    }
                    if (fn) {
                        gInScanlineBand = true;
                        try {
                            fn(ctx, bandIdx, totalH, stepH);
                        } catch (...) {
                        }
                        gInScanlineBand = false;
                    }
                    {
                        std::lock_guard<std::mutex> lk(mtx_);
                        if (remaining_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                            cvDone_.notify_one();
                        }
                    }
                }
            });
        }
    }

    ~ScanlineWorkerPool() {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            stop_ = true;
        }
        cvStart_.notify_all();
        for (auto& t : workers_) {
            if (t.joinable()) t.join();
        }
    }

    std::thread workers_[kNumWorkers];
    std::mutex dispatchMtx_;
    std::mutex mtx_;
    std::condition_variable cvStart_;
    std::condition_variable cvDone_;
    void* currentCtx_ = nullptr;
    void (*currentFn_)(void*, int, int, int) = nullptr;
    int currentTotalH_ = 0;
    int currentStep_ = 0;
    std::uint64_t epoch_ = 0u;
    std::atomic<int> remaining_{0};
    bool stop_ = false;
};

void BlendPixelOver(
    std::uint8_t* dstRgba,
    std::uint8_t srcR,
    std::uint8_t srcG,
    std::uint8_t srcB,
    float srcAlpha01,
    BlendingMode blendMode = BlendingMode::Normal) noexcept {
    const float a = std::clamp(srcAlpha01, 0.0f, 1.0f);
    if (a <= 0.001f) {
        return;
    }
    const float invA = 1.0f - a;
    if (blendMode == BlendingMode::Normal) {
#ifdef AERO_SW_SIMD_SSE2
        const __m128 vSrc = _mm_set_ps(255.0f, static_cast<float>(srcB), static_cast<float>(srcG), static_cast<float>(srcR));
        const __m128 vDst = _mm_set_ps(
            static_cast<float>(dstRgba[3]),
            static_cast<float>(dstRgba[2]),
            static_cast<float>(dstRgba[1]),
            static_cast<float>(dstRgba[0]));
        const __m128 vA = _mm_set1_ps(a);
        const __m128 vInvA = _mm_set1_ps(invA);
        const __m128 vHalf = _mm_set1_ps(0.5f);
        const __m128 vZero = _mm_setzero_ps();
        const __m128 v255 = _mm_set1_ps(255.0f);
        const __m128 sum = _mm_add_ps(_mm_add_ps(_mm_mul_ps(vSrc, vA), _mm_mul_ps(vDst, vInvA)), vHalf);
        const __m128 clamped = _mm_min_ps(_mm_max_ps(sum, vZero), v255);
        const __m128i i32 = _mm_cvttps_epi32(clamped);
        const __m128i i16 = _mm_packs_epi32(i32, i32);
        const __m128i u8 = _mm_packus_epi16(i16, i16);
        const std::uint32_t packed = static_cast<std::uint32_t>(_mm_cvtsi128_si32(u8));
        std::memcpy(dstRgba, &packed, 4u);
        return;
#else
        dstRgba[0] = static_cast<std::uint8_t>(std::clamp(static_cast<float>(srcR) * a + static_cast<float>(dstRgba[0]) * invA + 0.5f, 0.0f, 255.0f));
        dstRgba[1] = static_cast<std::uint8_t>(std::clamp(static_cast<float>(srcG) * a + static_cast<float>(dstRgba[1]) * invA + 0.5f, 0.0f, 255.0f));
        dstRgba[2] = static_cast<std::uint8_t>(std::clamp(static_cast<float>(srcB) * a + static_cast<float>(dstRgba[2]) * invA + 0.5f, 0.0f, 255.0f));
        dstRgba[3] = static_cast<std::uint8_t>(std::clamp(a * 255.0f + static_cast<float>(dstRgba[3]) * invA + 0.5f, 0.0f, 255.0f));
        return;
#endif
    }
    const float sr = static_cast<float>(srcR) / 255.0f;
    const float sg = static_cast<float>(srcG) / 255.0f;
    const float sb = static_cast<float>(srcB) / 255.0f;
    const float dr = static_cast<float>(dstRgba[0]) / 255.0f;
    const float dg = static_cast<float>(dstRgba[1]) / 255.0f;
    const float db = static_cast<float>(dstRgba[2]) / 255.0f;
    float br = sr, bg = sg, bb = sb;
    if (blendMode == BlendingMode::Multiply) {
        br = sr * dr;
        bg = sg * dg;
        bb = sb * db;
    } else if (blendMode == BlendingMode::Screen) {
        br = sr + dr - sr * dr;
        bg = sg + dg - sg * dg;
        bb = sb + db - sb * db;
    } else if (blendMode == BlendingMode::Additive) {
        dstRgba[0] = static_cast<std::uint8_t>(std::clamp((dr + sr * a) * 255.0f + 0.5f, 0.0f, 255.0f));
        dstRgba[1] = static_cast<std::uint8_t>(std::clamp((dg + sg * a) * 255.0f + 0.5f, 0.0f, 255.0f));
        dstRgba[2] = static_cast<std::uint8_t>(std::clamp((db + sb * a) * 255.0f + 0.5f, 0.0f, 255.0f));
        dstRgba[3] = static_cast<std::uint8_t>(std::clamp((a + (static_cast<float>(dstRgba[3]) / 255.0f) * invA) * 255.0f + 0.5f, 0.0f, 255.0f));
        return;
    }
    dstRgba[0] = static_cast<std::uint8_t>(std::clamp((br * a + dr * invA) * 255.0f + 0.5f, 0.0f, 255.0f));
    dstRgba[1] = static_cast<std::uint8_t>(std::clamp((bg * a + dg * invA) * 255.0f + 0.5f, 0.0f, 255.0f));
    dstRgba[2] = static_cast<std::uint8_t>(std::clamp((bb * a + db * invA) * 255.0f + 0.5f, 0.0f, 255.0f));
    dstRgba[3] = static_cast<std::uint8_t>(std::clamp(a * 255.0f + static_cast<float>(dstRgba[3]) * invA + 0.5f, 0.0f, 255.0f));
}

void BlendSpanOverNormalFast(
    std::uint8_t* dstRgbaSpan,
    int count,
    std::uint8_t srcR,
    std::uint8_t srcG,
    std::uint8_t srcB,
    float a) noexcept {
    const float invA = 1.0f - a;
#ifdef AERO_SW_SIMD_SSE2
    const __m128 vSrc = _mm_mul_ps(
        _mm_set_ps(255.0f, static_cast<float>(srcB), static_cast<float>(srcG), static_cast<float>(srcR)),
        _mm_set1_ps(a));
    const __m128 vSrcHalf = _mm_add_ps(vSrc, _mm_set1_ps(0.5f));
    const __m128 vInvA = _mm_set1_ps(invA);
    const __m128 vZero = _mm_setzero_ps();
    const __m128 v255 = _mm_set1_ps(255.0f);
    for (int i = 0; i < count; ++i) {
        std::uint8_t* d = dstRgbaSpan + static_cast<std::size_t>(i) * 4u;
        const __m128 vDst = _mm_set_ps(
            static_cast<float>(d[3]),
            static_cast<float>(d[2]),
            static_cast<float>(d[1]),
            static_cast<float>(d[0]));
        const __m128 sum = _mm_add_ps(vSrcHalf, _mm_mul_ps(vDst, vInvA));
        const __m128 clamped = _mm_min_ps(_mm_max_ps(sum, vZero), v255);
        const __m128i i32 = _mm_cvttps_epi32(clamped);
        const __m128i i16 = _mm_packs_epi32(i32, i32);
        const __m128i u8 = _mm_packus_epi16(i16, i16);
        const std::uint32_t packed = static_cast<std::uint32_t>(_mm_cvtsi128_si32(u8));
        std::memcpy(d, &packed, 4u);
    }
#else
    for (int i = 0; i < count; ++i) {
        std::uint8_t* d = dstRgbaSpan + static_cast<std::size_t>(i) * 4u;
        d[0] = static_cast<std::uint8_t>(std::clamp(static_cast<float>(srcR) * a + 0.5f + static_cast<float>(d[0]) * invA, 0.0f, 255.0f));
        d[1] = static_cast<std::uint8_t>(std::clamp(static_cast<float>(srcG) * a + 0.5f + static_cast<float>(d[1]) * invA, 0.0f, 255.0f));
        d[2] = static_cast<std::uint8_t>(std::clamp(static_cast<float>(srcB) * a + 0.5f + static_cast<float>(d[2]) * invA, 0.0f, 255.0f));
        d[3] = static_cast<std::uint8_t>(std::clamp(a * 255.0f + 0.5f + static_cast<float>(d[3]) * invA, 0.0f, 255.0f));
    }
#endif
}

struct PremulOffscreenLayer {
    int originX = 0;
    int originY = 0;
    int width = 0;
    int height = 0;
    std::vector<float> premulRgba;
};

struct PremulLayerBufferPool {
    static constexpr std::size_t kMaxSingleBufferBytes = 1u * 1024u * 1024u;
    static constexpr std::size_t kMaxTotalPooledBytes = 2u * 1024u * 1024u;

    std::vector<std::vector<float>> freeBuffers;
    std::size_t totalPooledBytes = 0u;

    void Acquire(std::vector<float>& target, std::size_t floatCount, bool zeroInit) {
        if (!freeBuffers.empty()) {
            std::size_t bestIdx = freeBuffers.size() - 1u;
            std::size_t bestCap = static_cast<std::size_t>(-1);
            for (std::size_t i = 0u; i < freeBuffers.size(); ++i) {
                const std::size_t cap = freeBuffers[i].capacity();
                if (cap >= floatCount && cap < bestCap) {
                    bestCap = cap;
                    bestIdx = i;
                }
            }
            target = std::move(freeBuffers[bestIdx]);
            if (bestIdx + 1u < freeBuffers.size()) {
                freeBuffers[bestIdx] = std::move(freeBuffers.back());
            }
            freeBuffers.pop_back();
            const std::size_t capBytes = target.capacity() * sizeof(float);
            totalPooledBytes = (totalPooledBytes >= capBytes) ? (totalPooledBytes - capBytes) : 0u;
        }
        if (target.size() < floatCount) {
            target.resize(floatCount, 0.0f);
            if (zeroInit) {
                std::fill_n(target.data(), floatCount, 0.0f);
            }
        } else {
            target.resize(floatCount);
            if (zeroInit) {
                std::fill_n(target.data(), floatCount, 0.0f);
            }
        }
    }

    void Release(std::vector<float>& target) {
        const std::size_t capBytes = target.capacity() * sizeof(float);
        if (capBytes > 0u &&
            capBytes <= kMaxSingleBufferBytes &&
            freeBuffers.size() < 4u &&
            (totalPooledBytes + capBytes) <= kMaxTotalPooledBytes) {
            totalPooledBytes += capBytes;
            freeBuffers.push_back(std::move(target));
        } else {
            std::vector<float>().swap(target);
        }
    }

    void Clear() noexcept {
        std::vector<std::vector<float>>().swap(freeBuffers);
        totalPooledBytes = 0u;
    }
};

thread_local PremulLayerBufferPool gTlPremulLayerPool;

struct ScopedPremulLayerGuard {
    PremulOffscreenLayer& layer;
    ~ScopedPremulLayerGuard() {
        gTlPremulLayerPool.Release(layer.premulRgba);
    }
};

struct ScopedFloatBufferGuard {
    std::vector<float>& buf;
    ~ScopedFloatBufferGuard() {
        gTlPremulLayerPool.Release(buf);
    }
};

struct CachedTexSlotEntry {
    const ImageAtlas* atlas = nullptr;
    std::uint32_t textureId = 0u;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    bool found = false;
    bool isAlpha8 = false;
    std::shared_ptr<const std::vector<std::uint8_t>> pixels;
};

struct ThreadTexSlotCache {
    static constexpr std::size_t kSlots = 8u;
    std::array<CachedTexSlotEntry, kSlots> entries{};
    int activeDepth = 0;

    void Clear() noexcept {
        for (auto& e : entries) {
            e.atlas = nullptr;
            e.textureId = 0u;
            e.width = 0u;
            e.height = 0u;
            e.found = false;
            e.pixels.reset();
        }
    }
};

thread_local ThreadTexSlotCache gTlTexSlotCache;

struct ScopedBandTexCacheGuard {
    ScopedBandTexCacheGuard() noexcept {
        ++gTlTexSlotCache.activeDepth;
    }
    ~ScopedBandTexCacheGuard() noexcept {
        if (--gTlTexSlotCache.activeDepth <= 0) {
            gTlTexSlotCache.activeDepth = 0;
            gTlTexSlotCache.Clear();
        }
    }
};

[[nodiscard]] bool TryGetTextureSlotCached(
    const ImageAtlas& atlas,
    std::uint32_t textureId,
    std::uint32_t& outWidth,
    std::uint32_t& outHeight,
    std::shared_ptr<const std::vector<std::uint8_t>>& outPixels,
    bool* optOutIsAlpha8 = nullptr) noexcept {
    if (gTlTexSlotCache.activeDepth <= 0) {
        return atlas.TryGetTextureSlotSnapshot(textureId, outWidth, outHeight, outPixels, optOutIsAlpha8);
    }
    CachedTexSlotEntry& entry = gTlTexSlotCache.entries[textureId & (ThreadTexSlotCache::kSlots - 1u)];
    if (entry.atlas == &atlas && entry.textureId == textureId) {
        outWidth = entry.width;
        outHeight = entry.height;
        outPixels = entry.pixels;
        if (optOutIsAlpha8) *optOutIsAlpha8 = entry.isAlpha8;
        return entry.found;
    }
    entry.atlas = &atlas;
    entry.textureId = textureId;
    entry.found = atlas.TryGetTextureSlotSnapshot(
        textureId, entry.width, entry.height, entry.pixels, &entry.isAlpha8);
    outWidth = entry.width;
    outHeight = entry.height;
    outPixels = entry.pixels;
    if (optOutIsAlpha8) *optOutIsAlpha8 = entry.isAlpha8;
    return entry.found;
}

void BlendPremulOver(
    PremulOffscreenLayer& layer,
    int px,
    int py,
    float premulR,
    float premulG,
    float premulB,
    float srcAlpha01,
    BlendingMode blendMode = BlendingMode::Normal) noexcept {
    const float a = std::clamp(srcAlpha01, 0.0f, 1.0f);
    if (a <= 0.001f) {
        return;
    }
    const int lx = px - layer.originX;
    const int ly = py - layer.originY;
    if (lx < 0 || lx >= layer.width || ly < 0 || ly >= layer.height) {
        return;
    }
    float* dst = layer.premulRgba.data() +
        (static_cast<std::size_t>(ly) * static_cast<std::size_t>(layer.width) + static_cast<std::size_t>(lx)) * 4u;
    const float invA = 1.0f - a;
    const float pr = std::clamp(premulR, 0.0f, 1.0f);
    const float pg = std::clamp(premulG, 0.0f, 1.0f);
    const float pb = std::clamp(premulB, 0.0f, 1.0f);
    switch (blendMode) {
        case BlendingMode::Additive:
            dst[0] = std::clamp(pr + dst[0], 0.0f, 1.0f);
            dst[1] = std::clamp(pg + dst[1], 0.0f, 1.0f);
            dst[2] = std::clamp(pb + dst[2], 0.0f, 1.0f);
            break;
        case BlendingMode::Multiply:
            dst[0] = std::clamp(pr * dst[0] + dst[0] * invA, 0.0f, 1.0f);
            dst[1] = std::clamp(pg * dst[1] + dst[1] * invA, 0.0f, 1.0f);
            dst[2] = std::clamp(pb * dst[2] + dst[2] * invA, 0.0f, 1.0f);
            break;
        case BlendingMode::Screen:
            dst[0] = std::clamp(pr + dst[0] * (1.0f - pr), 0.0f, 1.0f);
            dst[1] = std::clamp(pg + dst[1] * (1.0f - pg), 0.0f, 1.0f);
            dst[2] = std::clamp(pb + dst[2] * (1.0f - pb), 0.0f, 1.0f);
            break;
        case BlendingMode::Normal:
        default:
            dst[0] = pr + dst[0] * invA;
            dst[1] = pg + dst[1] * invA;
            dst[2] = pb + dst[2] * invA;
            break;
    }
    dst[3] = a + dst[3] * invA;
}

void BlendPremulOverRgba8(
    std::uint8_t* dstRgba,
    float premulR,
    float premulG,
    float premulB,
    float srcAlpha01,
    BlendingMode blendMode = BlendingMode::Normal) noexcept {
    const float a = std::clamp(srcAlpha01, 0.0f, 1.0f);
    if (a <= 0.001f) {
        return;
    }
    const float invA = 1.0f - a;
    const float pr = std::clamp(premulR, 0.0f, 1.0f);
    const float pg = std::clamp(premulG, 0.0f, 1.0f);
    const float pb = std::clamp(premulB, 0.0f, 1.0f);
    const float dr = static_cast<float>(dstRgba[0]) * (1.0f / 255.0f);
    const float dg = static_cast<float>(dstRgba[1]) * (1.0f / 255.0f);
    const float db = static_cast<float>(dstRgba[2]) * (1.0f / 255.0f);
    const float da = static_cast<float>(dstRgba[3]) * (1.0f / 255.0f);
    float outR = pr + dr * invA;
    float outG = pg + dg * invA;
    float outB = pb + db * invA;
    switch (blendMode) {
        case BlendingMode::Additive:
            outR = pr + dr;
            outG = pg + dg;
            outB = pb + db;
            break;
        case BlendingMode::Multiply:
            outR = pr * dr + dr * invA;
            outG = pg * dg + dg * invA;
            outB = pb * db + db * invA;
            break;
        case BlendingMode::Screen:
            outR = pr + dr * (1.0f - pr);
            outG = pg + dg * (1.0f - pg);
            outB = pb + db * (1.0f - pb);
            break;
        case BlendingMode::Normal:
        default:
            break;
    }
    const float outA = a + da * invA;
    dstRgba[0] = static_cast<std::uint8_t>(std::clamp(outR * 255.0f + 0.5f, 0.0f, 255.0f));
    dstRgba[1] = static_cast<std::uint8_t>(std::clamp(outG * 255.0f + 0.5f, 0.0f, 255.0f));
    dstRgba[2] = static_cast<std::uint8_t>(std::clamp(outB * 255.0f + 0.5f, 0.0f, 255.0f));
    dstRgba[3] = static_cast<std::uint8_t>(std::clamp(outA * 255.0f + 0.5f, 0.0f, 255.0f));
}

[[nodiscard]] Color SampleTextureSlotBilinear(
    std::uint32_t width,
    std::uint32_t height,
    const std::vector<std::uint8_t>& rgbaPixels,
    float u,
    float v,
    bool isAlpha8 = false) noexcept {
    const std::size_t bpp = isAlpha8 ? 1u : 4u;
    if (width == 0u || height == 0u ||
        rgbaPixels.size() < static_cast<std::size_t>(width) * height * bpp) {
        return Color{0.0f, 0.0f, 0.0f, 0.0f};
    }
    const float clampedU = std::clamp(u, 0.0f, 1.0f);
    const float clampedV = std::clamp(v, 0.0f, 1.0f);
    const float texX = clampedU * static_cast<float>(width) - 0.5f;
    const float texY = clampedV * static_cast<float>(height) - 0.5f;

    const int maxX = static_cast<int>(width) - 1;
    const int maxY = static_cast<int>(height) - 1;
    const float flX = std::floor(texX);
    const float flY = std::floor(texY);
    const int x0 = std::clamp(static_cast<int>(flX), 0, maxX);
    const int y0 = std::clamp(static_cast<int>(flY), 0, maxY);
    const int x1 = std::min(x0 + 1, maxX);
    const int y1 = std::min(y0 + 1, maxY);
    const float fx = std::clamp(texX - flX, 0.0f, 1.0f);
    const float fy = std::clamp(texY - flY, 0.0f, 1.0f);

    constexpr float kInv255 = 1.0f / 255.0f;
    if (isAlpha8) {
        const std::size_t stride = static_cast<std::size_t>(width);
        const float c00 = static_cast<float>(rgbaPixels[static_cast<std::size_t>(y0) * stride + x0]);
        const float c10 = static_cast<float>(rgbaPixels[static_cast<std::size_t>(y0) * stride + x1]);
        const float c01 = static_cast<float>(rgbaPixels[static_cast<std::size_t>(y1) * stride + x0]);
        const float c11 = static_cast<float>(rgbaPixels[static_cast<std::size_t>(y1) * stride + x1]);
        const float top = c00 + (c10 - c00) * fx;
        const float bot = c01 + (c11 - c01) * fx;
        return Color{1.0f, 1.0f, 1.0f, (top + (bot - top) * fy) * kInv255};
    }

    const std::size_t stride = static_cast<std::size_t>(width) * 4u;
    const std::uint8_t* p00 = rgbaPixels.data() + static_cast<std::size_t>(y0) * stride + static_cast<std::size_t>(x0) * 4u;
    const std::uint8_t* p10 = rgbaPixels.data() + static_cast<std::size_t>(y0) * stride + static_cast<std::size_t>(x1) * 4u;
    const std::uint8_t* p01 = rgbaPixels.data() + static_cast<std::size_t>(y1) * stride + static_cast<std::size_t>(x0) * 4u;
    const std::uint8_t* p11 = rgbaPixels.data() + static_cast<std::size_t>(y1) * stride + static_cast<std::size_t>(x1) * 4u;

    auto bilerpCh = [&](std::size_t ch) noexcept -> float {
        const float c00 = static_cast<float>(p00[ch]);
        const float c10 = static_cast<float>(p10[ch]);
        const float c01 = static_cast<float>(p01[ch]);
        const float c11 = static_cast<float>(p11[ch]);
        const float top = c00 + (c10 - c00) * fx;
        const float bot = c01 + (c11 - c01) * fx;
        return (top + (bot - top) * fy) * kInv255;
    };
    return Color{bilerpCh(0u), bilerpCh(1u), bilerpCh(2u), bilerpCh(3u)};
}

void SeparableBoxBlurPremul3Pass(PremulOffscreenLayer& layer, int boxRadius);

void SeparableBoxBlurPremul3PassFloat(PremulOffscreenLayer& layer, float boxRadiusF) {
    if (!std::isfinite(boxRadiusF) || boxRadiusF <= 0.05f || layer.width <= 0 || layer.height <= 0) {
        return;
    }
    const int coreR = std::max(0, static_cast<int>(std::floor(boxRadiusF)));
    const float frac = std::clamp(boxRadiusF - static_cast<float>(coreR), 0.0f, 1.0f);
    if (frac <= 0.01f && coreR > 0) {
        SeparableBoxBlurPremul3Pass(layer, coreR);
        return;
    }
    const int w = layer.width;
    const int h = layer.height;
    const float invKernel = 1.0f / (static_cast<float>(coreR * 2 + 1) + 2.0f * frac);
    const std::size_t rowFloats = static_cast<std::size_t>(w) * 4u;
    std::vector<float> scratchBuf;
    ScopedFloatBufferGuard scratchGuard{scratchBuf};
    const std::size_t totalFloats = rowFloats * static_cast<std::size_t>(h);
    gTlPremulLayerPool.Acquire(scratchBuf, totalFloats, false);

    for (int pass = 0; pass < 3; ++pass) {
        float* const scratchBase = scratchBuf.data();
        const float* const srcBase = layer.premulRgba.data();
        auto blurRowsFn = [&](int yStart, int yEnd) {
            for (int y = yStart; y < yEnd; ++y) {
                const float* srcRow = srcBase + static_cast<std::size_t>(y) * rowFloats;
                float* dstRow = scratchBase + static_cast<std::size_t>(y) * rowFloats;
                float sumR = 0.0f, sumG = 0.0f, sumB = 0.0f, sumA = 0.0f;
                for (int k = -coreR; k <= coreR; ++k) {
                    const int sx = std::clamp(k, 0, w - 1);
                    const float* p = srcRow + static_cast<std::size_t>(sx) * 4u;
                    sumR += p[0]; sumG += p[1]; sumB += p[2]; sumA += p[3];
                }
                for (int x = 0; x < w; ++x) {
                    const int leftTap = std::clamp(x - coreR - 1, 0, w - 1);
                    const int rightTap = std::clamp(x + coreR + 1, 0, w - 1);
                    const float* lp = srcRow + static_cast<std::size_t>(leftTap) * 4u;
                    const float* rp = srcRow + static_cast<std::size_t>(rightTap) * 4u;
                    float* dp = dstRow + static_cast<std::size_t>(x) * 4u;
                    dp[0] = (sumR + frac * (lp[0] + rp[0])) * invKernel;
                    dp[1] = (sumG + frac * (lp[1] + rp[1])) * invKernel;
                    dp[2] = (sumB + frac * (lp[2] + rp[2])) * invKernel;
                    dp[3] = (sumA + frac * (lp[3] + rp[3])) * invKernel;
                    const int removeX = std::clamp(x - coreR, 0, w - 1);
                    const int addX = std::clamp(x + coreR + 1, 0, w - 1);
                    const float* remP = srcRow + static_cast<std::size_t>(removeX) * 4u;
                    const float* addP = srcRow + static_cast<std::size_t>(addX) * 4u;
                    sumR += addP[0] - remP[0];
                    sumG += addP[1] - remP[1];
                    sumB += addP[2] - remP[2];
                    sumA += addP[3] - remP[3];
                }
            }
        };
        if (static_cast<std::size_t>(w) * static_cast<std::size_t>(h) >= 16384u) {
            ScanlineWorkerPool::Instance().ParallelBands4(h, blurRowsFn);
        } else {
            blurRowsFn(0, h);
        }

        float* const dstBase = layer.premulRgba.data();
        auto blurColsStripeFn = [&](int cStart, int cEnd) {
            const std::size_t c0 = static_cast<std::size_t>(std::max(0, cStart));
            const std::size_t c1 = static_cast<std::size_t>(std::min(static_cast<int>(rowFloats), cEnd));
            if (c1 <= c0) return;
            const std::size_t stripeLen = c1 - c0;
            thread_local std::vector<float> tlStripeSums;
            if (tlStripeSums.size() < stripeLen) {
                tlStripeSums.resize(stripeLen);
            }
            float* sums = tlStripeSums.data();
            std::fill_n(sums, stripeLen, 0.0f);
            for (int k = -coreR; k <= coreR; ++k) {
                const int sy = std::clamp(k, 0, h - 1);
                const float* srcRow = scratchBase + static_cast<std::size_t>(sy) * rowFloats + c0;
                for (std::size_t i = 0u; i < stripeLen; ++i) {
                    sums[i] += srcRow[i];
                }
            }
            for (int y = 0; y < h; ++y) {
                float* dstRow = dstBase + static_cast<std::size_t>(y) * rowFloats + c0;
                const int topTap = std::clamp(y - coreR - 1, 0, h - 1);
                const int botTap = std::clamp(y + coreR + 1, 0, h - 1);
                const float* topRow = scratchBase + static_cast<std::size_t>(topTap) * rowFloats + c0;
                const float* botRow = scratchBase + static_cast<std::size_t>(botTap) * rowFloats + c0;
                const int removeY = std::clamp(y - coreR, 0, h - 1);
                const int addY = std::clamp(y + coreR + 1, 0, h - 1);
                const float* remRow = scratchBase + static_cast<std::size_t>(removeY) * rowFloats + c0;
                const float* addRow = scratchBase + static_cast<std::size_t>(addY) * rowFloats + c0;
                for (std::size_t i = 0u; i < stripeLen; ++i) {
                    dstRow[i] = (sums[i] + frac * (topRow[i] + botRow[i])) * invKernel;
                    sums[i] += addRow[i] - remRow[i];
                }
            }
        };
        if (!gInScanlineBand && static_cast<std::size_t>(w) * static_cast<std::size_t>(h) >= 16384u && rowFloats >= 64u) {
            ScanlineWorkerPool::Instance().ParallelBands4(static_cast<int>(rowFloats), blurColsStripeFn);
        } else {
            blurColsStripeFn(0, static_cast<int>(rowFloats));
        }
    }
}

void SeparableBoxBlurPremul3Pass(PremulOffscreenLayer& layer, int boxRadius) {
    if (boxRadius <= 0 || layer.width <= 0 || layer.height <= 0) {
        return;
    }
    const int w = layer.width;
    const int h = layer.height;
    const float invKernel = 1.0f / static_cast<float>(boxRadius * 2 + 1);
    const std::size_t rowFloats = static_cast<std::size_t>(w) * 4u;
    std::vector<float> scratchBuf;
    ScopedFloatBufferGuard scratchGuard{scratchBuf};
    const std::size_t totalFloats = rowFloats * static_cast<std::size_t>(h);
    gTlPremulLayerPool.Acquire(scratchBuf, totalFloats, false);

    for (int pass = 0; pass < 3; ++pass) {
        // Horizontal 1D sliding-window box blur: premulRgba -> scratchBuf
        float* const scratchBase = scratchBuf.data();
        const float* const srcBase = layer.premulRgba.data();
        auto blurRowsFn = [&](int yStart, int yEnd) {
            for (int y = yStart; y < yEnd; ++y) {
                const float* srcRow = srcBase + static_cast<std::size_t>(y) * rowFloats;
                float* dstRow = scratchBase + static_cast<std::size_t>(y) * rowFloats;
                float sumR = 0.0f;
                float sumG = 0.0f;
                float sumB = 0.0f;
                float sumA = 0.0f;
                for (int k = -boxRadius; k <= boxRadius; ++k) {
                    const int sx = std::clamp(k, 0, w - 1);
                    const float* p = srcRow + static_cast<std::size_t>(sx) * 4u;
                    sumR += p[0];
                    sumG += p[1];
                    sumB += p[2];
                    sumA += p[3];
                }
                for (int x = 0; x < w; ++x) {
                    float* dp = dstRow + static_cast<std::size_t>(x) * 4u;
                    dp[0] = sumR * invKernel;
                    dp[1] = sumG * invKernel;
                    dp[2] = sumB * invKernel;
                    dp[3] = sumA * invKernel;
                    const int removeX = std::clamp(x - boxRadius, 0, w - 1);
                    const int addX = std::clamp(x + boxRadius + 1, 0, w - 1);
                    const float* remP = srcRow + static_cast<std::size_t>(removeX) * 4u;
                    const float* addP = srcRow + static_cast<std::size_t>(addX) * 4u;
                    sumR += addP[0] - remP[0];
                    sumG += addP[1] - remP[1];
                    sumB += addP[2] - remP[2];
                    sumA += addP[3] - remP[3];
                }
            }
        };
        if (static_cast<std::size_t>(w) * static_cast<std::size_t>(h) >= 16384u) {
            ScanlineWorkerPool::Instance().ParallelBands4(h, blurRowsFn);
        } else {
            blurRowsFn(0, h);
        }

        // Cache-friendly parallel column-striped vertical 1D sliding-window box blur: scratchBuf -> premulRgba
        float* const dstBase = layer.premulRgba.data();
        auto blurColsStripeFn = [&](int cStart, int cEnd) {
            const std::size_t c0 = static_cast<std::size_t>(std::max(0, cStart));
            const std::size_t c1 = static_cast<std::size_t>(std::min(static_cast<int>(rowFloats), cEnd));
            if (c1 <= c0) return;
            const std::size_t stripeLen = c1 - c0;
            thread_local std::vector<float> tlStripeSums;
            if (tlStripeSums.size() < stripeLen) {
                tlStripeSums.resize(stripeLen);
            }
            float* sums = tlStripeSums.data();
            std::fill_n(sums, stripeLen, 0.0f);
            for (int k = -boxRadius; k <= boxRadius; ++k) {
                const int sy = std::clamp(k, 0, h - 1);
                const float* srcRow = scratchBase + static_cast<std::size_t>(sy) * rowFloats + c0;
                for (std::size_t i = 0u; i < stripeLen; ++i) {
                    sums[i] += srcRow[i];
                }
            }
            for (int y = 0; y < h; ++y) {
                float* dstRow = dstBase + static_cast<std::size_t>(y) * rowFloats + c0;
                const int removeY = std::clamp(y - boxRadius, 0, h - 1);
                const int addY = std::clamp(y + boxRadius + 1, 0, h - 1);
                const float* remRow = scratchBase + static_cast<std::size_t>(removeY) * rowFloats + c0;
                const float* addRow = scratchBase + static_cast<std::size_t>(addY) * rowFloats + c0;
                for (std::size_t i = 0u; i < stripeLen; ++i) {
                    dstRow[i] = sums[i] * invKernel;
                    sums[i] += addRow[i] - remRow[i];
                }
            }
        };
        if (!gInScanlineBand && static_cast<std::size_t>(w) * static_cast<std::size_t>(h) >= 16384u && rowFloats >= 64u) {
            ScanlineWorkerPool::Instance().ParallelBands4(static_cast<int>(rowFloats), blurColsStripeFn);
        } else {
            blurColsStripeFn(0, static_cast<int>(rowFloats));
        }
    }
}

template <bool HasFrac>
void SeparableBoxBlurAlpha1ChImpl(
    std::vector<float>& alpha,
    int w,
    int h,
    int coreR,
    float frac,
    float invKernel) {
    const std::size_t rowFloats = static_cast<std::size_t>(w);
    std::vector<float> scratchBuf;
    ScopedFloatBufferGuard scratchGuard{scratchBuf};
    gTlPremulLayerPool.Acquire(scratchBuf, rowFloats * static_cast<std::size_t>(h), false);

    for (int pass = 0; pass < 3; ++pass) {
        float* const scratchBase = scratchBuf.data();
        const float* const srcBase = alpha.data();
        auto blurRowsFn = [&](int yStart, int yEnd) {
            for (int y = yStart; y < yEnd; ++y) {
                const float* srcRow = srcBase + static_cast<std::size_t>(y) * rowFloats;
                float* dstRow = scratchBase + static_cast<std::size_t>(y) * rowFloats;
                float sumA = 0.0f;
                for (int k = -coreR; k <= coreR; ++k) {
                    sumA += srcRow[static_cast<std::size_t>(std::clamp(k, 0, w - 1))];
                }
                for (int x = 0; x < w; ++x) {
                    if constexpr (HasFrac) {
                        const float lTap = srcRow[static_cast<std::size_t>(std::clamp(x - coreR - 1, 0, w - 1))];
                        const float rTap = srcRow[static_cast<std::size_t>(std::clamp(x + coreR + 1, 0, w - 1))];
                        dstRow[x] = (sumA + frac * (lTap + rTap)) * invKernel;
                    } else {
                        dstRow[x] = sumA * invKernel;
                    }
                    const int removeX = std::clamp(x - coreR, 0, w - 1);
                    const int addX = std::clamp(x + coreR + 1, 0, w - 1);
                    sumA += srcRow[addX] - srcRow[removeX];
                }
            }
        };
        if (!gInScanlineBand && static_cast<std::size_t>(w) * static_cast<std::size_t>(h) >= 16384u) {
            ScanlineWorkerPool::Instance().ParallelBands4(h, blurRowsFn);
        } else {
            blurRowsFn(0, h);
        }

        float* const dstBase = alpha.data();
        auto blurColsStripeFn = [&](int cStart, int cEnd) {
            const std::size_t c0 = static_cast<std::size_t>(std::max(0, cStart));
            const std::size_t c1 = static_cast<std::size_t>(std::min(static_cast<int>(rowFloats), cEnd));
            if (c1 <= c0) return;
            const std::size_t stripeLen = c1 - c0;
            thread_local std::vector<float> tlStripeSums;
            if (tlStripeSums.size() < stripeLen) {
                tlStripeSums.resize(stripeLen);
            }
            float* sums = tlStripeSums.data();
            std::fill_n(sums, stripeLen, 0.0f);
            for (int k = -coreR; k <= coreR; ++k) {
                const int sy = std::clamp(k, 0, h - 1);
                const float* srcRow = scratchBase + static_cast<std::size_t>(sy) * rowFloats + c0;
                for (std::size_t i = 0u; i < stripeLen; ++i) {
                    sums[i] += srcRow[i];
                }
            }
            for (int y = 0; y < h; ++y) {
                float* dstRow = dstBase + static_cast<std::size_t>(y) * rowFloats + c0;
                const int removeY = std::clamp(y - coreR, 0, h - 1);
                const int addY = std::clamp(y + coreR + 1, 0, h - 1);
                const float* remRow = scratchBase + static_cast<std::size_t>(removeY) * rowFloats + c0;
                const float* addRow = scratchBase + static_cast<std::size_t>(addY) * rowFloats + c0;
                if constexpr (HasFrac) {
                    const int topTap = std::clamp(y - coreR - 1, 0, h - 1);
                    const int botTap = std::clamp(y + coreR + 1, 0, h - 1);
                    const float* topRow = scratchBase + static_cast<std::size_t>(topTap) * rowFloats + c0;
                    const float* botRow = scratchBase + static_cast<std::size_t>(botTap) * rowFloats + c0;
                    for (std::size_t i = 0u; i < stripeLen; ++i) {
                        dstRow[i] = (sums[i] + frac * (topRow[i] + botRow[i])) * invKernel;
                        sums[i] += addRow[i] - remRow[i];
                    }
                } else {
                    for (std::size_t i = 0u; i < stripeLen; ++i) {
                        dstRow[i] = sums[i] * invKernel;
                        sums[i] += addRow[i] - remRow[i];
                    }
                }
            }
        };
        if (!gInScanlineBand && static_cast<std::size_t>(w) * static_cast<std::size_t>(h) >= 16384u && rowFloats >= 64u) {
            ScanlineWorkerPool::Instance().ParallelBands4(static_cast<int>(rowFloats), blurColsStripeFn);
        } else {
            blurColsStripeFn(0, static_cast<int>(rowFloats));
        }
    }
}

void SeparableBoxBlurAlpha1Ch3PassFloat(std::vector<float>& alpha, int w, int h, float boxRadiusF) {
    if (!std::isfinite(boxRadiusF) || boxRadiusF <= 0.05f || w <= 0 || h <= 0) {
        return;
    }
    const int coreR = std::max(0, static_cast<int>(std::floor(boxRadiusF)));
    const float frac = std::clamp(boxRadiusF - static_cast<float>(coreR), 0.0f, 1.0f);
    if (frac <= 0.01f && coreR > 0) {
        const float invKernel = 1.0f / static_cast<float>(coreR * 2 + 1);
        SeparableBoxBlurAlpha1ChImpl<false>(alpha, w, h, coreR, 0.0f, invKernel);
        return;
    }
    const float invKernel = 1.0f / (static_cast<float>(coreR * 2 + 1) + 2.0f * frac);
    SeparableBoxBlurAlpha1ChImpl<true>(alpha, w, h, coreR, frac, invKernel);
}

[[nodiscard]] Color EvaluateGradientBrushSafe(
    const GradientEvalPod& brush,
    float normU,
    float normV,
    float quadWidthPx,
    float quadHeightPx) noexcept {
    if (brush.kind == GradientBrushKind::Conic) {
        const float effW = std::max(1e-4f, quadWidthPx);
        const float effH = std::max(1e-4f, quadHeightPx);
        const float sampleX = (brush.mappingMode == BrushMappingMode::Absolute) ? (normU * effW) : normU;
        const float sampleY = (brush.mappingMode == BrushMappingMode::Absolute) ? (normV * effH) : normV;
        const float dx = sampleX - brush.center.x;
        const float dy = sampleY - brush.center.y;
        if (std::fabs(dx) + std::fabs(dy) <= 1e-6f) {
            const float safeU = (brush.mappingMode == BrushMappingMode::Absolute)
                ? (brush.center.x / effW)
                : brush.center.x;
            const float nudgeV = (brush.mappingMode == BrushMappingMode::Absolute)
                ? ((brush.center.y - 1e-3f) / effH)
                : (brush.center.y - 1e-3f);
            return EvaluateGradientPodAtNormalizedUv(brush, safeU, nudgeV, quadWidthPx, quadHeightPx);
        }
    }
    return EvaluateGradientPodAtNormalizedUv(brush, normU, normV, quadWidthPx, quadHeightPx);
}

void SamplePremulBilinear(
    const float* src,
    int w,
    int h,
    float fx,
    float fy,
    float out[4]) noexcept {
    const float cx = std::clamp(fx, 0.0f, static_cast<float>(w - 1));
    const float cy = std::clamp(fy, 0.0f, static_cast<float>(h - 1));
    const int x0 = static_cast<int>(std::floor(cx));
    const int y0 = static_cast<int>(std::floor(cy));
    const int x1 = std::min(x0 + 1, w - 1);
    const int y1 = std::min(y0 + 1, h - 1);
    const float tx = cx - static_cast<float>(x0);
    const float ty = cy - static_cast<float>(y0);
    const std::size_t rowFloats = static_cast<std::size_t>(w) * 4u;
    const float* p00 = src + static_cast<std::size_t>(y0) * rowFloats + static_cast<std::size_t>(x0) * 4u;
    const float* p10 = src + static_cast<std::size_t>(y0) * rowFloats + static_cast<std::size_t>(x1) * 4u;
    const float* p01 = src + static_cast<std::size_t>(y1) * rowFloats + static_cast<std::size_t>(x0) * 4u;
    const float* p11 = src + static_cast<std::size_t>(y1) * rowFloats + static_cast<std::size_t>(x1) * 4u;
    for (int ch = 0; ch < 4; ++ch) {
        const float top = p00[ch] + (p10[ch] - p00[ch]) * tx;
        const float bot = p01[ch] + (p11[ch] - p01[ch]) * tx;
        out[ch] = top + (bot - top) * ty;
    }
}

void ApplySaturationPremulLayer(PremulOffscreenLayer& layer, float saturationVal) noexcept {
    const float sat = std::clamp(std::isfinite(saturationVal) ? saturationVal : 1.0f, 0.0f, 4.0f);
    if (std::fabs(sat - 1.0f) <= 0.001f || layer.width <= 0 || layer.height <= 0) {
        return;
    }
    const std::size_t pxCount = static_cast<std::size_t>(layer.width) * static_cast<std::size_t>(layer.height);
    float* p = layer.premulRgba.data();
    for (std::size_t i = 0u; i < pxCount; ++i) {
        float* px = p + i * 4u;
        const float pa = std::clamp(px[3], 0.0f, 1.0f);
        if (pa <= 0.0005f) {
            continue;
        }
        const float lum = 0.2126f * px[0] + 0.7152f * px[1] + 0.0722f * px[2];
#ifdef AERO_SW_SIMD_SSE2
        const __m128 vLum = _mm_set1_ps(lum);
        const __m128 vSat = _mm_set1_ps(sat);
        const __m128 vPx = _mm_loadu_ps(px);
        const __m128 vOut = _mm_min_ps(
            _mm_max_ps(_mm_add_ps(vLum, _mm_mul_ps(vSat, _mm_sub_ps(vPx, vLum))), _mm_setzero_ps()),
            _mm_set1_ps(pa));
        alignas(16) float tmp[4];
        _mm_store_ps(tmp, vOut);
        px[0] = tmp[0];
        px[1] = tmp[1];
        px[2] = tmp[2];
#else
        px[0] = std::clamp(lum + sat * (px[0] - lum), 0.0f, pa);
        px[1] = std::clamp(lum + sat * (px[1] - lum), 0.0f, pa);
        px[2] = std::clamp(lum + sat * (px[2] - lum), 0.0f, pa);
#endif
    }
}

// Replaced TryResolveGradientBrush forward decl below after ApplySpanPostProcessEffect.

[[nodiscard]] float EvaluateOpacityMaskStopsAlpha(
    const std::vector<GradientStop>& sortedStops,
    float clampedT01) noexcept {
    if (sortedStops.empty()) {
        return 1.0f;
    }
    if (sortedStops.size() == 1u || clampedT01 <= sortedStops.front().offset) {
        return std::clamp(sortedStops.front().color.a, 0.0f, 1.0f);
    }
    if (clampedT01 >= sortedStops.back().offset) {
        return std::clamp(sortedStops.back().color.a, 0.0f, 1.0f);
    }
    for (std::size_t i = 0u; i + 1u < sortedStops.size(); ++i) {
        const GradientStop& s0 = sortedStops[i];
        const GradientStop& s1 = sortedStops[i + 1u];
        if (clampedT01 >= s0.offset && clampedT01 <= s1.offset) {
            const float span = s1.offset - s0.offset;
            const float f = (span > 1e-6f) ? std::clamp((clampedT01 - s0.offset) / span, 0.0f, 1.0f) : 0.0f;
            return std::clamp(s0.color.a + (s1.color.a - s0.color.a) * f, 0.0f, 1.0f);
        }
    }
    return std::clamp(sortedStops.back().color.a, 0.0f, 1.0f);
}

void ApplyOpacityMaskToPremulLayer(
    const UIResourceStore* optResourceStore,
    float rootDpiScale,
    PremulOffscreenLayer& layer,
    const BlurLayerSpan& span) {
    if (!span.hasOpacityMask && span.effectMode != UIEffectKind::OpacityMask) {
        return;
    }
    const float maskOp = std::clamp(span.opacityMaskOpacity, 0.0f, 1.0f);
    if (span.opacityMaskGradId == 0u) {
        const float solidA = (static_cast<float>((span.opacityMaskSolidRgba >> 24u) & 0xFFu) / 255.0f) * maskOp;
        if (solidA >= 0.999f) {
            return;
        }
        const std::size_t pxCount = static_cast<std::size_t>(layer.width) * static_cast<std::size_t>(layer.height);
        float* p = layer.premulRgba.data();
        for (std::size_t i = 0u; i < pxCount; ++i) {
            float* px = p + i * 4u;
            px[0] *= solidA;
            px[1] *= solidA;
            px[2] *= solidA;
            px[3] *= solidA;
        }
        return;
    }

    GradientBrushDesc desc{};
    const bool foundGrad =
        (span.opacityMaskGradId != 0u) &&
        ((optResourceStore != nullptr &&
          optResourceStore->GradientBrushes().TryGetBrush(span.opacityMaskGradId, desc)) ||
         GradientBrushRegistry::Instance().TryGetBrush(span.opacityMaskGradId, desc));
    if (!foundGrad || desc.stops.empty()) {
        return;
    }
    for (std::size_t i = 0u; i < desc.stops.size() && i < span.opacityMaskStopOffsets.size(); ++i) {
        if (std::isfinite(span.opacityMaskStopOffsets[i])) {
            desc.stops[i].offset = std::clamp(span.opacityMaskStopOffsets[i], 0.0f, 1.0f);
        }
    }
    std::stable_sort(
        desc.stops.begin(),
        desc.stops.end(),
        [](const GradientStop& a, const GradientStop& b) noexcept {
            return a.offset < b.offset;
        });

    const float totalOp = std::clamp(desc.opacity, 0.0f, 1.0f) * maskOp;
    const float invDpi = 1.0f / std::max(0.25f, rootDpiScale);
    const float effW = std::max(1e-4f, span.opacityMaskLocalRect.width);
    const float effH = std::max(1e-4f, span.opacityMaskLocalRect.height);
    const int w = layer.width;
    const int h = layer.height;

    auto evalT = [&](float sampleX, float sampleY) noexcept -> float {
        float t = 0.0f;
        if (desc.kind == GradientBrushKind::Linear) {
            const float dx = desc.endPoint.x - desc.startPoint.x;
            const float dy = desc.endPoint.y - desc.startPoint.y;
            const float lenSq = dx * dx + dy * dy;
            if (lenSq > 1e-8f) {
                t = ((sampleX - desc.startPoint.x) * dx + (sampleY - desc.startPoint.y) * dy) / lenSq;
            }
        } else if (desc.kind == GradientBrushKind::Conic) {
            constexpr float kTwoPi = 6.2831853071795864769f;
            constexpr float kHalfPi = 1.5707963267948966192f;
            float angle = std::atan2(sampleY - desc.center.y, sampleX - desc.center.x) + kHalfPi;
            if (angle < 0.0f) {
                angle += kTwoPi;
            } else if (angle >= kTwoPi) {
                angle -= kTwoPi;
            }
            t = angle / kTwoPi;
        } else {
            const float rx = std::max(1e-4f, std::fabs(desc.radiusX));
            const float ry = std::max(1e-4f, std::fabs(desc.radiusY));
            const float qx = (sampleX - desc.center.x) / rx;
            const float qy = (sampleY - desc.center.y) / ry;
            float fx = (desc.gradientOrigin.x - desc.center.x) / rx;
            float fy = (desc.gradientOrigin.y - desc.center.y) / ry;
            const float fLenSq = fx * fx + fy * fy;
            if (fLenSq < 1e-6f) {
                t = std::sqrt(qx * qx + qy * qy);
            } else {
                if (fLenSq > 0.9801f) {
                    const float invFLen = 0.99f / std::sqrt(fLenSq);
                    fx *= invFLen;
                    fy *= invFLen;
                }
                const float dx = qx - fx;
                const float dy = qy - fy;
                const float d2 = dx * dx + dy * dy;
                if (d2 <= 1e-10f) {
                    t = 0.0f;
                } else {
                    const float a = 1.0f - (fx * fx + fy * fy);
                    const float b = fx * dx + fy * dy;
                    const float denom = b + std::sqrt(std::max(0.0f, b * b + a * d2));
                    t = (denom > 1e-8f) ? (d2 / denom) : 1.0f;
                }
            }
        }
        switch (desc.spreadMethod) {
            case GradientSpreadMethod::Repeat:
                return std::clamp(t - std::floor(t), 0.0f, 1.0f);
            case GradientSpreadMethod::Reflect: {
                const float m = std::fmod(std::fabs(t), 2.0f);
                return std::clamp((m > 1.0f) ? (2.0f - m) : m, 0.0f, 1.0f);
            }
            case GradientSpreadMethod::Pad:
            default:
                return std::clamp(t, 0.0f, 1.0f);
        }
    };

    auto maskBandFn = [&](int y0, int y1) {
        for (int ly = y0; ly < y1; ++ly) {
            const float py = (static_cast<float>(layer.originY + ly) + 0.5f) * invDpi;
            float* row = layer.premulRgba.data() +
                static_cast<std::size_t>(ly) * static_cast<std::size_t>(w) * 4u;
            for (int lx = 0; lx < w; ++lx) {
                float* px = row + static_cast<std::size_t>(lx) * 4u;
                if (px[3] <= 0.0005f) {
                    continue;
                }
                const float ptX = (static_cast<float>(layer.originX + lx) + 0.5f) * invDpi;
                const Point localPt = span.opacityMaskInvWorld2D.TransformPoint(Point{ptX, py});
                const float normU = (localPt.x - span.opacityMaskLocalRect.x) / effW;
                const float normV = (localPt.y - span.opacityMaskLocalRect.y) / effH;
                const float sX = (desc.mappingMode == BrushMappingMode::Absolute) ? (normU * effW) : normU;
                const float sY = (desc.mappingMode == BrushMappingMode::Absolute) ? (normV * effH) : normV;
                const float t01 = evalT(sX, sY);
                const float ma = std::clamp(EvaluateOpacityMaskStopsAlpha(desc.stops, t01) * totalOp, 0.0f, 1.0f);
                px[0] *= ma;
                px[1] *= ma;
                px[2] *= ma;
                px[3] *= ma;
            }
        }
    };
    if (!gInScanlineBand && static_cast<std::size_t>(w) * static_cast<std::size_t>(h) >= 16384u) {
        ScanlineWorkerPool::Instance().ParallelBands4(h, maskBandFn);
    } else {
        maskBandFn(0, h);
    }
}

void ApplySpanPostProcessEffect(
    const UIResourceStore* optResourceStore,
    float rootDpiScale,
    PremulOffscreenLayer& layer,
    const BlurLayerSpan& span) {
    if (layer.width <= 0 || layer.height <= 0) {
        return;
    }
    const int w = layer.width;
    const int h = layer.height;
    switch (span.effectMode) {
        case UIEffectKind::Pixelate: {
            const int blockSz = std::max(2, static_cast<int>(std::lround(span.radiusPx)));
            for (int by = 0; by < h; by += blockSz) {
                const int bh = std::min(blockSz, h - by);
                for (int bx = 0; bx < w; bx += blockSz) {
                    const int bw = std::min(blockSz, w - bx);
                    const int cx = bx + bw / 2;
                    const int cy = by + bh / 2;
                    const float* cp = layer.premulRgba.data() +
                        (static_cast<std::size_t>(cy) * static_cast<std::size_t>(w) +
                         static_cast<std::size_t>(cx)) * 4u;
                    const float cr = cp[0], cg = cp[1], cb = cp[2], ca = cp[3];
                    for (int yy = by; yy < by + bh; ++yy) {
                        float* row = layer.premulRgba.data() +
                            static_cast<std::size_t>(yy) * static_cast<std::size_t>(w) * 4u;
                        for (int xx = bx; xx < bx + bw; ++xx) {
                            float* dp = row + static_cast<std::size_t>(xx) * 4u;
                            dp[0] = cr;
                            dp[1] = cg;
                            dp[2] = cb;
                            dp[3] = ca;
                        }
                    }
                }
            }
            break;
        }
        case UIEffectKind::Blur: {
            const float boxRf = ComputeSpanBoxRadiusFloat(span);
            if (boxRf > 0.05f) {
                SeparableBoxBlurPremul3PassFloat(layer, boxRf);
            }
            break;
        }
        case UIEffectKind::BackdropBlur: {
            const float boxRf = ComputeSpanBoxRadiusFloat(span);
            if (boxRf > 0.05f) {
                SeparableBoxBlurPremul3PassFloat(layer, boxRf);
            }
            if (std::isfinite(span.saturation) && std::fabs(span.saturation - 1.0f) > 0.001f) {
                ApplySaturationPremulLayer(layer, span.saturation);
            }
            break;
        }
        case UIEffectKind::Tint: {
            const float effA = std::clamp(
                (std::isfinite(span.effectColor.a) ? span.effectColor.a : 1.0f) *
                    (std::isfinite(span.effectOpacity) ? span.effectOpacity : 1.0f),
                0.0f,
                1.0f);
            if (effA <= 0.0005f) {
                break;
            }
            const float mr = 1.0f + (std::clamp(span.effectColor.r, 0.0f, 1.0f) - 1.0f) * effA;
            const float mg = 1.0f + (std::clamp(span.effectColor.g, 0.0f, 1.0f) - 1.0f) * effA;
            const float mb = 1.0f + (std::clamp(span.effectColor.b, 0.0f, 1.0f) - 1.0f) * effA;
            const std::size_t pxCount = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
            float* p = layer.premulRgba.data();
            for (std::size_t i = 0u; i < pxCount; ++i) {
                float* px = p + i * 4u;
                px[0] = std::clamp(px[0] * mr, 0.0f, px[3]);
                px[1] = std::clamp(px[1] * mg, 0.0f, px[3]);
                px[2] = std::clamp(px[2] * mb, 0.0f, px[3]);
            }
            break;
        }
        case UIEffectKind::Saturation:
            ApplySaturationPremulLayer(layer, span.saturation);
            break;
        case UIEffectKind::ChromaticAberration: {
            const float shift = std::clamp(
                std::isfinite(span.amountX) ? span.amountX : 0.0f, -64.0f, 64.0f);
            if (std::fabs(shift) <= 0.01f) {
                break;
            }
            constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
            const float ang = (std::isfinite(span.angleDeg) ? span.angleDeg : 0.0f) * kDegToRad;
            const float dx = std::cos(ang) * shift;
            const float dy = std::sin(ang) * shift;
            const std::size_t totalFloats = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u;
            std::vector<float> caSrcBuf;
            ScopedFloatBufferGuard caSrcGuard{caSrcBuf};
            gTlPremulLayerPool.Acquire(caSrcBuf, totalFloats, false);
            std::memcpy(caSrcBuf.data(), layer.premulRgba.data(), totalFloats * sizeof(float));
            const float* src = caSrcBuf.data();
            auto caBandFn = [&](int y0, int y1) {
                for (int y = y0; y < y1; ++y) {
                    float* dstRow = layer.premulRgba.data() +
                        static_cast<std::size_t>(y) * static_cast<std::size_t>(w) * 4u;
                    const float* centerRow = src +
                        static_cast<std::size_t>(y) * static_cast<std::size_t>(w) * 4u;
                    for (int x = 0; x < w; ++x) {
                        const float* cp = centerRow + static_cast<std::size_t>(x) * 4u;
                        float sR[4]{};
                        float sB[4]{};
                        SamplePremulBilinear(src, w, h, static_cast<float>(x) - dx, static_cast<float>(y) - dy, sR);
                        SamplePremulBilinear(src, w, h, static_cast<float>(x) + dx, static_cast<float>(y) + dy, sB);
                        const float outA = std::clamp(std::max({cp[3], sR[3], sB[3]}), 0.0f, 1.0f);
                        float* dp = dstRow + static_cast<std::size_t>(x) * 4u;
                        dp[0] = std::clamp(sR[0], 0.0f, outA);
                        dp[1] = std::clamp(cp[1], 0.0f, outA);
                        dp[2] = std::clamp(sB[2], 0.0f, outA);
                        dp[3] = outA;
                    }
                }
            };
            if (!gInScanlineBand && static_cast<std::size_t>(w) * static_cast<std::size_t>(h) >= 16384u) {
                ScanlineWorkerPool::Instance().ParallelBands4(h, caBandFn);
            } else {
                caBandFn(0, h);
            }
            break;
        }
        case UIEffectKind::DirectionalBlur: {
            const float radius = std::clamp(
                std::isfinite(span.radiusPx) ? span.radiusPx : 0.0f, 0.0f, 128.0f);
            if (radius <= 0.25f) {
                break;
            }
            constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
            const float ang = (std::isfinite(span.angleDeg) ? span.angleDeg : 0.0f) * kDegToRad;
            const float ux = std::cos(ang);
            const float uy = std::sin(ang);
            const int halfTaps = std::clamp(static_cast<int>(std::ceil(radius)), 1, 32);
            const float step = radius / static_cast<float>(halfTaps);
            const float invTaps = 1.0f / static_cast<float>(halfTaps * 2 + 1);
            const std::size_t totalFloats = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u;
            std::vector<float> dbSrcBuf;
            ScopedFloatBufferGuard dbSrcGuard{dbSrcBuf};
            gTlPremulLayerPool.Acquire(dbSrcBuf, totalFloats, false);
            std::memcpy(dbSrcBuf.data(), layer.premulRgba.data(), totalFloats * sizeof(float));
            const float* src = dbSrcBuf.data();
            auto dbBandFn = [&](int y0, int y1) {
                for (int y = y0; y < y1; ++y) {
                    float* dstRow = layer.premulRgba.data() +
                        static_cast<std::size_t>(y) * static_cast<std::size_t>(w) * 4u;
                    for (int x = 0; x < w; ++x) {
                        float accR = 0.0f, accG = 0.0f, accB = 0.0f, accA = 0.0f;
                        for (int k = -halfTaps; k <= halfTaps; ++k) {
                            const float off = static_cast<float>(k) * step;
                            float sp[4]{};
                            SamplePremulBilinear(
                                src, w, h,
                                static_cast<float>(x) + off * ux,
                                static_cast<float>(y) + off * uy,
                                sp);
                            accR += sp[0];
                            accG += sp[1];
                            accB += sp[2];
                            accA += sp[3];
                        }
                        float* dp = dstRow + static_cast<std::size_t>(x) * 4u;
                        const float outA = std::clamp(accA * invTaps, 0.0f, 1.0f);
                        dp[0] = std::clamp(accR * invTaps, 0.0f, outA);
                        dp[1] = std::clamp(accG * invTaps, 0.0f, outA);
                        dp[2] = std::clamp(accB * invTaps, 0.0f, outA);
                        dp[3] = outA;
                    }
                }
            };
            if (!gInScanlineBand && static_cast<std::size_t>(w) * static_cast<std::size_t>(h) >= 16384u) {
                ScanlineWorkerPool::Instance().ParallelBands4(h, dbBandFn);
            } else {
                dbBandFn(0, h);
            }
            break;
        }
        case UIEffectKind::DropShadow: {
            constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
            const float ws = (std::isfinite(span.worldScale) && span.worldScale > 0.0f) ? span.worldScale : 1.0f;
            const float dpi = std::max(0.25f, rootDpiScale);
            const float angRad = (std::isfinite(span.angleDeg) ? span.angleDeg : 0.0f) * kDegToRad;
            const float depthPx = (std::isfinite(span.amountX) ? span.amountX : 0.0f) * ws * dpi;
            const float dxPx = std::cos(angRad) * depthPx;
            const float dyPx = -std::sin(angRad) * depthPx;

            std::vector<float> shadowAlpha;
            ScopedFloatBufferGuard shadowAlphaGuard{shadowAlpha};
            const std::size_t pxCount = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
            gTlPremulLayerPool.Acquire(shadowAlpha, pxCount, false);
            const float* src = layer.premulRgba.data();
            float* shAlphaDst = shadowAlpha.data();

            auto sampleAlphaZeroOutside = [src, w, h](float fx, float fy) noexcept -> float {
                if (fx < 0.0f || fy < 0.0f ||
                    fx > static_cast<float>(w - 1) || fy > static_cast<float>(h - 1)) {
                    const int ix = static_cast<int>(std::lround(fx));
                    const int iy = static_cast<int>(std::lround(fy));
                    if (ix < 0 || ix >= w || iy < 0 || iy >= h) {
                        return 0.0f;
                    }
                    return src[(static_cast<std::size_t>(iy) * static_cast<std::size_t>(w) + static_cast<std::size_t>(ix)) * 4u + 3u];
                }
                const int x0 = static_cast<int>(std::floor(fx));
                const int y0 = static_cast<int>(std::floor(fy));
                const int x1 = std::min(x0 + 1, w - 1);
                const int y1 = std::min(y0 + 1, h - 1);
                const float tx = fx - static_cast<float>(x0);
                const float ty = fy - static_cast<float>(y0);
                const std::size_t rowFloats = static_cast<std::size_t>(w) * 4u;
                const float a00 = src[static_cast<std::size_t>(y0) * rowFloats + static_cast<std::size_t>(x0) * 4u + 3u];
                const float a10 = src[static_cast<std::size_t>(y0) * rowFloats + static_cast<std::size_t>(x1) * 4u + 3u];
                const float a01 = src[static_cast<std::size_t>(y1) * rowFloats + static_cast<std::size_t>(x0) * 4u + 3u];
                const float a11 = src[static_cast<std::size_t>(y1) * rowFloats + static_cast<std::size_t>(x1) * 4u + 3u];
                const float top = a00 + (a10 - a00) * tx;
                const float bot = a01 + (a11 - a01) * tx;
                return top + (bot - top) * ty;
            };

            for (int y = 0; y < h; ++y) {
                const float sy = static_cast<float>(y) - dyPx;
                float* dRow = shAlphaDst + static_cast<std::size_t>(y) * static_cast<std::size_t>(w);
                for (int x = 0; x < w; ++x) {
                    dRow[x] = std::clamp(sampleAlphaZeroOutside(static_cast<float>(x) - dxPx, sy), 0.0f, 1.0f);
                }
            }
            const float boxRf = ComputeSpanBoxRadiusFloat(span) * dpi;
            if (boxRf > 0.05f) {
                SeparableBoxBlurAlpha1Ch3PassFloat(shadowAlpha, w, h, boxRf);
                shAlphaDst = shadowAlpha.data();
            }
            const float shColR = std::clamp(span.effectColor.r, 0.0f, 1.0f);
            const float shColG = std::clamp(span.effectColor.g, 0.0f, 1.0f);
            const float shColB = std::clamp(span.effectColor.b, 0.0f, 1.0f);
            const float shColA = std::clamp(span.effectColor.a, 0.0f, 1.0f);
            const float outerOp = std::clamp(std::isfinite(span.effectOpacity) ? span.effectOpacity : 1.0f, 0.0f, 1.0f);
            float* dst = layer.premulRgba.data();
            for (std::size_t i = 0u; i < pxCount; ++i) {
                float* dp = dst + i * 4u;
                const float srcA = std::clamp(dp[3], 0.0f, 1.0f);
                const float shA = std::clamp(shAlphaDst[i] * shColA, 0.0f, 1.0f);
                const float invSrcA = 1.0f - srcA;
                dp[0] = std::clamp((dp[0] + (shColR * shA) * invSrcA) * outerOp, 0.0f, 1.0f);
                dp[1] = std::clamp((dp[1] + (shColG * shA) * invSrcA) * outerOp, 0.0f, 1.0f);
                dp[2] = std::clamp((dp[2] + (shColB * shA) * invSrcA) * outerOp, 0.0f, 1.0f);
                dp[3] = std::clamp((srcA + shA * invSrcA) * outerOp, 0.0f, 1.0f);
            }
            break;
        }
        default:
            break;
    }
    if (span.hasOpacityMask || span.effectMode == UIEffectKind::OpacityMask) {
        ApplyOpacityMaskToPremulLayer(optResourceStore, rootDpiScale, layer, span);
    }
}

[[nodiscard]] const GradientEvalPod* TryResolveGradientEvalPodTls(
    const PagedRenderTree& tree,
    std::uint32_t brushId) noexcept {
    if (brushId == 0u) {
        return nullptr;
    }
    struct TlsGradCacheSlot {
        const GradientBrushRegistry* reg = nullptr;
        std::uint32_t brushId = 0u;
        std::uint32_t revision = 0u;
        GradientEvalPod pod{};
    };
    constexpr std::size_t kGradCacheSlots = 8u;
    thread_local std::array<TlsGradCacheSlot, kGradCacheSlots> tlGradCache{};

    auto tryRegistry = [&](const GradientBrushRegistry* reg) noexcept -> const GradientEvalPod* {
        if (reg == nullptr) {
            return nullptr;
        }
        TlsGradCacheSlot& slot = tlGradCache[brushId & (kGradCacheSlots - 1u)];
        const std::uint32_t curRev = reg->Revision();
        if (slot.reg == reg && slot.brushId == brushId && slot.revision == curRev) {
            return &slot.pod;
        }
        GradientEvalPod pod{};
        std::uint32_t fetchedRev = 0u;
        if (reg->TryGetEvalPodWithRevision(brushId, pod, fetchedRev)) {
            slot.reg = reg;
            slot.brushId = brushId;
            slot.revision = fetchedRev;
            slot.pod = pod;
            return &slot.pod;
        }
        return nullptr;
    };

    if (const UIResourceStore* store = tree.ResourceStore(); store != nullptr) {
        if (const GradientEvalPod* pod = tryRegistry(&store->GradientBrushes())) {
            return pod;
        }
    }
    return tryRegistry(&GradientBrushRegistry::Instance());
}

void RasterizeSingleNode(
    const PagedRenderTree& tree,
    std::uint32_t nid,
    std::uint32_t viewportWidth,
    std::uint32_t viewportHeight,
    std::uint8_t* outRgba8,
    PremulOffscreenLayer* optPremulLayer) {
    const RenderNodeDescriptor* nd = tree.GetNode(nid);
    if (!nd || nd->pageIndex >= tree.PageCount() || !std::isfinite(nd->opacity) || nd->opacity <= 0.001f) {
        return;
    }
    const int vpW = static_cast<int>(std::min<std::uint32_t>(viewportWidth, 0x3FFFFFFFu));
    const int vpH = static_cast<int>(std::min<std::uint32_t>(viewportHeight, 0x3FFFFFFFu));
    const CommandPage& page = tree.GetPage(nd->pageIndex);
    const bool clipOn = (page.header.flags & CommandPageFlag_ScissorClipActive) != 0u;
    if (clipOn &&
        (!std::isfinite(page.header.clipX) || !std::isfinite(page.header.clipY) ||
         !std::isfinite(page.header.clipW) || !std::isfinite(page.header.clipH))) {
        return;
    }
    int clipMinX = clipOn ? SafeCeilToInt(page.header.clipX - 0.5f, 0, vpW) : 0;
    int clipMinY = clipOn ? SafeCeilToInt(page.header.clipY - 0.5f, 0, vpH) : 0;
    int clipMaxX = clipOn
        ? SafeCeilToInt(page.header.clipX + std::max(0.0f, page.header.clipW) - 0.5f, 0, vpW)
        : vpW;
    int clipMaxY = clipOn
        ? SafeCeilToInt(page.header.clipY + std::max(0.0f, page.header.clipH) - 0.5f, 0, vpH)
        : vpH;

    if (optPremulLayer) {
        clipMinX = std::max(clipMinX, optPremulLayer->originX);
        clipMinY = std::max(clipMinY, optPremulLayer->originY);
        clipMaxX = std::min(clipMaxX, optPremulLayer->originX + optPremulLayer->width);
        clipMaxY = std::min(clipMaxY, optPremulLayer->originY + optPremulLayer->height);
    }
    const int effClipMinX = optPremulLayer ? clipMinX : std::max(clipMinX, gRasterBandX0);
    const int effClipMaxX = optPremulLayer ? clipMaxX : std::min(clipMaxX, gRasterBandX1);
    const int effClipMinY = std::max(clipMinY, gRasterBandY0);
    const int effClipMaxY = std::min(clipMaxY, gRasterBandY1);
    if (effClipMaxX <= effClipMinX || effClipMaxY <= effClipMinY) {
        return;
    }

    const FontAtlas& fontAtlasRef = (tree.ResourceStore() && tree.ResourceStore()->TryFonts())
        ? *tree.ResourceStore()->TryFonts()
        : FontAtlas::Instance();
    const ImageAtlas& imageAtlasRef = (tree.ResourceStore() && tree.ResourceStore()->TryImages())
        ? *tree.ResourceStore()->TryImages()
        : ImageAtlas::Instance();
    const std::uint32_t boundTextureId = page.header.reserved[0];
    bool resolvedNodeTex = false;
    std::uint32_t cachedNodeTexW = 0u;
    std::uint32_t cachedNodeTexH = 0u;
    std::shared_ptr<const std::vector<std::uint8_t>> cachedNodeTexPixels;
    bool resolvedNodeMtsdf = false;
    const AxbMtsdfPathEntry* cachedNodeMtsdf = nullptr;
    bool resolvedNodeSdf = false;
    bool hasNodeSdfView = false;
    const std::uint8_t* cachedSdfData = nullptr;
    std::uint16_t cachedSdfW = 0u;
    std::uint16_t cachedSdfH = 0u;
    std::uint16_t cachedSdfGlyphH = 0u;

    const std::size_t quadCount = nd->vertexCount / 4u;
    for (std::size_t q = 0; q < quadCount; ++q) {
        const std::size_t vIdx = nd->vertexOffsetInPage + q * 4u;
        if (vIdx + 4u > kVerticesPerCommandPage) {
            break;
        }
        const AeroVertex& v0 = page.vertices[vIdx + 0u];
        const AeroVertex& v1 = page.vertices[vIdx + 1u];
        const AeroVertex& v2 = page.vertices[vIdx + 2u];
        const AeroVertex& v3 = page.vertices[vIdx + 3u];
        if (((v0.rgba | v1.rgba | v2.rgba | v3.rgba) & 0xFF000000u) == 0u) {
            continue;
        }
        if (!std::isfinite(v0.x) || !std::isfinite(v0.y) ||
            !std::isfinite(v1.x) || !std::isfinite(v1.y) ||
            !std::isfinite(v2.x) || !std::isfinite(v2.y) ||
            !std::isfinite(v3.x) || !std::isfinite(v3.y)) {
            continue;
        }

        const float minY = std::min(std::min(v0.y, v1.y), std::min(v2.y, v3.y));
        const float maxY = std::max(std::max(v0.y, v1.y), std::max(v2.y, v3.y));
        const int pyStart = SafeFloorToInt(minY, clipMinY, clipMaxY);
        const int pyEnd = SafeCeilToInt(maxY, clipMinY, clipMaxY);
        const int quadBandPyStart = std::max(pyStart, gRasterBandY0);
        const int quadBandPyEnd = std::min(pyEnd, gRasterBandY1);
        if (quadBandPyStart >= quadBandPyEnd) {
            continue;
        }

        const float minX = std::min(std::min(v0.x, v1.x), std::min(v2.x, v3.x));
        const float maxX = std::max(std::max(v0.x, v1.x), std::max(v2.x, v3.x));
        const int pxStart = SafeFloorToInt(minX, clipMinX, clipMaxX);
        const int pxEnd = SafeCeilToInt(maxX, clipMinX, clipMaxX);
        if (pxEnd <= pxStart) {
            continue;
        }

        const bool uniformVertexColor =
            (v0.rgba == v1.rgba) && (v0.rgba == v2.rgba) && (v0.rgba == v3.rgba);
        const std::uint8_t a8 = static_cast<std::uint8_t>((v0.rgba >> 24u) & 0xFFu);
        const std::uint8_t r8 = static_cast<std::uint8_t>(v0.rgba & 0xFFu);
        const std::uint8_t g8 = static_cast<std::uint8_t>((v0.rgba >> 8u) & 0xFFu);
        const std::uint8_t b8 = static_cast<std::uint8_t>((v0.rgba >> 16u) & 0xFFu);
        const float baseAlpha = static_cast<float>(a8) / 255.0f;
        const AeroPrimType prim = DecodeVertexPrimType(v0.primType);
        bool resolvedNodeTexAlpha8 = false;
        if (prim == AeroPrimType::TexturedQuad && !resolvedNodeTex) {
            (void)TryGetTextureSlotCached(
                imageAtlasRef, boundTextureId, cachedNodeTexW, cachedNodeTexH, cachedNodeTexPixels,
                &resolvedNodeTexAlpha8);
            resolvedNodeTex = true;
        }
        if (prim == AeroPrimType::SdfTextGlyph && !resolvedNodeSdf) {
            hasNodeSdfView = fontAtlasRef.AcquireSdfView(
                cachedSdfData, cachedSdfW, cachedSdfH, cachedSdfGlyphH);
            resolvedNodeSdf = true;
        }
        const bool hasBoundTexture =
            (cachedNodeTexPixels != nullptr && cachedNodeTexW > 0u && cachedNodeTexH > 0u);

        float e1x = v1.x - v0.x;
        float e1y = v1.y - v0.y;
        float e3x = v3.x - v0.x;
        float e3y = v3.y - v0.y;
        if (nd->hasPerspective3D) {
            e1x = 0.5f * ((v1.x - v0.x) + (v2.x - v3.x));
            e1y = 0.5f * ((v1.y - v0.y) + (v2.y - v3.y));
            e3x = 0.5f * ((v3.x - v0.x) + (v2.x - v1.x));
            e3y = 0.5f * ((v3.y - v0.y) + (v2.y - v1.y));
        }
        const float det = e1x * e3y - e1y * e3x;
        if (!std::isfinite(det) || std::fabs(det) <= 1e-4f) {
            continue;
        }
        const float invDet = 1.0f / det;
        const float localQuadW = std::sqrt(e1x * e1x + e1y * e1y);
        const float localQuadH = std::sqrt(e3x * e3x + e3y * e3y);
        if (localQuadW <= 0.01f || localQuadH <= 0.01f) {
            continue;
        }

        bool usePerspectiveInverse = false;
        float perspOrigX = nd->x;
        float perspOrigY = nd->y;
        float perspSpanW = nd->width;
        float perspSpanH = nd->height;
        if (nd->hasPerspective3D) {
            if (quadCount == 1u && prim != AeroPrimType::SdfTextGlyph &&
                nd->width > 1e-3f && nd->height > 1e-3f) {
                usePerspectiveInverse = true;
            } else {
                const Matrix4x4& wm3D = tree.GetNodeWorldMatrix3D(*nd);
                Point pLoc0{};
                Point pLoc2{};
                if (wm3D.InverseProjectScreenToLocalPlane(Point{v0.x, v0.y}, pLoc0) &&
                    wm3D.InverseProjectScreenToLocalPlane(Point{v2.x, v2.y}, pLoc2)) {
                    perspOrigX = pLoc0.x;
                    perspOrigY = pLoc0.y;
                    perspSpanW = pLoc2.x - pLoc0.x;
                    perspSpanH = pLoc2.y - pLoc0.y;
                    if (std::fabs(perspSpanW) > 1e-3f && std::fabs(perspSpanH) > 1e-3f) {
                        usePerspectiveInverse = true;
                    }
                }
            }
        }

        float pInv00 = 0.0f, pInv01 = 0.0f, pInv02 = 0.0f;
        float pInv10 = 0.0f, pInv11 = 0.0f, pInv12 = 0.0f;
        float pInv20 = 0.0f, pInv21 = 0.0f, pInv22 = 0.0f;
        float pH20 = 0.0f, pH21 = 0.0f, pH22 = 1.0f;
        float invPerspSpanW = 0.0f, invPerspSpanH = 0.0f;
        if (usePerspectiveInverse) {
            const auto& m = tree.GetNodeWorldMatrix3D(*nd).m;
            const float h00 = m[0][0], h01 = m[0][1], h02 = m[0][3];
            const float h10 = m[1][0], h11 = m[1][1], h12 = m[1][3];
            const float h20 = m[3][0], h21 = m[3][1], h22 = m[3][3];
            const float c00 = h11 * h22 - h12 * h21;
            const float c01 = h12 * h20 - h10 * h22;
            const float c02 = h10 * h21 - h11 * h20;
            const float hDet = h00 * c00 + h01 * c01 + h02 * c02;
            if (std::fabs(hDet) <= 1e-10f || std::fabs(perspSpanW) <= 1e-6f || std::fabs(perspSpanH) <= 1e-6f) {
                continue;
            }
            pInv00 = c00;
            pInv01 = h02 * h21 - h01 * h22;
            pInv02 = h01 * h12 - h02 * h11;
            pInv10 = c01;
            pInv11 = h00 * h22 - h02 * h20;
            pInv12 = h02 * h10 - h00 * h12;
            pInv20 = c02;
            pInv21 = h01 * h20 - h00 * h21;
            pInv22 = h00 * h11 - h01 * h10;
            pH20 = h20;
            pH21 = h21;
            pH22 = h22;
            invPerspSpanW = 1.0f / perspSpanW;
            invPerspSpanH = 1.0f / perspSpanH;
        }

        if (nd->primType == AeroPrimType::MsdfVectorPath && nd->mtsdfPathHash != 0 && !resolvedNodeMtsdf) {
            cachedNodeMtsdf = tree.FindMtsdfEntry(nd->mtsdfPathHash);
            if (!cachedNodeMtsdf) {
                const UIResourceStore* store = tree.ResourceStore() ? tree.ResourceStore() : &UIResourceStore::Default();
                cachedNodeMtsdf = store->FindMtsdfPath(nd->mtsdfPathHash);
            }
            resolvedNodeMtsdf = true;
        }
        const AxbMtsdfPathEntry* mtsdfEntry = cachedNodeMtsdf;

        CornerRadius sdfRadii = nd->cornerRadii;
        Thickness sdfBorder = nd->borderThicknesses;
        bool sdfHollowBorder = nd->isHollowBorder;
        if (prim == AeroPrimType::SdfRoundedRect) {
            const auto expectedPrim = static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(nd->primType) | (nd->encodedPrimExtra & 0xFFF8u));
            if (v0.primType != expectedPrim || v0.flags != nd->encodedSdfParams) {
                UnpackSdfRoundedRectVertexParams(
                    v0.primType,
                    v0.flags,
                    sdfRadii,
                    sdfBorder,
                    sdfHollowBorder);
            }
        }


        const float quadUnscaledHalfW = std::max(std::fabs(v0.u), std::fabs(v2.u));
        const float quadUnscaledHalfH = std::max(std::fabs(v0.v), std::fabs(v2.v));
        const float quadHalfW = (quadUnscaledHalfW > 1e-4f) ? quadUnscaledHalfW : (localQuadW * 0.5f);
        const float quadHalfH = (quadUnscaledHalfH > 1e-4f) ? quadUnscaledHalfH : (localQuadH * 0.5f);
        const float quadAaFeather = std::max(
            1e-4f,
            (quadHalfW * localQuadH + quadHalfH * localQuadW) * std::fabs(invDet));
        const float perpQuadW = std::fabs(det) / std::max(localQuadH, 1e-4f);
        const float perpQuadH = std::fabs(det) / std::max(localQuadW, 1e-4f);
        const bool uniformSdfCr = IsUniformCornerRadius(sdfRadii);

        const std::uint32_t pathGradId =
            (prim == AeroPrimType::MsdfVectorPath ||
             prim == AeroPrimType::TexturedQuad ||
             prim == AeroPrimType::SdfEllipse)
            ? ((nd->gradientBrushId != 0u)
                   ? nd->gradientBrushId
                   : static_cast<std::uint32_t>((v0.primType >> 4u) & 0x0FFFu))
            : 0u;
        const GradientEvalPod* hoistedPathGradBrush = (pathGradId != 0u)
            ? TryResolveGradientEvalPodTls(tree, pathGradId)
            : nullptr;

        std::uint32_t hoistedRectGradId = 0u;
        float hoistedGradRadius = 0.0f;
        bool hoistedGradIsEllipse = false;
        const GradientEvalPod* hoistedRectGradBrush = nullptr;
        bool hoistedUniformGradCr = true;
        float hoistedMaxGradCr = 0.0f;
        if (prim == AeroPrimType::GradientRoundedRect) {
            UnpackGradientRoundedRectVertexParams(
                v0.primType,
                v0.flags,
                hoistedRectGradId,
                hoistedGradRadius,
                hoistedGradIsEllipse);
            if (hoistedRectGradId == 0u) {
                hoistedRectGradId = nd->gradientBrushId;
            }
            if (hoistedRectGradId != 0u) {
                hoistedRectGradBrush = TryResolveGradientEvalPodTls(tree, hoistedRectGradId);
            }
            hoistedUniformGradCr = IsUniformCornerRadius(nd->cornerRadii);
            hoistedMaxGradCr = hoistedUniformGradCr
                ? hoistedGradRadius
                : std::max(std::max(nd->cornerRadii.topLeft, nd->cornerRadii.topRight),
                           std::max(nd->cornerRadii.bottomRight, nd->cornerRadii.bottomLeft));
        }

        float hoistedSdfScreenPxRange = 1.25f;
        float hoistedSdfUSpan = 0.0f;
        float hoistedSdfVSpan = 0.0f;
        float hoistedSdfDuV = 0.0f;
        float hoistedSdfDvU = 0.0f;
        if (prim == AeroPrimType::SdfTextGlyph && hasNodeSdfView) {
            hoistedSdfUSpan = v1.u - v0.u;
            hoistedSdfVSpan = v3.v - v0.v;
            hoistedSdfDuV = v3.u - v0.u;
            hoistedSdfDvU = v1.v - v0.v;
            const float texW = std::max(1.0f, std::fabs(hoistedSdfUSpan) * static_cast<float>(cachedSdfW));
            const float texH = std::max(1.0f, std::fabs(hoistedSdfVSpan) * static_cast<float>(cachedSdfH));
            const float scaleX = std::max(1.0f, perpQuadW) / texW;
            const float scaleY = std::max(1.0f, perpQuadH) / texH;
            const float avgScale = 0.5f * (scaleX + scaleY);
            hoistedSdfScreenPxRange = std::max(2.0f * std::max(FontAtlas::kDefaultSdfPxRange, 1.0f) * avgScale, 1.25f);
        }

        float hoistedMtsdfStrokePx = 0.0f;
        bool hoistedMtsdfStrokeOnly = false;
        float hoistedFullCellW = 0.0f;
        float hoistedFullCellH = 0.0f;
        float hoistedEntrySpanU = 1.0f;
        float hoistedEntrySpanV = 1.0f;
        if (prim == AeroPrimType::MsdfVectorPath && mtsdfEntry &&
            mtsdfEntry->cellWidth > 0 && mtsdfEntry->cellHeight > 0) {
            float dummyUvRange = 0.0f;
            UnpackMtsdfParams(v0.flags, dummyUvRange, hoistedMtsdfStrokePx);
            hoistedMtsdfStrokeOnly = IsMtsdfStrokeOnly(v0.flags);
            const float cellW = static_cast<float>(mtsdfEntry->cellWidth);
            const float cellH = static_cast<float>(mtsdfEntry->cellHeight);
            const float uMin = std::min(v0.u, v2.u);
            const float uMax = std::max(v0.u, v2.u);
            const float vMin = std::min(v0.v, v2.v);
            const float vMax = std::max(v0.v, v2.v);
            hoistedEntrySpanU = std::max(1e-6f, mtsdfEntry->u1 - mtsdfEntry->u0);
            hoistedEntrySpanV = std::max(1e-6f, mtsdfEntry->v1 - mtsdfEntry->v0);
            const float quadTexSpanX = std::max(1e-3f, ((uMax - uMin) / hoistedEntrySpanU) * std::max(1.0f, cellW - 1.0f));
            const float quadTexSpanY = std::max(1e-3f, ((vMax - vMin) / hoistedEntrySpanV) * std::max(1.0f, cellH - 1.0f));
            hoistedFullCellW = cellW * (perpQuadW / quadTexSpanX);
            hoistedFullCellH = cellH * (perpQuadH / quadTexSpanY);
        }



        float hoistedArcStartDeg = 0.0f;
        float hoistedArcSweepDeg = 0.0f;
        float hoistedArcInnerRatio = 0.0f;
        if (prim == AeroPrimType::SdfRadialArc) {
            UnpackSdfRadialArcParams(v0.flags, hoistedArcStartDeg, hoistedArcSweepDeg, hoistedArcInnerRatio);
        }
        float hoistedEllipseStrokePx = 0.0f;
        bool hoistedEllipseHollow = false;
        if (prim == AeroPrimType::SdfEllipse) {
            UnpackSdfEllipseParams(v0.flags, hoistedEllipseStrokePx, hoistedEllipseHollow);
        }

        float sdfSafeInteriorHalfW = -1.0f;
        float sdfSafeInteriorHalfH = -1.0f;
        if (prim == AeroPrimType::SdfRoundedRect && nd->softFeatherPx <= 0.0f && !sdfHollowBorder) {
            const float maxRad = std::max(
                std::max(sdfRadii.topLeft, sdfRadii.topRight),
                std::max(sdfRadii.bottomRight, sdfRadii.bottomLeft));
            const float maxBdr = std::max(
                std::max(sdfBorder.left, sdfBorder.right),
                std::max(sdfBorder.top, sdfBorder.bottom));
            const float pad = std::max(0.0f, nd->sdfPadPx);
            sdfSafeInteriorHalfW = quadHalfW - pad - maxRad - maxBdr - quadAaFeather - 0.5f;
            sdfSafeInteriorHalfH = quadHalfH - pad - maxRad - maxBdr - quadAaFeather - 0.5f;
        }

        const int bandClampedPyStart = quadBandPyStart;
        const int bandClampedPyEnd = quadBandPyEnd;
        const float stepSuX = e3y * invDet;
        const float stepSvX = -e1y * invDet;

        // Precompute non-zero RGB texel X bounds per MTSDF row for O(1) empty-space skipping on large vector artboards!
        std::int16_t mtsdfRowMinX[128];
        std::int16_t mtsdfRowMaxX[128];
        const std::int16_t* rowMinPtr = mtsdfRowMinX;
        const std::int16_t* rowMaxPtr = mtsdfRowMaxX;
        bool hasAxisAlignedMtsdfBounds = false;
        float mtsdfAxisTexX0 = 0.0f;
        float mtsdfAxisTexStepX = 0.0f;
        float mtsdfInvAxisTexStepX = 0.0f;
        if (prim == AeroPrimType::MsdfVectorPath && mtsdfEntry &&
            !usePerspectiveInverse && nd->softFeatherPx <= 0.0f && hoistedMtsdfStrokePx <= 0.0f && !hoistedMtsdfStrokeOnly &&
            mtsdfEntry->cellWidth > 0 && mtsdfEntry->cellWidth <= 128 &&
            mtsdfEntry->cellHeight > 0 && mtsdfEntry->cellHeight <= 128 &&
            mtsdfEntry->mtsdfRgbaPixels.size() >= static_cast<std::size_t>(mtsdfEntry->cellWidth) * mtsdfEntry->cellHeight * 4u &&
            std::fabs(e1y) <= 1e-6f && std::fabs(e3x) <= 1e-6f &&
            std::fabs(v3.u - v0.u) <= 1e-6f && std::fabs(v1.v - v0.v) <= 1e-6f &&
            (pxEnd - pxStart) * (bandClampedPyEnd - bandClampedPyStart) >= 256) {
            const int cw = static_cast<int>(mtsdfEntry->cellWidth);
            const int ch = static_cast<int>(mtsdfEntry->cellHeight);
            if (mtsdfEntry->rowMinX.size() == static_cast<std::size_t>(ch) &&
                mtsdfEntry->rowMaxX.size() == static_cast<std::size_t>(ch)) {
                rowMinPtr = mtsdfEntry->rowMinX.data();
                rowMaxPtr = mtsdfEntry->rowMaxX.data();
            } else {
                const std::uint8_t* mBase = mtsdfEntry->mtsdfRgbaPixels.data();
                for (int ry = 0; ry < ch; ++ry) {
                    const std::uint8_t* rPtr = mBase + static_cast<std::size_t>(ry) * static_cast<std::size_t>(cw) * 4u;
                    std::int16_t rMin = static_cast<std::int16_t>(cw);
                    std::int16_t rMax = -1;
                    for (int rx = 0; rx < cw; ++rx) {
                        const std::uint8_t* p = rPtr + static_cast<std::size_t>(rx) * 4u;
                        if ((p[0] | p[1] | p[2]) != 0u) {
                            if (rx < rMin) rMin = static_cast<std::int16_t>(rx);
                            rMax = static_cast<std::int16_t>(rx);
                        }
                    }
                    mtsdfRowMinX[ry] = rMin;
                    mtsdfRowMaxX[ry] = rMax;
                }
            }
            const float cellW = static_cast<float>(cw);
            const float duSu = ((v1.u - v0.u) / hoistedEntrySpanU) * std::max(1.0f, cellW - 1.0f);
            mtsdfAxisTexStepX = stepSuX * duSu;
            if (std::fabs(mtsdfAxisTexStepX) > 1e-6f) {
                mtsdfAxisTexX0 = ((v0.u - mtsdfEntry->u0) / hoistedEntrySpanU) * std::max(1.0f, cellW - 1.0f);
                mtsdfInvAxisTexStepX = 1.0f / mtsdfAxisTexStepX;
                hasAxisAlignedMtsdfBounds = true;
            }
        }
        auto narrowLinearInterval = [](float a0, float slope, int& ioX0, int& ioX1) noexcept {
            // Restrict px so that a0 + (px + 0.5f) * slope in [0, 1] (with 1px safety margin)
            if (std::fabs(slope) > 1e-6f) {
                const float invS = 1.0f / slope;
                const float pA = (-a0) * invS - 0.5f;
                const float pB = (1.0f - a0) * invS - 0.5f;
                const float lo = std::min(pA, pB);
                const float hi = std::max(pA, pB);
                if (std::isfinite(lo) && std::isfinite(hi)) {
                    const int boundLo = ioX0 - 2;
                    const int boundHi = ioX1 + 2;
                    ioX0 = std::max(ioX0, SafeFloorToInt(lo, boundLo, boundHi) - 1);
                    ioX1 = std::min(ioX1, SafeCeilToInt(hi, boundLo, boundHi) + 2);
                    ioX0 = std::clamp(ioX0, boundLo, boundHi);
                    ioX1 = std::clamp(ioX1, boundLo, boundHi);
                }
            } else if (a0 < -1e-4f || a0 > 1.0001f) {
                ioX1 = ioX0;
            }
        };

        const bool canHoistTexturedRowY =
            (prim == AeroPrimType::TexturedQuad) && hasBoundTexture &&
            !usePerspectiveInverse && e1y == 0.0f && e3x == 0.0f &&
            v0.v == v1.v && v3.v == v2.v;
        const std::uint8_t* hoistedTexPixelsData = hasBoundTexture ? cachedNodeTexPixels->data() : nullptr;
        const int hoistedTexMaxX = hasBoundTexture ? (static_cast<int>(cachedNodeTexW) - 1) : 0;
        const int hoistedTexMaxY = hasBoundTexture ? (static_cast<int>(cachedNodeTexH) - 1) : 0;
        const std::size_t hoistedTexStride = hasBoundTexture
            ? (static_cast<std::size_t>(cachedNodeTexW) * (resolvedNodeTexAlpha8 ? 1u : 4u))
            : 0u;
        const std::uint8_t* rowTexPtr0 = nullptr;
        const std::uint8_t* rowTexPtr1 = nullptr;
        float rowTexFy = 0.0f;

        float perspRowU = 0.0f, perspRowV = 0.0f, perspRowW = 0.0f;
        for (int py = bandClampedPyStart; py < bandClampedPyEnd; ++py) {
            const float sy = static_cast<float>(py) + 0.5f;
            const float dy = sy - v0.y;
            int rowPxStart = pxStart;
            int rowPxEnd = pxEnd;
            if (!usePerspectiveInverse) {
                const float baseSu = (-v0.x * e3y - dy * e3x) * invDet;
                const float baseSv = (e1x * dy + e1y * v0.x) * invDet;
                narrowLinearInterval(baseSu, stepSuX, rowPxStart, rowPxEnd);
                if (rowPxStart < rowPxEnd) {
                    narrowLinearInterval(baseSv, stepSvX, rowPxStart, rowPxEnd);
                }
                rowPxStart = std::max(effClipMinX, rowPxStart);
                rowPxEnd = std::min(effClipMaxX, rowPxEnd);
                if (rowPxStart >= rowPxEnd) {
                    continue;
                }
                if (canHoistTexturedRowY) {
                    const float svRow = (e1x * dy) * invDet;
                    const float clampedV = std::clamp(v0.v + svRow * (v3.v - v0.v), 0.0f, 1.0f);
                    const float texY = clampedV * static_cast<float>(cachedNodeTexH) - 0.5f;
                    const float flY = std::floor(texY);
                    const int y0 = std::clamp(static_cast<int>(flY), 0, hoistedTexMaxY);
                    const int y1 = std::min(y0 + 1, hoistedTexMaxY);
                    rowTexFy = std::clamp(texY - flY, 0.0f, 1.0f);
                    rowTexPtr0 = hoistedTexPixelsData + static_cast<std::size_t>(y0) * hoistedTexStride;
                    rowTexPtr1 = hoistedTexPixelsData + static_cast<std::size_t>(y1) * hoistedTexStride;
                }
                if (hasAxisAlignedMtsdfBounds) {
                    const float svA = baseSv + (static_cast<float>(rowPxStart) + 0.5f) * stepSvX;
                    const float svB = baseSv + (static_cast<float>(rowPxEnd) - 0.5f) * stepSvX;
                    const float svLo = std::min(svA, svB);
                    const float svHi = std::max(svA, svB);
                    if (svHi < 0.0f || svLo > 1.0f) {
                        continue;
                    }
                    const float cellW = static_cast<float>(mtsdfEntry->cellWidth);
                    const float cellH = static_cast<float>(mtsdfEntry->cellHeight);
                    const float dvSu = std::fabs(v1.v - v0.v);
                    const float vA = v0.v + std::clamp(svLo, 0.0f, 1.0f) * (v3.v - v0.v);
                    const float vB = v0.v + std::clamp(svHi, 0.0f, 1.0f) * (v3.v - v0.v);
                    const float vMinRow = std::min(vA, vB) - dvSu;
                    const float vMaxRow = std::max(vA, vB) + dvSu;
                    const float tvLo = std::clamp((0.5f + ((vMinRow - mtsdfEntry->v0) / hoistedEntrySpanV) * std::max(1.0f, cellH - 1.0f)) / cellH, 0.0f, 1.0f) * cellH - 0.5f;
                    const float tvHi = std::clamp((0.5f + ((vMaxRow - mtsdfEntry->v0) / hoistedEntrySpanV) * std::max(1.0f, cellH - 1.0f)) / cellH, 0.0f, 1.0f) * cellH - 0.5f;
                    const int maxTexY = static_cast<int>(mtsdfEntry->cellHeight) - 1;
                    const int ty0 = std::clamp(static_cast<int>(std::floor(std::min(tvLo, tvHi) - 1e-4f)), 0, maxTexY);
                    const int ty1 = std::clamp(static_cast<int>(std::floor(std::max(tvLo, tvHi) + 1e-4f)) + 1, 0, maxTexY);
                    int activeMinTexX = 32767;
                    int activeMaxTexX = -1;
                    for (int ty = ty0; ty <= ty1; ++ty) {
                        if (rowMaxPtr[ty] >= 0) {
                            activeMinTexX = std::min<int>(activeMinTexX, rowMinPtr[ty]);
                            activeMaxTexX = std::max<int>(activeMaxTexX, rowMaxPtr[ty]);
                        }
                    }
                    if (activeMaxTexX < 0) {
                        continue;
                    }
                    if (activeMinTexX > 0 || activeMaxTexX + 1 < static_cast<int>(mtsdfEntry->cellWidth) - 1) {
                        const float duSu = ((v1.u - v0.u) / hoistedEntrySpanU) * std::max(1.0f, cellW - 1.0f);
                        const float duSv = ( std::fabs(v3.u - v0.u) / hoistedEntrySpanU ) * std::max(1.0f, cellW - 1.0f);
                        const float rowTexX0 = mtsdfAxisTexX0 + baseSu * duSu;
                        const float tLo = static_cast<float>(activeMinTexX) - 1.02f - duSv;
                        const float tHi = static_cast<float>(activeMaxTexX) + 2.02f + duSv;
                        const float pA = (tLo - rowTexX0) * mtsdfInvAxisTexStepX - 0.5f;
                        const float pB = (tHi - rowTexX0) * mtsdfInvAxisTexStepX - 0.5f;
                        if (std::isfinite(pA) && std::isfinite(pB)) {
                            if (mtsdfAxisTexStepX > 0.0f) {
                                if (activeMinTexX > 0) {
                                    rowPxStart = std::max(rowPxStart, SafeFloorToInt(pA, rowPxStart - 2, rowPxEnd + 2));
                                }
                                if (activeMaxTexX + 1 < static_cast<int>(mtsdfEntry->cellWidth) - 1) {
                                    rowPxEnd = std::min(rowPxEnd, SafeCeilToInt(pB, rowPxStart - 2, rowPxEnd + 2));
                                }
                            } else {
                                if (activeMaxTexX + 1 < static_cast<int>(mtsdfEntry->cellWidth) - 1) {
                                    rowPxStart = std::max(rowPxStart, SafeFloorToInt(pB, rowPxStart - 2, rowPxEnd + 2));
                                }
                                if (activeMinTexX > 0) {
                                    rowPxEnd = std::min(rowPxEnd, SafeCeilToInt(pA, rowPxStart - 2, rowPxEnd + 2));
                                }
                            }
                            if (rowPxStart >= rowPxEnd) {
                                continue;
                            }
                        }
                    }
                }
            } else {
                float minEdgeX = static_cast<float>(pxEnd + 4);
                float maxEdgeX = static_cast<float>(pxStart - 4);
                auto accumEdgeSpan = [&](const AeroVertex& a, const AeroVertex& b) noexcept {
                    const float yLo = std::min(a.y, b.y);
                    const float yHi = std::max(a.y, b.y);
                    if (sy + 0.6f < yLo || sy - 0.6f > yHi) {
                        return;
                    }
                    const float dyEdge = b.y - a.y;
                    if (std::fabs(dyEdge) <= 1e-5f) {
                        minEdgeX = std::min(minEdgeX, std::min(a.x, b.x));
                        maxEdgeX = std::max(maxEdgeX, std::max(a.x, b.x));
                        return;
                    }
                    const float invDy = 1.0f / dyEdge;
                    const float t0 = std::clamp((sy - 0.6f - a.y) * invDy, 0.0f, 1.0f);
                    const float t1 = std::clamp((sy + 0.6f - a.y) * invDy, 0.0f, 1.0f);
                    const float x0 = a.x + (b.x - a.x) * t0;
                    const float x1 = a.x + (b.x - a.x) * t1;
                    minEdgeX = std::min(minEdgeX, std::min(x0, x1));
                    maxEdgeX = std::max(maxEdgeX, std::max(x0, x1));
                };
                accumEdgeSpan(v0, v1);
                accumEdgeSpan(v1, v2);
                accumEdgeSpan(v2, v3);
                accumEdgeSpan(v3, v0);
                if (minEdgeX > maxEdgeX) {
                    continue;
                }
                rowPxStart = std::max(pxStart, SafeFloorToInt(minEdgeX - 1.5f, pxStart - 2, pxEnd + 2));
                rowPxEnd = std::min(pxEnd, SafeCeilToInt(maxEdgeX + 1.5f, pxStart - 2, pxEnd + 2));
                if (rowPxStart >= rowPxEnd) {
                    continue;
                }
                perspRowU = pInv01 * sy + pInv02;
                perspRowV = pInv11 * sy + pInv12;
                perspRowW = pInv21 * sy + pInv22;
            }

            // Fast contiguous span fill for axis-aligned SolidQuad
            if (!usePerspectiveInverse && !optPremulLayer &&
                prim == AeroPrimType::SolidQuad &&
                nd->blendMode == BlendingMode::Normal &&
                ((v0.flags & AeroVertexFlag_PremultipliedAlpha) == 0u) &&
                std::fabs(e1y) <= 1e-6f && std::fabs(e3x) <= 1e-6f) {
                const float svConst = (e1x * dy) * invDet;
                if (svConst >= 0.0f && svConst < 1.0f && baseAlpha > 0.002f) {
                    int xSpan0 = rowPxStart;
                    while (xSpan0 < rowPxEnd) {
                        const float su0 = (((static_cast<float>(xSpan0) + 0.5f) - v0.x) * e3y) * invDet;
                        if (su0 >= 0.0f && su0 < 1.0f) break;
                        ++xSpan0;
                    }
                    int xSpan1 = rowPxEnd;
                    while (xSpan1 > xSpan0) {
                        const float su1 = (((static_cast<float>(xSpan1 - 1) + 0.5f) - v0.x) * e3y) * invDet;
                        if (su1 >= 0.0f && su1 < 1.0f) break;
                        --xSpan1;
                    }
                    if (xSpan1 > xSpan0) {
                        if (a8 == 255u) {
                            const std::uint32_t packedPixel =
                                static_cast<std::uint32_t>(r8) |
                                (static_cast<std::uint32_t>(g8) << 8u) |
                                (static_cast<std::uint32_t>(b8) << 16u) |
                                0xFF000000u;
                            auto* dst32 = reinterpret_cast<std::uint32_t*>(
                                outRgba8 + (static_cast<std::size_t>(py) * viewportWidth + static_cast<std::size_t>(xSpan0)) * 4u);
                            std::fill_n(dst32, static_cast<std::size_t>(xSpan1 - xSpan0), packedPixel);
                        } else {
                            std::uint8_t* dstRow = outRgba8 +
                                (static_cast<std::size_t>(py) * viewportWidth + static_cast<std::size_t>(xSpan0)) * 4u;
                            const int spanCount = xSpan1 - xSpan0;
                            for (int i = 0; i < spanCount; ++i) {
                                BlendPixelOver(dstRow + static_cast<std::size_t>(i) * 4u, r8, g8, b8, baseAlpha, BlendingMode::Normal);
                            }
                        }
                    }
                    continue;
                }
            }

            // Fast safe-interior horizontal span split for axis-aligned SdfRoundedRect (cards, borders, buttons, panels)
            int sdfInteriorX0 = rowPxEnd;
            int sdfInteriorX1 = rowPxEnd;
            if (!usePerspectiveInverse &&
                prim == AeroPrimType::SdfRoundedRect &&
                nd->softFeatherPx <= 0.0f &&
                !sdfHollowBorder &&
                sdfSafeInteriorHalfW > 0.0f &&
                sdfSafeInteriorHalfH > 0.0f &&
                e1y == 0.0f && e3x == 0.0f &&
                (rowPxEnd - rowPxStart) >= 8) {
                const float svConst = (e1x * dy) * invDet;
                const float localYConst = (svConst - 0.5f) * (2.0f * quadHalfH);
                if (svConst >= 0.0f && svConst <= 1.0f && std::fabs(localYConst) <= sdfSafeInteriorHalfH &&
                    std::fabs(stepSuX) > 1e-6f && quadHalfW > 1e-4f) {
                    const float suHalfSpan = sdfSafeInteriorHalfW / (2.0f * quadHalfW);
                    const float suMin = std::max(0.0f, 0.5f - suHalfSpan);
                    const float suMax = std::min(1.0f, 0.5f + suHalfSpan);
                    const float invStep = 1.0f / stepSuX;
                    const float pxA = v0.x + suMin * invStep - 0.5f;
                    const float pxB = v0.x + suMax * invStep - 0.5f;
                    int cand0 = std::clamp(SafeCeilToInt(std::min(pxA, pxB), rowPxStart, rowPxEnd), rowPxStart, rowPxEnd);
                    int cand1 = std::clamp(SafeFloorToInt(std::max(pxA, pxB), rowPxStart, rowPxEnd) + 1, rowPxStart, rowPxEnd);
                    auto isInteriorPx = [&](int testPx) noexcept -> bool {
                        const float testSu = (((static_cast<float>(testPx) + 0.5f) - v0.x) * e3y) * invDet;
                        if (testSu < 0.0f || testSu > 1.0f) return false;
                        const float testLocalX = (testSu - 0.5f) * (2.0f * quadHalfW);
                        return std::fabs(testLocalX) <= sdfSafeInteriorHalfW;
                    };
                    while (cand0 < cand1 && !isInteriorPx(cand0)) ++cand0;
                    while (cand1 > cand0 && !isInteriorPx(cand1 - 1)) --cand1;
                    if (cand1 - cand0 >= 4) {
                        sdfInteriorX0 = cand0;
                        sdfInteriorX1 = cand1;
                    }
                }
            }

            for (int px = rowPxStart; px < rowPxEnd; ++px) {
                if (px == sdfInteriorX0 && sdfInteriorX1 > sdfInteriorX0) {
                    const int spanLen = sdfInteriorX1 - sdfInteriorX0;
                    if (baseAlpha > 0.002f) {
                        if (optPremulLayer) {
                            const float inv255 = 1.0f / 255.0f;
                            const float preR = static_cast<float>(r8) * inv255 * baseAlpha;
                            const float preG = static_cast<float>(g8) * inv255 * baseAlpha;
                            const float preB = static_cast<float>(b8) * inv255 * baseAlpha;
                            for (int spx = sdfInteriorX0; spx < sdfInteriorX1; ++spx) {
                                BlendPremulOver(*optPremulLayer, spx, py, preR, preG, preB, baseAlpha);
                            }
                        } else if (a8 == 255u && nd->blendMode == BlendingMode::Normal) {
                            const std::uint32_t packedPixel =
                                static_cast<std::uint32_t>(r8) |
                                (static_cast<std::uint32_t>(g8) << 8u) |
                                (static_cast<std::uint32_t>(b8) << 16u) |
                                0xFF000000u;
                            auto* dst32 = reinterpret_cast<std::uint32_t*>(
                                outRgba8 + (static_cast<std::size_t>(py) * viewportWidth + static_cast<std::size_t>(sdfInteriorX0)) * 4u);
                            std::fill_n(dst32, static_cast<std::size_t>(spanLen), packedPixel);
                        } else if (nd->blendMode == BlendingMode::Normal) {
                            std::uint8_t* dstSpan = outRgba8 +
                                (static_cast<std::size_t>(py) * viewportWidth + static_cast<std::size_t>(sdfInteriorX0)) * 4u;
                            BlendSpanOverNormalFast(dstSpan, spanLen, r8, g8, b8, baseAlpha);
                        } else {
                            std::uint8_t* dstSpan = outRgba8 +
                                (static_cast<std::size_t>(py) * viewportWidth + static_cast<std::size_t>(sdfInteriorX0)) * 4u;
                            for (int i = 0; i < spanLen; ++i) {
                                BlendPixelOver(dstSpan + static_cast<std::size_t>(i) * 4u, r8, g8, b8, baseAlpha, nd->blendMode);
                            }
                        }
                    }
                    px = sdfInteriorX1 - 1;
                    continue;
                }
                const float sx = static_cast<float>(px) + 0.5f;
                float su = 0.0f;
                float sv = 0.0f;
                if (usePerspectiveInverse) {
                    const float wH = pInv20 * sx + perspRowW;
                    if (std::fabs(wH) <= 1e-8f) {
                        continue;
                    }
                    const float invWH = 1.0f / wH;
                    const float localX = (pInv00 * sx + perspRowU) * invWH;
                    const float localY = (pInv10 * sx + perspRowV) * invWH;
                    if ((pH20 * localX + pH21 * localY + pH22) <= 1e-4f) {
                        continue;
                    }
                    su = (localX - perspOrigX) * invPerspSpanW;
                    sv = (localY - perspOrigY) * invPerspSpanH;
                } else {
                    const float dx = sx - v0.x;
                    su = (dx * e3y - dy * e3x) * invDet;
                    sv = (e1x * dy - e1y * dx) * invDet;
                }
                if (su < 0.0f || su > 1.0f || sv < 0.0f || sv > 1.0f) {
                    continue;
                }
                if (prim == AeroPrimType::SolidQuad && (su >= 1.0f || sv >= 1.0f)) {
                    continue;
                }

                std::uint8_t shadeR = r8;
                std::uint8_t shadeG = g8;
                std::uint8_t shadeB = b8;
                float pixelBaseAlpha = baseAlpha;
                float coverage = 1.0f;
                if (prim == AeroPrimType::SdfRoundedRect) {
                    const float halfW = quadHalfW;
                    const float halfH = quadHalfH;
                    const float localX = (su - 0.5f) * (2.0f * halfW);
                    const float localY = (sv - 0.5f) * (2.0f * halfH);
                    const float aaFeather = quadAaFeather;
                    const float pad = std::max(0.0f, nd->sdfPadPx);
                    const float boxHalfW = std::max(0.5f, halfW - pad);
                    const float boxHalfH = std::max(0.5f, halfH - pad);

                    if (nd->softFeatherPx > 0.0f) {
                        const float softFeather = std::max(aaFeather, nd->softFeatherPx * 1.6f);
                        if (uniformSdfCr) {
                            coverage = EvaluateSdfRoundedRectCoverage(
                                localX,
                                localY,
                                boxHalfW,
                                boxHalfH,
                                sdfRadii.topLeft,
                                softFeather);
                        } else {
                            coverage = EvaluateSdfRoundedRectCoverage(
                                localX,
                                localY,
                                boxHalfW,
                                boxHalfH,
                                sdfRadii,
                                softFeather);
                        }
                    } else if (sdfSafeInteriorHalfW > 0.0f && sdfSafeInteriorHalfH > 0.0f &&
                               std::fabs(localX) <= sdfSafeInteriorHalfW &&
                               std::fabs(localY) <= sdfSafeInteriorHalfH) {
                        coverage = 1.0f;
                    } else if (sdfHollowBorder) {
                        coverage = EvaluateSdfRoundedBorderCoverage(
                            localX,
                            localY,
                            boxHalfW,
                            boxHalfH,
                            sdfRadii,
                            sdfBorder,
                            aaFeather);
                    } else if (uniformSdfCr) {
                        const float rad = sdfRadii.topLeft;
                        const float border = sdfBorder.left;
                        const float outerCov = EvaluateSdfRoundedRectCoverage(
                            localX,
                            localY,
                            boxHalfW,
                            boxHalfH,
                            rad,
                            aaFeather);
                        if (border > 0.0f) {
                            const float innerHalfW = std::max(0.0f, boxHalfW - border);
                            const float innerHalfH = std::max(0.0f, boxHalfH - border);
                            const float innerRad = std::max(0.0f, rad - border);
                            const float innerCov = (innerHalfW > 0.0f && innerHalfH > 0.0f)
                                ? EvaluateSdfRoundedRectCoverage(localX, localY, innerHalfW, innerHalfH, innerRad, aaFeather)
                                : 0.0f;
                            const float ringCov = std::clamp(outerCov - innerCov, 0.0f, 1.0f);
                            if (ringCov > 0.001f) {
                                auto accentCh = [&](std::uint8_t ch) noexcept -> std::uint8_t {
                                    const float cf = static_cast<float>(ch) * (1.0f / 255.0f);
                                    const float acc = std::min(cf * 1.35f + 0.08f, 1.0f);
                                    const float mixed = cf + (acc - cf) * ringCov;
                                    return static_cast<std::uint8_t>(std::clamp(mixed * 255.0f + 0.5f, 0.0f, 255.0f));
                                };
                                shadeR = accentCh(r8);
                                shadeG = accentCh(g8);
                                shadeB = accentCh(b8);
                            }
                            coverage = outerCov;
                        } else {
                            coverage = outerCov;
                        }
                    } else {
                        coverage = EvaluateSdfRoundedRectCoverage(
                            localX,
                            localY,
                            boxHalfW,
                            boxHalfH,
                            sdfRadii,
                            aaFeather);
                    }
                } else if (prim == AeroPrimType::MsdfVectorPath && mtsdfEntry &&
                           mtsdfEntry->cellWidth > 0 && mtsdfEntry->cellHeight > 0 &&
                           mtsdfEntry->mtsdfRgbaPixels.size() >=
                               static_cast<std::size_t>(mtsdfEntry->cellWidth) * mtsdfEntry->cellHeight * 4u) {
                    const float cellW = static_cast<float>(mtsdfEntry->cellWidth);
                    const float cellH = static_cast<float>(mtsdfEntry->cellHeight);
                    const float uInterp = v0.u + su * (v1.u - v0.u) + sv * (v3.u - v0.u);
                    const float vInterp = v0.v + su * (v1.v - v0.v) + sv * (v3.v - v0.v);
                    const float cellTexX = 0.5f + ((uInterp - mtsdfEntry->u0) / hoistedEntrySpanU) * std::max(1.0f, cellW - 1.0f);
                    const float cellTexY = 0.5f + ((vInterp - mtsdfEntry->v0) / hoistedEntrySpanV) * std::max(1.0f, cellH - 1.0f);
                    const float normX = std::clamp(cellTexX / cellW, 0.0f, 1.0f);
                    const float normY = std::clamp(cellTexY / cellH, 0.0f, 1.0f);
                    const float fullCellW = hoistedFullCellW;
                    const float fullCellH = hoistedFullCellH;
                    const float strokePx = hoistedMtsdfStrokePx;
                    const bool strokeOnly = hoistedMtsdfStrokeOnly;

                    if (nd->softFeatherPx > 0.0f) {
                        const float u = normX * cellW - 0.5f;
                        const float v = normY * cellH - 0.5f;
                        const int x0 = std::clamp(static_cast<int>(std::floor(u)), 0, static_cast<int>(mtsdfEntry->cellWidth) - 1);
                        const int y0 = std::clamp(static_cast<int>(std::floor(v)), 0, static_cast<int>(mtsdfEntry->cellHeight) - 1);
                        const int x1 = std::min(x0 + 1, static_cast<int>(mtsdfEntry->cellWidth) - 1);
                        const int y1 = std::min(y0 + 1, static_cast<int>(mtsdfEntry->cellHeight) - 1);
                        const float fx = std::clamp(u - std::floor(u), 0.0f, 1.0f);
                        const float fy = std::clamp(v - std::floor(v), 0.0f, 1.0f);
                        auto sampleA = [&](int cx, int cy) noexcept -> float {
                            const std::size_t idx =
                                (static_cast<std::size_t>(cy) * mtsdfEntry->cellWidth + static_cast<std::size_t>(cx)) * 4u + 3u;
                            return static_cast<float>(mtsdfEntry->mtsdfRgbaPixels[idx]) / 255.0f;
                        };
                        const float topA = sampleA(x0, y0) + (sampleA(x1, y0) - sampleA(x0, y0)) * fx;
                        const float botA = sampleA(x0, y1) + (sampleA(x1, y1) - sampleA(x0, y1)) * fx;
                        const float sdTrue = topA + (botA - topA) * fy;
                        const float scaleX = std::max(1.0f, fullCellW) / cellW;
                        const float scaleY = std::max(1.0f, fullCellH) / cellH;
                        const float screenPxRange = std::max(0.5f * mtsdfEntry->pxRange * (scaleX + scaleY), 1.0f);
                        const float trueDistPx = (sdTrue - 0.5f) * screenPxRange;
                        const float feather = std::max(1.0f, nd->softFeatherPx * 1.25f);
                        const float t = std::clamp(0.5f + (trueDistPx / feather), 0.0f, 1.0f);
                        coverage = t * t * (3.0f - 2.0f * t);
                    } else {
                        float fillCov = 0.0f;
                        if (!strokeOnly) {
                            const float mu = normX * cellW - 0.5f;
                            const float mv = normY * cellH - 0.5f;
                            const int maxMX = static_cast<int>(mtsdfEntry->cellWidth) - 1;
                            const int maxMY = static_cast<int>(mtsdfEntry->cellHeight) - 1;
                            const float fmu = std::floor(mu);
                            const float fmv = std::floor(mv);
                            const int mx0 = std::clamp(static_cast<int>(fmu), 0, maxMX);
                            const int my0 = std::clamp(static_cast<int>(fmv), 0, maxMY);
                            const int mx1 = std::min(mx0 + 1, maxMX);
                            const int my1 = std::min(my0 + 1, maxMY);
                            const std::uint8_t* mBase = mtsdfEntry->mtsdfRgbaPixels.data();
                            const std::size_t mStride = static_cast<std::size_t>(mtsdfEntry->cellWidth) * 4u;
                            const std::uint8_t* mp00 = mBase + static_cast<std::size_t>(my0) * mStride + static_cast<std::size_t>(mx0) * 4u;
                            const std::uint8_t* mp10 = mBase + static_cast<std::size_t>(my0) * mStride + static_cast<std::size_t>(mx1) * 4u;
                            const std::uint8_t* mp01 = mBase + static_cast<std::size_t>(my1) * mStride + static_cast<std::size_t>(mx0) * 4u;
                            const std::uint8_t* mp11 = mBase + static_cast<std::size_t>(my1) * mStride + static_cast<std::size_t>(mx1) * 4u;
                            const std::uint8_t orRgb =
                                mp00[0] | mp00[1] | mp00[2] |
                                mp10[0] | mp10[1] | mp10[2] |
                                mp01[0] | mp01[1] | mp01[2] |
                                mp11[0] | mp11[1] | mp11[2];
                            if (orRgb != 0u) {
                                const std::uint8_t andRgb =
                                    mp00[0] & mp00[1] & mp00[2] &
                                    mp10[0] & mp10[1] & mp10[2] &
                                    mp01[0] & mp01[1] & mp01[2] &
                                    mp11[0] & mp11[1] & mp11[2];
                                if (andRgb == 255u) {
                                    fillCov = 1.0f;
                                } else {
                                    const float mfx = std::clamp(mu - fmu, 0.0f, 1.0f);
                                    const float mfy = std::clamp(mv - fmv, 0.0f, 1.0f);
                                    auto bilerpM = [&](std::size_t ch) noexcept -> float {
                                        const float c00 = static_cast<float>(mp00[ch]) / 255.0f;
                                        const float c10 = static_cast<float>(mp10[ch]) / 255.0f;
                                        const float c01 = static_cast<float>(mp01[ch]) / 255.0f;
                                        const float c11 = static_cast<float>(mp11[ch]) / 255.0f;
                                        const float top = c00 + (c10 - c00) * mfx;
                                        const float bot = c01 + (c11 - c01) * mfx;
                                        return top + (bot - top) * mfy;
                                    };
                                    const float mr = bilerpM(0u);
                                    const float mg = bilerpM(1u);
                                    const float mb = bilerpM(2u);
                                    const float sd = std::max(std::min(mr, mg), std::min(std::max(mr, mg), mb));
                                    const float scaleX = std::max(1.0f, fullCellW) / cellW;
                                    const float scaleY = std::max(1.0f, fullCellH) / cellH;
                                    const float screenPxRange = std::max(0.5f * mtsdfEntry->pxRange * (scaleX + scaleY), 1.0f);
                                    fillCov = std::clamp(screenPxRange * (sd - 0.5f) + 0.5f, 0.0f, 1.0f);
                                }
                            }
                        }

                        if (strokePx > 0.0f) {
                            const float u = normX * cellW - 0.5f;
                            const float v = normY * cellH - 0.5f;
                            const int x0 = std::clamp(static_cast<int>(std::floor(u)), 0, static_cast<int>(mtsdfEntry->cellWidth) - 1);
                            const int y0 = std::clamp(static_cast<int>(std::floor(v)), 0, static_cast<int>(mtsdfEntry->cellHeight) - 1);
                            const int x1 = std::min(x0 + 1, static_cast<int>(mtsdfEntry->cellWidth) - 1);
                            const int y1 = std::min(y0 + 1, static_cast<int>(mtsdfEntry->cellHeight) - 1);
                            const float fx = std::clamp(u - std::floor(u), 0.0f, 1.0f);
                            const float fy = std::clamp(v - std::floor(v), 0.0f, 1.0f);
                            auto sampleA = [&](int cx, int cy) noexcept -> float {
                                const std::size_t idx =
                                    (static_cast<std::size_t>(cy) * mtsdfEntry->cellWidth + static_cast<std::size_t>(cx)) * 4u + 3u;
                                return static_cast<float>(mtsdfEntry->mtsdfRgbaPixels[idx]) / 255.0f;
                            };
                            const float topA = sampleA(x0, y0) + (sampleA(x1, y0) - sampleA(x0, y0)) * fx;
                            const float botA = sampleA(x0, y1) + (sampleA(x1, y1) - sampleA(x0, y1)) * fx;
                            const float sdTrue = topA + (botA - topA) * fy;
                            const float scaleX = std::max(1.0f, fullCellW) / cellW;
                            const float scaleY = std::max(1.0f, fullCellH) / cellH;
                            const float screenPxRange = std::max(0.5f * mtsdfEntry->pxRange * (scaleX + scaleY), 1.0f);
                            const float trueDistPx = (sdTrue - 0.5f) * screenPxRange;
                            const float strokeCov = std::clamp(0.5f + strokePx * 0.5f - std::fabs(trueDistPx), 0.0f, 1.0f);
                            fillCov = std::max(fillCov, strokeCov);
                        }
                        coverage = fillCov;
                    }
                    if (hoistedPathGradBrush != nullptr && coverage > 0.0f) {
                        if (const GradientEvalPod* gBrush = hoistedPathGradBrush) {
                            const Color gc = EvaluateGradientBrushSafe(
                                *gBrush,
                                std::clamp(su, 0.0f, 1.0f),
                                std::clamp(sv, 0.0f, 1.0f),
                                std::max(1.0f, perpQuadW),
                                std::max(1.0f, perpQuadH));
                            shadeR = static_cast<std::uint8_t>(std::clamp((static_cast<float>(shadeR) / 255.0f) * gc.r * 255.0f + 0.5f, 0.0f, 255.0f));
                            shadeG = static_cast<std::uint8_t>(std::clamp((static_cast<float>(shadeG) / 255.0f) * gc.g * 255.0f + 0.5f, 0.0f, 255.0f));
                            shadeB = static_cast<std::uint8_t>(std::clamp((static_cast<float>(shadeB) / 255.0f) * gc.b * 255.0f + 0.5f, 0.0f, 255.0f));
                            coverage *= std::clamp(gc.a, 0.0f, 1.0f);
                        }
                    }
                } else if (prim == AeroPrimType::SdfRadialArc) {
                    const float startDeg = hoistedArcStartDeg;
                    const float sweepDeg = hoistedArcSweepDeg;
                    const float innerRatio = hoistedArcInnerRatio;
                    const float halfW = quadHalfW;
                    const float halfH = quadHalfH;
                    const float localX = (su - 0.5f) * (2.0f * halfW);
                    const float localY = (sv - 0.5f) * (2.0f * halfH);
                    const float aaFeather = quadAaFeather;
                    coverage = EvaluateSdfRadialArcCoverage(
                        localX,
                        localY,
                        halfW,
                        halfH,
                        startDeg,
                        sweepDeg,
                        innerRatio,
                        aaFeather);
                } else if (prim == AeroPrimType::SdfTextGlyph) {
                    if (!hasNodeSdfView) {
                        coverage = 0.0f;
                    } else {
                        const float uInterp = v0.u + su * hoistedSdfUSpan + sv * hoistedSdfDuV;
                        const float vInterp = v0.v + su * hoistedSdfDvU + sv * hoistedSdfVSpan;
                        const float tx = std::clamp(uInterp, 0.0f, 1.0f) * static_cast<float>(cachedSdfW) - 0.5f;
                        const float ty = std::clamp(vInterp, 0.0f, 1.0f) * static_cast<float>(cachedSdfH) - 0.5f;
                        const float flX = std::floor(tx);
                        const float flY = std::floor(ty);
                        const int maxSdfX = static_cast<int>(cachedSdfW) - 1;
                        const int maxSdfY = static_cast<int>(cachedSdfH) - 1;
                        const int x0 = std::clamp(static_cast<int>(flX), 0, maxSdfX);
                        const int y0 = std::clamp(static_cast<int>(flY), 0, maxSdfY);
                        const int glyphHInt = static_cast<int>(cachedSdfGlyphH);
                        float sd = 0.0f;
                        if (y0 < glyphHInt) {
                            const int x1 = std::min(x0 + 1, maxSdfX);
                            const int y1 = std::min(y0 + 1, maxSdfY);
                            const float fx = std::clamp(tx - flX, 0.0f, 1.0f);
                            const float fy = std::clamp(ty - flY, 0.0f, 1.0f);
                            constexpr float kInv255 = 1.0f / 255.0f;
                            const std::size_t row0Off = static_cast<std::size_t>(y0) * cachedSdfW;
                            const float c00 = static_cast<float>(cachedSdfData[row0Off + static_cast<std::size_t>(x0)]) * kInv255;
                            const float c10 = static_cast<float>(cachedSdfData[row0Off + static_cast<std::size_t>(x1)]) * kInv255;
                            float c01 = 0.0f;
                            float c11 = 0.0f;
                            if (y1 < glyphHInt) {
                                const std::size_t row1Off = static_cast<std::size_t>(y1) * cachedSdfW;
                                c01 = static_cast<float>(cachedSdfData[row1Off + static_cast<std::size_t>(x0)]) * kInv255;
                                c11 = static_cast<float>(cachedSdfData[row1Off + static_cast<std::size_t>(x1)]) * kInv255;
                            }
                            const float top = c00 + (c10 - c00) * fx;
                            const float bot = c01 + (c11 - c01) * fx;
                            sd = top + (bot - top) * fy;
                        }
                        constexpr float kEdgeMidpoint = 128.0f / 255.0f;
                        const float screenDist = (sd - kEdgeMidpoint) * hoistedSdfScreenPxRange;
                        const float cov = std::clamp(screenDist + 0.5f, 0.0f, 1.0f);
                        coverage = cov * cov * (3.0f - 2.0f * cov);
                    }
                    if (!uniformVertexColor && coverage > 0.002f) {
                        auto bilerpVertByte = [&](std::uint32_t shift) noexcept -> float {
                            const float c0 = static_cast<float>((v0.rgba >> shift) & 0xFFu);
                            const float c1 = static_cast<float>((v1.rgba >> shift) & 0xFFu);
                            const float c2 = static_cast<float>((v2.rgba >> shift) & 0xFFu);
                            const float c3 = static_cast<float>((v3.rgba >> shift) & 0xFFu);
                            const float top = c0 + su * (c1 - c0);
                            const float bot = c3 + su * (c2 - c3);
                            return top + sv * (bot - top);
                        };
                        shadeR = static_cast<std::uint8_t>(std::clamp(bilerpVertByte(0u) + 0.5f, 0.0f, 255.0f));
                        shadeG = static_cast<std::uint8_t>(std::clamp(bilerpVertByte(8u) + 0.5f, 0.0f, 255.0f));
                        shadeB = static_cast<std::uint8_t>(std::clamp(bilerpVertByte(16u) + 0.5f, 0.0f, 255.0f));
                        pixelBaseAlpha = std::clamp(bilerpVertByte(24u) * (1.0f / 255.0f), 0.0f, 1.0f);
                    }
                } else if (prim == AeroPrimType::TexturedQuad) {
                    const float topU = v0.u + su * (v1.u - v0.u);
                    const float botU = v3.u + su * (v2.u - v3.u);
                    const float uInterp = topU + sv * (botU - topU);

                    Color texel{1.0f, 1.0f, 1.0f, 1.0f};
                    if (canHoistTexturedRowY) {
                        const float clampedU = std::clamp(uInterp, 0.0f, 1.0f);
                        const float texX = clampedU * static_cast<float>(cachedNodeTexW) - 0.5f;
                        const float flX = std::floor(texX);
                        const int x0 = std::clamp(static_cast<int>(flX), 0, hoistedTexMaxX);
                        const int x1 = std::min(x0 + 1, hoistedTexMaxX);
                        const float fx = std::clamp(texX - flX, 0.0f, 1.0f);
                        constexpr float kInv255 = 1.0f / 255.0f;
                        if (resolvedNodeTexAlpha8) {
                            const float c00 = static_cast<float>(rowTexPtr0[x0]);
                            const float c10 = static_cast<float>(rowTexPtr0[x1]);
                            const float c01 = static_cast<float>(rowTexPtr1[x0]);
                            const float c11 = static_cast<float>(rowTexPtr1[x1]);
                            const float top = c00 + (c10 - c00) * fx;
                            const float bot = c01 + (c11 - c01) * fx;
                            texel = Color{1.0f, 1.0f, 1.0f, (top + (bot - top) * rowTexFy) * kInv255};
                        } else {
                            const std::uint8_t* p00 = rowTexPtr0 + static_cast<std::size_t>(x0) * 4u;
                            const std::uint8_t* p10 = rowTexPtr0 + static_cast<std::size_t>(x1) * 4u;
                            const std::uint8_t* p01 = rowTexPtr1 + static_cast<std::size_t>(x0) * 4u;
                            const std::uint8_t* p11 = rowTexPtr1 + static_cast<std::size_t>(x1) * 4u;
#ifdef AERO_SW_SIMD_SSE2
                            auto loadRgbaFloat4 = [](const std::uint8_t* p) noexcept -> __m128 {
                                return _mm_set_ps(
                                    static_cast<float>(p[3]),
                                    static_cast<float>(p[2]),
                                    static_cast<float>(p[1]),
                                    static_cast<float>(p[0]));
                            };
                            const __m128 v00 = loadRgbaFloat4(p00);
                            const __m128 v10 = loadRgbaFloat4(p10);
                            const __m128 v01 = loadRgbaFloat4(p01);
                            const __m128 v11 = loadRgbaFloat4(p11);
                            const __m128 vFx = _mm_set1_ps(fx);
                            const __m128 vFy = _mm_set1_ps(rowTexFy);
                            const __m128 vTop = _mm_add_ps(v00, _mm_mul_ps(_mm_sub_ps(v10, v00), vFx));
                            const __m128 vBot = _mm_add_ps(v01, _mm_mul_ps(_mm_sub_ps(v11, v01), vFx));
                            const __m128 vOut = _mm_mul_ps(
                                _mm_add_ps(vTop, _mm_mul_ps(_mm_sub_ps(vBot, vTop), vFy)),
                                _mm_set1_ps(kInv255));
                            alignas(16) float ch[4];
                            _mm_store_ps(ch, vOut);
                            texel = Color{ch[0], ch[1], ch[2], ch[3]};
#else
                            auto bilerpRowCh = [&](std::size_t ch) noexcept -> float {
                                const float c00 = static_cast<float>(p00[ch]);
                                const float c10 = static_cast<float>(p10[ch]);
                                const float c01 = static_cast<float>(p01[ch]);
                                const float c11 = static_cast<float>(p11[ch]);
                                const float top = c00 + (c10 - c00) * fx;
                                const float bot = c01 + (c11 - c01) * fx;
                                return (top + (bot - top) * rowTexFy) * kInv255;
                            };
                            texel = Color{bilerpRowCh(0u), bilerpRowCh(1u), bilerpRowCh(2u), bilerpRowCh(3u)};
#endif
                        }
                    } else if (hasBoundTexture) {
                        const float topV = v0.v + su * (v1.v - v0.v);
                        const float botV = v3.v + su * (v2.v - v3.v);
                        const float vInterp = topV + sv * (botV - topV);
                        texel = SampleTextureSlotBilinear(
                            cachedNodeTexW, cachedNodeTexH, *cachedNodeTexPixels, uInterp, vInterp,
                            resolvedNodeTexAlpha8);
                    }

                    float vertR = static_cast<float>(r8) * (1.0f / 255.0f);
                    float vertG = static_cast<float>(g8) * (1.0f / 255.0f);
                    float vertB = static_cast<float>(b8) * (1.0f / 255.0f);
                    float vertA = baseAlpha;
                    if (!uniformVertexColor) {
                        auto bilerpVertByte = [&](std::uint32_t shift) noexcept -> float {
                            const float c0 = static_cast<float>((v0.rgba >> shift) & 0xFFu);
                            const float c1 = static_cast<float>((v1.rgba >> shift) & 0xFFu);
                            const float c2 = static_cast<float>((v2.rgba >> shift) & 0xFFu);
                            const float c3 = static_cast<float>((v3.rgba >> shift) & 0xFFu);
                            const float top = c0 + su * (c1 - c0);
                            const float bot = c3 + su * (c2 - c3);
                            return (top + sv * (bot - top)) * (1.0f / 255.0f);
                        };
                        vertR = bilerpVertByte(0u);
                        vertG = bilerpVertByte(8u);
                        vertB = bilerpVertByte(16u);
                        vertA = bilerpVertByte(24u);
                    }

                    if (hoistedPathGradBrush != nullptr && texel.a > 0.002f) {
                        const Color gc = EvaluateGradientBrushSafe(
                            *hoistedPathGradBrush,
                            std::clamp(su, 0.0f, 1.0f),
                            std::clamp(sv, 0.0f, 1.0f),
                            std::max(1.0f, perpQuadW),
                            std::max(1.0f, perpQuadH));
                        vertR *= gc.r;
                        vertG *= gc.g;
                        vertB *= gc.b;
                        vertA *= std::clamp(gc.a, 0.0f, 1.0f);
                    }
                    const float finalAlpha = std::clamp(texel.a * vertA, 0.0f, 1.0f);
                    if (finalAlpha > 0.002f) {
                        const bool isPremul = (v0.flags & AeroVertexFlag_PremultipliedAlpha) != 0u;
                        if (optPremulLayer) {
                            const float pR = isPremul ? (texel.r * vertR) : (texel.r * vertR * finalAlpha);
                            const float pG = isPremul ? (texel.g * vertG) : (texel.g * vertG * finalAlpha);
                            const float pB = isPremul ? (texel.b * vertB) : (texel.b * vertB * finalAlpha);
                            BlendPremulOver(*optPremulLayer, px, py, pR, pG, pB, finalAlpha);
                        } else {
                            const auto outR = static_cast<std::uint8_t>(
                                std::clamp(texel.r * vertR * 255.0f + 0.5f, 0.0f, 255.0f));
                            const auto outG = static_cast<std::uint8_t>(
                                std::clamp(texel.g * vertG * 255.0f + 0.5f, 0.0f, 255.0f));
                            const auto outB = static_cast<std::uint8_t>(
                                std::clamp(texel.b * vertB * 255.0f + 0.5f, 0.0f, 255.0f));
                            std::uint8_t* dst = outRgba8 +
                                (static_cast<std::size_t>(py) * viewportWidth + static_cast<std::size_t>(px)) * 4u;
                            if (!isPremul && finalAlpha >= 1.0f && nd->blendMode == BlendingMode::Normal) {
                                dst[0] = outR;
                                dst[1] = outG;
                                dst[2] = outB;
                                dst[3] = 255u;
                            } else if (isPremul) {
                                const float invA = 1.0f - finalAlpha;
                                dst[0] = static_cast<std::uint8_t>(std::clamp(static_cast<float>(outR) + static_cast<float>(dst[0]) * invA + 0.5f, 0.0f, 255.0f));
                                dst[1] = static_cast<std::uint8_t>(std::clamp(static_cast<float>(outG) + static_cast<float>(dst[1]) * invA + 0.5f, 0.0f, 255.0f));
                                dst[2] = static_cast<std::uint8_t>(std::clamp(static_cast<float>(outB) + static_cast<float>(dst[2]) * invA + 0.5f, 0.0f, 255.0f));
                                dst[3] = static_cast<std::uint8_t>(std::clamp(finalAlpha * 255.0f + static_cast<float>(dst[3]) * invA + 0.5f, 0.0f, 255.0f));
                            } else {
                                BlendPixelOver(dst, outR, outG, outB, finalAlpha, nd->blendMode);
                            }
                        }
                    }
                    continue;
                } else if (prim == AeroPrimType::SdfEllipse) {
                    const float strokePx = hoistedEllipseStrokePx;
                    const bool hollowStroke = hoistedEllipseHollow;
                    const float halfW = quadHalfW;
                    const float halfH = quadHalfH;
                    const float localX = (su - 0.5f) * (2.0f * halfW);
                    const float localY = (sv - 0.5f) * (2.0f * halfH);
                    const float aaFeather = quadAaFeather;
                    coverage = EvaluateSdfEllipseCoverage(
                        localX,
                        localY,
                        halfW,
                        halfH,
                        strokePx,
                        hollowStroke,
                        aaFeather);
                    if (hoistedPathGradBrush != nullptr && coverage > 0.0f) {
                        const Color gc = EvaluateGradientBrushSafe(
                            *hoistedPathGradBrush,
                            std::clamp(su, 0.0f, 1.0f),
                            std::clamp(sv, 0.0f, 1.0f),
                            std::max(1.0f, 2.0f * halfW),
                            std::max(1.0f, 2.0f * halfH));
                        shadeR = static_cast<std::uint8_t>(std::clamp((static_cast<float>(shadeR) / 255.0f) * gc.r * 255.0f + 0.5f, 0.0f, 255.0f));
                        shadeG = static_cast<std::uint8_t>(std::clamp((static_cast<float>(shadeG) / 255.0f) * gc.g * 255.0f + 0.5f, 0.0f, 255.0f));
                        shadeB = static_cast<std::uint8_t>(std::clamp((static_cast<float>(shadeB) / 255.0f) * gc.b * 255.0f + 0.5f, 0.0f, 255.0f));
                        coverage *= std::clamp(gc.a, 0.0f, 1.0f);
                    }
                } else if (prim == AeroPrimType::GradientRoundedRect) {
                    const float gradRadius = hoistedGradRadius;
                    const bool isEllipse = hoistedGradIsEllipse;
                    const float halfW = quadHalfW;
                    const float halfH = quadHalfH;
                    const float localX = (su - 0.5f) * (2.0f * halfW);
                    const float localY = (sv - 0.5f) * (2.0f * halfH);
                    const float aaFeather = quadAaFeather;

                    float shapeCov = 1.0f;
                    const float maxGradCr = hoistedMaxGradCr;
                    if (isEllipse || nd->isEllipseShape) {
                        shapeCov = EvaluateSdfEllipseCoverage(
                            localX,
                            localY,
                            halfW,
                            halfH,
                            0.0f,
                            false,
                            aaFeather);
                    } else if (std::fabs(localX) <= (halfW - maxGradCr - aaFeather - 0.5f) &&
                               std::fabs(localY) <= (halfH - maxGradCr - aaFeather - 0.5f)) {
                        shapeCov = 1.0f;
                    } else if (!hoistedUniformGradCr) {
                        shapeCov = EvaluateSdfRoundedRectCoverage(
                            localX,
                            localY,
                            halfW,
                            halfH,
                            nd->cornerRadii,
                            aaFeather);
                    } else {
                        shapeCov = EvaluateSdfRoundedRectCoverage(
                            localX,
                            localY,
                            halfW,
                            halfH,
                            gradRadius,
                            aaFeather);
                    }

                    if (shapeCov > 0.002f) {
                        const GradientEvalPod* brush = hoistedRectGradBrush;
                        const Color gradCol = brush
                            ? EvaluateGradientBrushSafe(
                                  *brush,
                                  su,
                                  sv,
                                  2.0f * halfW,
                                  2.0f * halfH)
                            : Color{1.0f, 1.0f, 1.0f, 1.0f};
                        const float modR = static_cast<float>(r8) * (1.0f / 255.0f);
                        const float modG = static_cast<float>(g8) * (1.0f / 255.0f);
                        const float modB = static_cast<float>(b8) * (1.0f / 255.0f);
                        const float finalAlpha = std::clamp(gradCol.a * baseAlpha * shapeCov, 0.0f, 1.0f);
                        if (finalAlpha > 0.002f) {
                            if (optPremulLayer) {
                                BlendPremulOver(
                                    *optPremulLayer,
                                    px,
                                    py,
                                    gradCol.r * modR * finalAlpha,
                                    gradCol.g * modG * finalAlpha,
                                    gradCol.b * modB * finalAlpha,
                                    finalAlpha);
                            } else {
                                const auto outR = static_cast<std::uint8_t>(
                                    std::clamp(gradCol.r * modR * 255.0f + 0.5f, 0.0f, 255.0f));
                                const auto outG = static_cast<std::uint8_t>(
                                    std::clamp(gradCol.g * modG * 255.0f + 0.5f, 0.0f, 255.0f));
                                const auto outB = static_cast<std::uint8_t>(
                                    std::clamp(gradCol.b * modB * 255.0f + 0.5f, 0.0f, 255.0f));
                                std::uint8_t* dst = outRgba8 +
                                    (static_cast<std::size_t>(py) * viewportWidth + static_cast<std::size_t>(px)) * 4u;
                                if (finalAlpha >= 1.0f && nd->blendMode == BlendingMode::Normal) {
                                    dst[0] = outR;
                                    dst[1] = outG;
                                    dst[2] = outB;
                                    dst[3] = 255u;
                                } else {
                                    BlendPixelOver(dst, outR, outG, outB, finalAlpha, nd->blendMode);
                                }
                            }
                        }
                    }
                    continue;
                }

                if (coverage > 0.002f) {
                    const bool isPremulPrim =
                        (prim == AeroPrimType::SolidQuad ||
                         prim == AeroPrimType::SdfTextGlyph ||
                         prim == AeroPrimType::DynamicMesh) &&
                        ((v0.flags & AeroVertexFlag_PremultipliedAlpha) != 0u);
                    const float effAlpha = std::clamp(pixelBaseAlpha * coverage, 0.0f, 1.0f);
                    if (optPremulLayer) {
                        const float inv255 = 1.0f / 255.0f;
                        const float rgbScale = isPremulPrim ? (inv255 * coverage) : (inv255 * effAlpha);
                        BlendPremulOver(
                            *optPremulLayer,
                            px,
                            py,
                            static_cast<float>(shadeR) * rgbScale,
                            static_cast<float>(shadeG) * rgbScale,
                            static_cast<float>(shadeB) * rgbScale,
                            effAlpha);
                    } else if (isPremulPrim) {
                        std::uint8_t* dst = outRgba8 +
                            (static_cast<std::size_t>(py) * viewportWidth + static_cast<std::size_t>(px)) * 4u;
                        const float invA = 1.0f - effAlpha;
                        dst[0] = static_cast<std::uint8_t>(std::clamp(
                            static_cast<float>(shadeR) * coverage + static_cast<float>(dst[0]) * invA + 0.5f,
                            0.0f,
                            255.0f));
                        dst[1] = static_cast<std::uint8_t>(std::clamp(
                            static_cast<float>(shadeG) * coverage + static_cast<float>(dst[1]) * invA + 0.5f,
                            0.0f,
                            255.0f));
                        dst[2] = static_cast<std::uint8_t>(std::clamp(
                            static_cast<float>(shadeB) * coverage + static_cast<float>(dst[2]) * invA + 0.5f,
                            0.0f,
                            255.0f));
                        dst[3] = static_cast<std::uint8_t>(std::clamp(
                            effAlpha * 255.0f + static_cast<float>(dst[3]) * invA + 0.5f,
                            0.0f,
                            255.0f));
                    } else {
                        std::uint8_t* dst = outRgba8 +
                            (static_cast<std::size_t>(py) * viewportWidth + static_cast<std::size_t>(px)) * 4u;
                        if (a8 == 255u && uniformVertexColor && coverage >= 1.0f && nd->blendMode == BlendingMode::Normal) {
                            dst[0] = shadeR;
                            dst[1] = shadeG;
                            dst[2] = shadeB;
                            dst[3] = 255u;
                        } else {
                            BlendPixelOver(dst, shadeR, shadeG, shadeB, effAlpha, nd->blendMode);
                        }
                    }
                }
            }
        }
    }
}

void ExecuteBackdropBlurSpan(
    const PagedRenderTree& tree,
    const BlurLayerSpan& span,
    std::uint32_t viewportWidth,
    std::uint32_t viewportHeight,
    std::uint8_t* outRgba8,
    PremulOffscreenLayer* optPremulLayer) {
    if (!SpanNeedsEffectPass(span) ||
        (outRgba8 == nullptr && optPremulLayer == nullptr)) {
        return;
    }
    const int vpW = static_cast<int>(std::min<std::uint32_t>(viewportWidth, 0x3FFFFFFFu));
    const int vpH = static_cast<int>(std::min<std::uint32_t>(viewportHeight, 0x3FFFFFFFu));
    const Rect mr = span.maskDestRect;
    if (mr.width <= 1.0f || mr.height <= 1.0f) {
        return;
    }
    const int surfX0 = optPremulLayer ? optPremulLayer->originX : 0;
    const int surfY0 = optPremulLayer ? optPremulLayer->originY : 0;
    const int surfX1 = optPremulLayer ? (optPremulLayer->originX + optPremulLayer->width) : vpW;
    const int surfY1 = optPremulLayer ? (optPremulLayer->originY + optPremulLayer->height) : vpH;
    const int padPx = ComputeSpanPaddingPx(span);
    const int bx0 = std::clamp(SafeFloorToInt(mr.x, surfX0 - padPx, surfX1 + padPx) - padPx, surfX0, surfX1);
    const int by0 = std::clamp(SafeFloorToInt(mr.y, surfY0 - padPx, surfY1 + padPx) - padPx, surfY0, surfY1);
    const int bx1 = std::clamp(SafeCeilToInt(mr.x + mr.width, surfX0 - padPx, surfX1 + padPx) + padPx, surfX0, surfX1);
    const int by1 = std::clamp(SafeCeilToInt(mr.y + mr.height, surfY0 - padPx, surfY1 + padPx) + padPx, surfY0, surfY1);
    if (bx1 <= bx0 || by1 <= by0) {
        return;
    }
    PremulOffscreenLayer bgLayer{};
    ScopedPremulLayerGuard bgLayerGuard{bgLayer};
    bgLayer.originX = bx0;
    bgLayer.originY = by0;
    bgLayer.width = bx1 - bx0;
    bgLayer.height = by1 - by0;
    gTlPremulLayerPool.Acquire(
        bgLayer.premulRgba,
        static_cast<std::size_t>(bgLayer.width) * static_cast<std::size_t>(bgLayer.height) * 4u,
        false);
    auto copyBackdropRowsFn = [&](int relY0, int relY1) {
        for (int ry = relY0; ry < relY1; ++ry) {
            const int py = by0 + ry;
            float* dstRow = bgLayer.premulRgba.data() +
                static_cast<std::size_t>(ry) * static_cast<std::size_t>(bgLayer.width) * 4u;
            if (optPremulLayer) {
                const float* srcRow = optPremulLayer->premulRgba.data() +
                    static_cast<std::size_t>(py - optPremulLayer->originY) *
                        static_cast<std::size_t>(optPremulLayer->width) * 4u +
                    static_cast<std::size_t>(bx0 - optPremulLayer->originX) * 4u;
                std::memcpy(dstRow, srcRow, static_cast<std::size_t>(bgLayer.width) * 4u * sizeof(float));
            } else {
                const std::uint8_t* srcRow = outRgba8 +
                    (static_cast<std::size_t>(py) * viewportWidth + static_cast<std::size_t>(bx0)) * 4u;
                for (int i = 0; i < bgLayer.width * 4; ++i) {
                    dstRow[i] = static_cast<float>(srcRow[i]) / 255.0f;
                }
            }
        }
    };
    if (!gInScanlineBand && static_cast<std::size_t>(bgLayer.width) * static_cast<std::size_t>(bgLayer.height) >= 16384u) {
        ScanlineWorkerPool::Instance().ParallelBands4(bgLayer.height, copyBackdropRowsFn);
    } else {
        copyBackdropRowsFn(0, bgLayer.height);
    }
    ApplySpanPostProcessEffect(tree.ResourceStore(), tree.RootDpiScale(), bgLayer, span);
    const AxbMtsdfPathEntry* maskEntry = nullptr;
    if (span.maskMtsdfHash != 0u) {
        maskEntry = tree.FindMtsdfEntry(span.maskMtsdfHash);
        if (!maskEntry) {
            const UIResourceStore* store = tree.ResourceStore() ? tree.ResourceStore() : &UIResourceStore::Default();
            maskEntry = store->FindMtsdfPath(span.maskMtsdfHash);
        }
    }
    int mx0 = std::clamp(SafeFloorToInt(mr.x, surfX0, surfX1), bx0, bx1);
    int my0 = std::clamp(SafeFloorToInt(mr.y, surfY0, surfY1), by0, by1);
    int mx1 = std::clamp(SafeCeilToInt(mr.x + mr.width, surfX0, surfX1), bx0, bx1);
    int my1 = std::clamp(SafeCeilToInt(mr.y + mr.height, surfY0, surfY1), by0, by1);
    if (span.clipActive) {
        if (!std::isfinite(span.clipRect.x) || !std::isfinite(span.clipRect.y) ||
            !std::isfinite(span.clipRect.width) || !std::isfinite(span.clipRect.height)) {
            mx1 = mx0;
        } else {
            mx0 = std::max(mx0, SafeFloorToInt(span.clipRect.x, surfX0, surfX1));
            my0 = std::max(my0, SafeFloorToInt(span.clipRect.y, surfY0, surfY1));
            mx1 = std::min(mx1, SafeCeilToInt(span.clipRect.x + std::max(0.0f, span.clipRect.width), surfX0, surfX1));
            my1 = std::min(my1, SafeCeilToInt(span.clipRect.y + std::max(0.0f, span.clipRect.height), surfY0, surfY1));
        }
    }
    if (gInScanlineBand) {
        my0 = std::max(my0, gRasterBandY0);
        my1 = std::min(my1, gRasterBandY1);
    }
    float cellW = 1.0f, cellH = 1.0f, cellBoxX0 = 0.0f, cellBoxY0 = 0.0f, scaledW = 1.0f, scaledH = 1.0f;
    float cellRenderedW = mr.width, cellRenderedH = mr.height;
    if (maskEntry && maskEntry->cellWidth > 0u && maskEntry->cellHeight > 0u) {
        cellW = static_cast<float>(maskEntry->cellWidth);
        cellH = static_cast<float>(maskEntry->cellHeight);
        const float safeRange = std::max(maskEntry->pxRange, 1.0f);
        const float padTexels = std::ceil(safeRange) + 1.0f;
        const float usableW = std::max(2.0f, cellW - 2.0f * padTexels);
        const float usableH = std::max(2.0f, cellH - 2.0f * padTexels);
        const float uniformScale = std::min(
            usableW / std::max(1e-3f, maskEntry->boundsW),
            usableH / std::max(1e-3f, maskEntry->boundsH));
        scaledW = std::max(1e-3f, maskEntry->boundsW * uniformScale);
        scaledH = std::max(1e-3f, maskEntry->boundsH * uniformScale);
        cellBoxX0 = (cellW - scaledW) * 0.5f;
        cellBoxY0 = (cellH - scaledH) * 0.5f;
        cellRenderedW = mr.width * (cellW / scaledW);
        cellRenderedH = mr.height * (cellH / scaledH);
    }
    if (my1 > my0 && mx1 > mx0) {
        const int maskRows = my1 - my0;
        auto compositeBackdropFn = [&](int relY0, int relY1) {
            for (int ry = relY0; ry < relY1; ++ry) {
                const int py = my0 + ry;
                const float relY = ((static_cast<float>(py) + 0.5f) - mr.y) / std::max(1.0f, mr.height);
                const float normY = (cellBoxY0 + relY * scaledH) / cellH;
                const float* blurRow = bgLayer.premulRgba.data() +
                    static_cast<std::size_t>(py - by0) * static_cast<std::size_t>(bgLayer.width) * 4u;
                for (int px = mx0; px < mx1; ++px) {
                    const float relX = ((static_cast<float>(px) + 0.5f) - mr.x) / std::max(1.0f, mr.width);
                    const float normX = (cellBoxX0 + relX * scaledW) / cellW;
                    float cov = 1.0f;
                    if (maskEntry) {
                        cov = EvaluateMtsdfCoverage(*maskEntry, normX, normY, cellRenderedW, cellRenderedH, false);
                    }
                    const float effMask = std::clamp(cov * span.maskOpacity, 0.0f, 1.0f);
                    if (effMask <= 0.002f) continue;
                    const float* bp = blurRow + static_cast<std::size_t>(px - bx0) * 4u;
                    const float pa = std::clamp(bp[3] * effMask, 0.0f, 1.0f);
                    if (pa <= 0.001f) continue;
                    const float pr = bp[0] * effMask;
                    const float pg = bp[1] * effMask;
                    const float pb = bp[2] * effMask;
                    if (optPremulLayer) {
                        BlendPremulOver(*optPremulLayer, px, py, pr, pg, pb, pa);
                    } else {
                        std::uint8_t* dst = outRgba8 +
                            (static_cast<std::size_t>(py) * viewportWidth + static_cast<std::size_t>(px)) * 4u;
                        const float invA = 1.0f - pa;
                        dst[0] = static_cast<std::uint8_t>(
                            std::clamp(pr * 255.0f + static_cast<float>(dst[0]) * invA + 0.5f, 0.0f, 255.0f));
                        dst[1] = static_cast<std::uint8_t>(
                            std::clamp(pg * 255.0f + static_cast<float>(dst[1]) * invA + 0.5f, 0.0f, 255.0f));
                        dst[2] = static_cast<std::uint8_t>(
                            std::clamp(pb * 255.0f + static_cast<float>(dst[2]) * invA + 0.5f, 0.0f, 255.0f));
                        dst[3] = static_cast<std::uint8_t>(
                            std::clamp(pa * 255.0f + static_cast<float>(dst[3]) * invA + 0.5f, 0.0f, 255.0f));
                    }
                }
            }
        };
        if (!gInScanlineBand && static_cast<std::size_t>(mx1 - mx0) * static_cast<std::size_t>(maskRows) >= 16384u) {
            ScanlineWorkerPool::Instance().ParallelBands4(maskRows, compositeBackdropFn);
        } else {
            compositeBackdropFn(0, maskRows);
        }
    }
}

void RasterizeNodeSpanRecursive(
    const PagedRenderTree& tree,
    std::uint32_t startNodeId,
    std::uint32_t endNodeId,
    std::size_t& ioBlurCursor,
    std::uint32_t viewportWidth,
    std::uint32_t viewportHeight,
    std::uint8_t* outRgba8,
    PremulOffscreenLayer* optPremulLayer) {
    const int vpW = static_cast<int>(std::min<std::uint32_t>(viewportWidth, 0x3FFFFFFFu));
    const int vpH = static_cast<int>(std::min<std::uint32_t>(viewportHeight, 0x3FFFFFFFu));
    const auto& blurSpans = tree.BlurLayerSpans();
    std::uint32_t nid = startNodeId;
    while (nid < endNodeId) {
        while (ioBlurCursor < blurSpans.size() &&
               (blurSpans[ioBlurCursor].firstNodeId < nid ||
                (blurSpans[ioBlurCursor].endNodeId <= nid &&
                 blurSpans[ioBlurCursor].effectMode != UIEffectKind::BackdropBlur) ||
                !SpanNeedsEffectPass(blurSpans[ioBlurCursor]))) {
            ++ioBlurCursor;
        }
        if (ioBlurCursor < blurSpans.size() &&
            blurSpans[ioBlurCursor].firstNodeId == nid &&
            (blurSpans[ioBlurCursor].endNodeId > nid ||
             blurSpans[ioBlurCursor].effectMode == UIEffectKind::BackdropBlur) &&
            blurSpans[ioBlurCursor].endNodeId <= endNodeId &&
            SpanNeedsEffectPass(blurSpans[ioBlurCursor])) {
            const BlurLayerSpan span = blurSpans[ioBlurCursor];
            ++ioBlurCursor;

            if (span.effectMode == UIEffectKind::BackdropBlur && (outRgba8 != nullptr || optPremulLayer != nullptr)) {
                ExecuteBackdropBlurSpan(tree, span, viewportWidth, viewportHeight, outRgba8, optPremulLayer);
                // Do NOT skip [span.firstNodeId, span.endNodeId): the frosted-glass element's own
                // tint/border and child controls must still render on top of the blurred backdrop.
                continue;
            }

            float uMinX = 0.0f;
            float uMinY = 0.0f;
            float uMaxX = 0.0f;
            float uMaxY = 0.0f;
            if (ComputeNodeRangeScreenBounds(tree, span.firstNodeId, span.endNodeId, uMinX, uMinY, uMaxX, uMaxY)) {
                const int padPx = ComputeSpanPaddingPx(span);
                const int bx0 = std::clamp(SafeFloorToInt(uMinX, -padPx, vpW + padPx) - padPx, 0, vpW);
                const int by0 = std::clamp(SafeFloorToInt(uMinY, -padPx, vpH + padPx) - padPx, 0, vpH);
                const int bx1 = std::clamp(SafeCeilToInt(uMaxX, -padPx, vpW + padPx) + padPx, 0, vpW);
                const int by1 = std::clamp(SafeCeilToInt(uMaxY, -padPx, vpH + padPx) + padPx, 0, vpH);

                if (bx1 > bx0 && by1 > by0) {
                    PremulOffscreenLayer subLayer{};
                    ScopedPremulLayerGuard subLayerGuard{subLayer};
                    subLayer.originX = bx0;
                    subLayer.originY = by0;
                    subLayer.width = bx1 - bx0;
                    subLayer.height = by1 - by0;
                    gTlPremulLayerPool.Acquire(
                        subLayer.premulRgba,
                        static_cast<std::size_t>(subLayer.width) * static_cast<std::size_t>(subLayer.height) * 4u,
                        true);

                    const int savedBandY0 = gRasterBandY0;
                    const int savedBandY1 = gRasterBandY1;
                    gRasterBandY0 = subLayer.originY;
                    gRasterBandY1 = subLayer.originY + subLayer.height;
                    const std::size_t baseSubCursor = ioBlurCursor;
                    const bool hasInnerBlurSpan =
                        (baseSubCursor < blurSpans.size() && blurSpans[baseSubCursor].firstNodeId < span.endNodeId);
                    const bool canParallelSub =
                        !gInScanlineBand &&
                        !hasInnerBlurSpan &&
                        (static_cast<std::size_t>(subLayer.width) * static_cast<std::size_t>(subLayer.height) >= 16384u) &&
                        (span.endNodeId - span.firstNodeId >= 4u) &&
                        (subLayer.height >= 64);
                    if (canParallelSub) {
                        std::size_t endSubCursor = baseSubCursor;
                        ScanlineWorkerPool::Instance().ParallelBands4(
                            subLayer.height,
                            [&](int relY0, int relY1) {
                                ScopedBandTexCacheGuard subBandTexGuard;
                                const int p0 = gRasterBandY0;
                                const int p1 = gRasterBandY1;
                                gRasterBandY0 = subLayer.originY + relY0;
                                gRasterBandY1 = subLayer.originY + relY1;
                                std::size_t localC = baseSubCursor;
                                RasterizeNodeSpanRecursive(
                                    tree,
                                    span.firstNodeId,
                                    span.endNodeId,
                                    localC,
                                    viewportWidth,
                                    viewportHeight,
                                    outRgba8,
                                    &subLayer);
                                if (relY0 == 0) {
                                    endSubCursor = localC;
                                }
                                gRasterBandY0 = p0;
                                gRasterBandY1 = p1;
                            });
                        ioBlurCursor = endSubCursor;
                        while (ioBlurCursor < blurSpans.size() && blurSpans[ioBlurCursor].firstNodeId < span.endNodeId) {
                            ++ioBlurCursor;
                        }
                    } else {
                        RasterizeNodeSpanRecursive(
                            tree, span.firstNodeId, span.endNodeId, ioBlurCursor, viewportWidth, viewportHeight, outRgba8, &subLayer);
                    }
                    gRasterBandY0 = savedBandY0;
                    gRasterBandY1 = savedBandY1;

                    ApplySpanPostProcessEffect(tree.ResourceStore(), tree.RootDpiScale(), subLayer, span);

                    int compX0 = bx0;
                    int compY0 = by0;
                    int compX1 = bx1;
                    int compY1 = by1;
                    if (span.clipActive) {
                        if (!std::isfinite(span.clipRect.x) || !std::isfinite(span.clipRect.y) ||
                            !std::isfinite(span.clipRect.width) || !std::isfinite(span.clipRect.height)) {
                            compX1 = compX0;
                        } else {
                            compX0 = std::max(compX0, SafeFloorToInt(span.clipRect.x, 0, vpW));
                            compY0 = std::max(compY0, SafeFloorToInt(span.clipRect.y, 0, vpH));
                            compX1 = std::min(compX1, SafeCeilToInt(span.clipRect.x + std::max(0.0f, span.clipRect.width), 0, vpW));
                            compY1 = std::min(compY1, SafeCeilToInt(span.clipRect.y + std::max(0.0f, span.clipRect.height), 0, vpH));
                        }
                    }
                    if (const RenderNodeDescriptor* firstNd = tree.GetNode(span.firstNodeId);
                        firstNd && firstNd->pageIndex < tree.PageCount()) {
                        const CommandPage& firstPage = tree.GetPage(firstNd->pageIndex);
                        if ((firstPage.header.flags & CommandPageFlag_ScissorClipActive) != 0u) {
                            if (!std::isfinite(firstPage.header.clipX) || !std::isfinite(firstPage.header.clipY) ||
                                !std::isfinite(firstPage.header.clipW) || !std::isfinite(firstPage.header.clipH)) {
                                compX1 = compX0;
                            } else {
                                compX0 = std::max(compX0, SafeFloorToInt(firstPage.header.clipX, 0, vpW));
                                compY0 = std::max(compY0, SafeFloorToInt(firstPage.header.clipY, 0, vpH));
                                compX1 = std::min(compX1, SafeCeilToInt(firstPage.header.clipX + std::max(0.0f, firstPage.header.clipW), 0, vpW));
                                compY1 = std::min(compY1, SafeCeilToInt(firstPage.header.clipY + std::max(0.0f, firstPage.header.clipH), 0, vpH));
                            }
                        }
                    }
                    if (optPremulLayer) {
                        compX0 = std::max(compX0, optPremulLayer->originX);
                        compY0 = std::max(compY0, optPremulLayer->originY);
                        compX1 = std::min(compX1, optPremulLayer->originX + optPremulLayer->width);
                        compY1 = std::min(compY1, optPremulLayer->originY + optPremulLayer->height);
                    }
                    if (gInScanlineBand) {
                        compY0 = std::max(compY0, gRasterBandY0);
                        compY1 = std::min(compY1, gRasterBandY1);
                    }

                    for (int py = compY0; py < compY1; ++py) {
                        const float* srcRow = subLayer.premulRgba.data() +
                            static_cast<std::size_t>(py - subLayer.originY) * static_cast<std::size_t>(subLayer.width) * 4u;
                        for (int px = compX0; px < compX1; ++px) {
                            const float* sp = srcRow + static_cast<std::size_t>(px - subLayer.originX) * 4u;
                            const float pa = std::clamp(sp[3], 0.0f, 1.0f);
                            if (pa <= 0.001f) {
                                continue;
                            }
                            if (optPremulLayer) {
                                BlendPremulOver(*optPremulLayer, px, py, sp[0], sp[1], sp[2], pa, span.blendMode);
                            } else {
                                std::uint8_t* dst = outRgba8 +
                                    (static_cast<std::size_t>(py) * viewportWidth + static_cast<std::size_t>(px)) * 4u;
                                BlendPremulOverRgba8(dst, sp[0], sp[1], sp[2], pa, span.blendMode);
                            }
                        }
                    }
                }
            }
            nid = span.endNodeId;
            continue;
        }

        RasterizeSingleNode(tree, nid, viewportWidth, viewportHeight, outRgba8, optPremulLayer);
        ++nid;
    }
}

} // namespace

void ZeroCopyGpuStream::ReleaseTransientPools() noexcept {
    gTlPremulLayerPool.Clear();
}

void ZeroCopyGpuStream::RasterizeTreeToRgba8(
    const PagedRenderTree& tree,
    std::uint32_t viewportWidth,
    std::uint32_t viewportHeight,
    std::vector<std::uint8_t>& outRgba8,
    Color clearColor) {
    const std::size_t pixelCount = static_cast<std::size_t>(viewportWidth) * viewportHeight;
    outRgba8.resize(pixelCount * 4u);
    if (pixelCount == 0u) {
        return;
    }
    ScopedBandTexCacheGuard rootTexGuard;

    const std::uint32_t clearPacked = PackColorRgba8(clearColor, 1.0f);
    const std::uint8_t cr = static_cast<std::uint8_t>(clearPacked & 0xFFu);
    const std::uint8_t cg = static_cast<std::uint8_t>((clearPacked >> 8u) & 0xFFu);
    const std::uint8_t cb = static_cast<std::uint8_t>((clearPacked >> 16u) & 0xFFu);
    const std::uint8_t ca = static_cast<std::uint8_t>((clearPacked >> 24u) & 0xFFu);
    if (cr == cg && cg == cb && cb == ca) {
        std::memset(outRgba8.data(), cr, outRgba8.size());
    } else {
        const std::uint8_t clearRgba[4]{cr, cg, cb, ca};
        std::uint32_t clearWord = 0u;
        std::memcpy(&clearWord, clearRgba, 4u);
        std::fill_n(reinterpret_cast<std::uint32_t*>(outRgba8.data()), pixelCount, clearWord);
    }

    const std::uint32_t nNodes = static_cast<std::uint32_t>(tree.NodeCount());
    if (nNodes == 0u) {
        return;
    }

    if (pixelCount >= 16384u && viewportHeight >= 64u) {
        std::uint8_t* rawOut = outRgba8.data();
        const auto& blurSpans = tree.BlurLayerSpans();
        auto runParallelNodeSlice = [&](std::uint32_t sliceStart, std::uint32_t sliceEnd, std::size_t startCursor) {
            if (sliceEnd <= sliceStart) return;
            if (sliceEnd - sliceStart < 2u && pixelCount < 65536u) {
                std::size_t c = startCursor;
                RasterizeNodeSpanRecursive(tree, sliceStart, sliceEnd, c, viewportWidth, viewportHeight, rawOut, nullptr);
                return;
            }
            const int vpHInt = static_cast<int>(std::min<std::uint32_t>(viewportHeight, 0x3FFFFFFFu));
            const bool hasSliceBlur =
                startCursor < blurSpans.size() && blurSpans[startCursor].firstNodeId < sliceEnd;
            if (!hasSliceBlur && (sliceEnd - sliceStart) >= 16u && vpHInt >= 4) {
                // Pre-filter active node IDs per vertical band in exact painter's order so worker threads skip non-overlapping nodes.
                thread_local std::vector<std::pair<int, int>> tlNodeYRange;
                const std::size_t sliceLen = static_cast<std::size_t>(sliceEnd - sliceStart);
                tlNodeYRange.resize(sliceLen);
                const std::size_t totalPages = tree.PageCount();
                for (std::size_t idx = 0u; idx < sliceLen; ++idx) {
                    const RenderNodeDescriptor* nd = tree.GetNode(sliceStart + static_cast<std::uint32_t>(idx));
                    if (!nd || nd->pageIndex >= totalPages || !std::isfinite(nd->opacity) || nd->opacity <= 0.001f) {
                        tlNodeYRange[idx] = {1, 0};
                        continue;
                    }
                    const CommandPage& page = tree.GetPage(nd->pageIndex);
                    const bool clipOn = (page.header.flags & CommandPageFlag_ScissorClipActive) != 0u;
                    if (clipOn &&
                        (!std::isfinite(page.header.clipX) || !std::isfinite(page.header.clipY) ||
                         !std::isfinite(page.header.clipW) || !std::isfinite(page.header.clipH))) {
                        tlNodeYRange[idx] = {1, 0};
                        continue;
                    }
                    const int cMinY = clipOn ? SafeFloorToInt(page.header.clipY, 0, vpHInt) : 0;
                    const int cMaxY = clipOn
                        ? SafeCeilToInt(page.header.clipY + std::max(0.0f, page.header.clipH), 0, vpHInt)
                        : vpHInt;
                    if (cMaxY <= cMinY) {
                        tlNodeYRange[idx] = {1, 0};
                        continue;
                    }
                    const std::size_t quadCount = nd->vertexCount / 4u;
                    int nY0 = vpHInt;
                    int nY1 = 0;
                    for (std::size_t q = 0u; q < quadCount; ++q) {
                        const std::size_t vIdx = nd->vertexOffsetInPage + q * 4u;
                        if (vIdx + 4u > kVerticesPerCommandPage) break;
                        const AeroVertex& v0 = page.vertices[vIdx + 0u];
                        const AeroVertex& v1 = page.vertices[vIdx + 1u];
                        const AeroVertex& v2 = page.vertices[vIdx + 2u];
                        const AeroVertex& v3 = page.vertices[vIdx + 3u];
                        if (((v0.rgba | v1.rgba | v2.rgba | v3.rgba) & 0xFF000000u) == 0u) continue;
                        if (!std::isfinite(v0.y) || !std::isfinite(v1.y) ||
                            !std::isfinite(v2.y) || !std::isfinite(v3.y)) continue;
                        const float qMinY = std::min(std::min(v0.y, v1.y), std::min(v2.y, v3.y));
                        const float qMaxY = std::max(std::max(v0.y, v1.y), std::max(v2.y, v3.y));
                        nY0 = std::min(nY0, SafeFloorToInt(qMinY, cMinY, cMaxY));
                        nY1 = std::max(nY1, SafeCeilToInt(qMaxY, cMinY, cMaxY));
                    }
                    tlNodeYRange[idx] = {nY0, nY1};
                }
                const auto* yRangesPtr = tlNodeYRange.data();
                ScanlineWorkerPool::Instance().ParallelBands4(
                    vpHInt,
                    [&](int y0, int y1) {
                        ScopedBandTexCacheGuard bandTexGuard;
                        const int prevY0 = gRasterBandY0;
                        const int prevY1 = gRasterBandY1;
                        gRasterBandY0 = y0;
                        gRasterBandY1 = y1;
                        for (std::size_t idx = 0u; idx < sliceLen; ++idx) {
                            const auto& yr = yRangesPtr[idx];
                            if (yr.second > y0 && yr.first < y1) {
                                RasterizeSingleNode(
                                    tree,
                                    sliceStart + static_cast<std::uint32_t>(idx),
                                    viewportWidth,
                                    viewportHeight,
                                    rawOut,
                                    nullptr);
                            }
                        }
                        gRasterBandY0 = prevY0;
                        gRasterBandY1 = prevY1;
                    });
                return;
            }
            ScanlineWorkerPool::Instance().ParallelBands4(
                static_cast<int>(viewportHeight),
                [&](int y0, int y1) {
                    ScopedBandTexCacheGuard bandTexGuard;
                    const int prevY0 = gRasterBandY0;
                    const int prevY1 = gRasterBandY1;
                    gRasterBandY0 = y0;
                    gRasterBandY1 = y1;
                    std::size_t localBlurCursor = startCursor;
                    RasterizeNodeSpanRecursive(
                        tree, sliceStart, sliceEnd, localBlurCursor, viewportWidth, viewportHeight, rawOut, nullptr);
                    gRasterBandY0 = prevY0;
                    gRasterBandY1 = prevY1;
                });
        };

        if (blurSpans.empty()) {
            runParallelNodeSlice(0u, nNodes, 0u);
            return;
        }

        // Partition top-level execution at BlurLayerSpans so non-blur ranges run across all 4 parallel bands,
        // each Blur span executes coherently once, and each BackdropBlur executes before its node range
        // while still allowing child nodes inside frosted containers to run in parallel bands.
        std::uint32_t curNode = 0u;
        std::size_t cursor = 0u;
        while (curNode < nNodes) {
            while (cursor < blurSpans.size() &&
                   (blurSpans[cursor].firstNodeId < curNode ||
                    (blurSpans[cursor].endNodeId <= curNode &&
                     blurSpans[cursor].effectMode != UIEffectKind::BackdropBlur) ||
                    !SpanNeedsEffectPass(blurSpans[cursor]))) {
                ++cursor;
            }
            if (cursor >= blurSpans.size()) {
                runParallelNodeSlice(curNode, nNodes, cursor);
                break;
            }
            const BlurLayerSpan& nextSpan = blurSpans[cursor];
            if (nextSpan.firstNodeId > curNode) {
                runParallelNodeSlice(curNode, std::min(nextSpan.firstNodeId, nNodes), cursor);
                curNode = std::min(nextSpan.firstNodeId, nNodes);
                if (curNode >= nNodes) break;
            }
            if (nextSpan.firstNodeId <= curNode) {
                if (nextSpan.effectMode == UIEffectKind::BackdropBlur) {
                    ExecuteBackdropBlurSpan(tree, nextSpan, viewportWidth, viewportHeight, rawOut, nullptr);
                    ++cursor;
                    continue;
                }
                if (nextSpan.endNodeId <= curNode || !SpanNeedsEffectPass(nextSpan)) {
                    ++cursor;
                    continue;
                }
                const std::uint32_t spanStop = std::min(nextSpan.endNodeId, nNodes);
                std::size_t stepCursor = cursor;
                RasterizeNodeSpanRecursive(tree, curNode, spanStop, stepCursor, viewportWidth, viewportHeight, rawOut, nullptr);
                curNode = spanStop;
                cursor = stepCursor;
            }
        }
        while (cursor < blurSpans.size()) {
            if (blurSpans[cursor].effectMode == UIEffectKind::BackdropBlur &&
                blurSpans[cursor].firstNodeId <= nNodes) {
                ExecuteBackdropBlurSpan(tree, blurSpans[cursor], viewportWidth, viewportHeight, rawOut, nullptr);
            }
            ++cursor;
        }
        return;
    }

    std::size_t blurCursor = 0u;
    RasterizeNodeSpanRecursive(
        tree,
        0u,
        nNodes,
        blurCursor,
        viewportWidth,
        viewportHeight,
        outRgba8.data(),
        nullptr);
}

bool ZeroCopyGpuStream::RasterizeBlurSpanPatchPremulRgba8(
    const PagedRenderTree& tree,
    const BlurLayerSpan& span,
    std::size_t& ioBlurCursor,
    std::uint32_t viewportWidth,
    std::uint32_t viewportHeight,
    int& outPatchX,
    int& outPatchY,
    int& outPatchW,
    int& outPatchH,
    std::vector<std::uint8_t>& outPremulRgba8) {
    outPatchX = 0;
    outPatchY = 0;
    outPatchW = 0;
    outPatchH = 0;
    outPremulRgba8.clear();
    if (viewportWidth == 0u || viewportHeight == 0u ||
        span.endNodeId <= span.firstNodeId ||
        !SpanNeedsEffectPass(span)) {
        return false;
    }

    const int vpW = static_cast<int>(std::min<std::uint32_t>(viewportWidth, 0x3FFFFFFFu));
    const int vpH = static_cast<int>(std::min<std::uint32_t>(viewportHeight, 0x3FFFFFFFu));
    float uMinX = 0.0f;
    float uMinY = 0.0f;
    float uMaxX = 0.0f;
    float uMaxY = 0.0f;
    if (!ComputeNodeRangeScreenBounds(tree, span.firstNodeId, span.endNodeId, uMinX, uMinY, uMaxX, uMaxY)) {
        return false;
    }

    const int padPx = ComputeSpanPaddingPx(span);
    const int bx0 = std::clamp(SafeFloorToInt(uMinX, -padPx, vpW + padPx) - padPx, 0, vpW);
    const int by0 = std::clamp(SafeFloorToInt(uMinY, -padPx, vpH + padPx) - padPx, 0, vpH);
    const int bx1 = std::clamp(SafeCeilToInt(uMaxX, -padPx, vpW + padPx) + padPx, 0, vpW);
    const int by1 = std::clamp(SafeCeilToInt(uMaxY, -padPx, vpH + padPx) + padPx, 0, vpH);
    if (bx1 <= bx0 || by1 <= by0) {
        return false;
    }

    ScopedBandTexCacheGuard patchTexGuard;

    PremulOffscreenLayer subLayer{};
    ScopedPremulLayerGuard subLayerGuard{subLayer};
    subLayer.originX = bx0;
    subLayer.originY = by0;
    subLayer.width = bx1 - bx0;
    subLayer.height = by1 - by0;
    gTlPremulLayerPool.Acquire(
        subLayer.premulRgba,
        static_cast<std::size_t>(subLayer.width) * static_cast<std::size_t>(subLayer.height) * 4u,
        true);

    const auto& blurSpans = tree.BlurLayerSpans();
    if (ioBlurCursor < blurSpans.size() &&
        blurSpans[ioBlurCursor].firstNodeId == span.firstNodeId &&
        blurSpans[ioBlurCursor].endNodeId == span.endNodeId &&
        blurSpans[ioBlurCursor].effectMode == span.effectMode) {
        ++ioBlurCursor;
    }

    RasterizeNodeSpanRecursive(
        tree,
        span.firstNodeId,
        span.endNodeId,
        ioBlurCursor,
        viewportWidth,
        viewportHeight,
        nullptr,
        &subLayer);

    ApplySpanPostProcessEffect(tree.ResourceStore(), tree.RootDpiScale(), subLayer, span);

    int compX0 = bx0;
    int compY0 = by0;
    int compX1 = bx1;
    int compY1 = by1;
    if (span.clipActive) {
        if (!std::isfinite(span.clipRect.x) || !std::isfinite(span.clipRect.y) ||
            !std::isfinite(span.clipRect.width) || !std::isfinite(span.clipRect.height)) {
            return false;
        }
        compX0 = std::max(compX0, SafeFloorToInt(span.clipRect.x, 0, vpW));
        compY0 = std::max(compY0, SafeFloorToInt(span.clipRect.y, 0, vpH));
        compX1 = std::min(compX1, SafeCeilToInt(span.clipRect.x + std::max(0.0f, span.clipRect.width), 0, vpW));
        compY1 = std::min(compY1, SafeCeilToInt(span.clipRect.y + std::max(0.0f, span.clipRect.height), 0, vpH));
    }
    if (const RenderNodeDescriptor* firstNd = tree.GetNode(span.firstNodeId);
        firstNd && firstNd->pageIndex < tree.PageCount()) {
        const CommandPage& firstPage = tree.GetPage(firstNd->pageIndex);
        if ((firstPage.header.flags & CommandPageFlag_ScissorClipActive) != 0u) {
            if (!std::isfinite(firstPage.header.clipX) || !std::isfinite(firstPage.header.clipY) ||
                !std::isfinite(firstPage.header.clipW) || !std::isfinite(firstPage.header.clipH)) {
                return false;
            }
            compX0 = std::max(compX0, SafeFloorToInt(firstPage.header.clipX, 0, vpW));
            compY0 = std::max(compY0, SafeFloorToInt(firstPage.header.clipY, 0, vpH));
            compX1 = std::min(compX1, SafeCeilToInt(firstPage.header.clipX + std::max(0.0f, firstPage.header.clipW), 0, vpW));
            compY1 = std::min(compY1, SafeCeilToInt(firstPage.header.clipY + std::max(0.0f, firstPage.header.clipH), 0, vpH));
        }
    }
    if (compX1 <= compX0 || compY1 <= compY0) {
        return false;
    }

    const int pw = compX1 - compX0;
    const int ph = compY1 - compY0;
    outPremulRgba8.resize(static_cast<std::size_t>(pw) * static_cast<std::size_t>(ph) * 4u);
    bool anyVisible = false;
    for (int py = compY0; py < compY1; ++py) {
        const float* srcRow = subLayer.premulRgba.data() +
            static_cast<std::size_t>(py - subLayer.originY) * static_cast<std::size_t>(subLayer.width) * 4u;
        std::uint8_t* dstRow = outPremulRgba8.data() +
            static_cast<std::size_t>(py - compY0) * static_cast<std::size_t>(pw) * 4u;
        for (int px = compX0; px < compX1; ++px) {
            const float* sp = srcRow + static_cast<std::size_t>(px - subLayer.originX) * 4u;
            std::uint8_t* dp = dstRow + static_cast<std::size_t>(px - compX0) * 4u;
            const float pa = std::clamp(sp[3], 0.0f, 1.0f);
            if (pa > 0.001f) {
                anyVisible = true;
            }
            dp[0] = static_cast<std::uint8_t>(std::clamp(sp[0] * 255.0f + 0.5f, 0.0f, 255.0f));
            dp[1] = static_cast<std::uint8_t>(std::clamp(sp[1] * 255.0f + 0.5f, 0.0f, 255.0f));
            dp[2] = static_cast<std::uint8_t>(std::clamp(sp[2] * 255.0f + 0.5f, 0.0f, 255.0f));
            dp[3] = static_cast<std::uint8_t>(std::clamp(pa * 255.0f + 0.5f, 0.0f, 255.0f));
        }
    }
    if (!anyVisible) {
        return false;
    }
    outPatchX = compX0;
    outPatchY = compY0;
    outPatchW = pw;
    outPatchH = ph;
    return true;
}

bool ZeroCopyGpuStream::ComputeBackdropBlurPatchPremulRgba8(
    const BlurLayerSpan& span,
    std::uint32_t viewportWidth,
    std::uint32_t viewportHeight,
    int readX,
    int readY,
    int readW,
    int readH,
    const std::uint8_t* topDownReadbackRgba8,
    int& outPatchX,
    int& outPatchY,
    int& outPatchW,
    int& outPatchH,
    std::vector<std::uint8_t>& outPremulRgba8,
    const UIResourceStore* optResourceStore) {
    outPatchX = 0;
    outPatchY = 0;
    outPatchW = 0;
    outPatchH = 0;
    outPremulRgba8.clear();
    if (!topDownReadbackRgba8 || readW <= 0 || readH <= 0 ||
        viewportWidth == 0u || viewportHeight == 0u ||
        !SpanNeedsEffectPass(span)) {
        return false;
    }
    const Rect mr = span.maskDestRect;
    if (!std::isfinite(mr.x) || !std::isfinite(mr.y) ||
        !std::isfinite(mr.width) || !std::isfinite(mr.height) ||
        mr.width <= 1.0f || mr.height <= 1.0f) {
        return false;
    }

    const int vpW = static_cast<int>(std::min<std::uint32_t>(viewportWidth, 0x3FFFFFFFu));
    const int vpH = static_cast<int>(std::min<std::uint32_t>(viewportHeight, 0x3FFFFFFFu));

    PremulOffscreenLayer bgLayer{};
    ScopedPremulLayerGuard bgLayerGuard{bgLayer};
    bgLayer.originX = readX;
    bgLayer.originY = readY;
    bgLayer.width = readW;
    bgLayer.height = readH;
    gTlPremulLayerPool.Acquire(
        bgLayer.premulRgba,
        static_cast<std::size_t>(readW) * static_cast<std::size_t>(readH) * 4u,
        false);
    for (int ry = 0; ry < readH; ++ry) {
        const std::uint8_t* srcRow = topDownReadbackRgba8 +
            static_cast<std::size_t>(ry) * static_cast<std::size_t>(readW) * 4u;
        float* dstRow = bgLayer.premulRgba.data() +
            static_cast<std::size_t>(ry) * static_cast<std::size_t>(readW) * 4u;
        for (int rx = 0; rx < readW; ++rx) {
            const std::uint8_t* sp = srcRow + static_cast<std::size_t>(rx) * 4u;
            float* dp = dstRow + static_cast<std::size_t>(rx) * 4u;
            dp[0] = static_cast<float>(sp[0]) / 255.0f;
            dp[1] = static_cast<float>(sp[1]) / 255.0f;
            dp[2] = static_cast<float>(sp[2]) / 255.0f;
            dp[3] = static_cast<float>(sp[3]) / 255.0f;
        }
    }
    ApplySpanPostProcessEffect(optResourceStore, 1.0f, bgLayer, span);

    int mx0 = std::clamp(SafeFloorToInt(mr.x, 0, vpW), readX, readX + readW);
    int my0 = std::clamp(SafeFloorToInt(mr.y, 0, vpH), readY, readY + readH);
    int mx1 = std::clamp(SafeCeilToInt(mr.x + mr.width, 0, vpW), readX, readX + readW);
    int my1 = std::clamp(SafeCeilToInt(mr.y + mr.height, 0, vpH), readY, readY + readH);
    if (span.clipActive) {
        if (!std::isfinite(span.clipRect.x) || !std::isfinite(span.clipRect.y) ||
            !std::isfinite(span.clipRect.width) || !std::isfinite(span.clipRect.height)) {
            return false;
        }
        mx0 = std::max(mx0, SafeFloorToInt(span.clipRect.x, 0, vpW));
        my0 = std::max(my0, SafeFloorToInt(span.clipRect.y, 0, vpH));
        mx1 = std::min(mx1, SafeCeilToInt(span.clipRect.x + std::max(0.0f, span.clipRect.width), 0, vpW));
        my1 = std::min(my1, SafeCeilToInt(span.clipRect.y + std::max(0.0f, span.clipRect.height), 0, vpH));
    }
    if (mx1 <= mx0 || my1 <= my0) {
        return false;
    }

    const UIResourceStore* maskStore = optResourceStore ? optResourceStore : &UIResourceStore::Default();
    const AxbMtsdfPathEntry* maskEntry = (span.maskMtsdfHash != 0u)
        ? maskStore->FindMtsdfPath(span.maskMtsdfHash)
        : nullptr;
    const float screenScale = std::max(
        1.0f,
        0.5f * (mr.width / std::max(1.0f, maskEntry ? maskEntry->boundsW : mr.width) +
                mr.height / std::max(1.0f, maskEntry ? maskEntry->boundsH : mr.height)));

    const int pw = mx1 - mx0;
    const int ph = my1 - my0;
    outPremulRgba8.assign(static_cast<std::size_t>(pw) * static_cast<std::size_t>(ph) * 4u, 0u);
    float cellW = 1.0f, cellH = 1.0f, cellBoxX0 = 0.0f, cellBoxY0 = 0.0f, scaledW = 1.0f, scaledH = 1.0f;
    float cellRenderedW = mr.width, cellRenderedH = mr.height;
    if (maskEntry && maskEntry->cellWidth > 0u && maskEntry->cellHeight > 0u) {
        cellW = static_cast<float>(maskEntry->cellWidth);
        cellH = static_cast<float>(maskEntry->cellHeight);
        const float safeRange = std::max(maskEntry->pxRange, 1.0f);
        const float padTexels = std::ceil(safeRange) + 1.0f;
        const float usableW = std::max(2.0f, cellW - 2.0f * padTexels);
        const float usableH = std::max(2.0f, cellH - 2.0f * padTexels);
        const float uniformScale = std::min(
            usableW / std::max(1e-3f, maskEntry->boundsW),
            usableH / std::max(1e-3f, maskEntry->boundsH));
        scaledW = std::max(1e-3f, maskEntry->boundsW * uniformScale);
        scaledH = std::max(1e-3f, maskEntry->boundsH * uniformScale);
        cellBoxX0 = (cellW - scaledW) * 0.5f;
        cellBoxY0 = (cellH - scaledH) * 0.5f;
        cellRenderedW = mr.width * (cellW / scaledW);
        cellRenderedH = mr.height * (cellH / scaledH);
    }
    (void)screenScale;
    bool anyVisible = false;
    for (int py = my0; py < my1; ++py) {
        const float relY = ((static_cast<float>(py) + 0.5f) - mr.y) / std::max(1.0f, mr.height);
        const float normY = (cellBoxY0 + relY * scaledH) / cellH;
        const float* blurRow = bgLayer.premulRgba.data() +
            static_cast<std::size_t>(py - readY) * static_cast<std::size_t>(readW) * 4u;
        std::uint8_t* dstRow = outPremulRgba8.data() +
            static_cast<std::size_t>(py - my0) * static_cast<std::size_t>(pw) * 4u;
        for (int px = mx0; px < mx1; ++px) {
            const float relX = ((static_cast<float>(px) + 0.5f) - mr.x) / std::max(1.0f, mr.width);
            const float normX = (cellBoxX0 + relX * scaledW) / cellW;
            float cov = 1.0f;
            if (maskEntry) {
                cov = EvaluateMtsdfCoverage(*maskEntry, normX, normY, cellRenderedW, cellRenderedH, false);
            }
            const float effMask = std::clamp(cov * span.maskOpacity, 0.0f, 1.0f);
            if (effMask <= 0.002f) {
                continue;
            }
            const float* bp = blurRow + static_cast<std::size_t>(px - readX) * 4u;
            const float pa = std::clamp(bp[3] * effMask, 0.0f, 1.0f);
            if (pa <= 0.001f) {
                continue;
            }
            anyVisible = true;
            std::uint8_t* dp = dstRow + static_cast<std::size_t>(px - mx0) * 4u;
            dp[0] = static_cast<std::uint8_t>(std::clamp(bp[0] * effMask * 255.0f + 0.5f, 0.0f, 255.0f));
            dp[1] = static_cast<std::uint8_t>(std::clamp(bp[1] * effMask * 255.0f + 0.5f, 0.0f, 255.0f));
            dp[2] = static_cast<std::uint8_t>(std::clamp(bp[2] * effMask * 255.0f + 0.5f, 0.0f, 255.0f));
            dp[3] = static_cast<std::uint8_t>(std::clamp(pa * 255.0f + 0.5f, 0.0f, 255.0f));
        }
    }
    if (!anyVisible) {
        return false;
    }
    outPatchX = mx0;
    outPatchY = my0;
    outPatchW = pw;
    outPatchH = ph;
    return true;
}

void ZeroCopyGpuStream::RasterizeTreeDirtyRectsToRgba8(
    const PagedRenderTree& tree,
    std::uint32_t viewportWidth,
    std::uint32_t viewportHeight,
    const std::vector<Rect>& dirtyRects,
    std::vector<std::uint8_t>& ioRgba8,
    Color clearColor,
    SoftwareDirtyRectStats* outStats) {
    SoftwareDirtyRectStats stats{};
    if (viewportWidth == 0u || viewportHeight == 0u) {
        ioRgba8.clear();
        if (outStats) *outStats = stats;
        return;
    }

    const std::size_t pixelCount =
        static_cast<std::size_t>(viewportWidth) * static_cast<std::size_t>(viewportHeight);
    constexpr std::uint32_t kTileSize = 64u;
    const std::uint32_t tilesX = (viewportWidth + kTileSize - 1u) / kTileSize;
    const std::uint32_t tilesY = (viewportHeight + kTileSize - 1u) / kTileSize;
    const std::uint32_t totalTiles = tilesX * tilesY;
    stats.totalTiles = totalTiles;

    const bool canPartial =
        (ioRgba8.size() == pixelCount * 4u) &&
        tree.BlurLayerSpans().empty() &&
        totalTiles > 0u;
    if (!canPartial) {
        stats.dirtyTiles = totalTiles;
        stats.mergedStrips = (totalTiles > 0u) ? 1u : 0u;
        stats.fellBackToFullFrame = true;
        RasterizeTreeToRgba8(tree, viewportWidth, viewportHeight, ioRgba8, clearColor);
        if (outStats) *outStats = stats;
        return;
    }

    if (dirtyRects.empty()) {
        if (outStats) *outStats = stats;
        return;
    }

    thread_local std::vector<std::uint8_t> tlDirtyTiles;
    tlDirtyTiles.assign(totalTiles, 0u);
    const int vpW = static_cast<int>(std::min<std::uint32_t>(viewportWidth, 0x3FFFFFFFu));
    const int vpH = static_cast<int>(std::min<std::uint32_t>(viewportHeight, 0x3FFFFFFFu));

    std::uint32_t dirtyTileCount = 0u;
    for (const Rect& dr : dirtyRects) {
        if (!std::isfinite(dr.x) || !std::isfinite(dr.y) ||
            !std::isfinite(dr.width) || !std::isfinite(dr.height) ||
            dr.width <= 0.0f || dr.height <= 0.0f) {
            continue;
        }
        const int rx0 = SafeFloorToInt(dr.x - 1.0f, 0, vpW);
        const int ry0 = SafeFloorToInt(dr.y - 1.0f, 0, vpH);
        const int rx1 = SafeCeilToInt(dr.x + dr.width + 1.0f, 0, vpW);
        const int ry1 = SafeCeilToInt(dr.y + dr.height + 1.0f, 0, vpH);
        if (rx1 <= rx0 || ry1 <= ry0) {
            continue;
        }
        const std::uint32_t tx0 = static_cast<std::uint32_t>(rx0) / kTileSize;
        const std::uint32_t tx1 = static_cast<std::uint32_t>(rx1 - 1) / kTileSize;
        const std::uint32_t ty0 = static_cast<std::uint32_t>(ry0) / kTileSize;
        const std::uint32_t ty1 = static_cast<std::uint32_t>(ry1 - 1) / kTileSize;
        for (std::uint32_t ty = ty0; ty <= ty1 && ty < tilesY; ++ty) {
            const std::size_t rowBase = static_cast<std::size_t>(ty) * tilesX;
            for (std::uint32_t tx = tx0; tx <= tx1 && tx < tilesX; ++tx) {
                if (tlDirtyTiles[rowBase + tx] == 0u) {
                    tlDirtyTiles[rowBase + tx] = 1u;
                    ++dirtyTileCount;
                }
            }
        }
    }

    stats.dirtyTiles = dirtyTileCount;
    if (dirtyTileCount == 0u) {
        if (outStats) *outStats = stats;
        return;
    }

    if (static_cast<double>(dirtyTileCount) > 0.65 * static_cast<double>(totalTiles)) {
        stats.mergedStrips = 1u;
        stats.fellBackToFullFrame = true;
        RasterizeTreeToRgba8(tree, viewportWidth, viewportHeight, ioRgba8, clearColor);
        if (outStats) *outStats = stats;
        return;
    }

    ScopedBandTexCacheGuard rootTexGuard;

    const std::uint32_t clearPacked = PackColorRgba8(clearColor, 1.0f);
    const std::uint32_t nNodes = static_cast<std::uint32_t>(tree.NodeCount());
    const std::size_t totalPages = tree.PageCount();
    struct NodePxBounds {
        int x0 = 1;
        int y0 = 1;
        int x1 = 0;
        int y1 = 0;
    };
    thread_local std::vector<NodePxBounds> tlNodeBounds;
    tlNodeBounds.resize(nNodes);
    for (std::uint32_t ni = 0u; ni < nNodes; ++ni) {
        const RenderNodeDescriptor* nd = tree.GetNode(ni);
        if (!nd || nd->pageIndex >= totalPages || !std::isfinite(nd->opacity) || nd->opacity <= 0.001f) {
            tlNodeBounds[ni] = NodePxBounds{};
            continue;
        }
        const CommandPage& page = tree.GetPage(nd->pageIndex);
        const bool clipOn = (page.header.flags & CommandPageFlag_ScissorClipActive) != 0u;
        if (clipOn &&
            (!std::isfinite(page.header.clipX) || !std::isfinite(page.header.clipY) ||
             !std::isfinite(page.header.clipW) || !std::isfinite(page.header.clipH))) {
            tlNodeBounds[ni] = NodePxBounds{};
            continue;
        }
        const int cMinX = clipOn ? SafeCeilToInt(page.header.clipX - 0.5f, 0, vpW) : 0;
        const int cMinY = clipOn ? SafeCeilToInt(page.header.clipY - 0.5f, 0, vpH) : 0;
        const int cMaxX = clipOn
            ? SafeCeilToInt(page.header.clipX + std::max(0.0f, page.header.clipW) - 0.5f, 0, vpW)
            : vpW;
        const int cMaxY = clipOn
            ? SafeCeilToInt(page.header.clipY + std::max(0.0f, page.header.clipH) - 0.5f, 0, vpH)
            : vpH;
        if (cMaxX <= cMinX || cMaxY <= cMinY) {
            tlNodeBounds[ni] = NodePxBounds{};
            continue;
        }
        const std::size_t quadCount = nd->vertexCount / 4u;
        int bX0 = vpW, bY0 = vpH, bX1 = 0, bY1 = 0;
        for (std::size_t q = 0u; q < quadCount; ++q) {
            const std::size_t vIdx = nd->vertexOffsetInPage + q * 4u;
            if (vIdx + 4u > kVerticesPerCommandPage) break;
            const AeroVertex& v0 = page.vertices[vIdx + 0u];
            const AeroVertex& v1 = page.vertices[vIdx + 1u];
            const AeroVertex& v2 = page.vertices[vIdx + 2u];
            const AeroVertex& v3 = page.vertices[vIdx + 3u];
            if (((v0.rgba | v1.rgba | v2.rgba | v3.rgba) & 0xFF000000u) == 0u) continue;
            if (!std::isfinite(v0.x) || !std::isfinite(v0.y) ||
                !std::isfinite(v1.x) || !std::isfinite(v1.y) ||
                !std::isfinite(v2.x) || !std::isfinite(v2.y) ||
                !std::isfinite(v3.x) || !std::isfinite(v3.y)) continue;
            const float qMinX = std::min(std::min(v0.x, v1.x), std::min(v2.x, v3.x));
            const float qMaxX = std::max(std::max(v0.x, v1.x), std::max(v2.x, v3.x));
            const float qMinY = std::min(std::min(v0.y, v1.y), std::min(v2.y, v3.y));
            const float qMaxY = std::max(std::max(v0.y, v1.y), std::max(v2.y, v3.y));
            bX0 = std::min(bX0, SafeFloorToInt(qMinX, cMinX, cMaxX));
            bX1 = std::max(bX1, SafeCeilToInt(qMaxX, cMinX, cMaxX));
            bY0 = std::min(bY0, SafeFloorToInt(qMinY, cMinY, cMaxY));
            bY1 = std::max(bY1, SafeCeilToInt(qMaxY, cMinY, cMaxY));
        }
        tlNodeBounds[ni] = NodePxBounds{bX0, bY0, bX1, bY1};
    }

    const int prevX0 = gRasterBandX0;
    const int prevX1 = gRasterBandX1;
    const int prevY0 = gRasterBandY0;
    const int prevY1 = gRasterBandY1;
    std::uint8_t* rawOut = ioRgba8.data();
    const std::size_t rowStrideBytes = static_cast<std::size_t>(viewportWidth) * 4u;

    for (std::uint32_t ty = 0u; ty < tilesY; ++ty) {
        const std::size_t rowBase = static_cast<std::size_t>(ty) * tilesX;
        const int sy0 = static_cast<int>(ty * kTileSize);
        const int sy1 = std::min(vpH, static_cast<int>((ty + 1u) * kTileSize));
        std::uint32_t tx = 0u;
        while (tx < tilesX) {
            if (tlDirtyTiles[rowBase + tx] == 0u) {
                ++tx;
                continue;
            }
            const std::uint32_t txStart = tx;
            while (tx < tilesX && tlDirtyTiles[rowBase + tx] != 0u) {
                ++tx;
            }
            const int sx0 = static_cast<int>(txStart * kTileSize);
            const int sx1 = std::min(vpW, static_cast<int>(tx * kTileSize));
            if (sx1 <= sx0 || sy1 <= sy0) {
                continue;
            }
            stats.mergedStrips += 1u;

            const std::size_t stripCols = static_cast<std::size_t>(sx1 - sx0);
            for (int py = sy0; py < sy1; ++py) {
                auto* dst32 = reinterpret_cast<std::uint32_t*>(
                    rawOut + static_cast<std::size_t>(py) * rowStrideBytes + static_cast<std::size_t>(sx0) * 4u);
                std::fill_n(dst32, stripCols, clearPacked);
            }

            gRasterBandX0 = sx0;
            gRasterBandX1 = sx1;
            gRasterBandY0 = sy0;
            gRasterBandY1 = sy1;
            for (std::uint32_t ni = 0u; ni < nNodes; ++ni) {
                const NodePxBounds& nb = tlNodeBounds[ni];
                if (nb.x1 > sx0 && nb.x0 < sx1 && nb.y1 > sy0 && nb.y0 < sy1) {
                    RasterizeSingleNode(tree, ni, viewportWidth, viewportHeight, rawOut, nullptr);
                }
            }
        }
    }

    gRasterBandX0 = prevX0;
    gRasterBandX1 = prevX1;
    gRasterBandY0 = prevY0;
    gRasterBandY1 = prevY1;
    if (outStats) *outStats = stats;
}

} // namespace aero
