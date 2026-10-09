// MixCast GUI - one channel strip (mic or app).
#include "channel_strip.h"
#include "mic_presets.h"
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
#include <functional>

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

QString BigDbText(float db)
{
    // Large number, small unit; boost above 0 dB shows in coral.
    QString n = (db <= -59.95f) ? QStringLiteral("\u2212\u221E") : QString::number(std::fabs(db), 'f', 1);
    if (db < -0.05f && db > -59.95f) n.prepend(QChar(0x2212));   // proper minus sign
    else if (db > 0.05f) n.prepend(QLatin1Char('+'));
    const QString colour = db > 0.05f ? theme::Tally.name() : theme::Legend.name();
    return QStringLiteral("<span style=\"font-size:21pt; font-weight:300; color:%1\">%2</span>"
                          "<span style=\"font-size:9pt; color:%3\">&nbsp;dB</span>")
        .arg(colour, n, theme::Muted.name());
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
    setFixedWidth(148);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 12, 10, 14);
    root->setSpacing(10);

    // ---- Section header: what it is, then which one ------------------------
    auto* header = new QHBoxLayout;
    header->setSpacing(8);
    auto* iconLbl = new QLabel;
    iconLbl->setPixmap(icon.pixmap(20, 20));
    iconLbl->setFixedSize(20, 20);
    header->addWidget(iconLbl, 0, Qt::AlignTop);

    auto* titles = new QVBoxLayout;
    titles->setSpacing(1);
    auto* title = new QLabel(isMic ? QStringLiteral("MIC")
                             : kind == StripKind::Soundboard ? QStringLiteral("SOUNDBOARD") : QStringLiteral("APP"));
    title->setObjectName(QStringLiteral("StripTitle"));
    titles->addWidget(title);
    auto* sub = new QLabel;
    sub->setObjectName(QStringLiteral("StripSub"));
    sub->setText(QFontMetrics(theme::Font(8.5)).elidedText(name, Qt::ElideRight, 88));
    sub->setToolTip(name);
    titles->addWidget(sub);
    header->addLayout(titles, 1);

    if (isMic)
    {
        tally_ = new TallyLamp;   // lights while you talk
        tally_->setToolTip(QStringLiteral("Lights while you're talking"));
        header->addWidget(tally_, 0, Qt::AlignTop);
    }
    else if (kind == StripKind::App)
    {
        auto* remove = new QToolButton;
        remove->setObjectName(QStringLiteral("Remove"));
        remove->setText(QStringLiteral("\u2715"));
        remove->setToolTip(QStringLiteral("Remove from mix"));
        remove->setCursor(Qt::PointingHandCursor);
        connect(remove, &QToolButton::clicked, this, [this] { emit removeRequested(id_); });
        header->addWidget(remove, 0, Qt::AlignTop);
    }
    root->addLayout(header);

    // ---- Info panel: the mic's clean-up, or what this channel is doing -----
    auto* panel = new QFrame;
    panel->setObjectName(QStringLiteral("InfoPanel"));
    panel->setFixedHeight(100);   // same on every channel, so readouts and faders line up
    auto* pl = new QVBoxLayout(panel);
    pl->setContentsMargins(8, 6, 8, 7);
    pl->setSpacing(4);

    status_ = new QLabel;
    status_->setObjectName(QStringLiteral("StripStatus"));
    status_->setWordWrap(true);
    status_->setAlignment(Qt::AlignCenter);

    if (isMic && voice_)
    {
        auto* top = new QHBoxLayout;
        top->setSpacing(4);
        auto* lbl = new QLabel(QStringLiteral("SOUND"));
        lbl->setObjectName(QStringLiteral("PanelLabel"));
        top->addWidget(lbl);
        top->addStretch(1);
        buildNoiseButton();
        top->addWidget(noiseBtn_);
        pl->addLayout(top);

        scope_ = new CleanupScope;
        pl->addWidget(scope_, 1);
        status_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        pl->addWidget(status_);
    }
    else
    {
        auto* lbl = new QLabel(isMic ? QStringLiteral("STATUS")
                               : kind == StripKind::Soundboard ? QStringLiteral("PADS") : QStringLiteral("STATUS"));
        lbl->setObjectName(QStringLiteral("PanelLabel"));
        pl->addWidget(lbl);
        pl->addStretch(1);
        pl->addWidget(status_);
        pl->addStretch(1);
    }
    root->addWidget(panel);

    // ---- Big gain readout --------------------------------------------------
    db_ = new QLabel;
    db_->setObjectName(QStringLiteral("BigDb"));
    db_->setAlignment(Qt::AlignRight | Qt::AlignBottom);
    db_->setTextFormat(Qt::RichText);
    root->addWidget(db_);

    // ---- Meter | buttons | fader -------------------------------------------
    auto* mid = new QHBoxLayout;
    mid->setSpacing(10);
    meter_ = new LevelMeter;
    meter_->setFixedWidth(14);
    mid->addWidget(meter_);

    auto* buttons = new QVBoxLayout;
    buttons->setSpacing(8);
    auto stripButton = [](const QString& text, const QString& tip) {
        auto* b = new QPushButton(text);
        b->setObjectName(QStringLiteral("StripBtn"));
        b->setToolTip(tip);
        b->setFixedSize(54, 30);
        b->setCursor(Qt::PointingHandCursor);
        return b;
    };

    onBtn_ = stripButton(QString(), isMic ? QStringLiteral("Turn your mic on or off")
                                  : kind == StripKind::Soundboard ? QStringLiteral("Turn the soundboard on or off")
                                                                   : QStringLiteral("Turn this app on or off"));
    onBtn_->setProperty("power", true);   // coral "OFF" when switched off
    onBtn_->setCheckable(true);
    onBtn_->setChecked(ctl_->enabled);
    onBtn_->setText(ctl_->enabled ? QStringLiteral("ON") : QStringLiteral("OFF"));
    connect(onBtn_, &QPushButton::toggled, this, [this](bool on) {
        ctl_->enabled = on;
        onBtn_->setText(on ? QStringLiteral("ON") : QStringLiteral("OFF"));
        setProperty("off", !on);
        fader_->setDimmed(!on);
        Repolish(this);
        emit settingsChanged();
    });
    buttons->addWidget(onBtn_);

    if (kind == StripKind::Soundboard)
    {
        auto* open = stripButton(QStringLiteral("PADS"), QStringLiteral("Open the soundboard"));
        connect(open, &QPushButton::clicked, this, &ChannelStrip::openRequested);
        buttons->addWidget(open);
    }
    else if (kind == StripKind::App)
    {
        duckBtn_ = stripButton(QStringLiteral("DUCK"), QStringLiteral("Lower this app while you talk"));
        duckBtn_->setCheckable(true);
        duckBtn_->setChecked(ctl_->duckable);
        connect(duckBtn_, &QPushButton::toggled, this, [this](bool on) {
            ctl_->duckable = on;
            emit settingsChanged();
        });
        buttons->addWidget(duckBtn_);
    }
    buttons->addStretch(1);
    mid->addLayout(buttons);

    fader_ = new Fader;
    fader_->setValue(static_cast<int>(std::lround(ctl_->gainDb.load() * 10)));
    fader_->setDimmed(!ctl_->enabled);
    mid->addWidget(fader_);
    root->addLayout(mid, 1);

    updateDbLabel();
    connect(fader_, &QAbstractSlider::valueChanged, this, [this](int v) {
        ctl_->gainDb = Fader::DbFromValue(v);
        updateDbLabel();
        emit settingsChanged();
    });

    setProperty("off", !ctl_->enabled);
}

