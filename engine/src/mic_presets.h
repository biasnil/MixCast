// MixCast engine - one-choice mic sound presets.
//
// Each preset sets every mic clean-up option at once. The options themselves
// stay available (the GUI's Advanced menu, the console keys); a mix that
// matches no preset shows as "Custom".
#pragma once

#include "voice_processor.h"

namespace mixcast {

enum MicPreset : int { PresetNatural = 0, PresetClean, PresetStudio, PresetNoisyRoom, PresetCount, PresetCustom = -1 };

struct MicPresetDef
{
    const char* name;
    const char* blurb;
    int  noise, gate;
    bool rumble, deEss, eq, comp, limit, learn, autoLevel, cleanTalk, clicks;
};

inline const MicPresetDef& MicPresetInfo(int preset)
{
    static const MicPresetDef kDefs[PresetCount] = {
        { "Natural",    "Your mic almost as it is: only hiss and low rumble taken off",
          NoiseLow,    GateOff,    true, false, false, false, true, true, false, false, false },
        { "Clean",      "Fans and hum removed, quiet between your words (recommended)",
          NoiseMedium, GateGentle, true, false, false, false, true, true, false, false, false },
        { "Studio",     "Clean, plus a radio voice: softer \"s\", clearer tone, even and steady level",
          NoiseMedium, GateGentle, true, true,  true,  true,  true, true, true,  false, false },
        { "Noisy room", "Strongest: only your voice opens the mic, keyboard and clicks removed",
          NoiseHigh,   GateVoice,  true, false, false, false, true, true, false, true,  true },
    };
    return kDefs[preset];
}

inline void ApplyMicPreset(VoiceSettings& v, int preset)
{
    if (preset < 0 || preset >= PresetCount) return;
    const MicPresetDef& d = MicPresetInfo(preset);
    v.noiseLevel = d.noise;
    v.gateMode = d.gate;
    v.rumbleFilter = d.rumble;
    v.deEsser = d.deEss;
    v.voiceEq = d.eq;
    v.compressor = d.comp;
    v.limiter = d.limit;
    v.learnVoice = d.learn;
    v.autoLevel = d.autoLevel;
    v.cleanWhileTalking = d.cleanTalk;
    v.removeClicks = d.clicks;
}

// The preset the current settings match, or PresetCustom.
inline int MatchMicPreset(const VoiceSettings& v)
{
    for (int p = 0; p < PresetCount; p++)
    {
        const MicPresetDef& d = MicPresetInfo(p);
        if (v.noiseLevel == d.noise && v.gateMode == d.gate && v.rumbleFilter == d.rumble
            && v.deEsser == d.deEss && v.voiceEq == d.eq && v.compressor == d.comp && v.limiter == d.limit
            && v.learnVoice == d.learn && v.autoLevel == d.autoLevel
            && v.cleanWhileTalking == d.cleanTalk && v.removeClicks == d.clicks)
            return p;
    }
    return PresetCustom;
}

} // namespace mixcast
