// MixCast engine - mixes mic + app sources into the MixCast Input endpoint.
#include "mix_engine.h"
#include "devices.h"
#include "soundboard.h"

#include <algorithm>
#include <cmath>

namespace mixcast {

// Per-source buffering (see DriftReader): aim for 30 ms, never exceed 150 ms.
static constexpr size_t kSourceTargetFrames = kSampleRate * 30 / 1000;
static constexpr size_t kSourceMaxFrames    = kSampleRate * 150 / 1000;

// Ducking envelope: fast down, slow back up.
static const float kDuckAttack  = 1.0f - std::exp(-1.0f / (0.010f * kSampleRate));  // 10 ms
static const float kDuckRelease = 1.0f - std::exp(-1.0f / (0.250f * kSampleRate));  // 250 ms

// Transparent below 0.9, smoothly saturates to 1.0 above: no hard clipping.
static inline float SoftClip(float x)
{
    const float ax = std::fabs(x);
    if (ax <= 0.9f) return x;
    const float y = 0.9f + 0.1f * std::tanh((ax - 0.9f) / 0.1f);
    return std::copysign(y, x);
}

MixEngine::MixEngine(std::wstring outputDeviceId, int latencyMs)
    : outputId_(std::move(outputDeviceId)), latencyMs_(std::clamp(latencyMs, 10, 200))
{
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

MixEngine::~MixEngine()
{
    Stop();
    bLive_ = false;
    outputB_.reset();
    if (stopEvent_) CloseHandle(stopEvent_);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
void MixEngine::Start()
{
    if (running_.exchange(true)) return;
    {
        std::lock_guard<std::mutex> lk(inputsMu_);
        for (auto& in : inputs_) in->source->Start();
    }
    ResetEvent(stopEvent_);
    failed_ = false;
    thread_ = std::thread(&MixEngine::RenderThread, this);
}

void MixEngine::Stop()
{
    if (!running_.exchange(false)) return;
    SetEvent(stopEvent_);
    if (thread_.joinable()) thread_.join();

    std::vector<std::shared_ptr<Input>> copy;
    {
        std::lock_guard<std::mutex> lk(inputsMu_);
        copy = inputs_;
    }
    for (auto& in : copy) in->source->Stop();
}

// ---------------------------------------------------------------------------
// Live source management
// ---------------------------------------------------------------------------
SourceId MixEngine::AddSource(std::unique_ptr<CaptureSource> source, bool isMic, float gainDb, bool duckable)
{
    auto in = std::make_shared<Input>();
    in->isMic  = isMic;
    in->reader = std::make_unique<DriftReader>(source->Ring(), kSourceTargetFrames, kSourceMaxFrames);
    in->source = std::move(source);
    in->ctl.gainDb   = gainDb;
    in->ctl.duckable = duckable;
    if (isMic) in->source->EnableVoiceProcessing(&controls.voice);

    if (running_) in->source->Start();   // start capturing before it joins the mix

    std::lock_guard<std::mutex> lk(inputsMu_);
    in->id = nextId_++;
    if (isMic) inputs_.insert(inputs_.begin(), in);
    else       inputs_.push_back(in);
    return in->id;
}

void MixEngine::RemoveSource(SourceId id)
{
    std::shared_ptr<Input> victim;
    {
        std::lock_guard<std::mutex> lk(inputsMu_);
        auto it = std::find_if(inputs_.begin(), inputs_.end(), [&](auto& in) { return in->id == id; });
        if (it == inputs_.end()) return;
        victim = *it;
        inputs_.erase(it);
    }
    // Outside the lock: stopping joins the capture thread.
    victim->source->Stop();
}

SourceId MixEngine::ReplaceMic(std::unique_ptr<CaptureSource> newMic)
{
    float gain = 0.0f;
    bool  enabled = true;
    SourceId oldId = 0;
    {
        std::lock_guard<std::mutex> lk(inputsMu_);
        for (auto& in : inputs_)
            if (in->isMic) { oldId = in->id; gain = in->ctl.gainDb; enabled = in->ctl.enabled; break; }
    }
    if (oldId) RemoveSource(oldId);

    SourceId id = AddSource(std::move(newMic), true, gain, false);
    if (auto* c = Controls(id)) c->enabled = enabled;
    return id;
}

// ---------------------------------------------------------------------------
// Output B
// ---------------------------------------------------------------------------
void MixEngine::SetOutputB(const std::wstring& deviceId)
{
    if (deviceId == outputBWanted_ && (outputB_ || deviceId.empty())) return;
    outputBWanted_ = deviceId;
    bLive_ = false;
    outputB_.reset();
    UpdateOutputB();
}

bool MixEngine::OutputBActive() const
{
    return outputB_ && !outputB_->Failed();
}

void MixEngine::UpdateOutputB()
{
    if (outputBWanted_.empty()) return;

    std::wstring id = outputBWanted_;
    if (id == L"default")
    {
        std::optional<Endpoint> speakers;
        try { speakers = DefaultRealSpeakers(); } catch (...) {}
        if (!speakers) { bLive_ = false; outputB_.reset(); return; }
        id = speakers->id;
    }

    // (Re)open when missing, failed, or the default headphones changed.
    if (outputB_ && !outputB_->Failed() && outputB_->DeviceId() == id) return;
    bLive_ = false;
    outputB_.reset();
    outputB_ = std::make_unique<MonitorOutput>(id, ringB_);
    outputB_->Start();
    bLive_ = true;
}

void MixEngine::Maintain()
{
    UpdateOutputB();

    // Find app sources whose process has closed.
    std::vector<std::shared_ptr<Input>> closed;
    {
        std::lock_guard<std::mutex> lk(inputsMu_);
        for (auto& in : inputs_)
            if (!in->isMic && in->source->ProcessExited()) closed.push_back(in);
    }

    for (auto& in : closed)
    {
        SourceConfig cfg = in->source->Config();
        if (cfg.exeName.empty()) continue;

        auto pid = FindRootProcess(cfg.exeName);
        if (!pid || *pid == cfg.pid) continue;          // app not back yet

        // App is running again: attach a fresh capture, keep the same controls.
        cfg.pid = *pid;
        auto src = std::make_unique<CaptureSource>(cfg);
        auto rd  = std::make_unique<DriftReader>(src->Ring(), kSourceTargetFrames, kSourceMaxFrames);
        if (running_) src->Start();

        std::unique_ptr<CaptureSource> old;
        {
            std::lock_guard<std::mutex> lk(inputsMu_);
            old        = std::move(in->source);
            in->source = std::move(src);
            in->reader = std::move(rd);
        }
        old->Stop();
    }
}

SourceControls* MixEngine::Controls(SourceId id) const
{
    std::lock_guard<std::mutex> lk(inputsMu_);
    for (auto& in : inputs_)
        if (in->id == id) return &in->ctl;
    return nullptr;
}

std::string MixEngine::Error() const
{
    std::lock_guard<std::mutex> lk(errMu_);
    return error_;
}

std::vector<SourceStatus> MixEngine::Status() const
{
    std::lock_guard<std::mutex> lk(inputsMu_);
    std::vector<SourceStatus> out;
    for (auto& in : inputs_)
    {
        SourceStatus st;
        st.id         = in->id;
        st.label      = in->source->Config().label;
        st.isMic      = in->isMic;
        st.controls   = &in->ctl;
        st.bufferedMs = in->source->Ring().Available() * 1000.0f / kSampleRate;
        st.driftRatio = in->reader->ratioPublished.load();
        st.underruns  = in->reader->underruns.load();

        if (in->source->ProcessExited())  st.state = SourceState::WaitingForApp;
        else if (in->source->Failed())  { st.state = SourceState::Failed; st.error = in->source->Error(); }
        else                              st.state = SourceState::Running;

        out.push_back(std::move(st));
    }
    return out;
}

void MixEngine::RenderThread()
{
    ComScope com;
    MmcssScope mmcss;
    try
    {
        RunRender();
    }
    catch (const std::exception& ex)
    {
        {
            std::lock_guard<std::mutex> lk(errMu_);
            error_ = ex.what();
        }
        failed_ = true;
    }
}

void MixEngine::RunRender()
{
    ComPtr<IMMDeviceEnumerator> e;
    MC_CHECK(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&e)));
    ComPtr<IMMDevice> dev;
    MC_CHECK(e->GetDevice(outputId_.c_str(), &dev));
    ComPtr<IAudioClient> client;
    MC_CHECK(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client));

