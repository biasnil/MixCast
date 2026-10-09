// MixCast GUI - custom painted controls.
#include "widgets.h"
#include "theme.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDrag>
#include <QMimeData>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>

#include <algorithm>
#include <cmath>

// ============================================================================
// LevelMeter
// ============================================================================
LevelMeter::LevelMeter(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    clock_.start();
}

void LevelMeter::setLevel(float linearPeak)
{
    const qint64 now = clock_.elapsed();
    const float dt = std::min(0.2f, (now - lastMs_) / 1000.0f);
    lastMs_ = now;

    const float db = linearPeak > 1e-6f ? std::max(-60.0f, 20.0f * std::log10(linearPeak)) : -60.0f;

    // Instant rise, 24 dB/s fall.
    displayDb_ = (db > displayDb_) ? db : std::max(db, displayDb_ - 24.0f * dt);

    // Peak hold for 1.2 s, then fall.
    if (db >= peakDb_) { peakDb_ = db; peakAtMs_ = now; }
    else if (now - peakAtMs_ > 1200) peakDb_ = std::max(displayDb_, peakDb_ - 30.0f * dt);

    update();
}

void LevelMeter::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Dark well.
    const QRectF well = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(QColor(0x0F, 0x15, 0x1A), 1.0));
    p.setBrush(theme::Slot);
    p.drawRoundedRect(well, 4, 4);

    // Fine LED ladder: green, yellow in the top 12 dB, coral in the top 3.
    const qreal pad = 3.0, segH = 3.0, gap = 1.5;
    const int   n = std::max(1, static_cast<int>((well.height() - 2 * pad + gap) / (segH + gap)));
    const qreal step = 60.0 / n;
    const int   peakIdx = static_cast<int>((peakDb_ + 60.0f) / step) - 1;

    p.setPen(Qt::NoPen);
    for (int i = 0; i < n; i++)
    {
        const qreal lower = -60.0 + i * step;
        const qreal upper = lower + step;
        const bool lit = displayDb_ > lower + 0.01 || (i == peakIdx && peakDb_ > -59.0f);
        const QColor on = upper > -3.0 ? theme::Tally : (upper > -12.0 ? theme::Warm : theme::Accent);

        QColor c = on;
        if (!lit)
        {
            // Unlit LEDs keep a trace of their colour.
            c = theme::LedOff;
            c.setRed((c.red() * 82 + on.red() * 18) / 100);
            c.setGreen((c.green() * 82 + on.green() * 18) / 100);
            c.setBlue((c.blue() * 82 + on.blue() * 18) / 100);
        }
        const qreal y = well.bottom() - pad - (i + 1) * segH - i * gap;
        p.setBrush(c);
        p.drawRect(QRectF(well.left() + pad, y, well.width() - 2 * pad, segH));
    }
}

// ============================================================================
// Fader
// ============================================================================
static constexpr double kKnob  = 30.0;   // round knob diameter
static constexpr double kTrack = 22.0;   // pill track width

Fader::Fader(QWidget* parent) : QAbstractSlider(parent)
{
    setOrientation(Qt::Vertical);
    setRange(-600, 120);
    setSingleStep(10);   // 1 dB (arrow keys, wheel)
    setPageStep(60);     // 6 dB (Page Up/Down)
    setValue(0);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::SizeVerCursor);
    setToolTip(QStringLiteral("Drag, scroll or use arrow keys. Double-click for 0 dB."));
}

void Fader::setDimmed(bool dimmed)
{
    if (dimmed == dimmed_) return;
    dimmed_ = dimmed;
    update();
}

double Fader::PosFromDb(double db)
{
    if (db >= 0.0) return 0.75 + 0.25 * std::min(db, 12.0) / 12.0;
    return 0.75 * std::clamp((db + 60.0) / 60.0, 0.0, 1.0);
}

double Fader::DbFromPos(double pos)
{
    pos = std::clamp(pos, 0.0, 1.0);
    if (pos >= 0.75) return (pos - 0.75) / 0.25 * 12.0;
    return pos / 0.75 * 60.0 - 60.0;
}

double Fader::travelTop() const    { return kKnob / 2 + 2; }
double Fader::travelBottom() const { return height() - kKnob / 2 - 4; }

double Fader::yFromValue(int v) const
{
    return travelBottom() - PosFromDb(v / 10.0) * (travelBottom() - travelTop());
}

