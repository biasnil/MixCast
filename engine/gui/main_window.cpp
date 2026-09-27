// MixCast GUI - main window.
#include "main_window.h"
#include "app_picker.h"
#include "channel_strip.h"
#include "editor_page.h"
#include "hotkeys.h"
#include "soundboard_page.h"
#include "devices.h"
#include "theme.h"
#include "widgets.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QUrl>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QButtonGroup>
#include <QScrollArea>
#include <QStackedWidget>
#include <QSignalBlocker>
#include <QSettings>
#include <QSlider>
#include <QSystemTrayIcon>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <optional>

using namespace mixcast;

namespace {

QString FormatDb(float db, int decimals = 1)
{
    if (db <= -59.95f) return QStringLiteral("\u2212\u221E dB");
    QString s = QString::number(std::fabs(db), 'f', decimals);
    if (db < -0.05f)     s.prepend(QChar(0x2212));
    else if (db > 0.05f) s.prepend(QLatin1Char('+'));
    return s + QStringLiteral(" dB");
}

std::unique_ptr<CaptureSource> MakeMic(const Endpoint& ep)
{
    SourceConfig c;
    c.kind     = SourceKind::Microphone;
    c.deviceId = ep.id;
    c.label    = ToUtf8(ep.name);
    return std::make_unique<CaptureSource>(c);
}

std::unique_ptr<CaptureSource> MakeApp(const std::wstring& exe, DWORD pid)
{
    SourceConfig c;
    c.kind    = SourceKind::ProcessLoopback;
    c.pid     = pid;            // 0 = not running yet; attaches when it starts
    c.exeName = exe;
    c.label   = ToUtf8(exe);
    return std::make_unique<CaptureSource>(c);
}

QIcon IconForExe(const QString& path)
{
    if (!path.isEmpty() && QFileInfo::exists(path))
    {
        QIcon ic = QFileIconProvider().icon(QFileInfo(path));
        if (!ic.isNull()) return ic;
    }
    return theme::AppFallbackIcon();
}

QSlider* MakeHSlider(int lo, int hi, int value)
{
    auto* s = new QSlider(Qt::Horizontal);
    s->setRange(lo, hi);
    s->setValue(value);
    s->setFixedWidth(120);
    return s;
}

} // namespace

// ============================================================================
// Construction
// ============================================================================
MainWindow::MainWindow()
{
    setWindowTitle(QStringLiteral("MixCast"));
    setWindowIcon(theme::LogoIcon());
    setMinimumSize(800, 600);
    resize(960, 630);

    soundboard_ = std::make_unique<Soundboard>();
    hotkeys_    = std::make_unique<HotkeyManager>(this);

    auto* central = new QWidget;
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(buildHeader());
    root->addWidget(buildTabs());
    root->addWidget(buildBanner());

    // Page 1: the mixer with its ducking bar. Page 2: the soundboard.
    pages_ = new QStackedWidget;
    auto* mixerPage = new QWidget;
    auto* mp = new QVBoxLayout(mixerPage);
    mp->setContentsMargins(0, 0, 0, 0);
    mp->setSpacing(0);
    mp->addWidget(buildMixer(), 1);
    mp->addWidget(buildDuckBar());
    pages_->addWidget(mixerPage);

    sbPage_ = new SoundboardPage(soundboard_.get(), hotkeys_.get());
    connect(sbPage_, &SoundboardPage::settingsChanged, this, &MainWindow::saveSettingsSoon);
    pages_->addWidget(sbPage_);

    editor_ = new EditorPage(sbPage_);
    pages_->addWidget(editor_);
    connect(sbPage_, &SoundboardPage::editRequested, this, [this](int key, const QString& path, const QString& name) {
        tabs_[2]->click();
        editor_->openPad(key, path, name);
    });
    root->addWidget(pages_, 1);
    setCentralWidget(central);

    setupTray();
    loadGlobalSettings();
    sbPage_->loadSettings();
    tabs_[std::clamp(QSettings().value(QStringLiteral("window/tab"), 0).toInt(), 0, 2)]->click();

    QSettings s;
    restoreGeometry(s.value(QStringLiteral("window/geometry")).toByteArray());

    connect(&tickTimer_, &QTimer::timeout, this, &MainWindow::tick);
    tickTimer_.start(33);   // ~30 fps meters

    saveTimer_.setSingleShot(true);
    saveTimer_.setInterval(800);
    connect(&saveTimer_, &QTimer::timeout, this, &MainWindow::saveSettings);

    startEngine();
}

MainWindow::~MainWindow()
{
    stopEngine();
    delete editor_;          // uses the soundboard page
    editor_ = nullptr;
    delete sbPage_;          // before the soundboard and hotkeys it points at
    sbPage_ = nullptr;
    hotkeys_->clearAll();
}

bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    const int id = HotkeyManager::idFromNativeMessage(message);
    if (id >= 0 && sbPage_ && sbPage_->handleHotkey(id))
    {
        *result = 0;
        return true;
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}

QWidget* MainWindow::buildTabs()
{
    auto* bar = new QFrame;
    bar->setObjectName(QStringLiteral("TabBar"));
    auto* l = new QHBoxLayout(bar);
    l->setContentsMargins(20, 0, 20, 0);
    l->setSpacing(22);

    auto* group = new QButtonGroup(bar);
    const QString names[3] = { QStringLiteral("Mixer"), QStringLiteral("Soundboard"), QStringLiteral("Editor") };
    for (int i = 0; i < 3; i++)
    {
        auto* t = new QPushButton(names[i]);
        t->setObjectName(QStringLiteral("Tab"));
        t->setCheckable(true);
        t->setCursor(Qt::PointingHandCursor);
        group->addButton(t, i);
        l->addWidget(t);
        tabs_ << t;
        connect(t, &QPushButton::clicked, this, [this, i] {
            pages_->setCurrentIndex(i);
            QSettings().setValue(QStringLiteral("window/tab"), i);
        });
    }
    l->addStretch(1);
    return bar;
}

QWidget* MainWindow::buildHeader()
{
    auto* w = new QWidget;
    auto* l = new QHBoxLayout(w);
    l->setContentsMargins(20, 16, 20, 12);
    l->setSpacing(10);

    auto* logo = new QLabel;
    logo->setPixmap(theme::LogoIcon().pixmap(28, 28));
    l->addWidget(logo);

    auto* word = new QLabel(QStringLiteral("MixCast"));
    word->setObjectName(QStringLiteral("Wordmark"));
    word->setFont(theme::Font(17, QFont::DemiBold));
    l->addWidget(word);

    l->addSpacing(24);
    auto* micLbl = new QLabel(QStringLiteral("Microphone"));
    micLbl->setObjectName(QStringLiteral("Muted"));
    l->addWidget(micLbl);

    micCombo_ = new QComboBox;
    micCombo_->setMinimumWidth(200);
    micCombo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    micCombo_->setToolTip(QStringLiteral("The mic your voice comes from"));
    connect(micCombo_, QOverload<int>::of(&QComboBox::activated), this, &MainWindow::onMicChosen);
    l->addWidget(micCombo_);

    l->addSpacing(12);
    auto* outLbl = new QLabel(QStringLiteral("Send to"));
    outLbl->setObjectName(QStringLiteral("Muted"));
    l->addWidget(outLbl);

    outputCombo_ = new QComboBox;
    outputCombo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    outputCombo_->setToolTip(QStringLiteral("The virtual cable your mix goes into. "
                                            "Discord and OBS then use its other end as their mic."));
    connect(outputCombo_, QOverload<int>::of(&QComboBox::activated), this, &MainWindow::onOutputChosen);
    l->addWidget(outputCombo_);

    l->addStretch(1);

    liveDot_ = new QLabel;
    liveDot_->setFixedSize(10, 10);
    l->addWidget(liveDot_);
    liveText_ = new QLabel;
    l->addWidget(liveText_);
    setLive(false, QStringLiteral("Starting\u2026"));
    return w;
}

QWidget* MainWindow::buildBanner()
{
    banner_ = new QFrame;
    banner_->setObjectName(QStringLiteral("Banner"));
    auto* l = new QHBoxLayout(banner_);
    l->setContentsMargins(14, 10, 10, 10);

    bannerText_ = new QLabel;
    bannerText_->setWordWrap(true);
    l->addWidget(bannerText_, 1);

    getCableBtn_ = new QPushButton(QStringLiteral("Get VB-CABLE"));
    getCableBtn_->setObjectName(QStringLiteral("Primary"));
    getCableBtn_->setToolTip(QStringLiteral("Opens vb-audio.com. VB-CABLE is free and needs no test mode."));
    connect(getCableBtn_, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://vb-audio.com/Cable/")));
    });
    l->addWidget(getCableBtn_);

    auto* retry = new QPushButton(QStringLiteral("Try again"));
    connect(retry, &QPushButton::clicked, this, &MainWindow::startEngine);
    l->addWidget(retry);

    // Wrap so the banner gets side margins.
    auto* wrap = new QWidget;
    auto* wl = new QVBoxLayout(wrap);
    wl->setContentsMargins(20, 0, 20, 8);
    wl->addWidget(banner_);
    wrap->hide();
    banner_->setProperty("wrap", QVariant::fromValue<QObject*>(wrap));
    return wrap;
}

