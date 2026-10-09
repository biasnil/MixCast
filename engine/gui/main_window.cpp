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
#include <QCryptographicHash>
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
#include <QStyle>
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

// Each mic has its own voice profile: a headset and a desk mic hear you differently.
QString ProfileKey(const QString& micId)
{
    const QByteArray h = QCryptographicHash::hash(micId.toUtf8(), QCryptographicHash::Md5).toHex().left(16);
    return QStringLiteral("voice/profiles/") + QString::fromLatin1(h);
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

    // What the mic learns about your voice is kept even if MixCast is killed.
    auto* learnSave = new QTimer(this);
    connect(learnSave, &QTimer::timeout, this, &MainWindow::saveSettings);
    learnSave->start(60 * 1000);

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
    const QString names[3] = { QStringLiteral("MIXER"), QStringLiteral("SOUNDBOARD"), QStringLiteral("EDITOR") };
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
    w->setObjectName(QStringLiteral("Header"));
    w->setAttribute(Qt::WA_StyledBackground);
    auto* l = new QHBoxLayout(w);
    l->setContentsMargins(20, 14, 20, 14);
    l->setSpacing(10);

    // Wordmark: "MIX" on a mint badge, "CAST" beside it.
    auto* badge = new QLabel(QStringLiteral("MIX"));
    badge->setObjectName(QStringLiteral("Badge"));
    l->addWidget(badge);
    auto* word = new QLabel(QStringLiteral("CAST"));
    word->setObjectName(QStringLiteral("Wordmark"));
    l->addWidget(word);
    l->addSpacing(24);

    auto* micLbl = new QLabel(QStringLiteral("MICROPHONE"));
    micLbl->setObjectName(QStringLiteral("HeaderLabel"));
    l->addWidget(micLbl);

    micCombo_ = new QComboBox;
    micCombo_->setMinimumWidth(200);
    micCombo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    micCombo_->setToolTip(QStringLiteral("The mic your voice comes from"));
    connect(micCombo_, QOverload<int>::of(&QComboBox::activated), this, &MainWindow::onMicChosen);
    l->addWidget(micCombo_);

    l->addSpacing(12);
    auto* outLbl = new QLabel(QStringLiteral("SEND TO"));
    outLbl->setObjectName(QStringLiteral("HeaderLabel"));
    l->addWidget(outLbl);

    outputCombo_ = new QComboBox;
    outputCombo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    outputCombo_->setToolTip(QStringLiteral("The virtual cable your mix goes into. "
                                            "Discord and OBS then use its other end as their mic."));
    connect(outputCombo_, QOverload<int>::of(&QComboBox::activated), this, &MainWindow::onOutputChosen);
    l->addWidget(outputCombo_);

    l->addStretch(1);

    // On-air pill: glows red while Discord can hear you.
    livePill_ = new QFrame;
    livePill_->setObjectName(QStringLiteral("LivePill"));
    auto* pl = new QHBoxLayout(livePill_);
    pl->setContentsMargins(10, 4, 12, 4);
    pl->setSpacing(7);
    liveDot_ = new QLabel;
    liveDot_->setObjectName(QStringLiteral("LiveDot"));
    liveDot_->setFixedSize(10, 10);
    pl->addWidget(liveDot_);
    liveText_ = new QLabel;
    pl->addWidget(liveText_);
    l->addWidget(livePill_);
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

    // One console panel; each channel is a column with a divider on its right.
    auto* row = new QWidget;
    row->setObjectName(QStringLiteral("Console"));
    row->setAttribute(Qt::WA_StyledBackground);
    stripsLayout_ = new QHBoxLayout(row);
    stripsLayout_->setContentsMargins(0, 0, 0, 0);
    stripsLayout_->setSpacing(0);

    addCard_ = new QPushButton(QStringLiteral("+\nADD APP"));
    addCard_->setObjectName(QStringLiteral("AddCard"));
    addCard_->setFixedWidth(110);
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

    // Output: what Discord hears. Same parts as a channel, set apart.
    auto* master = new QFrame;
    master->setObjectName(QStringLiteral("MasterStrip"));
    master->setFixedWidth(150);
    auto* ml = new QVBoxLayout(master);
    ml->setContentsMargins(12, 12, 12, 14);
    ml->setSpacing(10);

    auto* titles = new QVBoxLayout;
    titles->setSpacing(1);
    auto* title = new QLabel(QStringLiteral("OUTPUT"));
    title->setObjectName(QStringLiteral("StripTitle"));
    titles->addWidget(title);
    outputSub_ = new QLabel(QStringLiteral("What Discord hears"));
    outputSub_->setObjectName(QStringLiteral("StripSub"));
    titles->addWidget(outputSub_);
    ml->addLayout(titles);

    auto* panel = new QFrame;
    panel->setObjectName(QStringLiteral("InfoPanel"));
    panel->setFixedHeight(100);
    auto* pl = new QVBoxLayout(panel);
    pl->setContentsMargins(8, 6, 8, 7);
    // Output B: where B-sent and soloed channels play, with its own level.
    pl->setSpacing(4);
    auto* pLbl = new QLabel(QStringLiteral("OUTPUT B"));
    pLbl->setObjectName(QStringLiteral("PanelLabel"));
    pl->addWidget(pLbl);
    outBPick_ = new QToolButton;
    outBPick_->setObjectName(QStringLiteral("PanelPick"));
    outBPick_->setPopupMode(QToolButton::InstantPopup);
    outBPick_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    outBPick_->setCursor(Qt::PointingHandCursor);
    outBPick_->setToolTip(QStringLiteral("Where output B plays: your headphones (to hear yourself or a soloed channel), "
                                         "or a second output such as another virtual cable for OBS"));
    auto* bMenu = new QMenu(outBPick_);
    connect(bMenu, &QMenu::aboutToShow, this, [this, bMenu] { fillOutputBMenu(bMenu); });
    outBPick_->setMenu(bMenu);
    pl->addWidget(outBPick_);

    auto* bRow = new QHBoxLayout;
    bRow->setSpacing(6);
    outBLevel_ = new QSlider(Qt::Horizontal);
    outBLevel_->setRange(-30, 6);
    outBLevel_->setValue(0);
    outBLevel_->setToolTip(QStringLiteral("Output B level"));
    bRow->addWidget(outBLevel_, 1);
    outBLevelLbl_ = new QLabel;
    outBLevelLbl_->setObjectName(QStringLiteral("StripStatus"));
    outBLevelLbl_->setFixedWidth(34);
    outBLevelLbl_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    bRow->addWidget(outBLevelLbl_);
    pl->addLayout(bRow);
    connect(outBLevel_, &QSlider::valueChanged, this, [this](int v) {
        outBLevelLbl_->setText(FormatDb(static_cast<float>(v), 0).replace(QStringLiteral(" dB"), QString()));
        if (engine_) engine_->controls.masterBGainDb = static_cast<float>(v);
        saveSettingsSoon();
    });
    outBLevelLbl_->setText(QStringLiteral("0"));

    soloNote_ = new QLabel(QStringLiteral("SOLO ON B"));
    soloNote_->setObjectName(QStringLiteral("SoloNote"));
    soloNote_->setAlignment(Qt::AlignCenter);
    soloNote_->setToolTip(QStringLiteral("A channel is soloed: output B plays only soloed channels. Discord isn't affected."));
    soloNote_->hide();
    pl->addWidget(soloNote_);
    glitches_ = new QLabel;
    glitches_->setObjectName(QStringLiteral("StripStatus"));
    glitches_->setAlignment(Qt::AlignCenter);
    glitches_->setToolTip(QStringLiteral("Times the output ran dry. A few is harmless; "
                                         "a steadily rising count means the PC is overloaded."));
    pl->addWidget(glitches_);
    pl->addStretch(1);
    ml->addWidget(panel);

    masterDb_ = new QLabel;
    masterDb_->setObjectName(QStringLiteral("BigDb"));
    masterDb_->setAlignment(Qt::AlignRight | Qt::AlignBottom);
    masterDb_->setTextFormat(Qt::RichText);
    masterDb_->setText(BigDbText(0));
    ml->addWidget(masterDb_);

    auto* mid = new QHBoxLayout;
    mid->setSpacing(10);
    mid->addStretch(1);
    outMeter_ = new LevelMeter;
    outMeter_->setFixedWidth(22);
    mid->addWidget(outMeter_);
    masterFader_ = new Fader;
    mid->addWidget(masterFader_);
    mid->addStretch(1);
    ml->addLayout(mid, 1);

    connect(masterFader_, &QAbstractSlider::valueChanged, this, [this](int v) {
        const float db = Fader::DbFromValue(v);
        masterDb_->setText(BigDbText(db));
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
    duckThreshName_ = new QLabel(QStringLiteral("Voice sensitivity"));
    duckThreshName_->setObjectName(QStringLiteral("Muted"));
    l->addWidget(duckThreshName_);
    duckThresh_ = MakeHSlider(-60, -15, -40);
    duckThresh_->setInvertedAppearance(true);   // right = more sensitive (lower threshold)
    duckThresh_->setInvertedControls(true);
    duckThresh_->setToolTip(QStringLiteral("Slide right if quiet speech doesn't lower the apps; "
                                           "left if background noise does. Once MixCast has learned "
                                           "your voice, ducking follows your voice instead."));
    l->addWidget(duckThresh_);
    duckThreshLbl_ = new QLabel;
    duckThreshLbl_->setFixedWidth(60);
    l->addWidget(duckThreshLbl_);

    // Once your voice is learned, ducking follows it and the slider has nothing to do.
    duckFollows_ = new QLabel(QStringLiteral("Follows your voice"));
    duckFollows_->setObjectName(QStringLiteral("Muted"));
    duckFollows_->setToolTip(QStringLiteral("MixCast knows your voice, so apps duck only when you speak: "
                                            "keyboard, clicks and other people don't count."));
    duckFollows_->hide();
    l->addWidget(duckFollows_);

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
    outputSub_->setText(liveMicName_);   // the device Discord should pick
    outputSub_->setToolTip(QStringLiteral("What Discord hears. In Discord, pick %1 as your input device.").arg(liveMicName_));
    outputCombo_->setCurrentIndex(std::max(0, static_cast<int>(outputIds_.indexOf(QString::fromStdWString(out->endpoint.id)))));

    engine_ = std::make_unique<MixEngine>(out->endpoint.id, 20);
    outputAId_ = out->endpoint.id;
    engine_->controls.duckEnabled     = duckToggle_->isChecked();
    engine_->controls.duckDepthDb     = static_cast<float>(duckDepth_->value());
    engine_->controls.duckThresholdDb = static_cast<float>(duckThresh_->value());
    engine_->controls.masterGainDb    = Fader::DbFromValue(masterFader_->value());
    engine_->controls.masterBGainDb   = static_cast<float>(outBLevel_->value());

    QSettings s;
    engine_->controls.voice.noiseLevel   = s.value(QStringLiteral("voice/noise"),  int(NoiseMedium)).toInt();
    engine_->controls.voice.gateMode     = s.value(QStringLiteral("voice/gate"),   int(GateGentle)).toInt();
    engine_->controls.voice.rumbleFilter = s.value(QStringLiteral("voice/rumble"), true).toBool();
    engine_->controls.voice.deEsser      = s.value(QStringLiteral("voice/deEsser"),    false).toBool();
    engine_->controls.voice.voiceEq      = s.value(QStringLiteral("voice/eq"),         false).toBool();
    engine_->controls.voice.compressor   = s.value(QStringLiteral("voice/compressor"), false).toBool();
    engine_->controls.voice.limiter      = s.value(QStringLiteral("voice/limiter"),    true).toBool();
    engine_->controls.voice.learnVoice        = s.value(QStringLiteral("voice/learn"),     true).toBool();
    engine_->controls.voice.autoLevel         = s.value(QStringLiteral("voice/autoLevel"), false).toBool();
    engine_->controls.voice.cleanWhileTalking = s.value(QStringLiteral("voice/cleanTalk"), false).toBool();
    engine_->controls.voice.removeClicks      = s.value(QStringLiteral("voice/clicks"),    false).toBool();
    engine_->controls.voice.voiceFx           = s.value(QStringLiteral("voice/fx"),        0).toInt();

    // ---- Mic --------------------------------------------------------------
    // No saved choice -> default mic. Saved "" -> user chose no mic.
    profileMicId_.clear();
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
        loadVoiceProfile(QString::fromStdWString(mic->id));
        SourceId id = engine_->AddSource(MakeMic(*mic), true,
                                         s.value(QStringLiteral("mic/gain"), 0.0f).toFloat(), false);
        engine_->Controls(id)->enabled = s.value(QStringLiteral("mic/enabled"), true).toBool();
        LoadChannelSettings(s, QStringLiteral("mic/"), *engine_->Controls(id));
    }

    // ---- Saved apps --------------------------------------------------------
    const int n = s.beginReadArray(QStringLiteral("apps"));
    for (int i = 0; i < n; i++)
    {
        s.setArrayIndex(i);
        const SourceId app = addApp(s.value(QStringLiteral("exe")).toString(),
                                    s.value(QStringLiteral("path")).toString(),
                                    s.value(QStringLiteral("gain"), 0.0f).toFloat(),
                                    s.value(QStringLiteral("enabled"), true).toBool(),
                                    s.value(QStringLiteral("duck"), true).toBool());
        if (auto* c = app ? engine_->Controls(app) : nullptr) LoadChannelSettings(s, QString(), *c);
    }
    s.endArray();

    engine_->SetSoundboard(soundboard_.get());
    setOutputB(s.value(QStringLiteral("outputB/id"), QStringLiteral("default")).toString());
    engine_->Start();
    hideBanner();
    setLive(true, QStringLiteral("Live"));
    liveText_->setToolTip(QStringLiteral("Your mix is going to %1. Pick it as your input device in Discord or OBS.").arg(liveMicName_));
    refreshMicList(true);
    tick();
}

SourceId MainWindow::addApp(const QString& exe, const QString& path, float gainDb, bool enabled, bool duck)
{
    if (!engine_ || exe.isEmpty()) return 0;
    const std::wstring wexe = exe.toStdWString();
    const DWORD pid = FindRootProcess(wexe).value_or(0);

    SourceId id = engine_->AddSource(MakeApp(wexe, pid), false, gainDb, duck);
    engine_->Controls(id)->enabled = enabled;

    QString p = path;
    if (pid) { const QString live = QString::fromStdWString(ProcessImagePath(pid)); if (!live.isEmpty()) p = live; }
    appPaths_[id] = p;
    return id;
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

    const bool byVoice = engine_->controls.voice.profileInUse;
    if (duckFollows_->isHidden() == byVoice)
    {
        duckFollows_->setVisible(byVoice);
        for (QWidget* w : { static_cast<QWidget*>(duckThreshName_), static_cast<QWidget*>(duckThresh_),
                            static_cast<QWidget*>(duckThreshLbl_) })
            w->setVisible(!byVoice);
    }

    const float duckDb = LinToDb(engine_->meters.duckGain);
    duckNow_->setText(duckDb < -0.5f ? QStringLiteral("Apps lowered %1").arg(FormatDb(duckDb))
                                     : QStringLiteral("Apps at full level"));

    const uint32_t g = engine_->meters.renderGlitches;
    glitches_->setText(g ? QStringLiteral("%1 dropouts").arg(g) : QString());
    soloNote_->setVisible(engine_->meters.soloOn);
    const bool haveB = !engine_->OutputB().empty();
    for (auto* strip : strips_) strip->setOutputBAvailable(haveB);
    sbStrip_->setOutputBAvailable(haveB);

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

    // Keep what was learned about the old mic, and bring back this one's.
    saveVoiceProfile();
    if (id.isEmpty()) profileMicId_.clear();
    else              loadVoiceProfile(id);

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
    for (QWidget* w : { static_cast<QWidget*>(livePill_), static_cast<QWidget*>(liveDot_) })
    {
        w->setProperty("live", live);
        w->style()->unpolish(w);
        w->style()->polish(w);
    }
    liveText_->setText(text.toUpper());
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
    QSignalBlocker b1(duckToggle_), b2(duckDepth_), b3(duckThresh_), b4(masterFader_), b5(outBLevel_);
    outBLevel_->setValue(s.value(QStringLiteral("outputB/gain"), 0).toInt());
    outBLevelLbl_->setText(FormatDb(static_cast<float>(outBLevel_->value()), 0).replace(QStringLiteral(" dB"), QString()));
    duckToggle_->setChecked(s.value(QStringLiteral("duck/enabled"), true).toBool());
    duckDepth_->setValue(s.value(QStringLiteral("duck/depth"), -12).toInt());
    duckThresh_->setValue(s.value(QStringLiteral("duck/threshold"), -40).toInt());
    masterFader_->setValue(static_cast<int>(std::lround(s.value(QStringLiteral("master/gain"), 0.0f).toFloat() * 10)));

    duckDepth_->setEnabled(duckToggle_->isChecked());
    duckThresh_->setEnabled(duckToggle_->isChecked());
    duckDepthLbl_->setText(FormatDb(static_cast<float>(duckDepth_->value()), 0));
    duckThreshLbl_->setText(FormatDb(static_cast<float>(duckThresh_->value()), 0));
    masterDb_->setText(BigDbText(Fader::DbFromValue(masterFader_->value())));
}

// ---- Output B --------------------------------------------------------------
void MainWindow::setOutputB(const QString& id)
{
    if (!engine_) return;
    engine_->SetOutputB(id.toStdWString());
    QString name = QStringLiteral("Off");
    if (id == QStringLiteral("default")) name = QStringLiteral("Headphones");
    else if (!id.isEmpty())
    {
        name = QStringLiteral("Unplugged");
        try
        {
            for (auto& ep : ListEndpoints(eRender))
                if (QString::fromStdWString(ep.id) == id) name = DisplayNameForMic(QString::fromStdWString(ep.name));
        }
        catch (...) {}
    }
    outBPick_->setText(QFontMetrics(outBPick_->font()).elidedText(name, Qt::ElideRight, 92) + QStringLiteral(" \u25BE"));
}

void MainWindow::fillOutputBMenu(QMenu* menu)
{
    menu->clear();
    const QString current = engine_ ? QString::fromStdWString(engine_->OutputB()) : QString();
    auto add = [&](const QString& text, const QString& id, const QString& tip) {
        auto* a = menu->addAction(text);
        a->setCheckable(true);
        a->setChecked(id == current);
        a->setToolTip(tip);
        connect(a, &QAction::triggered, this, [this, id] { setOutputB(id); saveSettingsSoon(); });
    };
    add(QStringLiteral("Off"), QString(), QStringLiteral("No output B"));
    add(QStringLiteral("Your headphones (Windows default)"), QStringLiteral("default"),
        QStringLiteral("Follows your Windows default playback device"));
    menu->addSeparator();
    try
    {
        for (auto& ep : ListEndpoints(eRender))
        {
            const QString id = QString::fromStdWString(ep.id);
            if (ep.id == outputAId_) continue;   // that's output A
            add(QString::fromStdWString(ep.name), id, QString());
        }
    }
    catch (...) {}
    menu->setToolTipsVisible(true);
}

void MainWindow::loadVoiceProfile(const QString& micId)
{
    if (!engine_) return;
    QSettings s;
    QByteArray bytes = s.value(ProfileKey(micId)).toByteArray();
    if (bytes.isEmpty())   // older versions kept one profile for every mic: it goes to the first mic used
        bytes = s.value(QStringLiteral("voice/profile")).toByteArray();

    VoiceProfile learned;
    if (!VoiceProfile::Deserialize(bytes.toStdString(), learned)) learned = VoiceProfile{};
    engine_->controls.voice.LoadProfile(learned);
    profileMicId_ = micId;
}

void MainWindow::saveVoiceProfile()
{
    if (!engine_ || profileMicId_.isEmpty()) return;
    const std::string learned = engine_->controls.voice.CopyProfile().Serialize();
    QSettings s;
    s.setValue(ProfileKey(profileMicId_), QByteArray(learned.data(), static_cast<int>(learned.size())));
    s.remove(QStringLiteral("voice/profile"));   // migrated
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
    s.setValue(QStringLiteral("outputB/gain"),    outBLevel_->value());
    if (engine_) s.setValue(QStringLiteral("outputB/id"), QString::fromStdWString(engine_->OutputB()));

    if (!engine_) return;   // keep the saved app list if the engine never started

    s.setValue(QStringLiteral("voice/noise"),  engine_->controls.voice.noiseLevel.load());
    s.setValue(QStringLiteral("voice/gate"),   engine_->controls.voice.gateMode.load());
    s.setValue(QStringLiteral("voice/rumble"), engine_->controls.voice.rumbleFilter.load());
    s.setValue(QStringLiteral("voice/deEsser"),    engine_->controls.voice.deEsser.load());
    s.setValue(QStringLiteral("voice/eq"),         engine_->controls.voice.voiceEq.load());
    s.setValue(QStringLiteral("voice/compressor"), engine_->controls.voice.compressor.load());
    s.setValue(QStringLiteral("voice/limiter"),    engine_->controls.voice.limiter.load());
    s.setValue(QStringLiteral("voice/learn"),      engine_->controls.voice.learnVoice.load());
    s.setValue(QStringLiteral("voice/autoLevel"),  engine_->controls.voice.autoLevel.load());
    s.setValue(QStringLiteral("voice/cleanTalk"),  engine_->controls.voice.cleanWhileTalking.load());
    s.setValue(QStringLiteral("voice/clicks"),     engine_->controls.voice.removeClicks.load());
    s.setValue(QStringLiteral("voice/fx"),         engine_->controls.voice.voiceFx.load());
    saveVoiceProfile();

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
            SaveChannelSettings(s, QStringLiteral("mic/"), *src.controls);
            continue;
        }
        s.setArrayIndex(i++);
        s.setValue(QStringLiteral("exe"),     QString::fromStdString(src.label));
        s.setValue(QStringLiteral("path"),    appPaths_.value(src.id));
        s.setValue(QStringLiteral("gain"),    src.controls->gainDb.load());
        s.setValue(QStringLiteral("enabled"), src.controls->enabled.load());
        s.setValue(QStringLiteral("duck"),    src.controls->duckable.load());
        SaveChannelSettings(s, QString(), *src.controls);
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
