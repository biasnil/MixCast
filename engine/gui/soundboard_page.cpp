// MixCast GUI - soundboard page (pads, hotkeys, "hear it myself").
#include "soundboard_page.h"
#include "app_paths.h"
#include "hotkeys.h"
#include "mix_engine.h"
#include "theme.h"

#include <QCheckBox>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QThreadPool>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <cmath>
#include <map>

namespace {

const QStringList kAudioExtensions = { QStringLiteral("mp3"), QStringLiteral("wav"), QStringLiteral("m4a"),
                                       QStringLiteral("aac"), QStringLiteral("wma"), QStringLiteral("flac") };

QString FormatDb(float db)
{
    QString s = QString::number(std::fabs(db), 'f', 0);
    if (db < -0.5f)     s.prepend(QChar(0x2212));
    else if (db > 0.5f) s.prepend(QLatin1Char('+'));
    return s + QStringLiteral(" dB");
}

QString FormatDuration(double seconds)
{
    const int s = static_cast<int>(std::lround(seconds));
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

} // namespace

// ============================================================================
// SoundPad
// ============================================================================
SoundPad::SoundPad(QWidget* parent) : QAbstractButton(parent)
{
    setCursor(Qt::PointingHandCursor);
    setFixedSize(sizeHint());
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_Hover);
}

void SoundPad::setState(State s, const QString& detail)
{
    state_ = s;
    detail_ = detail;
    update();
}

void SoundPad::setProgress(float p)
{
    if (std::fabs(p - progress_) < 0.002f) return;
    progress_ = p;
    update();
}

void SoundPad::contextMenuEvent(QContextMenuEvent* e)
{
    emit menuRequested(e->globalPos());
}

