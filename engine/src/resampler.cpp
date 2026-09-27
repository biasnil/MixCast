// MixCast engine - offline sample-rate conversion.
#include "resampler.h"

#include <algorithm>
#include <cmath>

namespace mixcast {

// Windowed-sinc resampler (32 taps, Blackman window, table-driven).
std::vector<float> ResampleStereo(const std::vector<float>& in, uint32_t inRate, uint32_t outRate)
{
    const size_t inFrames = in.size() / 2;
    if (inRate == outRate || inFrames == 0) return in;

    constexpr int    kHalf = 16;          // taps each side
    constexpr int    kRes  = 512;         // table points per input sample
    const double     ratio = static_cast<double>(inRate) / outRate;   // input samples per output sample
    const double     fc    = std::min(1.0, 1.0 / ratio) * 0.94;           // cutoff, leaves a transition band

    // Kernel table over x in [-kHalf, kHalf].
    std::vector<float> table(2 * kHalf * kRes + 1);
    for (size_t i = 0; i < table.size(); i++)
    {
        const double x = static_cast<double>(i) / kRes - kHalf;
        const double s = (x == 0.0) ? 1.0 : std::sin(3.14159265358979 * fc * x) / (3.14159265358979 * fc * x);
        const double w = 0.42 + 0.5 * std::cos(3.14159265358979 * x / kHalf) + 0.08 * std::cos(2 * 3.14159265358979 * x / kHalf);
        table[i] = static_cast<float>(fc * s * w);
    }
    auto kernel = [&](double x) -> float {
        const double p = (x + kHalf) * kRes;
        const size_t i = static_cast<size_t>(p);
        if (i + 1 >= table.size()) return 0.0f;
        const float f = static_cast<float>(p - i);
        return table[i] + (table[i + 1] - table[i]) * f;
    };

    const size_t outFrames = static_cast<size_t>(std::floor(inFrames / ratio));
    std::vector<float> out(outFrames * 2);
    for (size_t m = 0; m < outFrames; m++)
    {
        const double t  = m * ratio;
        const long   k0 = static_cast<long>(std::floor(t));
        float l = 0.0f, r = 0.0f, wsum = 0.0f;
        for (long k = k0 - kHalf + 1; k <= k0 + kHalf; k++)
        {
            if (k < 0 || static_cast<size_t>(k) >= inFrames) continue;
            const float w = kernel(t - k);
            l += in[k * 2] * w;
            r += in[k * 2 + 1] * w;
            wsum += w;
        }
        const float norm = (wsum > 1e-6f) ? 1.0f / wsum : 0.0f;   // unity DC gain
        out[m * 2]     = l * norm;
        out[m * 2 + 1] = r * norm;
    }
    return out;
}


} // namespace mixcast
