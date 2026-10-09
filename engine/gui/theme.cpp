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
QMainWindow, QDialog {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #1E2228, stop:1 #191C21);
}
QWidget { color: #E6E1D6; }

QLabel { background: transparent; }
QLabel#Wordmark     { font-size: 17pt; font-weight: 600; letter-spacing: 0.5px; }
QLabel#Muted        { color: #8C939C; }
QLabel#StripStatus  { color: #8C939C; font-size: 8.5pt; }
QLabel#StripStatus[warn="true"] { color: #F2A93B; }
QLabel#Talking      { color: #5A626D; font-size: 8pt; font-weight: 600; letter-spacing: 1px; }
QLabel#Talking[lit="true"] { color: #FF6B70; }
QLabel#Warning      { color: #F2A93B; }

/* ---- Header: a lit band across the top -------------------------------- */
QWidget#Header {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #262B33, stop:1 #1F2329);
    border-bottom: 1px solid #14171B;
}
QFrame#LivePill {
    background: #22262C;
    border: 1px solid #3A424D;
    border-radius: 12px;
}
QFrame#LivePill[live="true"] {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #4A2023, stop:1 #361A1D);
    border-color: #E5484D;
}
QFrame#LivePill QLabel { font-size: 8.5pt; font-weight: 700; letter-spacing: 1.5px; color: #8C939C; }
QFrame#LivePill[live="true"] QLabel { color: #FFB3B5; }
QLabel#LiveDot { background: #5A626D; border-radius: 4px; }
QLabel#LiveDot[live="true"] { background: #FF5A5F; border: 2px solid #7A2A2E; border-radius: 5px; }

/* ---- Tabs: segmented control ------------------------------------------ */
QFrame#TabBar { background: transparent; }
QPushButton#Tab {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 7px;
    padding: 5px 14px;
    color: #8C939C;
    font-size: 10pt;
    font-weight: 600;
}
QPushButton#Tab:hover   { color: #E6E1D6; background: #23282F; }
QPushButton#Tab:checked {
    color: #FFD28A;
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #343B45, stop:1 #2A3038);
    border-color: #454E5A;
    border-bottom-color: #F2A93B;
}
QPushButton#Tab:focus   { border-color: #FFD28A; }

/* ---- Channel strips: lit top edge, soft body gradient ----------------- */
QFrame#Strip, QFrame#MasterStrip {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #2F353E, stop:0.5 #2A3038, stop:1 #252A31);
    border: 1px solid #343B45;
    border-top-color: #4A5360;
    border-bottom-color: #1C2025;
    border-radius: 10px;
}
QFrame#Strip[off="true"] {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #262A31, stop:1 #212429);
    border-top-color: #343B45;
}
QFrame#Strip[off="true"] QLabel#StripName { color: #7A818A; }
QFrame#MasterStrip {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #3A3328, stop:0.18 #2C3038, stop:1 #252A31);
    border-color: #4A4335;
    border-top-color: #8A6A38;
}

/* Scribble strip: the channel name on a recessed, colour-coded label. */
QLabel#StripName {
    background: #16191E;
    border: 1px solid #101215;
    border-left: 3px solid #8C939C;
    border-radius: 2px;
    padding: 4px 6px;
    font-size: 10pt;
    font-weight: 600;
}
QLabel#StripName[kind="mic"]    { border-left-color: #E5484D; }
QLabel#StripName[kind="sb"]     { border-left-color: #F2A93B; }
QLabel#StripName[kind="app"]    { border-left-color: #4CC3D9; }
QLabel#StripName[kind="master"] { border-left-color: #FFD28A; color: #FFD28A; }

/* dB readout: a small amber LCD. */
QLabel#Db {
    background: #121418;
    border: 1px solid #0E1013;
    border-top-color: #0A0B0D;
    border-bottom-color: #353C46;
    border-radius: 4px;
    padding: 2px 0;
    color: #FFD28A;
    font-size: 10pt;
    font-weight: 600;
}
QFrame#Strip[off="true"] QLabel#Db { color: #6B5530; }

