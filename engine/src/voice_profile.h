// MixCast engine - what the mic has learned about your voice (no AI).
//
// Plain running statistics, updated only from moments the processor is
// confident are you talking:
//   - pitch histogram     -> your personal pitch range (5th-95th percentile)
//   - speech level        -> gate threshold, auto level
//   - average spectrum    -> where your voice has energy, so the noise
//                            suppressor can be stricter everywhere else
//
// Learning is fast at first (a plain average) and then becomes a moving
// average over the last ~5 minutes of voiced speech, so the profile follows
// you as your voice changes (tired, ill, shouting in a game, a new mic).
#pragma once

#include <array>
#include <string>

namespace mixcast {

struct VoiceProfile
{
    static constexpr int   kBins       = 257;       // VoiceProcessor::kBins
    static constexpr int   kPitchBins  = 48;        // 60-480 Hz, 16 per octave
    static constexpr float kPitchMinHz = 60.0f;
    static constexpr float kTrainedSec = 20.0f;     // voiced speech needed before it's used
    static constexpr float kMemorySec  = 300.0f;    // then: moving average over this much
    static constexpr float kHopSec     = 256.0f / 48000.0f;

    std::array<float, kPitchBins> pitchHist{};      // share of voiced time at each pitch
    std::array<float, kBins>      spectrumDb{};     // average voice spectrum, relative dB
    float speechDb    = -30.0f;                     // typical speech level (hop RMS, dBFS)
    float voicedSec   = 0.0f;                       // voiced speech heard so far
    float spectrumSec = 0.0f;                       // speech hops in the spectrum average

    bool Trained() const { return voicedSec >= kTrainedSec; }

    // One voiced hop at pitch `hz` and level `levelDb`.
    void LearnPitch(float hz, float levelDb);
    // One speech hop: per-bin level relative to the hop's voice band.
    void LearnSpectrum(const float* relDb);

    // 5th and 95th percentile of your pitch, in Hz.
    void PitchRange(float& loHz, float& hiHz) const;

    std::string Serialize() const;
    static bool Deserialize(const std::string& bytes, VoiceProfile& out);
};

} // namespace mixcast