QWidget* MainWindow::buildMixer()
{
    auto* w = new QWidget;
    auto* l = new QHBoxLayout(w);
    l->setContentsMargins(20, 4, 20, 16);
    l->setSpacing(16);

    // ---- Scrollable row of channel strips -------------------------------
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* row = new QWidget;
    stripsLayout_ = new QHBoxLayout(row);
    stripsLayout_->setContentsMargins(0, 0, 0, 4);
    stripsLayout_->setSpacing(10);

    addCard_ = new QPushButton(QStringLiteral("+\nAdd app"));
    addCard_->setObjectName(QStringLiteral("AddCard"));
    addCard_->setFixedWidth(120);
    addCard_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    addCard_->setCursor(Qt::PointingHandCursor);
    addCard_->setToolTip(QStringLiteral("Add Spotify, a game, Soundpad or any other app"));
    connect(addCard_, &QPushButton::clicked, this, &MainWindow::onAddAppClicked);

    sbStrip_ = new ChannelStrip(0, StripKind::Soundboard, QStringLiteral("Soundboard"),
                                theme::SoundboardIcon(), &soundboard_->Bus());
    connect(sbStrip_, &ChannelStrip::openRequested, this, [this] { tabs_[1]->click(); });
    connect(sbStrip_, &ChannelStrip::settingsChanged, this, &MainWindow::saveSettingsSoon);
    stripsLayout_->addWidget(sbStrip_);
    stripsLayout_->addWidget(addCard_);
    stripsLayout_->addStretch(1);

    scroll->setWidget(row);
    l->addWidget(scroll, 1);

    // ---- Divider + output strip ----------------------------------------
    auto* divider = new QFrame;
    divider->setObjectName(QStringLiteral("Divider"));
    divider->setFixedWidth(1);
    l->addWidget(divider);

    auto* master = new QFrame;
    master->setObjectName(QStringLiteral("MasterStrip"));
    master->setFixedWidth(140);
    auto* ml = new QVBoxLayout(master);
    ml->setContentsMargins(12, 10, 12, 12);
    ml->setSpacing(8);

    auto* title = new QLabel(QStringLiteral("Output"));
    title->setObjectName(QStringLiteral("StripName"));
    title->setFont(theme::Font(10.5, QFont::DemiBold));
    ml->addWidget(title);

    outputSub_ = new QLabel(QStringLiteral("What Discord hears"));
    outputSub_->setObjectName(QStringLiteral("StripStatus"));
    outputSub_->setWordWrap(true);
    outputSub_->setFixedHeight(30);
    ml->addWidget(outputSub_);

    auto* mid = new QHBoxLayout;
    mid->setSpacing(4);
    mid->addStretch(1);
    outMeter_ = new LevelMeter;
    outMeter_->setFixedWidth(12);
    mid->addWidget(outMeter_);
    masterFader_ = new Fader;
    mid->addWidget(masterFader_);
    mid->addStretch(1);
    ml->addLayout(mid, 1);

    masterDb_ = new QLabel(FormatDb(0));
    masterDb_->setObjectName(QStringLiteral("Db"));
    masterDb_->setAlignment(Qt::AlignCenter);
    ml->addWidget(masterDb_);

    glitches_ = new QLabel;
    glitches_->setObjectName(QStringLiteral("StripStatus"));
    glitches_->setAlignment(Qt::AlignCenter);
    glitches_->setToolTip(QStringLiteral("Times the output ran dry. A few is harmless; "
                                         "a steadily rising count means the PC is overloaded."));
    ml->addWidget(glitches_);

    connect(masterFader_, &QAbstractSlider::valueChanged, this, [this](int v) {
        const float db = Fader::DbFromValue(v);
        masterDb_->setText(FormatDb(db));
        if (engine_) engine_->controls.masterGainDb = db;
        saveSettingsSoon();
    });

    l->addWidget(master);
    return w;
}

