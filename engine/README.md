# MixCast Engine — mixer, desktop app and console

MixCast mixes your **real mic** with **app audio** and sends the result into a **virtual cable**. Discord, Zoom or OBS then pick the cable's other end as their microphone.

| Cable | Discord / OBS input device | Needs test mode? |
|---|---|---|
| **VB-CABLE** (recommended) | **CABLE Output** | No |
| MixCast driver | **MixCast Mic** | Yes |

MixCast finds whichever cables are installed. VB-CABLE is used first, and the **Send to** picker switches between them.

The code is split into three parts:

- **`mixcast_core`**: the audio engine, built as a static library.
- **`mixcast`**: the Qt 6 desktop app.
- **`mixcast-cli`**: the console version.

## Folder layout
```
MixCast/
├── MixCast-driver/        (step 1)
└── engine/                (this step)
    ├── CMakeLists.txt
    └── src/
        ├── common.h          helpers, 48 kHz stereo float format, COM/MMCSS scopes
        ├── ring_buffer.h     lock-free capture -> mixer ring
        ├── drift_reader.h    clock-drift compensation (tiny adaptive resample)
        ├── voice_processor.h/.cpp  mic noise suppression, gate, rumble filter
        ├── voice_polish.h/.cpp     radio voice: de-esser, EQ, compressor, limiter
        ├── voice_profile.h/.cpp    what the mic has learned about your voice
        ├── sound_dictionary.h/.cpp remove keyboard & clicks (learned NMF dictionaries)
        ├── mic_presets.h           Natural / Clean / Studio / Noisy room
        ├── voice_effects.h/.cpp    Deep, Chipmunk, Robot, Radio, Echo, Reverb
        ├── channel_dsp.h/.cpp      per-channel settings, tone, pan, width
        ├── soundboard.h/.cpp       built-in soundboard: voices, resampler, headphone monitor
        ├── audio_decoder.h/.cpp    MP3/WAV/M4A/WMA/FLAC decoding (Windows Media Foundation)
        ├── audio_encoder.h/.cpp    WAV writer + MP3/M4A export (Windows' built-in encoders)
        ├── audio_edit.h/.cpp       cut/crop/paste/fades/volume/normalize/trim/reverse + undo
        ├── resampler.h/.cpp        high-quality sample-rate conversion for loaded files
        ├── preview_player.h/.cpp   editor playback on your headphones
        ├── devices.h/.cpp    endpoints, apps with audio, process lookup
        ├── capture_source.h/.cpp   mic / speaker loopback / per-app loopback
        ├── mix_engine.h/.cpp       render thread, mixer, ducking, limiter
        └── main.cpp          console UI
    └── gui/
        ├── main.cpp          app entry, Fusion style + theme
        ├── theme.h/.cpp      palette, stylesheet, painted icons
        ├── widgets.h/.cpp    LED meter, console fader, tally lamp
        ├── channel_strip.h/.cpp   one strip per source
        ├── app_picker.h/.cpp      "Add app" dialog
        ├── soundboard_page.h/.cpp pads, hotkey dialog, soundboard tab
        ├── hotkeys.h/.cpp         global hotkeys (RegisterHotKey)
        ├── app_paths.h/.cpp       %LOCALAPPDATA%\MixCast: settings file + stored sounds
        ├── editor_page.h/.cpp     Editor tab: waveform, selection, edits, save/export
        └── main_window.h/.cpp     layout, engine wiring, settings, tray
```

## Build
Requirements:
- **Visual Studio 2022** with the C++ workload
- **CMake 3.20+**
- **Windows SDK 10.0.20348 or newer**
- **Qt 6.4+** for the desktop app. Get it from the Qt Online Installer and pick the *MSVC 2022 64-bit* kit.

Run these in a *Developer PowerShell for VS 2022*, adjusting the Qt path to your install:
```powershell
cd MixCast\engine
cmake -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.8.0/msvc2022_64
cmake --build build --config Release
.\build\Release\mixcast.exe          # desktop app
.\build\Release\mixcast-cli.exe      # console version
```
The build copies Qt's DLLs next to `mixcast.exe` with `windeployqt`, so you can zip the `Release` folder and run it anywhere. To build only the console version, add `-DMIXCAST_BUILD_GUI=OFF`.

