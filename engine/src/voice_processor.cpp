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
constexpr int   kSpeakHoldHops = static_cast<int>(kVoiceHoldSec * kFs / VoiceProcessor::kHop);

// Learn my voice.
constexpr float kLearnR       = 0.75f;   // only learn from clearly pitched hops...
constexpr float kLearnAboveDb = 12.0f;   // ...well above the room
constexpr float kRangeBelow   = 1.26f;   // accept 4 semitones under your usual low
constexpr float kRangeAbove   = 1.41f;   // and 6 over your usual high (shouting)
constexpr float kLearnMargin  = 1.60f;   // learning follows you if your voice drifts
constexpr float kGateShare[4] = { 0.0f, 0.40f, 0.55f, 0.0f };   // Gentle/Firm: point between room and voice
constexpr int   kExchangeHops = 94;      // swap profile with the UI every ~0.5 s

// Auto level.
constexpr float kAutoTargetDb = -30.0f;  // learned speech level it aims for (~-26 dBFS RMS while talking)
const float kAutoGainCoef     = Coef(0.5f);

// Remove keyboard & clicks.
constexpr float kClickFloor = 0.1f;      // turns sounds that aren't you down by up to 20 dB
constexpr int   kClickHops  = 4;         // a click lasts ~20 ms: cut this long after it starts...
constexpr int   kClickFade  = 4;         // ...then let go over ~20 ms. Consonants last 50-150 ms
                                         // and sound much like keys, so they must get through.
constexpr float kOnsetJump  = 3.0f;      // "not you" energy this many times its recent level = a click

// Clean while I talk: weights of one and two periods back at full strength.
constexpr float kCombW1     = 0.4f;
constexpr float kCombW2     = 0.2f;
constexpr float kCombMinR   = 0.5f;      // pitch must be this trackable
constexpr float kCombFullSnr = 10.0f;    // voice-band SNR (dB) where it works fully...
constexpr float kCombOffSnr  = 25.0f;    // ...and where it isn't needed at all
const float kCombFade       = Coef(0.005f);
const float kCombGlide      = Coef(0.002f);

inline float DbToLin(float db) { return std::pow(10.0f, db / 20.0f); }
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
    fpOver_.fill(1.0f);
    fpFloor_.fill(1.0f);
    clickGain_.fill(1.0f);
    combSplit_ = VoicePolish::Biquad::LowPass(4000.0f, 0.7071f);

    // Scope bands: log-spaced 100 Hz - 16 kHz, at least one bin each.
    constexpr int nb = VoiceSettings::kScopeBands;
    for (int b = 0; b <= nb; b++)
    {
        const float hz = 100.0f * std::pow(160.0f, static_cast<float>(b) / nb);
        scopeEdge_[b] = static_cast<int>(std::lround(hz / (kFs / kFft)));
        if (b > 0) scopeEdge_[b] = std::max(scopeEdge_[b], scopeEdge_[b - 1] + 1);
    }
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
// `lag` is the pitch period: the shortest lag that scores nearly as well as
// the best one, so a voice isn't mistaken for half its pitch.
bool VoiceProcessor::PitchSearch(int& lag, float& r) const
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

    float rs[maxLag + 1] = {};
    float best = 0.0f;
    for (int l = minLag; l <= maxLag; l++)
    {
        float num = 0.0f, el = 0.0f;
        for (int n = 0; n < W; n++) { num += x[n] * x[n + l]; el += x[n + l] * x[n + l]; }
        rs[l] = num / std::sqrt(e0 * el + 1e-12f);
        best = std::max(best, rs[l]);
    }

    lag = minLag;
    while (lag < maxLag && rs[lag] < 0.9f * best) lag++;
    while (lag < maxLag && rs[lag + 1] > rs[lag]) lag++;   // climb to that peak
    r = rs[lag];
    return best > kVoicedR;
}