QWidget* MainWindow::buildDuckBar()
{
    auto* bar = new QFrame;
    bar->setObjectName(QStringLiteral("DuckBar"));
    auto* l = new QHBoxLayout(bar);
    l->setContentsMargins(20, 12, 20, 12);
    l->setSpacing(10);

    duckToggle_ = new QPushButton(QStringLiteral("Auto-duck"));
    duckToggle_->setToolTip(QStringLiteral("Lower apps automatically while you talk"));
    duckToggle_->setObjectName(QStringLiteral("Toggle"));
    duckToggle_->setCheckable(true);
    duckToggle_->setChecked(true);
    duckToggle_->setMinimumWidth(104);   // room for the bold "checked" label
    l->addWidget(duckToggle_);

    l->addSpacing(12);
    auto* byLbl = new QLabel(QStringLiteral("Lower apps by"));
    byLbl->setObjectName(QStringLiteral("Muted"));
    l->addWidget(byLbl);
    duckDepth_ = MakeHSlider(-30, -3, -12);
    duckDepth_->setToolTip(QStringLiteral("How much quieter apps get while you talk"));
    l->addWidget(duckDepth_);
    duckDepthLbl_ = new QLabel;
    duckDepthLbl_->setFixedWidth(60);
    l->addWidget(duckDepthLbl_);

    l->addSpacing(12);
    auto* thLbl = new QLabel(QStringLiteral("Voice sensitivity"));
    thLbl->setObjectName(QStringLiteral("Muted"));
    l->addWidget(thLbl);
    duckThresh_ = MakeHSlider(-60, -15, -40);
    duckThresh_->setInvertedAppearance(true);   // right = more sensitive (lower threshold)
    duckThresh_->setInvertedControls(true);
    duckThresh_->setToolTip(QStringLiteral("Slide right if quiet speech doesn't lower the apps; "
                                           "left if background noise does"));
    l->addWidget(duckThresh_);
    duckThreshLbl_ = new QLabel;
    duckThreshLbl_->setFixedWidth(60);
    l->addWidget(duckThreshLbl_);

    l->addStretch(1);
    duckNow_ = new QLabel;
    duckNow_->setObjectName(QStringLiteral("Muted"));
    l->addWidget(duckNow_);

    auto apply = [this] {
        const bool on = duckToggle_->isChecked();
        duckDepth_->setEnabled(on);
        duckThresh_->setEnabled(on);
        duckDepthLbl_->setText(FormatDb(static_cast<float>(duckDepth_->value()), 0));
        duckThreshLbl_->setText(FormatDb(static_cast<float>(duckThresh_->value()), 0));
        if (engine_)
        {
            engine_->controls.duckEnabled     = on;
            engine_->controls.duckDepthDb     = static_cast<float>(duckDepth_->value());
            engine_->controls.duckThresholdDb = static_cast<float>(duckThresh_->value());
        }
        saveSettingsSoon();
    };
    connect(duckToggle_, &QPushButton::toggled, this, apply);
    connect(duckDepth_,  &QSlider::valueChanged, this, apply);
    connect(duckThresh_, &QSlider::valueChanged, this, apply);
    apply();
    return bar;
}

void MainWindow::setupTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;

    tray_ = new QSystemTrayIcon(theme::LogoIcon(), this);
    tray_->setToolTip(QStringLiteral("MixCast"));

    auto* menu = new QMenu(this);
    auto* version = menu->addAction(QStringLiteral("MixCast %1").arg(QCoreApplication::applicationVersion()));
    version->setEnabled(false);
    menu->addSeparator();
    menu->addAction(QStringLiteral("Show MixCast"), this, [this] { showNormal(); raise(); activateWindow(); });
    trayMute_ = menu->addAction(QStringLiteral("Mute mic"));
    trayMute_->setCheckable(true);
    connect(trayMute_, &QAction::triggered, this, [this](bool muted) {
        if (auto* c = micControls()) c->enabled = !muted;
        saveSettingsSoon();
    });
    menu->addAction(QStringLiteral("Stop all sounds"), this, [this] { soundboard_->StopAll(); });
    menu->addSeparator();
    menu->addAction(QStringLiteral("Quit MixCast"), this, [this] {
        quitting_ = true;
        if (close()) qApp->quit();
        else quitting_ = false;   // you cancelled (unsaved edits)
    });
    tray_->setContextMenu(menu);

    connect(tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
        if (r == QSystemTrayIcon::Trigger || r == QSystemTrayIcon::DoubleClick)
        {
            showNormal();
            raise();
            activateWindow();
        }
    });
    tray_->show();
}

// ============================================================================
// Engine
// ============================================================================
void MainWindow::stopEngine()
{
    if (!engine_) return;
    engine_->Stop();

    for (auto* s : strips_) s->deleteLater();
    strips_.clear();
    stripOrder_.clear();
    appPaths_.clear();
    engine_.reset();
}