## Desktop app (mixcast.exe)
- **Microphone picker (top):** choose which mic your voice comes from, or *No microphone*. The red dot shows when you're live; hover over it to see which device Discord should use.
- **Channel strips:** one for your mic, the soundboard, and one per app, side by side like a mixing desk. Top to bottom, each has:
  - a header (MIC, SOUNDBOARD, APP) with the device or app name under it, and **✕** to remove an app
  - an info panel: on the mic, the **Sound** preset, the **FX** voice effect and the clean-up display; on the soundboard and apps, **Bass / Mid / High** tone knobs and a **Pan** bar
  - a big gain readout (coral above 0 dB)
  - an LED meter, the channel's buttons, and a pill fader: drag, scroll, or use the arrow keys; double-click resets it to 0 dB
- **Channel buttons:** **ON** (coral **OFF** when switched off), **A** and **B** (which outputs it goes to, see below), **SOLO**, plus **DUCK** on apps (lower this app while you talk) and **PADS** on the soundboard.
- **Talking lamp:** the dot in the mic's header lights red whenever your voice is detected.
- **+ Add app:** pick from apps that are playing sound, or browse for an .exe that isn't running yet. That app joins the mix automatically when it starts.
- **Send to (top):** picks the virtual cable. The output strip names the device Discord should use.
- **Output strip:** shows exactly what Discord hears, with a master fader, and where **Output B** goes, with its own level.
- **Bottom bar:** *Auto-duck* on/off, how much to lower apps, and voice sensitivity.
- **Saved settings:** your mic, apps, levels and ducking settings are saved and restored the next time you open MixCast. See *Where MixCast saves things* below.
- **Live indicator (top right):** red **Live** while something is switched on, grey **All off** when your mic, every app and the soundboard are all off (Discord hears silence).
- **Closing the window** keeps MixCast mixing in the system tray. Right-click the tray icon to mute your mic or quit.

## Two outputs, tone and pan
Like a broadcast desk, MixCast has two outputs:

| Output | Where it goes | Typical use |
|---|---|---|
| **A** | The virtual cable (**Send to** at the top) | What Discord hears |
| **B** | Chosen in the Output strip: **Your headphones** (follows your Windows default), any playback device, or **Off** | Hearing yourself, checking a channel, or a second mix for OBS on another virtual cable |

- **A and B buttons:** every channel picks where it goes. By default everything goes to A and nothing to B. Example: put your music on A and B but the soundboard only on A, then point B at a second cable for OBS, and your stream gets the music without the meme sounds.
- **Hear yourself:** press **B** on the mic strip and set Output B to your headphones.
- **SOLO:** listen to one channel alone on Output B, at its fader level and without ducking, to check how it sounds. Several can be soloed at once. What Discord hears never changes. The Output strip shows **SOLO ON B** while any channel is soloed.
- **Why not B for everything?** An app you add still plays on your headphones as usual; MixCast captures a copy. So sending it to headphones on B would play it twice. B is for the mic, for checking channels, or for a second cable.
- **Tone:** **Bass** (shelf at 120 Hz), **Mid** (1 kHz) and **High** (shelf at 6 kHz), each ±12 dB. Drag a knob up or down, or scroll; double-click resets it. A mint arc means boost, coral means cut.
- **Pan:** drag the bar to move a channel left or right; double-click to centre it. Right-click it for **stereo width**: Mono, Narrow, Normal, Wide or Extra wide.
- Tone, pan and width apply to both outputs. At their middle settings the sound passes through untouched.

## Soundboard
The **Soundboard** tab is a built-in Soundpad. Sounds go straight into your mix, so Discord hears them on CABLE Output alongside your voice.

