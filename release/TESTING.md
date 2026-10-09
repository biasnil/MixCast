# Releasing MixCast

## 1. Build the release
You need these on your **build PC**, not the VM:
- Visual Studio 2022 and Qt, as in the main README
- **Inno Setup 6**, free from https://jrsoftware.org/isdl.php. It's optional, but you need it for the installer.

In **Developer PowerShell for VS 2022**, run:
```powershell
cd C:\Path\To\MixCast\release
.\package.ps1 -QtDir C:\Qt\6.8.0\msvc2022_64
```

You get these in `release\dist\`:

| File | What it is |
|---|---|
| `MixCast-1.0.0-Setup.exe` | The installer (recommended for users) |
| `MixCast-1.0.0-win64-portable.zip` | Unzip-and-run version |
| `MixCast-1.0.0\` | The app folder both are made from |

- **Next version:** change `project(MixCast VERSION …)` in `engine/CMakeLists.txt`, add a section to `CHANGELOG.md`, and run the script again.
- **Repackaging without rebuilding:** use `-SkipBuild`.

## 2. Test on the VM
Use a VM that has **never** had Visual Studio or Qt installed; ideally, take a snapshot of a fresh Windows 10 or 11 first. That's what proves nothing is missing.

**Give the VM sound**
- **Hyper-V:** connect with *Enhanced Session*. Under *Show Options → Local Resources → Remote audio → Settings*, choose **Play on this computer** and **Record from this computer**.
- **VirtualBox:** in *Settings → Audio*, enable audio output **and input**.
- **VMware:** make sure the VM has a sound card, and connect your mic under *VM → Removable Devices*.

**Checklist**

*Install*
- [ ] Copy `MixCast-1.0.0-Setup.exe` into the VM and run it.
- [ ] SmartScreen says "Windows protected your PC": click **More info → Run anyway**. That's expected, because the app is unsigned.
- [ ] The installer doesn't ask for admin, and the app appears in the Start menu.
- [ ] On the last page, tick **Open the VB-CABLE download page**, install VB-CABLE, and restart.

*First run*
- [ ] MixCast opens with no "missing DLL" errors.
- [ ] **Send to** shows VB-CABLE, and the top right shows red **Live**.
- [ ] The mic meter moves when you speak.
- [ ] Mic strip → **Sound → Noisy room**: tapping the desk stays silent.
- [ ] The mic strip's clean-up display moves when you speak (green) and shows red when there's background noise.
- [ ] Output strip → **Output B: Your headphones**, then press **B** on the mic strip: you hear yourself. Press **SOLO** on an app: only that app plays on B, and Discord still hears the full mix.
- [ ] Turn an app's **Bass** knob up and drag its **Pan** bar left: you hear the change in Discord's **Let's Check**.
- [ ] Mic **FX → Robot**: Discord hears the robot voice; **FX → No effect** brings your voice back.
- [ ] Soundboard: make a page, give a pad a colour and **Loop**, drag it onto another pad, restart MixCast: all of it is still there.

*Features*
- [ ] **Add app:** play a YouTube video in Edge, add `msedge.exe`, and its meter moves.
- [ ] **Soundboard:** add an MP3 and give it a hotkey; it plays with the window minimised, and you hear it with **Hear it myself** on.
- [ ] **Editor:** right-click the pad → **Edit…**, crop it, **Save to pad**, and the pad plays the new version.
- [ ] **Editor export:** open a file and export an MP3, a WAV and an M4A; all three play in Windows Media Player.
- [ ] **Recording test:** in Voice Recorder (or Discord), record from **CABLE Output** and you hear your mix.

*Tray, settings and single instance*
- [ ] Close the window: MixCast stays in the tray. Tray → **Quit MixCast** exits.
- [ ] Reopen it: your mic, apps, pads and hotkeys are all still there.
- [ ] Try to start a second copy: it tells you MixCast is already running.
- [ ] `%LOCALAPPDATA%\MixCast` contains `MixCast.ini` and `Sounds\`.

*Start with Windows*
- [ ] Reinstall with **Start MixCast when Windows starts** ticked, then restart. MixCast starts in the tray, with no window.

*Uninstall*
- [ ] Uninstall from *Settings → Apps*. It asks whether to delete your settings. Choose **Yes** and the `%LOCALAPPDATA%\MixCast` folder is gone.

*Portable zip*
- [ ] On a fresh snapshot, unzip the portable zip and run `mixcast.exe`. It works the same.

If something fails, note which step and the exact message.

## 3. Publish on GitHub
1. Create a repository and push the source code. The `.gitignore` already leaves out build folders and `release\dist`.
2. Open **Releases → Draft a new release**, create the tag `v1.0.0`, and title it **MixCast 1.0.0**.
3. Paste the 1.0.0 section of `CHANGELOG.md` as the description, and add a **Requirements** line:
   > Windows 10 (2004) or 11, 64-bit, and VB-CABLE (free, https://vb-audio.com/Cable/).
4. Attach `MixCast-1.0.0-Setup.exe` and `MixCast-1.0.0-win64-portable.zip`.
5. Publish.

**Add this to your release notes about the SmartScreen warning:**
> Windows may say "Windows protected your PC" because MixCast isn't code-signed yet. Click **More info → Run anyway**.