// The mic's sound menu: one choice of preset for most people, with every
// individual clean-up option still there under Advanced.
void ChannelStrip::buildNoiseButton()
{
    noiseBtn_ = new QToolButton;
    noiseBtn_->setObjectName(QStringLiteral("NoiseBtn"));
    noiseBtn_->setPopupMode(QToolButton::InstantPopup);
    noiseBtn_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    noiseBtn_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    noiseBtn_->setCursor(Qt::PointingHandCursor);

    auto* menu = new QMenu(noiseBtn_);
    menu->setToolTipsVisible(true);

    // Applies a change, refreshes the ticks and saves.
    auto changed = [this] {
        updateNoiseButton();
        emit settingsChanged();
    };
    // A checkable item that sets one value (radio-style choices).
    auto choice = [&](QMenu* m, QActionGroup* group, const char* text, const char* tip, int data,
                      std::function<void()> apply) {
        auto* a = m->addAction(QString::fromUtf8(text));
        a->setToolTip(QString::fromUtf8(tip));
        a->setCheckable(true);
        a->setData(data);
        if (group) group->addAction(a);
        connect(a, &QAction::triggered, this, [apply, changed] { apply(); changed(); });
        return a;
    };
    // A checkbox item bound to one on/off setting.
    using Flag = std::atomic<bool> mixcast::VoiceSettings::*;
    auto toggle = [&](QMenu* m, const char* text, const char* tip, int data, Flag flag) {
        auto* a = m->addAction(QString::fromUtf8(text));
        a->setToolTip(QString::fromUtf8(tip));
        a->setCheckable(true);
        a->setData(data);
        connect(a, &QAction::toggled, this, [this, flag, changed](bool on) { (voice_->*flag) = on; changed(); });
        return a;
    };
    auto submenu = [](QMenu* parent, int data) {
        auto* m = parent->addMenu(QString());
        m->menuAction()->setData(data);   // its title shows the current setting
        m->setToolTipsVisible(true);
        return m;
    };

    // ---- Presets ----------------------------------------------------------
    auto* title = menu->addAction(QStringLiteral("Mic sound"));
    title->setEnabled(false);
    auto* presets = new QActionGroup(menu);
    for (int p = 0; p < mixcast::PresetCount; p++)
    {
        const auto& d = mixcast::MicPresetInfo(p);
        choice(menu, presets, d.name, d.blurb, 600 + p, [this, p] { mixcast::ApplyMicPreset(*voice_, p); });
    }

    // ---- Advanced: every option on its own --------------------------------
    menu->addSeparator();
    auto* adv = menu->addMenu(QStringLiteral("Advanced"));
    adv->setToolTipsVisible(true);

    auto* noise = submenu(adv, 503);
    auto* nGroup = new QActionGroup(noise);
    choice(noise, nGroup, "Off",    "Your mic exactly as it sounds", mixcast::NoiseOff,
           [this] { voice_->noiseLevel = mixcast::NoiseOff; });
    choice(noise, nGroup, "Low",    "Takes the edge off fans and hiss (up to 10 dB)", mixcast::NoiseLow,
           [this] { voice_->noiseLevel = mixcast::NoiseLow; });
    choice(noise, nGroup, "Medium", "Clear voice, quiet background (up to 18 dB)", mixcast::NoiseMedium,
           [this] { voice_->noiseLevel = mixcast::NoiseMedium; });
    choice(noise, nGroup, "High",   "Loud rooms: removes up to 28 dB of steady noise", mixcast::NoiseHigh,
           [this] { voice_->noiseLevel = mixcast::NoiseHigh; });

    auto* gate = submenu(adv, 504);
    auto* gGroup = new QActionGroup(gate);
    choice(gate, gGroup, "Off",        "Mic stays open all the time", 100 + mixcast::GateOff,
           [this] { voice_->gateMode = mixcast::GateOff; });
    choice(gate, gGroup, "Gentle",     "Turns quiet background down between phrases", 100 + mixcast::GateGentle,
           [this] { voice_->gateMode = mixcast::GateGentle; });
    choice(gate, gGroup, "Firm",       "Mutes the room between phrases; loud sounds still open it", 100 + mixcast::GateFirm,
           [this] { voice_->gateMode = mixcast::GateFirm; });
    choice(gate, gGroup, "Voice only", "Opens only for your voice. Chewing, clicks, taps and bumps stay muted, "
                                       "even loud ones (adds 25 ms delay)", 100 + mixcast::GateVoice,
           [this] { voice_->gateMode = mixcast::GateVoice; });

    toggle(adv, "Cut low rumble", "Removes desk bumps and low hum below 80 Hz", 200,
           &mixcast::VoiceSettings::rumbleFilter);

    // Radio voice chain (after the clean-up, in broadcast order).
    auto* radio = submenu(adv, 501);
    toggle(radio, "De-esser",   "Softens harsh \"s\", \"sh\" and \"t\" sounds", 300, &mixcast::VoiceSettings::deEsser);
    toggle(radio, "Voice EQ",   "Less mud, more presence and air: clearer on calls", 301, &mixcast::VoiceSettings::voiceEq);
    toggle(radio, "Compressor", "Evens out quiet and loud words so you're always easy to hear", 302,
           &mixcast::VoiceSettings::compressor);
    toggle(radio, "Limiter",    "Stops shouts and laughs from clipping (ceiling \u22121 dB)", 303,
           &mixcast::VoiceSettings::limiter);

    // Learns your voice (statistics only, no AI; stays on this PC).
    auto* learn = submenu(adv, 502);
    auto* lStatus = learn->addAction(QString());
    lStatus->setEnabled(false);
    toggle(learn, "Learn my voice",
           "Learns your pitch, level and voice spectrum while you talk, and keeps adjusting as your voice changes. "
           "Then only your voice opens \"Voice only\" and ducks apps, the gate sits between your room and your "
           "voice, and noise is removed harder where your voice never is. Each mic learns separately.",
           400, &mixcast::VoiceSettings::learnVoice);
    toggle(learn, "Auto level", "Keeps your voice at a steady level whatever your mic's gain (needs Learn my voice)",
           401, &mixcast::VoiceSettings::autoLevel);
    toggle(learn, "Clean while I talk",
           "Follows your pitch and turns down noise between your voice's harmonics while you speak. "
           "Works only when the room is noisy, and only on vowels.", 402, &mixcast::VoiceSettings::cleanWhileTalking);
    toggle(learn, "Remove keyboard && clicks",
           "Learns what your voice and the sounds around you look like, and cuts keypresses, mouse clicks and "
           "taps, even while you talk. Starts working after about a minute of talking (needs Learn my voice).",
           403, &mixcast::VoiceSettings::removeClicks);
    learn->addSeparator();
    auto* forget = learn->addAction(QStringLiteral("Forget my voice on this mic\u2026"));
    forget->setToolTip(QStringLiteral("Clears what MixCast has learned with this mic and starts again"));
    connect(forget, &QAction::triggered, this, [this] {
        if (QMessageBox::question(this, QStringLiteral("Forget my voice"),
                QStringLiteral("Clear everything MixCast has learned about your voice with this mic and start learning again?"))
            != QMessageBox::Yes) return;
        voice_->LoadProfile(mixcast::VoiceProfile{});
        emit settingsChanged();
    });
    connect(learn, &QMenu::aboutToShow, this, [this, lStatus] {
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

    noiseBtn_->setMenu(menu);
    updateNoiseButton();
}

void ChannelStrip::updateNoiseButton()
{
    if (!noiseBtn_ || !voice_) return;
    const int  level  = std::clamp(voice_->noiseLevel.load(), 0, 3);
    const int  gate   = std::clamp(voice_->gateMode.load(), 0, 3);
    const bool rumble = voice_->rumbleFilter;
    const int  flags  = (voice_->deEsser ? 1 : 0) | (voice_->voiceEq ? 2 : 0)
                      | (voice_->compressor ? 4 : 0) | (voice_->limiter ? 8 : 0)
                      | (voice_->learnVoice ? 16 : 0) | (voice_->autoLevel ? 32 : 0)
                      | (voice_->cleanWhileTalking ? 64 : 0) | (voice_->removeClicks ? 128 : 0);
    if (level == shownNoise_ && gate == shownGate_ && rumble == shownRumble_ && flags == shownPolish_) return;
    shownNoise_ = level; shownGate_ = gate; shownRumble_ = rumble; shownPolish_ = flags;

    const int preset = mixcast::MatchMicPreset(*voice_);
    const QString name = preset >= 0 ? QString::fromUtf8(mixcast::MicPresetInfo(preset).name) : QStringLiteral("Custom");
    noiseBtn_->setText(QStringLiteral("%1 \u25BE").arg(name));
    noiseBtn_->setToolTip(QStringLiteral("How your mic sounds. Pick a preset, or fine-tune under Advanced.\n"
                                         "All of it runs on this PC, no AI."));
    noiseBtn_->setProperty("active", level > 0);
    Repolish(noiseBtn_);

    // Every item, however deep in the submenus.
    QList<QAction*> actions;
    std::function<void(QMenu*)> collect = [&](QMenu* m) {
        for (auto* a : m->actions())
        {
            actions << a;
            if (a->menu()) collect(a->menu());
        }
    };
    collect(noiseBtn_->menu());

    static const char* kNoise[] = { "Off", "Low", "Medium", "High" };
    static const char* kGate[]  = { "Off", "Gentle", "Firm", "Voice only" };
    for (auto* a : actions)
    {
        const int d = a->data().isValid() ? a->data().toInt() : -1;
        QSignalBlocker b(a);
        if (d >= 0 && d < 4)               a->setChecked(d == level);
        else if (d >= 100 && d < 200)      a->setChecked(d - 100 == gate);
        else if (d == 200)                 a->setChecked(rumble);
        else if (d >= 300 && d < 304)      a->setChecked((flags & (1 << (d - 300))) != 0);
        else if (d >= 400 && d < 404)      a->setChecked((flags & (16 << (d - 400))) != 0);
        else if (d >= 600 && d < 610)      a->setChecked(d - 600 == preset);
        else if (d == 501)
        {
            const int on = (flags & 1) + ((flags >> 1) & 1) + ((flags >> 2) & 1) + ((flags >> 3) & 1);
            a->setText(on ? QStringLiteral("Radio voice: %1 of 4 on").arg(on) : QStringLiteral("Radio voice: off"));
        }
        else if (d == 502) a->setText((flags & 16) ? QStringLiteral("Learns your voice: on") : QStringLiteral("Learns your voice: off"));
        else if (d == 503) a->setText(QStringLiteral("Noise suppression: %1").arg(QString::fromUtf8(kNoise[level])));
        else if (d == 504) a->setText(QStringLiteral("Silence between words: %1").arg(QString::fromUtf8(kGate[gate])));
    }
}

void ChannelStrip::updateDbLabel()
{
    db_->setText(BigDbText(ctl_->gainDb));
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

        updateNoiseButton();
        if (scope_)
        {
            static_assert(CleanupScope::kBands == mixcast::VoiceSettings::kScopeBands, "scope bands");
            float in[CleanupScope::kBands], out[CleanupScope::kBands];
            const bool live = ctl_->enabled && st.state == SourceState::Running;
            for (int b = 0; b < CleanupScope::kBands; b++)
            {
                in[b]  = live ? voice_->scopeInDb[b].load(std::memory_order_relaxed)  : -120.0f;
                out[b] = live ? voice_->scopeOutDb[b].load(std::memory_order_relaxed) : -120.0f;
            }
            scope_->setBands(in, out, live ? voice_->removedDb.load() : 0.0f);
        }
        if (st.state == SourceState::Failed)
            setStatus(QStringLiteral("Unavailable"), true);
        else if (!ctl_->enabled)
            setStatus(QStringLiteral("Muted"), true);
        else if (voice_ && voice_->noiseLevel > 0)
            setStatus(QStringLiteral("Cleaning up"), false);
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