- **Adding sounds:** press **Add sounds**, or drag files onto the tab. MP3, WAV, M4A/AAC, WMA and FLAC all work, up to 10 minutes each.
- **Playing:** click a pad to play it, and click again to stop. The pad fills with mint as it plays.
- **Pad options:** right-click a pad to set a **hotkey**, change its **volume**, rename it, or remove it. Also:
  - **Loop:** keeps playing from the start until you stop it (marked ↻).
  - **Fade out when stopped:** quickly, over half a second, or over two seconds. Restarting a pad always cuts the old one quickly.
  - **Colour:** a coloured stripe and tint, to find pads at a glance.
  - **Page:** put it on a page.
- **Pages:** group pads, e.g. *Memes*, *Music*, *Game*. The tabs above the pads show **All** or one page; **+ Page** makes a new one. Right-click a page tab to rename or delete it (its pads stay, under All).
- **Reorder:** drag a pad onto another pad to move it there, or onto a page tab to move it to that page.
- **Global hotkeys:** hotkeys work everywhere, even inside a fullscreen game. Pressing a hotkey always restarts its sound. Use keys you don't type with, like the numpad, F13–F24, or Ctrl/Alt combinations, because Windows gives the key to MixCast while it's bound. There's also a **Stop key** that stops everything.
- **One sound at a time:** a new sound stops whatever is playing.
- **Hear it myself:** plays the sounds on your own headphones as well, with a separate volume that others don't hear. It follows your Windows default playback device.
- **Soundboard strip:** the Mixer tab has a Soundboard strip with its own fader, meter and On/Off. Turning it off mutes the soundboard everywhere. Sounds are never ducked by your voice.
- **Saved:** your pads, hotkeys and volumes come back the next time you open MixCast.

Sounds are decoded once into memory (48 kHz, 16-bit), so a hotkey plays instantly. Anything that isn't 48 kHz is converted with a 32-tap windowed-sinc resampler. It keeps levels exact and pushes aliases more than 60 dB down.

## Editor
The **Editor** tab is a simple audio editor for your sounds.

**Opening a sound**
- **From a pad:** right-click it on the Soundboard tab and choose **Edit…**. The editor shows **Soundboard pad**, and the main button is **Save to pad** (Ctrl+S). Saving replaces the pad's sound and keeps its hotkey and volume. The edit is saved as a lossless WAV in `%LOCALAPPDATA%\MixCast\Sounds`, and the old copy is removed.
- **From a file:** press **Open…** or drop a file onto the tab. The editor shows **File**, and nothing is ever overwritten. The main button is **Export…** (Ctrl+S), which saves a new MP3 (192 kbps), WAV or M4A. **Add to soundboard** turns it into a pad, and from then on saving updates that pad.

**Working with the waveform**
- **Select:** drag across the waveform. Drag a selection's edges to adjust them. Double-click selects everything, and Shift-click extends the selection.
- **Move and zoom:** scroll to move along the sound, and hold Ctrl while scrolling to zoom. When zoomed in far enough you see the actual sample points.
- **Play:** plays the selection, or from the cursor when nothing is selected. It plays on **your headphones only**, never into Discord. **Loop** repeats it.

**Edits**

| Button | What it does | Shortcut |
|---|---|---|
| Cut / Copy / Paste | Paste goes in at the cursor, or replaces the selection | Ctrl+X / C / V |
| Delete | Removes the selection | Del |
| Crop | Keeps only the selection | Ctrl+T |
| Fade in / Fade out | Fades the selection. With nothing selected, fades the first or last half-second | |
| Volume… | Changes the level by ± dB | |
| More ▸ Normalize | Makes it as loud as possible without clipping (−1 dB peak) | |
| More ▸ Trim silence | Removes silence from the start and end | |
| More ▸ Reverse | Plays the selection, or everything, backwards | |

- **Clean cuts:** cuts are joined with a 2 ms crossfade, so they don't click.
- **Undo and redo:** Ctrl+Z and Ctrl+Y. The history keeps up to about 1 GB of undo steps.
- **Unsaved edits:** switching sounds or quitting with unsaved edits asks first.

## Where MixCast saves things
Everything is in **`%LOCALAPPDATA%\MixCast`**, usually `C:\Users\<you>\AppData\Local\MixCast`:

