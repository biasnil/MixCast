// MixCast GUI - audio editor tab.
#include "editor_page.h"
#include "app_paths.h"
#include "audio_decoder.h"
#include "audio_encoder.h"
#include "common.h"
#include "soundboard_page.h"
#include "theme.h"

#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
#include <QStandardPaths>
#include <QStyle>
#include <QThreadPool>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

using namespace mixcast;
namespace ed = mixcast::edit;

ed::Samples EditorPage::clipboard_;

namespace {

constexpr double kRate = 48000.0;

const QStringList kOpenable = { QStringLiteral("mp3"), QStringLiteral("wav"), QStringLiteral("m4a"),
                                QStringLiteral("aac"), QStringLiteral("wma"), QStringLiteral("flac") };

QString FormatTime(double seconds, int decimals = 3)
{
    if (seconds < 0) seconds = 0;
    const int m = static_cast<int>(seconds / 60);
    const double s = seconds - m * 60;
    const int width = decimals > 0 ? 3 + decimals : 2;
    return QStringLiteral("%1:%2").arg(m).arg(s, width, 'f', decimals, QLatin1Char('0'));
}

QString FramesToTime(size_t frames) { return FormatTime(frames / kRate); }

} // namespace

// ============================================================================
// WaveformView
// ============================================================================
WaveformView::WaveformView(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumHeight(180);
    setCursor(Qt::IBeamCursor);
}

size_t WaveformView::totalFrames() const { return audio_ ? audio_->size() / 2 : 0; }

void WaveformView::setAudio(ed::Buffer audio, std::shared_ptr<const ed::Peaks> peaks, bool keepView)
{
    audio_ = std::move(audio);
    peaks_ = std::move(peaks);
    const size_t n = totalFrames();
    sel_ = ed::Range{ std::min(sel_.a, n), std::min(sel_.b, n) };
    cursor_ = std::min(cursor_, n);
    if (!keepView) zoomFit();
    else clampView();
    update();
    emit viewChanged();
}

size_t WaveformView::clampFrame(double f) const
{
    return static_cast<size_t>(std::clamp(f, 0.0, static_cast<double>(totalFrames())));
}

void WaveformView::setSelection(Range r)
{
    const size_t n = totalFrames();
    sel_ = ed::Range{ std::min(r.a, n), std::min(r.b, n) };
    if (sel_.b < sel_.a) std::swap(sel_.a, sel_.b);
    update();
    emit selectionChanged();
}

void WaveformView::setCursor(size_t frame)
{
    cursor_ = std::min(frame, totalFrames());
    update();
    emit selectionChanged();
}

void WaveformView::setPlayhead(long long frame)
{
    if (frame == playhead_) return;
    playhead_ = frame;
    update();
}

void WaveformView::clampView()
{
    const double n = static_cast<double>(totalFrames());
    const double w = std::max(1, width());
    const double maxFpp = std::max(n / w, 0.05);
    fpp_ = std::clamp(fpp_, 0.05, maxFpp);          // 0.05 = 20 px per sample
    first_ = std::clamp(first_, 0.0, std::max(0.0, n - w * fpp_));
}

void WaveformView::zoomAround(double factor, double x)
{
    const double f = frameAtX(x);
    fpp_ /= factor;
    clampView();
    first_ = f - x * fpp_;
    clampView();
    update();
    emit viewChanged();
}

void WaveformView::zoomIn()  { zoomAround(2.0, width() / 2.0); }
void WaveformView::zoomOut() { zoomAround(0.5, width() / 2.0); }

void WaveformView::zoomFit()
{
    fpp_ = 1e12;   // clampView brings it to "whole clip"
    first_ = 0;
    clampView();
    update();
    emit viewChanged();
}

void WaveformView::zoomToSelection()
{
    if (sel_.empty()) return zoomFit();
    const double len = static_cast<double>(sel_.length());
    fpp_ = len * 1.1 / std::max(1, width());
    clampView();
    first_ = sel_.a - len * 0.05;
    clampView();
    update();
    emit viewChanged();
}

void WaveformView::follow(size_t frame)
{
    const double x = xAtFrame(static_cast<double>(frame));
    if (x < 0 || x > width() - 20)
    {
        first_ = frame - width() * fpp_ * 0.1;   // page forward, playhead near the left
        clampView();
        update();
        emit viewChanged();
    }
}

int WaveformView::scrollMax() const
{
    const double total = totalFrames() / fpp_;
    return static_cast<int>(std::max(0.0, total - width()));
}

int  WaveformView::scrollValue() const { return static_cast<int>(first_ / fpp_); }

void WaveformView::setScrollValue(int v)
{
    first_ = v * fpp_;
    clampView();
    update();
}

void WaveformView::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    const bool wasFit = first_ <= 0.0 && fpp_ * (width() > 0 ? width() : 1) >= totalFrames() * 0.98;
    if (wasFit) zoomFit(); else clampView();
    emit viewChanged();
}

