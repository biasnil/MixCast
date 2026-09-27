// MixCast engine - plays audio on your own headphones (editor preview).
// Never goes to Discord: it uses your default playback device, and refuses
// virtual cables.
#pragma once

#include "common.h"

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace mixcast {

using AudioBuffer = std::shared_ptr<const std::vector<float>>;   // stereo, 48 kHz

class PreviewPlayer
{
public:
    PreviewPlayer();
    ~PreviewPlayer();

    // Plays frames [start, end) of `audio`. Restarts if already playing.
    bool   Play(AudioBuffer audio, size_t startFrame, size_t endFrame, bool loop, std::string& error);
    void   Stop();
    void   SetLoop(bool loop) { loop_ = loop; }

    bool   IsPlaying() const { return playing_.load(); }
    size_t Position() const  { return heard_.load(); }   // frame you're hearing now

private:
    void Run(std::wstring deviceId);

    AudioBuffer         audio_;
    size_t              start_ = 0, end_ = 0;
    std::atomic<bool>   loop_{false};
    std::atomic<bool>   playing_{false};
    std::atomic<bool>   stop_{false};
    std::atomic<size_t> heard_{0};
    std::thread         thread_;
    HANDLE              stopEvent_ = nullptr;
};

} // namespace mixcast