// Refines a pitch period to a fraction of a sample at 48 kHz, on the < 4 kHz
// band the comb uses. Returns the period in samples; `r` is how periodic it is.
float VoiceProcessor::RefinePitch(float coarse, float& r) const
{
    constexpr int W = 512, minT = 100, maxT = 700, Search = 10;
    auto at = [this](int back) { return combRing_[(combPos_ - 1 - back) & (kCombRing - 1)]; };

    float e0 = 0.0f;
    for (int n = 0; n < W; n++) e0 += at(n) * at(n);

    const int c = static_cast<int>(std::lround(coarse));
    const int lo = std::max(minT, c - Search), hi = std::min(maxT, c + Search);
    float rs[2 * Search + 3] = {};
    int bestT = lo;
    float best = -1.0f;
    for (int T = lo - 1; T <= hi + 1; T++)
    {
        float num = 0.0f, eT = 0.0f;
        for (int n = 0; n < W; n++) { const float d = at(n + T); num += at(n) * d; eT += d * d; }
        const float rt = num / std::sqrt(e0 * eT + 1e-12f);
        rs[T - lo + 1] = rt;
        if (T >= lo && T <= hi && rt > best) { best = rt; bestT = T; }
    }

    // Parabolic interpolation around the best lag.
    const float ym = rs[bestT - lo], y0 = rs[bestT - lo + 1], yp = rs[bestT - lo + 2];
    const float den = ym - 2.0f * y0 + yp;
    const float frac = (den < -1e-9f) ? std::clamp(0.5f * (ym - yp) / den, -0.5f, 0.5f) : 0.0f;
    r = best;
    return static_cast<float>(bestT) + frac;
}

// Pitch-tracked comb on the band below 4 kHz, averaging one and two pitch
// periods back. Your harmonics line up with their earlier copies and pass at
// full level; noise between them is turned down (about 3 dB below 4 kHz).
// Its strength follows how noisy the voice band is, so a clean voice is left
// alone. At strength 0 the output is exactly x.
float VoiceProcessor::Comb(float x)
{
    const float lo = combSplit_.Run(x);
    combRing_[combPos_] = lo;
    combPos_ = (combPos_ + 1) & (kCombRing - 1);

    combAmt_ += (combTargetAmt_ - combAmt_) * kCombFade;
    if (combAmt_ < 1e-4f)
    {
        combT_ = combTargetT_;   // nothing audible: jump straight to the new pitch
        return x;
    }
    combT_ += (combTargetT_ - combT_) * kCombGlide;

    auto past = [this](float periods) {
        const float back = static_cast<float>(combPos_ - 1) - periods * combT_;
        const float fl = std::floor(back);
        const float fr = back - fl;
        const int   i  = static_cast<int>(fl);
        return combRing_[i & (kCombRing - 1)] * (1.0f - fr) + combRing_[(i + 1) & (kCombRing - 1)] * fr;
    };
    return x + combAmt_ * (kCombW1 * (past(1.0f) - lo) + kCombW2 * (past(2.0f) - lo));
}

