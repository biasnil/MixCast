// MixCast engine - mixes any number of live sources into the MixCast Input endpoint.
//
// Sources can be added, removed, switched on/off and re-leveled WHILE running.
// The mic is the "main" channel: it drives ducking. App sources (Spotify,
// Soundpad, a game...) each have their own gain, on/off and duck setting.
//
// Two outputs, like a broadcast desk:
//   A  the virtual cable (what Discord hears) - the render thread's clock
//   B  any playback device: your headphones, or a second cable for OBS.
//      Each channel picks A and/or B; "solo" puts one channel alone on B
//      to check it, without touching A.
#pragma once

#include "capture_source.h"
#include "channel_dsp.h"
#include "drift_reader.h"
#include "ring_buffer.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace mixcast {

class Soundboard;
class MonitorOutput;
using SourceId = uint32_t;

// Global settings - safe to change from any thread while running.
struct MixControls
{
    std::atomic<bool>  duckEnabled{true};        // master ducking switch
    std::atomic<float> duckDepthDb{-12.0f};      // how far apps drop while you talk
    std::atomic<float> duckThresholdDb{-40.0f};  // mic level that counts as "talking"
    std::atomic<float> duckHoldMs{300.0f};       // stay ducked this long after you stop
    std::atomic<float> masterGainDb{0.0f};
    std::atomic<float> masterBGainDb{0.0f};      // output B level

    VoiceSettings      voice;                    // mic noise suppression / gate / rumble
};

struct MixMeters
{
    std::atomic<float>    outPeak{0.0f};   // after limiter, linear
    std::atomic<float>    outBPeak{0.0f};  // output B
    std::atomic<bool>     soloOn{false};   // some channel is soloed (B plays only those)
    std::atomic<float>    duckGain{1.0f};  // 1.0 = not ducked
    std::atomic<bool>     talking{false};  // voice detected on the mic
    std::atomic<uint32_t> renderGlitches{0};
};

enum class SourceState { Running, WaitingForApp, Failed };

struct SourceStatus
{
    SourceId        id = 0;
    std::string     label;
    bool            isMic = false;
    SourceState     state = SourceState::Running;
    std::string     error;
    float           bufferedMs = 0.0f;
    double          driftRatio = 1.0;
    uint32_t        underruns = 0;
    SourceControls* controls = nullptr;   // valid until RemoveSource(id)
};

class MixEngine
{
public:
    // outputDeviceId: the MixCast Input endpoint. latencyMs: render buffer size.
    MixEngine(std::wstring outputDeviceId, int latencyMs = 20);
    ~MixEngine();

    void Start();
    void Stop();
    bool IsRunning() const { return running_.load(); }

    // ---- Live source management (call from one UI thread) ----------------
    // The source is started immediately if the engine is running.
    SourceId AddSource(std::unique_ptr<CaptureSource> source, bool isMic,
                       float gainDb = 0.0f, bool duckable = true);
    void     RemoveSource(SourceId id);

    // Swap the microphone device, keeping its gain / on-off settings.
    SourceId ReplaceMic(std::unique_ptr<CaptureSource> newMic);

    // Call ~once a second from the UI thread: re-attaches app sources whose
    // app was closed and has been started again (same exe name).
    void     Maintain();

    std::vector<SourceStatus> Status() const;
    SourceControls*           Controls(SourceId id) const;

    // Output B (UI thread). "" = off, "default" = your default headphones
    // (followed as Windows' default changes), else a playback endpoint id.
    void         SetOutputB(const std::wstring& deviceId);
    std::wstring OutputB() const { return outputBWanted_; }
    bool         OutputBActive() const;

    // Adds the soundboard bus to the mix (never ducked). It must outlive the
    // engine, or be detached with nullptr first.
    void SetSoundboard(Soundboard* sb) { soundboard_.store(sb); }

    bool        Failed() const { return failed_.load(); }
    std::string Error() const;

    MixControls controls;
    MixMeters   meters;

private:
    struct Input
    {
        SourceId                       id = 0;
        bool                           isMic = false;
        std::unique_ptr<CaptureSource> source;
        std::unique_ptr<DriftReader>   reader;
        SourceControls                 ctl;
        ChannelDsp                     dsp;   // render thread only
    };

    void RenderThread();
    void RunRender();
    void Mix(float* out, size_t frames);   // called with inputsMu_ held

    std::wstring                        outputId_;
    int                                 latencyMs_;

    mutable std::mutex                  inputsMu_;   // guards inputs_ (render thread + UI)
    std::vector<std::shared_ptr<Input>> inputs_;
    SourceId                            nextId_ = 1;
    std::atomic<Soundboard*>            soundboard_{nullptr};

    std::thread        thread_;
    std::atomic<bool>  running_{false};
    std::atomic<bool>  failed_{false};
    mutable std::mutex errMu_;
    std::string        error_;
    HANDLE             stopEvent_ = nullptr;

    void UpdateOutputB();   // UI thread: open / follow / close the B device

    // Output B. The ring is declared first so it outlives the device that reads it.
    StereoRing                     ringB_{kSampleRate};
    std::wstring                   outputBWanted_;
    std::unique_ptr<MonitorOutput> outputB_;
    std::atomic<bool>              bLive_{false};   // render thread feeds ringB_

    // Render-thread-only state.
    std::vector<float> micBuf_, appBuf_, freeBuf_, tmpBuf_;   // output A buses
    std::vector<float> micB_, appB_, freeB_, soloB_, outB_;   // output B buses
    ChannelDsp         sbDsp_;                                // soundboard channel
    float              duckGain_ = 1.0f;
    float              holdLeft_ = 0.0f;   // samples
};

} // namespace mixcast
