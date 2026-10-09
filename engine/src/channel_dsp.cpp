// MixCast engine - per-channel tone, balance and stereo width.
#include "channel_dsp.h"

#include <algorithm>
#include <cmath>

namespace mixcast {

namespace {
constexpr float kPi = 3.14159265358979f;
constexpr float kFs = 48000.0f;

using Biquad = VoicePolish::Biquad;

// RBJ cookbook shelves (slope 1) and peak.
Biquad Shelf(float hz, float db, bool high)
{
    const float A = std::pow(10.0f, db / 40.0f);
    const float w = 2.0f * kPi * hz / kFs, c = std::cos(w);
    const float al = std::sin(w) / 2.0f * std::sqrt(2.0f);
    const float sa = 2.0f * std::sqrt(A) * al;
    const float s = high ? 1.0f : -1.0f;   // the two shelves differ only in these signs
    const float a0 = (A + 1.0f) - s * (A - 1.0f) * c + sa;
    Biquad b;
    b.b0 = A * ((A + 1.0f) + s * (A - 1.0f) * c + sa) / a0;
    b.b1 = -2.0f * s * A * ((A - 1.0f) + s * (A + 1.0f) * c) / a0;
    b.b2 = A * ((A + 1.0f) + s * (A - 1.0f) * c - sa) / a0;
    b.a1 = 2.0f * s * ((A - 1.0f) - s * (A + 1.0f) * c) / a0;
    b.a2 = ((A + 1.0f) - s * (A - 1.0f) * c - sa) / a0;
    return b;
}

Biquad Peak(float hz, float q, float db)
{
    const float A = std::pow(10.0f, db / 40.0f);
    const float w = 2.0f * kPi * hz / kFs, c = std::cos(w), al = std::sin(w) / (2.0f * q);
    const float a0 = 1.0f + al / A;
    Biquad b;
    b.b0 = (1.0f + al * A) / a0;
    b.b1 = -2.0f * c / a0;
    b.b2 = (1.0f - al * A) / a0;
    b.a1 = b.b1;
    b.a2 = (1.0f - al / A) / a0;
    return b;
}

// Keeps a filter's running state when its coefficients change.
void Retune(Biquad& f, const Biquad& design)
{
    const float z1 = f.z1, z2 = f.z2;
    f = design;
    f.z1 = z1;
    f.z2 = z2;
}
} // namespace

void ChannelDsp::Design(float bass, float mid, float treble)
{
    for (int ch = 0; ch < 2; ch++)
    {
        Retune(bass_[ch],   Shelf(kBassHz, bass, false));
        Retune(mid_[ch],    Peak(kMidHz, 0.7f, mid));
        Retune(treble_[ch], Shelf(kTrebleHz, treble, true));
    }
    bassDb_ = bass;
    midDb_ = mid;
    trebleDb_ = treble;
}

void ChannelDsp::Process(float* x, size_t frames, const SourceControls& c)
{
    // ---- Tone -----------------------------------------------------------------
    const float bass   = std::clamp(c.bassDb.load(std::memory_order_relaxed), -kToneRangeDb, kToneRangeDb);
    const float mid    = std::clamp(c.midDb.load(std::memory_order_relaxed), -kToneRangeDb, kToneRangeDb);
    const float treble = std::clamp(c.trebleDb.load(std::memory_order_relaxed), -kToneRangeDb, kToneRangeDb);
    const bool  flat   = std::fabs(bass) < 0.05f && std::fabs(mid) < 0.05f && std::fabs(treble) < 0.05f;
    if (bass != bassDb_ || mid != midDb_ || treble != trebleDb_) Design(bass, mid, treble);

    if (!flat)
    {
        if (!toneOn_)   // switching on from flat: start from a clean filter state
            for (int ch = 0; ch < 2; ch++) bass_[ch].z1 = bass_[ch].z2 = mid_[ch].z1 = mid_[ch].z2 = treble_[ch].z1 = treble_[ch].z2 = 0.0f;
        for (size_t f = 0; f < frames; f++)
            for (int ch = 0; ch < 2; ch++)
            {
                float& s = x[f * 2 + ch];
                s = treble_[ch].Run(mid_[ch].Run(bass_[ch].Run(s)));
            }
    }
    toneOn_ = !flat;

    // ---- Balance and width (ramped over the block) ------------------------------
    const float pan   = std::clamp(c.pan.load(std::memory_order_relaxed), -1.0f, 1.0f);
    const float width = std::clamp(c.width.load(std::memory_order_relaxed), 0.0f, 2.0f);
    const float gl = pan > 0.0f ? std::cos(pan * kPi * 0.5f) : 1.0f;    // right turns the left down
    const float gr = pan < 0.0f ? std::cos(-pan * kPi * 0.5f) : 1.0f;

    const bool neutralNow  = gl == 1.0f && gr == 1.0f && width == 1.0f;
    const bool neutralWas  = gl_ == 1.0f && gr_ == 1.0f && width_ == 1.0f;
    if (!(neutralNow && neutralWas) && frames > 0)
    {
        const float inv = 1.0f / static_cast<float>(frames);
        for (size_t f = 0; f < frames; f++)
        {
            const float t = (f + 1) * inv;
            const float w = width_ + (width - width_) * t;
            const float l = x[f * 2], r = x[f * 2 + 1];
            const float m = 0.5f * (l + r), s = 0.5f * (l - r) * w;
            x[f * 2]     = (m + s) * (gl_ + (gl - gl_) * t);
            x[f * 2 + 1] = (m - s) * (gr_ + (gr - gr_) * t);
        }
    }
    gl_ = gl;
    gr_ = gr;
    width_ = width;
}

} // namespace mixcast