// ---- Painting ---------------------------------------------------------------------
void WaveformView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), theme::Slot);

    if (!audio_ || totalFrames() == 0) return;

    paintRuler(p);
    const int top = kRuler, h = height() - kRuler;

    // Selection band.
    if (!sel_.empty())
    {
        const double x0 = std::max(0.0, xAtFrame(sel_.a));
        const double x1 = std::min<double>(width(), xAtFrame(sel_.b));
        QColor band = theme::Legend;
        band.setAlpha(28);
        p.fillRect(QRectF(x0, top, std::max(1.0, x1 - x0), h), band);
        p.setPen(QPen(theme::AccentHot, 1));
        p.drawLine(QPointF(x0, top), QPointF(x0, height()));
        p.drawLine(QPointF(x1, top), QPointF(x1, height()));
    }

    paintWave(p, top, h);

    // Centre line.
    p.setPen(QPen(theme::PanelEdge, 1));
    p.drawLine(0, top + h / 2, width(), top + h / 2);

    // Cursor (where Play / Paste start).
    if (sel_.empty())
    {
        const double x = xAtFrame(static_cast<double>(cursor_));
        p.setPen(QPen(theme::Legend, 1, Qt::DashLine));
        p.drawLine(QPointF(x, top), QPointF(x, height()));
    }

    // Playhead.
    if (playhead_ >= 0)
    {
        const double x = xAtFrame(static_cast<double>(playhead_));
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(theme::Legend, 2));
        p.drawLine(QPointF(x, top), QPointF(x, height()));
        QPolygonF tri;
        tri << QPointF(x - 6, top) << QPointF(x + 6, top) << QPointF(x, top + 8);
        p.setPen(Qt::NoPen);
        p.setBrush(theme::Legend);
        p.drawPolygon(tri);
    }
}

void WaveformView::paintRuler(QPainter& p)
{
    p.fillRect(QRect(0, 0, width(), kRuler), theme::Panel);
    p.setPen(QPen(theme::PanelEdge, 1));
    p.drawLine(0, kRuler - 1, width(), kRuler - 1);

    // Pick a tick step that leaves ~90 px between labels.
    const double steps[] = { 0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120 };
    double step = steps[std::size(steps) - 1];
    for (double s : steps) if (s * kRate / fpp_ >= 90.0) { step = s; break; }
    const int decimals = step < 0.01 ? 3 : step < 0.1 ? 2 : step < 1 ? 1 : 0;

    p.setFont(theme::Font(8.5));
    const double t0 = first_ / kRate;
    const double t1 = (first_ + width() * fpp_) / kRate;
    for (double t = std::floor(t0 / step) * step; t <= t1; t += step)
    {
        const double x = xAtFrame(t * kRate);
        p.setPen(QPen(theme::PanelEdge, 1));
        p.drawLine(QPointF(x, kRuler - 8), QPointF(x, kRuler - 1));
        // Minor ticks.
        for (int k = 1; k < 5; k++)
        {
            const double xm = xAtFrame((t + step * k / 5.0) * kRate);
            p.drawLine(QPointF(xm, kRuler - 4), QPointF(xm, kRuler - 1));
        }
        p.setPen(theme::Muted);
        p.drawText(QPointF(x + 4, kRuler - 10), FormatTime(t, decimals));
    }
}

void WaveformView::paintWave(QPainter& p, int top, int h)
{
    const float* s = audio_->data();
    const size_t n = totalFrames();
    const double mid = top + h / 2.0;
    const double amp = h / 2.0 - 6.0;

    auto colourFor = [&](double frame) {
        return (!sel_.empty() && frame >= sel_.a && frame < sel_.b) ? theme::AccentHot : theme::Accent;
    };

    if (fpp_ < 1.0)
    {
        // Zoomed in past one sample per pixel: draw the actual sample curve.
        p.setRenderHint(QPainter::Antialiasing);
        const size_t f0 = static_cast<size_t>(std::max(0.0, std::floor(first_)));
        const size_t f1 = std::min(n, static_cast<size_t>(std::ceil(first_ + width() * fpp_)) + 1);
        QPointF prev;
        for (size_t f = f0; f < f1; f++)
        {
            const float v = 0.5f * (s[f * 2] + s[f * 2 + 1]);
            const QPointF pt(xAtFrame(static_cast<double>(f)), mid - v * amp);
            if (f > f0)
            {
                p.setPen(QPen(colourFor(static_cast<double>(f)), 1.5));
                p.drawLine(prev, pt);
            }
            if (fpp_ < 0.15)
            {
                p.setPen(Qt::NoPen);
                p.setBrush(colourFor(static_cast<double>(f)));
                p.drawEllipse(pt, 2.2, 2.2);
            }
            prev = pt;
        }
        return;
    }

    // One vertical min/max line per pixel column.
    const size_t block = ed::Peaks::kBlock;
    for (int x = 0; x < width(); x++)
    {
        const double fa = first_ + x * fpp_;
        const double fb = fa + fpp_;
        if (fa >= n) break;

        float lo = 0.0f, hi = 0.0f;
        if (fpp_ >= block && peaks_)
        {
            const size_t b0 = static_cast<size_t>(fa / block);
            const size_t b1 = std::min(peaks_->mx.size(), static_cast<size_t>(std::ceil(fb / block)));
            for (size_t b = b0; b < b1; b++) { lo = std::min(lo, peaks_->mn[b]); hi = std::max(hi, peaks_->mx[b]); }
        }
        else
        {
            const size_t a = static_cast<size_t>(fa);
            const size_t b = std::min(n, static_cast<size_t>(std::ceil(fb)));
            for (size_t f = a; f < b; f++)
            {
                lo = std::min({ lo, s[f * 2], s[f * 2 + 1] });
                hi = std::max({ hi, s[f * 2], s[f * 2 + 1] });
            }
        }
        p.setPen(colourFor(fa));
        const double y0 = mid - hi * amp, y1 = mid - lo * amp;
        p.drawLine(QPointF(x + 0.5, y0), QPointF(x + 0.5, std::max(y1, y0 + 1.0)));
    }
}

