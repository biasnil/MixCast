// MixCast engine - writing audio files.
// WAV is written directly; MP3 and M4A (AAC) use the encoders built into
// Windows (Media Foundation), so nothing extra is needed.
#pragma once

#include <string>
#include <vector>

namespace mixcast {

enum class ExportFormat { Wav, Mp3, M4a };

// `stereo48k`: interleaved stereo float at 48 kHz. `kbps` is used for MP3/M4A
// (MP3: 128/160/192/320, M4A: 96/128/160/192). Needs COM on the calling thread.
bool WriteAudioFile(const std::wstring& path, const std::vector<float>& stereo48k,
                    ExportFormat format, int kbps, std::string& error);

// Picks the format from the extension (.wav / .mp3 / .m4a).
ExportFormat FormatFromPath(const std::wstring& path);

} // namespace mixcast
