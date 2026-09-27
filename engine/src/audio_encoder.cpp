// MixCast engine - writing audio files.
#include "audio_encoder.h"
#include "audio_decoder.h"

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

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cwctype>

namespace mixcast {

using Microsoft::WRL::ComPtr;

namespace {

constexpr UINT32 kRate = 48000;

inline int16_t ToPcm16(float v)
{
    return static_cast<int16_t>(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f));
}

bool WriteWav16(const std::wstring& path, const std::vector<float>& s, std::string& error)
{
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) { error = "Can't write to that location"; return false; }

    const uint32_t dataBytes = static_cast<uint32_t>(s.size() * sizeof(int16_t));
    auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };

    fwrite("RIFF", 1, 4, f); u32(36 + dataBytes); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); u32(16); u16(1); u16(2); u32(kRate); u32(kRate * 4); u16(4); u16(16);
    fwrite("data", 1, 4, f); u32(dataBytes);

    std::vector<int16_t> chunk(96000);
    for (size_t i = 0; i < s.size(); i += chunk.size())
    {
        const size_t n = std::min(chunk.size(), s.size() - i);
        for (size_t k = 0; k < n; k++) chunk[k] = ToPcm16(s[i + k]);
        fwrite(chunk.data(), sizeof(int16_t), n, f);
    }
    const bool ok = !ferror(f);
    fclose(f);
    if (!ok) error = "Writing the file failed (disk full?)";
    return ok;
}

bool WriteWithMediaFoundation(const std::wstring& path, const std::vector<float>& s,
                              ExportFormat format, int kbps, std::string& error)
{
    if (!EnsureMediaFoundation()) { error = "Media Foundation is not available on this PC"; return false; }

    ComPtr<IMFSinkWriter> writer;
    if (FAILED(MFCreateSinkWriterFromURL(path.c_str(), nullptr, nullptr, &writer)))
    {
        error = "Can't create that file";
        return false;
    }

    // Encoded output.
    ComPtr<IMFMediaType> out;
    MFCreateMediaType(&out);
    out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    out->SetGUID(MF_MT_SUBTYPE, format == ExportFormat::Mp3 ? MFAudioFormat_MP3 : MFAudioFormat_AAC);
    out->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kRate);
    out->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
    out->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, static_cast<UINT32>(kbps * 1000 / 8));
    if (format == ExportFormat::M4a) out->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    else                             out->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 1);

    DWORD stream = 0;
    if (FAILED(writer->AddStream(out.Get(), &stream)))
    {
        error = format == ExportFormat::Mp3 ? "Windows' MP3 encoder isn't available or doesn't accept this bitrate"
                                            : "Windows' AAC encoder isn't available or doesn't accept this bitrate";
        return false;
    }

    // What we feed in: 16-bit PCM, 48 kHz stereo.
    ComPtr<IMFMediaType> in;
    MFCreateMediaType(&in);
    in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    in->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    in->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kRate);
    in->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
    in->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    in->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
    in->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, kRate * 4);
    if (FAILED(writer->SetInputMediaType(stream, in.Get(), nullptr)) || FAILED(writer->BeginWriting()))
    {
        error = "The encoder refused the audio format";
        return false;
    }

    const size_t frames = s.size() / 2;
    const size_t chunk  = kRate;   // 1 s per sample
    for (size_t f0 = 0; f0 < frames; f0 += chunk)
    {
        const size_t n = std::min(chunk, frames - f0);
        const DWORD bytes = static_cast<DWORD>(n * 4);

        ComPtr<IMFMediaBuffer> buf;
        ComPtr<IMFSample> sample;
        BYTE* data = nullptr;
        if (FAILED(MFCreateMemoryBuffer(bytes, &buf)) || FAILED(buf->Lock(&data, nullptr, nullptr)))
        {
            error = "Out of memory while encoding";
            return false;
        }
        int16_t* pcm = reinterpret_cast<int16_t*>(data);
        for (size_t i = 0; i < n * 2; i++) pcm[i] = ToPcm16(s[f0 * 2 + i]);
        buf->Unlock();
        buf->SetCurrentLength(bytes);

        MFCreateSample(&sample);
        sample->AddBuffer(buf.Get());
        sample->SetSampleTime(static_cast<LONGLONG>(f0) * 10000000LL / kRate);   // 100 ns units
        sample->SetSampleDuration(static_cast<LONGLONG>(n) * 10000000LL / kRate);
        if (FAILED(writer->WriteSample(stream, sample.Get())))
        {
            error = "Encoding failed";
            return false;
        }
    }

    if (FAILED(writer->Finalize())) { error = "Couldn't finish the file"; return false; }
    return true;
}

} // namespace

ExportFormat FormatFromPath(const std::wstring& path)
{
    std::wstring ext = path.substr(path.find_last_of(L'.') + 1);
    for (auto& c : ext) c = static_cast<wchar_t>(std::towlower(c));
    if (ext == L"mp3") return ExportFormat::Mp3;
    if (ext == L"m4a" || ext == L"aac" || ext == L"mp4") return ExportFormat::M4a;
    return ExportFormat::Wav;
}

bool WriteAudioFile(const std::wstring& path, const std::vector<float>& stereo48k,
                    ExportFormat format, int kbps, std::string& error)
{
    if (stereo48k.empty()) { error = "There's no audio to save"; return false; }
    if (format == ExportFormat::Wav) return WriteWav16(path, stereo48k, error);
    return WriteWithMediaFoundation(path, stereo48k, format, kbps, error);
}

} // namespace mixcast