void SoundPad::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const bool playing = progress_ >= 0.0f;
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

    QPainterPath shape;
    shape.addRoundedRect(r, 6, 6);

    // Body.
    QColor body = theme::Panel;
    if (isDown())                 body = QColor(0x25, 0x2A, 0x31);
    else if (underMouse())        body = QColor(0x31, 0x38, 0x42);
    p.fillPath(shape, body);

    // Progress: a wash that fills left to right while the sound plays.
    if (playing)
    {
        p.save();
        p.setClipPath(shape);
        QColor wash = theme::Amber;
        wash.setAlpha(38);
        p.fillRect(QRectF(r.left(), r.top(), r.width() * progress_, r.height()), wash);
        p.fillRect(QRectF(r.left(), r.bottom() - 3, r.width() * progress_, 3), theme::Amber);
        p.restore();
    }

    // Border.
    QColor edge = playing ? theme::Amber : theme::PanelEdge;
    if (hasFocus() && !playing) edge = theme::AmberHot;
    p.setPen(QPen(edge, playing ? 1.5 : 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawPath(shape);

    // Name: up to two lines, the second one elided.
    p.setFont(theme::Font(10, QFont::DemiBold));
    p.setPen(state_ == State::Failed ? theme::Muted : theme::Legend);
    {
        const QFontMetrics fm(p.font());
        const int maxW = width() - 24;
        QString line1, rest = name_;
        const QStringList words = name_.split(QLatin1Char(' '));
        for (int i = 0; i < words.size(); i++)
        {
            const QString trial = line1.isEmpty() ? words[i] : line1 + QLatin1Char(' ') + words[i];
            if (fm.horizontalAdvance(trial) > maxW && !line1.isEmpty())
            {
                rest = words.mid(i).join(QLatin1Char(' '));
                break;
            }
            line1 = trial;
            rest.clear();
        }
        if (fm.horizontalAdvance(line1) > maxW) { line1 = fm.elidedText(line1, Qt::ElideRight, maxW); rest.clear(); }
        p.drawText(QPointF(12, 10 + fm.ascent()), line1);
        if (!rest.isEmpty())
            p.drawText(QPointF(12, 10 + fm.lineSpacing() + fm.ascent()), fm.elidedText(rest, Qt::ElideRight, maxW));
    }

    // Hotkey chip, bottom left.
    const qreal baseY = height() - 28;
    if (!hotkey_.isEmpty())
    {
        p.setFont(theme::Font(8.5));
        const QFontMetrics fm(p.font());
        const qreal w = std::min<qreal>(fm.horizontalAdvance(hotkey_) + 14, width() - 24);
        const QRectF chip(12, baseY, w, 18);
        p.setPen(Qt::NoPen);
        p.setBrush(theme::Slot);
        p.drawRoundedRect(chip, 3, 3);
        p.setPen(theme::Muted);
        p.drawText(chip, Qt::AlignCenter, fm.elidedText(hotkey_, Qt::ElideRight, static_cast<int>(w - 10)));
    }

    // State, bottom right.
    QString right;
    QColor  rightColor = theme::Muted;
    if (state_ == State::Loading)      right = QStringLiteral("Loading\u2026");
    else if (state_ == State::Failed) { right = QStringLiteral("Can't open"); rightColor = theme::Amber; }
    else if (playing)                 { right = QStringLiteral("Playing"); rightColor = theme::AmberHot; }
    else                               right = detail_;   // duration
    if (!right.isEmpty())
    {
        p.setFont(theme::Font(8.5));
        p.setPen(rightColor);
        p.drawText(QRectF(12, baseY, width() - 24, 18), Qt::AlignRight | Qt::AlignVCenter, right);
    }
}

// ============================================================================
// HotkeyDialog
// ============================================================================
HotkeyDialog::HotkeyDialog(const QString& forWhat, const QKeySequence& current, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Set hotkey"));
    setMinimumWidth(380);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 18);
    root->setSpacing(12);

    auto* title = new QLabel(QStringLiteral("Press the key for \u201C%1\u201D").arg(forWhat));
    title->setFont(theme::Font(11, QFont::DemiBold));
    title->setWordWrap(true);
    root->addWidget(title);

    edit_ = new QKeySequenceEdit(current);
    root->addWidget(edit_);

    auto* tip = new QLabel(QStringLiteral("Hotkeys work everywhere, even in games. Pick keys you don't type with, "
                                          "like the numpad, F13\u2013F24, or Ctrl/Alt combinations."));
    tip->setObjectName(QStringLiteral("Muted"));
    tip->setWordWrap(true);
    root->addWidget(tip);

    auto* buttons = new QHBoxLayout;
    auto* clear = new QPushButton(QStringLiteral("Remove hotkey"));
    buttons->addWidget(clear);
    buttons->addStretch(1);
    auto* cancel = new QPushButton(QStringLiteral("Cancel"));
    auto* save   = new QPushButton(QStringLiteral("Save"));
    save->setObjectName(QStringLiteral("Primary"));
    save->setDefault(true);
    buttons->addWidget(cancel);
    buttons->addWidget(save);
    root->addLayout(buttons);

    connect(clear,  &QPushButton::clicked, this, [this] { edit_->clear(); accept(); });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(save,   &QPushButton::clicked, this, &QDialog::accept);
    edit_->setFocus();
}

QKeySequence HotkeyDialog::sequence() const
{
    const QKeySequence seq = edit_->keySequence();
    return seq.isEmpty() ? QKeySequence() : QKeySequence(seq[0]);   // one combination only
}

