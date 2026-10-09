// MixCast GUI - one channel strip (mic or app).
#pragma once

#include "mix_engine.h"

#include <QFrame>
#include <QIcon>

class QLabel;
class QPushButton;
class QToolButton;
class LevelMeter;
class Fader;
class TallyLamp;

enum class StripKind { Mic, App, Soundboard };

class ChannelStrip : public QFrame
{
    Q_OBJECT
public:
    ChannelStrip(mixcast::SourceId id, StripKind kind, const QString& name, const QIcon& icon,
                 mixcast::SourceControls* controls, mixcast::VoiceSettings* voice = nullptr,
                 QWidget* parent = nullptr);

    mixcast::SourceId id() const { return id_; }
    bool isMic() const { return isMic_; }

    // Called every UI tick with the latest engine status.
    void refresh(const mixcast::SourceStatus& st, bool talking);

    // Soundboard strip only.
    void refreshSoundboard(int sounds, int playing);

signals:
    void removeRequested(mixcast::SourceId id);
    void openRequested();   // soundboard strip: show the pads
    void settingsChanged();

private:
    void syncFromControls();
    void updateDbLabel();
    void setStatus(const QString& text, bool warn);
    void buildNoiseButton();
    void updateNoiseButton();

    mixcast::SourceId        id_;
    bool                     isMic_;
    StripKind                kind_;
    mixcast::SourceControls* ctl_;
    mixcast::VoiceSettings*  voice_ = nullptr;
    QToolButton*             noiseBtn_ = nullptr;
    class CleanupScope*      scope_ = nullptr;     // mic only
    int                      shownNoise_ = -1, shownGate_ = -1;
    bool                     shownRumble_ = false;
    int                      shownPolish_ = -1;

    QLabel*      status_  = nullptr;
    QLabel*      db_      = nullptr;
    LevelMeter*  meter_   = nullptr;
    Fader*       fader_   = nullptr;
    QPushButton* onBtn_   = nullptr;
    QPushButton* duckBtn_ = nullptr;
    TallyLamp*   tally_   = nullptr;
    QLabel*      talking_ = nullptr;
    QString      lastStatus_;
    bool         lastWarn_ = false;
};

// Friendly display names: "Microphone (HyperX Cloud)" -> "HyperX Cloud",
// "Spotify.exe" -> "Spotify".
QString DisplayNameForMic(const QString& deviceName);
QString DisplayNameForExe(const QString& exe);
