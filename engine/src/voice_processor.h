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
//   4. Voice polish     de-esser, voice EQ, compressor, limiter (voice_polish.h)
//
// Added latency: 512 + 63 samples = 12.0 ms (+25 ms lookahead in "Voice only" gate mode)
// (so toggling never clicks).
#pragma once

#include <array>
#include <atomic>
#include <complex>
#include <cstddef>

#include "voice_polish.h"

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

    // Published by the processor (read-only for the UI).
    std::atomic<float> noiseFloorDb{-90.0f};   // estimated background level
    std::atomic<float> removedDb{0.0f};        // how much is being taken out right now
    std::atomic<bool>  gateOpen{true};
    std::atomic<bool>  voiceDetected{false};   // pitch found in the current hop
    std::atomic<float> deEssDb{0.0f};          // polish gain reduction right now (<= 0)
    std::atomic<float> compressDb{0.0f};
    std::atomic<float> limitDb{0.0f};
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
    bool  Voiced() const;
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

    VoicePolish polish_;
};

} // namespace mixcast
