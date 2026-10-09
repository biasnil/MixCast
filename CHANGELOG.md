# Changelog

## Unreleased

**Radio voice**
- New **Radio voice** section in the mic's **Noise** menu: **De-esser**, **Voice EQ**, **Compressor** and **Limiter**, run after the clean-up in broadcast order. Together with the rumble filter and the between-words gate, the mic now has the full six-stage radio chain.
- The limiter is on by default and never touches normal speech; the other three start off.
- Console version: `p` turns all four on or off.

**Learns your voice (statistics, no AI)**
- **Learn my voice** (on): learns your pitch range, speaking level and voice spectrum from moments it's sure are you, and keeps following your voice as it changes. Once trained, only your pitch range counts as voice (Voice only gate, ducking, Talking lamp), the gate threshold sits between your room and your voice, and noise suppression is stricter where your voice never has energy.
- **Auto level** (off): keeps your voice at a steady level whatever the mic gain.
- **Clean while I talk** (off): a pitch-tracked comb that turns down noise between your harmonics during vowels, only as hard as the room is noisy.
- **Forget my voice…** starts learning again. The profile is saved in `MixCast.ini`.
- With these off, the mic sounds exactly as before.
- Console version: `l`, `v` and `h` toggle them.

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
