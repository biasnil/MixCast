// MixCast GUI - one channel strip (mic or app).
#include "channel_strip.h"
#include "theme.h"
#include "widgets.h"

#include <QFontMetrics>
#include <QSignalBlocker>
#include <QHBoxLayout>
#include <QActionGroup>
#include <QLabel>
#include <QMessageBox>
#include <QMenu>
#include <QPushButton>
#include <QRegularExpression>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

QString DisplayNameForMic(const QString& deviceName)
{
    // "Microphone (HyperX Cloud Flight)" -> "HyperX Cloud Flight"
    static const QRegularExpression re(QStringLiteral(R"(\((.+)\)\s*$)"));
    const auto m = re.match(deviceName);
    return m.hasMatch() ? m.captured(1) : deviceName;
}

QString DisplayNameForExe(const QString& exe)
{
    QString n = exe;
    if (n.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) n.chop(4);
    return n;
}

static void Repolish(QWidget* w)
{
    w->style()->unpolish(w);
    w->style()->polish(w);
}

ChannelStrip::ChannelStrip(mixcast::SourceId id, StripKind kind, const QString& name, const QIcon& icon,
                           mixcast::SourceControls* controls, mixcast::VoiceSettings* voice, QWidget* parent)
    : QFrame(parent), id_(id), isMic_(kind == StripKind::Mic), kind_(kind), ctl_(controls), voice_(voice)
{
    const bool isMic = isMic_;
    setObjectName(QStringLiteral("Strip"));
    setFixedWidth(140);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 10, 12, 12);
    root->setSpacing(8);

    // ---- Header: icon + remove on one row, full-width name below ------
    auto* header = new QHBoxLayout;
    header->setSpacing(6);
    auto* iconLbl = new QLabel;
    iconLbl->setPixmap(icon.pixmap(22, 22));
    iconLbl->setFixedSize(22, 22);
    header->addWidget(iconLbl);
    header->addStretch(1);

    if (kind == StripKind::App)
    {
        auto* remove = new QToolButton;
        remove->setObjectName(QStringLiteral("Remove"));
        remove->setText(QStringLiteral("\u2715"));
        remove->setToolTip(QStringLiteral("Remove from mix"));
        remove->setCursor(Qt::PointingHandCursor);
        connect(remove, &QToolButton::clicked, this, [this] { emit removeRequested(id_); });
        header->addWidget(remove);
    }
    root->addLayout(header);

    auto* nameLbl = new QLabel;
    nameLbl->setObjectName(QStringLiteral("StripName"));
    nameLbl->setFont(theme::Font(10.5, QFont::DemiBold));
    nameLbl->setText(QFontMetrics(nameLbl->font()).elidedText(name, Qt::ElideRight, 116));
    nameLbl->setToolTip(name);
    root->addWidget(nameLbl);

    // ---- Status line ----------------------------------------------------
    status_ = new QLabel;
    status_->setObjectName(QStringLiteral("StripStatus"));
    status_->setWordWrap(true);
    status_->setFixedHeight(32);
    status_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    root->addWidget(status_);

    // ---- Meter + fader --------------------------------------------------
    auto* mid = new QHBoxLayout;
    mid->setSpacing(4);
    mid->addStretch(1);
    meter_ = new LevelMeter;
    meter_->setFixedWidth(12);
    mid->addWidget(meter_);
    fader_ = new Fader;
    fader_->setValue(static_cast<int>(std::lround(ctl_->gainDb.load() * 10)));
    mid->addWidget(fader_);
    mid->addStretch(1);
    root->addLayout(mid, 1);

    db_ = new QLabel;
    db_->setObjectName(QStringLiteral("Db"));
    db_->setAlignment(Qt::AlignCenter);
    root->addWidget(db_);
    updateDbLabel();

    connect(fader_, &QAbstractSlider::valueChanged, this, [this](int v) {
        ctl_->gainDb = Fader::DbFromValue(v);
        updateDbLabel();
        emit settingsChanged();
    });

    // ---- Mic: tally lamp / Apps: duck toggle -----------------------------
    if (isMic)
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(6);
        row->addStretch(1);
        tally_ = new TallyLamp;
        row->addWidget(tally_);
        talking_ = new QLabel(QStringLiteral("Talking"));
        talking_->setObjectName(QStringLiteral("Talking"));
        row->addWidget(talking_);
        row->addStretch(1);
        root->addLayout(row);

        if (voice_) buildNoiseButton();
        if (noiseBtn_) root->addWidget(noiseBtn_);
    }
    else if (kind == StripKind::Soundboard)
    {
        auto* open = new QPushButton(QStringLiteral("Pads"));
        open->setToolTip(QStringLiteral("Open the soundboard"));
        connect(open, &QPushButton::clicked, this, &ChannelStrip::openRequested);
        root->addWidget(open);
    }
    else
    {
        duckBtn_ = new QPushButton(QStringLiteral("Duck"));
        duckBtn_->setObjectName(QStringLiteral("Toggle"));
        duckBtn_->setCheckable(true);
        duckBtn_->setChecked(ctl_->duckable);
        duckBtn_->setToolTip(QStringLiteral("Lower this app while you talk"));
        connect(duckBtn_, &QPushButton::toggled, this, [this](bool on) {
            ctl_->duckable = on;
            emit settingsChanged();
        });
        root->addWidget(duckBtn_);
    }

    // ---- On/off ----------------------------------------------------------
    onBtn_ = new QPushButton;
    onBtn_->setObjectName(QStringLiteral("Toggle"));
    onBtn_->setCheckable(true);
    onBtn_->setChecked(ctl_->enabled);
    onBtn_->setText(ctl_->enabled ? QStringLiteral("On") : QStringLiteral("Off"));
    onBtn_->setToolTip(isMic ? QStringLiteral("Turn your mic on or off")
                     : kind == StripKind::Soundboard ? QStringLiteral("Turn the soundboard on or off")
                                                      : QStringLiteral("Turn this app on or off"));
    connect(onBtn_, &QPushButton::toggled, this, [this](bool on) {
        ctl_->enabled = on;
        onBtn_->setText(on ? QStringLiteral("On") : QStringLiteral("Off"));
        setProperty("off", !on);
        Repolish(this);
        emit settingsChanged();
    });
    root->addWidget(onBtn_);

    setProperty("off", !ctl_->enabled);
}

