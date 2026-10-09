// MixCast engine - built-in soundboard.
#include "soundboard.h"
#include "devices.h"
#include "mix_engine.h"

#include "audio_decoder.h"
#include "resampler.h"

#include <algorithm>
#include <cmath>

namespace mixcast {

// ============================================================================
// Decoding
// ============================================================================
std::shared_ptr<const Clip> DecodeAudioFile(const std::wstring& path, std::string& error)
{
    std::vector<float> at48k;
    if (!DecodeStereo48k(path, kMaxClipSeconds, at48k, error)) return nullptr;

    auto clip = std::make_shared<Clip>();
    clip->frames = at48k.size() / 2;
    clip->samples.resize(at48k.size());
    for (size_t i = 0; i < at48k.size(); i++)
        clip->samples[i] = static_cast<int16_t>(std::lround(std::clamp(at48k[i], -1.0f, 1.0f) * 32767.0f));
    return clip;
}

// ============================================================================
// MonitorOutput
// ============================================================================
MonitorOutput::MonitorOutput(std::wstring deviceId, StereoRing& ring)
    : deviceId_(std::move(deviceId)), ring_(ring)
{
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

MonitorOutput::~MonitorOutput()
{
    Stop();
    if (stopEvent_) CloseHandle(stopEvent_);
}

void MonitorOutput::Start()
{
    if (running_.exchange(true)) return;
    ResetEvent(stopEvent_);
    thread_ = std::thread([this] {
        ComScope com;
        MmcssScope mmcss;
        try { Run(); } catch (...) { failed_ = true; }
    });
}

void MonitorOutput::Stop()
{
    if (!running_.exchange(false)) return;
    SetEvent(stopEvent_);
    if (thread_.joinable()) thread_.join();
}

void MonitorOutput::Run()
{
    ComPtr<IMMDeviceEnumerator> e;
    MC_CHECK(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&e)));
    ComPtr<IMMDevice> dev;
    MC_CHECK(e->GetDevice(deviceId_.c_str(), &dev));
    ComPtr<IAudioClient> client;
    MC_CHECK(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client));

    WAVEFORMATEXTENSIBLE wf = MakeFormat(true);
    MC_CHECK(client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                300000, 0, reinterpret_cast<WAVEFORMATEX*>(&wf), nullptr));   // 30 ms

    EventHandle audioEvent;
    MC_CHECK(client->SetEventHandle(audioEvent.h));
    UINT32 bufferFrames = 0;
    MC_CHECK(client->GetBufferSize(&bufferFrames));
    ComPtr<IAudioRenderClient> render;
    MC_CHECK(client->GetService(IID_PPV_ARGS(&render)));

    // Your headphones run on a different clock than the cable: compensate.
    DriftReader reader(ring_, kSampleRate * 30 / 1000, kSampleRate * 150 / 1000);

    BYTE* data = nullptr;
    MC_CHECK(render->GetBuffer(bufferFrames, &data));
    MC_CHECK(render->ReleaseBuffer(bufferFrames, AUDCLNT_BUFFERFLAGS_SILENT));
    MC_CHECK(client->Start());

    const HANDLE waits[2] = { audioEvent.h, stopEvent_ };
    while (running_)
    {
        DWORD w = WaitForMultipleObjects(2, waits, FALSE, 200);
        if (w == WAIT_OBJECT_0 + 1) break;
        if (w != WAIT_OBJECT_0) continue;

        UINT32 padding = 0;
        MC_CHECK(client->GetCurrentPadding(&padding));
        const UINT32 frames = bufferFrames - padding;
        if (frames == 0) continue;

        MC_CHECK(render->GetBuffer(frames, &data));
        reader.Pull(reinterpret_cast<float*>(data), frames);
        MC_CHECK(render->ReleaseBuffer(frames, 0));
    }
    client->Stop();
}

// ============================================================================
// Soundboard
// ============================================================================
static constexpr float kStopFadePerFrame = 1.0f / (0.012f * kSampleRate);   // 12 ms fade-out

Soundboard::Soundboard()
    : bus_(std::make_unique<SourceControls>()), monitorRing_(kSampleRate)
{
    voices_.reserve(64);
    retired_.reserve(256);
    monitorBuf_.resize(kSampleRate * kChannels);
}

Soundboard::~Soundboard()
{
    monitor_.reset();
}

ClipId Soundboard::AddClip(std::shared_ptr<const Clip> clip, float gainDb)
{
    std::lock_guard<std::mutex> lk(mu_);
    const ClipId id = nextId_++;
    clips_[id] = Entry{ std::move(clip), DbToLin(gainDb), false, kStopFadePerFrame };
    return id;
}

void Soundboard::RemoveClip(ClipId id)
{
    std::shared_ptr<const Clip> keep;   // freed after unlocking
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = clips_.find(id);
        if (it == clips_.end()) return;
        keep = it->second.clip;
        clips_.erase(it);
        for (auto& v : voices_) if (v.id == id) v.stopping = true;
    }
}

void Soundboard::SetClipGain(ClipId id, float gainDb)
{
    std::lock_guard<std::mutex> lk(mu_);
    auto it = clips_.find(id);
    if (it == clips_.end()) return;
    it->second.gain = DbToLin(gainDb);
    for (auto& v : voices_) if (v.id == id) v.gain = it->second.gain;
}

