// MixCast engine - one WASAPI capture stream feeding a ring buffer.
#include "capture_source.h"

#include <vector>

// Process loopback declarations ship in Windows SDK 10.0.20348+.
// The fallback lets older SDKs / MinGW build too (same ABI as the SDK header).
#if __has_include(<audioclientactivationparams.h>)
#include <audioclientactivationparams.h>
#else
typedef enum AUDIOCLIENT_ACTIVATION_TYPE {
    AUDIOCLIENT_ACTIVATION_TYPE_DEFAULT = 0,
    AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK = 1
} AUDIOCLIENT_ACTIVATION_TYPE;
typedef enum PROCESS_LOOPBACK_MODE {
    PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE = 0,
    PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE = 1
} PROCESS_LOOPBACK_MODE;
typedef struct AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS {
    DWORD TargetProcessId;
    PROCESS_LOOPBACK_MODE ProcessLoopbackMode;
} AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS;
typedef struct AUDIOCLIENT_ACTIVATION_PARAMS {
    AUDIOCLIENT_ACTIVATION_TYPE ActivationType;
    union { AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS ProcessLoopbackParams; };
} AUDIOCLIENT_ACTIVATION_PARAMS;
#define VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK L"VAD\\Process_Loopback"
#endif

namespace mixcast {

// Ring holds up to 1 s per source; DriftReader keeps actual latency ~30 ms.
static constexpr size_t kRingFrames = kSampleRate;

// 20 ms WASAPI capture buffer.
static constexpr REFERENCE_TIME kCaptureBufferHns = 200000;

// ---------------------------------------------------------------------------
// Completion handler for ActivateAudioInterfaceAsync. Must be agile
// (callable from any thread), hence IAgileObject.
// ---------------------------------------------------------------------------
class ActivationHandler final : public IActivateAudioInterfaceCompletionHandler, public IAgileObject
{
public:
    ActivationHandler() : done_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IActivateAudioInterfaceCompletionHandler))
            *ppv = static_cast<IActivateAudioInterfaceCompletionHandler*>(this);
        else if (riid == __uuidof(IAgileObject))
            *ppv = static_cast<IAgileObject*>(this);
        else { *ppv = nullptr; return E_NOINTERFACE; }
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs_); }
    STDMETHODIMP_(ULONG) Release() override
    {
        ULONG r = InterlockedDecrement(&refs_);
        if (r == 0) delete this;
        return r;
    }

    // IActivateAudioInterfaceCompletionHandler
    STDMETHODIMP ActivateCompleted(IActivateAudioInterfaceAsyncOperation* op) override
    {
        HRESULT hrActivate = E_FAIL;
        ComPtr<IUnknown> unk;
        hr_ = op->GetActivateResult(&hrActivate, &unk);
        if (SUCCEEDED(hr_)) hr_ = hrActivate;
        if (SUCCEEDED(hr_)) hr_ = unk.As(&client_);
        SetEvent(done_);
        return S_OK;
    }

    HANDLE DoneEvent() const { return done_; }
    HRESULT Result() const { return hr_; }
    ComPtr<IAudioClient> Client() const { return client_; }

private:
    ~ActivationHandler() { if (done_) CloseHandle(done_); }

    LONG                 refs_ = 1;
    HANDLE               done_;
    HRESULT              hr_ = E_PENDING;
    ComPtr<IAudioClient> client_;
};

// ---------------------------------------------------------------------------
CaptureSource::CaptureSource(SourceConfig cfg)
    : cfg_(std::move(cfg)), ring_(kRingFrames)
{
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (cfg_.kind == SourceKind::ProcessLoopback)
        process_ = OpenProcess(SYNCHRONIZE, FALSE, cfg_.pid);
}

CaptureSource::~CaptureSource()
{
    Stop();
    if (stopEvent_) CloseHandle(stopEvent_);
    if (process_) CloseHandle(process_);
}

void CaptureSource::EnableVoiceProcessing(VoiceSettings* settings)
{
    voice_ = settings;
    voiceProc_ = settings ? std::make_unique<VoiceProcessor>() : nullptr;
}

bool CaptureSource::ProcessExited() const
{
    if (cfg_.kind != SourceKind::ProcessLoopback) return false;
    if (!process_) return true;   // couldn't open it: treat as gone
    return WaitForSingleObject(process_, 0) == WAIT_OBJECT_0;
}

void CaptureSource::Start()
{
    // An app source added while the app isn't running has pid 0: it stays idle
    // (shown as "closed") until MixEngine::Maintain() re-attaches it.
    if (cfg_.kind == SourceKind::ProcessLoopback && cfg_.pid == 0) return;
    if (running_.exchange(true)) return;
    ResetEvent(stopEvent_);
    failed_ = false;
    thread_ = std::thread(&CaptureSource::ThreadMain, this);
}

void CaptureSource::Stop()
{
    if (!running_.exchange(false)) return;
    SetEvent(stopEvent_);
    if (thread_.joinable()) thread_.join();
}

void CaptureSource::ThreadMain()
{
    ComScope com;
    MmcssScope mmcss;
    try
    {
        RunStream();
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

// ---------------------------------------------------------------------------
// Activation
// ---------------------------------------------------------------------------
ComPtr<IAudioClient> CaptureSource::ActivateDevice()
{
    ComPtr<IMMDeviceEnumerator> e;
    MC_CHECK(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&e)));
    ComPtr<IMMDevice> dev;
    MC_CHECK(e->GetDevice(cfg_.deviceId.c_str(), &dev));
    ComPtr<IAudioClient> client;
    MC_CHECK(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client));
    return client;
}

