// MixCast GUI - look & feel.
#include "theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QStringList>

#include <functional>

namespace theme {

QFont Font(qreal pointSize, int weight)
{
    // Bahnschrift ships with Windows 10+; its DIN-like shapes read like
    // printed console legends. Segoe UI is the fallback.
    QFont f;
    f.setFamilies(QStringList{ QStringLiteral("Bahnschrift"), QStringLiteral("Segoe UI") });
    f.setPointSizeF(pointSize);
    f.setWeight(static_cast<QFont::Weight>(weight));
    return f;
}

QString StyleSheet()
{
    // Colours mirror theme.h.
    return QStringLiteral(R"(
QMainWindow, QDialog { background: #1C252D; }
QWidget { color: #E2EAEE; }

QLabel { background: transparent; }
QLabel#Muted        { color: #8A9EAB; }
QLabel#Warning      { color: #F2645A; }

/* ---- Header -------------------------------------------------------------- */
QWidget#Header { background: #18212A; border-bottom: 1px solid #0F151A; }
QLabel#Badge {
    background: #6EDBA6; color: #12241B;
    border-radius: 4px; padding: 2px 7px;
    font-size: 13pt; font-weight: 800; letter-spacing: 1px;
}
QLabel#Wordmark     { font-size: 13pt; font-weight: 700; letter-spacing: 3px; color: #E2EAEE; }
QLabel#HeaderLabel  { color: #8A9EAB; font-size: 8pt; font-weight: 700; letter-spacing: 1.5px; }
QFrame#LivePill {
    background: #222D36; border: 1px solid #3C4D5A; border-radius: 12px;
}
QFrame#LivePill[live="true"] { background: #3A2326; border-color: #F2645A; }
QFrame#LivePill QLabel { font-size: 8.5pt; font-weight: 700; letter-spacing: 1.5px; color: #8A9EAB; }
QFrame#LivePill[live="true"] QLabel { color: #FFB1AB; }
QLabel#LiveDot { background: #4E606C; border-radius: 4px; }
QLabel#LiveDot[live="true"] { background: #F2645A; border: 2px solid #6E2E2A; border-radius: 5px; }

/* ---- Tabs ---------------------------------------------------------------- */
QFrame#TabBar { background: #18212A; border-bottom: 1px solid #0F151A; }
QPushButton#Tab {
    background: transparent; border: none; border-bottom: 2px solid transparent; border-radius: 0;
    padding: 8px 4px; color: #8A9EAB;
    font-size: 9pt; font-weight: 700; letter-spacing: 1.5px;
}
QPushButton#Tab:hover   { color: #E2EAEE; background: transparent; }
QPushButton#Tab:checked { color: #E2EAEE; border-bottom-color: #6EDBA6; }
QPushButton#Tab:focus   { color: #B4F2D3; }

/* ---- The console: one panel, a column per channel ------------------------ */
QWidget#Console {
    background: #27343F;
    border: 1px solid #33434F;
    border-radius: 8px;
}
QFrame#Strip {
    background: transparent;
    border: none;
    border-right: 1px solid #364753;
}
QFrame#Strip[off="true"] { background: rgba(0, 0, 0, 40); }
QFrame#MasterStrip {
    background: #27343F;
    border: 1px solid #33434F;
    border-top: 2px solid #6EDBA6;
    border-radius: 8px;
}
QLabel#StripTitle { color: #E2EAEE; font-size: 9pt; font-weight: 700; letter-spacing: 1.5px; }
QLabel#StripSub   { color: #8A9EAB; font-size: 8.5pt; }
QFrame#Strip[off="true"] QLabel#StripTitle { color: #8A9EAB; }

QFrame#InfoPanel {
    background: #18222A;
    border: 1px solid #0F151A;
    border-bottom-color: #3A4B57;
    border-radius: 6px;
}
QLabel#PanelLabel   { color: #6E8593; font-size: 7.5pt; font-weight: 700; letter-spacing: 1.5px; }
QLabel#StripStatus  { color: #A9BAC4; font-size: 8.5pt; }
QLabel#StripStatus[warn="true"] { color: #F2645A; }
QLabel#BigDb { padding: 0 2px; }