// ---- Mouse ---------------------------------------------------------------------------
void WaveformView::mousePressEvent(QMouseEvent* e)
{
    if (!audio_ || e->button() != Qt::LeftButton) return;
    setFocus();
    pressPos_ = e->pos();
    const double x = e->position().x();
    const size_t f = clampFrame(frameAtX(x));

    if (!sel_.empty() && std::fabs(x - xAtFrame(sel_.a)) <= 5) { drag_ = Drag::EdgeA; return; }
    if (!sel_.empty() && std::fabs(x - xAtFrame(sel_.b)) <= 5) { drag_ = Drag::EdgeB; return; }

    drag_ = Drag::Select;
    if (e->modifiers() & Qt::ShiftModifier)
    {
        anchor_ = sel_.empty() ? cursor_ : (f < sel_.a ? sel_.b : sel_.a);
        setSelection({ std::min(anchor_, f), std::max(anchor_, f) });
    }
    else
    {
        anchor_ = f;
        sel_ = {};
        cursor_ = f;
        update();
    }
}

void WaveformView::mouseMoveEvent(QMouseEvent* e)
{
    const double x = e->position().x();
    if (drag_ == Drag::None)
    {
        const bool onEdge = !sel_.empty() &&
            (std::fabs(x - xAtFrame(sel_.a)) <= 5 || std::fabs(x - xAtFrame(sel_.b)) <= 5);
        QWidget::setCursor(onEdge ? Qt::SizeHorCursor : Qt::IBeamCursor);
        return;
    }

    // Dragging past the edge scrolls the view.
    if (x < 0)            { first_ += x * fpp_ * 0.2; clampView(); emit viewChanged(); }
    if (x > width())      { first_ += (x - width()) * fpp_ * 0.2; clampView(); emit viewChanged(); }

    const size_t f = clampFrame(frameAtX(std::clamp(x, 0.0, static_cast<double>(width()))));
    if (drag_ == Drag::Select)     sel_ = { std::min(anchor_, f), std::max(anchor_, f) };
    else if (drag_ == Drag::EdgeA) sel_ = { std::min(f, sel_.b), std::max(f, sel_.b) };
    else if (drag_ == Drag::EdgeB) sel_ = { std::min(sel_.a, f), std::max(sel_.a, f) };
    update();
    emit selectionChanged();
}

void WaveformView::mouseReleaseEvent(QMouseEvent* e)
{
    if (drag_ == Drag::Select && (e->pos() - pressPos_).manhattanLength() < 3)
    {
        sel_ = {};   // a click just moves the cursor
        update();
    }
    if (!sel_.empty()) cursor_ = sel_.a;
    drag_ = Drag::None;
    emit selectionChanged();
}

void WaveformView::mouseDoubleClickEvent(QMouseEvent*)
{
    setSelection({ 0, totalFrames() });   // double-click selects everything
}

void WaveformView::wheelEvent(QWheelEvent* e)
{
    const double steps = e->angleDelta().y() / 120.0;
    if (e->modifiers() & Qt::ControlModifier)
        zoomAround(std::pow(1.25, steps), e->position().x());
    else
    {
        const double dx = (e->angleDelta().x() != 0 ? -e->angleDelta().x() : -e->angleDelta().y()) / 120.0;
        first_ += dx * width() * 0.15 * fpp_;
        clampView();
        update();
        emit viewChanged();
    }
    e->accept();
}

// ============================================================================
// EditorPage
// ============================================================================
static QToolButton* ToolButton(const QString& text, const QString& tip)
{
    auto* b = new QToolButton;
    b->setObjectName(QStringLiteral("Tool"));
    b->setText(text);
    b->setToolTip(tip);
    b->setToolButtonStyle(Qt::ToolButtonTextOnly);
    b->setCursor(Qt::PointingHandCursor);
    b->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    return b;
}

static QFrame* ToolDivider()
{
    auto* d = new QFrame;
    d->setObjectName(QStringLiteral("Divider"));
    d->setFixedSize(1, 22);
    return d;
}

EditorPage::EditorPage(SoundboardPage* soundboard, QWidget* parent) : QWidget(parent), sb_(soundboard)
{
    setAcceptDrops(true);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 10, 20, 14);
    root->setSpacing(10);
    root->addWidget(buildFileBar());

    editArea_ = new QWidget;
    auto* el = new QVBoxLayout(editArea_);
    el->setContentsMargins(0, 0, 0, 0);
    el->setSpacing(8);
    el->addWidget(buildToolBar());

    view_ = new WaveformView;
    el->addWidget(view_, 1);
    hscroll_ = new QScrollBar(Qt::Horizontal);
    el->addWidget(hscroll_);

    auto* status = new QHBoxLayout;
    lengthLbl_ = new QLabel;
    lengthLbl_->setObjectName(QStringLiteral("Muted"));
    selLbl_ = new QLabel;
    status->addWidget(lengthLbl_);
    status->addSpacing(18);
    status->addWidget(selLbl_);
    status->addStretch(1);

    auto* zOut = ToolButton(QStringLiteral("\u2212"), QStringLiteral("Zoom out (Ctrl + scroll)"));
    auto* zIn  = ToolButton(QStringLiteral("+"), QStringLiteral("Zoom in (Ctrl + scroll)"));
    auto* zSel = ToolButton(QStringLiteral("Selection"), QStringLiteral("Zoom to the selection"));
    auto* zFit = ToolButton(QStringLiteral("Fit"), QStringLiteral("Show the whole sound"));
    connect(zOut, &QToolButton::clicked, this, [this] { view_->zoomOut(); });
    connect(zIn,  &QToolButton::clicked, this, [this] { view_->zoomIn(); });
    connect(zSel, &QToolButton::clicked, this, [this] { view_->zoomToSelection(); });
    connect(zFit, &QToolButton::clicked, this, [this] { view_->zoomFit(); });
    auto* zl = new QLabel(QStringLiteral("Zoom"));
    zl->setObjectName(QStringLiteral("Muted"));
    status->addWidget(zl);
    for (auto* b : { zOut, zIn, zSel, zFit }) status->addWidget(b);
    needsSelection_ << zSel;
    el->addLayout(status);

    view_->setToolTip(QStringLiteral("Drag to select. Drag the edges to adjust. Double-click selects all.\n"
                                     "Scroll to move, Ctrl + scroll to zoom, Space to play."));
    root->addWidget(editArea_, 1);

    empty_ = new QLabel(QStringLiteral("Open a sound to edit it.\n\n"
                                       "Right-click a pad on the Soundboard tab and choose Edit,\n"
                                       "drop an audio file here, or press Open."));
    empty_->setObjectName(QStringLiteral("Muted"));
    empty_->setAlignment(Qt::AlignCenter);
    empty_->setFont(theme::Font(11));
    root->addWidget(empty_, 1);

    connect(view_, &WaveformView::selectionChanged, this, &EditorPage::updateUi);
    connect(view_, &WaveformView::viewChanged, this, [this] {
        const QSignalBlocker b(hscroll_);
        hscroll_->setRange(0, view_->scrollMax());
        hscroll_->setPageStep(std::max(1, view_->width()));
        hscroll_->setSingleStep(std::max(1, view_->width() / 10));
        hscroll_->setValue(view_->scrollValue());
    });
    connect(hscroll_, &QScrollBar::valueChanged, view_, &WaveformView::setScrollValue);

    setupShortcuts();
    updateUi();
}

