// MixCast engine - offline sample-rate conversion (file loading only, not real time).
#pragma once

#include <cstdint>
#include <vector>

namespace mixcast {

// Windowed-sinc (32 taps, Blackman, table-driven). Interleaved stereo in and out.
// Levels are preserved exactly; aliasing stays more than 60 dB down.
std::vector<float> ResampleStereo(const std::vector<float>& in, uint32_t inRate, uint32_t outRate = 48000);

} // namespace mixcast