void ChannelStrip::buildNoiseButton()
{
    noiseBtn_ = new QToolButton;
    noiseBtn_->setObjectName(QStringLiteral("NoiseBtn"));
    noiseBtn_->setPopupMode(QToolButton::InstantPopup);
    noiseBtn_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    noiseBtn_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    noiseBtn_->setCursor(Qt::PointingHandCursor);

    auto* menu = new QMenu(noiseBtn_);

    // Noise suppression level.
    auto* nsTitle = menu->addAction(QStringLiteral("Noise suppression"));
    nsTitle->setEnabled(false);
    auto* nsGroup = new QActionGroup(menu);
    const struct { const char* text; const char* tip; int level; } levels[] = {
        { "Off",    "Your mic exactly as it sounds", mixcast::NoiseOff },
        { "Low",    "Takes the edge off fans and hiss (up to 10 dB)", mixcast::NoiseLow },
        { "Medium", "Clear voice, quiet background (up to 18 dB)", mixcast::NoiseMedium },
        { "High",   "Loud rooms: removes up to 28 dB of steady noise", mixcast::NoiseHigh },
    };
    for (const auto& l : levels)
    {
        auto* a = menu->addAction(QString::fromUtf8(l.text));
        a->setToolTip(QString::fromUtf8(l.tip));
        a->setCheckable(true);
        a->setData(l.level);
        nsGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, lvl = l.level] {
            voice_->noiseLevel = lvl;
            updateNoiseButton();
            emit settingsChanged();
        });
    }

    // Background gate.
    menu->addSeparator();
    auto* gTitle = menu->addAction(QStringLiteral("Silence between words"));
    gTitle->setEnabled(false);
    auto* gGroup = new QActionGroup(menu);
    const struct { const char* text; const char* tip; int mode; } gates[] = {
        { "Off",        "Mic stays open all the time", mixcast::GateOff },
        { "Gentle",     "Turns quiet background down between phrases", mixcast::GateGentle },
        { "Firm",       "Mutes the room between phrases; loud sounds still open it", mixcast::GateFirm },
        { "Voice only", "Opens only for your voice. Chewing, clicks, taps and bumps stay muted, "
                        "even loud ones (adds 25 ms delay)", mixcast::GateVoice },
    };
    for (const auto& g : gates)
    {
        auto* a = menu->addAction(QString::fromUtf8(g.text));
        a->setToolTip(QString::fromUtf8(g.tip));
        a->setCheckable(true);
        a->setData(100 + g.mode);
        gGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, mode = g.mode] {
            voice_->gateMode = mode;
            updateNoiseButton();
            emit settingsChanged();
        });
    }

    menu->addSeparator();
    auto* rumble = menu->addAction(QStringLiteral("Cut low rumble"));
    rumble->setToolTip(QStringLiteral("Removes desk bumps and low hum below 80 Hz"));
    rumble->setCheckable(true);
    rumble->setData(200);
    connect(rumble, &QAction::toggled, this, [this](bool on) {
        voice_->rumbleFilter = on;
        updateNoiseButton();
        emit settingsChanged();
    });

    // Radio voice chain (after the clean-up, in broadcast order).
    menu->addSeparator();
    auto* pTitle = menu->addAction(QStringLiteral("Radio voice"));
    pTitle->setEnabled(false);
    const struct { const char* text; const char* tip; int data; std::atomic<bool> mixcast::VoiceSettings::* flag; } stages[] = {
        { "De-esser",   "Softens harsh \"s\", \"sh\" and \"t\" sounds", 300, &mixcast::VoiceSettings::deEsser },
        { "Voice EQ",   "Less mud, more presence and air: clearer on calls", 301, &mixcast::VoiceSettings::voiceEq },
        { "Compressor", "Evens out quiet and loud words so you're always easy to hear", 302, &mixcast::VoiceSettings::compressor },
        { "Limiter",    "Stops shouts and laughs from clipping (ceiling \u22121 dB)", 303, &mixcast::VoiceSettings::limiter },
    };
    for (const auto& st : stages)
    {
        auto* a = menu->addAction(QString::fromUtf8(st.text));
        a->setToolTip(QString::fromUtf8(st.tip));
        a->setCheckable(true);
        a->setData(st.data);
        connect(a, &QAction::toggled, this, [this, flag = st.flag](bool on) {
            (voice_->*flag) = on;
            updateNoiseButton();
            emit settingsChanged();
        });
    }

    // Learns your voice (statistics only, no AI; stays on this PC).
    menu->addSeparator();
    auto* lTitle = menu->addAction(QStringLiteral("Learns your voice"));
    lTitle->setEnabled(false);
    auto* lStatus = menu->addAction(QString());
    lStatus->setEnabled(false);
    const struct { const char* text; const char* tip; int data; std::atomic<bool> mixcast::VoiceSettings::* flag; } learns[] = {
        { "Learn my voice",
          "Learns your pitch, level and voice spectrum while you talk, and keeps adjusting as your voice changes. "
          "Then only your voice opens \"Voice only\" and ducks apps, the gate sits between your room and your "
          "voice, and noise is removed harder where your voice never is.", 400, &mixcast::VoiceSettings::learnVoice },
        { "Auto level",
          "Keeps your voice at a steady level whatever your mic's gain (needs Learn my voice)", 401,
          &mixcast::VoiceSettings::autoLevel },
        { "Clean while I talk",
          "Follows your pitch and turns down noise between your voice's harmonics while you speak. "
          "Works only when the room is noisy, and only on vowels.", 402, &mixcast::VoiceSettings::cleanWhileTalking },
    };
    for (const auto& l : learns)
    {
        auto* a = menu->addAction(QString::fromUtf8(l.text));
        a->setToolTip(QString::fromUtf8(l.tip));
        a->setCheckable(true);
        a->setData(l.data);
        connect(a, &QAction::toggled, this, [this, flag = l.flag](bool on) {
            (voice_->*flag) = on;
            updateNoiseButton();
            emit settingsChanged();
        });
    }
    auto* forget = menu->addAction(QStringLiteral("Forget my voice\u2026"));
    forget->setToolTip(QStringLiteral("Clears what MixCast has learned and starts again"));
    connect(forget, &QAction::triggered, this, [this] {
        if (QMessageBox::question(this, QStringLiteral("Forget my voice"),
                QStringLiteral("Clear everything MixCast has learned about your voice and start learning again?"))
            != QMessageBox::Yes) return;
        voice_->LoadProfile(mixcast::VoiceProfile{});
        emit settingsChanged();
    });
    connect(menu, &QMenu::aboutToShow, this, [this, lStatus] {
        const mixcast::VoiceProfile p = voice_->CopyProfile();
        QString text;
        if (!voice_->learnVoice)
            text = QStringLiteral("Not learning");
        else if (!p.Trained())
            text = QStringLiteral("Learning\u2026 %1% (keep talking)")
                       .arg(static_cast<int>(100.0f * p.voicedSec / mixcast::VoiceProfile::kTrainedSec));
        else
        {
            float lo = 0.0f, hi = 0.0f;
            p.PitchRange(lo, hi);
            text = QStringLiteral("Knows your voice: %1\u2013%2 Hz, %3 min heard")
                       .arg(std::lround(lo)).arg(std::lround(hi)).arg(std::max(1L, std::lround(p.voicedSec / 60.0f)));
        }
        lStatus->setText(text);
    });

    menu->setToolTipsVisible(true);
    noiseBtn_->setMenu(menu);
    updateNoiseButton();
}

