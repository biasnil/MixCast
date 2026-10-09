// MixCast engine - per-channel settings and the channel's own processing.
//
// Every channel (mic, each app, the soundboard) has, after its fader:
//   - tone        3-band EQ: bass shelf 120 Hz, mid peak 1 kHz, treble shelf
//                 6 kHz, each +-12 dB
//   - pan         balance, -1 (left) .. +1 (right); the far side fades out
//   - width       stereo width: 0 mono, 1 as it is, 2 extra wide (mid/side)
// and routing:
//   - A           to output A, the virtual cable (what Discord hears)
//   - B           to output B: your headphones or a second cable (e.g. OBS)
//   - solo        listen to this channel alone on B; A is never affected
//
// With everything at its neutral setting the audio passes bit-exact.
#pragma once

#include "voice_polish.h"   // VoicePolish::Biquad

#include <atomic>
#include <cstddef>

namespace mixcast {

// Per-source settings - safe to change from any thread while running.
struct SourceControls
{
    std::atomic<float> gainDb{0.0f};
    std::atomic<bool>  enabled{true};    // on/off switch (mute)
    std::atomic<bool>  duckable{true};   // apps only: lower this app while you talk
    std::atomic<float> peak{0.0f};       // meter, post-gain, linear

    std::atomic<bool>  sendA{true};      // to output A (the cable)
    std::atomic<bool>  sendB{false};     // to output B (headphones / second cable)
    std::atomic<bool>  solo{false};      // B plays only soloed channels while any is on

    std::atomic<float> bassDb{0.0f};     // tone, -12..+12 dB
    std::atomic<float> midDb{0.0f};
    std::atomic<float> trebleDb{0.0f};
    std::atomic<float> pan{0.0f};        // -1 left .. +1 right
    std::atomic<float> width{1.0f};      // 0 mono .. 1 normal .. 2 wide
};

class ChannelDsp
{
public:
    static constexpr float kBassHz = 120.0f, kMidHz = 1000.0f, kTrebleHz = 6000.0f;
    static constexpr float kToneRangeDb = 12.0f;

    // In place on interleaved stereo at 48 kHz.
    void Process(float* stereo, size_t frames, const SourceControls& c);

private:
    void Design(float bass, float mid, float treble);

    using Biquad = VoicePolish::Biquad;
    Biquad bass_[2], mid_[2], treble_[2];
    float  bassDb_ = 0.0f, midDb_ = 0.0f, trebleDb_ = 0.0f;
    bool   toneOn_ = false;

    // Last applied balance / width (ramped per block so moves never click).
    float  gl_ = 1.0f, gr_ = 1.0f, width_ = 1.0f;
};

} // namespace mixcast
