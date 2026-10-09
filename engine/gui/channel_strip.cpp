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

    if (isMic)
    {
        // Talking lamp lives in the header row, out of the way of the controls.
        tally_ = new TallyLamp;
        header->addWidget(tally_);
        talking_ = new QLabel(QStringLiteral("TALKING"));
        talking_->setObjectName(QStringLiteral("Talking"));
        header->addWidget(talking_);
    }
    else if (kind == StripKind::App)
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
    nameLbl->setProperty("kind", isMic ? "mic" : kind == StripKind::Soundboard ? "sb" : "app");   // scribble-strip colour
    nameLbl->setText(QFontMetrics(nameLbl->font()).elidedText(name, Qt::ElideRight, 94));
    nameLbl->setToolTip(name);
    root->addWidget(nameLbl);

    // ---- Status line ----------------------------------------------------
    status_ = new QLabel;
    status_->setObjectName(QStringLiteral("StripStatus"));
    status_->setWordWrap(true);
    status_->setFixedHeight(32);
    status_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    root->addWidget(status_);

    if (isMic && voice_)
    {
        scope_ = new CleanupScope;
        scope_->setFixedHeight(40);
        root->addWidget(scope_);
    }

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
    onBtn_->setProperty("power", true);   // shows red "Off" when switched off
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
    toggle(radio, "Limiter",    "Stops shouts and laughs from clipping (ceiling −1 dB)", 303,
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
    auto* forget = learn->addAction(QStringLiteral("Forget my voice on this mic…"));
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
            text = QStringLiteral("Learning… %1% (keep talking)")
                       .arg(static_cast<int>(100.0f * p.voicedSec / mixcast::VoiceProfile::kTrainedSec));
        else
        {
            float lo = 0.0f, hi = 0.0f;
            p.PitchRange(lo, hi);
            text = QStringLiteral("Knows your voice: %1–%2 Hz, %3 min heard")
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
    noiseBtn_->setText(QStringLiteral("Sound: %1  ▾").arg(name));
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
        if (scope_)
        {
            static_assert(CleanupScope::kBands == mixcast::VoiceSettings::kScopeBands, "scope bands");
            float in[CleanupScope::kBands], out[CleanupScope::kBands];
            for (int b = 0; b < CleanupScope::kBands; b++)
            {
                const bool live = ctl_->enabled && st.state == SourceState::Running;
                in[b]  = live ? voice_->scopeInDb[b].load(std::memory_order_relaxed)  : -120.0f;
                out[b] = live ? voice_->scopeOutDb[b].load(std::memory_order_relaxed) : -120.0f;
            }
            scope_->setBands(in, out, voice_->removedDb);
        }
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