/* Outlined channel buttons; filled mint when on. */
QPushButton#StripBtn {
    background: transparent;
    border: 1.5px solid #5A6F7D;
    border-radius: 7px;
    color: #C8D6DD;
    font-size: 8.5pt; font-weight: 700; letter-spacing: 1px;
    padding: 0;
}
QPushButton#StripBtn:hover   { border-color: #B4F2D3; color: #FFFFFF; }
QPushButton#StripBtn:pressed { background: rgba(110, 219, 166, 40); }
QPushButton#StripBtn:focus   { border-color: #B4F2D3; }
QPushButton#StripBtn:checked { background: #6EDBA6; border-color: #6EDBA6; color: #12241B; }
QPushButton#StripBtn:checked:hover { background: #8FE6BC; }
QPushButton#StripBtn[power="true"]:!checked { color: #F2645A; border-color: #F2645A; }
QPushButton#StripBtn[solo="true"]:checked { background: #F5D25E; border-color: #F5D25E; color: #2A2410; }
QPushButton#StripBtn:disabled { color: #4E616E; border-color: #33434F; background: transparent; }

/* Soundboard page tabs. */
QPushButton#PageTab {
    background: transparent; border: 1.5px solid #3C4D5A; border-radius: 12px;
    padding: 3px 12px; color: #A9BAC4; font-size: 8.5pt; font-weight: 700;
}
QPushButton#PageTab:hover   { border-color: #B4F2D3; color: #FFFFFF; }
QPushButton#PageTab:checked { background: #6EDBA6; border-color: #6EDBA6; color: #12241B; }

/* Small pickers inside info panels (output B device). */
QToolButton#PanelPick {
    background: #24313B; border: 1px solid #3C4D5A; border-radius: 5px;
    padding: 2px 6px; color: #E2EAEE; font-size: 8pt; font-weight: 700;
}
QToolButton#PanelPick:hover { border-color: #6EDBA6; }
QToolButton#PanelPick::menu-indicator { image: none; width: 0; }
QLabel#SoloNote { color: #F5D25E; font-size: 7.5pt; font-weight: 700; letter-spacing: 1px; }

/* Preset picker inside the mic's info panel. */
QToolButton#NoiseBtn {
    background: #24313B;
    border: 1px solid #3C4D5A;
    border-radius: 5px;
    padding: 2px 6px;
    color: #E2EAEE;
    font-size: 8pt; font-weight: 700;
}
QToolButton#NoiseBtn:hover { border-color: #6EDBA6; }
QToolButton#NoiseBtn:focus { border-color: #B4F2D3; }
QToolButton#NoiseBtn[active="true"] { color: #6EDBA6; }
QToolButton#NoiseBtn::menu-indicator { image: none; width: 0; }

QPushButton#AddCard {
    background: transparent;
    border: none;
    border-right: 1px solid #364753;
    color: #6E8593;
    font-size: 10pt; font-weight: 700; letter-spacing: 1px;
}
QPushButton#AddCard:hover { color: #6EDBA6; background: rgba(110, 219, 166, 14); }

QFrame#Divider { background: transparent; }
QFrame#DuckBar { background: #18212A; border-top: 1px solid #0F151A; }
QFrame#Banner {
    background: #3A2A28;
    border: 1px solid #F2645A;
    border-radius: 8px;
}