void MainWindow::startEngine()
{
    if (engine_) { saveSettings(); stopEngine(); }
    failedShown_ = false;

    // ---- Which virtual cable? Saved choice, else VB-CABLE, else MixCast driver.
    std::vector<VirtualOutput> outputs;
    try { outputs = ListVirtualOutputs(); } catch (...) {}
    refreshOutputList();

    std::optional<VirtualOutput> out;
    const QString savedOut = QSettings().value(QStringLiteral("output/id")).toString();
    for (auto& v : outputs)
        if (QString::fromStdWString(v.endpoint.id) == savedOut) out = v;
    if (!out && !outputs.empty()) out = outputs.front();

    if (!out)
    {
        showBanner(QStringLiteral("No virtual cable found. Install VB-CABLE (free, no test mode needed), "
                                  "restart your PC if its installer asks, then press Try again."), true);
        setLive(false, QStringLiteral("No cable"));
        refreshMicList(true);
        return;
    }

    liveMicName_ = QString::fromStdWString(out->micName);
    outputSub_->setText(QStringLiteral("What Discord hears on %1").arg(liveMicName_));
    outputCombo_->setCurrentIndex(std::max(0, static_cast<int>(outputIds_.indexOf(QString::fromStdWString(out->endpoint.id)))));

    engine_ = std::make_unique<MixEngine>(out->endpoint.id, 20);
    engine_->controls.duckEnabled     = duckToggle_->isChecked();
    engine_->controls.duckDepthDb     = static_cast<float>(duckDepth_->value());
    engine_->controls.duckThresholdDb = static_cast<float>(duckThresh_->value());
    engine_->controls.masterGainDb    = Fader::DbFromValue(masterFader_->value());

    QSettings s;
    engine_->controls.voice.noiseLevel   = s.value(QStringLiteral("voice/noise"),  int(NoiseMedium)).toInt();
    engine_->controls.voice.gateMode     = s.value(QStringLiteral("voice/gate"),   int(GateGentle)).toInt();
    engine_->controls.voice.rumbleFilter = s.value(QStringLiteral("voice/rumble"), true).toBool();

    // ---- Mic --------------------------------------------------------------
    // No saved choice -> default mic. Saved "" -> user chose no mic.
    std::optional<Endpoint> mic;
    if (s.contains(QStringLiteral("mic/id")))
    {
        const std::wstring id = s.value(QStringLiteral("mic/id")).toString().toStdWString();
        if (!id.empty())
            for (auto& ep : ListEndpoints(eCapture)) if (ep.id == id) mic = ep;
        if (!id.empty() && !mic) mic = DefaultRealMic();   // saved mic unplugged -> fall back
    }
    else
    {
        mic = DefaultRealMic();
    }
    if (mic)
    {
        SourceId id = engine_->AddSource(MakeMic(*mic), true,
                                         s.value(QStringLiteral("mic/gain"), 0.0f).toFloat(), false);
        engine_->Controls(id)->enabled = s.value(QStringLiteral("mic/enabled"), true).toBool();
    }

    // ---- Saved apps --------------------------------------------------------
    const int n = s.beginReadArray(QStringLiteral("apps"));
    for (int i = 0; i < n; i++)
    {
        s.setArrayIndex(i);
        addApp(s.value(QStringLiteral("exe")).toString(),
               s.value(QStringLiteral("path")).toString(),
               s.value(QStringLiteral("gain"), 0.0f).toFloat(),
               s.value(QStringLiteral("enabled"), true).toBool(),
               s.value(QStringLiteral("duck"), true).toBool());
    }
    s.endArray();

    engine_->SetSoundboard(soundboard_.get());
    engine_->Start();
    hideBanner();
    setLive(true, QStringLiteral("Live"));
    liveText_->setToolTip(QStringLiteral("Your mix is going to %1. Pick it as your input device in Discord or OBS.").arg(liveMicName_));
    refreshMicList(true);
    tick();
}

void MainWindow::addApp(const QString& exe, const QString& path, float gainDb, bool enabled, bool duck)
{
    if (!engine_ || exe.isEmpty()) return;
    const std::wstring wexe = exe.toStdWString();
    const DWORD pid = FindRootProcess(wexe).value_or(0);

    SourceId id = engine_->AddSource(MakeApp(wexe, pid), false, gainDb, duck);
    engine_->Controls(id)->enabled = enabled;

    QString p = path;
    if (pid) { const QString live = QString::fromStdWString(ProcessImagePath(pid)); if (!live.isEmpty()) p = live; }
    appPaths_[id] = p;
}

void MainWindow::removeSource(SourceId id)
{
    if (!engine_) return;
    if (auto* strip = strips_.take(id)) strip->deleteLater();   // stop touching its controls first
    engine_->RemoveSource(id);
    appPaths_.remove(id);
    stripOrder_.removeAll(id);
    tick();
    saveSettingsSoon();
}

SourceControls* MainWindow::micControls() const
{
    if (!engine_) return nullptr;
    for (auto& s : engine_->Status()) if (s.isMic) return s.controls;
    return nullptr;
}

