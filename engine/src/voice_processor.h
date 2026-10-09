// MixCast engine - mic clean-up (classic DSP, no AI, no third-party code).
//
// Chain, all at 48 kHz on the mono mic signal:
//   1. Rumble filter   2nd-order 80 Hz high-pass (desk bumps, AC hum lows, plosive thumps)
//   2. Noise suppressor STFT (512-pt, 50% overlap) with a continuously tracked
//                       noise spectrum and decision-directed Wiener gains
//                       (Ephraim-Malah style), smoothed to avoid "musical noise"
//   3. Background gate  expander that follows the residual noise floor and
//                       closes between words (keyboard, mouse, breathing).
//                       "Voice only" mode opens only for voiced speech
//                       (pitch detected by autocorrelation), so chewing,
//                       clicks and bumps stay muted even when they're loud.
//   4. Voice effect     Deep, Chipmunk, Robot, Radio, Echo, Reverb (voice_effects.h)
//   5. Voice polish     de-esser, voice EQ, compressor, limiter (voice_polish.h)
//
// "Learn my voice" (voice_profile.h) builds a profile from moments it's sure
// are you talking and uses it to:
//   - accept only your own pitch range in the voice detector ("Voice only"
//     gate, ducking, the Talking lamp)
//   - set the Gentle/Firm gate threshold between your room and your voice
//   - make the noise suppressor stricter where your voice never has energy
//   - "Auto level": bring your speech to a steady level, whatever the mic gain
// "Remove keyboard & clicks" (sound_dictionary.h) learns your voice and the
// sounds around you between sentences, and keeps only the voice part of each
// moment, so sudden noises go too, even under your words.
// "Clean while I talk" is a pitch-tracked comb filter: while you speak a
// vowel it keeps your harmonics and turns down what lies between them, so
// noise under your voice drops too (the STFT's 94 Hz bins are too coarse to
// do that per harmonic, so it runs in the time domain, below 4 kHz). It only
// works as hard as the voice band is noisy.
//
// Added latency: 512 + 63 samples = 12.0 ms (+25 ms lookahead in "Voice only" gate mode)
// (so toggling never clicks).
#pragma once

#include <array>
#include <atomic>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <mutex>

#include "sound_dictionary.h"
#include "voice_effects.h"
#include "voice_polish.h"
#include "voice_profile.h"

namespace mixcast {

enum NoiseLevel : int { NoiseOff = 0, NoiseLow = 1, NoiseMedium = 2, NoiseHigh = 3 };
enum GateMode   : int { GateOff = 0, GateGentle = 1, GateFirm = 2, GateVoice = 3 };

// Shared between UI and the mic's capture thread.
struct VoiceSettings
{
    std::atomic<int>   noiseLevel{NoiseMedium};
    std::atomic<int>   gateMode{GateGentle};
    std::atomic<bool>  rumbleFilter{true};
    std::atomic<bool>  deEsser{false};
    std::atomic<bool>  voiceEq{false};
    std::atomic<bool>  compressor{false};
    std::atomic<bool>  limiter{true};
    std::atomic<bool>  learnVoice{true};
    std::atomic<bool>  autoLevel{false};
    std::atomic<bool>  cleanWhileTalking{false};
    std::atomic<bool>  removeClicks{false};
    std::atomic<int>   voiceFx{FxNone};          // fun effect (voice_effects.h)

    // Published by the processor (read-only for the UI).
    std::atomic<float> noiseFloorDb{-90.0f};   // estimated background level
    std::atomic<float> removedDb{0.0f};        // how much is being taken out right now
    std::atomic<bool>  gateOpen{true};
    std::atomic<bool>  voiceDetected{false};   // pitch found in the current hop
    std::atomic<float> deEssDb{0.0f};          // polish gain reduction right now (<= 0)
    std::atomic<float> compressDb{0.0f};
    std::atomic<float> limitDb{0.0f};
    std::atomic<bool>  speaking{false};        // your voice, right now (ducking, lamp)
    std::atomic<bool>  profileInUse{false};    // learning on and enough heard
    std::atomic<float> learnedSec{0.0f};       // voiced speech in the profile
    std::atomic<float> pitchLoHz{0.0f};        // your range (5th-95th percentile)
    std::atomic<float> pitchHiHz{0.0f};
    std::atomic<float> speechLevelDb{0.0f};
    std::atomic<float> autoGainDb{0.0f};

    // "What's being removed": the mic in 20 bands (100 Hz - 16 kHz) before
    // and after noise suppression and click removal, in dBFS.
    static constexpr int kScopeBands = 20;
    std::array<std::atomic<float>, kScopeBands> scopeInDb{};
    std::array<std::atomic<float>, kScopeBands> scopeOutDb{};

    // The learned profile. The UI loads/saves it; the capture thread adopts a
    // newly loaded one and publishes what it learns, without ever blocking.
    void LoadProfile(const VoiceProfile& p)
    {
        std::lock_guard<std::mutex> lk(profileMu_);
        profile_ = p;
        profileVersion_++;
    }
    VoiceProfile CopyProfile()
    {
        std::lock_guard<std::mutex> lk(profileMu_);
        return profile_;
    }

private:
    friend class VoiceProcessor;
    std::mutex   profileMu_;
    VoiceProfile profile_;
    uint32_t     profileVersion_ = 0;
};

class VoiceProcessor
{
public:
    static constexpr int kFft = 512;
    static constexpr int kHop = kFft / 2;
    static constexpr int kBins = kFft / 2 + 1;

