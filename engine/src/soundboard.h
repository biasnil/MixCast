// MixCast engine - built-in soundboard.
//
// Sounds are decoded once (Windows Media Foundation: MP3, WAV, M4A/AAC, WMA,
// FLAC) into 48 kHz stereo and kept in memory, so a hotkey plays instantly.
// The soundboard is a bus inside the mix: never ducked, with its own fader.
// A copy can also play on your own headphones ("hear it myself").
#pragma once

#include "common.h"
#include "drift_reader.h"
#include "ring_buffer.h"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace mixcast {

struct SourceControls;   // mix_engine.h

// A decoded sound: 48 kHz, stereo, interleaved 16-bit (half the memory of float).
struct Clip
{
    std::vector<int16_t> samples;
    size_t               frames = 0;
    double Seconds() const { return static_cast<double>(frames) / kSampleRate; }
};

// Decodes any file Media Foundation can read. Call from any thread that has
// COM initialised. Returns nullptr and fills `error` on failure.
std::shared_ptr<const Clip> DecodeAudioFile(const std::wstring& path, std::string& error);

constexpr double kMaxClipSeconds = 10 * 60;

// ---------------------------------------------------------------------------
// Plays a ring buffer on a real playback device (your headphones).
// ---------------------------------------------------------------------------
class MonitorOutput
{
public:
    MonitorOutput(std::wstring deviceId, StereoRing& ring);
    ~MonitorOutput();

    void Start();
    void Stop();
    bool Failed() const { return failed_.load(); }
    const std::wstring& DeviceId() const { return deviceId_; }

private:
    void Run();

    std::wstring      deviceId_;
    StereoRing&       ring_;
    std::thread       thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> failed_{false};
    HANDLE            stopEvent_ = nullptr;
};

// ---------------------------------------------------------------------------
using ClipId = uint32_t;

struct ClipPlayback
{
    ClipId id = 0;
    float  progress = 0.0f;   // 0..1
};

class Soundboard
{
public:
    Soundboard();
    ~Soundboard();

    // ---- UI thread ---------------------------------------------------------
    ClipId AddClip(std::shared_ptr<const Clip> clip, float gainDb = 0.0f);
    void   RemoveClip(ClipId id);
    void   SetClipGain(ClipId id, float gainDb);

    void   Play(ClipId id);      // restarts if already playing
    void   Stop(ClipId id);      // short fade, no click
    void   Toggle(ClipId id);
    void   StopAll();

    std::vector<ClipPlayback> Playing() const;

    // Call a few times a second: frees finished sounds, follows the
    // "hear it myself" switch and default-device changes.
    void   Maintain();

    std::atomic<bool>  oneAtATime{false};     // a new sound stops the others
    std::atomic<bool>  monitorEnabled{true};  // also play on your headphones
    std::atomic<float> monitorGainDb{-6.0f};
    bool   MonitorActive() const { return monitor_ && !monitor_->Failed(); }

    SourceControls&    Bus() { return *bus_; }   // fader, on/off, meter

    // ---- Render thread (called by MixEngine) -------------------------------
    // Overwrites out[frames * 2] with the soundboard bus.
    void   Render(float* out, size_t frames);

private:
    struct Entry { std::shared_ptr<const Clip> clip; float gain = 1.0f; };
    struct Voice
    {
        ClipId                      id = 0;
        std::shared_ptr<const Clip> clip;
        size_t                      pos = 0;
        float                       gain = 1.0f;
        float                       fade = 1.0f;
        bool                        stopping = false;
    };

    mutable std::mutex                       mu_;       // clips_, voices_, retired_
    std::map<ClipId, Entry>                  clips_;
    std::vector<Voice>                       voices_;
    std::vector<std::shared_ptr<const Clip>> retired_;  // freed on the UI thread
    ClipId                                   nextId_ = 1;

    std::unique_ptr<SourceControls>          bus_;
    StereoRing                               monitorRing_;
    std::unique_ptr<MonitorOutput>           monitor_;
    std::vector<float>                       monitorBuf_;
    int                                      maintainCount_ = 0;
};

} // namespace mixcast