int Fader::valueFromY(double y) const
{
    const double pos = (travelBottom() - y) / std::max(1.0, travelBottom() - travelTop());
    double db = DbFromPos(pos);
    if (std::fabs(db) < 0.5) db = 0.0;   // gentle detent at 0 dB
    return static_cast<int>(std::lround(db * 10.0));
}

QRectF Fader::capRect() const
{
    const double cx = width() / 2.0;
    const double y  = yFromValue(value());
    return QRectF(cx - kKnob / 2, y - kKnob / 2, kKnob, kKnob);
}

void Fader::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const double cx = width() / 2.0;
    const QRectF track(cx - kTrack / 2, travelTop() - kTrack / 2, kTrack, travelBottom() - travelTop() + kTrack);
    const QRectF knob = capRect();
    const QColor fill = dimmed_ ? theme::AccentDim : theme::Accent;

    // Pill track, filled up to the knob.
    p.setPen(QPen(QColor(0x0F, 0x15, 0x1A), 1.0));
    p.setBrush(theme::Slot);
    p.drawRoundedRect(track, kTrack / 2, kTrack / 2);
    {
        QPainterPath pill;
        pill.addRoundedRect(track.adjusted(1, 1, -1, -1), kTrack / 2 - 1, kTrack / 2 - 1);
        p.save();
        p.setClipPath(pill);
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawRect(QRectF(track.left(), knob.center().y(), track.width(), track.bottom() - knob.center().y()));

        // "GAIN" printed up the track, dark on the fill.
        p.translate(cx, track.bottom() - 10);
        p.rotate(-90);
        p.setFont(theme::Font(7.5, QFont::Bold));
        p.setPen(QColor(0x10, 0x30, 0x22, dimmed_ ? 120 : 170));
        p.drawText(QRectF(0, -kTrack / 2, 80, kTrack), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("GAIN"));
        p.restore();
    }

    // 0 dB mark on the left of the track.
    const double y0 = yFromValue(0);
    p.setPen(QPen(theme::Muted, 1.5, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(track.left() - 7, y0), QPointF(track.left() - 3, y0));

    // Round knob: soft shadow, light rim, slate face, centre dot.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 90));
    p.drawEllipse(knob.translated(0, 2));
    QRadialGradient face(knob.center() + QPointF(-4, -5), kKnob * 0.75);
    face.setColorAt(0.0, QColor(0x5B, 0x70, 0x7E));
    face.setColorAt(1.0, QColor(0x33, 0x44, 0x50));
    p.setBrush(face);
    const bool active = hasFocus() || dragging_;
    p.setPen(QPen(active ? theme::AccentHot : QColor(0xC8, 0xD8, 0xDF), 2.0));
    p.drawEllipse(knob.adjusted(1, 1, -1, -1));
    p.setPen(Qt::NoPen);
    p.setBrush(dimmed_ ? theme::Muted : theme::Legend);
    p.drawEllipse(knob.center(), 3.0, 3.0);
}

void Fader::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) return QAbstractSlider::mousePressEvent(e);
    const QPointF pos = e->position();
    const QRectF cap = capRect();
    dragging_ = true;
    if (cap.adjusted(-4, -4, 4, 4).contains(pos))
        dragOffset_ = pos.y() - cap.center().y();
    else
    {
        dragOffset_ = 0.0;
        setValue(valueFromY(pos.y()));
    }
    setSliderDown(true);
    update();
}

void Fader::mouseMoveEvent(QMouseEvent* e)
{
    if (!dragging_) return;
    setValue(valueFromY(e->position().y() - dragOffset_));
}

void Fader::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) return;
    dragging_ = false;
    setSliderDown(false);
    update();
}

void Fader::mouseDoubleClickEvent(QMouseEvent*)
{
    setValue(0);
}

// ============================================================================
// TallyLamp
// ============================================================================
TallyLamp::TallyLamp(QWidget* parent) : QWidget(parent)
{
    setFixedSize(14, 14);
}

void TallyLamp::setLit(bool lit)
{
    if (lit == lit_) return;
    lit_ = lit;
    update();
}