// ============================================================================
// SoundboardPage
// ============================================================================
SoundboardPage::SoundboardPage(mixcast::Soundboard* sb, HotkeyManager* hotkeys, QWidget* parent)
    : QWidget(parent), sb_(sb), hotkeys_(hotkeys)
{
    setAcceptDrops(true);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 8, 20, 16);
    root->setSpacing(10);

    // ---- Toolbar ----------------------------------------------------------
    auto* bar = new QHBoxLayout;
    bar->setSpacing(8);

    auto* add = new QPushButton(QStringLiteral("Add sounds"));
    add->setObjectName(QStringLiteral("Primary"));
    connect(add, &QPushButton::clicked, this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(
            this, QStringLiteral("Add sounds"), QString(),
            QStringLiteral("Audio (*.mp3 *.wav *.m4a *.aac *.wma *.flac)"));
        addFiles(files);
    });
    bar->addWidget(add);

    auto* stopAll = new QPushButton(QStringLiteral("Stop all"));
    connect(stopAll, &QPushButton::clicked, this, [this] { sb_->StopAll(); });
    bar->addWidget(stopAll);

    stopAllKeyBtn_ = new QPushButton;
    stopAllKeyBtn_->setToolTip(QStringLiteral("A hotkey that stops every sound"));
    connect(stopAllKeyBtn_, &QPushButton::clicked, this, &SoundboardPage::editStopAllHotkey);
    bar->addWidget(stopAllKeyBtn_);
    updateStopAllLabel();

    bar->addStretch(1);

    oneAtATime_ = new QCheckBox(QStringLiteral("One sound at a time"));
    oneAtATime_->setToolTip(QStringLiteral("Starting a sound stops whatever is playing"));
    connect(oneAtATime_, &QCheckBox::toggled, this, [this](bool on) { sb_->oneAtATime = on; changed(); });
    bar->addWidget(oneAtATime_);

    bar->addSpacing(12);
    monitorBtn_ = new QPushButton(QStringLiteral("Hear it myself"));
    monitorBtn_->setObjectName(QStringLiteral("Toggle"));
    monitorBtn_->setCheckable(true);
    monitorBtn_->setChecked(true);
    monitorBtn_->setToolTip(QStringLiteral("Also play sounds on your own headphones"));
    monitorBtn_->setMinimumWidth(126);   // room for the bold "checked" label
    bar->addWidget(monitorBtn_);

    monitorVol_ = new QSlider(Qt::Horizontal);
    monitorVol_->setRange(-30, 6);
    monitorVol_->setValue(-6);
    monitorVol_->setFixedWidth(100);
    monitorVol_->setToolTip(QStringLiteral("How loud sounds are in your headphones (doesn't change what others hear)"));
    bar->addWidget(monitorVol_);
    monitorLbl_ = new QLabel;
    monitorLbl_->setFixedWidth(48);
    bar->addWidget(monitorLbl_);

    auto applyMonitor = [this] {
        sb_->monitorEnabled = monitorBtn_->isChecked();
        sb_->monitorGainDb  = static_cast<float>(monitorVol_->value());
        monitorVol_->setEnabled(monitorBtn_->isChecked());
        monitorLbl_->setText(FormatDb(static_cast<float>(monitorVol_->value())));
        changed();
    };
    connect(monitorBtn_, &QPushButton::toggled, this, applyMonitor);
    connect(monitorVol_, &QSlider::valueChanged, this, applyMonitor);
    monitorLbl_->setText(FormatDb(-6));

    root->addLayout(bar);

    notice_ = new QLabel;
    notice_->setObjectName(QStringLiteral("Warning"));
    notice_->setWordWrap(true);
    notice_->hide();
    root->addWidget(notice_);

    // ---- Pads -------------------------------------------------------------
    scroll_ = new QScrollArea;
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    grid_ = new QWidget;
    gridLayout_ = new QGridLayout(grid_);
    gridLayout_->setContentsMargins(0, 0, 0, 0);
    gridLayout_->setSpacing(10);
    scroll_->setWidget(grid_);
    root->addWidget(scroll_, 1);

    empty_ = new QLabel(QStringLiteral("Drop sound files here, or use Add sounds.\n"
                                       "MP3, WAV, M4A, WMA and FLAC all work."));
    empty_->setObjectName(QStringLiteral("Muted"));
    empty_->setAlignment(Qt::AlignCenter);
    empty_->setFont(theme::Font(11));
    root->addWidget(empty_, 1);

    relayout();
}

SoundboardPage::~SoundboardPage()
{
    // Let background decodes finish before this page (their callback target) goes away.
    QThreadPool::globalInstance()->waitForDone();
}

SoundboardPage::Entry* SoundboardPage::find(int key)
{
    for (auto& e : entries_) if (e->key == key) return e.get();
    return nullptr;
}

void SoundboardPage::changed()
{
    emit settingsChanged();
}