EditorPage::~EditorPage()
{
    player_.Stop();
    QThreadPool::globalInstance()->waitForDone();
}

QWidget* EditorPage::buildFileBar()
{
    auto* w = new QWidget;
    auto* l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(8);

    auto* open = new QPushButton(QStringLiteral("Open\u2026"));
    open->setToolTip(QStringLiteral("Open an audio file (Ctrl+O)"));
    connect(open, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Open audio"), QString(),
                                                          QStringLiteral("Audio (*.mp3 *.wav *.m4a *.aac *.wma *.flac)"));
        if (!path.isEmpty()) openFile(path);
    });
    l->addWidget(open);
    l->addSpacing(8);

    titleLbl_ = new QLabel;
    titleLbl_->setFont(theme::Font(12, QFont::DemiBold));
    l->addWidget(titleLbl_);
    sourceLbl_ = new QLabel;
    sourceLbl_->setObjectName(QStringLiteral("Chip"));
    l->addWidget(sourceLbl_);
    l->addSpacing(12);

    // Latest message ("Saved", "Exported to ...") sits in the free space here.
    statusLbl_ = new QLabel;
    statusLbl_->setObjectName(QStringLiteral("Muted"));
    statusLbl_->setMinimumWidth(40);
    statusLbl_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    l->addWidget(statusLbl_, 1);

    undoBtn_ = ToolButton(QStringLiteral("Undo"), QStringLiteral("Undo (Ctrl+Z)"));
    redoBtn_ = ToolButton(QStringLiteral("Redo"), QStringLiteral("Redo (Ctrl+Y)"));
    connect(undoBtn_, &QToolButton::clicked, this, &EditorPage::undo);
    connect(redoBtn_, &QToolButton::clicked, this, &EditorPage::redo);
    l->addWidget(undoBtn_);
    l->addWidget(redoBtn_);
    l->addSpacing(10);

    addPadBtn_ = new QPushButton(QStringLiteral("Add to soundboard"));
    addPadBtn_->setToolTip(QStringLiteral("Make this a soundboard pad. After that, saving updates the pad."));
    connect(addPadBtn_, &QPushButton::clicked, this, &EditorPage::addToSoundboard);
    l->addWidget(addPadBtn_);

    exportBtn_ = new QPushButton(QStringLiteral("Export\u2026"));
    exportBtn_->setToolTip(QStringLiteral("Save a copy as MP3, WAV or M4A"));
    connect(exportBtn_, &QPushButton::clicked, this, &EditorPage::exportFile);
    l->addWidget(exportBtn_);

    saveBtn_ = new QPushButton(QStringLiteral("Save to pad"));
    saveBtn_->setObjectName(QStringLiteral("Primary"));
    saveBtn_->setToolTip(QStringLiteral("Replace the pad's sound with this edit (Ctrl+S)"));
    connect(saveBtn_, &QPushButton::clicked, this, &EditorPage::saveToPad);
    l->addWidget(saveBtn_);

    needsAudio_ << addPadBtn_ << exportBtn_ << saveBtn_;
    return w;
}

