// MixCast engine - plays audio on your own headphones (editor preview).
#include "preview_player.h"
#include "devices.h"

#include <algorithm>

namespace mixcast {

PreviewPlayer::PreviewPlayer()
{
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

PreviewPlayer::~PreviewPlayer()
{
    Stop();
    if (stopEvent_) CloseHandle(stopEvent_);
}

void PreviewPlayer::Stop()
{
    stop_ = true;
    SetEvent(stopEvent_);
    if (thread_.joinable()) thread_.join();
    playing_ = false;
}

bool PreviewPlayer::Play(AudioBuffer audio, size_t startFrame, size_t endFrame, bool loop, std::string& error)
{
    Stop();

    const size_t frames = audio ? audio->size() / 2 : 0;
    endFrame = std::min(endFrame, frames);
    if (startFrame >= endFrame) { error = "Nothing to play"; return false; }

    // Your headphones/speakers; never a virtual cable.
    std::optional<Endpoint> dev;
    try { dev = DefaultRealSpeakers(); } catch (...) {}
    if (!dev)
    {
        for (auto& ep : ListEndpoints(eRender))
            if (!IsVirtualCable(ep.name)) { dev = ep; break; }
    }
    if (!dev) { error = "No headphones or speakers found"; return false; }

    audio_ = std::move(audio);
    start_ = startFrame;
    end_   = endFrame;
    loop_  = loop;
    heard_ = startFrame;
    stop_  = false;
    playing_ = true;
    ResetEvent(stopEvent_);

    thread_ = std::thread([this, id = dev->id] {
        ComScope com;
        MmcssScope mmcss;
        try { Run(id); } catch (...) {}
        playing_ = false;
    });
    return true;
}

void PreviewPlayer::Run(std::wstring deviceId)
{
    ComPtr<IMMDeviceEnumerator> e;
    MC_CHECK(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&e)));
    ComPtr<IMMDevice> dev;
    MC_CHECK(e->GetDevice(deviceId.c_str(), &dev));
    ComPtr<IAudioClient> client;
    MC_CHECK(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client));

    WAVEFORMATEXTENSIBLE wf = MakeFormat(true);
    MC_CHECK(client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                400000, 0, reinterpret_cast<WAVEFORMATEX*>(&wf), nullptr));   // 40 ms

    EventHandle audioEvent;
    MC_CHECK(client->SetEventHandle(audioEvent.h));
    UINT32 bufferFrames = 0;
    MC_CHECK(client->GetBufferSize(&bufferFrames));
    ComPtr<IAudioRenderClient> render;
    MC_CHECK(client->GetService(IID_PPV_ARGS(&render)));

    const float* src = audio_->data();
    size_t pos = start_;
    bool   finished = false;

    auto fill = [&](float* out, UINT32 n) {
        for (UINT32 i = 0; i < n; i++)
        {
            if (pos >= end_)
            {
                if (loop_) pos = start_;
                else { finished = true; out[i * 2] = out[i * 2 + 1] = 0.0f; continue; }
            }
            out[i * 2]     = src[pos * 2];
            out[i * 2 + 1] = src[pos * 2 + 1];
            pos++;
        }
    };

    BYTE* data = nullptr;
    MC_CHECK(render->GetBuffer(bufferFrames, &data));
    fill(reinterpret_cast<float*>(data), bufferFrames);
    MC_CHECK(render->ReleaseBuffer(bufferFrames, 0));
    MC_CHECK(client->Start());

    const HANDLE waits[2] = { audioEvent.h, stopEvent_ };
    while (!stop_)
    {
        DWORD w = WaitForMultipleObjects(2, waits, FALSE, 200);
        if (w == WAIT_OBJECT_0 + 1) break;

        UINT32 padding = 0;
        MC_CHECK(client->GetCurrentPadding(&padding));

        // What you hear now = what we've written minus what's still queued.
        if (!finished)
        {
            const size_t queued = padding;
            heard_ = (pos >= start_ + queued) ? pos - queued : start_;
        }
        else if (padding == 0)
        {
            heard_ = end_;
            break;                                 // played to the end
        }

        const UINT32 frames = bufferFrames - padding;
        if (frames == 0 || w != WAIT_OBJECT_0) continue;
        MC_CHECK(render->GetBuffer(frames, &data));
        fill(reinterpret_cast<float*>(data), frames);
        MC_CHECK(render->ReleaseBuffer(frames, 0));
    }
    client->Stop();
}

} // namespace mixcast