// ---- Adding / decoding -------------------------------------------------------
void SoundboardPage::addFiles(const QStringList& paths)
{
    for (const QString& path : paths)
    {
        const QFileInfo fi(path);
        if (!kAudioExtensions.contains(fi.suffix().toLower())) continue;
        // Keep our own copy in %LOCALAPPDATA%\MixCast\Sounds.
        Entry* e = addEntry(StoreSoundFile(path), fi.completeBaseName(), 0.0f, {});
        startDecode(e->key);
    }
    relayout();
    changed();
}

SoundboardPage::Entry* SoundboardPage::addEntry(const QString& path, const QString& name, float gainDb,
                                                const QKeySequence& key)
{
    auto e = std::make_unique<Entry>();
    e->key    = nextKey_++;
    e->path   = path;
    e->name   = name;
    e->gainDb = gainDb;

    e->pad = new SoundPad(grid_);
    e->pad->setName(name);
    e->pad->setToolTip(path);
    const int k = e->key;
    connect(e->pad, &QAbstractButton::clicked, this, [this, k] {
        Entry* en = find(k);
        if (!en) return;
        if (en->clip) sb_->Toggle(en->clip);
        else if (!en->error.isEmpty()) { notice_->setText(en->name + QStringLiteral(": ") + en->error); notice_->show(); }
    });
    connect(e->pad, &SoundPad::menuRequested, this, [this, k](const QPoint& pos) { showPadMenu(k, pos); });

    Entry* raw = e.get();
    entries_.push_back(std::move(e));
    if (!key.isEmpty()) bindHotkey(*raw, key, true);
    return raw;
}

void SoundboardPage::startDecode(int key)
{
    Entry* e = find(key);
    if (!e) return;
    const std::wstring path = e->path.toStdWString();

    QThreadPool::globalInstance()->start([this, key, path] {
        mixcast::ComScope com;
        std::string err;
        std::shared_ptr<const mixcast::Clip> clip = mixcast::DecodeAudioFile(path, err);
        const QString qerr = QString::fromStdString(err);
        QMetaObject::invokeMethod(this, [this, key, clip, qerr] { onDecoded(key, clip, qerr); },
                                  Qt::QueuedConnection);
    });
}

void SoundboardPage::onDecoded(int key, std::shared_ptr<const mixcast::Clip> clip, const QString& error)
{
    Entry* e = find(key);
    if (!e) return;   // removed while loading
    e->loading = false;

    if (clip)
    {
        e->clip = sb_->AddClip(clip, e->gainDb);
        e->pad->setState(SoundPad::State::Ready, FormatDuration(clip->Seconds()));
    }
    else
    {
        e->error = error.isEmpty() ? QStringLiteral("Couldn't read this file") : error;
        e->pad->setState(SoundPad::State::Failed);
        e->pad->setToolTip(e->path + QStringLiteral("\n") + e->error);
    }
}

void SoundboardPage::removeEntry(int key)
{
    for (auto it = entries_.begin(); it != entries_.end(); ++it)
    {
        if ((*it)->key != key) continue;
        hotkeys_->clear(HotkeyIdFor(key));
        if ((*it)->clip) sb_->RemoveClip((*it)->clip);
        (*it)->pad->deleteLater();

        // Delete our stored copy unless another pad still uses it.
        const QString path = (*it)->path;
        bool shared = false;
        for (auto& o : entries_) if (o.get() != it->get() && o->path == path) shared = true;
        if (!shared && IsStoredSound(path)) QFile::remove(path);
        entries_.erase(it);
        break;
    }
    relayout();
    changed();
}

// ---- Editor hooks ----------------------------------------------------------------
QString SoundboardPage::padPath(int key)
{
    Entry* e = find(key);
    return e ? e->path : QString();
}

bool SoundboardPage::replaceSound(int key, const QString& newPath)
{
    Entry* e = find(key);
    if (!e) return false;

    const QString old = e->path;
    if (e->clip) { sb_->RemoveClip(e->clip); e->clip = 0; }
    e->path    = newPath;
    e->error.clear();
    e->loading = true;
    e->pad->setState(SoundPad::State::Loading);
    e->pad->setToolTip(newPath);
    startDecode(key);

    // Drop the old stored copy if nothing else uses it.
    if (old != newPath && IsStoredSound(old))
    {
        bool shared = false;
        for (auto& o : entries_) if (o->path == old) shared = true;
        if (!shared) QFile::remove(old);
    }
    changed();
    return true;
}

