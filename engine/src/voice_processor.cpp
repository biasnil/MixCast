// MixCast engine - mic clean-up (classic DSP, no AI, no third-party code).
#include "voice_processor.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace mixcast {

namespace {
constexpr float kPi = 3.14159265358979f;
constexpr float kFs = 48000.0f;

inline float Coef(float seconds) { return 1.0f - std::exp(-1.0f / (seconds * kFs)); }

const float kGateOpen  = Coef(0.001f);   // gate opens in ~1 ms (no clipped word starts)
const float kGateClose = Coef(0.050f);   // closes over ~50 ms (no chattering)
const float kEnvAttack = Coef(0.001f);
const float kEnvRelease= Coef(0.080f);

struct NoiseParams { float overSub; float floorGain; };
// overSub: how aggressively the tracked noise is removed.
// floorGain: the most a bin can be turned down (keeps a natural residue).
constexpr NoiseParams kNoise[4] = {
    { 1.0f, 1.00f },     // Off
    { 1.5f, 0.316f },    // Low:    up to -10 dB
    { 2.0f, 0.126f },    // Medium: up to -18 dB
    { 3.0f, 0.040f },    // High:   up to -28 dB
};

struct GateParams { float aboveFloorDb; float rangeDb; };
constexpr GateParams kGate[4] = {
    {  0.0f,   0.0f },   // Off
    { 10.0f, -12.0f },   // Gentle
    { 16.0f, -35.0f },   // Firm
    {  6.0f, -45.0f },   // Voice only (level must also be this far above the floor)
};

constexpr float kVoiceHoldSec = 0.25f;   // keep open through unvoiced consonants
constexpr float kVoicedR      = 0.60f;   // autocorrelation needed to call it a voice
} // namespace

VoiceProcessor::VoiceProcessor()
{
    // 80 Hz Butterworth high-pass (RBJ cookbook).
    const float w0 = 2.0f * kPi * 80.0f / kFs;
    const float cw = std::cos(w0);
    const float alpha = std::sin(w0) / (2.0f * 0.7071f);
    const float a0 = 1.0f + alpha;
    b0_ = (1.0f + cw) / 2.0f / a0;
    b1_ = -(1.0f + cw) / a0;
    b2_ = (1.0f + cw) / 2.0f / a0;
    a1_ = -2.0f * cw / a0;
    a2_ = (1.0f - alpha) / a0;

    // 900 Hz low-pass for the pitch detector.
    {
        const float lw = 2.0f * kPi * 900.0f / kFs;
        const float lc = std::cos(lw);
        const float la = std::sin(lw) / (2.0f * 0.7071f);
        const float l0 = 1.0f + la;
        lb0_ = (1.0f - lc) / 2.0f / l0;
        lb1_ = (1.0f - lc) / l0;
        lb2_ = lb0_;
        la1_ = -2.0f * lc / l0;
        la2_ = (1.0f - la) / l0;
    }

    // sqrt-Hann: analysis x synthesis = Hann, which sums to 1 at 50% overlap.
    for (int n = 0; n < kFft; n++)
        window_[n] = std::sqrt(0.5f * (1.0f - std::cos(2.0f * kPi * n / kFft)));

    for (int k = 0; k < kFft / 2; k++)
        twiddle_[k] = std::polar(1.0f, -2.0f * kPi * k / kFft);

    int bits = 0;
    while ((1 << bits) < kFft) bits++;
    for (int i = 0; i < kFft; i++)
    {
        int r = 0;
        for (int b = 0; b < bits; b++) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
        bitrev_[i] = r;
    }

    gain_.fill(1.0f);
    noise_.fill(1e-10f);
}

float VoiceProcessor::HighPass(float x)
{
    const float y = b0_ * x + z1_;
    z1_ = b1_ * x - a1_ * y + z2_;
    z2_ = b2_ * x - a2_ * y;
    return y;
}

