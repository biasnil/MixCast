// MixCast engine - shared helpers
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include <avrt.h>
#include <wrl/client.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace mixcast {

using Microsoft::WRL::ComPtr;

// Internal engine format: 48 kHz, stereo, 32-bit float, interleaved.
constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kChannels   = 2;

// ---------------------------------------------------------------------------
// Errors
// ---------------------------------------------------------------------------
class HrError : public std::runtime_error
{
public:
    HrError(const char* what, HRESULT hr)
        : std::runtime_error(Format(what, hr)), hr_(hr) {}
    HRESULT hr() const { return hr_; }

private:
    static std::string Format(const char* what, HRESULT hr)
    {
        char buf[512];
        std::snprintf(buf, sizeof(buf), "%s failed (0x%08lX)", what, static_cast<unsigned long>(hr));
        return buf;
    }
    HRESULT hr_;
};

#define MC_CHECK(expr)                                              \
    do {                                                            \
        HRESULT hr__ = (expr);                                      \
        if (FAILED(hr__)) throw ::mixcast::HrError(#expr, hr__);    \
    } while (0)

// ---------------------------------------------------------------------------
// Formats
// ---------------------------------------------------------------------------
// KSDATAFORMAT_SUBTYPE_PCM / _IEEE_FLOAT, defined locally so no extra
// GUID library is needed with any toolchain.
inline constexpr GUID kSubtypePcm   = { 0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };
inline constexpr GUID kSubtypeFloat = { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };

inline WAVEFORMATEXTENSIBLE MakeFormat(bool floatSamples)
{
    WAVEFORMATEXTENSIBLE wf{};
    wf.Format.wFormatTag      = WAVE_FORMAT_EXTENSIBLE;
    wf.Format.nChannels       = kChannels;
    wf.Format.nSamplesPerSec  = kSampleRate;
    wf.Format.wBitsPerSample  = floatSamples ? 32 : 16;
    wf.Format.nBlockAlign     = static_cast<WORD>(wf.Format.nChannels * wf.Format.wBitsPerSample / 8);
    wf.Format.nAvgBytesPerSec = wf.Format.nSamplesPerSec * wf.Format.nBlockAlign;
    wf.Format.cbSize          = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    wf.Samples.wValidBitsPerSample = wf.Format.wBitsPerSample;
    wf.dwChannelMask          = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    wf.SubFormat              = floatSamples ? kSubtypeFloat : kSubtypePcm;
    return wf;
}

// ---------------------------------------------------------------------------
// RAII scopes
// ---------------------------------------------------------------------------
struct ComScope
{
    ComScope()  { hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED); }
    ~ComScope() { if (SUCCEEDED(hr)) CoUninitialize(); }
    HRESULT hr;
};

// Registers the calling thread with MMCSS so Windows schedules it like pro-audio.
struct MmcssScope
{
    MmcssScope()
    {
        DWORD index = 0;
        handle = AvSetMmThreadCharacteristicsW(L"Pro Audio", &index);
        if (handle) AvSetMmThreadPriority(handle, AVRT_PRIORITY_HIGH);
    }
    ~MmcssScope() { if (handle) AvRevertMmThreadCharacteristics(handle); }
    HANDLE handle = nullptr;
};

struct EventHandle
{
    EventHandle() : h(CreateEventW(nullptr, FALSE, FALSE, nullptr)) {}
    ~EventHandle() { if (h) CloseHandle(h); }
    EventHandle(const EventHandle&) = delete;
    EventHandle& operator=(const EventHandle&) = delete;
    HANDLE h;
};

// ---------------------------------------------------------------------------
// Math / strings
// ---------------------------------------------------------------------------
inline float DbToLin(float db) { return std::pow(10.0f, db / 20.0f); }
inline float LinToDb(float lin) { return lin > 1e-6f ? 20.0f * std::log10(lin) : -120.0f; }

inline std::string ToUtf8(const std::wstring& w)
{
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

} // namespace mixcast