void MainWindow::tick()
{
    // Soundboard runs its UI side even when no cable is available.
    sbPage_->tick();
    editor_->tick();
    sbStrip_->refreshSoundboard(sbPage_->soundCount(), sbPage_->playingCount());
    if (tickCount_ % 8 == 0) soundboard_->Maintain();   // ~4x per second

    if (!engine_) { ++tickCount_; return; }

    if (engine_->Failed())
    {
        if (!failedShown_)
        {
            failedShown_ = true;
            showBanner(QStringLiteral("Audio stopped: %1. Check that the virtual cable is still installed and enabled, then try again.")
                           .arg(QString::fromStdString(engine_->Error())));
            setLive(false, QStringLiteral("Stopped"));
        }
        return;
    }

    const auto st = engine_->Status();
    syncStrips(st);

    // Live only while something can actually be heard; grey when everything is off.
    bool anyOn = soundboard_->Bus().enabled && sbPage_->soundCount() > 0;
    for (const auto& s : st) if (s.controls->enabled) anyOn = true;
    const QString liveWanted = anyOn ? QStringLiteral("Live") : QStringLiteral("All off");
    if (liveText_->text() != liveWanted)
    {
        setLive(anyOn, liveWanted);
        const QString tip = anyOn
            ? QStringLiteral("Your mix is going to %1. Pick it as your input device in Discord or OBS.").arg(liveMicName_)
            : QStringLiteral("Everything is switched off, so Discord hears silence.");
        liveText_->setToolTip(tip);
        liveDot_->setToolTip(tip);
    }

    const bool talking = engine_->meters.talking;
    for (const auto& s : st)
        if (auto* strip = strips_.value(s.id)) strip->refresh(s, talking);

    outMeter_->setLevel(engine_->meters.outPeak);

    const float duckDb = LinToDb(engine_->meters.duckGain);
    duckNow_->setText(duckDb < -0.5f ? QStringLiteral("Apps lowered %1").arg(FormatDb(duckDb))
                                     : QStringLiteral("Apps at full level"));

    const uint32_t g = engine_->meters.renderGlitches;
    glitches_->setText(g ? QStringLiteral("%1 dropouts").arg(g) : QString());

    if (trayMute_)
        if (auto* c = micControls()) trayMute_->setChecked(!c->enabled);

    ++tickCount_;
    if (tickCount_ % 30 == 0) engine_->Maintain();       // re-attach reopened apps (~1 s)
    if (tickCount_ % 90 == 0) { refreshMicList(false); refreshOutputList(); }   // devices come and go (~3 s)
}

void MainWindow::syncStrips(const std::vector<SourceStatus>& st)
{
    QList<SourceId> ids;
    for (const auto& s : st) ids << s.id;
    if (ids == stripOrder_) return;

    // Drop strips whose source is gone.
    for (auto it = strips_.begin(); it != strips_.end();)
    {
        if (!ids.contains(it.key())) { it.value()->deleteLater(); it = strips_.erase(it); }
        else ++it;
    }

    // Create strips for new sources.
    for (const auto& s : st)
    {
        if (strips_.contains(s.id)) continue;
        const QString label = QString::fromStdString(s.label);
        const QString name  = s.isMic ? DisplayNameForMic(label) : DisplayNameForExe(label);
        const QIcon   icon  = s.isMic ? theme::MicIcon() : IconForExe(appPaths_.value(s.id));

        auto* strip = new ChannelStrip(s.id, s.isMic ? StripKind::Mic : StripKind::App, name, icon, s.controls,
                                       s.isMic ? &engine_->controls.voice : nullptr);
        connect(strip, &ChannelStrip::removeRequested, this, &MainWindow::removeSource);
        connect(strip, &ChannelStrip::settingsChanged, this, &MainWindow::saveSettingsSoon);
        strips_.insert(s.id, strip);
    }

    // Re-lay out in engine order (mic first), then the add card.
    // Order: mic, soundboard, apps..., add card.
    while (stripsLayout_->count() > 0) delete stripsLayout_->takeAt(0);
    bool sbPlaced = false;
    for (const auto& s : st)
    {
        if (!s.isMic && !sbPlaced) { stripsLayout_->addWidget(sbStrip_); sbPlaced = true; }
        stripsLayout_->addWidget(strips_.value(s.id));
        if (s.isMic) { stripsLayout_->addWidget(sbStrip_); sbPlaced = true; }
    }
    if (!sbPlaced) stripsLayout_->addWidget(sbStrip_);
    stripsLayout_->addWidget(addCard_);
    stripsLayout_->addStretch(1);

    stripOrder_ = ids;
}

