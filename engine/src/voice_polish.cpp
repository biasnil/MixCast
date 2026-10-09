// MixCast engine - "radio voice" polish for the mic (classic DSP, no AI).
#include "voice_polish.h"

#include <algorithm>
#include <cmath>

namespace mixcast {

namespace {
constexpr float kPi = 3.14159265358979f;
constexpr float kFs = 48000.0f;

inline float Coef(float seconds) { return 1.0f - std::exp(-1.0f / (seconds * kFs)); }
inline float ToDb(float lin)     { return 20.0f * std::log10(lin + 1e-9f); }
inline float FromDb(float db)    { return std::pow(10.0f, db / 20.0f); }

// RBJ cookbook designs.
VoicePolish::Biquad LowPass(float f, float q)
{
    const float w = 2.0f * kPi * f / kFs, c = std::cos(w), al = std::sin(w) / (2.0f * q);
    const float a0 = 1.0f + al;
    VoicePolish::Biquad b;
    b.b0 = (1.0f - c) / 2.0f / a0;
    b.b1 = (1.0f - c) / a0;
    b.b2 = b.b0;
    b.a1 = -2.0f * c / a0;
    b.a2 = (1.0f - al) / a0;
    return b;
}

VoicePolish::Biquad Peak(float f, float q, float db)
{
    const float A = std::pow(10.0f, db / 40.0f);
    const float w = 2.0f * kPi * f / kFs, c = std::cos(w), al = std::sin(w) / (2.0f * q);
    const float a0 = 1.0f + al / A;
    VoicePolish::Biquad b;
    b.b0 = (1.0f + al * A) / a0;
    b.b1 = -2.0f * c / a0;
    b.b2 = (1.0f - al * A) / a0;
    b.a1 = b.b1;
    b.a2 = (1.0f - al / A) / a0;
    return b;
}

VoicePolish::Biquad HighShelf(float f, float db)
{
    const float A = std::pow(10.0f, db / 40.0f);
    const float w = 2.0f * kPi * f / kFs, c = std::cos(w);
    const float al = std::sin(w) / 2.0f * std::sqrt(2.0f);    // shelf slope S = 1
    const float sa = 2.0f * std::sqrt(A) * al;
    const float a0 = (A + 1.0f) - (A - 1.0f) * c + sa;
    VoicePolish::Biquad b;
    b.b0 = A * ((A + 1.0f) + (A - 1.0f) * c + sa) / a0;
    b.b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * c) / a0;
    b.b2 = A * ((A + 1.0f) + (A - 1.0f) * c - sa) / a0;
    b.a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * c) / a0;
    b.a2 = ((A + 1.0f) - (A - 1.0f) * c - sa) / a0;
    return b;
}

// De-esser.
constexpr float kSplitHz      = 4500.0f;
constexpr float kDeEssAboveDb = 0.0f;     // top band louder than the rest -> sibilance
constexpr float kDeEssFloorDb = -50.0f;   // ignore quiet hiss
constexpr float kDeEssRatio   = 4.0f;
constexpr float kDeEssMaxDb   = 12.0f;
const float kDetAttack  = Coef(0.0005f);
const float kDetRelease = Coef(0.050f);
const float kDeEssDown  = Coef(0.001f);
const float kDeEssUp    = Coef(0.040f);

// Compressor.
constexpr float kThreshDb  = -24.0f;
constexpr float kRatio     = 3.0f;
constexpr float kKneeDb    = 6.0f;
constexpr float kSpeechDb  = -18.0f;      // typical speech peak; make-up keeps it here
const float kCompAttack  = Coef(0.005f);
const float kCompRelease = Coef(0.150f);

// Limiter.
const float kCeiling     = FromDb(-1.0f);
const float kLimRelease  = Coef(0.060f);

// Static compressor curve: dB of gain reduction (>= 0) for an input level.
float CompGr(float inDb)
{
    const float over = inDb - kThreshDb;
    if (2.0f * over < -kKneeDb) return 0.0f;
    if (2.0f * over > kKneeDb)  return over * (1.0f - 1.0f / kRatio);
    const float k = over + kKneeDb / 2.0f;
    return (1.0f - 1.0f / kRatio) * k * k / (2.0f * kKneeDb);
}
} // namespace

VoicePolish::VoicePolish()
{
    split_    = LowPass(kSplitHz, 0.7071f);
    mud_      = Peak(250.0f, 1.0f, -2.5f);
    presence_ = Peak(4000.0f, 0.9f, 3.0f);
    air_      = HighShelf(10000.0f, 2.0f);
    makeupDb_ = CompGr(kSpeechDb);
    need_.fill(1.0f);
    held_.fill(1.0f);
}

float VoicePolish::Process(float x, bool deEss, bool eq, bool compress, bool limit)
{
    // Every stage runs either way, so its state is warm when switched on.
    const float d = DeEss(x);
    if (deEss) x = d;

    const float e = air_.Run(presence_.Run(mud_.Run(x)));
    if (eq) x = e;

    const float c = Compress(x);
    if (compress) x = c;

    return Limit(x, limit);
}

float VoicePolish::LimitDb() const { return ToDb(limGain_); }

float VoicePolish::DeEss(float x)
{
    // lo + hi == x exactly, so with no reduction the voice is untouched.
    const float lo = split_.Run(x);
    const float hi = x - lo;

    const float ah = std::fabs(hi), al = std::fabs(lo);
    hiEnv_ += (ah - hiEnv_) * (ah > hiEnv_ ? kDetAttack : kDetRelease);
    loEnv_ += (al - loEnv_) * (al > loEnv_ ? kDetAttack : kDetRelease);

    float targetDb = 0.0f;
    const float hiDb = ToDb(hiEnv_);
    const float excess = hiDb - ToDb(loEnv_) - kDeEssAboveDb;
    if (hiDb > kDeEssFloorDb && excess > 0.0f)
        targetDb = -std::min(kDeEssMaxDb, excess * (1.0f - 1.0f / kDeEssRatio));

    const float target = FromDb(targetDb);
    deEssGain_ += (target - deEssGain_) * (target < deEssGain_ ? kDeEssDown : kDeEssUp);
    deEssDb_ = ToDb(deEssGain_);
    return lo + hi * deEssGain_;
}

float VoicePolish::Compress(float x)
{
    const float gr = CompGr(ToDb(std::fabs(x)));
    compGrDb_ += (gr - compGrDb_) * (gr > compGrDb_ ? kCompAttack : kCompRelease);
    return x * FromDb(makeupDb_ - compGrDb_);
}

// Look-ahead limiter: the gain each sample needs is min-held over the window,
// then box-averaged over the same window, so the gain is fully down by the time
// the (delayed) peak comes out, and it ramps there smoothly instead of jumping.
float VoicePolish::Limit(float x, bool on)
{
    const float ax = std::fabs(x);
    need_[lpos_] = (on && ax > kCeiling) ? kCeiling / ax : 1.0f;

    float held = 1.0f;
    for (float n : need_) held = std::min(held, n);

    boxSum_ += held - held_[lpos_];
    held_[lpos_] = held;
    const float box = static_cast<float>(boxSum_ / kLookahead);

    limGain_ = (box < limGain_) ? box : limGain_ + (box - limGain_) * kLimRelease;

    // Oldest slot in the ring = the sample from kLookahead - 1 ago.
    const int next = (lpos_ + 1) % kLookahead;
    const float delayed = delay_[next];
    delay_[lpos_] = x;
    lpos_ = next;

    if (!on) return delayed;
    return std::clamp(delayed * limGain_, -kCeiling, kCeiling);   // float-rounding safety
}

} // namespace mixcast