QWidget* EditorPage::buildToolBar()
{
    auto* w = new QWidget;
    auto* l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(4);

    playBtn_ = ToolButton(QStringLiteral("Play"), QStringLiteral("Play the selection, or from the cursor (Space)"));
    auto* stop = ToolButton(QStringLiteral("Stop"), QStringLiteral("Stop"));
    loopBtn_ = ToolButton(QStringLiteral("Loop"), QStringLiteral("Repeat while playing"));
    loopBtn_->setCheckable(true);
    connect(playBtn_, &QToolButton::clicked, this, &EditorPage::playPause);
    connect(stop, &QToolButton::clicked, this, &EditorPage::stopPlayback);
    connect(loopBtn_, &QToolButton::toggled, this, [this](bool on) { player_.SetLoop(on); });
    l->addWidget(playBtn_);
    l->addWidget(stop);
    l->addWidget(loopBtn_);
    l->addSpacing(6);
    l->addWidget(ToolDivider());
    l->addSpacing(6);

    auto* cutB  = ToolButton(QStringLiteral("Cut"), QStringLiteral("Cut the selection (Ctrl+X)"));
    auto* copyB = ToolButton(QStringLiteral("Copy"), QStringLiteral("Copy the selection (Ctrl+C)"));
    pasteBtn_   = ToolButton(QStringLiteral("Paste"), QStringLiteral("Paste at the cursor, or over the selection (Ctrl+V)"));
    auto* delB  = ToolButton(QStringLiteral("Delete"), QStringLiteral("Remove the selection (Delete)"));
    auto* keepB = ToolButton(QStringLiteral("Crop"), QStringLiteral("Keep only the selection; remove everything else (Ctrl+T)"));
    connect(cutB,  &QToolButton::clicked, this, &EditorPage::cut);
    connect(copyB, &QToolButton::clicked, this, &EditorPage::copy);
    connect(pasteBtn_, &QToolButton::clicked, this, &EditorPage::paste);
    connect(delB,  &QToolButton::clicked, this, &EditorPage::deleteSelection);
    connect(keepB, &QToolButton::clicked, this, &EditorPage::keepSelection);
    for (auto* b : { cutB, copyB, pasteBtn_, delB, keepB }) l->addWidget(b);
    needsSelection_ << cutB << copyB << delB << keepB;

    l->addSpacing(6);
    l->addWidget(ToolDivider());
    l->addSpacing(6);

    auto* fadeInB  = ToolButton(QStringLiteral("Fade in"), QStringLiteral("Fade in the selection (or the first half second)"));
    auto* fadeOutB = ToolButton(QStringLiteral("Fade out"), QStringLiteral("Fade out the selection (or the last half second)"));
    auto* volB     = ToolButton(QStringLiteral("Volume\u2026"), QStringLiteral("Make the selection (or everything) louder or quieter"));
    auto* moreB    = ToolButton(QStringLiteral("More \u25BE"), QStringLiteral("Normalize, trim silence, reverse"));
    moreB->setPopupMode(QToolButton::InstantPopup);
    auto* more = new QMenu(moreB);
    more->setToolTipsVisible(true);
    QAction* normA = more->addAction(QStringLiteral("Normalize"));
    normA->setToolTip(QStringLiteral("Make it as loud as possible without clipping"));
    QAction* trimA = more->addAction(QStringLiteral("Trim silence at start and end"));
    QAction* revA  = more->addAction(QStringLiteral("Reverse"));
    revA->setToolTip(QStringLiteral("Play the selection (or everything) backwards"));
    moreB->setMenu(more);

    connect(fadeInB, &QToolButton::clicked, this, [this] {
        const Range r = effectRange(true, false);
        applyEdit(QStringLiteral("Fade in"), [r](const Samples& s) { Samples o = s; ed::FadeIn(o, r); return o; },
                  view_->selection(), view_->cursor());
    });
    connect(fadeOutB, &QToolButton::clicked, this, [this] {
        const Range r = effectRange(false, true);
        applyEdit(QStringLiteral("Fade out"), [r](const Samples& s) { Samples o = s; ed::FadeOut(o, r); return o; },
                  view_->selection(), view_->cursor());
    });
    connect(volB, &QToolButton::clicked, this, [this] {
        bool ok = false;
        const double db = QInputDialog::getDouble(this, QStringLiteral("Volume"),
                                                  QStringLiteral("Change the volume by (dB).\nPositive is louder, negative is quieter."),
                                                  3.0, -30.0, 30.0, 1, &ok);
        if (!ok) return;
        const Range r = effectRange();
        applyEdit(QStringLiteral("Volume"), [r, db](const Samples& s) { Samples o = s; ed::Gain(o, r, static_cast<float>(db)); return o; },
                  view_->selection(), view_->cursor());
    });
    connect(normA, &QAction::triggered, this, [this] {
        const Range r = effectRange();
        applyEdit(QStringLiteral("Normalize"), [r](const Samples& s) { Samples o = s; ed::Normalize(o, r, -1.0f); return o; },
                  view_->selection(), view_->cursor());
    });
    connect(trimA, &QAction::triggered, this, [this] {
        const Range sound = ed::FindSound(*audio_);
        if (sound.empty()) { showStatus(QStringLiteral("It's all silence; nothing to keep."), true); return; }
        if (sound.a == 0 && sound.b == ed::Frames(*audio_)) { showStatus(QStringLiteral("No silence at the start or end.")); return; }
        applyEdit(QStringLiteral("Trim silence"), [sound](const Samples& s) { return ed::Keep(s, sound); }, {}, 0, true);
    });
    connect(revA, &QAction::triggered, this, [this] {
        const Range r = effectRange();
        applyEdit(QStringLiteral("Reverse"), [r](const Samples& s) { Samples o = s; ed::Reverse(o, r); return o; },
                  view_->selection(), view_->cursor());
    });
    for (auto* b : { fadeInB, fadeOutB, volB, moreB }) l->addWidget(b);
    l->addStretch(1);
    return w;
}