void MainWindow::onAddAppClicked()
{
    if (!engine_)
    {
        showBanner(QStringLiteral("Install VB-CABLE first, then press Try again."), true);
        return;
    }

    QStringList already;
    for (const auto& s : engine_->Status())
        if (!s.isMic) already << QString::fromStdString(s.label);

    AppPicker dlg(already, this);
    if (dlg.exec() != QDialog::Accepted) return;

    const PickedApp p = dlg.result();
    addApp(p.exe, p.path, 0.0f, true, p.duck);
    tick();
    saveSettingsSoon();
}

void MainWindow::onMicChosen(int index)
{
    if (index < 0 || index >= micIds_.size()) return;
    const QString id = micIds_[index];

    QSettings().setValue(QStringLiteral("mic/id"), id);
    if (!engine_) return;

    SourceId oldMic = 0;
    for (const auto& s : engine_->Status()) if (s.isMic) oldMic = s.id;

    if (id.isEmpty())
    {
        if (oldMic) removeSource(oldMic);
        return;
    }

    for (auto& ep : ListEndpoints(eCapture))
    {
        if (QString::fromStdWString(ep.id) != id) continue;
        if (oldMic)
        {
            if (auto* strip = strips_.take(oldMic)) strip->deleteLater();
            engine_->ReplaceMic(MakeMic(ep));
        }
        else        engine_->AddSource(MakeMic(ep), true, 0.0f, false);
        stripOrder_.clear();   // force re-layout
        tick();
        saveSettingsSoon();
        return;
    }
}

void MainWindow::onOutputChosen(int index)
{
    if (index < 0 || index >= outputIds_.size()) return;
    QSettings s;
    if (s.value(QStringLiteral("output/id")).toString() == outputIds_[index] && engine_) return;
    s.setValue(QStringLiteral("output/id"), outputIds_[index]);
    startEngine();   // saves the current mix, then rebuilds it on the new cable
}

void MainWindow::refreshOutputList()
{
    std::vector<VirtualOutput> outs;
    try { outs = ListVirtualOutputs(); } catch (...) { return; }

    QStringList ids;
    for (auto& v : outs) ids << QString::fromStdWString(v.endpoint.id);
    if (ids == outputIds_ && outputCombo_->count() > 0) return;

    const QString current = outputIds_.value(outputCombo_->currentIndex());
    QSignalBlocker block(outputCombo_);
    outputCombo_->clear();
    for (auto& v : outs)
    {
        // Just the brand, unless several cables share it (VB-CABLE A+B): then the mic name.
        int sameBrand = 0;
        for (auto& o : outs) if (o.brand == v.brand) sameBrand++;
        outputCombo_->addItem(sameBrand > 1 ? QString::fromStdWString(v.micName)
                                            : QString::fromStdWString(v.brand));
        outputCombo_->setItemData(outputCombo_->count() - 1,
                                  QStringLiteral("Mix goes into %1. In Discord pick %2 as your input device.")
                                      .arg(QString::fromStdWString(v.endpoint.name), QString::fromStdWString(v.micName)),
                                  Qt::ToolTipRole);
    }
    if (outs.empty()) outputCombo_->addItem(QStringLiteral("No virtual cable"));
    outputCombo_->setEnabled(!outs.empty());
    outputIds_ = ids;
    outputCombo_->setCurrentIndex(std::max(0, static_cast<int>(ids.indexOf(current))));
}

void MainWindow::refreshMicList(bool force)
{
    std::vector<Endpoint> mics;
    try { mics = ListEndpoints(eCapture); } catch (...) { return; }
    mics.erase(std::remove_if(mics.begin(), mics.end(),
                              [](const Endpoint& e) { return IsVirtualCable(e.name); }), mics.end());

    QStringList ids{ QString() };
    for (auto& m : mics) ids << QString::fromStdWString(m.id);
    if (!force && ids == micIds_) return;

    // Which mic is live right now?
    QString current;
    if (engine_)
        for (const auto& s : engine_->Status())
            if (s.isMic)
                for (auto& m : mics)
                    if (ToUtf8(m.name) == s.label) current = QString::fromStdWString(m.id);

    QSignalBlocker block(micCombo_);
    micCombo_->clear();
    micCombo_->addItem(QStringLiteral("No microphone"));
    for (auto& m : mics)
    {
        micCombo_->addItem(theme::MicIcon(), DisplayNameForMic(QString::fromStdWString(m.name)) +
                                             (m.isDefault ? QStringLiteral("  (default)") : QString()));
        micCombo_->setItemData(micCombo_->count() - 1, QString::fromStdWString(m.name), Qt::ToolTipRole);
    }
    micIds_ = ids;
    micCombo_->setCurrentIndex(std::max(0, static_cast<int>(micIds_.indexOf(current))));
}

