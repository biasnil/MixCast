// MixCast engine - "Remove keyboard & clicks": learned sound dictionaries (NMF, no AI).
#include "sound_dictionary.h"

#include <algorithm>
#include <cmath>

namespace mixcast {

namespace {
constexpr int   kSeparateIters = 8;
constexpr int   kLearnIters    = 8;
constexpr int   kSeedEvery     = 6;          // spread seed atoms out over different moments
constexpr float kVoiceKeep     = 1.0f - 1.0f / 4000.0f;   // ~20 s of speech hops of memory
constexpr float kConsonantKeep = 1.0f - 1.0f / 1000.0f;   // consonant hops are rarer
constexpr float kRoomKeep      = 1.0f - 1.0f / 1500.0f;   // ~8 s of quiet hops: follows the room
constexpr float kEps           = 1e-9f;
} // namespace

SoundDictionary::SoundDictionary()
{
    den_.fill(1.0f);
}

void SoundDictionary::Normalize(int atom)
{
    float* w = &w_[static_cast<size_t>(atom) * kBins];
    float sum = 0.0f;
    for (int f = 0; f < kBins; f++) sum += w[f];
    const float inv = 1.0f / std::max(sum, kEps);
    for (int f = 0; f < kBins; f++) w[f] *= inv;
}

// out = W h over atoms [first, first + count). Atom-major, so the inner loop
// runs over contiguous bins and vectorises.
void SoundDictionary::Model(int first, int count, const float* h, float* out) const
{
    std::fill(out, out + kBins, 0.0f);
    for (int k = 0; k < count; k++)
    {
        const float* w = &w_[static_cast<size_t>(first + k) * kBins];
        const float hk = h[k];
        for (int f = 0; f < kBins; f++) out[f] += w[f] * hk;
    }
}

// KL-NMF activations for atoms [first, first + count), dictionary fixed.
// Atoms sum to 1, so the multiplicative update is h *= W^T (v / Wh).
void SoundDictionary::Activations(const float* mag, int first, int count, float* h, int iters) const
{
    std::array<float, kBins> ratio{};
    for (int it = 0; it < iters; it++)
    {
        Model(first, count, h, ratio.data());
        for (int f = 0; f < kBins; f++) ratio[f] = mag[f] / (ratio[f] + kEps);
        for (int k = 0; k < count; k++)
        {
            const float* w = &w_[static_cast<size_t>(first + k) * kBins];
            float s = 0.0f;
            for (int f = 0; f < kBins; f++) s += w[f] * ratio[f];
            h[k] *= s;
        }
    }
}

// Seeds atoms from real spectra first (a few hops apart, so they differ),
// then refines them with an online multiplicative update that slowly forgets.
void SoundDictionary::Learn(const float* mag, int first, int count, int& seeded, int& seedSkip, float keep)
{
    float sum = 0.0f;
    for (int f = 0; f < kBins; f++) sum += mag[f];
    if (sum <= kEps) return;

    if (seeded < count)
    {
        if (seedSkip++ % kSeedEvery != 0) return;
        const int atom = first + seeded;
        float* w = &w_[static_cast<size_t>(atom) * kBins];
        const float floor = 1e-3f * sum / kBins;   // no exact zeros: an atom must be able to grow anywhere
        for (int f = 0; f < kBins; f++) w[f] = mag[f] + floor;
        Normalize(atom);
        for (int f = 0; f < kBins; f++) num_[static_cast<size_t>(atom) * kBins + f] = w[f];
        den_[atom] = 1.0f;
        seeded++;
        return;
    }

    std::array<float, kAtoms> h{};
    for (int k = 0; k < count; k++) h[k] = sum / count;
    Activations(mag, first, count, h.data(), kLearnIters);

    std::array<float, kBins> ratio{};
    Model(first, count, h.data(), ratio.data());
    for (int f = 0; f < kBins; f++) ratio[f] = mag[f] / (ratio[f] + kEps);
    for (int k = 0; k < count; k++)
    {
        const int atom = first + k;
        float* w   = &w_[static_cast<size_t>(atom) * kBins];
        float* num = &num_[static_cast<size_t>(atom) * kBins];
        const float hk = h[k] / sum;               // loudness-independent weight
        den_[atom] = keep * den_[atom] + hk;
        for (int f = 0; f < kBins; f++)
        {
            num[f] = keep * num[f] + w[f] * ratio[f] * hk;
            w[f] = num[f] / std::max(den_[atom], kEps);
        }
        Normalize(atom);
    }
}

void SoundDictionary::LearnVowel(const float* mag)
{
    Learn(mag, 0, kVowelAtoms, vowelSeeded_, vowelSkip_, kVoiceKeep);
}

void SoundDictionary::LearnConsonant(const float* mag)
{
    Learn(mag, kVowelAtoms, kConsonantAtoms, consonantSeeded_, consonantSkip_, kConsonantKeep);
}

void SoundDictionary::LearnRoom(const float* mag)
{
    Learn(mag, kVoiceAtoms, kRoomAtoms, roomSeeded_, roomSkip_, kRoomKeep);
}

void SoundDictionary::Separate(const float* mag, float* mask)
{
    float sum = 0.0f;
    for (int f = 0; f < kBins; f++) sum += mag[f];
    if (sum <= kEps)
    {
        for (int f = 0; f < kBins; f++) mask[f] = 1.0f;
        return;
    }

    // Warm start from the last hop, rescaled to this hop's level, with a
    // little of every atom so one that was silent can come back.
    const float scale = (lastSum_ > kEps) ? sum / lastSum_ : 0.0f;
    for (int k = 0; k < kAtoms; k++) h_[k] = h_[k] * scale + 0.02f * sum / kAtoms;
    lastSum_ = sum;
    Activations(mag, 0, kAtoms, h_.data(), kSeparateIters);

    std::array<float, kBins> voice{}, room{};
    Model(0, kVoiceAtoms, h_.data(), voice.data());
    Model(kVoiceAtoms, kRoomAtoms, h_.data() + kVoiceAtoms, room.data());
    for (int f = 0; f < kBins; f++)
    {
        const float v2 = voice[f] * voice[f], r2 = room[f] * room[f];
        mask[f] = v2 / (v2 + r2 + kEps);
    }
}

void SoundDictionary::CopyVoiceAtoms(std::vector<float>& out) const
{
    if (!VoiceReady()) { out.clear(); return; }
    out.assign(w_.begin(), w_.begin() + kVoiceAtoms * kBins);
}

void SoundDictionary::SetVoiceAtoms(const std::vector<float>& atoms)
{
    ForgetVoice();
    if (atoms.size() != static_cast<size_t>(kVoiceAtoms) * kBins) return;
    std::copy(atoms.begin(), atoms.end(), w_.begin());
    for (int k = 0; k < kVoiceAtoms; k++)
    {
        Normalize(k);
        std::copy(w_.begin() + static_cast<size_t>(k) * kBins, w_.begin() + static_cast<size_t>(k + 1) * kBins,
                  num_.begin() + static_cast<size_t>(k) * kBins);
        den_[k] = 1.0f;
    }
    vowelSeeded_ = kVowelAtoms;
    consonantSeeded_ = kConsonantAtoms;
}

void SoundDictionary::ForgetVoice()
{
    std::fill(w_.begin(), w_.begin() + kVoiceAtoms * kBins, 0.0f);
    std::fill(num_.begin(), num_.begin() + kVoiceAtoms * kBins, 0.0f);
    std::fill(den_.begin(), den_.begin() + kVoiceAtoms, 1.0f);
    vowelSeeded_ = consonantSeeded_ = 0;
    vowelSkip_ = consonantSkip_ = 0;
}

} // namespace mixcast
