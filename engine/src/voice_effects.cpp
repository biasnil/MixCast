// MixCast engine - fun voice effects for the mic (classic DSP).
#include "voice_effects.h"

#include <algorithm>
#include <cmath>

namespace mixcast {

namespace {
constexpr float kPi = 3.14159265358979f;
constexpr float kFs = 48000.0f;
constexpr float kFadeStep = 1.0f / (0.030f * kFs);   // 30 ms crossfade

VoicePolish::Biquad HighPass(float hz, float q)
{
    const float w = 2.0f * kPi * hz / kFs, c = std::cos(w), al = std::sin(w) / (2.0f * q);
    const float a0 = 1.0f + al;
    VoicePolish::Biquad b;
    b.b0 = (1.0f + c) / 2.0f / a0;
    b.b1 = -(1.0f + c) / a0;
    b.b2 = b.b0;
    b.a1 = -2.0f * c / a0;
    b.a2 = (1.0f - al) / a0;
    return b;
}
} // namespace

const char* VoiceFxName(int fx)
{
    static const char* kNames[FxCount] = { "No effect", "Deep", "Chipmunk", "Robot", "Radio", "Echo", "Reverb" };
    return (fx >= 0 && fx < FxCount) ? kNames[fx] : kNames[0];
}

VoiceEffects::VoiceEffects()
{
    hp1_ = hp2_ = HighPass(500.0f, 0.7071f);
    lp1_ = lp2_ = VoicePolish::Biquad::LowPass(3000.0f, 0.7071f);

    echo_.assign(static_cast<size_t>(0.320f * kFs), 0.0f);

    // Freeverb's comb and all-pass lengths, scaled from 44.1 to 48 kHz.
    const int combLen[4] = { 1215, 1293, 1390, 1476 };
    const int apLen[2]   = { 605, 480 };
    for (int i = 0; i < 4; i++) combs_[i].buf.assign(combLen[i], 0.0f);
    for (int i = 0; i < 2; i++) allpasses_[i].buf.assign(apLen[i], 0.0f);
}

float VoiceEffects::Process(float x, int fx)
{
    fx = std::clamp(fx, 0, FxCount - 1);
    if (fx != current_)
    {
        previous_ = current_;
        current_ = fx;
        fade_ = 0.0f;
    }

    if (fade_ >= 1.0f)
        return current_ == FxNone ? x : Run(x, current_);

    // Crossfade the old effect out and the new one in (both keep running).
    fade_ = std::min(1.0f, fade_ + kFadeStep);
    const float a = previous_ == FxNone ? x : Run(x, previous_);
    const float b = current_ == FxNone ? x : Run(x, current_);
    return a + (b - a) * fade_;
}

float VoiceEffects::Run(float x, int fx)
{
    switch (fx)
    {
    case FxDeep:     return Pitch(x, 0.749f);   // -5 semitones
    case FxChipmunk: return Pitch(x, 1.498f);   // +7 semitones
    case FxRobot:    return Robot(x);
    case FxRadio:    return Radio(x);
    case FxEcho:     return Echo(x);
    case FxReverb:   return Reverb(x);
    default:         return x;
    }
}

// Two read heads sweep the delay line at `ratio` x speed, half a grain apart;
// each fades in and out with a triangle window, and the two always sum to one.
float VoiceEffects::Pitch(float x, float ratio)
{
    pitchBuf_[pitchPos_] = x;

    head_ += 1.0f - ratio;                       // how far behind "now" head 1 reads
    while (head_ < 0.0f)    head_ += kGrain;
    while (head_ >= kGrain) head_ -= kGrain;

    auto read = [this](float delay) {
        const float pos = static_cast<float>(pitchPos_) - delay;
        const float fl = std::floor(pos);
        const float fr = pos - fl;
        const int   i  = static_cast<int>(fl);
        const float a = pitchBuf_[i & (kPitchRing - 1)], b = pitchBuf_[(i + 1) & (kPitchRing - 1)];
        return a + (b - a) * fr;
    };
    const float d1 = head_ + 1.0f;
    float d2 = head_ + kGrain * 0.5f;
    if (d2 >= kGrain) d2 -= kGrain;
    d2 += 1.0f;

    const float w1 = 1.0f - std::fabs(2.0f * head_ / kGrain - 1.0f);   // 0 at the ends, 1 mid-grain
    const float y = read(d1) * w1 + read(d2) * (1.0f - w1);

    pitchPos_ = (pitchPos_ + 1) & (kPitchRing - 1);
    return y;
}

// Comb at 110 Hz (436 samples) gives the buzzy monotone; a touch of 50 Hz
// ring modulation adds the metal.
float VoiceEffects::Robot(float x)
{
    constexpr int kPeriod = 436;
    const int back = (combPos_ - kPeriod + static_cast<int>(comb_.size())) % static_cast<int>(comb_.size());
    const float y = x + 0.65f * comb_[back];
    comb_[combPos_] = y;
    combPos_ = (combPos_ + 1) % static_cast<int>(comb_.size());

    phase_ += 2.0f * kPi * 50.0f / kFs;
    if (phase_ > 2.0f * kPi) phase_ -= 2.0f * kPi;
    return 1.1f * y * (0.6f + 0.4f * std::sin(phase_));   // level matched to the dry voice
}

float VoiceEffects::Radio(float x)
{
    const float band = lp2_.Run(lp1_.Run(hp2_.Run(hp1_.Run(x))));
    return 0.35f * std::tanh(4.0f * band);   // overdriven and squashed like a radio; level matched
}

float VoiceEffects::Echo(float x)
{
    const float delayed = echo_[echoPos_];
    echoLp_ += (delayed - echoLp_) * 0.45f;            // each repeat a little darker
    echo_[echoPos_] = x + 0.38f * echoLp_;
    echoPos_ = (echoPos_ + 1) % static_cast<int>(echo_.size());
    return x + 0.35f * delayed;
}

float VoiceEffects::Reverb(float x)
{
    constexpr float kFeedback = 0.80f, kDamp = 0.25f, kWet = 0.30f;
    const float in = x * 0.25f;
    float sum = 0.0f;
    for (auto& c : combs_)
    {
        const float out = c.buf[c.pos];
        c.store = out * (1.0f - kDamp) + c.store * kDamp;
        c.buf[c.pos] = in + c.store * kFeedback;
        c.pos = (c.pos + 1) % static_cast<int>(c.buf.size());
        sum += out;
    }
    for (auto& a : allpasses_)
    {
        const float bufOut = a.buf[a.pos];
        const float out = bufOut - sum;
        a.buf[a.pos] = sum + bufOut * 0.5f;
        a.pos = (a.pos + 1) % static_cast<int>(a.buf.size());
        sum = out;
    }
    return x + kWet * sum;
}

} // namespace mixcast
