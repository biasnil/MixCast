// MixCast engine - audio editing operations.
#include "audio_edit.h"

#include <algorithm>
#include <cmath>

namespace mixcast::edit {

namespace {
constexpr size_t kJoinFrames = 96;    // 2 ms crossfade where two pieces meet
constexpr float  kPi = 3.14159265358979f;

inline float SCurve(float t) { return 0.5f - 0.5f * std::cos(kPi * std::clamp(t, 0.0f, 1.0f)); }
inline float Lin(float db) { return std::pow(10.0f, db / 20.0f); }
} // namespace

Range Clamp(Range r, const Samples& s)
{
    const size_t n = Frames(s);
    r.a = std::min(r.a, n);
    r.b = std::min(r.b, n);
    if (r.b < r.a) std::swap(r.a, r.b);
    return r;
}

Samples Extract(const Samples& s, Range r)
{
    r = Clamp(r, s);
    return Samples(s.begin() + r.a * 2, s.begin() + r.b * 2);
}

Samples Remove(const Samples& s, Range r)
{
    r = Clamp(r, s);
    if (r.empty()) return s;
    const size_t n = Frames(s);

    Samples out;
    out.reserve((n - r.length()) * 2);
    out.insert(out.end(), s.begin(), s.begin() + r.a * 2);
    out.insert(out.end(), s.begin() + r.b * 2, s.end());

    // Crossfade the join: the frames just before the cut blend into the frames
    // just before the resume point, so the waveform meets without a click.
    const size_t x = std::min({ kJoinFrames, r.a, r.length() });
    for (size_t i = 0; i < x; i++)
    {
        const float t = SCurve((i + 1) / static_cast<float>(x + 1));
        const size_t o = r.a - x + i;        // position in `out`
        const size_t in = r.b - x + i;       // matching frame before the resume point
        for (int c = 0; c < 2; c++)
            out[o * 2 + c] = s[o * 2 + c] * (1.0f - t) + s[in * 2 + c] * t;
    }
    return out;
}

Samples Keep(const Samples& s, Range r)
{
    r = Clamp(r, s);
    Samples out = Extract(s, r);
    // Tiny fades so a cut through loud audio doesn't start/end with a click.
    const size_t n = Frames(out);
    const size_t f = std::min<size_t>(48, n / 2);
    if (r.a > 0) FadeIn(out, { 0, f });
    if (r.b < Frames(s)) FadeOut(out, { n - f, n });
    return out;
}

Samples Insert(const Samples& s, size_t at, const Samples& clip)
{
    at = std::min(at, Frames(s));
    Samples out;
    out.reserve(s.size() + clip.size());
    out.insert(out.end(), s.begin(), s.begin() + at * 2);
    out.insert(out.end(), clip.begin(), clip.end());
    out.insert(out.end(), s.begin() + at * 2, s.end());
    return out;
}

Samples Replace(const Samples& s, Range r, const Samples& clip)
{
    r = Clamp(r, s);
    Samples out;
    out.reserve(s.size() - r.length() * 2 + clip.size());
    out.insert(out.end(), s.begin(), s.begin() + r.a * 2);
    out.insert(out.end(), clip.begin(), clip.end());
    out.insert(out.end(), s.begin() + r.b * 2, s.end());
    return out;
}

void Silence(Samples& s, Range r)
{
    r = Clamp(r, s);
    std::fill(s.begin() + r.a * 2, s.begin() + r.b * 2, 0.0f);
}

void FadeIn(Samples& s, Range r)
{
    r = Clamp(r, s);
    const size_t n = r.length();
    for (size_t i = 0; i < n; i++)
    {
        const float g = SCurve(i / static_cast<float>(std::max<size_t>(n - 1, 1)));
        s[(r.a + i) * 2] *= g;
        s[(r.a + i) * 2 + 1] *= g;
    }
}

void FadeOut(Samples& s, Range r)
{
    r = Clamp(r, s);
    const size_t n = r.length();
    for (size_t i = 0; i < n; i++)
    {
        const float g = 1.0f - SCurve(i / static_cast<float>(std::max<size_t>(n - 1, 1)));
        s[(r.a + i) * 2] *= g;
        s[(r.a + i) * 2 + 1] *= g;
    }
}

void Gain(Samples& s, Range r, float db)
{
    r = Clamp(r, s);
    const float g = Lin(db);
    for (size_t i = r.a * 2; i < r.b * 2; i++) s[i] = std::clamp(s[i] * g, -1.0f, 1.0f);
}

float PeakDb(const Samples& s, Range r)
{
    r = Clamp(r, s);
    float pk = 0.0f;
    for (size_t i = r.a * 2; i < r.b * 2; i++) pk = std::max(pk, std::fabs(s[i]));
    return pk > 1e-9f ? 20.0f * std::log10(pk) : -200.0f;
}

void Normalize(Samples& s, Range r, float targetDb)
{
    const float pk = PeakDb(s, r);
    if (pk < -150.0f) return;   // silent
    Gain(s, r, targetDb - pk);
}

void Reverse(Samples& s, Range r)
{
    r = Clamp(r, s);
    if (r.empty()) return;
    size_t i = r.a, j = r.b - 1;
    while (i < j)
    {
        std::swap(s[i * 2], s[j * 2]);
        std::swap(s[i * 2 + 1], s[j * 2 + 1]);
        i++; j--;
    }
}

Range FindSound(const Samples& s, float thresholdDb, float padSeconds)
{
    const float thr = Lin(thresholdDb);
    const size_t n = Frames(s);
    size_t first = n, last = 0;
    for (size_t f = 0; f < n; f++)
        if (std::fabs(s[f * 2]) > thr || std::fabs(s[f * 2 + 1]) > thr) { first = f; break; }
    if (first == n) return {};
    for (size_t f = n; f-- > first;)
        if (std::fabs(s[f * 2]) > thr || std::fabs(s[f * 2 + 1]) > thr) { last = f + 1; break; }

    const size_t pad = static_cast<size_t>(padSeconds * kRate);
    return { first > pad ? first - pad : 0, std::min(n, last + pad) };
}

Peaks BuildPeaks(const Samples& s)
{
    Peaks p;
    const size_t n = Frames(s);
    const size_t blocks = (n + Peaks::kBlock - 1) / Peaks::kBlock;
    p.mn.resize(blocks);
    p.mx.resize(blocks);
    for (size_t b = 0; b < blocks; b++)
    {
        float lo = 0.0f, hi = 0.0f;
        const size_t end = std::min(n, (b + 1) * Peaks::kBlock);
        for (size_t f = b * Peaks::kBlock; f < end; f++)
        {
            lo = std::min({ lo, s[f * 2], s[f * 2 + 1] });
            hi = std::max({ hi, s[f * 2], s[f * 2 + 1] });
        }
        p.mn[b] = lo;
        p.mx[b] = hi;
    }
    return p;
}

// ---- History -------------------------------------------------------------------------
void History::Reset(Buffer initial)
{
    states_.clear();
    states_.push_back(std::move(initial));
    pos_ = 0;
}

void History::Push(Buffer next)
{
    while (states_.size() > pos_ + 1) states_.pop_back();   // drop redo
    states_.push_back(std::move(next));
    pos_ = states_.size() - 1;
    Trim();
}

Buffer History::Undo()
{
    if (CanUndo()) pos_--;
    return Current();
}

Buffer History::Redo()
{
    if (CanRedo()) pos_++;
    return Current();
}

void History::Trim()
{
    auto bytes = [this] {
        size_t t = 0;
        for (auto& b : states_) if (b) t += b->size() * sizeof(float);
        return t;
    };
    while (states_.size() > 2 && pos_ > 0 && bytes() > memoryLimitBytes)
    {
        states_.pop_front();
        pos_--;
    }
}

} // namespace mixcast::edit
