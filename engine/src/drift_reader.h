// MixCast engine - drift-compensating reader.
//
// Every capture device (your mic, the app loopback) runs on its own clock,
// and so does the MixCast driver. Over minutes they drift apart by a small
// fraction of a percent, which would slowly fill or drain the ring buffers.
//
// DriftReader sits on the consumer side of a StereoRing and resamples by a
// ratio very close to 1.0 (cubic Hermite interpolation). A gentle controller
// nudges the ratio so the ring stays near a target fill level. The pitch
// change is at most +/-0.4% (~7 cents) and only while correcting.
#pragma once

#include "ring_buffer.h"

#include <algorithm>
#include <atomic>
#include <cstdint>

namespace mixcast {

class DriftReader
{
public:
    DriftReader(StereoRing& ring, size_t targetFrames, size_t maxFrames)
        : ring_(ring), target_(targetFrames), max_(maxFrames) {}

    // Fills out[n * 2] with audio (or silence while priming / on underrun).
    void Pull(float* out, size_t n)
    {
        size_t avail = ring_.Available();

        if (priming_)
        {
            if (avail < target_)
            {
                std::fill(out, out + n * 2, 0.0f);
                return;
            }
            priming_ = false;
            std::fill(&hist_[0][0], &hist_[0][0] + 8, 0.0f);
            pos_   = 0.0;
            ratio_ = 1.0;
        }

        // Latency guard: never let this source lag more than max_.
        if (avail > max_)
        {
            ring_.Skip(avail - target_);
            avail = target_;
            trims.fetch_add(1, std::memory_order_relaxed);
        }

        // Controller: fuller than target -> consume slightly faster.
        const double err  = (static_cast<double>(avail) - static_cast<double>(target_)) / static_cast<double>(target_);
        const double want = 1.0 + std::clamp(err * 0.002, -0.004, 0.004);
        ratio_ += (want - ratio_) * 0.05;

        for (size_t i = 0; i < n; i++)
        {
            while (pos_ >= 1.0)
            {
                for (int k = 0; k < 3; k++) { hist_[k][0] = hist_[k + 1][0]; hist_[k][1] = hist_[k + 1][1]; }
                if (ring_.Read(hist_[3], 1) == 0)
                {
                    // Underrun: silence for the rest of this block, then re-prime.
                    std::fill(out + i * 2, out + n * 2, 0.0f);
                    priming_ = true;
                    underruns.fetch_add(1, std::memory_order_relaxed);
                    return;
                }
                pos_ -= 1.0;
            }

            const float t = static_cast<float>(pos_);
            out[i * 2 + 0] = Hermite(hist_[0][0], hist_[1][0], hist_[2][0], hist_[3][0], t);
            out[i * 2 + 1] = Hermite(hist_[0][1], hist_[1][1], hist_[2][1], hist_[3][1], t);
            pos_ += ratio_;
        }

        ratioPublished.store(ratio_, std::memory_order_relaxed);
    }

    bool   Priming() const { return priming_; }

    // Stats (read from UI thread).
    std::atomic<uint32_t> underruns{0};
    std::atomic<uint32_t> trims{0};
    std::atomic<double>   ratioPublished{1.0};

private:
    // 4-point, 3rd-order Hermite; interpolates between y1 and y2.
    static float Hermite(float y0, float y1, float y2, float y3, float t)
    {
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

    StereoRing& ring_;
    size_t      target_;
    size_t      max_;
    bool        priming_ = true;
    float       hist_[4][2] = {};
    double      pos_   = 0.0;
    double      ratio_ = 1.0;
};

} // namespace mixcast