/* ---- Generic buttons ------------------------------------------------------ */
QPushButton {
    background: transparent;
    border: 1.5px solid #4E616E;
    border-radius: 7px;
    padding: 5px 14px;
    color: #D3DEE3;
    font-size: 9pt; font-weight: 600;
}
QPushButton:hover   { border-color: #B4F2D3; color: #FFFFFF; }
QPushButton:pressed { background: rgba(110, 219, 166, 30); }
QPushButton:focus   { border-color: #B4F2D3; }
QPushButton:disabled { color: #4E616E; border-color: #33434F; }

QPushButton#Toggle:checked,
QPushButton#Primary {
    background: #6EDBA6;
    border-color: #6EDBA6;
    color: #12241B;
    font-weight: 700;
}
QPushButton#Toggle:checked:hover, QPushButton#Primary:hover { background: #8FE6BC; border-color: #8FE6BC; }
QPushButton#Toggle:checked:pressed, QPushButton#Primary:pressed { background: #57C08E; }

QToolButton#Tool {
    background: transparent;
    border: 1.5px solid #4E616E;
    border-radius: 7px;
    padding: 4px 10px;
    color: #D3DEE3;
    font-size: 9pt; font-weight: 600;
}
QToolButton#Tool:hover    { border-color: #B4F2D3; color: #FFFFFF; }
QToolButton#Tool:pressed  { background: rgba(110, 219, 166, 30); }
QToolButton#Tool:checked  { background: #6EDBA6; color: #12241B; border-color: #6EDBA6; }
QToolButton#Tool:disabled { color: #4E616E; border-color: #33434F; }
QToolButton#Tool:focus    { border-color: #B4F2D3; }
QToolButton#Tool::menu-indicator { image: none; width: 0; }
QLabel#Chip {
    background: #18222A;
    border: 1px solid #3C4D5A;
    border-radius: 9px;
    padding: 1px 9px;
    color: #8A9EAB;
    font-size: 8.5pt;
}

QToolButton#Remove {
    background: transparent; border: none; border-radius: 9px;
    color: #6E8593; font-size: 10pt; padding: 0 4px;
}
QToolButton#Remove:hover { color: #F2645A; background: rgba(242, 100, 90, 40); }