    // We always hand WASAPI 48 kHz stereo float; Windows converts if needed.
    WAVEFORMATEXTENSIBLE wf = MakeFormat(true);
    const REFERENCE_TIME bufferHns = static_cast<REFERENCE_TIME>(latencyMs_) * 10000;
    MC_CHECK(client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                                AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                bufferHns, 0, reinterpret_cast<WAVEFORMATEX*>(&wf), nullptr));

    EventHandle audioEvent;
    MC_CHECK(client->SetEventHandle(audioEvent.h));

    UINT32 bufferFrames = 0;
    MC_CHECK(client->GetBufferSize(&bufferFrames));

    ComPtr<IAudioRenderClient> render;
    MC_CHECK(client->GetService(IID_PPV_ARGS(&render)));

    micBuf_.assign(static_cast<size_t>(bufferFrames) * kChannels, 0.0f);
    for (auto* b : { &appBuf_, &freeBuf_, &tmpBuf_, &micB_, &appB_, &freeB_, &soloB_, &outB_ })
        b->assign(micBuf_.size(), 0.0f);

    // Pre-fill with silence so the first period doesn't glitch.
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
        HRESULT hr = client->GetCurrentPadding(&padding);
        if (hr == AUDCLNT_E_DEVICE_INVALIDATED)
            throw std::runtime_error("the virtual cable was removed or disabled");
        MC_CHECK(hr);

        if (padding == 0) meters.renderGlitches.fetch_add(1, std::memory_order_relaxed);

        const UINT32 frames = bufferFrames - padding;
        if (frames == 0) continue;

        MC_CHECK(render->GetBuffer(frames, &data));
        {
            std::lock_guard<std::mutex> lk(inputsMu_);   // held < 1 ms
            Mix(reinterpret_cast<float*>(data), frames);
        }
        MC_CHECK(render->ReleaseBuffer(frames, 0));
    }

    client->Stop();
}

