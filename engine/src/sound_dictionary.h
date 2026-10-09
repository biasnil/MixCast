// MixCast engine - "Remove keyboard & clicks": learned sound dictionaries (NMF, no AI).
//
// Small dictionaries of spectral building blocks ("atoms"), learned on this
// PC from this mic:
//   - your voice     vowels, learned while you're clearly talking, and your
//                    consonants ("s", "t", "k"...), learned from the steady
//                    hiss just before a vowel starts (saved with your voice
//                    profile, so it gets better over days)
//   - your room      learned between your sentences: your keyboard, mouse,
//                    chair, desk, fan (relearned every session, ~10 s of
//                    memory, so it follows what's around you now)
// Every STFT hop the spectrum is explained as a non-negative mix of both sets
// (KL-divergence NMF, multiplicative updates). The share explained by your
// voice atoms is kept; the rest is turned down. Unlike the noise suppressor,
// which only learns *steady* sounds, this also works on sudden ones - and
// while you talk.
//
// Plain linear algebra on 257-bin magnitude spectra: 24 vowel + 8 consonant
// atoms for your voice, 16 room atoms, 8 iterations per hop (warm-started).
#pragma once

#include <array>
#include <vector>

namespace mixcast {

class SoundDictionary
{
public:
    static constexpr int kBins        = 257;
    static constexpr int kVowelAtoms     = 24;
    static constexpr int kConsonantAtoms = 8;
    static constexpr int kVoiceAtoms     = kVowelAtoms + kConsonantAtoms;   // all "you"
    static constexpr int kRoomAtoms      = 16;
    static constexpr int kAtoms          = kVoiceAtoms + kRoomAtoms;

    SoundDictionary();

    // Learning, one magnitude spectrum at a time.
    void LearnVowel(const float* mag);
    void LearnConsonant(const float* mag);
    void LearnRoom(const float* mag);

    bool VoiceReady() const { return vowelSeeded_ >= kVowelAtoms && consonantSeeded_ >= kConsonantAtoms; }
    bool RoomReady() const  { return roomSeeded_ >= kRoomAtoms; }
    bool Ready() const      { return VoiceReady() && RoomReady(); }

    // mask[f] = share of bin f that is your voice (0..1). Needs Ready().
    void Separate(const float* mag, float* mask);

    // Voice atoms, for saving with the voice profile (kVoiceAtoms * kBins, or
    // empty while still learning). Copy reuses out's memory.
    void CopyVoiceAtoms(std::vector<float>& out) const;
    void SetVoiceAtoms(const std::vector<float>& atoms);
    void ForgetVoice();

private:
    void Learn(const float* mag, int first, int count, int& seeded, int& seedSkip, float keep);
    void Activations(const float* mag, int first, int count, float* h, int iters) const;
    void Model(int first, int count, const float* h, float* out) const;
    void Normalize(int atom);

    // Atom-major: w_[atom * kBins + bin], each atom sums to 1.
    std::array<float, kAtoms * kBins> w_{};
    std::array<float, kAtoms * kBins> num_{};    // online-update statistics
    std::array<float, kAtoms>         den_{};
    std::array<float, kAtoms>         h_{};      // last activations (warm start)
    float lastSum_ = 0.0f;
    int   vowelSeeded_ = 0, consonantSeeded_ = 0, roomSeeded_ = 0;
    int   vowelSkip_ = 0, consonantSkip_ = 0, roomSkip_ = 0;
};

} // namespace mixcast
