// MixCast GUI - one channel strip (mic or app).
#pragma once

#include "mix_engine.h"

#include <QFrame>
#include <QIcon>

class QLabel;
class QPushButton;
class QToolButton;
class QSettings;
class LevelMeter;
class Fader;
class TallyLamp;
class Knob;
class PanBar;

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

    // Output B exists (a device is chosen): B and SOLO only make sense then.
    void setOutputBAvailable(bool available);

    // After a theme change: redraw what has colours baked in.
    void refreshTheme();

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
    void updateFxButton();

    mixcast::SourceId        id_;
    bool                     isMic_;
    StripKind                kind_;
    mixcast::SourceControls* ctl_;
    mixcast::VoiceSettings*  voice_ = nullptr;
    QToolButton*             noiseBtn_ = nullptr;
    QToolButton*             fxBtn_ = nullptr;
    int                      shownFx_ = -1;
    class CleanupScope*      scope_ = nullptr;     // mic only
    int                      shownNoise_ = -1, shownGate_ = -1;
    bool                     shownRumble_ = false;
    int                      shownPolish_ = -1;

    QLabel*      status_  = nullptr;
    QLabel*      iconLbl_ = nullptr;
    QIcon        icon_;
    QLabel*      db_      = nullptr;
    LevelMeter*  meter_   = nullptr;
    Fader*       fader_   = nullptr;
    QPushButton* onBtn_   = nullptr;
    QPushButton* duckBtn_ = nullptr;
    QPushButton* aBtn_    = nullptr;
    QPushButton* bBtn_    = nullptr;
    QPushButton* soloBtn_ = nullptr;
    Knob*        bassK_   = nullptr;
    Knob*        midK_    = nullptr;
    Knob*        trebleK_ = nullptr;
    PanBar*      pan_     = nullptr;
    TallyLamp*   tally_   = nullptr;
    QString      lastStatus_;
    bool         lastWarn_ = false;
};

// Friendly display names: "Microphone (HyperX Cloud)" -> "HyperX Cloud",
// "Spotify.exe" -> "Spotify".
QString DisplayNameForMic(const QString& deviceName);
QString DisplayNameForExe(const QString& exe);

// Rich text for a channel's big gain readout ("-6.0 dB").
QString BigDbText(float db);

// A channel's routing, tone, pan and width, under `prefix` in the settings
// ("" inside an array entry, "mic/", "soundboard/bus" ...). Solo isn't saved.
void SaveChannelSettings(QSettings& s, const QString& prefix, const mixcast::SourceControls& c);
void LoadChannelSettings(QSettings& s, const QString& prefix, mixcast::SourceControls& c);