| Path | What's in it |
|---|---|
| `MixCast.ini` | All settings: mic, cable, apps, levels, ducking, mic clean-up, what it has learned about your voice, soundboard pads and hotkeys, window size |
| `Sounds\` | MixCast's own copies of your soundboard files. Pads keep working even if you move or delete the originals, and removing a pad deletes its copy. |

Older builds stored settings in the registry. The first launch moves them into `MixCast.ini` and clears the registry copy. To reset MixCast completely, close it and delete the folder.

## Mic noise suppression
The **Sound** button on the mic strip picks a preset: **Natural**, **Clean** (default), **Studio** or **Noisy room**. Each preset sets everything below at once:

| Preset | Noise suppression | Silence between words | Radio voice | Learns your voice |
|---|---|---|---|---|
| Natural | Low | Off | Limiter | Learn |
| Clean | Medium | Gentle | Limiter | Learn |
| Studio | Medium | Gentle | De-esser, EQ, compressor, limiter | Learn, auto level |
| Noisy room | High | Voice only | Limiter | Learn, clean while I talk, remove keyboard & clicks |

Every setting can still be changed on its own under **Sound → Advanced**. The button then shows **Custom**. The settings affect only your mic; apps are never filtered.

The small display in the mic's info panel shows the clean-up live, in 20 bands from low to high pitch. Green is your voice that's kept; red above it is background and clicks being removed, with the total in dB at the top right.

| Setting | Options | What it does |
|---|---|---|
| **Noise suppression** | Off / Low / Medium / High | Learns the steady sound of your room and removes it continuously: fans, PC hum, AC, hiss and traffic rumble. It removes up to 10, 18 or 28 dB. |
| **Silence between words** | Off / Gentle / Firm / **Voice only** | A gate that turns the mic down between phrases. Gentle and Firm open for anything louder than the room. **Voice only** opens only when it hears a voice (a pitch between 70 and 400 Hz held for at least 20 ms). Chewing, crunching, keyboard, mouse clicks, taps and desk bumps stay muted even when they're loud. It adds 25 ms of look-ahead so your first syllable isn't clipped. |
| **Cut low rumble** | On / off | An 80 Hz high-pass filter for desk bumps, handling noise and low hum. |

This is classic signal processing that runs on your PC with no AI. It uses:
- a 512-point STFT
- continuous noise-spectrum tracking
- decision-directed Wiener gains
- smoothing that avoids the "watery" sound

The whole chain, including Radio voice, adds a constant **12 ms** of delay, whether it's on or off.

**Why eating or tapping still gets through on High + Firm.** Noise suppression learns *steady* sounds (fans, hum, hiss). A crunch or a click is sudden and loud, so it looks like speech to that stage, and the Firm gate opens for anything that loud. Use **Voice only** for these sounds. In a test with loud crunches between sentences:

| Gate | Crunches between sentences | Your voice |
|---|---|---|
| Firm | let through (−0.2 dB) | unchanged |
| Voice only | muted (−45 dB) | unchanged (−0.2 dB) |

There are two limits:
- Sounds made **while** you talk, or within about a quarter of a second after, still pass. The gate has to stay open through consonants like "s" and "t".
- Humming or "mm" sounds count as voice.

**How it compares with Discord's Krisp.**
- **Steady noise:** fans, AC and computer hum are handled about as well as Krisp handles them, and your voice stays full and natural.
- **Between your words:** keyboard and mouse sounds are silenced by the gate.
- **While you talk:** sudden sounds that overlap your speech, like a dog bark or a click mid-sentence, still get through. Krisp's AI model is better there.

Keep Discord's own Noise Suppression **off** when you use MixCast's, so the two don't fight each other.

**Measured on a synthetic test** (speech with fan noise, hiss and 60 Hz hum):

| Setting | Background removed | Voice level change |
|---|---|---|
| Low | −10.7 dB | −0.2 dB |
| Medium | −18.9 dB | −0.2 dB |
| High | −28.2 dB | −0.4 dB |
| High + Firm gate | −30.2 dB | −0.4 dB |

With every feature off, the output is bit-identical to the input, just 12 ms later.

## Voice effects
The **FX** button in the mic's panel adds a fun effect. Everyone hears it, so it's for fun with friends rather than meetings.

| Effect | What it does |
|---|---|
| **Deep** | Pitch down 5 semitones |
| **Chipmunk** | Pitch up 7 semitones |
| **Robot** | A buzzy, metallic monotone (a 110 Hz comb with a little ring modulation) |
| **Radio** | Walkie-talkie: only 500 Hz – 3 kHz, overdriven and squashed |
| **Echo** | Repeats every 320 ms, each a little darker |
| **Reverb** | A room around your voice |

The pitch effects splice your voice at a different speed in 30 ms grains, which adds about 15 ms of delay. Switching effects crossfades, so it never clicks, and **No effect** leaves your voice exactly as it was. The effect comes after the clean-up and before the radio voice chain, so the limiter still catches anything it makes louder. Every effect is level-matched to within 2 dB of your normal voice.

## Radio voice
Radio stations run a voice through six stages: high-pass, downward expander, de-esser, EQ, compressor, limiter. MixCast already does the first two as **Cut low rumble** and **Silence between words**. **Sound → Advanced → Radio voice** adds the other four, in that order, after the clean-up:

| Stage | Default | What it does |
|---|---|---|
| **De-esser** | Off | Splits your voice at 4.5 kHz and turns the top band down (up to 12 dB) only while it's louder than the rest of your voice, so harsh "s", "sh" and "t" sounds soften but vowels keep their brightness. |
| **Voice EQ** | Off | −2.5 dB at 250 Hz (less mud and boxiness), +3 dB at 4 kHz (presence, easier to understand), +2 dB shelf above 10 kHz (air). |
| **Compressor** | Off | 3:1 above −24 dBFS with a 6 dB soft knee, 5 ms attack and 150 ms release. Make-up gain keeps normal speech (around −18 dBFS) at the same level, so quiet words come up and loud ones come down. |
| **Limiter** | On | A look-ahead peak limiter with a −1 dBFS ceiling. Shouts and laughs never clip, and it does nothing to normal speech. |

Measured on test signals:

| Test | Result |
|---|---|
| De-esser on a 300 Hz vowel | 0.0 dB (untouched) |
| De-esser on a 7 kHz "sss" | −10 dB |
| Compressor, input −40 / −18 / 0 dBFS | output −36 / −17.6 / −11.4 dBFS |
| Limiter, input +12 dBFS or a 3× transient | peak −1.0 dBFS |

- The compressor also lifts the room between words by about 4 dB, so keep **Silence between words** on when you use it.
- If your voice already sounds "pumped", turn off Discord's **Automatic Gain Control**: two compressors fight.
- All four stages together use under 1% of one CPU core.

## Learns your voice
Discord's Krisp is a neural network. MixCast does it with plain statistics instead: **Sound → Advanced → Learns your voice** builds a profile of *your* voice while you talk, and keeps updating it.

**What it learns.** It learns only from moments it's sure are you: a clear pitch, held for at least 20 ms, well above the room.
- Your **pitch range** (5th to 95th percentile)
- Your normal **speaking level**
- Your voice's **spectrum**: where your voice has energy and where it never does
- Your voice's **sound dictionary**, if **Remove keyboard & clicks** is on (below)

**Each mic learns separately.** A headset and a desk mic hear you differently, so switching mics brings back what that mic learned instead of starting over.

It needs about 20 seconds of speech, roughly a minute of talking, before it's used. The menu shows progress, then something like *Knows your voice: 108–146 Hz, 3 min heard*. After that it's a moving average over your last ~5 minutes of speech, so it follows you when you're tired, ill, shouting in a game, or on a new mic. Learning only accepts pitches near what it already knows, so a TV or another person can't slowly take it over. **Forget my voice on this mic…** starts again.

**What it changes**

| Setting | Default | What it does |
|---|---|---|
| **Learn my voice** | On | Once trained: only *your* pitch range (4 semitones under your low, 6 over your high) counts as voice for **Voice only**, for ducking and for the Talking lamp, so keyboard, clicks and other voices don't duck your music. The Gentle/Firm gate threshold sits a set share of the way from your room to your voice, so a noisy room doesn't cut your quiet words. Noise suppression is stricter (up to twice as hard and 6 dB deeper) in frequencies where your voice is 15–40 dB below its strongest band. The **Voice sensitivity** slider is used only until it's trained. |
| **Auto level** | Off | Brings your speech to a steady level (about −26 dBFS while talking), from −12 to +20 dB of gain, changing over seconds, not words. Needs Learn my voice. |
| **Remove keyboard & clicks** | Off | See below. Needs Learn my voice. |
| **Clean while I talk** | Off | Tracks your pitch to a fraction of a sample and averages each moment with one and two pitch periods earlier, below 4 kHz. Your harmonics line up and pass; noise between them drops. It works only as hard as the room is noisy (fully at 10 dB voice-to-noise, not at all above 25 dB), so a clean voice is left alone. |

Measured on a synthetic talker (pitch 100–150 Hz, syllables, consonants) with background noise:

| Test | Result |
|---|---|
| Learned pitch range | 108–146 Hz after ~1 minute of talking |
| A second voice at 220–280 Hz, once trained | detected as voice 0% of the time (untrained: 80%); yours still 79% |
| Noise between words, Medium suppression + learned spectrum | 2–3 dB quieter than Medium alone; voice unchanged |
| Auto level | −50 dBFS speech → −30 dBFS (+20 dB, the limit); −24 → −26 dBFS |
| Clean while I talk, fan noise | about 1.5 dB less noise below 4 kHz while it works; on a clean voice it stays off |
| CPU, everything on except keyboard removal | under 2% of one core |

**Limits, honestly.**
- **Learn my voice** helps most with *what counts as you*: gating, ducking, other voices. It only trims noise a little.
- **Clean while I talk** is modest. It needs a pitch it can follow, so it works on vowels, not on "s", "f" or "t", and it backs off in very heavy noise where the pitch is lost. It doesn't touch clicks, which are mostly above 4 kHz. A neural network still does better with noise *during* your words.
- **Same pitch, different person:** someone with a voice very like yours still counts as you.
- **The console version** learns per session and doesn't save the profile.

Each mic's profile is saved under `voice/profiles/` in `MixCast.ini` every minute and when you quit. It's about 1 KB, or 34 KB once it holds a sound dictionary. It never leaves your PC.

### Remove keyboard & clicks
Noise suppression learns *steady* sounds, and the gate works only *between* your words. This one goes after keypresses, mouse clicks and taps, including the ones under your voice. It uses non-negative matrix factorization (NMF), a classic method from about 2000, not a neural network:

- **Two small sound dictionaries.** One holds your voice: 24 building blocks for vowels, learned from clearly pitched moments, and 8 for consonants, learned from the steady hiss just before a vowel starts. It's saved with your profile and keeps improving over days. The other holds your room: 16 building blocks learned in the gaps between your sentences (keyboard, mouse, desk, fan). It's relearned every session, so it follows what's around you now.
- **Every 5 ms** the sound is explained as a mix of the two, and only the part your voice explains is kept, up to 20 dB less for the rest.
- **Clicks, not consonants.** A keypress lasts 10–20 ms, but a "t" or "k" is a sharp burst too. So the strong cut applies only for ~20 ms after something that isn't you starts, and then lets go. The consonant building blocks keep your "s", "t" and "f" intact.

Measured on a synthetic talker with a synthetic keyboard (7 keypresses a second, including while talking), after a minute of learning:

| Test | Without | With |
|---|---|---|
| Keyboard between words | −40 to −51 dBFS | 4–8 dB quieter |
| Keyboard while talking | 7.5–20 dB under your voice | up to 3.3 dB better (one case 0.6 dB worse) |
| Change to a clean voice | | 20–29 dB below your voice, consonants 16–18 dB below them: not audible |
| CPU | 1.9% of one core | 2.8% of one core |

It helps most **between and around your words**. For the room between sentences, **Silence between words → Voice only** still does the heavy lifting, which is why the Noisy room preset uses both. Under your words it's a modest gain: a keypress inside a vowel shares the same frequencies, and telling them apart there is where a neural network still wins. These numbers come from synthetic sounds; a real keyboard and voice will differ, so try it and keep what sounds best.

## Console version (mixcast-cli.exe)
```powershell
mixcast-cli                                  # start with your mic, add apps live
mixcast-cli --app Spotify.exe --sfx Soundpad.exe
mixcast-cli --list                           # devices + apps playing audio
```
In Discord, set **Settings → Voice & Video → Input Device** to **CABLE Output** (or **MixCast Mic** if you use the MixCast driver). Turn off Discord's **Noise Suppression** and **Automatic Gain Control** as well.

**Live controls**

| Key | Action |
|---|---|
| ↑ / ↓ or `1`–`9` | Select a source |
| ← / → | Volume of the selected source (±1 dB) |
| `Space` | Turn the selected source on/off |
| `k` | Toggle whether the selected app ducks while you talk |
| `m` | Mic on/off |
| `a` | Add an app (pick from apps playing audio, or type an exe name) |
| `x` | Remove the selected app |
| `c` | Change microphone |
| `d` | Ducking master switch |
| `n` | Cycle noise suppression (off / low / medium / high) |
| `g` | Cycle the between-words gate (off / gentle / firm) |
| `r` | Rumble filter on/off |
| `p` | Radio voice on/off (de-esser, EQ, compressor and limiter together) |
| `l` | Learn my voice on/off |
| `v` | Auto level on/off |
| `h` | Clean while I talk on/off |
| `b` | Remove keyboard & clicks on/off |
| `s` | Next mic sound preset (Natural / Clean / Studio / Noisy room) |
| `e` | Next voice effect |
| `q` | Quit |

- **Ducking:** apps marked **ducks** get quieter while you talk (music, games). Apps marked **no-duck** stay at full level (Soundpad, sound effects).
- **Closing and reopening an app:** its source stays in the list as "closed". When you start the app again, MixCast re-attaches automatically and keeps the same settings.

### Avoiding echo
- **Each app is isolated.** Every app source captures only that app and its child processes, so Discord's call audio never gets in by accident.
- **Capturing Discord itself echoes.** If you add Discord, your friends hear themselves. MixCast asks you to confirm before doing it.
- **Don't route the cable into itself.** Never set CABLE Input (or MixCast Input) as your Windows default playback device.

## How it works
```
Real mic ──WASAPI capture──► ring ─► DriftReader ─┐
Spotify ──process loopback─► ring ─► DriftReader ─┼─► mix ► duck ► soft-limit ─► CABLE Input ─► [cable] ─► CABLE Output
Chrome  ──process loopback─► ring ─► DriftReader ─┘        (render thread, event-driven, MMCSS "Pro Audio")
```
- **One format:** everything is requested as 48 kHz stereo float, and Windows converts on the way in (`AUTOCONVERTPCM`).
- **Clock drift:** each device has its own clock, so each source is read at a ratio of about 1.0000 ± 0.004. The ratio is steered to keep roughly 30 ms buffered. A 60-second simulation with a 0.05 % fast source settled at a 1.00049 ratio, about 27 ms buffered, with zero dropouts.
- **Ducking:** when the mic RMS goes above −40 dB, app audio drops by 12 dB within 10 ms. It recovers over 250 ms after a 300 ms hold.
- **Limiter:** the output is transparent up to −0.9 dBFS, then saturates smoothly, so it never hard-clips.
- **Rough latency:** mic → CABLE Output takes about 11 ms (clean-up) + 20 ms (capture) + 30 ms (source buffer) + 20 ms (output) + 10–20 ms (cable), roughly 100 ms. That's fine for voice chat. You can lower it later with `--latency` and the driver constants.

## Status line
- **`glitches`**: times the output buffer ran empty. It should stay at or near 0.
