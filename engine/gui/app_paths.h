// MixCast GUI - where MixCast keeps its files.
//
// Everything lives in %LOCALAPPDATA%\MixCast:
//   MixCast.ini   all settings (mixer, mic clean-up, soundboard pads, window)
//   Sounds\       copies of soundboard files, so pads keep working if the
//                 originals are moved or deleted
#pragma once

#include <QString>

// %LOCALAPPDATA%\MixCast (created if missing).
QString MixCastDataDir();

// %LOCALAPPDATA%\MixCast\Sounds (created if missing).
QString MixCastSoundsDir();

// Call once at startup, before any QSettings is used. Switches settings to
// %LOCALAPPDATA%\MixCast\MixCast.ini and moves over anything an older
// version stored in the registry.
void SetupSettingsStorage();

// Copies `path` into the Sounds folder (unless it's already there) and returns
// the new path. Returns the original path if copying fails.
QString StoreSoundFile(const QString& path);

// True if `path` is one of MixCast's own copies in the Sounds folder.
bool IsStoredSound(const QString& path);