// In-place iterative radix-2 FFT. Inverse is scaled by 1/N.
void VoiceProcessor::Fft(std::complex<float>* a, bool inverse) const
{
    for (int i = 0; i < kFft; i++)
        if (i < bitrev_[i]) std::swap(a[i], a[bitrev_[i]]);

    for (int len = 2; len <= kFft; len <<= 1)
    {
        const int half = len / 2;
        const int step = kFft / len;
        for (int i = 0; i < kFft; i += len)
        {
            for (int j = 0; j < half; j++)
            {
                std::complex<float> w = twiddle_[j * step];
                if (inverse) w = std::conj(w);
                const std::complex<float> u = a[i + j];
                const std::complex<float> v = a[i + j + half] * w;
                a[i + j]        = u + v;
                a[i + j + half] = u - v;
            }
        }
    }
    if (inverse)
        for (int i = 0; i < kFft; i++) a[i] /= static_cast<float>(kFft);
}

float VoiceProcessor::PitchLowPass(float x)
{
    for (int st = 0; st < 2; st++)
    {
        const float y = lb0_ * x + lz[st][0];
        lz[st][0] = lb1_ * x - la1_ * y + lz[st][1];
        lz[st][1] = lb2_ * x - la2_ * y;
        x = y;
    }
    return x;
}

// Normalised autocorrelation of the last ~36 ms (6 kHz) for 70-400 Hz pitch.
// Voiced speech scores 0.7-0.95; chewing, crunching, clicks and taps score low.
bool VoiceProcessor::Voiced() const
{
    constexpr int W = 128, minLag = 15, maxLag = 86, N = W + maxLag;
    float x[N];
    const int start = (ringPos_ - N + kPitchRing) % kPitchRing;
    float mean = 0.0f;
    for (int i = 0; i < N; i++) { x[i] = pitchRing_[(start + i) % kPitchRing]; mean += x[i]; }
    mean /= N;
    for (int i = 0; i < N; i++) x[i] -= mean;

    float e0 = 0.0f;
    for (int n = 0; n < W; n++) e0 += x[n] * x[n];
    if (e0 < 1e-9f) return false;

    float best = 0.0f;
    for (int lag = minLag; lag <= maxLag; lag++)
    {
        float num = 0.0f, el = 0.0f;
        for (int n = 0; n < W; n++) { num += x[n] * x[n + lag]; el += x[n + lag] * x[n + lag]; }
        const float r = num / std::sqrt(e0 * el + 1e-12f);
        best = std::max(best, r);
    }
    return best > kVoicedR;
}

