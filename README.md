# MixCast

MixCast mixes your mic with any app's audio: music, a game, or its built-in soundboard. The mix goes out through a virtual cable, so Discord, OBS or Zoom hear it as one microphone.

---

# Install, step by step

Follow these steps in order. You only do Steps 1–5 once.

### Step 1: Install VB-CABLE (the virtual cable)
1. Go to https://vb-audio.com/Cable/ and download **VB-CABLE Driver Pack** for Windows.
2. Unzip it.
3. Right-click `VBCABLE_Setup_x64.exe`, choose **Run as administrator**, and press **Install Driver**.
4. **Restart your PC.**

### Step 2: Set VB-CABLE to 48 kHz
1. Press **Win + R**, type `mmsys.cpl`, and press Enter.
2. On the **Playback** tab, right-click **CABLE Input** and choose **Properties**. On the **Advanced** tab, pick **2 channel, 24 bit, 48000 Hz (Studio Quality)** and press **OK**.
3. On the **Recording** tab, do the same for **CABLE Output**.
4. Still on the **Playback** tab, make sure your **real headset or speakers** are the default device (green tick), **not** CABLE Input.

### Step 3: Install the build tools
1. **Visual Studio 2022 Community** (free, from https://visualstudio.microsoft.com/). In the installer, tick:
   - **Desktop development with C++**
   - On the right-hand side of that workload, make sure these are ticked: **C++ CMake tools for Windows** and a **Windows 11 SDK** (or a Windows 10 SDK version **10.0.20348 or newer**)
2. **Qt 6** (free, from the Qt Online Installer at https://www.qt.io/download-qt-installer):
   - Sign in or create a free Qt account.
   - Choose **Custom installation**.
   - Under **Qt → Qt 6.x** (any 6.4 or newer), tick **MSVC 2022 64-bit**.
   - Note where it installs, e.g. `C:\Qt\6.8.0\msvc2022_64`.

### Step 4: Build MixCast
1. Open the Start menu and launch **Developer PowerShell for VS 2022**.
2. Go to the engine folder, changing the path to wherever you unzipped MixCast:
   ```powershell
   cd C:\Path\To\MixCast\engine
   ```
3. Configure the build, changing the Qt path to your version from Step 3:
   ```powershell
   cmake -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.8.0/msvc2022_64
   ```
4. Build it:
   ```powershell
   cmake --build build --config Release
   ```
5. When it finishes, the app is at `engine\build\Release\mixcast.exe`. The Qt files it needs are copied next to it automatically, so you can move the whole `Release` folder anywhere or pin it to Start.

### Step 5: First run
1. Double-click **mixcast.exe**.
2. At the top:
   - **Microphone:** pick your real mic, e.g. your HyperX headset.
   - **Send to:** should say **VB-CABLE**.
   - The dot on the right should be red and say **Live**.
3. Speak. The meter on your mic strip should move, and the **Talking** lamp should light up.

### Step 6: Set up Discord
1. In Discord, open **Settings → Voice & Video**.
2. Set **Input Device** to **CABLE Output (VB-Audio Virtual Cable)**.
3. Leave **Output Device** on your headset.
4. Turn **off** **Noise Suppression**, **Echo Cancellation** and **Automatic Gain Control**. MixCast already cleans your mic, and having both on makes them fight.
5. Use **Let's Check** in Discord to hear yourself through MixCast.

The same idea works in OBS, Zoom and other apps: pick **CABLE Output** as the microphone.

### Step 7: Make it yours
- **Add music or a game:** on the **Mixer** tab press **+ Add app** and pick it from the list. Its strip has its own volume fader and an **On/Off** button.
- **Mic clean-up:** on your mic strip press **Noise** and choose:
  - **Noise suppression: Medium or High** for fans and hum.
  - **Silence between words: Voice only** to also mute eating, typing and clicks between your words.
- **Soundboard:** on the **Soundboard** tab press **Add sounds**, then right-click a pad to give it a hotkey.
- **Edit a sound:** right-click a pad and choose **Edit…**, trim it on the **Editor** tab, then press **Save to pad**.

That's it: you're set up. Everything below explains how it works and the other options.

---

# How it all works

## Virtual cables: VB-CABLE or the MixCast driver
A virtual cable has two ends:
- MixCast plays your mix into one end, the "Input".
- Discord records from the other end, the "Output".

MixCast works with two kinds of cable:

| | **VB-CABLE** (recommended) | **MixCast driver** (included) |
|---|---|---|
| Where to get it | Free download from vb-audio.com | Already in the `MixCast-driver/` folder |
| Install | Run the installer, restart | Build it with the Windows Driver Kit, install with a script |
| Windows test mode | **Not needed** | **Required**: Secure Boot off and test signing on |
| Signed by | VB-Audio, so Windows trusts it normally | A test certificate only |
| Discord's input device | **CABLE Output** | **MixCast Mic** |
| Best for | Everyday use, sharing with friends | Learning how audio drivers work, or avoiding third-party software |

If both are installed, MixCast uses VB-CABLE first. You can switch any time with **Send to**, and your settings stay the same.

### Using the MixCast driver instead (advanced)
Only choose this if you're comfortable putting Windows into test mode.
1. Turn **Secure Boot off** in your BIOS/UEFI.
2. In an **admin** PowerShell, run `bcdedit /set testsigning on`, then restart. Windows shows a "Test Mode" watermark while this is on.
3. Build and install the driver. `MixCast-driver/README.md` has the exact steps: run `prepare.ps1`, build in Visual Studio with the WDK, then run `install.ps1`.
4. In MixCast choose **Send to → MixCast driver**, and in Discord pick **MixCast Mic**.

A public release of this driver would need an EV code-signing certificate and Microsoft's attestation signing. That's why VB-CABLE is the easier choice.

### Switching from the MixCast driver to VB-CABLE
1. Install VB-CABLE (Steps 1–2 above).
2. In an **admin** PowerShell, run:
   ```powershell
   cd C:\Path\To\MixCast\MixCast-driver
   .\uninstall.ps1                      # removes the MixCast device + driver package
   bcdedit /set testsigning off
   ```
3. Restart, and turn Secure Boot back on in your BIOS/UEFI.
4. In Discord, change the input device from **MixCast Mic** to **CABLE Output**.

MixCast keeps all your apps, pads and hotkeys; only the cable changes.

## What's in the app
| Tab | What it does |
|---|---|
| **Mixer** | One strip each for your mic, the soundboard and each app, plus the **Output** strip (what Discord hears). Each strip has a fader, an LED meter and On/Off. App strips have **Duck**, which lowers that app while you talk. The bottom bar sets how much apps are lowered and how sensitive voice detection is. |
| **Soundboard** | Pads with global hotkeys that work inside games. **Hear it myself** plays sounds on your headphones too. The **Stop key** stops everything. |
| **Editor** | Cut, copy, paste, crop, fades, volume, normalize, trim silence and reverse, with undo. Pads save back to the pad; other files are exported as MP3, WAV or M4A. |

- **Live indicator** (top right): red **Live** while something is switched on, grey **All off** when your mic, every app and the soundboard are all off.
- **Tray:** closing the window keeps MixCast running in the tray. Right-click the tray icon to mute your mic, stop sounds or quit.

The full feature guide, technical details and the console version are in `engine/README.md`.

## Your settings and sounds
Everything MixCast saves lives in `%LOCALAPPDATA%\MixCast`, usually `C:\Users\<you>\AppData\Local\MixCast`:

| Path | What's in it |
|---|---|
| `MixCast.ini` | All settings: mic, cable, apps, levels, ducking, noise options, pads, hotkeys, window size |
| `Sounds\` | MixCast's own copies of your soundboard files, so pads keep working if you move or delete the originals |

To reset MixCast completely, close it (tray icon → Quit) and delete that folder.

## Troubleshooting
| Problem | Fix |
|---|---|
| Banner says **No virtual cable found** | Install VB-CABLE (Step 1) and restart, then press **Try again**. |
| Discord hears nothing | Discord's input must be **CABLE Output**, the dot must be red **Live**, and your mic strip must be **On**. |
| Friends hear an echo of themselves | Don't add Discord as an app, and keep your Windows default playback on your headset, never CABLE Input. |
| My voice sounds "pumped" or cuts out | Turn off Discord's Noise Suppression and Automatic Gain Control (Step 6). |
| Eating or typing still gets through | Mic strip → **Noise → Silence between words → Voice only**. |
| A hotkey doesn't work | Another app already uses that key. Pick a different one; numpad keys, F13–F24 or Ctrl/Alt combos work best. |
| Build error about Qt | Check the `-DCMAKE_PREFIX_PATH` path in Step 4 points at your Qt `msvc2022_64` folder. |

## Folders
| Folder | What it is |
|---|---|
| `engine/` | The mixer, the desktop app (`mixcast.exe`) and the console version (`mixcast-cli.exe`) |
| `MixCast-driver/` | Optional. MixCast's own virtual cable driver. You don't need it if you use VB-CABLE. |

## About VB-CABLE
VB-CABLE is made by VB-Audio Software and is **not included** in MixCast. Everyone downloads it themselves from vb-audio.com. If you share MixCast with others, point them to that page instead of bundling the installer, because VB-Audio's licence covers redistribution. If you find it useful, consider supporting VB-Audio with a donation.