int SoundboardPage::addStoredSound(const QString& path, const QString& name)
{
    Entry* e = addEntry(path, name, 0.0f, {});
    startDecode(e->key);
    relayout();
    changed();
    return e->key;
}

// ---- Hotkeys -------------------------------------------------------------------
bool SoundboardPage::bindHotkey(Entry& e, const QKeySequence& seq, bool quiet)
{
    if (!seq.isEmpty())
    {
        // One key, one sound: take it away from any other pad (or Stop all).
        for (auto& o : entries_)
        {
            if (o.get() == &e || o->hotkey != seq) continue;
            hotkeys_->clear(HotkeyIdFor(o->key));
            o->hotkey = QKeySequence();
            o->pad->setHotkey({});
        }
        if (stopAllKey_ == seq)
        {
            hotkeys_->clear(kStopAllHotkeyId);
            stopAllKey_ = QKeySequence();
            updateStopAllLabel();
        }
    }

    QString err;
    const bool ok = hotkeys_->set(HotkeyIdFor(e.key), seq, &err);
    e.hotkey = ok ? seq : QKeySequence();
    e.pad->setHotkey(HotkeyLabel(e.hotkey));
    if (!ok)
    {
        notice_->setText(QStringLiteral("%1: %2").arg(e.name, err));
        notice_->show();
        if (!quiet) QTimer::singleShot(8000, notice_, &QLabel::hide);
    }
    return ok;
}

void SoundboardPage::editHotkey(int key)
{
    Entry* e = find(key);
    if (!e) return;
    HotkeyDialog dlg(e->name, e->hotkey, this);
    if (dlg.exec() != QDialog::Accepted) return;
    bindHotkey(*e, dlg.sequence(), false);
    changed();
}

void SoundboardPage::editStopAllHotkey()
{
    HotkeyDialog dlg(QStringLiteral("Stop all"), stopAllKey_, this);
    if (dlg.exec() != QDialog::Accepted) return;
    const QKeySequence seq = dlg.sequence();

    for (auto& o : entries_)
        if (!seq.isEmpty() && o->hotkey == seq)
        {
            hotkeys_->clear(HotkeyIdFor(o->key));
            o->hotkey = QKeySequence();
            o->pad->setHotkey({});
        }

    QString err;
    if (hotkeys_->set(kStopAllHotkeyId, seq, &err)) stopAllKey_ = seq;
    else
    {
        stopAllKey_ = QKeySequence();
        notice_->setText(err);
        notice_->show();
        QTimer::singleShot(8000, notice_, &QLabel::hide);
    }
    updateStopAllLabel();
    changed();
}

void SoundboardPage::updateStopAllLabel()
{
    stopAllKeyBtn_->setText(stopAllKey_.isEmpty() ? QStringLiteral("Set stop key")
                                                  : QStringLiteral("Stop key: %1").arg(HotkeyLabel(stopAllKey_)));
}

bool SoundboardPage::handleHotkey(int id)
{
    if (id == kStopAllHotkeyId) { sb_->StopAll(); return true; }
    if (id < 1000) return false;
    Entry* e = find(id - 1000);
    if (!e) return false;
    if (e->clip) sb_->Play(e->clip);   // hotkey always (re)starts, like Soundpad
    return true;
}