// Learns from this hop, and every ~0.5 s swaps the profile with the UI side:
// adopt one the UI just loaded (or reset), otherwise publish what was learned.
void VoiceProcessor::LearnAndExchange(VoiceSettings& s, bool learn, bool voicedHop, float f0, float rmsDb)
{
    const bool clicks = learn && s.removeClicks.load(std::memory_order_relaxed);   // dictionaries learn only when used
    if (learn)
    {
        if (voicedHop && f0 >= learnLo_ && f0 <= learnHi_ && rmsDb > floorDb_ + kLearnAboveDb)
            work_.LearnPitch(f0, rmsDb);
        if (speakHops_ > 0 && rmsDb > floorDb_ + 10.0f)
            work_.LearnSpectrum(hopRelDb_.data());

        // Voice atoms learn only from clearly pitched hops, with the steady
        // noise taken out: the moments around words can hold a keypress too,
        // and the voice atoms must never learn to explain those.
        if (clicks && voicedHop && rmsDb > floorDb_ + kLearnAboveDb)
        {
            std::array<float, kBins> v{};
            const auto& mag = magRing_[(magPos_ + kRoomDelay - 3) % kRoomDelay];   // pitch is judged 2 hops later than analysed
            for (int k = 0; k < kBins; k++) v[k] = std::max(mag[k] - std::sqrt(noise_[k]), 0.0f);
            dict_.LearnVowel(v.data());
        }

        // A vowel just started: the hops before it may hold a consonant.
        // Learn the ones that are steady hiss (a keypress fades within a hop,
        // so it can't pass this) and mostly above 2.5 kHz.
        if (clicks && voicedRun_ == 4)
        {
            const int hf = static_cast<int>(2500.0f / (kFs / kFft));
            float e[kRoomDelay] = {}, eHigh[kRoomDelay] = {};
            std::array<std::array<float, kBins>, kRoomDelay> clean{};
            for (int back = 0; back < kRoomDelay; back++)        // back = 0: newest hop
            {
                const auto& mag = magRing_[(magPos_ + kRoomDelay - 1 - back) % kRoomDelay];
                for (int k = 0; k < kBins; k++)
                {
                    const float c = std::max(mag[k] - std::sqrt(noise_[k]), 0.0f);
                    clean[back][k] = c;
                    e[back] += c * c;
                    if (k >= hf) eHigh[back] += c * c;
                }
            }
            float bg = 0.0f;
            for (int k = 0; k < kBins; k++) bg += noise_[k];
            // The vowel began ~6 hops before now (4 voiced hops + 2 of STFT delay);
            // look at the ~100 ms before that.
            for (int back = 7; back < 26 && back + 1 < kRoomDelay; back++)
            {
                const bool loud   = e[back] > 4.0f * bg;
                const bool hissy  = eHigh[back] > 0.5f * e[back];
                const bool steady = e[back - 1] > 0.25f * e[back] && e[back + 1] > 0.25f * e[back]
                                 && e[back - 1] < 4.0f * e[back] && e[back + 1] < 4.0f * e[back];
                if (loud && hissy && steady) dict_.LearnConsonant(clean[back].data());
            }
        }

        // Room atoms learn from the oldest hop in the ring (kRoomDelay ago), if
        // nobody's voice came within kRoomAfter hops before it or any time since.
        hopsSinceVoiced_ = (speakHops_ == kSpeakHoldHops) ? 0 : hopsSinceVoiced_ + 1;
        if (clicks && hops_ > 60 && hopsSinceVoiced_ > kRoomDelay + kRoomAfter)
            dict_.LearnRoom(magRing_[magPos_].data());
    }

    if (--exchangeIn_ > 0) return;
    std::unique_lock<std::mutex> lk(s.profileMu_, std::try_to_lock);
    if (!lk.owns_lock()) { exchangeIn_ = 1; return; }   // UI is busy with it: next hop
    exchangeIn_ = kExchangeHops;

    if (s.profileVersion_ != seenVersion_)
    {
        work_ = s.profile_;
        seenVersion_ = s.profileVersion_;
        dict_.SetVoiceAtoms(work_.voiceAtoms);
    }
    else if (learn)
    {
        dict_.CopyVoiceAtoms(work_.voiceAtoms);
        s.profile_ = work_;
    }
    lk.unlock();

    useProfile_ = learn && work_.Trained();
    UseProfile(work_);

    float lo = 0.0f, hi = 0.0f;
    work_.PitchRange(lo, hi);
    s.profileInUse.store(useProfile_, std::memory_order_relaxed);
    s.learnedSec.store(work_.voicedSec, std::memory_order_relaxed);
    s.pitchLoHz.store(work_.Trained() ? lo : 0.0f, std::memory_order_relaxed);
    s.pitchHiHz.store(work_.Trained() ? hi : 0.0f, std::memory_order_relaxed);
    s.speechLevelDb.store(work_.speechDb, std::memory_order_relaxed);
}

void VoiceProcessor::UseProfile(const VoiceProfile& p)
{
    float lo = 70.0f, hi = 400.0f;
    if (p.Trained()) p.PitchRange(lo, hi);

    rangeLo_ = useProfile_ ? std::max(70.0f, lo / kRangeBelow) : 70.0f;
    rangeHi_ = useProfile_ ? std::min(400.0f, hi * kRangeAbove) : 400.0f;
    learnLo_ = p.Trained() ? std::max(70.0f, lo / kLearnMargin) : 70.0f;
    learnHi_ = p.Trained() ? std::min(400.0f, hi * kLearnMargin) : 400.0f;

    // Voice fingerprint: bins where your voice sits more than 15 dB under its
    // strongest band get progressively stricter suppression (up to twice the
    // over-subtraction and 6 dB more depth at 40 dB under).
    fpOver_.fill(1.0f);
    fpFloor_.fill(1.0f);
    if (!useProfile_ || p.spectrumSec < VoiceProfile::kTrainedSec) return;

    float peak = -1e9f;
    for (int k = 2; k <= 170; k++) peak = std::max(peak, p.spectrumDb[k]);   // 94 Hz - 8 kHz
    for (int k = 0; k < kBins; k++)
    {
        const float below = std::clamp((peak - p.spectrumDb[k] - 15.0f) / 25.0f, 0.0f, 1.0f);
        fpOver_[k]  = 1.0f + below;
        fpFloor_[k] = DbToLin(-6.0f * below);
    }
}