void TallyLamp::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPointF c = QRectF(rect()).center();

    if (lit_)
    {
        QRadialGradient glow(c, 7);
        glow.setColorAt(0.0, QColor(0xFF, 0xD0, 0xD1));
        glow.setColorAt(0.35, QColor(0xFF, 0x6B, 0x70));
        glow.setColorAt(0.65, theme::Tally);
        glow.setColorAt(1.0, QColor(0xE5, 0x48, 0x4D, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(glow);
        p.drawEllipse(c, 7, 7);
    }
    else
    {
        QRadialGradient lens(c + QPointF(-1, -1), 5);
        lens.setColorAt(0.0, QColor(0x4A, 0x30, 0x34));
        lens.setColorAt(1.0, QColor(0x22, 0x1E, 0x22));
        p.setPen(QPen(QColor(0x0F, 0x15, 0x1A), 1.0));
        p.setBrush(lens);
        p.drawEllipse(c, 4.5, 4.5);
    }
}

// ============================================================================
// Knob
// ============================================================================
Knob::Knob(const QString& label, QWidget* parent) : QAbstractSlider(parent), label_(label)
{
    setRange(-120, 120);
    setSingleStep(5);    // 0.5 dB per wheel step
    setPageStep(30);
    setValue(0);
    setFixedSize(sizeHint());
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::SizeVerCursor);
    connect(this, &QAbstractSlider::valueChanged, this, [this](int v) {
        setToolTip(QStringLiteral("%1: %2%3 dB. Drag up/down or scroll; double-click for 0.")
                       .arg(label_, v > 0 ? QStringLiteral("+") : QString(), QString::number(v / 10.0, 'f', 1)));
    });
    emit valueChanged(0);
}

void Knob::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF ring(5, 1, 24, 24);
    const QPointF c = ring.center();

    // Body.
    QRadialGradient face(c + QPointF(-3, -4), 16);
    face.setColorAt(0.0, QColor(0x4A, 0x5C, 0x69));
    face.setColorAt(1.0, QColor(0x2A, 0x38, 0x43));
    p.setPen(QPen(QColor(0x0F, 0x15, 0x1A), 1.0));
    p.setBrush(face);
    p.drawEllipse(ring.adjusted(3, 3, -3, -3));

    // Track (270 degrees, gap at the bottom) and the lit arc from 0 to the value.
    const int start = 225 * 16, span = -270 * 16;
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(theme::Slot, 3.0, Qt::SolidLine, Qt::FlatCap));
    p.drawArc(ring, start, span);
    const double t = value() / 120.0;   // -1 .. +1
    if (value() != 0)
    {
        QColor arc = value() > 0 ? theme::Accent : theme::Tally;
        if (!isEnabled()) arc = theme::Muted;
        p.setPen(QPen(arc, 3.0, Qt::SolidLine, Qt::FlatCap));
        p.drawArc(ring, 90 * 16, static_cast<int>(-t * 135 * 16));
    }

    // Pointer.
    const double ang = (90.0 - t * 135.0) * 3.14159265358979 / 180.0;
    p.setPen(QPen(hasFocus() || dragging_ ? theme::AccentHot : theme::Legend, 2.0, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(c + QPointF(std::cos(ang) * 3, -std::sin(ang) * 3), c + QPointF(std::cos(ang) * 8, -std::sin(ang) * 8));

    // Label under it.
    p.setFont(theme::Font(6.5, QFont::Bold));
    p.setPen(value() != 0 ? theme::Legend : theme::Muted);
    p.drawText(QRectF(0, 28, width(), 14), Qt::AlignCenter, label_);
}

void Knob::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) return QAbstractSlider::mousePressEvent(e);
    dragging_ = true;
    dragStartY_ = static_cast<int>(e->position().y());
    dragStartValue_ = value();
    setSliderDown(true);
    update();
}

void Knob::mouseMoveEvent(QMouseEvent* e)
{
    if (!dragging_) return;
    const int dy = dragStartY_ - static_cast<int>(e->position().y());
    setValue(dragStartValue_ + dy * 2);   // 100 px of travel covers the range
}

void Knob::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) return;
    dragging_ = false;
    setSliderDown(false);
    update();
}

void Knob::mouseDoubleClickEvent(QMouseEvent*)
{
    setValue(0);
}

// ============================================================================
// PanBar
// ============================================================================
PanBar::PanBar(QWidget* parent) : QAbstractSlider(parent)
{
    setOrientation(Qt::Horizontal);
    setRange(-100, 100);
    setSingleStep(5);
    setPageStep(25);
    setValue(0);
    setFixedHeight(sizeHint().height());
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::SizeHorCursor);
    setToolTip(QStringLiteral("Balance: drag or scroll; double-click to centre.\nRight-click for stereo width."));
}