// ---- Pad menu -------------------------------------------------------------------
void SoundboardPage::showPadMenu(int key, const QPoint& globalPos)
{
    Entry* e = find(key);
    if (!e) return;

    bool isPlaying = false;
    for (const auto& p : sb_->Playing()) if (p.id == e->clip) isPlaying = true;

    QMenu menu(this);
    QAction* play = menu.addAction(isPlaying ? QStringLiteral("Stop") : QStringLiteral("Play"));
    play->setEnabled(e->clip != 0);
    QAction* hotkey = menu.addAction(e->hotkey.isEmpty() ? QStringLiteral("Set hotkey\u2026")
                                                         : QStringLiteral("Change hotkey (%1)\u2026").arg(HotkeyLabel(e->hotkey)));

    // Inline volume slider.
    auto* volRow = new QWidget(&menu);
    auto* vl = new QHBoxLayout(volRow);
    vl->setContentsMargins(18, 6, 14, 6);
    auto* vlab = new QLabel(QStringLiteral("Volume"));
    vl->addWidget(vlab);
    auto* vol = new QSlider(Qt::Horizontal);
    vol->setRange(-30, 12);
    vol->setValue(static_cast<int>(std::lround(e->gainDb)));
    vol->setFixedWidth(110);
    vl->addWidget(vol);
    auto* vval = new QLabel(FormatDb(e->gainDb));
    vval->setFixedWidth(46);
    vl->addWidget(vval);
    connect(vol, &QSlider::valueChanged, this, [this, key, vval](int v) {
        Entry* en = find(key);
        if (!en) return;
        en->gainDb = static_cast<float>(v);
        if (en->clip) sb_->SetClipGain(en->clip, en->gainDb);
        vval->setText(FormatDb(en->gainDb));
        changed();
    });
    auto* volAction = new QWidgetAction(&menu);
    volAction->setDefaultWidget(volRow);
    menu.addAction(volAction);

    QAction* editAct = menu.addAction(QStringLiteral("Edit\u2026"));
    editAct->setToolTip(QStringLiteral("Cut, trim and fade this sound in the Editor"));
    editAct->setEnabled(QFileInfo::exists(e->path));
    QAction* rename = menu.addAction(QStringLiteral("Rename\u2026"));
    menu.addSeparator();
    QAction* remove = menu.addAction(QStringLiteral("Remove"));

    QAction* chosen = menu.exec(globalPos);
    if (!chosen) return;

    if (chosen == play && e->clip)  sb_->Toggle(e->clip);
    else if (chosen == hotkey)      editHotkey(key);
    else if (chosen == editAct)     emit editRequested(key, e->path, e->name);
    else if (chosen == rename)
    {
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("Rename sound"), QStringLiteral("Name"),
                                                   QLineEdit::Normal, e->name, &ok).trimmed();
        if (ok && !name.isEmpty()) { e->name = name; e->pad->setName(name); changed(); }
    }
    else if (chosen == remove)      removeEntry(key);
}

// ---- Layout / ticking -----------------------------------------------------------
void SoundboardPage::relayout()
{
    const int padW = 172, gap = 10;
    const int avail = width() - 40;   // page width minus side margins (viewport may not be sized yet)
    const int cols = std::max(1, (avail + gap) / (padW + gap));
    columns_ = cols;

    while (gridLayout_->count() > 0) delete gridLayout_->takeAt(0);
    int i = 0;
    for (auto& e : entries_)
    {
        gridLayout_->addWidget(e->pad, i / cols, i % cols);
        i++;
    }
    gridLayout_->setColumnStretch(cols, 1);
    gridLayout_->setRowStretch((i + cols - 1) / cols, 1);

    const bool none = entries_.empty();
    empty_->setVisible(none);
    scroll_->setVisible(!none);
}

void SoundboardPage::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    const int cols = std::max(1, (width() - 40 + 10) / (172 + 10));
    if (cols != columns_) relayout();
}

void SoundboardPage::tick()
{
    std::map<mixcast::ClipId, float> playing;
    for (const auto& p : sb_->Playing()) playing[p.id] = p.progress;
    playing_ = static_cast<int>(playing.size());

    for (auto& e : entries_)
    {
        auto it = e->clip ? playing.find(e->clip) : playing.end();
        e->pad->setProgress(it != playing.end() ? it->second : -1.0f);
    }
}

