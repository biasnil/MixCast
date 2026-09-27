// MixCast engine - file decoding via Windows Media Foundation.
#include "audio_decoder.h"
#include "resampler.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <mutex>

namespace mixcast {

using Microsoft::WRL::ComPtr;

namespace {
std::once_flag g_mfOnce;
HRESULT        g_mfStartup = E_FAIL;
}

bool EnsureMediaFoundation()
{
    // Process-wide; left running until exit.
    std::call_once(g_mfOnce, [] { g_mfStartup = MFStartup(MF_VERSION, MFSTARTUP_LITE); });
    return SUCCEEDED(g_mfStartup);
}

bool DecodeStereo48k(const std::wstring& path, double maxSeconds,
                     std::vector<float>& stereo48k, std::string& error)
{
    std::vector<float> native;
    uint32_t rate = 0;
    if (!DecodeToStereoFloat(path, maxSeconds, native, rate, error)) return false;
    stereo48k = ResampleStereo(native, rate, 48000);
    return true;
}

bool DecodeToStereoFloat(const std::wstring& path, double maxSeconds,
                         std::vector<float>& stereo, uint32_t& sampleRate, std::string& error)
{
    if (!EnsureMediaFoundation()) { error = "Media Foundation is not available on this PC"; return false; }

    ComPtr<IMFSourceReader> reader;
    if (FAILED(MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader)))
    {
        error = "Windows can't open this file type";
        return false;
    }

    const DWORD stream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
    reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    if (FAILED(reader->SetStreamSelection(stream, TRUE))) { error = "No audio in this file"; return false; }

    // Float PCM at the file's own rate and channel count.
    ComPtr<IMFMediaType> want;
    if (FAILED(MFCreateMediaType(&want))) { error = "Media Foundation error"; return false; }
    want->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    want->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
    if (FAILED(reader->SetCurrentMediaType(stream, nullptr, want.Get())))
    {
        error = "Windows has no decoder for this audio format";
        return false;
    }

    ComPtr<IMFMediaType> got;
    if (FAILED(reader->GetCurrentMediaType(stream, &got))) { error = "Unreadable audio format"; return false; }
    const UINT32 rate     = MFGetAttributeUINT32(got.Get(), MF_MT_AUDIO_SAMPLES_PER_SECOND, 0);
    const UINT32 channels = MFGetAttributeUINT32(got.Get(), MF_MT_AUDIO_NUM_CHANNELS, 0);
    if (rate == 0 || channels == 0) { error = "Unreadable audio format"; return false; }

    const size_t maxFrames = static_cast<size_t>(maxSeconds * rate);
    stereo.clear();
    stereo.reserve(static_cast<size_t>(rate) * 2 * 10);

    for (;;)
    {
        DWORD flags = 0;
        ComPtr<IMFSample> sample;
        if (FAILED(reader->ReadSample(stream, 0, nullptr, &flags, nullptr, &sample)))
        {
            error = "The file is damaged or incomplete";
            return false;
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        if (!sample) continue;

        ComPtr<IMFMediaBuffer> buf;
        if (FAILED(sample->ConvertToContiguousBuffer(&buf))) continue;
        BYTE* data = nullptr;
        DWORD bytes = 0;
        if (FAILED(buf->Lock(&data, nullptr, &bytes))) continue;

        const float* f = reinterpret_cast<const float*>(data);
        const size_t frames = bytes / (sizeof(float) * channels);
        for (size_t i = 0; i < frames; i++)
        {
            const float l = f[i * channels];
            const float r = (channels > 1) ? f[i * channels + 1] : l;   // mono -> both sides
            stereo.push_back(l);
            stereo.push_back(r);
        }
        buf->Unlock();

        if (stereo.size() / 2 > maxFrames) { error = "Longer than 10 minutes"; return false; }
    }

    if (stereo.empty()) { error = "The file has no audio"; return false; }
    sampleRate = rate;
    return true;
}

} // namespace mixcast
