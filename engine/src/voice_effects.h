// MixCast engine - fun voice effects for the mic (classic DSP).
//
//   Deep / Chipmunk  pitch shift down 5 / up 7 semitones: two read heads
//                    sweep a 30 ms delay line at a different speed and
//                    crossfade (granular, ~15 ms latency)
//   Robot            a 110 Hz comb (metallic monotone) with a little ring
//                    modulation
//   Radio            walkie-talkie: 500 Hz - 3 kHz band, gently overdriven
//   Echo             320 ms repeats, each a little darker
//   Reverb           a room: four damped combs into two all-passes
//
// Runs after the clean-up and before the radio voice chain, so the limiter
// still catches anything an effect makes louder. Switching effects
// crossfades over 30 ms; with no effect the voice passes through exactly.
#pragma once

#include "voice_polish.h"   // VoicePolish::Biquad

#include <array>
#include <vector>

namespace mixcast {

enum VoiceFx : int { FxNone = 0, FxDeep, FxChipmunk, FxRobot, FxRadio, FxEcho, FxReverb, FxCount };

const char* VoiceFxName(int fx);

class VoiceEffects
{
public:
    VoiceEffects();
    float Process(float x, int fx);

private:
    float Run(float x, int fx);   // one effect, no crossfade
    float Pitch(float x, float ratio);
    float Robot(float x);
    float Radio(float x);
    float Echo(float x);
    float Reverb(float x);

    int   current_ = FxNone, previous_ = FxNone;
    float fade_ = 1.0f;           // 0 -> 1 while crossfading previous -> current

    // Pitch: a delay line read by two heads half a window apart.
    static constexpr int kPitchRing = 4096;
    static constexpr int kGrain = 1440;   // 30 ms
    std::array<float, kPitchRing> pitchBuf_{};
    int   pitchPos_ = 0;
    float head_ = 0.0f;

    // Robot.
    std::array<float, 512> comb_{};
    int   combPos_ = 0;
    float phase_ = 0.0f;

    // Radio.
    VoicePolish::Biquad hp1_, hp2_, lp1_, lp2_;

    // Echo.
    std::vector<float> echo_;
    int   echoPos_ = 0;
    float echoLp_ = 0.0f;

    // Reverb.
    struct Comb { std::vector<float> buf; int pos = 0; float store = 0.0f; };
    struct AllPass { std::vector<float> buf; int pos = 0; };
    std::array<Comb, 4>    combs_;
    std::array<AllPass, 2> allpasses_;
};

} // namespace mixcast
