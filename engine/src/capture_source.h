// MixCast engine - one WASAPI capture stream feeding a ring buffer.
#pragma once

#include "common.h"
#include "ring_buffer.h"
#include "voice_processor.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace mixcast {

enum class SourceKind
{
    Microphone,        // a real recording device
    EndpointLoopback,  // everything playing on a playback device
    ProcessLoopback,   // one app (and its child processes), Win10 2004+
};

struct SourceConfig
{
    SourceKind   kind = SourceKind::Microphone;
    std::wstring deviceId;      // Microphone / EndpointLoopback
    DWORD        pid = 0;       // ProcessLoopback
    std::wstring exeName;       // ProcessLoopback: used to re-attach when the app restarts
    std::string  label;         // shown in the UI
};

class CaptureSource
{
public:
    explicit CaptureSource(SourceConfig cfg);
    ~CaptureSource();

    CaptureSource(const CaptureSource&) = delete;
    CaptureSource& operator=(const CaptureSource&) = delete;

    void Start();
    void Stop();

    // Mic only: run noise suppression / gate / rumble filter on this source.
    // Call before Start(). `settings` must outlive this source.
    void EnableVoiceProcessing(VoiceSettings* settings);

    StereoRing&         Ring()   { return ring_; }
    const SourceConfig& Config() const { return cfg_; }

    bool        Failed() const { return failed_.load(); }
    bool        ProcessExited() const;   // ProcessLoopback only: target app has closed
    std::string Error() const  { std::lock_guard<std::mutex> lk(errMu_); return error_; }

    std::atomic<uint64_t> droppedFrames{0};  // ring was full (consumer stalled)

private:
    void ThreadMain();
    void RunStream();
    ComPtr<IAudioClient> ActivateProcessLoopback();
    ComPtr<IAudioClient> ActivateDevice();
    void InitClient(ComPtr<IAudioClient>& client, HANDLE event);
    void ConvertToStereoFloat(const BYTE* data, UINT32 frames, float* out) const;

    SourceConfig      cfg_;
    StereoRing        ring_;
    std::thread       thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> failed_{false};
    mutable std::mutex errMu_;
    std::string       error_;
    HANDLE            stopEvent_ = nullptr;
    HANDLE            process_   = nullptr;   // SYNCHRONIZE handle to the captured app

    VoiceSettings*                  voice_ = nullptr;
    std::unique_ptr<VoiceProcessor> voiceProc_;

    // Actual stream format after Initialize.
    WORD  fmtChannels_  = kChannels;
    bool  fmtFloat_     = true;
    WORD  fmtBlockAlign_ = 8;
};

} // namespace mixcast
