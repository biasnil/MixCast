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
QMainWindow, QDialog { background: #20242B; }
QWidget { color: #E6E1D6; }

QLabel { background: transparent; }
QLabel#Wordmark     { font-size: 17pt; font-weight: 600; letter-spacing: 0.5px; }
QLabel#Muted        { color: #8C939C; }
QLabel#StripName    { font-size: 10.5pt; font-weight: 600; }
QLabel#StripStatus  { color: #8C939C; font-size: 8.5pt; }
QLabel#StripStatus[warn="true"] { color: #F2A93B; }
QLabel#Db           { font-size: 11pt; }
QLabel#Talking      { color: #5A626D; font-size: 9pt; }
QLabel#Talking[lit="true"] { color: #E5484D; }
QLabel#Warning      { color: #F2A93B; }

QFrame#Strip {
    background: #2A3038;
    border: 1px solid #3A424D;
    border-radius: 6px;
}
QFrame#Strip[off="true"] { background: #252A31; }
QFrame#MasterStrip {
    background: #2A3038;
    border: 1px solid #3A424D;
    border-radius: 6px;
}
QFrame#Divider { background: #3A424D; }
QFrame#TabBar  { border-bottom: 1px solid #3A424D; }
QPushButton#Tab {
    background: transparent;
    border: none;
    border-bottom: 2px solid transparent;
    border-radius: 0;
    padding: 8px 2px;
    color: #8C939C;
    font-size: 10.5pt;
}
QPushButton#Tab:hover   { color: #E6E1D6; background: transparent; }
QPushButton#Tab:checked { color: #E6E1D6; border-bottom-color: #F2A93B; }
QPushButton#Tab:focus   { color: #FFD28A; }
QFrame#DuckBar {
    background: #262B32;
    border-top: 1px solid #3A424D;
}
QFrame#Banner {
    background: #3A2A24;
    border: 1px solid #F2A93B;
    border-radius: 6px;
}

QPushButton {
    background: #323943;
    border: 1px solid #3A424D;
    border-radius: 4px;
    padding: 5px 12px;
    font-size: 9.5pt;
}
QPushButton:hover   { background: #3A424D; }
QPushButton:pressed { background: #2A3038; }
QPushButton:focus   { border-color: #FFD28A; }
QPushButton:disabled { color: #5A626D; }

QPushButton#Toggle:checked,
QPushButton#Primary {
    background: #F2A93B;
    border-color: #F2A93B;
    color: #20242B;
    font-weight: 600;
}
QPushButton#Toggle:checked:hover, QPushButton#Primary:hover { background: #FFD28A; }

QPushButton#AddCard {
    background: transparent;
    border: 1px dashed #4A535F;
    border-radius: 6px;
    color: #8C939C;
    font-size: 11pt;
}
QPushButton#AddCard:hover { border-color: #F2A93B; color: #F2A93B; }

QToolButton#NoiseBtn {
    background: #323943;
    border: 1px solid #3A424D;
    border-radius: 4px;
    padding: 5px 6px;
    font-size: 9pt;
}
QToolButton#NoiseBtn:hover { background: #3A424D; }
QToolButton#NoiseBtn:focus { border-color: #FFD28A; }
QToolButton#NoiseBtn[active="true"] { color: #FFD28A; border-color: #6B5530; }
QToolButton#NoiseBtn::menu-indicator { image: none; width: 0; }
QMenu::item:disabled { color: #8C939C; font-size: 8.5pt; padding-top: 8px; }
QMenu::indicator { width: 12px; height: 12px; margin-left: 4px; }
QMenu::indicator:checked { background: #F2A93B; border-radius: 6px; }
QMenu::indicator:unchecked { background: transparent; }

QToolButton#Tool {
    background: #2A3038;
    border: 1px solid #3A424D;
    border-radius: 4px;
    padding: 4px 9px;
    font-size: 9.5pt;
}
QToolButton#Tool:hover    { background: #3A424D; }
QToolButton#Tool:pressed  { background: #20242B; }
QToolButton#Tool:checked  { background: #F2A93B; color: #20242B; border-color: #F2A93B; }
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
    color: #8C939C;
    font-size: 11pt;
    padding: 0 4px;
}
QToolButton#Remove:hover { color: #E5484D; }

QComboBox, QLineEdit {
    background: #15181D;
    border: 1px solid #3A424D;
    border-radius: 4px;
    padding: 5px 8px;
    min-height: 18px;
    selection-background-color: #F2A93B;
    selection-color: #20242B;
}
QComboBox:focus, QLineEdit:focus { border-color: #FFD28A; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox QAbstractItemView {
    background: #2A3038;
    border: 1px solid #3A424D;
    selection-background-color: #3A424D;
    selection-color: #E6E1D6;
    outline: 0;
}

QSlider::groove:horizontal { height: 4px; background: #15181D; border-radius: 2px; }
QSlider::sub-page:horizontal { background: #F2A93B; border-radius: 2px; }
QSlider::handle:horizontal {
    width: 14px; height: 14px; margin: -5px 0;
    border-radius: 7px; background: #E6E1D6;
}
QSlider::handle:horizontal:hover { background: #FFD28A; }
QSlider:disabled::sub-page:horizontal { background: #4A535F; }

QListWidget {
    background: #15181D;
    border: 1px solid #3A424D;
    border-radius: 4px;
    padding: 4px;
    outline: 0;
}
QListWidget::item { padding: 6px; border-radius: 4px; }
QListWidget::item:hover { background: #2A3038; }
QListWidget::item:selected { background: #3A424D; color: #E6E1D6; }

QCheckBox { spacing: 8px; }
QCheckBox::indicator {
    width: 16px; height: 16px;
    border: 1px solid #4A535F; border-radius: 3px; background: #15181D;
}
QCheckBox::indicator:checked { background: #F2A93B; border-color: #F2A93B; }

QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; border: none; }
QScrollBar:horizontal { height: 10px; background: transparent; margin: 2px; }
QScrollBar::handle:horizontal { background: #3A424D; border-radius: 3px; min-width: 40px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QMenu { background: #2A3038; border: 1px solid #3A424D; padding: 4px; }
QMenu::item { padding: 6px 18px; border-radius: 3px; }
QMenu::item:selected { background: #3A424D; }

QToolTip { background: #2A3038; color: #E6E1D6; border: 1px solid #3A424D; padding: 4px; }
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
