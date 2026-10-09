// MixCast - interactive console mixer. The Qt GUI will reuse mixcast_core.
//
// Your mic is the main channel (on/off any time). Add or remove app sources
// (Spotify, Soundpad, a game, a browser...) while it runs; each one has its
// own volume, on/off and "duck while I talk" setting.
//
//   mixcast-cli                      start with just your mic, add apps live
//   mixcast-cli --app Spotify.exe    pre-add an app (ducked while you talk)
//   mixcast-cli --sfx Soundpad.exe   pre-add an app that is NOT ducked
//   mixcast-cli --list               show devices + apps playing audio

#include "devices.h"
#include "mix_engine.h"

#include <conio.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace mixcast;

namespace {

struct PendingApp { std::wstring exe; bool duck; };

struct Options
{
    bool                    list = false;
    std::wstring            outName;          // empty = first virtual cable found
    std::wstring            micName;
    bool                    noMic = false;
    std::vector<PendingApp> apps;
    int                     latencyMs = 20;
};

void PrintUsage()
{
    std::printf(
        "MixCast console mixer\n\n"
        "  --list               show devices and apps currently playing audio\n"
        "  --app <exe>          add an app at start, ducked while you talk (repeatable)\n"
        "  --sfx <exe>          add an app at start, never ducked, e.g. Soundpad.exe\n"
        "  --mic <name part>    pick a microphone (default: system default mic)\n"
        "  --no-mic             start without a mic (add one later with [c])\n"
        "  --out <name part>    virtual cable to send to (default: VB-CABLE, else MixCast driver)\n"
        "  --latency <ms>       output buffer, 10-200 (default 20)\n");
}

bool Parse(int argc, wchar_t** argv, Options& o)
{
    for (int i = 1; i < argc; i++)
    {
        std::wstring a = argv[i];
        auto next = [&]() -> std::wstring {
            if (i + 1 >= argc) throw std::runtime_error("missing value after " + ToUtf8(a));
            return argv[++i];
        };
        if      (a == L"--list")    o.list = true;
        else if (a == L"--app")     o.apps.push_back({ next(), true });
        else if (a == L"--sfx")     o.apps.push_back({ next(), false });
        else if (a == L"--mic")     o.micName = next();
        else if (a == L"--no-mic")  o.noMic = true;
        else if (a == L"--out")     o.outName = next();
        else if (a == L"--latency") o.latencyMs = std::stoi(next());
        else if (a == L"--help" || a == L"-h") return false;
        else throw std::runtime_error("unknown option: " + ToUtf8(a));
    }
    return true;
}

void EnableAnsi()
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(h, &mode)) SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    SetConsoleOutputCP(CP_UTF8);
}

std::string Meter(float peakLin, int width = 16)
{
    const float db = LinToDb(peakLin);   // -60..0 dB
    int cells = static_cast<int>((db + 60.0f) / 60.0f * width);
    cells = std::max(0, std::min(width, cells));
    return std::string(static_cast<size_t>(cells), '#') + std::string(static_cast<size_t>(width - cells), '-');
}

std::string ReadLine(const char* prompt)
{
    std::printf("%s", prompt);
    std::fflush(stdout);
    std::string s;
    std::getline(std::cin, s);
    return s;
}

bool AskYesNo(const char* prompt, bool def)
{
    std::string s = ReadLine(prompt);
    if (s.empty()) return def;
    return s[0] == 'y' || s[0] == 'Y';
}

std::unique_ptr<CaptureSource> MakeMic(const Endpoint& ep)
{
    SourceConfig c;
    c.kind     = SourceKind::Microphone;
    c.deviceId = ep.id;
    c.label    = ToUtf8(ep.name);
    return std::make_unique<CaptureSource>(c);
}

std::unique_ptr<CaptureSource> MakeApp(const std::wstring& exe, DWORD pid)
{
    SourceConfig c;
    c.kind    = SourceKind::ProcessLoopback;
    c.pid     = pid;
    c.exeName = exe;
    c.label   = ToUtf8(exe);
    return std::make_unique<CaptureSource>(c);
}