void Soundboard::SetClipOptions(ClipId id, bool loop, float fadeOutSec)
{
    std::lock_guard<std::mutex> lk(mu_);
    auto it = clips_.find(id);
    if (it == clips_.end()) return;
    it->second.loop = loop;
    it->second.fadeStep = fadeOutSec > 0.02f ? 1.0f / (fadeOutSec * kSampleRate) : kStopFadePerFrame;
    for (auto& v : voices_) if (v.id == id && !v.stopping) v.loop = loop;
}

void Soundboard::Play(ClipId id)
{
    std::lock_guard<std::mutex> lk(mu_);
    auto it = clips_.find(id);
    if (it == clips_.end()) return;

    for (auto& v : voices_)
        if (v.id == id || oneAtATime)   // restart this one / stop the rest: always quickly
        {
            v.stopping = true;
            v.fadeStep = kStopFadePerFrame;
        }

    if (voices_.size() < voices_.capacity())                // never allocate under the render lock
        voices_.push_back(Voice{ id, it->second.clip, 0, it->second.gain, 1.0f, false, it->second.loop, 0.0f });
}

void Soundboard::Stop(ClipId id)
{
    std::lock_guard<std::mutex> lk(mu_);
    auto it = clips_.find(id);
    const float step = it != clips_.end() ? it->second.fadeStep : kStopFadePerFrame;
    for (auto& v : voices_)
        if (v.id == id && !v.stopping) { v.stopping = true; v.fadeStep = step; }
}

void Soundboard::Toggle(ClipId id)
{
    bool playing = false;
    {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& v : voices_) if (v.id == id && !v.stopping) playing = true;
    }
    if (playing) Stop(id); else Play(id);
}

void Soundboard::StopAll()
{
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& v : voices_)
    {
        if (v.stopping) continue;
        auto it = clips_.find(v.id);
        v.stopping = true;
        v.fadeStep = it != clips_.end() ? it->second.fadeStep : kStopFadePerFrame;
    }
}

std::vector<ClipPlayback> Soundboard::Playing() const
{
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<ClipPlayback> out;
    for (auto& v : voices_)
        if (!v.stopping && v.clip->frames > 0)
            out.push_back({ v.id, static_cast<float>(v.pos) / static_cast<float>(v.clip->frames) });
    return out;
}

void Soundboard::Maintain()
{
    // Free finished sounds here, not on the audio thread.
    std::vector<std::shared_ptr<const Clip>> drop;
    {
        std::lock_guard<std::mutex> lk(mu_);
        drop.swap(retired_);
        retired_.reserve(256);
    }
    drop.clear();

    // Follow the "hear it myself" switch at once; re-check the default
    // headphones every 8th call (~2 s when called every 250 ms).
    const bool periodic = (maintainCount_++ % 8 == 0);

    if (!monitorEnabled) { monitor_.reset(); return; }
    if (monitor_ && !monitor_->Failed() && !periodic) return;
    if (!monitor_ && !periodic && maintainCount_ > 1) return;

    std::optional<Endpoint> speakers;
    try { speakers = DefaultRealSpeakers(); } catch (...) {}

    const bool wrongDevice = monitor_ && (!speakers || monitor_->DeviceId() != speakers->id);
    if (monitor_ && (monitor_->Failed() || wrongDevice)) monitor_.reset();

    if (!monitor_ && speakers)
    {
        monitor_ = std::make_unique<MonitorOutput>(speakers->id, monitorRing_);
        monitor_->Start();
    }
}

void Soundboard::Render(float* out, size_t frames)
{
    const size_t samples = frames * kChannels;
    std::fill(out, out + samples, 0.0f);

    {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& v : voices_)
        {
            const int16_t* src = v.clip->samples.data();
            for (size_t f = 0; f < frames; f++, v.pos++)
            {
                if (v.pos >= v.clip->frames)
                {
                    if (!v.loop || v.clip->frames == 0) break;
                    v.pos = 0;   // loop: straight back to the start (also while fading out)
                }
                if (v.stopping)
                {
                    v.fade -= v.fadeStep > 0.0f ? v.fadeStep : kStopFadePerFrame;
                    if (v.fade <= 0.0f) { v.fade = 0.0f; break; }
                }
                const float g = v.gain * v.fade * (1.0f / 32768.0f);
                out[f * 2]     += src[v.pos * 2] * g;
                out[f * 2 + 1] += src[v.pos * 2 + 1] * g;
            }
        }

        // Drop voices that ended or finished fading; their clips are freed later.
        for (size_t i = 0; i < voices_.size();)
        {
            Voice& v = voices_[i];
            const bool ended = v.pos >= v.clip->frames && !(v.loop && v.clip->frames > 0);
            if (ended || v.fade <= 0.0f)
            {
                if (retired_.size() < retired_.capacity()) retired_.push_back(std::move(v.clip));
                voices_[i] = std::move(voices_.back());
                voices_.pop_back();
            }
            else i++;
        }
    }

    // Copy for your headphones (independent volume), before the bus fader.
    const bool on = bus_->enabled;
    if (monitorEnabled && on && samples <= monitorBuf_.size())
    {
        const float mg = DbToLin(monitorGainDb);
        for (size_t i = 0; i < samples; i++) monitorBuf_[i] = std::clamp(out[i] * mg, -1.0f, 1.0f);
        monitorRing_.Write(monitorBuf_.data(), frames);
    }

    // Bus fader / on-off / meter for the mix.
    const float g = on ? DbToLin(bus_->gainDb) : 0.0f;
    float pk = 0.0f;
    for (size_t i = 0; i < samples; i++)
    {
        out[i] *= g;
        pk = std::max(pk, std::fabs(out[i]));
    }
    bus_->peak.store(pk, std::memory_order_relaxed);
}

} // namespace mixcast
