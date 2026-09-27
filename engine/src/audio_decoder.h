// MixCast engine - file decoding via Windows Media Foundation.
// Kept in its own file so Media Foundation headers never meet the WASAPI ones.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mixcast {

// Decodes the first audio stream of `path` to interleaved stereo float at the
// file's own sample rate. Mono is duplicated; extra channels are dropped.
// Needs COM initialised on the calling thread. Returns false + error text.
bool DecodeToStereoFloat(const std::wstring& path, double maxSeconds,
                         std::vector<float>& stereo, uint32_t& sampleRate, std::string& error);

// Same, then converted to 48 kHz (MixCast's working rate).
bool DecodeStereo48k(const std::wstring& path, double maxSeconds,
                     std::vector<float>& stereo48k, std::string& error);

// Starts Media Foundation once per process. False if unavailable.
bool EnsureMediaFoundation();

} // namespace mixcast
