// MixCast GUI - custom painted controls.
#include "widgets.h"
#include "theme.h"

#include <QContextMenuEvent>
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

    // Recessed well: dark body, shadow at the top, a lit lip at the bottom.
    const QRectF well = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(QColor(0x0E, 0x10, 0x13), 1.0));
    p.setBrush(theme::Slot);
    p.drawRoundedRect(well, 3, 3);
    p.setPen(QPen(QColor(0x35, 0x3C, 0x46), 1.0));
    p.drawLine(QPointF(well.left() + 3, well.bottom()), QPointF(well.right() - 3, well.bottom()));

    const qreal pad = 2.0, segH = 4.0, gap = 2.0;
    const int   n = std::max(1, static_cast<int>((well.height() - 2 * pad + gap) / (segH + gap)));
    const qreal step = 60.0 / n;
    const int   peakIdx = static_cast<int>((peakDb_ + 60.0f) / step) - 1;

    p.setPen(Qt::NoPen);
    for (int i = 0; i < n; i++)
    {
        const qreal lower = -60.0 + i * step;
        const qreal upper = lower + step;
        const bool lit = displayDb_ > lower + 0.01 || (i == peakIdx && peakDb_ > -59.0f);
        const QColor on = upper > -3.0 ? theme::Tally : (upper > -12.0 ? theme::AmberHot : theme::Amber);

        const qreal y = well.bottom() - pad - (i + 1) * segH - i * gap;
        const QRectF seg(well.left() + pad, y, well.width() - 2 * pad, segH);
        if (lit)
        {
            // Lit LED: a soft halo behind it and a bright core.
            QColor halo = on;
            halo.setAlpha(70);
            p.setBrush(halo);
            p.drawRoundedRect(seg.adjusted(-1, -1, 1, 1), 2, 2);
            QLinearGradient g(seg.topLeft(), seg.bottomLeft());
            g.setColorAt(0.0, on.lighter(125));
            g.setColorAt(1.0, on);
            p.setBrush(g);
        }
        else
        {
            // Unlit LEDs keep a trace of their colour, like real ones.
            QColor dim = theme::LedOff;
            dim.setRed((dim.red() * 85 + on.red() * 15) / 100);
            dim.setGreen((dim.green() * 85 + on.green() * 15) / 100);
            dim.setBlue((dim.blue() * 85 + on.blue() * 15) / 100);
            p.setBrush(dim);
        }
        p.drawRoundedRect(seg, 1, 1);
    }
}

// ============================================================================
// Fader
// ============================================================================
static constexpr double kCapW = 28.0;
static constexpr double kCapH = 24.0;
static constexpr double kScaleW = 18.0;   // dB numbers left of the ticks

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
    const double cx = kScaleW + 14 + kCapW / 2;   // scale numbers, ticks, then the slot
    const double y  = yFromValue(value());
    return QRectF(cx - kCapW / 2, y - kCapH / 2, kCapW, kCapH);
}