void PanBar::setWidthValue(float width)
{
    if (width == width_) return;
    width_ = width;
    update();
}

QRectF PanBar::track() const
{
    return QRectF(22, height() / 2.0 - 2.5, width() - 22 - 30, 5);
}

int PanBar::valueAt(double x) const
{
    const QRectF t = track();
    const double f = std::clamp((x - t.left()) / t.width(), 0.0, 1.0);
    int v = static_cast<int>(std::lround(f * 200.0 - 100.0));
    if (std::abs(v) < 4) v = 0;   // gentle detent in the middle
    return v;
}

void PanBar::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF t = track();

    p.setFont(theme::Font(6.5, QFont::Bold));
    p.setPen(theme::Muted);
    p.drawText(QRectF(0, 0, 20, height()), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("PAN"));

    p.setPen(Qt::NoPen);
    p.setBrush(theme::Slot);
    p.drawRoundedRect(t, 2.5, 2.5);
    const double cx = t.center().x();
    const double x = t.left() + (value() + 100) / 200.0 * t.width();
    if (value() != 0)
    {
        p.setBrush(theme::Accent);
        p.drawRoundedRect(QRectF(std::min(cx, x), t.top(), std::fabs(x - cx), t.height()), 2.5, 2.5);
    }
    p.setPen(QPen(theme::Muted, 1.0));
    p.drawLine(QPointF(cx, t.top() - 2), QPointF(cx, t.bottom() + 2));

    p.setPen(QPen(hasFocus() ? theme::AccentHot : QColor(0xC8, 0xD8, 0xDF), 1.5));
    p.setBrush(QColor(0x33, 0x44, 0x50));
    p.drawEllipse(QPointF(x, t.center().y()), 5, 5);

    // Value, and the width when it isn't normal.
    QString txt = value() == 0 ? QStringLiteral("C")
                : QStringLiteral("%1%2").arg(value() < 0 ? QStringLiteral("L") : QStringLiteral("R")).arg(std::abs(value()));
    if (width_ < 0.05f)       txt = QStringLiteral("MONO");
    else if (width_ > 1.05f)  txt += QStringLiteral(" W");
    p.setPen(value() != 0 || width_ != 1.0f ? theme::Legend : theme::Muted);
    p.drawText(QRectF(t.right() + 4, 0, width() - t.right() - 4, height()), Qt::AlignRight | Qt::AlignVCenter, txt);
}

void PanBar::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) return QAbstractSlider::mousePressEvent(e);
    setValue(valueAt(e->position().x()));
}

void PanBar::mouseMoveEvent(QMouseEvent* e)
{
    if (e->buttons() & Qt::LeftButton) setValue(valueAt(e->position().x()));
}

void PanBar::mouseDoubleClickEvent(QMouseEvent*)
{
    setValue(0);
}

void PanBar::contextMenuEvent(QContextMenuEvent* e)
{
    QMenu menu(this);
    auto* title = menu.addAction(QStringLiteral("Stereo width"));
    title->setEnabled(false);
    const struct { const char* name; float w; } widths[] = {
        { "Mono", 0.0f }, { "Narrow", 0.5f }, { "Normal", 1.0f }, { "Wide", 1.5f }, { "Extra wide", 2.0f } };
    for (const auto& w : widths)
    {
        auto* a = menu.addAction(QString::fromUtf8(w.name));
        a->setCheckable(true);
        a->setChecked(std::fabs(width_ - w.w) < 0.01f);
        connect(a, &QAction::triggered, this, [this, v = w.w] { setWidthValue(v); emit widthChosen(v); });
    }
    menu.exec(e->globalPos());
}

// ============================================================================
// CleanupScope
// ============================================================================
static constexpr float kScopeFloorDb = -100.0f;
static constexpr float kScopeTopDb   = -10.0f;

CleanupScope::CleanupScope(QWidget* parent) : QWidget(parent)
{
    std::fill(std::begin(in_), std::end(in_), kScopeFloorDb);
    std::fill(std::begin(out_), std::end(out_), kScopeFloorDb);
    clock_.start();
    setToolTip(QStringLiteral("What the clean-up is doing right now, low to high pitch.\n"
                              "Green: your voice, kept. Red: background and clicks being removed."));
}

