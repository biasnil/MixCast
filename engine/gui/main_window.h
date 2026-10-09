// MixCast GUI - main window.
#pragma once

#include "mix_engine.h"
#include "soundboard.h"

#include <QList>
#include <QMainWindow>
#include <QMap>
#include <QTimer>

#include <memory>

class QAction;
class QComboBox;
class QFrame;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QSlider;
class QToolButton;
class QSystemTrayIcon;
class QStackedWidget;
class HotkeyManager;
class SoundboardPage;
class EditorPage;
class ChannelStrip;
class Fader;
class LevelMeter;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow();
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* e) override;
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    // UI construction
    QWidget* buildHeader();
    QWidget* buildBanner();
    QWidget* buildTabs();
    QWidget* buildMixer();
    QWidget* buildDuckBar();
    void     setupTray();

    // Engine
    void startEngine();
    void stopEngine();
    void tick();
    void syncStrips(const std::vector<mixcast::SourceStatus>& st);
    mixcast::SourceId addApp(const QString& exe, const QString& path, float gainDb, bool enabled, bool duck);
    void setOutputB(const QString& id);       // "" off, "default" headphones, else an endpoint id
    void fillOutputBMenu(class QMenu* menu);
    void removeSource(mixcast::SourceId id);
    void onAddAppClicked();
    void onMicChosen(int index);
    void onOutputChosen(int index);
    void refreshOutputList();
    void refreshMicList(bool force);
    mixcast::SourceControls* micControls() const;

    // State display
    void setLive(bool live, const QString& text);
    void showBanner(const QString& text, bool offerCableDownload = false);
    void hideBanner();

    // Settings
    void loadGlobalSettings();
    void saveSettings();
    void saveSettingsSoon();
    void loadVoiceProfile(const QString& micId);
    void saveVoiceProfile();

    // Declared before engine_ so they outlive it.
    std::unique_ptr<mixcast::Soundboard> soundboard_;
    std::unique_ptr<HotkeyManager>       hotkeys_;
    std::unique_ptr<mixcast::MixEngine>  engine_;

    SoundboardPage*  sbPage_  = nullptr;
    EditorPage*      editor_  = nullptr;
    ChannelStrip*    sbStrip_ = nullptr;
    QStackedWidget*  pages_   = nullptr;
    QList<QPushButton*> tabs_;

    QMap<mixcast::SourceId, ChannelStrip*> strips_;
    QMap<mixcast::SourceId, QString>       appPaths_;
    QList<mixcast::SourceId>               stripOrder_;
    QStringList                            micIds_;
    QStringList                            outputIds_;
    QString                                profileMicId_;  // mic whose voice profile is loaded
    std::wstring                           outputAId_;     // the cable the mix goes into
    QString                                liveMicName_;   // what Discord should pick, e.g. "CABLE Output"

    QHBoxLayout* stripsLayout_ = nullptr;
    QPushButton* addCard_      = nullptr;
    QComboBox*   micCombo_     = nullptr;
    QComboBox*   outputCombo_  = nullptr;
    QFrame*      livePill_     = nullptr;
    QLabel*      outputSub_    = nullptr;
    QPushButton* getCableBtn_  = nullptr;
    QToolButton* outBPick_     = nullptr;
    QSlider*     outBLevel_    = nullptr;
    QLabel*      outBLevelLbl_ = nullptr;
    QLabel*      soloNote_     = nullptr;
    QLabel*      duckThreshName_ = nullptr;
    QLabel*      duckFollows_  = nullptr;
    QLabel*      liveDot_      = nullptr;
    QLabel*      liveText_     = nullptr;
    QFrame*      banner_       = nullptr;
    QLabel*      bannerText_   = nullptr;

    LevelMeter*  outMeter_     = nullptr;
    Fader*       masterFader_  = nullptr;
    QLabel*      masterDb_     = nullptr;
    QLabel*      glitches_     = nullptr;

    QPushButton* duckToggle_   = nullptr;
    QSlider*     duckDepth_    = nullptr;
    QSlider*     duckThresh_   = nullptr;
    QLabel*      duckDepthLbl_ = nullptr;
    QLabel*      duckThreshLbl_= nullptr;
    QLabel*      duckNow_      = nullptr;

    QSystemTrayIcon* tray_     = nullptr;
    QAction*     trayMute_     = nullptr;

    QTimer       tickTimer_;
    QTimer       saveTimer_;
    int          tickCount_    = 0;
    bool         quitting_     = false;
    bool         failedShown_  = false;
};