void EditorPage::setupShortcuts()
{
    auto add = [this](const QKeySequence& k, auto fn) {
        auto* s = new QShortcut(k, this);
        s->setContext(Qt::WidgetWithChildrenShortcut);
        connect(s, &QShortcut::activated, this, fn);
    };
    add(QKeySequence(Qt::Key_Space), [this] { if (audio_) playPause(); });
    add(QKeySequence::Undo,  [this] { undo(); });
    add(QKeySequence::Redo,  [this] { redo(); });
    add(QKeySequence(Qt::CTRL | Qt::Key_Y), [this] { redo(); });
    add(QKeySequence::Cut,   [this] { cut(); });
    add(QKeySequence::Copy,  [this] { copy(); });
    add(QKeySequence::Paste, [this] { paste(); });
    add(QKeySequence::Delete, [this] { deleteSelection(); });
    add(QKeySequence(Qt::Key_Backspace), [this] { deleteSelection(); });
    add(QKeySequence::SelectAll, [this] { if (audio_) view_->setSelection({ 0, view_->totalFrames() }); });
    add(QKeySequence(Qt::CTRL | Qt::Key_T), [this] { keepSelection(); });
    add(QKeySequence::Save,  [this] { saveOrExport(); });
    add(QKeySequence::Open,  [this] {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Open audio"), QString(),
                                                          QStringLiteral("Audio (*.mp3 *.wav *.m4a *.aac *.wma *.flac)"));
        if (!path.isEmpty()) openFile(path);
    });
    add(QKeySequence(Qt::Key_Home), [this] { if (audio_) { view_->setSelection({}); view_->setCursor(0); } });
    add(QKeySequence(Qt::Key_End),  [this] { if (audio_) { view_->setSelection({}); view_->setCursor(view_->totalFrames()); } });
    add(QKeySequence(Qt::Key_Escape), [this] { if (audio_) view_->setSelection({}); });
}

// ---- Opening ------------------------------------------------------------------------
void EditorPage::openFile(const QString& path)
{
    if (!confirmDiscard()) return;
    load(path, QFileInfo(path).completeBaseName(), 0);
}

void EditorPage::openPad(int padKey, const QString& path, const QString& name)
{
    if (padKey == padKey_ && audio_) return;   // already open
    if (!confirmDiscard()) return;
    load(path, name, padKey);
}

void EditorPage::load(const QString& path, const QString& title, int padKey)
{
    player_.Stop();
    busy_ = true;
    showStatus(QStringLiteral("Loading %1\u2026").arg(title));
    updateUi();

    const std::wstring wpath = path.toStdWString();
    QThreadPool::globalInstance()->start([this, wpath, path, title, padKey] {
        ComScope com;
        auto samples = std::make_shared<ed::Samples>();
        std::string err;
        const bool ok = DecodeStereo48k(wpath, 10 * 60, *samples, err);
        QMetaObject::invokeMethod(this, [this, ok, samples, err, path, title, padKey] {
            busy_ = false;
            if (!ok)
            {
                showStatus(QStringLiteral("Couldn't open %1: %2").arg(title, QString::fromStdString(err)), true);
                updateUi();
                return;
            }
            title_      = title;
            sourcePath_ = path;
            padKey_     = padKey;
            ed::Buffer buf = samples;
            history_.Reset(buf);
            savedAudio_ = buf;
            view_->setSelection({});
            view_->setCursor(0);
            setAudio(buf, false);
            showStatus(padKey ? QStringLiteral("Save to pad replaces this pad's sound.")
                              : QStringLiteral("The original file stays untouched. Use Export."));
        }, Qt::QueuedConnection);
    });
}

void EditorPage::setAudio(ed::Buffer audio, bool keepView)
{
    audio_ = std::move(audio);
    auto peaks = std::make_shared<ed::Peaks>(ed::BuildPeaks(*audio_));
    view_->setAudio(audio_, peaks, keepView);
    updateUi();
}

bool EditorPage::confirmDiscard()
{
    if (!audio_ || audio_ == savedAudio_) return true;

    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("Unsaved changes"));
    box.setText(QStringLiteral("You have unsaved edits to \u201C%1\u201D.").arg(title_));
    auto* keep = padKey_ ? box.addButton(QStringLiteral("Save to pad"), QMessageBox::AcceptRole)
                         : box.addButton(QStringLiteral("Export\u2026"), QMessageBox::AcceptRole);
    box.addButton(QStringLiteral("Discard edits"), QMessageBox::DestructiveRole);
    auto* cancel = box.addButton(QMessageBox::Cancel);
    box.exec();

    if (box.clickedButton() == cancel) return false;
    if (box.clickedButton() == keep)
    {
        if (padKey_) saveToPad(); else exportFile();
        return audio_ == savedAudio_;   // false if export was cancelled
    }
    return true;
}

// ---- Editing -------------------------------------------------------------------------
EditorPage::Range EditorPage::effectRange(bool fadeIn, bool fadeOut) const
{
    const Range sel = view_->selection();
    if (!sel.empty()) return sel;
    const size_t n = view_->totalFrames();
    const size_t half = std::min<size_t>(n, 24000);   // 0.5 s
    if (fadeIn)  return { 0, half };
    if (fadeOut) return { n - half, n };
    return { 0, n };
}

void EditorPage::applyEdit(const QString& what, const std::function<Samples(const Samples&)>& op,
                           Range newSelection, size_t newCursor, bool fit)
{
    if (!audio_ || busy_) return;
    player_.Stop();

    auto next = std::make_shared<Samples>(op(*audio_));
    if (next->empty())
    {
        showStatus(QStringLiteral("That would leave no audio, so it wasn't applied."), true);
        return;
    }
    history_.Push(next);
    view_->setSelection(newSelection);
    view_->setCursor(newCursor);
    setAudio(next, !fit);
    showStatus(what + QStringLiteral(" applied."));
}