ComPtr<IAudioClient> CaptureSource::ActivateProcessLoopback()
{
    AUDIOCLIENT_ACTIVATION_PARAMS ap{};
    ap.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    ap.ProcessLoopbackParams.TargetProcessId     = cfg_.pid;
    ap.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;

    PROPVARIANT pv{};
    pv.vt             = VT_BLOB;
    pv.blob.cbSize    = sizeof(ap);
    pv.blob.pBlobData = reinterpret_cast<BYTE*>(&ap);

    auto* handler = new ActivationHandler();   // refcount 1, released below
    ComPtr<IActivateAudioInterfaceAsyncOperation> op;
    HRESULT hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
                                             __uuidof(IAudioClient), &pv, handler, &op);
    if (SUCCEEDED(hr))
    {
        if (WaitForSingleObject(handler->DoneEvent(), 5000) != WAIT_OBJECT_0) hr = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        else hr = handler->Result();
    }
    ComPtr<IAudioClient> client = SUCCEEDED(hr) ? handler->Client() : nullptr;
    handler->Release();

    if (FAILED(hr)) throw HrError("Process loopback activation (needs Windows 10 2004+)", hr);
    return client;
}

// Tries float first, then 16-bit PCM; with and without auto-conversion.
// Each attempt uses a fresh IAudioClient (a failed Initialize can't be retried).
void CaptureSource::InitClient(ComPtr<IAudioClient>& client, HANDLE event)
{
    const bool loopback = cfg_.kind != SourceKind::Microphone;
    const DWORD base = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | (loopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0);
    const DWORD convert = AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;

    struct Attempt { bool isFloat; DWORD flags; };
    const Attempt attempts[] = {
        { true,  base | convert },
        { true,  base },
        { false, base | convert },
        { false, base },
    };

    HRESULT last = E_FAIL;
    for (const auto& a : attempts)
    {
        client = (cfg_.kind == SourceKind::ProcessLoopback) ? ActivateProcessLoopback() : ActivateDevice();
        WAVEFORMATEXTENSIBLE wf = MakeFormat(a.isFloat);

        last = client->Initialize(AUDCLNT_SHAREMODE_SHARED, a.flags, kCaptureBufferHns, 0,
                                  reinterpret_cast<WAVEFORMATEX*>(&wf), nullptr);
        if (SUCCEEDED(last))
        {
            MC_CHECK(client->SetEventHandle(event));
            fmtChannels_   = wf.Format.nChannels;
            fmtFloat_      = a.isFloat;
            fmtBlockAlign_ = wf.Format.nBlockAlign;
            return;
        }
    }
    throw HrError("IAudioClient::Initialize (capture)", last);
}

// ---------------------------------------------------------------------------
// Stream loop
// ---------------------------------------------------------------------------
void CaptureSource::RunStream()
{
    EventHandle audioEvent;
    ComPtr<IAudioClient> client;
    InitClient(client, audioEvent.h);

    ComPtr<IAudioCaptureClient> capture;
    MC_CHECK(client->GetService(IID_PPV_ARGS(&capture)));

    std::vector<float> scratch(kSampleRate * kChannels);   // 1 s, grows if ever needed

    MC_CHECK(client->Start());

    const HANDLE waits[2] = { audioEvent.h, stopEvent_ };
    while (running_)
    {
        // Timeout also polls, in case a loopback stream goes quiet without signalling.
        DWORD w = WaitForMultipleObjects(2, waits, FALSE, 100);
        if (w == WAIT_OBJECT_0 + 1) break;

        UINT32 packet = 0;
        for (;;)
        {
            HRESULT hr = capture->GetNextPacketSize(&packet);
            if (hr == AUDCLNT_E_DEVICE_INVALIDATED)
                throw std::runtime_error("Device was removed or disabled");
            MC_CHECK(hr);
            if (packet == 0) break;

            BYTE*  data   = nullptr;
            UINT32 frames = 0;
            DWORD  flags  = 0;
            MC_CHECK(capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr));

            if (scratch.size() < static_cast<size_t>(frames) * kChannels)
                scratch.resize(static_cast<size_t>(frames) * kChannels);

            if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
                std::fill(scratch.begin(), scratch.begin() + static_cast<size_t>(frames) * kChannels, 0.0f);
            else
                ConvertToStereoFloat(data, frames, scratch.data());

            MC_CHECK(capture->ReleaseBuffer(frames));

            // Mic clean-up runs here, on the capture thread, before mixing.
            if (voiceProc_) voiceProc_->Process(scratch.data(), frames, *voice_);

            size_t written = ring_.Write(scratch.data(), frames);
            if (written < frames) droppedFrames.fetch_add(frames - written, std::memory_order_relaxed);
        }
    }

    client->Stop();
}

void CaptureSource::ConvertToStereoFloat(const BYTE* data, UINT32 frames, float* out) const
{
    const UINT32 bytesPerSample = fmtBlockAlign_ / fmtChannels_;

    for (UINT32 f = 0; f < frames; f++)
    {
        const BYTE* frame = data + static_cast<size_t>(f) * fmtBlockAlign_;
        for (UINT32 ch = 0; ch < kChannels; ch++)
        {
            const UINT32 src = (fmtChannels_ == 1) ? 0 : ch;   // mono -> both sides
            const BYTE* p = frame + src * bytesPerSample;
            float v;
            if (fmtFloat_)                v = *reinterpret_cast<const float*>(p);
            else if (bytesPerSample == 2) v = *reinterpret_cast<const int16_t*>(p) / 32768.0f;
            else if (bytesPerSample == 4) v = static_cast<float>(*reinterpret_cast<const int32_t*>(p) / 2147483648.0);
            else                          v = 0.0f;
            out[f * kChannels + ch] = v;
        }
    }
}

} // namespace mixcast