void VoiceProcessor::Process(float* stereo, size_t frames, VoiceSettings& s)
{
    const bool rumble = s.rumbleFilter.load(std::memory_order_relaxed);
    const int  gate   = std::clamp(s.gateMode.load(std::memory_order_relaxed), 0, 3);
    const bool deEss  = s.deEsser.load(std::memory_order_relaxed);
    const bool eq     = s.voiceEq.load(std::memory_order_relaxed);
    const bool comp   = s.compressor.load(std::memory_order_relaxed);
    const bool limit  = s.limiter.load(std::memory_order_relaxed);
    const bool learn  = s.learnVoice.load(std::memory_order_relaxed);
    const bool clean  = s.cleanWhileTalking.load(std::memory_order_relaxed);
    const bool autoLv = s.autoLevel.load(std::memory_order_relaxed);
    const int  effect = s.voiceFx.load(std::memory_order_relaxed);

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
        float g = Comb(o);
        if (gate == GateVoice)
        {
            g = delay_[delayPos_];
            delay_[delayPos_] = o;
            delayPos_ = (delayPos_ + 1) % kLookahead;
        }
        autoGain_ += (autoTarget_ - autoGain_) * kAutoGainCoef;
        const float y = polish_.Process(fx_.Process(Gate(g, gate) * autoGain_, effect), deEss, eq, comp, limit);
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
            // only fake it for a hop or two. Require ~21 ms in a row. Once it
            // knows your voice, only your own pitch range counts.
            int   lag = 0;
            float r6 = 0.0f, rFull = 0.0f, f0 = 0.0f;
            bool pitched = hops_ > 40 && rmsDb > floorDb_ + kGate[GateVoice].aboveFloorDb && PitchSearch(lag, r6);
            if (pitched)
            {
                const float T = RefinePitch(static_cast<float>(lag * kDecim), rFull);
                f0 = kFs / T;
                if (useProfile_ && (f0 < rangeLo_ || f0 > rangeHi_)) pitched = false;
                else                                combTargetT_ = T;
            }
            voicedRun_ = pitched ? voicedRun_ + 1 : 0;
            const bool voiced = voicedRun_ >= 4;
            if (voiced) { voiceHold_ = kVoiceHoldSec * kFs; speakHops_ = kSpeakHoldHops; }
            else        speakHops_ = std::max(0, speakHops_ - 1);
            s.voiceDetected.store(voiced, std::memory_order_relaxed);
            s.speaking.store(speakHops_ > 0, std::memory_order_relaxed);

            const float need = std::clamp((kCombOffSnr - bandSnrDb_) / (kCombOffSnr - kCombFullSnr), 0.0f, 1.0f);
            combTargetAmt_ = (clean && pitched && voicedRun_ >= 2 && rFull > kCombMinR) ? need : 0.0f;

            LearnAndExchange(s, learn, voiced && r6 > kLearnR, f0, rmsDb);

            autoTarget_ = (autoLv && useProfile_)
                ? DbToLin(std::clamp(kAutoTargetDb - work_.speechDb, -12.0f, 20.0f)) : 1.0f;
            s.autoGainDb.store(20.0f * std::log10(autoGain_), std::memory_order_relaxed);

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
    bandSnrDb_ = 10.0f * std::log10(snrSum / (hi - lo + 1) + 1e-12f);

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

    // ---- Voice spectrum for "Learn my voice" (noise taken out) ---------------
    if (s.learnVoice.load(std::memory_order_relaxed))
    {
        float mean = 0.0f;
        for (int k = 0; k < kBins; k++)
        {
            hopRelDb_[k] = 10.0f * std::log10(std::max(std::norm(spec_[k]) - noise_[k], 1e-12f));
            if (k >= lo && k <= hi) mean += hopRelDb_[k];
        }
        mean /= static_cast<float>(hi - lo + 1);
        for (float& d : hopRelDb_) d -= mean;
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
        const float n     = noise_[k] * np.overSub * fpOver_[k];   // fingerprint: 1 unless learned
        const float post  = p / n;
        const float prior = 0.98f * prevClean_[k] / n + 0.02f * std::max(post - 1.0f, 0.0f);
        float gk = prior / (1.0f + prior);
        gk = std::max(gk, np.floorGain * fpFloor_[k]);
        prevClean_[k] = gk * gk * p;
        g[k] = gk;
    }

    // ---- Keyboard & clicks: keep only what the voice atoms explain ----------
    // Applied as a ceiling on the noise suppressor's gain: steady noise is
    // already handled there; this catches what it lets through.
    std::array<float, kBins> mag{};
    for (int k = 0; k < kBins; k++) mag[k] = std::abs(spec_[k]);
    magRing_[magPos_] = mag;
    magPos_ = (magPos_ + 1) % kRoomDelay;
    if (s.removeClicks.load(std::memory_order_relaxed) && useProfile_ && dict_.Ready())
    {
        std::array<float, kBins> mask{};
        dict_.Separate(mag.data(), mask.data());

        // Did something that isn't you just start, on top of the steady background?
        float evE = 0.0f, bgE = 0.0f;
        for (int k = 0; k < kBins; k++)
        {
            evE += (1.0f - mask[k]) * std::max(std::norm(spec_[k]) - noise_[k], 0.0f);
            bgE += noise_[k];
        }
        const bool onset = evE > kOnsetJump * roomAvg_ + 0.5f * bgE;
        roomAvg_ = 0.9f * roomAvg_ + 0.1f * evE;
        sinceOnset_ = onset ? 0 : std::min(sinceOnset_ + 1, 1000);
        const float strength = std::clamp(1.0f - static_cast<float>(sinceOnset_ + 1 - kClickHops) / kClickFade, 0.0f, 1.0f);

        for (int k = 0; k < kBins; k++)
        {
            const float m = std::max(1.0f - strength * (1.0f - mask[k]), kClickFloor);
            clickGain_[k] = (m < clickGain_[k]) ? m : 0.5f * clickGain_[k] + 0.5f * m;   // drop at once, recover over ~2 hops
        }
    }
    else
        clickGain_.fill(1.0f);

    for (int k = 0; k < kBins; k++)
    {
        // Smooth across neighbouring bins, then over time (fast up, slower down):
        // this is what keeps the residue from sounding "watery".
        const float gl = g[std::max(k - 1, 0)], gr = g[std::min(k + 1, kBins - 1)];
        const float gs = 0.25f * gl + 0.5f * g[k] + 0.25f * gr;
        gain_[k] = (gs > gain_[k]) ? gs : 0.65f * gain_[k] + 0.35f * gs;

        const float gk = std::min(gain_[k], clickGain_[k]);
        const float p = std::norm(spec_[k]);
        inE  += p;
        outE += gk * gk * p;
        spec_[k] *= gk;
    }
    // Scope: what came in and what's left, per band (spec_ now holds the output).
    for (int b = 0; b < VoiceSettings::kScopeBands; b++)
    {
        float in = 0.0f, out = 0.0f;
        for (int k = scopeEdge_[b]; k < scopeEdge_[b + 1] && k < kBins; k++)
        {
            in  += mag[k] * mag[k];
            out += std::norm(spec_[k]);
        }
        // Window gain (sum of sqrt-Hann^2 = N/2) brings this to roughly dBFS.
        const float scale = 2.0f / (kFft * 0.5f * kFft);
        s.scopeInDb[b].store(10.0f * std::log10(in * scale + 1e-12f), std::memory_order_relaxed);
        s.scopeOutDb[b].store(10.0f * std::log10(out * scale + 1e-12f), std::memory_order_relaxed);
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
        // Learned: a set share of the way from your room up to your voice.
        const float above = useProfile_
            ? std::clamp((work_.speechDb - floorDb_) * kGateShare[mode], 6.0f, 24.0f) : gp.aboveFloorDb;
        const float threshold = std::clamp(floorDb_ + above, -65.0f, -30.0f);
        const float envDb = 20.0f * std::log10(env_ + 1e-9f);

        if (envDb > threshold) holdLeft_ = 0.20f * kFs;          // stay open 200 ms after words
        else                   holdLeft_ = std::max(0.0f, holdLeft_ - 1.0f);

        target = (holdLeft_ > 0.0f) ? 1.0f : std::pow(10.0f, gp.rangeDb / 20.0f);
    }

    gateGain_ += (target - gateGain_) * (target > gateGain_ ? kGateOpen : kGateClose);
    return x * gateGain_;
}

} // namespace mixcast