/* ---- Inputs --------------------------------------------------------------- */
QComboBox, QLineEdit {
    background: #141C22;
    border: 1px solid #33434F;
    border-radius: 6px;
    padding: 5px 10px;
    min-height: 18px;
    selection-background-color: #6EDBA6;
    selection-color: #12241B;
}
QComboBox:hover, QLineEdit:hover { border-color: #5A6F7D; }
QComboBox:focus, QLineEdit:focus { border-color: #6EDBA6; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox QAbstractItemView {
    background: #24313B;
    border: 1px solid #3C4D5A;
    selection-background-color: #33434F;
    selection-color: #6EDBA6;
    outline: 0;
}

QSlider::groove:horizontal { height: 6px; border-radius: 3px; background: #141C22; }
QSlider::sub-page:horizontal { border-radius: 3px; background: #6EDBA6; }
QSlider::handle:horizontal {
    width: 12px; height: 12px; margin: -5px 0; border-radius: 8px;
    background: #33444F; border: 2px solid #C8D8DF;
}
QSlider::handle:horizontal:hover { border-color: #B4F2D3; }
QSlider:disabled::sub-page:horizontal { background: #3C7A5E; }
QSlider::handle:horizontal:disabled { border-color: #5A6F7D; }

QListWidget {
    background: #141C22; border: 1px solid #33434F; border-radius: 6px; padding: 4px; outline: 0;
}
QListWidget::item { padding: 6px; border-radius: 4px; }
QListWidget::item:hover { background: #24313B; }
QListWidget::item:selected { background: #33434F; color: #6EDBA6; }

QCheckBox { spacing: 8px; }
QCheckBox::indicator {
    width: 16px; height: 16px; border: 1.5px solid #5A6F7D; border-radius: 4px; background: #141C22;
}
QCheckBox::indicator:hover { border-color: #B4F2D3; }
QCheckBox::indicator:checked { background: #6EDBA6; border-color: #6EDBA6; }

QScrollArea { background: transparent; border: none; }
QWidget#qt_scrollarea_viewport { background: transparent; }
QScrollBar:horizontal { height: 10px; background: transparent; margin: 2px; }
QScrollBar::handle:horizontal { background: #3C4D5A; border-radius: 3px; min-width: 40px; }
QScrollBar::handle:horizontal:hover { background: #5A6F7D; }
QScrollBar:vertical { width: 10px; background: transparent; margin: 2px; }
QScrollBar::handle:vertical { background: #3C4D5A; border-radius: 3px; min-height: 40px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

/* ---- Menus and tooltips --------------------------------------------------- */
QMenu { background: #24313B; border: 1px solid #3C4D5A; border-radius: 8px; padding: 6px; }
QMenu::item { padding: 6px 22px 6px 18px; border-radius: 5px; }
QMenu::item:selected { background: #33434F; color: #B4F2D3; }
QMenu::item:disabled { color: #6EDBA6; font-size: 8pt; font-weight: 700; padding-top: 8px; }
QMenu::separator { height: 1px; background: #3C4D5A; margin: 5px 8px; }
QMenu::indicator { width: 10px; height: 10px; margin-left: 5px; border-radius: 6px; }
QMenu::indicator:checked { background: #6EDBA6; border: 2px solid #2F5E48; }
QMenu::indicator:unchecked { background: transparent; border: 2px solid #3C4D5A; }
QMenu::right-arrow { width: 8px; height: 8px; }

QToolTip { background: #24313B; color: #E2EAEE; border: 1px solid #5A6F7D; border-radius: 6px; padding: 6px; }
)");
}

// ---------------------------------------------------------------------------
// Painted icons (no image files to ship)
// ---------------------------------------------------------------------------
static QIcon Paint(const std::function<void(QPainter&)>& draw)
{
    QPixmap pm(64, 64);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    draw(p);
    p.end();
    return QIcon(pm);
}

QIcon LogoIcon()
{
    return Paint([](QPainter& p) {
        p.setPen(Qt::NoPen);
        p.setBrush(Panel);
        p.drawRoundedRect(QRectF(2, 2, 60, 60), 12, 12);

        // Three LED ladders at different levels.
        const qreal heights[3] = { 26, 40, 18 };
        for (int i = 0; i < 3; i++)
        {
            p.setBrush(Accent);
            p.drawRoundedRect(QRectF(14 + i * 13, 50 - heights[i], 9, heights[i]), 2, 2);
        }
        // Tally dot.
        p.setBrush(Tally);
        p.drawEllipse(QPointF(50, 14), 5, 5);
    });
}

QIcon MicIcon()
{
    return Paint([](QPainter& p) {
        QPen pen(Legend, 5, Qt::SolidLine, Qt::RoundCap);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(22, 6, 20, 32), 10, 10);   // capsule
        p.drawArc(QRectF(13, 18, 38, 32), 200 * 16, 140 * 16);  // cradle
        p.drawLine(QPointF(32, 50), QPointF(32, 58));
        p.drawLine(QPointF(22, 58), QPointF(42, 58));
    });
}

QIcon SoundboardIcon()
{
    return Paint([](QPainter& p) {
        p.setPen(Qt::NoPen);
        const QRectF cells[4] = { {8, 8, 22, 22}, {34, 8, 22, 22}, {8, 34, 22, 22}, {34, 34, 22, 22} };
        for (int i = 0; i < 4; i++)
        {
            p.setBrush(i == 1 ? Accent : Legend);
            p.drawRoundedRect(cells[i], 5, 5);
        }
    });
}

QIcon AppFallbackIcon()
{
    return Paint([](QPainter& p) {
        p.setPen(QPen(Muted, 4));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(8, 8, 48, 48), 8, 8);
        // Speaker cone.
        QPainterPath cone;
        cone.moveTo(18, 27); cone.lineTo(26, 27); cone.lineTo(36, 18);
        cone.lineTo(36, 46); cone.lineTo(26, 37); cone.lineTo(18, 37); cone.closeSubpath();
        p.setBrush(Muted);
        p.setPen(Qt::NoPen);
        p.drawPath(cone);
        p.setPen(QPen(Muted, 3, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(34, 22, 12, 20), -60 * 16, 120 * 16);
    });
}

} // namespace theme