void MixEngine::Mix(float* out, size_t n)
{
    const size_t samples = n * kChannels;
    const bool   feedB   = bLive_.load(std::memory_order_relaxed);

    // Buses, for A and for B: mic (drives ducking), duckable apps, the rest.
    for (auto* b : { &micBuf_, &appBuf_, &freeBuf_, &micB_, &appB_, &freeB_, &soloB_ })
        std::fill(b->begin(), b->begin() + samples, 0.0f);

    // Any channel soloed? Then B plays only the soloed ones.
    bool solo = false;
    for (auto& in : inputs_) solo = solo || (in->ctl.solo && in->ctl.enabled);
    Soundboard* sb = soundboard_.load();
    if (sb) solo = solo || (sb->Bus().solo && sb->Bus().enabled);

    double micSumSq = 0.0;

    // One channel, already at its fader level in tmpBuf_: tone, pan, width,
    // meter, then into the buses it's sent to.
    auto route = [&](SourceControls& c, ChannelDsp& dsp, float* busA, float* busB) {
        dsp.Process(tmpBuf_.data(), n, c);
        float pk = 0.0f;
        for (size_t i = 0; i < samples; i++) pk = std::max(pk, std::fabs(tmpBuf_[i]));
        c.peak.store(pk, std::memory_order_relaxed);

        if (c.sendA)
            for (size_t i = 0; i < samples; i++) busA[i] += tmpBuf_[i];
        if (!feedB) return;
        float* b = solo ? (c.solo ? soloB_.data() : nullptr) : (c.sendB ? busB : nullptr);
        if (b)
            for (size_t i = 0; i < samples; i++) b[i] += tmpBuf_[i];
    };

    for (auto& in : inputs_)
    {
        // Always pull, even when switched off, so the ring keeps draining
        // and switching back on is instant (no stale audio burst).
        in->reader->Pull(tmpBuf_.data(), n);

        if (!in->ctl.enabled)
        {
            in->ctl.peak.store(0.0f, std::memory_order_relaxed);
            continue;
        }

        const float g = DbToLin(in->ctl.gainDb);
        for (size_t i = 0; i < samples; i++) tmpBuf_[i] *= g;

        if (in->isMic)
        {
            route(in->ctl, in->dsp, micBuf_.data(), micB_.data());
            for (size_t i = 0; i < samples; i++) micSumSq += static_cast<double>(tmpBuf_[i]) * tmpBuf_[i];
        }
        else if (in->ctl.duckable) route(in->ctl, in->dsp, appBuf_.data(), appB_.data());
        else                       route(in->ctl, in->dsp, freeBuf_.data(), freeB_.data());
    }

    // Soundboard: its own channel, never ducked (Render applies its fader and on/off).
    if (sb)
    {
        sb->Render(tmpBuf_.data(), n);
        if (sb->Bus().enabled) route(sb->Bus(), sbDsp_, freeBuf_.data(), freeB_.data());
    }

    // Voice activity on the mic (wherever it's routed) -> ducking target.
    const float micRmsDb = LinToDb(static_cast<float>(std::sqrt(micSumSq / std::max<size_t>(samples, 1))));

    // Once the mic knows your voice, duck on your voice itself (keyboard,
    // clicks and other people don't count); until then, on the mic's level.
    const bool talking = controls.voice.profileInUse.load(std::memory_order_relaxed)
        ? controls.voice.speaking.load(std::memory_order_relaxed) && micRmsDb > -70.0f
        : micRmsDb > controls.duckThresholdDb;
    if (talking) holdLeft_ = controls.duckHoldMs * (kSampleRate / 1000.0f);
    else         holdLeft_ = std::max(0.0f, holdLeft_ - static_cast<float>(n));

    const float duckTarget = (controls.duckEnabled && holdLeft_ > 0.0f) ? DbToLin(controls.duckDepthDb) : 1.0f;
    const float master     = DbToLin(controls.masterGainDb);
    const float masterB    = DbToLin(controls.masterBGainDb);

    float outPk = 0.0f, outBPk = 0.0f;
    for (size_t f = 0; f < n; f++)
    {
        const float coef = (duckTarget < duckGain_) ? kDuckAttack : kDuckRelease;
        duckGain_ += (duckTarget - duckGain_) * coef;

        for (size_t ch = 0; ch < kChannels; ch++)
        {
            const size_t i = f * kChannels + ch;
            const float y = SoftClip(master * (micBuf_[i] + appBuf_[i] * duckGain_ + freeBuf_[i]));
            out[i] = y;
            outPk = std::max(outPk, std::fabs(y));

            if (feedB)
            {
                // A soloed channel is heard as it is: no ducking, so you can judge it.
                const float b = solo ? soloB_[i] : micB_[i] + appB_[i] * duckGain_ + freeB_[i];
                outB_[i] = SoftClip(masterB * b);
                outBPk = std::max(outBPk, std::fabs(outB_[i]));
            }
        }
    }
    if (feedB) ringB_.Write(outB_.data(), n);

    meters.outPeak.store(outPk, std::memory_order_relaxed);
    meters.outBPeak.store(outBPk, std::memory_order_relaxed);
    meters.soloOn.store(solo, std::memory_order_relaxed);
    meters.duckGain.store(duckGain_, std::memory_order_relaxed);
    meters.talking.store(talking, std::memory_order_relaxed);
}

} // namespace mixcast