// ============================================================================
// Status display
// ============================================================================
void MainWindow::setLive(bool live, const QString& text)
{
    liveDot_->setStyleSheet(QStringLiteral("background:%1; border-radius:5px;")
                                .arg(live ? theme::Tally.name() : QColor(0x5A, 0x62, 0x6D).name()));
    liveText_->setText(text);
    if (tray_) tray_->setToolTip(QStringLiteral("MixCast: ") + text);
}

void MainWindow::showBanner(const QString& text, bool offerCableDownload)
{
    bannerText_->setText(text);
    getCableBtn_->setVisible(offerCableDownload);
    if (auto* wrap = qobject_cast<QWidget*>(banner_->property("wrap").value<QObject*>())) wrap->show();
}

void MainWindow::hideBanner()
{
    if (auto* wrap = qobject_cast<QWidget*>(banner_->property("wrap").value<QObject*>())) wrap->hide();
}

// ============================================================================
// Settings
// ============================================================================
void MainWindow::loadGlobalSettings()
{
    QSettings s;
    QSignalBlocker b1(duckToggle_), b2(duckDepth_), b3(duckThresh_), b4(masterFader_);
    duckToggle_->setChecked(s.value(QStringLiteral("duck/enabled"), true).toBool());
    duckDepth_->setValue(s.value(QStringLiteral("duck/depth"), -12).toInt());
    duckThresh_->setValue(s.value(QStringLiteral("duck/threshold"), -40).toInt());
    masterFader_->setValue(static_cast<int>(std::lround(s.value(QStringLiteral("master/gain"), 0.0f).toFloat() * 10)));

    duckDepth_->setEnabled(duckToggle_->isChecked());
    duckThresh_->setEnabled(duckToggle_->isChecked());
    duckDepthLbl_->setText(FormatDb(static_cast<float>(duckDepth_->value()), 0));
    duckThreshLbl_->setText(FormatDb(static_cast<float>(duckThresh_->value()), 0));
    masterDb_->setText(FormatDb(Fader::DbFromValue(masterFader_->value())));
}

void MainWindow::saveSettingsSoon()
{
    saveTimer_.start();
}

void MainWindow::saveSettings()
{
    if (sbPage_) sbPage_->saveSettings();
    QSettings s;
    s.setValue(QStringLiteral("window/geometry"), saveGeometry());
    s.setValue(QStringLiteral("duck/enabled"),    duckToggle_->isChecked());
    s.setValue(QStringLiteral("duck/depth"),      duckDepth_->value());
    s.setValue(QStringLiteral("duck/threshold"),  duckThresh_->value());
    s.setValue(QStringLiteral("master/gain"),     Fader::DbFromValue(masterFader_->value()));

    if (!engine_) return;   // keep the saved app list if the engine never started

    s.setValue(QStringLiteral("voice/noise"),  engine_->controls.voice.noiseLevel.load());
    s.setValue(QStringLiteral("voice/gate"),   engine_->controls.voice.gateMode.load());
    s.setValue(QStringLiteral("voice/rumble"), engine_->controls.voice.rumbleFilter.load());

    const auto st = engine_->Status();
    s.remove(QStringLiteral("apps"));
    s.beginWriteArray(QStringLiteral("apps"));
    int i = 0;
    for (const auto& src : st)
    {
        if (src.isMic)
        {
            s.setValue(QStringLiteral("mic/gain"),    src.controls->gainDb.load());
            s.setValue(QStringLiteral("mic/enabled"), src.controls->enabled.load());
            continue;
        }
        s.setArrayIndex(i++);
        s.setValue(QStringLiteral("exe"),     QString::fromStdString(src.label));
        s.setValue(QStringLiteral("path"),    appPaths_.value(src.id));
        s.setValue(QStringLiteral("gain"),    src.controls->gainDb.load());
        s.setValue(QStringLiteral("enabled"), src.controls->enabled.load());
        s.setValue(QStringLiteral("duck"),    src.controls->duckable.load());
    }
    s.endArray();
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    // Really quitting (not just hiding to the tray): don't lose editor work.
    const bool toTray = tray_ && tray_->isVisible() && !quitting_;
    if (!toTray && editor_ && !editor_->confirmDiscard())
    {
        e->ignore();
        return;
    }

    saveSettings();

    if (tray_ && tray_->isVisible() && !quitting_)
    {
        // Keep mixing in the background; the tray icon brings it back.
        hide();
        QSettings s;
        if (!s.value(QStringLiteral("tray/hintShown"), false).toBool())
        {
            tray_->showMessage(QStringLiteral("MixCast is still running"),
                               QStringLiteral("Your mix keeps going. Right-click the tray icon to quit."),
                               theme::LogoIcon(), 4000);
            s.setValue(QStringLiteral("tray/hintShown"), true);
        }
        e->ignore();
        return;
    }

    stopEngine();
    e->accept();
}