void VoiceProcessor::Process(float* stereo, size_t frames, VoiceSettings& s)
{
    const bool rumble = s.rumbleFilter.load(std::memory_order_relaxed);
    const int  gate   = std::clamp(s.gateMode.load(std::memory_order_relaxed), 0, 3);
    const bool deEss  = s.deEsser.load(std::memory_order_relaxed);
    const bool eq     = s.voiceEq.load(std::memory_order_relaxed);
    const bool comp   = s.compressor.load(std::memory_order_relaxed);
    const bool limit  = s.limiter.load(std::memory_order_relaxed);

    if (gate != lastGate_)
    {
        delay_.fill(0.0f);     // no stale audio when switching into "Voice only"
        lastGate_ = gate;
    }

    for (size_t f = 0; f < frames; f++)
    {
        float x = 0.5f * (stereo[f * 2] + stereo[f * 2 + 1]);
        const float hp = HighPass(x);      // keep the filter warm even when off
        if (rumble) x = hp;

        inHop_[pos_] = x;

        // Cleaned voice (fans/hum already removed) feeds the pitch detector.
        const float o = outHop_[pos_];
        const float lp = PitchLowPass(o);
        if (++decimCount_ == kDecim)
        {
            decimCount_ = 0;
            pitchRing_[ringPos_] = lp;
            ringPos_ = (ringPos_ + 1) % kPitchRing;
        }

        // "Voice only" listens 25 ms ahead so word starts aren't clipped.
        float g = o;
        if (gate == GateVoice)
        {
            g = delay_[delayPos_];
            delay_[delayPos_] = o;
            delayPos_ = (delayPos_ + 1) % kLookahead;
        }
        const float y = polish_.Process(Gate(g, gate), deEss, eq, comp, limit);
        hopEnergy_ += static_cast<double>(o) * o;

        stereo[f * 2] = stereo[f * 2 + 1] = y;

        if (++pos_ == kHop)
        {
            pos_ = 0;
            ProcessHop(s);

            // Track the background level after suppression (for the gate).
            const float rmsDb = 10.0f * std::log10(static_cast<float>(hopEnergy_ / kHop) + 1e-12f);
            hopEnergy_ = 0.0;
            if (hops_ == 40)          floorDb_ = rmsDb;                             // first real reading
            else if (hops_ > 40)
            {
                if (rmsDb < floorDb_) floorDb_ = 0.5f * floorDb_ + 0.5f * rmsDb;    // falls fast
                else                  floorDb_ += 0.03f;                           // rises ~5.6 dB/s
            }
            floorDb_ = std::clamp(floorDb_, -90.0f, -30.0f);

            // A real voice stays pitched for tens of ms; crunches and clicks
            // only fake it for a hop or two. Require ~21 ms in a row.
            const bool pitched = hops_ > 40 && rmsDb > floorDb_ + kGate[GateVoice].aboveFloorDb && Voiced();
            voicedRun_ = pitched ? voicedRun_ + 1 : 0;
            const bool voiced = voicedRun_ >= 4;
            if (voiced) voiceHold_ = kVoiceHoldSec * kFs;
            s.voiceDetected.store(voiced, std::memory_order_relaxed);

            s.noiseFloorDb.store(floorDb_, std::memory_order_relaxed);
            s.gateOpen.store(gate == GateOff || gateGain_ > 0.5f, std::memory_order_relaxed);
            s.deEssDb.store(deEss ? polish_.DeEssDb() : 0.0f, std::memory_order_relaxed);
            s.compressDb.store(comp ? polish_.CompressDb() : 0.0f, std::memory_order_relaxed);
            s.limitDb.store(limit ? polish_.LimitDb() : 0.0f, std::memory_order_relaxed);
        }
    }
}