void EditorPage::cut()
{
    const Range r = view_->selection();
    if (!audio_ || r.empty()) return;
    clipboard_ = ed::Extract(*audio_, r);
    applyEdit(QStringLiteral("Cut"), [r](const Samples& s) { return ed::Remove(s, r); }, {}, r.a);
}

void EditorPage::copy()
{
    const Range r = view_->selection();
    if (!audio_ || r.empty()) return;
    clipboard_ = ed::Extract(*audio_, r);
    showStatus(QStringLiteral("Copied %1.").arg(FramesToTime(r.length())));
    updateUi();
}

void EditorPage::paste()
{
    if (!audio_ || clipboard_.empty()) return;
    const Range r = view_->selection();
    const size_t at = r.empty() ? view_->cursor() : r.a;
    const size_t len = ed::Frames(clipboard_);
    const Samples clip = clipboard_;
    applyEdit(QStringLiteral("Paste"),
              [r, at, clip](const Samples& s) { return r.empty() ? ed::Insert(s, at, clip) : ed::Replace(s, r, clip); },
              { at, at + len }, at);
}

void EditorPage::deleteSelection()
{
    const Range r = view_->selection();
    if (!audio_ || r.empty()) return;
    applyEdit(QStringLiteral("Delete"), [r](const Samples& s) { return ed::Remove(s, r); }, {}, r.a);
}

void EditorPage::keepSelection()
{
    const Range r = view_->selection();
    if (!audio_ || r.empty()) return;
    applyEdit(QStringLiteral("Crop"), [r](const Samples& s) { return ed::Keep(s, r); }, {}, 0, true);
}

void EditorPage::undo()
{
    if (!history_.CanUndo()) return;
    player_.Stop();
    setAudio(history_.Undo(), true);
    showStatus(QStringLiteral("Undone."));
}

void EditorPage::redo()
{
    if (!history_.CanRedo()) return;
    player_.Stop();
    setAudio(history_.Redo(), true);
    showStatus(QStringLiteral("Redone."));
}

// ---- Playback ------------------------------------------------------------------------
void EditorPage::playPause()
{
    if (!audio_) return;
    if (player_.IsPlaying())
    {
        const size_t pos = player_.Position();
        player_.Stop();
        if (view_->selection().empty()) view_->setCursor(pos);   // pause keeps your place
        return;
    }

    const Range sel = view_->selection();
    size_t start = sel.empty() ? view_->cursor() : sel.a;
    const size_t end = sel.empty() ? view_->totalFrames() : sel.b;
    if (start >= end) start = 0;   // at the end: play from the top

    std::string err;
    if (!player_.Play(audio_, start, end, loopBtn_->isChecked(), err))
        showStatus(QString::fromStdString(err), true);
}

void EditorPage::stopPlayback()
{
    player_.Stop();
    view_->setPlayhead(-1);
}

void EditorPage::tick()
{
    const bool playing = player_.IsPlaying();
    if (playing)
    {
        const size_t pos = player_.Position();
        view_->setPlayhead(static_cast<long long>(pos));
        view_->follow(pos);
    }
    else if (wasPlaying_)
    {
        view_->setPlayhead(-1);
    }
    if (playing != wasPlaying_)
    {
        playBtn_->setText(playing ? QStringLiteral("Pause") : QStringLiteral("Play"));
        wasPlaying_ = playing;
    }
}

// ---- Saving ----------------------------------------------------------------------------
void EditorPage::saveOrExport()
{
    if (!audio_) return;
    if (padKey_) saveToPad(); else exportFile();
}

static QString UniqueSoundPath(const QString& name, const QString& keepIfSame)
{
    QString base = name;
    base.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|])")), QStringLiteral("_"));
    const QString dir = MixCastSoundsDir();
    QString path = QStringLiteral("%1/%2.wav").arg(dir, base);
    for (int n = 2; QFileInfo::exists(path) && QFileInfo(path) != QFileInfo(keepIfSame); n++)
        path = QStringLiteral("%1/%2 (%3).wav").arg(dir, base).arg(n);
    return path;
}

void EditorPage::saveToPad()
{
    if (!audio_ || busy_) return;
    player_.Stop();

    // Lossless WAV in %LOCALAPPDATA%\MixCast\Sounds. If this pad already uses
    // a WAV we made, overwrite it in place.
    const QString current = sb_->padPath(padKey_);
    const bool reuse = !current.isEmpty() && IsStoredSound(current) && current.endsWith(QStringLiteral(".wav"), Qt::CaseInsensitive);
    const QString path = reuse ? current : UniqueSoundPath(title_, current);

    std::string err;
    if (!WriteAudioFile(path.toStdWString(), *audio_, ExportFormat::Wav, 0, err))
    {
        showStatus(QStringLiteral("Couldn't save: %1").arg(QString::fromStdString(err)), true);
        return;
    }

    if (!sb_->replaceSound(padKey_, path))
    {
        // The pad was removed while you were editing: bring it back as a new one.
        padKey_ = sb_->addStoredSound(path, title_);
        showStatus(QStringLiteral("The pad was gone, so this was added as a new pad."));
    }
    else
    {
        showStatus(QStringLiteral("Saved. The pad now plays your edit."));
    }
    sourcePath_ = path;
    savedAudio_ = audio_;
    updateUi();
}

void EditorPage::addToSoundboard()
{
    if (!audio_ || busy_) return;
    const QString path = UniqueSoundPath(title_, QString());
    std::string err;
    if (!WriteAudioFile(path.toStdWString(), *audio_, ExportFormat::Wav, 0, err))
    {
        showStatus(QStringLiteral("Couldn't add: %1").arg(QString::fromStdString(err)), true);
        return;
    }
    padKey_ = sb_->addStoredSound(path, title_);
    sourcePath_ = path;
    savedAudio_ = audio_;
    showStatus(QStringLiteral("Added to the soundboard. From now on, Save to pad updates it."));
    updateUi();
}