    VoiceProcessor();

    // In place: interleaved stereo float @ 48 kHz. Output is the cleaned mono
    // voice on both channels.
    void Process(float* stereo, size_t frames, VoiceSettings& s);

private:
    float HighPass(float x);
    float PitchLowPass(float x);
    bool  PitchSearch(int& lag, float& r) const;
    float RefinePitch(float coarse, float& r) const;
    float Comb(float x);
    void  LearnAndExchange(VoiceSettings& s, bool learn, bool voicedHop, float f0, float rmsDb);
    void  UseProfile(const VoiceProfile& p);
    void  ProcessHop(VoiceSettings& s);
    void  Fft(std::complex<float>* a, bool inverse) const;
    float Gate(float x, int mode);

    // Rumble filter (biquad, transposed direct form II).
    float b0_, b1_, b2_, a1_, a2_;
    float z1_ = 0.0f, z2_ = 0.0f;

    // STFT buffers.
    std::array<float, kFft>  window_{};     // sqrt-Hann (analysis and synthesis)
    std::array<float, kFft>  frame_{};      // last kFft input samples
    std::array<float, kHop>  inHop_{};      // incoming samples for the next hop
    std::array<float, kFft>  ola_{};        // overlap-add accumulator
    std::array<float, kHop>  outHop_{};     // finished samples being played out
    int                      pos_ = 0;

    std::array<std::complex<float>, kFft> spec_{};
    std::array<std::complex<float>, kFft / 2> twiddle_{};
    std::array<int, kFft>    bitrev_{};

    // Noise tracking / Wiener state (per bin).
    std::array<float, kBins> smoothPow_{};
    std::array<float, kBins> noise_{};
    std::array<float, kBins> prevClean_{};
    std::array<float, kBins> gain_{};
    int                      hops_ = 0;

    // Voice detector: 900 Hz low-pass (2 x biquad), decimate by 8 -> 6 kHz,
    // normalised autocorrelation over 70-400 Hz pitch.
    static constexpr int kDecim = 8;
    static constexpr int kPitchRing = 256;
    static constexpr int kLookahead = 1200;          // 25 ms: gate "sees" words before they arrive
    float lb0_, lb1_, lb2_, la1_, la2_;
    float lz[2][2] = {};
    std::array<float, kPitchRing> pitchRing_{};
    int   ringPos_ = 0;
    int   decimCount_ = 0;
    std::array<float, kLookahead> delay_{};
    int   delayPos_ = 0;
    int   lastGate_ = -1;
    float voiceHold_ = 0.0f;
    int   voicedRun_ = 0;       // consecutive voiced hops

    // Gate state.
    float env_ = 0.0f;
    float gateGain_ = 1.0f;
    float holdLeft_ = 0.0f;
    float floorDb_ = -70.0f;
    double hopEnergy_ = 0.0;

    // Learned profile (this thread's working copy) and what's derived from it.
    VoiceProfile work_;
    uint32_t     seenVersion_ = UINT32_MAX;
    int          exchangeIn_ = 0;
    bool         useProfile_ = false;     // learning on and trained
    float        rangeLo_ = 70.0f, rangeHi_ = 400.0f;    // accepted as your voice
    float        learnLo_ = 70.0f, learnHi_ = 400.0f;    // accepted for learning
    std::array<float, kBins> fpOver_{};   // per-bin over-subtraction factor
    std::array<float, kBins> fpFloor_{};  // per-bin extra floor (linear)
    std::array<float, kBins> hopRelDb_{}; // this hop's voice-band-relative spectrum
    int          speakHops_ = 0;          // hops since you last spoke a vowel (hold)

    // Remove keyboard & clicks.
    static constexpr int kRoomDelay = 24;   // hops (128 ms) of no voice after a "room" hop...
    static constexpr int kRoomAfter = 36;   // ...and 192 ms since the last voice before it
                                            // (so trailing "s" and leading consonants aren't room)
    SoundDictionary dict_;
    std::array<std::array<float, kBins>, kRoomDelay> magRing_{};
    int   magPos_ = 0;
    int   hopsSinceVoiced_ = 0;
    std::array<float, kBins> clickGain_{};
    std::array<int, VoiceSettings::kScopeBands + 1> scopeEdge_{};   // first bin of each band
    int   sinceOnset_ = 1000;             // hops since a "not you" sound last started
    float roomAvg_ = 0.0f;                // recent "not you" energy

    // Auto level.
    float autoGain_ = 1.0f, autoTarget_ = 1.0f;

    // "Clean while I talk": pitch-tracked comb on the band below 4 kHz.
    static constexpr int kCombRing = 2048;
    VoicePolish::Biquad combSplit_;
    std::array<float, kCombRing> combRing_{};
    int   combPos_ = 0;
    float combAmt_ = 0.0f, combTargetAmt_ = 0.0f;
    float combT_ = 0.0f, combTargetT_ = 0.0f;
    float bandSnrDb_ = 0.0f;              // voice band (300-3400 Hz) level over the noise

    VoicePolish  polish_;
    VoiceEffects fx_;
};

} // namespace mixcast