void CleanupScope::setBands(const float* inDb, const float* outDb, float removedDb)
{
    const qint64 now = clock_.elapsed();
    const float fall = 40.0f * std::min(0.2f, (now - lastMs_) / 1000.0f);   // 40 dB/s, like a meter
    lastMs_ = now;
    for (int b = 0; b < kBands; b++)
    {
        const float i = std::clamp(inDb[b], kScopeFloorDb, kScopeTopDb);
        const float o = std::clamp(std::min(outDb[b], inDb[b]), kScopeFloorDb, kScopeTopDb);
        in_[b]  = (i > in_[b])  ? i : std::max(i, in_[b] - fall);
        out_[b] = (o > out_[b]) ? o : std::max(o, out_[b] - fall);
        out_[b] = std::min(out_[b], in_[b]);
    }
    removedDb_ = removedDb;
    update();
}

void CleanupScope::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF well = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(QColor(0x0E, 0x10, 0x13), 1.0));
    p.setBrush(theme::Slot);
    p.drawRoundedRect(well, 4, 4);
    p.setPen(QPen(QColor(0x35, 0x3C, 0x46), 1.0));
    p.drawLine(QPointF(well.left() + 4, well.bottom()), QPointF(well.right() - 4, well.bottom()));

    const QRectF area = well.adjusted(4, 4, -4, -3);
    const qreal slot = area.width() / kBands;
    auto yOf = [&](float db) {
        const qreal t = (db - kScopeFloorDb) / (kScopeTopDb - kScopeFloorDb);
        return area.bottom() - t * area.height();
    };

    QColor removed = theme::Tally;
    removed.setAlpha(150);
    p.setPen(Qt::NoPen);
    for (int b = 0; b < kBands; b++)
    {
        const qreal x = area.left() + b * slot + 0.5;
        const qreal w = slot - 1.5;
        const qreal yIn = yOf(in_[b]), yOut = yOf(out_[b]);
        if (yOut - yIn > 0.5)
        {
            p.setBrush(removed);
            p.drawRoundedRect(QRectF(x, yIn, w, yOut - yIn), 1, 1);
        }
        if (area.bottom() - yOut > 0.5)
        {
            QLinearGradient g(QPointF(0, yOut), QPointF(0, area.bottom()));
            g.setColorAt(0.0, theme::AccentHot);
            g.setColorAt(1.0, theme::Accent.darker(160));
            p.setBrush(g);
            p.drawRoundedRect(QRectF(x, yOut, w, area.bottom() - yOut), 1, 1);
        }
    }

    // How much is coming out overall, top right.
    if (removedDb_ < -0.5f)
    {
        p.setFont(theme::Font(6.5, QFont::DemiBold));
        p.setPen(QColor(0xFF, 0x8A, 0x8D));
        p.drawText(area.adjusted(0, -2, 0, 0), Qt::AlignRight | Qt::AlignTop,
                   QStringLiteral("\u2212%1 dB").arg(std::lround(-removedDb_)));
    }
}

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

void SoundPad::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) pressPos_ = e->position().toPoint();
    QAbstractButton::mousePressEvent(e);
}

// Dragging a pad a little way picks it up, to drop on another pad or a page.
void SoundPad::mouseMoveEvent(QMouseEvent* e)
{
    if (dragId_ < 0 || !(e->buttons() & Qt::LeftButton)
        || (e->position().toPoint() - pressPos_).manhattanLength() < QApplication::startDragDistance())
        return QAbstractButton::mouseMoveEvent(e);

    setDown(false);   // no click when the drag ends
    auto* mime = new QMimeData;
    mime->setData(QString::fromLatin1(kMime), QByteArray::number(dragId_));
    auto* drag = new QDrag(this);
    drag->setMimeData(mime);
    const QPixmap pm = grab();
    drag->setPixmap(pm);
    drag->setHotSpot(pressPos_);
    drag->exec(Qt::MoveAction);
}