QFrame#Divider { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 transparent, stop:0.5 #3A424D, stop:1 transparent); }
QFrame#DuckBar {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #262B32, stop:1 #20242A);
    border-top: 1px solid #3A424D;
}
QFrame#Banner {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #44302A, stop:1 #382722);
    border: 1px solid #F2A93B;
    border-radius: 8px;
}

/* ---- Buttons ------------------------------------------------------------ */
QPushButton {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #3A424D, stop:1 #313842);
    border: 1px solid #2A3038;
    border-top-color: #4A5360;
    border-bottom-color: #1C2025;
    border-radius: 6px;
    padding: 5px 12px;
    font-size: 9.5pt;
    font-weight: 600;
}
QPushButton:hover   { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #434C58, stop:1 #39414C); }
QPushButton:pressed { background: #2A3038; border-top-color: #1C2025; }
QPushButton:focus   { border-color: #FFD28A; }
QPushButton:disabled { color: #5A626D; }

QPushButton#Toggle:checked,
QPushButton#Primary {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FFC266, stop:0.5 #F2A93B, stop:1 #DA9128);
    border: 1px solid #B7781F;
    border-top-color: #FFD28A;
    color: #1E1A14;
}
QPushButton#Toggle:checked:hover, QPushButton#Primary:hover {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FFD28A, stop:1 #F2A93B);
}
QPushButton#Toggle:checked:pressed, QPushButton#Primary:pressed { background: #DA9128; }
QPushButton#Toggle:!checked { color: #A9AFB7; }
QPushButton#Toggle[power="true"]:!checked { color: #FF8A8D; border-color: #5A2A2E; border-top-color: #6A3438; }

QPushButton#AddCard {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 rgba(255,255,255,8), stop:1 transparent);
    border: 2px dashed #3A424D;
    border-radius: 10px;
    color: #8C939C;
    font-size: 11pt;
}
QPushButton#AddCard:hover { border-color: #F2A93B; color: #FFD28A; background: rgba(242,169,59,14); }

QToolButton#NoiseBtn {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #3A424D, stop:1 #313842);
    border: 1px solid #2A3038;
    border-top-color: #4A5360;
    border-bottom-color: #1C2025;
    border-radius: 6px;
    padding: 5px 6px;
    font-size: 9pt;
    font-weight: 600;
}
QToolButton#NoiseBtn:hover { background: #434C58; }
QToolButton#NoiseBtn:focus { border-color: #FFD28A; }
QToolButton#NoiseBtn[active="true"] { color: #FFD28A; border-color: #6B5530; border-top-color: #8A6A38; }
QToolButton#NoiseBtn::menu-indicator { image: none; width: 0; }

QToolButton#Tool {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #343B45, stop:1 #2C323A);
    border: 1px solid #2A3038;
    border-top-color: #464F5B;
    border-radius: 6px;
    padding: 4px 9px;
    font-size: 9.5pt;
}
QToolButton#Tool:hover    { background: #3A424D; }
QToolButton#Tool:pressed  { background: #20242B; }
QToolButton#Tool:checked  { background: #F2A93B; color: #20242B; border-color: #B7781F; }
QToolButton#Tool:disabled { color: #5A626D; background: #252A31; }
QToolButton#Tool:focus    { border-color: #FFD28A; }
QToolButton#Tool::menu-indicator { image: none; width: 0; }
QLabel#Chip {
    background: #2A3038;
    border: 1px solid #3A424D;
    border-radius: 9px;
    padding: 1px 9px;
    color: #8C939C;
    font-size: 8.5pt;
}

QToolButton#Remove {
    background: transparent;
    border: none;
    border-radius: 9px;
    color: #6B737D;
    font-size: 10pt;
    padding: 0 4px;
}
QToolButton#Remove:hover { color: #FF8A8D; background: rgba(229,72,77,40); }

