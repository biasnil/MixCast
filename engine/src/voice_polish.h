// MixCast engine - "radio voice" polish for the mic (classic DSP, no AI).
//
// Runs after the clean-up chain (rumble filter -> noise suppressor -> gate),
// mono at 48 kHz, in the order broadcast chains use:
//   1. De-esser     split at 4.5 kHz; the top band is turned down only while
//                   it is louder than the rest of the voice ("s", "sh", "t")
//   2. Voice EQ     -2.5 dB at 250 Hz (mud), +3 dB at 4 kHz (presence),
//                   +2 dB shelf above 10 kHz (air)
//   3. Compressor   3:1 above -24 dBFS, 6 dB soft knee, 5 ms / 150 ms,
//                   make-up gain so normal speech (-18 dBFS) keeps its level
//   4. Limiter      look-ahead peak limiter, ceiling -1 dBFS
//
// Every stage keeps running while switched off, so toggling never clicks.
// Added latency: 63 samples = 1.3 ms (the limiter's look-ahead), always.
#pragma once

#include <array>

namespace mixcast {

class VoicePolish
{
public:
    VoicePolish();

    float Process(float x, bool deEss, bool eq, bool compress, bool limit);

    // Current gain reduction in dB (<= 0), for meters.
    float DeEssDb() const    { return deEssDb_; }
    float CompressDb() const { return -compGrDb_; }
    float LimitDb() const;

    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;
        float Run(float x)
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        static Biquad LowPass(float hz, float q);   // RBJ cookbook, 48 kHz
    };

    static constexpr int kLookahead = 64;    // limiter window; delay is kLookahead - 1

private:
    float DeEss(float x);
    float Compress(float x);
    float Limit(float x, bool on);

    // De-esser.
    Biquad split_;
    float  hiEnv_ = 0.0f, loEnv_ = 0.0f;
    float  deEssGain_ = 1.0f;
    float  deEssDb_ = 0.0f;

    // EQ.
    Biquad mud_, presence_, air_;

    // Compressor (log-domain, smooth branching detector).
    float compGrDb_ = 0.0f;
    float makeupDb_ = 0.0f;

    // Limiter.
    std::array<float, kLookahead> delay_{};
    std::array<float, kLookahead> need_{};     // required gain per sample
    std::array<float, kLookahead> held_{};     // min-hold output (box filter input)
    int    lpos_ = 0;
    double boxSum_ = kLookahead;
    float  limGain_ = 1.0f;
};

} // namespace mixcast