// Adds an app by exe name; warns about Discord. Returns true if added.
bool AddApp(MixEngine& engine, const std::wstring& exe, bool duck, bool interactive)
{
    auto pid = FindRootProcess(exe);
    if (!pid) { std::printf("  \"%s\" is not running.\n", ToUtf8(exe).c_str()); return false; }

    if (ContainsNoCase(exe, L"discord"))
    {
        std::printf("  Warning: Discord's audio is your friends' voices. Capturing it sends\n"
                    "  them back into your mic, so they hear themselves (echo).\n");
        if (interactive && !AskYesNo("  Add it anyway? [y/N] ", false)) return false;
    }
    engine.AddSource(MakeApp(exe, *pid), false, 0.0f, duck);
    return true;
}

// ---- Menus (pause the live view) --------------------------------------------
void MenuAddApp(MixEngine& engine)
{
    std::printf("\n  Apps currently playing audio:\n");
    auto procs = ListAudioProcesses();

    // Hide ourselves and apps already added.
    const DWORD self = GetCurrentProcessId();
    auto current = engine.Status();
    procs.erase(std::remove_if(procs.begin(), procs.end(), [&](const AudioProcess& p) {
        if (p.pid == self) return true;
        for (auto& s : current)
            if (!s.isMic && _stricmp(s.label.c_str(), ToUtf8(p.exe).c_str()) == 0) return true;
        return false;
    }), procs.end());

    for (size_t i = 0; i < procs.size(); i++)
        std::printf("    %2zu) %s\n", i + 1, ToUtf8(procs[i].exe).c_str());
    if (procs.empty()) std::printf("    (none playing right now - you can still type an exe name)\n");

    std::string in = ReadLine("  Number, or exe name (e.g. Soundpad.exe), Enter to cancel: ");
    if (in.empty()) return;

    std::wstring exe;
    char* end = nullptr;
    long n = std::strtol(in.c_str(), &end, 10);
    if (end && *end == '\0' && n >= 1 && static_cast<size_t>(n) <= procs.size())
        exe = procs[static_cast<size_t>(n) - 1].exe;
    else
        exe.assign(in.begin(), in.end());

    const bool suggestNoDuck = ContainsNoCase(exe, L"soundpad") || ContainsNoCase(exe, L"voicemod");
    const bool duck = AskYesNo(suggestNoDuck ? "  Lower it while you talk? [y/N] "
                                             : "  Lower it while you talk? [Y/n] ", !suggestNoDuck);
    AddApp(engine, exe, duck, true);
}