void Fader::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const double cx = capRect().center().x();

    // Slot: recessed, with a lit lip on its right edge.
    const QRectF slot(cx - 2.5, travelTop() - 4, 5, travelBottom() - travelTop() + 8);
    p.setPen(QPen(QColor(0x0E, 0x10, 0x13), 1.0));
    p.setBrush(theme::Slot);
    p.drawRoundedRect(slot, 2.5, 2.5);
    p.setPen(QPen(QColor(0x3A, 0x42, 0x4D), 1.0));
    p.drawLine(QPointF(slot.right() + 0.5, slot.top() + 3), QPointF(slot.right() + 0.5, slot.bottom() - 3));

    // Gain above unity glows faintly amber in the slot.
    const double y0 = yFromValue(0), yv = yFromValue(value());
    if (yv < y0)
    {
        QColor a = theme::Amber;
        a.setAlpha(110);
        p.setPen(Qt::NoPen);
        p.setBrush(a);
        p.drawRoundedRect(QRectF(cx - 1.5, yv, 3, y0 - yv), 1.5, 1.5);
    }

    // Scale: ticks and printed numbers; 0 dB is marked in amber.
    struct Mark { double db; const char* text; };
    const Mark marks[] = { {12, "+12"}, {6, "+6"}, {0, "0"}, {-10, "10"}, {-20, "20"},
                           {-30, "30"}, {-40, "40"}, {-50, ""}, {-60, "60"} };
    QFont f = theme::Font(6.5, QFont::DemiBold);
    p.setFont(f);
    for (const Mark& m : marks)
    {
        const double y = yFromValue(static_cast<int>(m.db * 10));
        const bool zero = (m.db == 0.0);
        p.setPen(QPen(zero ? theme::Amber : QColor(0x4A, 0x53, 0x60), zero ? 2.0 : 1.0));
        p.drawLine(QPointF(cx - (zero ? 13 : 11), y), QPointF(cx - 6, y));
        if (*m.text)
        {
            p.setPen(zero ? theme::Amber : theme::Muted);
            p.drawText(QRectF(0, y - 7, kScaleW + 1, 14), Qt::AlignRight | Qt::AlignVCenter, QString::fromLatin1(m.text));
        }
    }

    // Cap: soft shadow, brushed-metal body, amber index line.
    const QRectF cap = capRect();
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 90));
    p.drawRoundedRect(cap.translated(0, 2.5), 4, 4);

    QLinearGradient g(cap.topLeft(), cap.bottomLeft());
    g.setColorAt(0.00, QColor(0x7A, 0x84, 0x91));
    g.setColorAt(0.45, QColor(0x55, 0x5E, 0x6A));
    g.setColorAt(0.55, QColor(0x48, 0x50, 0x5B));
    g.setColorAt(1.00, QColor(0x32, 0x38, 0x41));
    p.setBrush(g);
    p.setPen(QPen(QColor(0x14, 0x17, 0x1B), 1.0));
    p.drawRoundedRect(cap, 4, 4);
    p.setPen(QPen(QColor(255, 255, 255, 50), 1.0));
    p.drawLine(QPointF(cap.left() + 4, cap.top() + 1.5), QPointF(cap.right() - 4, cap.top() + 1.5));

    // Grip ridges.
    p.setPen(QPen(QColor(0x26, 0x2B, 0x32, 200), 1.0));
    for (double dy : { -7.0, -4.0, 4.0, 7.0 })
        p.drawLine(QPointF(cap.left() + 5, cap.center().y() + dy), QPointF(cap.right() - 5, cap.center().y() + dy));

    const bool active = hasFocus() || dragging_;
    QColor line = active ? theme::AmberHot : theme::Amber;
    if (!isEnabled()) line = theme::Muted;
    p.setPen(QPen(line, 2.0, Qt::SolidLine, Qt::RoundCap));
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
        lens.setColorAt(0.0, QColor(0x4A, 0x2A, 0x2D));
        lens.setColorAt(1.0, QColor(0x22, 0x1A, 0x1C));
        p.setPen(QPen(QColor(0x14, 0x17, 0x1B), 1.0));
        p.setBrush(lens);
        p.drawEllipse(c, 4.5, 4.5);
    }
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
                              "Amber: your voice, kept. Red: background and clicks being removed."));
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
            g.setColorAt(0.0, theme::AmberHot);
            g.setColorAt(1.0, theme::Amber.darker(160));
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
                   QStringLiteral("−%1 dB").arg(std::lround(-removedDb_)));
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
    const int lift = underMouse() ? 10 : 0;
    body.setColorAt(0.0, QColor(0x33 + lift, 0x3A + lift, 0x44 + lift));
    body.setColorAt(1.0, QColor(0x27 + lift, 0x2C + lift, 0x34 + lift));
    p.fillPath(shape, body);

    // Progress: an amber wash that fills left to right while the sound plays.
    if (playing)
    {
        p.save();
        p.setClipPath(shape);
        QLinearGradient wash(r.topLeft(), r.topRight());
        QColor a = theme::Amber, b = theme::Amber;
        a.setAlpha(20);
        b.setAlpha(70);
        wash.setColorAt(0.0, a);
        wash.setColorAt(1.0, b);
        const QRectF done(r.left(), r.top(), r.width() * progress_, r.height());
        p.fillRect(done, wash);
        p.fillRect(QRectF(r.left(), r.bottom() - 3, r.width() * progress_, 3), theme::Amber);
        p.restore();
    }

    // Edge: lit top, amber glow while playing, highlight with focus.
    QColor edge = playing ? theme::Amber : QColor(0x3A, 0x42, 0x4D);
    if (hasFocus() && !playing) edge = theme::AmberHot;
    if (playing)
    {
        QColor glow = theme::Amber;
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
        p.setBrush(QColor(0x14, 0x17, 0x1B));
        p.drawRoundedRect(chip.translated(0, 2), 4, 4);
        QLinearGradient cap(chip.topLeft(), chip.bottomLeft());
        cap.setColorAt(0.0, QColor(0x46, 0x4E, 0x59));
        cap.setColorAt(1.0, QColor(0x36, 0x3D, 0x47));
        p.setBrush(cap);
        p.drawRoundedRect(chip, 4, 4);
        p.setPen(theme::Legend);
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
