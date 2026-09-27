// MixCast engine - audio endpoint and process discovery.
#pragma once

#include "common.h"

#include <optional>
#include <string>
#include <vector>

namespace mixcast {

struct Endpoint
{
    std::wstring id;        // IMMDevice ID, stable across reboots
    std::wstring name;      // e.g. "Speakers (MixCast Virtual Audio Device)"
    bool         isDefault = false;
};

struct AudioProcess
{
    DWORD        pid = 0;
    std::wstring exe;       // e.g. "Spotify.exe"
    std::wstring path;      // full path to the exe (for icons); may be empty
};

// Active endpoints for eRender (playback) or eCapture (recording).
std::vector<Endpoint> ListEndpoints(EDataFlow flow);

// First active endpoint whose name contains `part` (case-insensitive).
std::optional<Endpoint> FindEndpoint(EDataFlow flow, const std::wstring& part);

// A virtual cable MixCast can send its mix into.
struct VirtualOutput
{
    Endpoint     endpoint;   // playback side the mix is written to, e.g. "CABLE Input (VB-Audio Virtual Cable)"
    std::wstring micName;    // what Discord/OBS should pick as their mic, e.g. "CABLE Output"
    std::wstring brand;      // "VB-CABLE" or "MixCast driver"
};

// All installed virtual cables: VB-Audio cables (VB-CABLE, A+B, Hi-Fi) and the
// MixCast driver. VB-CABLE first, since it needs no test mode.
std::vector<VirtualOutput> ListVirtualOutputs();

// True for virtual cable devices (either side). These are never used as
// your microphone and never captured, because that would loop the mix back.
bool IsVirtualCable(const std::wstring& deviceName);

// The MixCast driver's playback endpoint (kept for compatibility).
std::optional<Endpoint> FindMixCastInput();

// Your default playback device (headphones/speakers), unless it's a virtual cable.
std::optional<Endpoint> DefaultRealSpeakers();

// Default recording device, skipping virtual cables (that would loop back).
std::optional<Endpoint> DefaultRealMic();

// Processes that currently have an audio session on any playback device.
std::vector<AudioProcess> ListAudioProcesses();

// Top-most process with this exe name (e.g. the main Chrome/Spotify process,
// not one of its children), so process-tree loopback catches everything.
std::optional<DWORD> FindRootProcess(const std::wstring& exeName);

// Full path of a running process's exe, or empty if it can't be read.
std::wstring ProcessImagePath(DWORD pid);

bool ContainsNoCase(const std::wstring& haystack, const std::wstring& needle);

} // namespace mixcast