/* ---- Inputs ------------------------------------------------------------- */
QComboBox, QLineEdit {
    background: #14171B;
    border: 1px solid #2E343D;
    border-top-color: #0E1013;
    border-bottom-color: #3A424D;
    border-radius: 6px;
    padding: 5px 10px;
    min-height: 18px;
    selection-background-color: #F2A93B;
    selection-color: #20242B;
}
QComboBox:hover, QLineEdit:hover { border-color: #4A5360; }
QComboBox:focus, QLineEdit:focus { border-color: #FFD28A; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox QAbstractItemView {
    background: #2A3038;
    border: 1px solid #3A424D;
    selection-background-color: #3A424D;
    selection-color: #FFD28A;
    outline: 0;
}

QSlider::groove:horizontal {
    height: 6px; border-radius: 3px;
    background: #121418; border: 1px solid #0E1013; border-bottom-color: #353C46;
}
QSlider::sub-page:horizontal {
    border-radius: 3px;
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #B7781F, stop:1 #FFC266);
}
QSlider::handle:horizontal {
    width: 12px; height: 12px; margin: -5px 0;
    border-radius: 8px;
    background: qradialgradient(cx:0.5, cy:0.4, radius:0.6, fx:0.5, fy:0.3, stop:0 #FFFFFF, stop:1 #CFC8BA);
    border: 2px solid #F2A93B;
}
QSlider::handle:horizontal:hover { border-color: #FFD28A; }
QSlider:disabled::sub-page:horizontal { background: #4A535F; }
QSlider::handle:horizontal:disabled { border-color: #5A626D; background: #8C939C; }

QListWidget {
    background: #14171B;
    border: 1px solid #2E343D;
    border-radius: 6px;
    padding: 4px;
    outline: 0;
}
QListWidget::item { padding: 6px; border-radius: 4px; }
QListWidget::item:hover { background: #2A3038; }
QListWidget::item:selected { background: #3A424D; color: #FFD28A; }

QCheckBox { spacing: 8px; }
QCheckBox::indicator {
    width: 16px; height: 16px;
    border: 1px solid #4A535F; border-radius: 4px; background: #14171B;
}
QCheckBox::indicator:hover { border-color: #FFD28A; }
QCheckBox::indicator:checked {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FFC266, stop:1 #DA9128);
    border-color: #B7781F;
}

QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; border: none; }
QScrollBar:horizontal { height: 10px; background: transparent; margin: 2px; }
QScrollBar::handle:horizontal { background: #3A424D; border-radius: 3px; min-width: 40px; }
QScrollBar::handle:horizontal:hover { background: #4A5360; }
QScrollBar:vertical { width: 10px; background: transparent; margin: 2px; }
QScrollBar::handle:vertical { background: #3A424D; border-radius: 3px; min-height: 40px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

/* ---- Menus and tooltips ------------------------------------------------- */
QMenu {
    background: #252A31;
    border: 1px solid #3A424D;
    border-top-color: #4A5360;
    border-radius: 8px;
    padding: 6px;
}
QMenu::item { padding: 6px 22px 6px 18px; border-radius: 5px; }
QMenu::item:selected { background: #343B45; color: #FFD28A; }
QMenu::item:disabled { color: #F2A93B; font-size: 8pt; font-weight: 700; padding-top: 8px; }
QMenu::separator { height: 1px; background: #3A424D; margin: 5px 8px; }
QMenu::indicator { width: 10px; height: 10px; margin-left: 5px; border-radius: 6px; }
QMenu::indicator:checked { background: #F2A93B; border: 2px solid #6B5530; }
QMenu::indicator:unchecked { background: transparent; border: 2px solid #3A424D; }
QMenu::right-arrow { width: 8px; height: 8px; }

QToolTip {
    background: #252A31; color: #E6E1D6;
    border: 1px solid #4A5360; border-radius: 6px; padding: 6px;
}
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
            p.setBrush(Amber);
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
            p.setBrush(i == 1 ? Amber : Legend);
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
