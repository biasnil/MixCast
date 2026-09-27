// MixCast engine - lock-free single-producer / single-consumer stereo ring.
// Producer: a capture thread. Consumer: the render (mixer) thread.
#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <vector>

namespace mixcast {

class StereoRing
{
public:
    explicit StereoRing(size_t capacityFrames)
    {
        size_t cap = 1;
        while (cap < capacityFrames) cap <<= 1;   // power of two -> cheap wrap
        cap_  = cap;
        mask_ = cap - 1;
        data_.assign(cap * 2, 0.0f);
    }

    // Producer side. Returns frames actually written (rest dropped when full).
    size_t Write(const float* frames, size_t n)
    {
        const size_t w = w_.load(std::memory_order_relaxed);
        const size_t r = r_.load(std::memory_order_acquire);
        n = std::min(n, cap_ - (w - r));

        const size_t start = w & mask_;
        const size_t first = std::min(n, cap_ - start);
        std::memcpy(&data_[start * 2], frames, first * 2 * sizeof(float));
        std::memcpy(&data_[0], frames + first * 2, (n - first) * 2 * sizeof(float));

        w_.store(w + n, std::memory_order_release);
        return n;
    }

    // Consumer side. Returns frames actually read.
    size_t Read(float* out, size_t n)
    {
        const size_t r = r_.load(std::memory_order_relaxed);
        const size_t w = w_.load(std::memory_order_acquire);
        n = std::min(n, w - r);

        const size_t start = r & mask_;
        const size_t first = std::min(n, cap_ - start);
        std::memcpy(out, &data_[start * 2], first * 2 * sizeof(float));
        std::memcpy(out + first * 2, &data_[0], (n - first) * 2 * sizeof(float));

        r_.store(r + n, std::memory_order_release);
        return n;
    }

    // Consumer side: discard up to n oldest frames.
    size_t Skip(size_t n)
    {
        const size_t r = r_.load(std::memory_order_relaxed);
        const size_t w = w_.load(std::memory_order_acquire);
        n = std::min(n, w - r);
        r_.store(r + n, std::memory_order_release);
        return n;
    }

    size_t Available() const
    {
        return w_.load(std::memory_order_acquire) - r_.load(std::memory_order_acquire);
    }

    size_t Capacity() const { return cap_; }

private:
    std::vector<float>  data_;
    size_t              cap_  = 0;
    size_t              mask_ = 0;
    std::atomic<size_t> w_{0};
    std::atomic<size_t> r_{0};
};

} // namespace mixcast