void ChannelStrip::updateNoiseButton()
{
    if (!noiseBtn_ || !voice_) return;
    const int  level  = std::clamp(voice_->noiseLevel.load(), 0, 3);
    const int  gate   = std::clamp(voice_->gateMode.load(), 0, 3);
    const bool rumble = voice_->rumbleFilter;
    const int  polish = (voice_->deEsser ? 1 : 0) | (voice_->voiceEq ? 2 : 0)
                      | (voice_->compressor ? 4 : 0) | (voice_->limiter ? 8 : 0)
                      | (voice_->learnVoice ? 16 : 0) | (voice_->autoLevel ? 32 : 0)
                      | (voice_->cleanWhileTalking ? 64 : 0);
    if (level == shownNoise_ && gate == shownGate_ && rumble == shownRumble_ && polish == shownPolish_) return;
    shownNoise_ = level; shownGate_ = gate; shownRumble_ = rumble; shownPolish_ = polish;

    static const char* kNames[] = { "Off", "Low", "Medium", "High" };
    noiseBtn_->setText(QStringLiteral("Noise: %1  \u25BE").arg(QString::fromUtf8(kNames[level])));
    noiseBtn_->setToolTip(QStringLiteral("Noise suppression for your mic. Runs on this PC, no AI."));
    noiseBtn_->setProperty("active", level > 0);
    Repolish(noiseBtn_);

    for (auto* a : noiseBtn_->menu()->actions())
    {
        const int d = a->data().isValid() ? a->data().toInt() : -1;
        QSignalBlocker b(a);
        if (d >= 0 && d < 4)        a->setChecked(d == level);
        else if (d >= 100 && d < 200) a->setChecked(d - 100 == gate);
        else if (d == 200)          a->setChecked(rumble);
        else if (d >= 300 && d < 304) a->setChecked((polish & (1 << (d - 300))) != 0);
        else if (d >= 400 && d < 403) a->setChecked((polish & (16 << (d - 400))) != 0);
    }
}