void VoiceProcessor::ProcessHop(VoiceSettings& s)
{
    // Slide the analysis frame by one hop.
    std::memmove(frame_.data(), frame_.data() + kHop, (kFft - kHop) * sizeof(float));
    std::memcpy(frame_.data() + (kFft - kHop), inHop_.data(), kHop * sizeof(float));

    for (int n = 0; n < kFft; n++) spec_[n] = std::complex<float>(frame_[n] * window_[n], 0.0f);
    Fft(spec_.data(), false);

    const int level = std::clamp(s.noiseLevel.load(std::memory_order_relaxed), 0, 3);
    const NoiseParams np = kNoise[level];

    // ---- Is someone talking? (voice band SNR against the noise estimate) ----
    const int lo = static_cast<int>(300.0f / (kFs / kFft));
    const int hi = static_cast<int>(3400.0f / (kFs / kFft));
    float snrSum = 0.0f;
    for (int k = lo; k <= hi; k++) snrSum += smoothPow_[k] / (noise_[k] + 1e-12f);
    const bool speech = hops_ > 25 && snrSum / (hi - lo + 1) > 3.0f;

    // ---- Track the noise spectrum -------------------------------------------
    // Follows the lower envelope of the smoothed power: drops quickly, creeps
    // up slowly (slower still while you talk), so steady noise like fans, AC,
    // PC hum and hiss is learned without eating your voice.
    const float rise = speech ? 1.0012f : 1.006f;   // ~1 dB/s vs ~5 dB/s
    for (int k = 0; k < kBins; k++)
    {
        const float p = std::norm(spec_[k]);
        smoothPow_[k] = (hops_ == 0) ? p : 0.6f * smoothPow_[k] + 0.4f * p;

        if (hops_ < 25)                               // first ~130 ms: learn fast
            noise_[k] = (hops_ == 0) ? smoothPow_[k] : 0.8f * noise_[k] + 0.2f * smoothPow_[k];
        else if (smoothPow_[k] < noise_[k])
            noise_[k] = 0.8f * noise_[k] + 0.2f * smoothPow_[k];
        else
            noise_[k] *= rise;
        noise_[k] = std::max(noise_[k], 1e-12f);
    }

    // ---- Wiener gains (decision-directed a-priori SNR) -----------------------
    std::array<float, kBins> g{};
    float inE = 0.0f, outE = 0.0f;
    for (int k = 0; k < kBins; k++)
    {
        const float p = std::norm(spec_[k]);
        if (level == NoiseOff)
        {
            g[k] = 1.0f;
            prevClean_[k] = p;
            continue;
        }
        const float n     = noise_[k] * np.overSub;
        const float post  = p / n;
        const float prior = 0.98f * prevClean_[k] / n + 0.02f * std::max(post - 1.0f, 0.0f);
        float gk = prior / (1.0f + prior);
        gk = std::max(gk, np.floorGain);
        prevClean_[k] = gk * gk * p;
        g[k] = gk;
    }

    for (int k = 0; k < kBins; k++)
    {
        // Smooth across neighbouring bins, then over time (fast up, slower down):
        // this is what keeps the residue from sounding "watery".
        const float gl = g[std::max(k - 1, 0)], gr = g[std::min(k + 1, kBins - 1)];
        const float gs = 0.25f * gl + 0.5f * g[k] + 0.25f * gr;
        gain_[k] = (gs > gain_[k]) ? gs : 0.65f * gain_[k] + 0.35f * gs;

        const float p = std::norm(spec_[k]);
        inE  += p;
        outE += gain_[k] * gain_[k] * p;
        spec_[k] *= gain_[k];
    }
    for (int k = 1; k < kFft / 2; k++) spec_[kFft - k] = std::conj(spec_[k]);

    Fft(spec_.data(), true);

    // Overlap-add; the first hop of the accumulator is now complete.
    for (int n = 0; n < kFft; n++) ola_[n] += spec_[n].real() * window_[n];
    std::memcpy(outHop_.data(), ola_.data(), kHop * sizeof(float));
    std::memmove(ola_.data(), ola_.data() + kHop, (kFft - kHop) * sizeof(float));
    std::fill(ola_.begin() + (kFft - kHop), ola_.end(), 0.0f);

    const float removed = (inE > 1e-9f) ? 10.0f * std::log10(std::max(outE, 1e-12f) / inE) : 0.0f;
    const float prevRemoved = s.removedDb.load(std::memory_order_relaxed);
    s.removedDb.store(0.9f * prevRemoved + 0.1f * removed, std::memory_order_relaxed);

    hops_++;
}

float VoiceProcessor::Gate(float x, int mode)
{
    const float ax = std::fabs(x);
    env_ += (ax - env_) * (ax > env_ ? kEnvAttack : kEnvRelease);

    float target = 1.0f;
    if (mode == GateVoice)
    {
        voiceHold_ = std::max(0.0f, voiceHold_ - 1.0f);
        target = (voiceHold_ > 0.0f) ? 1.0f : std::pow(10.0f, kGate[GateVoice].rangeDb / 20.0f);
    }
    else if (mode != GateOff)
    {
        const GateParams gp = kGate[mode];
        const float threshold = std::clamp(floorDb_ + gp.aboveFloorDb, -65.0f, -30.0f);
        const float envDb = 20.0f * std::log10(env_ + 1e-9f);

        if (envDb > threshold) holdLeft_ = 0.20f * kFs;          // stay open 200 ms after words
        else                   holdLeft_ = std::max(0.0f, holdLeft_ - 1.0f);

        target = (holdLeft_ > 0.0f) ? 1.0f : std::pow(10.0f, gp.rangeDb / 20.0f);
    }

    gateGain_ += (target - gateGain_) * (target > gateGain_ ? kGateOpen : kGateClose);
    return x * gateGain_;
}

} // namespace mixcast
