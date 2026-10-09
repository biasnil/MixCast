// MixCast engine - what the mic has learned about your voice (no AI).
#include "voice_profile.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace mixcast {

namespace {
constexpr char     kMagic[4] = { 'M', 'X', 'V', 'P' };
constexpr uint32_t kVersion  = 2;   // 2 added the voice atoms; 1 still loads

// Plain average while young, then an exponential moving average.
float Weight(float seenSec)
{
    using P = VoiceProfile;
    return std::max(P::kHopSec / (seenSec + P::kHopSec), P::kHopSec / P::kMemorySec);
}
} // namespace

void VoiceProfile::LearnPitch(float hz, float levelDb)
{
    const float w = Weight(voicedSec);
    const int bin = std::clamp(static_cast<int>(16.0f * std::log2(hz / kPitchMinHz)), 0, kPitchBins - 1);
    for (float& h : pitchHist) h *= 1.0f - w;
    pitchHist[bin] += w;
    speechDb += (levelDb - speechDb) * w;
    voicedSec = std::min(voicedSec + kHopSec, 1e6f);
}

void VoiceProfile::LearnSpectrum(const float* relDb)
{
    const float w = Weight(spectrumSec);
    for (int k = 0; k < kBins; k++) spectrumDb[k] += (relDb[k] - spectrumDb[k]) * w;
    spectrumSec = std::min(spectrumSec + kHopSec, 1e6f);
}

void VoiceProfile::PitchRange(float& loHz, float& hiHz) const
{
    float total = 0.0f;
    for (float h : pitchHist) total += h;
    loHz = 70.0f;
    hiHz = 400.0f;
    if (total <= 0.0f) return;

    auto hzAt = [](int bin) { return kPitchMinHz * std::exp2((bin + 0.5f) / 16.0f); };
    float acc = 0.0f;
    bool haveLo = false;
    for (int i = 0; i < kPitchBins; i++)
    {
        acc += pitchHist[i] / total;
        if (!haveLo && acc >= 0.05f) { loHz = hzAt(i); haveLo = true; }
        if (acc >= 0.95f)            { hiHz = hzAt(i); break; }
    }
}

std::string VoiceProfile::Serialize() const
{
    std::string out(kMagic, 4);
    auto put = [&out](const void* p, size_t n) { out.append(static_cast<const char*>(p), n); };
    put(&kVersion, sizeof(kVersion));
    put(pitchHist.data(), sizeof(pitchHist));
    put(spectrumDb.data(), sizeof(spectrumDb));
    put(&speechDb, sizeof(speechDb));
    put(&voicedSec, sizeof(voicedSec));
    put(&spectrumSec, sizeof(spectrumSec));
    const uint32_t atoms = static_cast<uint32_t>(voiceAtoms.size());
    put(&atoms, sizeof(atoms));
    put(voiceAtoms.data(), atoms * sizeof(float));
    return out;
}

bool VoiceProfile::Deserialize(const std::string& bytes, VoiceProfile& out)
{
    constexpr size_t kV1Size = 4 + sizeof(uint32_t) + sizeof(pitchHist) + sizeof(spectrumDb) + 3 * sizeof(float);
    if (bytes.size() < kV1Size || std::memcmp(bytes.data(), kMagic, 4) != 0) return false;

    uint32_t version = 0;
    const char* p = bytes.data() + 4;
    const char* end = bytes.data() + bytes.size();
    auto get = [&p](void* dst, size_t n) { std::memcpy(dst, p, n); p += n; };
    get(&version, sizeof(version));
    if (version != 1 && version != kVersion) return false;
    if (version == 1 && bytes.size() != kV1Size) return false;

    VoiceProfile v;
    get(v.pitchHist.data(), sizeof(v.pitchHist));
    get(v.spectrumDb.data(), sizeof(v.spectrumDb));
    get(&v.speechDb, sizeof(v.speechDb));
    get(&v.voicedSec, sizeof(v.voicedSec));
    get(&v.spectrumSec, sizeof(v.spectrumSec));
    if (version >= 2)
    {
        uint32_t atoms = 0;
        if (end - p < static_cast<ptrdiff_t>(sizeof(atoms))) return false;
        get(&atoms, sizeof(atoms));
        if (atoms > 100000 || end - p != static_cast<ptrdiff_t>(atoms * sizeof(float))) return false;
        v.voiceAtoms.resize(atoms);
        get(v.voiceAtoms.data(), atoms * sizeof(float));
        for (float f : v.voiceAtoms) if (!std::isfinite(f) || f < 0.0f) return false;
    }

    // Reject anything damaged rather than steer the mic with it.
    auto ok = [](float f) { return std::isfinite(f); };
    for (float f : v.pitchHist)  if (!ok(f) || f < 0.0f) return false;
    for (float f : v.spectrumDb) if (!ok(f)) return false;
    if (!ok(v.speechDb) || !ok(v.voicedSec) || !ok(v.spectrumSec) || v.voicedSec < 0.0f || v.spectrumSec < 0.0f)
        return false;

    out = v;
    return true;
}

} // namespace mixcast