void ChannelStrip::updateDbLabel()
{
    const float db = ctl_->gainDb;
    QString s = (db <= -59.95f) ? QStringLiteral("\u2212\u221E") : QString::number(std::fabs(db), 'f', 1);
    if (db < -0.05f && db > -59.95f) s.prepend(QChar(0x2212));   // proper minus sign
    else if (db > 0.05f) s.prepend(QLatin1Char('+'));
    db_->setText(s + QStringLiteral(" dB"));
}

void ChannelStrip::setStatus(const QString& text, bool warn)
{
    if (text == lastStatus_ && warn == lastWarn_) return;
    lastStatus_ = text;
    lastWarn_   = warn;
    status_->setText(text);
    status_->setProperty("warn", warn);
    Repolish(status_);
}

void ChannelStrip::syncFromControls()
{
    // Reflect changes made elsewhere (tray menu, settings load).
    const bool on = ctl_->enabled;
    if (onBtn_->isChecked() != on) onBtn_->setChecked(on);   // toggled() updates text/style

    if (duckBtn_ && duckBtn_->isChecked() != ctl_->duckable.load())
    {
        QSignalBlocker b(duckBtn_);
        duckBtn_->setChecked(ctl_->duckable);
    }
}

void ChannelStrip::refreshSoundboard(int sounds, int playing)
{
    syncFromControls();
    meter_->setLevel(ctl_->peak);
    if (!ctl_->enabled)      setStatus(QStringLiteral("Off: sounds won't reach Discord"), true);
    else if (playing > 0)    setStatus(playing == 1 ? QStringLiteral("Playing 1 sound")
                                                    : QStringLiteral("Playing %1 sounds").arg(playing), false);
    else if (sounds == 0)    setStatus(QStringLiteral("No sounds yet"), false);
    else                     setStatus(sounds == 1 ? QStringLiteral("1 sound ready")
                                                   : QStringLiteral("%1 sounds ready").arg(sounds), false);
}

