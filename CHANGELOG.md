# Changelog

## Unreleased

**New look**
- Channel strips with depth: a lit top edge, a soft gradient, and colour-coded scribble strips for the names (red for your mic, amber for the soundboard, cyan for apps, gold for the output).
- LED meters that glow, with unlit segments keeping a trace of their colour like real LEDs.
- Faders with brushed-metal caps, an amber index line and a printed dB scale.
- Amber LCD-style dB readouts, a glowing red LIVE pill, segmented tabs and an amber-accented wordmark.
- Soundboard pads lit from above, with keycap-style hotkeys and an amber glow while playing.
- The mic's menu is shorter: Radio voice and Learns your voice are now submenus that show their status.

**Simpler**
- One choice for your mic's sound: **Natural**, **Clean**, **Studio** or **Noisy room**. Every individual setting is still under **Sound → Advanced**; changing one shows **Custom**.
- A small live display on the mic strip shows what's being removed: amber is your voice, red is noise.
- The Talking lamp moved into the mic strip's header.
- Once MixCast knows your voice, the **Voice sensitivity** slider gives way to "Follows your voice", since ducking then follows your voice.
- Console version: `s` steps through the presets.

**Radio voice**
- New **Radio voice** section in the mic's **Noise** menu: **De-esser**, **Voice EQ**, **Compressor** and **Limiter**, run after the clean-up in broadcast order. Together with the rumble filter and the between-words gate, the mic now has the full six-stage radio chain.
- The limiter is on by default and never touches normal speech; the other three start off.
- Console version: `p` turns all four on or off.

**Learns your voice (statistics, no AI)**
- **Learn my voice** (on): learns your pitch range, speaking level and voice spectrum from moments it's sure are you, and keeps following your voice as it changes. Once trained, only your pitch range counts as voice (Voice only gate, ducking, Talking lamp), the gate threshold sits between your room and your voice, and noise suppression is stricter where your voice never has energy.
- **Auto level** (off): keeps your voice at a steady level whatever the mic gain.
- **Clean while I talk** (off): a pitch-tracked comb that turns down noise between your harmonics during vowels, only as hard as the room is noisy.
- **Remove keyboard & clicks** (off; on in Noisy room): learned sound dictionaries for your voice and your room (NMF, no AI) cut keypresses, mouse clicks and taps, including under your words, while keeping your consonants.
- **Each mic learns separately**, so switching mics doesn't start over.
- **Forget my voice on this mic…** starts learning again. Profiles are saved in `MixCast.ini`.
- With these off, the mic sounds exactly as before.
- Console version: `l`, `v`, `h` and `b` toggle them.

## 1.0.0 — first release

**Mixer**
- Mix your mic with any app's audio and send it to Discord, OBS or Zoom through VB-CABLE, or through the optional MixCast driver.
- One strip per source: an LED meter, a console fader, On/Off and a remove button. App icons are shown on the strips.
- Per-app capture, so only the apps you choose are heard, never your call.
- Auto-duck lowers music while you talk. You can set how much, set the voice sensitivity, and choose which apps duck.
- A master output strip, plus a Live / All off indicator.

**Mic clean-up (no AI, runs on your PC)**
- Noise suppression: Off, Low, Medium or High, for fans, hum and hiss.
- Silence between words: Gentle, Firm, or **Voice only**, which mutes chewing, clicks and taps between your words.
- A rumble filter that removes the low hum and thumps below 80 Hz.

**Soundboard**
- Pads with global hotkeys that work inside games, a Stop key, and "one sound at a time".
- "Hear it myself" plays sounds on your headphones too, with its own volume.
- Plays MP3, WAV, M4A/AAC, WMA and FLAC files, up to 10 minutes each.

**Editor**
- Cut, copy, paste, delete, crop, fade in and out, volume, normalize, trim silence and reverse, with undo and redo.
- Pads save back to the pad; other files export as MP3, WAV or M4A.

**General**
- All settings and sounds are kept in `%LOCALAPPDATA%\MixCast`.
- Runs in the system tray, with an optional start with Windows.