void EditorPage::exportFile()
{
    if (!audio_ || busy_) return;
    player_.Stop();

    QSettings settings;
    const QString lastDir = settings.value(QStringLiteral("editor/exportDir"),
                                           QStandardPaths::writableLocation(QStandardPaths::MusicLocation)).toString();
    const QString lastExt = settings.value(QStringLiteral("editor/exportExt"), QStringLiteral("mp3")).toString();

    QString selectedFilter = lastExt == QStringLiteral("wav") ? QStringLiteral("WAV, lossless (*.wav)")
                           : lastExt == QStringLiteral("m4a") ? QStringLiteral("M4A / AAC, 192 kbps (*.m4a)")
                                                              : QStringLiteral("MP3, 192 kbps (*.mp3)");
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export"), QStringLiteral("%1/%2.%3").arg(lastDir, title_, lastExt),
        QStringLiteral("MP3, 192 kbps (*.mp3);;WAV, lossless (*.wav);;M4A / AAC, 192 kbps (*.m4a)"), &selectedFilter);
    if (path.isEmpty()) return;

    // Never overwrite the file this sound was opened from.
    if (!padKey_ && QFileInfo(path) == QFileInfo(sourcePath_))
    {
        showStatus(QStringLiteral("Pick a different name: exporting never overwrites the original file."), true);
        return;
    }

    settings.setValue(QStringLiteral("editor/exportDir"), QFileInfo(path).absolutePath());
    settings.setValue(QStringLiteral("editor/exportExt"), QFileInfo(path).suffix().toLower());

    busy_ = true;
    showStatus(QStringLiteral("Exporting\u2026"));
    updateUi();

    const ed::Buffer audio = audio_;
    const std::wstring wpath = path.toStdWString();
    QThreadPool::globalInstance()->start([this, audio, wpath, path] {
        ComScope com;
        std::string err;
        const bool ok = WriteAudioFile(wpath, *audio, FormatFromPath(wpath), 192, err);
        QMetaObject::invokeMethod(this, [this, ok, err, path, audio] {
            busy_ = false;
            if (ok)
            {
                if (!padKey_) savedAudio_ = audio;   // a file's edits count as saved once exported
                showStatus(QStringLiteral("Exported to %1").arg(QDir::toNativeSeparators(path)));
            }
            else showStatus(QStringLiteral("Export failed: %1").arg(QString::fromStdString(err)), true);
            updateUi();
        }, Qt::QueuedConnection);
    });
}

// ---- UI state ----------------------------------------------------------------------------
void EditorPage::showStatus(const QString& text, bool warn)
{
    statusLbl_->setText(text);
    statusLbl_->setToolTip(text);
    statusLbl_->setObjectName(warn ? QStringLiteral("Warning") : QStringLiteral("Muted"));
    statusLbl_->style()->unpolish(statusLbl_);
    statusLbl_->style()->polish(statusLbl_);
}

void EditorPage::updateUi()
{
    const bool has = static_cast<bool>(audio_);
    editArea_->setVisible(has || busy_);
    empty_->setVisible(!has && !busy_);

    const bool dirty = has && audio_ != savedAudio_;
    titleLbl_->setText(has ? title_ + (dirty ? QStringLiteral("  (edited)") : QString()) : QString());
    sourceLbl_->setVisible(has);
    sourceLbl_->setText(padKey_ ? QStringLiteral("Soundboard pad") : QStringLiteral("File"));

    // Pad: Save to pad is the main action. File: Export is.
    saveBtn_->setVisible(has && padKey_ != 0);
    addPadBtn_->setVisible(has && padKey_ == 0);
    exportBtn_->setObjectName(padKey_ ? QString() : QStringLiteral("Primary"));
    exportBtn_->style()->unpolish(exportBtn_);
    exportBtn_->style()->polish(exportBtn_);

    for (auto* w : needsAudio_) w->setEnabled(has && !busy_);
    const bool sel = has && !view_->selection().empty();
    for (auto* b : needsSelection_) b->setEnabled(sel && !busy_);
    pasteBtn_->setEnabled(has && !clipboard_.empty() && !busy_);
    undoBtn_->setEnabled(history_.CanUndo() && !busy_);
    redoBtn_->setEnabled(history_.CanRedo() && !busy_);

    if (has)
    {
        lengthLbl_->setText(QStringLiteral("Length %1").arg(FramesToTime(view_->totalFrames())));
        const Range r = view_->selection();
        selLbl_->setText(r.empty() ? QStringLiteral("Cursor %1").arg(FramesToTime(view_->cursor()))
                                   : QStringLiteral("Selected %1 to %2 (%3)")
                                         .arg(FramesToTime(r.a), FramesToTime(r.b), FramesToTime(r.length())));
    }
}

// ---- Drag & drop ---------------------------------------------------------------------------
void EditorPage::dragEnterEvent(QDragEnterEvent* e)
{
    if (!e->mimeData()->hasUrls()) return;
    const QUrl u = e->mimeData()->urls().value(0);
    if (u.isLocalFile() && kOpenable.contains(QFileInfo(u.toLocalFile()).suffix().toLower()))
        e->acceptProposedAction();
}

void EditorPage::dropEvent(QDropEvent* e)
{
    const QUrl u = e->mimeData()->urls().value(0);
    if (u.isLocalFile()) openFile(u.toLocalFile());
    e->acceptProposedAction();
}
