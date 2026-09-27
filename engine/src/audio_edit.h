// MixCast engine - audio editing operations (no Windows/Qt; unit-testable).
// Audio is interleaved stereo float at 48 kHz. Every operation returns or
// changes a buffer; the editor keeps old buffers for undo.
#pragma once

#include <cstddef>
#include <deque>
#include <memory>
#include <vector>

namespace mixcast::edit {

using Samples = std::vector<float>;
using Buffer  = std::shared_ptr<const Samples>;

constexpr int kRate = 48000;

struct Range
{
    size_t a = 0, b = 0;   // frames [a, b)
    bool   empty() const { return b <= a; }
    size_t length() const { return empty() ? 0 : b - a; }
};

inline size_t Frames(const Samples& s) { return s.size() / 2; }
Range  Clamp(Range r, const Samples& s);

// ---- Cut / copy / paste ----------------------------------------------------
Samples Extract(const Samples& s, Range r);                     // copy of the range
Samples Remove(const Samples& s, Range r);                      // delete range (click-free join)
Samples Keep(const Samples& s, Range r);                        // keep only the range
Samples Insert(const Samples& s, size_t at, const Samples& clip);
Samples Replace(const Samples& s, Range r, const Samples& clip);

// ---- Effects (in place, on a range) ---------------------------------------------
void  Silence(Samples& s, Range r);
void  FadeIn(Samples& s, Range r);                              // smooth S-curve
void  FadeOut(Samples& s, Range r);
void  Gain(Samples& s, Range r, float db);                      // clamps at +/-1
float PeakDb(const Samples& s, Range r);
void  Normalize(Samples& s, Range r, float targetDb = -1.0f);
void  Reverse(Samples& s, Range r);

// Where the sound actually starts/ends (first/last frame above threshold),
// with a little padding. Empty range if the whole thing is silent.
Range FindSound(const Samples& s, float thresholdDb = -50.0f, float padSeconds = 0.02f);

// ---- Waveform overview ------------------------------------------------------------
struct Peaks
{
    static constexpr size_t kBlock = 256;   // frames per block
    std::vector<float> mn, mx;              // per block, over both channels
};
Peaks BuildPeaks(const Samples& s);

// ---- Undo / redo ---------------------------------------------------------------------
class History
{
public:
    void   Reset(Buffer initial);
    void   Push(Buffer next);             // new edit: clears redo
    bool   CanUndo() const { return pos_ > 0; }
    bool   CanRedo() const { return pos_ + 1 < states_.size(); }
    Buffer Undo();
    Buffer Redo();
    Buffer Current() const { return states_.empty() ? nullptr : states_[pos_]; }

    // Keeps undo steps while they fit in this much memory (oldest dropped first).
    size_t memoryLimitBytes = 1024ull * 1024 * 1024;

private:
    void   Trim();
    std::deque<Buffer> states_;
    size_t             pos_ = 0;
};

} // namespace mixcast::edit
