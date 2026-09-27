# MixCast Driver — Step 1: loopback virtual audio device

A kernel-mode virtual audio driver built from Microsoft's **SimpleAudioSample**
(Windows-driver-samples, MS-PL). It exposes two endpoints:

| Endpoint | Type | Role |
|---|---|---|
| **MixCast Input** | Playback | Your mixer app (later) writes the mix here |
| **MixCast Mic** | Recording | Discord / Zoom / OBS select this as their mic |

Anything played into *MixCast Input* comes out of *MixCast Mic*, looped inside the driver.

## What changed vs. the Microsoft sample

| File | Change |
|---|---|
| `Source/Inc/loopback.h`, `Source/Main/loopback.cpp` | **New.** Shared ring buffer (48 kHz stereo, 32-bit canonical), format conversion, latency control |
| `Source/Main/minwavertstream.cpp` | Render path pushes into the ring instead of a .wav file; capture path pulls from the ring instead of the sine generator |
| `Source/Main/adapter.cpp` | Allocates the ring in `DriverEntry`, frees it in `DriverUnload` |
| `Source/Inc/definitions.h` | New product GUID, debug prefix `MIXCAST:` |
| `Source/Main/MixCast.inx` | Rebranded INF (replaces `SimpleAudioSample.inx`): hardware ID `ROOT\MixCast`, service/sys `MixCast`, endpoint names, installs on Win10 2004+ and Win11 |
| `Source/Main/Main.vcxproj` | Output `MixCast.sys`, compiles `loopback.cpp` |
| `Source/Main/SimpleAudioSample.rc` | Version-info strings |

Every edit in existing files is tagged with a `// MixCast` comment.

### How the loopback works
- Both endpoints are paced by the same QPC timer inside the driver, so there is **no clock drift** between them.
- Capture stays silent until **20 ms** is buffered (absorbs DPC timing jitter).
- If more than **60 ms** builds up, the oldest audio is dropped back to 20 ms, so latency can never creep.
- On underrun it outputs silence and re-primes. Tunables are at the top of `loopback.h`.

## Prerequisites
- **Visual Studio 2022** with the *Desktop development with C++* workload, plus the *MSVC Spectre-mitigated libs (x64)* individual component.
- **Windows SDK** and a **WDK** of the matching version, plus the WDK Visual Studio extension.
- Git (optional; used by `prepare.ps1`).

## Build
```powershell
cd MixCast-driver
.\prepare.ps1                      # clones the MS sample, applies the overlay -> .\driver
```
1. Open `driver\SimpleAudioSample.sln`.
2. Pick **x64 / Debug**.
3. Build the **package** project.
4. Output: `driver\x64\Debug\package\` containing `MixCast.inf`, `MixCast.sys`, `MixCast.cat` and the test certificate.

## Install (test-signed, development only)
Run these in an **admin** PowerShell:
```powershell
bcdedit /set testsigning on        # once; needs Secure Boot OFF in firmware; reboot after
.\install.ps1                      # trusts the WDK test cert, installs ROOT\MixCast via devcon
```
To remove it: `.\uninstall.ps1`

> Use a spare PC or a VM if you can. A kernel-driver bug means a blue screen, not a crash dialog.

## Test it (no mixer app needed yet)
1. Go to **Settings → System → Sound → Volume mixer**. Set a media player's output device to **MixCast Input** and play some music.
2. Open the classic Sound control panel (`mmsys.cpl`) and go to **Recording → MixCast Mic → Properties → Listen**. Tick **Listen to this device** and choose your real speakers.
3. You should hear the music, delayed by about 20–60 ms.
4. Alternatively, record from MixCast Mic in Voice Recorder or Audacity (WASAPI).

Debug builds print `MIXCAST:` messages. View them in **DebugView** (Sysinternals) with *Capture Kernel* enabled.

## Known limitations (next steps)
- **The volume/mute slider on "MixCast Input" does nothing.** The sample exposes hardware volume/mute nodes but never applies them. Your mixer app's own gain is unaffected. Fix: remove those topology nodes so Windows applies software volume.
- The recording endpoint is still typed as a *Microphone Array*. This is fine for Discord and Zoom; it is cosmetic.
- **Distribution** requires an EV certificate plus Microsoft Partner Center attestation signing. Test signing only works on your own machines.
- Control from the GUI (status and stats via a private KS property set) comes in the next step. `CLoopbackBuffer::GetStats()` is already in place for it.

## License
Derived from Microsoft Windows-driver-samples (MS-PL). Keep the original Microsoft copyright headers.