void ChannelStrip::refresh(const mixcast::SourceStatus& st, bool talking)
{
    syncFromControls();
    meter_->setLevel(ctl_->peak);

    using mixcast::SourceState;
    if (isMic_)
    {
        const bool lit = talking && ctl_->enabled && st.state == SourceState::Running;
        tally_->setLit(lit);
        if (talking_->property("lit").toBool() != lit)
        {
            talking_->setProperty("lit", lit);
            Repolish(talking_);
        }

        updateNoiseButton();
        if (st.state == SourceState::Failed)
            setStatus(QStringLiteral("Unavailable. Pick another mic above."), true);
        else if (!ctl_->enabled)
            setStatus(QStringLiteral("Muted"), true);
        else if (voice_ && voice_->noiseLevel > 0)
            setStatus(QStringLiteral("Your voice, background filtered"), false);
        else
            setStatus(QStringLiteral("Your voice"), false);
        return;
    }

    switch (st.state)
    {
    case SourceState::WaitingForApp:
        setStatus(QStringLiteral("Joins when the app opens"), false);
        break;
    case SourceState::Failed:
        setStatus(QStringLiteral("Can't capture this app."), true);
        status_->setToolTip(QString::fromStdString(st.error));
        break;
    case SourceState::Running:
        setStatus(ctl_->duckable ? QStringLiteral("Lowers while you talk")
                                 : QStringLiteral("Stays at full level"), false);
        break;
    }
}