void SoundPad::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const bool playing = progress_ >= 0.0f;
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -2.5);   // room for the shadow

    QPainterPath shape;
    shape.addRoundedRect(r, 9, 9);

    // Shadow under the pad (it sinks in while pressed).
    if (!isDown())
    {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 80));
        p.drawRoundedRect(r.translated(0, 2), 9, 9);
    }
    const QPointF press = isDown() ? QPointF(0, 1.5) : QPointF(0, 0);
    p.translate(press);

    // Body: lit from above.
    QLinearGradient body(r.topLeft(), r.bottomLeft());
    const int lift = underMouse() ? 18 : 0;
    QColor top = theme::PanelHi.lighter(100 + lift), bottom = theme::Panel.lighter(100 + lift);
    if (colour_.isValid())   // a light wash of the pad's colour
    {
        auto tint = [this](QColor c, int pct) {
            return QColor((c.red() * (100 - pct) + colour_.red() * pct) / 100,
                          (c.green() * (100 - pct) + colour_.green() * pct) / 100,
                          (c.blue() * (100 - pct) + colour_.blue() * pct) / 100);
        };
        top = tint(top, 16);
        bottom = tint(bottom, 10);
    }
    body.setColorAt(0.0, top);
    body.setColorAt(1.0, bottom);
    p.fillPath(shape, body);
    if (colour_.isValid())
    {
        p.save();
        p.setClipPath(shape);
        p.fillRect(QRectF(r.left(), r.top(), 4, r.height()), colour_);
        p.restore();
    }

    // Progress: an amber wash that fills left to right while the sound plays.
    if (playing)
    {
        p.save();
        p.setClipPath(shape);
        QLinearGradient wash(r.topLeft(), r.topRight());
        QColor a = theme::Accent, b = theme::Accent;
        a.setAlpha(20);
        b.setAlpha(70);
        wash.setColorAt(0.0, a);
        wash.setColorAt(1.0, b);
        const QRectF done(r.left(), r.top(), r.width() * progress_, r.height());
        p.fillRect(done, wash);
        p.fillRect(QRectF(r.left(), r.bottom() - 3, r.width() * progress_, 3), theme::Accent);
        p.restore();
    }

    // Edge: lit top, amber glow while playing, highlight with focus.
    QColor edge = playing ? theme::Accent : theme::PanelEdge;
    if (hasFocus() && !playing) edge = theme::AccentHot;
    if (playing)
    {
        QColor glow = theme::Accent;
        glow.setAlpha(60);
        p.setPen(QPen(glow, 4.0));
        p.setBrush(Qt::NoBrush);
        p.drawPath(shape);
    }
    p.setPen(QPen(edge, playing ? 1.5 : 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawPath(shape);
    if (!playing)
    {
        p.setPen(QPen(QColor(255, 255, 255, 22), 1.0));
        p.drawLine(QPointF(r.left() + 8, r.top() + 1.5), QPointF(r.right() - 8, r.top() + 1.5));
    }

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
    const qreal baseY = height() - 30;
    if (!hotkey_.isEmpty())
    {
        p.setFont(theme::Font(8.5));
        const QFontMetrics fm(p.font());
        const qreal w = std::min<qreal>(fm.horizontalAdvance(hotkey_) + 14, width() - 24);
        // Drawn as a keycap: a darker base under a lighter top.
        const QRectF chip(12, baseY - 2, w, 18);
        p.setPen(Qt::NoPen);
        p.setBrush(theme::Slot);
        p.drawRoundedRect(chip.translated(0, 2), 4, 4);
        QLinearGradient cap(chip.topLeft(), chip.bottomLeft());
        cap.setColorAt(0.0, QColor(0x4A, 0x5E, 0x6C));
        cap.setColorAt(1.0, QColor(0x3A, 0x4C, 0x58));
        p.setBrush(cap);
        p.drawRoundedRect(chip, 4, 4);
        p.setPen(theme::Legend);
        p.drawText(chip, Qt::AlignCenter, fm.elidedText(hotkey_, Qt::ElideRight, static_cast<int>(w - 10)));
    }

    // State, bottom right.
    QString right;
    QColor  rightColor = theme::Muted;
    if (state_ == State::Loading)      right = QStringLiteral("Loading\u2026");
    else if (state_ == State::Failed) { right = QStringLiteral("Can't open"); rightColor = theme::Tally; }
    else if (playing)                 { right = QStringLiteral("Playing"); rightColor = theme::AccentHot; }
    else                               right = detail_;   // duration
    if (looping_ && state_ == State::Ready) right = QStringLiteral("\u21BB ") + right;   // loops
    if (!right.isEmpty())
    {
        p.setFont(theme::Font(8.5));
        p.setPen(rightColor);
        p.drawText(QRectF(12, baseY, width() - 24, 18), Qt::AlignRight | Qt::AlignVCenter, right);
    }
}
