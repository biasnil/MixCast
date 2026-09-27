// MixCast GUI - custom painted controls.
#include "widgets.h"
#include "theme.h"

#include <QMouseEvent>
#include <QPainter>
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

    const QRectF well = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(Qt::NoPen);
    p.setBrush(theme::Slot);
    p.drawRoundedRect(well, 3, 3);

    const qreal pad = 2.0, segH = 4.0, gap = 2.0;
    const int   n = std::max(1, static_cast<int>((well.height() - 2 * pad + gap) / (segH + gap)));
    const qreal step = 60.0 / n;
    const int   peakIdx = static_cast<int>((peakDb_ + 60.0f) / step) - 1;

    for (int i = 0; i < n; i++)
    {
        const qreal lower = -60.0 + i * step;
        const qreal upper = lower + step;
        const bool lit = displayDb_ > lower + 0.01 || (i == peakIdx && peakDb_ > -59.0f);

        QColor c = theme::LedOff;
        if (lit) c = upper > -3.0 ? theme::Tally : (upper > -12.0 ? theme::AmberHot : theme::Amber);

        const qreal y = well.bottom() - pad - (i + 1) * segH - i * gap;
        p.setBrush(c);
        p.drawRoundedRect(QRectF(well.left() + pad, y, well.width() - 2 * pad, segH), 1, 1);
    }
}

// ============================================================================
// Fader
// ============================================================================
static constexpr double kCapW = 30.0;
static constexpr double kCapH = 22.0;

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

double Fader::travelTop() const    { return kCapH / 2 + 2; }
double Fader::travelBottom() const { return height() - kCapH / 2 - 2; }

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
    const double cx = width() / 2.0 + 4;   // leave room for ticks on the left
    const double y  = yFromValue(value());
    return QRectF(cx - kCapW / 2, y - kCapH / 2, kCapW, kCapH);
}

void Fader::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const double cx = width() / 2.0 + 4;

    // Slot.
    p.setPen(Qt::NoPen);
    p.setBrush(theme::Slot);
    p.drawRoundedRect(QRectF(cx - 2, travelTop() - 4, 4, travelBottom() - travelTop() + 8), 2, 2);

    // Scale ticks; 0 dB is marked in amber.
    const double marks[] = { 12, 6, 0, -10, -20, -30, -40, -50, -60 };
    for (double db : marks)
    {
        const double y = yFromValue(static_cast<int>(db * 10));
        const bool zero = (db == 0.0);
        p.setPen(QPen(zero ? theme::Amber : theme::PanelEdge, zero ? 2.0 : 1.0));
        p.drawLine(QPointF(cx - (zero ? 20 : 17), y), QPointF(cx - 9, y));
    }

    // Cap.
    const QRectF cap = capRect();
    QLinearGradient g(cap.topLeft(), cap.bottomLeft());
    g.setColorAt(0.0, QColor(0x5A, 0x63, 0x6F));
    g.setColorAt(0.5, QColor(0x46, 0x4E, 0x59));
    g.setColorAt(1.0, QColor(0x33, 0x3A, 0x43));
    p.setBrush(g);
    p.setPen(QPen(theme::Slot, 1.0));
    p.drawRoundedRect(cap, 3, 3);

    // Grip lines + centre line.
    p.setPen(QPen(QColor(0x2A, 0x30, 0x38), 1.0));
    p.drawLine(QPointF(cap.left() + 5, cap.top() + 5),    QPointF(cap.right() - 5, cap.top() + 5));
    p.drawLine(QPointF(cap.left() + 5, cap.bottom() - 5), QPointF(cap.right() - 5, cap.bottom() - 5));
    const bool active = hasFocus() || dragging_;
    p.setPen(QPen(active ? theme::Amber : theme::Legend, 2.0));
    p.drawLine(QPointF(cap.left() + 3, cap.center().y()), QPointF(cap.right() - 3, cap.center().y()));
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
        glow.setColorAt(0.0, QColor(0xFF, 0x8A, 0x8D));
        glow.setColorAt(0.55, theme::Tally);
        glow.setColorAt(1.0, QColor(0xE5, 0x48, 0x4D, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(glow);
        p.drawEllipse(c, 7, 7);
    }
    else
    {
        p.setPen(QPen(theme::PanelEdge, 1.0));
        p.setBrush(theme::LedOff);
        p.drawEllipse(c, 4.5, 4.5);
    }
}