// ---- Drag & drop --------------------------------------------------------------------
void SoundboardPage::dragEnterEvent(QDragEnterEvent* e)
{
    if (!e->mimeData()->hasUrls()) return;
    for (const QUrl& u : e->mimeData()->urls())
        if (u.isLocalFile() && kAudioExtensions.contains(QFileInfo(u.toLocalFile()).suffix().toLower()))
        {
            e->acceptProposedAction();
            return;
        }
}

void SoundboardPage::dropEvent(QDropEvent* e)
{
    QStringList files;
    for (const QUrl& u : e->mimeData()->urls())
        if (u.isLocalFile()) files << u.toLocalFile();
    addFiles(files);
    e->acceptProposedAction();
}

// ---- Settings -----------------------------------------------------------------------
void SoundboardPage::loadSettings()
{
    QSettings s;

    const QSignalBlocker b1(oneAtATime_), b2(monitorBtn_), b3(monitorVol_);
    oneAtATime_->setChecked(s.value(QStringLiteral("soundboard/oneAtATime"), false).toBool());
    monitorBtn_->setChecked(s.value(QStringLiteral("soundboard/monitor"), true).toBool());
    monitorVol_->setValue(s.value(QStringLiteral("soundboard/monitorGain"), -6).toInt());
    monitorVol_->setEnabled(monitorBtn_->isChecked());
    monitorLbl_->setText(FormatDb(static_cast<float>(monitorVol_->value())));
    sb_->oneAtATime     = oneAtATime_->isChecked();
    sb_->monitorEnabled = monitorBtn_->isChecked();
    sb_->monitorGainDb  = static_cast<float>(monitorVol_->value());
    sb_->Bus().gainDb   = s.value(QStringLiteral("soundboard/busGain"), 0.0f).toFloat();
    sb_->Bus().enabled  = s.value(QStringLiteral("soundboard/busEnabled"), true).toBool();

    const int n = s.beginReadArray(QStringLiteral("soundboard/pads"));
    for (int i = 0; i < n; i++)
    {
        s.setArrayIndex(i);
        QString path = s.value(QStringLiteral("path")).toString();
        if (path.isEmpty()) continue;
        path = StoreSoundFile(path);   // older pads pointed at the original files: copy them in
        Entry* e = addEntry(path,
                            s.value(QStringLiteral("name"), QFileInfo(path).completeBaseName()).toString(),
                            s.value(QStringLiteral("gain"), 0.0f).toFloat(),
                            QKeySequence(s.value(QStringLiteral("key")).toString(), QKeySequence::PortableText));
        startDecode(e->key);
    }
    s.endArray();
    changed();   // save any paths that just moved into the Sounds folder

    const QKeySequence stop(s.value(QStringLiteral("soundboard/stopAllKey")).toString(), QKeySequence::PortableText);
    if (!stop.isEmpty() && hotkeys_->set(kStopAllHotkeyId, stop)) stopAllKey_ = stop;
    updateStopAllLabel();

    relayout();
}

void SoundboardPage::saveSettings()
{
    QSettings s;
    s.setValue(QStringLiteral("soundboard/oneAtATime"),  oneAtATime_->isChecked());
    s.setValue(QStringLiteral("soundboard/monitor"),     monitorBtn_->isChecked());
    s.setValue(QStringLiteral("soundboard/monitorGain"), monitorVol_->value());
    s.setValue(QStringLiteral("soundboard/busGain"),     sb_->Bus().gainDb.load());
    s.setValue(QStringLiteral("soundboard/busEnabled"),  sb_->Bus().enabled.load());
    s.setValue(QStringLiteral("soundboard/stopAllKey"),  stopAllKey_.toString(QKeySequence::PortableText));

    s.remove(QStringLiteral("soundboard/pads"));
    s.beginWriteArray(QStringLiteral("soundboard/pads"));
    int i = 0;
    for (auto& e : entries_)
    {
        s.setArrayIndex(i++);
        s.setValue(QStringLiteral("path"), e->path);
        s.setValue(QStringLiteral("name"), e->name);
        s.setValue(QStringLiteral("gain"), e->gainDb);
        s.setValue(QStringLiteral("key"),  e->hotkey.toString(QKeySequence::PortableText));
    }
    s.endArray();
}