void MenuChangeMic(MixEngine& engine)
{
    std::printf("\n  Microphones:\n");
    auto mics = ListEndpoints(eCapture);
    mics.erase(std::remove_if(mics.begin(), mics.end(),
                              [](const Endpoint& e) { return IsVirtualCable(e.name); }), mics.end());
    for (size_t i = 0; i < mics.size(); i++)
        std::printf("    %2zu) %s%s\n", i + 1, ToUtf8(mics[i].name).c_str(), mics[i].isDefault ? "  (default)" : "");

    std::string in = ReadLine("  Number, Enter to cancel: ");
    if (in.empty()) return;
    long n = std::strtol(in.c_str(), nullptr, 10);
    if (n < 1 || static_cast<size_t>(n) > mics.size()) return;
    engine.ReplaceMic(MakeMic(mics[static_cast<size_t>(n) - 1]));
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    EnableAnsi();
    ComScope com;

    Options opt;
    try
    {
        if (!Parse(argc, argv, opt)) { PrintUsage(); return 0; }

        if (opt.list)
        {
            std::printf("Playback devices:\n");
            for (auto& ep : ListEndpoints(eRender)) std::printf("  %s %s\n", ep.isDefault ? "*" : " ", ToUtf8(ep.name).c_str());
            std::printf("\nRecording devices:\n");
            for (auto& ep : ListEndpoints(eCapture)) std::printf("  %s %s\n", ep.isDefault ? "*" : " ", ToUtf8(ep.name).c_str());
            std::printf("\nVirtual cables MixCast can send to:\n");
            for (auto& v : ListVirtualOutputs())
                std::printf("    %-16s Discord/OBS input: %s\n", ToUtf8(v.brand).c_str(), ToUtf8(v.micName).c_str());
            std::printf("\nApps with audio sessions:\n");
            for (auto& p : ListAudioProcesses()) std::printf("    %s\n", ToUtf8(p.exe).c_str());
            return 0;
        }

        std::optional<VirtualOutput> out;
        for (auto& v : ListVirtualOutputs())
            if (opt.outName.empty() || ContainsNoCase(v.endpoint.name, opt.outName)) { out = v; break; }
        if (!out)
        {
            std::printf("No virtual cable found%s.\n"
                        "Install VB-CABLE (free, https://vb-audio.com/Cable/) or the MixCast driver.\n",
                        opt.outName.empty() ? "" : (" matching \"" + ToUtf8(opt.outName) + "\"").c_str());
            return 1;
        }

        MixEngine engine(out->endpoint.id, opt.latencyMs);

        if (!opt.noMic)
        {
            auto mic = opt.micName.empty() ? DefaultRealMic() : FindEndpoint(eCapture, opt.micName);
            if (!mic || IsVirtualCable(mic->name)) { std::printf("Microphone not found. Try --list.\n"); return 1; }
            engine.AddSource(MakeMic(*mic), true, 0.0f, false);
        }
        for (auto& a : opt.apps) AddApp(engine, a.exe, a.duck, false);

        engine.Start();

        size_t selected = 0;
        int drawn = 0;
        auto lastMaintain = std::chrono::steady_clock::now();
        bool quit = false;

        std::printf("\nMixCast -> %s (%s)\n", ToUtf8(out->endpoint.name).c_str(), ToUtf8(out->brand).c_str());
        std::printf("In Discord / OBS, set your input device to: %s\n", ToUtf8(out->micName).c_str());

        while (!quit && !engine.Failed())
        {
            auto sources = engine.Status();
            if (!sources.empty()) selected = std::min(selected, sources.size() - 1);
            SourceControls* sel = sources.empty() ? nullptr : sources[selected].controls;

            // ---- Keys ------------------------------------------------------
            bool menu = false;
            while (_kbhit())
            {
                int k = _getch();
                if (k == 0 || k == 224)   // arrow keys arrive as two codes
                {
                    int code = _getch();
                    if (code == 72 && selected > 0) selected--;                                // Up
                    if (code == 80 && selected + 1 < sources.size()) selected++;               // Down
                    if (sel && code == 75) sel->gainDb = std::max(-60.0f, sel->gainDb - 1.0f); // Left
                    if (sel && code == 77) sel->gainDb = std::min(12.0f, sel->gainDb + 1.0f);  // Right
                    sel = sources.empty() ? nullptr : sources[selected].controls;
                    continue;
                }
                switch (k)
                {
                case 'q': case 'Q': case 27: quit = true; break;
                case ' ': if (sel) sel->enabled = !sel->enabled; break;
                case 'k': case 'K':
                    if (sel && !sources[selected].isMic) sel->duckable = !sel->duckable;
                    break;
                case 'm': case 'M':
                    for (auto& s : sources) if (s.isMic) s.controls->enabled = !s.controls->enabled;
                    break;
                case 'd': case 'D': engine.controls.duckEnabled = !engine.controls.duckEnabled; break;
                case 'n': case 'N': engine.controls.voice.noiseLevel = (engine.controls.voice.noiseLevel + 1) % 4; break;
                case 'g': case 'G': engine.controls.voice.gateMode   = (engine.controls.voice.gateMode + 1) % 4; break;
                case 'r': case 'R': engine.controls.voice.rumbleFilter = !engine.controls.voice.rumbleFilter; break;
                case 'p': case 'P':   // radio voice: all four polish stages together
                {
                    auto& v = engine.controls.voice;
                    const bool on = !(v.deEsser && v.voiceEq && v.compressor && v.limiter);
                    v.deEsser = on; v.voiceEq = on; v.compressor = on; v.limiter = on;
                    break;
                }
                case 'l': case 'L': engine.controls.voice.learnVoice = !engine.controls.voice.learnVoice; break;
                case 'v': case 'V': engine.controls.voice.autoLevel = !engine.controls.voice.autoLevel; break;
                case 'h': case 'H': engine.controls.voice.cleanWhileTalking = !engine.controls.voice.cleanWhileTalking; break;
                case 'x': case 'X':
                    if (!sources.empty() && !sources[selected].isMic) { engine.RemoveSource(sources[selected].id); menu = true; }
                    break;
                case 'a': case 'A': menu = true; MenuAddApp(engine);    break;
                case 'c': case 'C': menu = true; MenuChangeMic(engine); break;
                default:
                    if (k >= '1' && k <= '9' && static_cast<size_t>(k - '1') < sources.size())
                    {
                        selected = static_cast<size_t>(k - '1');
                        sel = sources[selected].controls;
                    }
                    break;
                }
                if (menu) break;
            }
            if (menu) { drawn = 0; std::printf("\n"); continue; }   // list changed: redraw fresh

            // ---- Re-attach apps that were closed and reopened ------------
            auto now = std::chrono::steady_clock::now();
            if (now - lastMaintain > std::chrono::seconds(1)) { engine.Maintain(); lastMaintain = now; }

            // ---- Draw -------------------------------------------------------
            if (drawn > 0) std::printf("\x1b[%dA", drawn);
            int lines = 0;
            auto line = [&](const std::string& text) { std::printf("\x1b[2K%s\n", text.c_str()); lines++; };
            char buf[320];

            line("----------------------------------------------------------------------------");
            for (size_t i = 0; i < sources.size(); i++)
            {
                const auto& s = sources[i];
                const auto* c = s.controls;
                std::string state =
                    s.state == SourceState::WaitingForApp ? "closed - re-attaches when reopened" :
                    s.state == SourceState::Failed        ? "ERROR " + s.error :
                    s.isMic ? std::string(engine.meters.talking && c->enabled ? "talking" : "") :
                    std::string(c->duckable ? "ducks" : "no-duck");
                std::snprintf(buf, sizeof(buf), "%s%zu %s %-24.24s %s [%s] %+5.1f dB  %s",
                              i == selected ? ">" : " ", i + 1, s.isMic ? "MIC" : "APP",
                              s.label.c_str(), c->enabled ? "ON " : "OFF",
                              Meter(c->peak).c_str(), c->gainDb.load(), state.c_str());
                line(buf);
            }
            if (sources.empty()) line("  (no sources - press [a] to add an app, [c] for a mic)");

            std::snprintf(buf, sizeof(buf), "  OUT [%s]  ducking %s  (apps now %+.1f dB)  glitches %u",
                          Meter(engine.meters.outPeak).c_str(), engine.controls.duckEnabled ? "ON " : "OFF",
                          LinToDb(engine.meters.duckGain), engine.meters.renderGlitches.load());
            line(buf);
            {
                static const char* kNoise[] = { "off", "low", "medium", "high" };
                static const char* kGate[]  = { "off", "gentle", "firm", "voice only" };
                const auto& v = engine.controls.voice;
                std::snprintf(buf, sizeof(buf), "  MIC CLEAN-UP  noise %s  gate %s  rumble filter %s  (background %.0f dB)",
                              kNoise[v.noiseLevel.load() & 3], kGate[std::min(v.gateMode.load(), 3)],
                              v.rumbleFilter ? "on" : "off", v.noiseFloorDb.load());
                line(buf);
                std::snprintf(buf, sizeof(buf), "  RADIO VOICE   de-esser %s  EQ %s  compressor %s (%.1f dB)  limiter %s",
                              v.deEsser ? "on" : "off", v.voiceEq ? "on" : "off",
                              v.compressor ? "on" : "off", v.compressDb.load(), v.limiter ? "on" : "off");
                line(buf);
                if (!v.learnVoice)
                    std::snprintf(buf, sizeof(buf), "  YOUR VOICE    not learning");
                else if (!v.profileInUse)
                    std::snprintf(buf, sizeof(buf), "  YOUR VOICE    learning... %.0f of %.0f s heard",
                                  v.learnedSec.load(), VoiceProfile::kTrainedSec);
                else
                    std::snprintf(buf, sizeof(buf), "  YOUR VOICE    %.0f-%.0f Hz  auto level %s (%+.1f dB)  clean while talking %s",
                                  v.pitchLoHz.load(), v.pitchHiHz.load(), v.autoLevel ? "on" : "off",
                                  v.autoGainDb.load(), v.cleanWhileTalking ? "on" : "off");
                line(buf);
            }
            line("----------------------------------------------------------------------------");
            line("  Up/Down select  Left/Right volume  Space on/off  k duck this app");
            line("  m mic on/off  a add app  x remove app  c change mic  d ducking  q quit");
            line("  n noise suppression  g gate (off/gentle/firm/voice only)  r rumble filter");
            line("  p radio voice (de-esser, EQ, compressor, limiter) on/off");
            line("  l learn my voice  v auto level  h clean while I talk");
            drawn = lines;

            std::this_thread::sleep_for(std::chrono::milliseconds(60));
        }

        if (engine.Failed()) std::printf("\nEngine stopped: %s\n", engine.Error().c_str());
        engine.Stop();
        return engine.Failed() ? 1 : 0;
    }
    catch (const std::exception& ex)
    {
        std::printf("Error: %s\n", ex.what());
        return 1;
    }
}
