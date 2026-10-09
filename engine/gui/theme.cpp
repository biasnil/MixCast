// MixCast GUI - look & feel.
#include "theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QStringList>

#include <algorithm>
#include <functional>
#include <utility>

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

// ---------------------------------------------------------------------------
// Themes
// ---------------------------------------------------------------------------
namespace {
const Palette kPalettes[ThemeCount] = {
    // Ocean: deep navy with a cyan accent.
    { "Ocean",
      {0x0A, 0x15, 0x20}, {0x08, 0x11, 0x1A}, {0x10, 0x22, 0x36}, {0x18, 0x30, 0x52}, {0x0B, 0x18, 0x28},
      {0x07, 0x11, 0x1C}, {0x05, 0x0C, 0x14}, {0x22, 0x40, 0x5E},
      {0x1E, 0x37, 0x53}, {0x1C, 0x35, 0x50}, {0x27, 0x47, 0x6A}, {0x33, 0x57, 0x7C}, {0x41, 0x69, 0x8F},
      {0x13, 0x29, 0x3F}, {0x10, 0x21, 0x2F},
      {0x4C, 0x6F, 0x8E}, {0x1A, 0x35, 0x50}, {0xBF, 0xE0, 0xF0}, {0x2E, 0x52, 0x74}, {0x21, 0x3F, 0x5C},
      {0xE4, 0xF2, 0xFA}, {0xCF, 0xE3, 0xEF}, {0x9D, 0xBB, 0xD0}, {0x7E, 0x9F, 0xB7}, {0x5F, 0x81, 0x99},
      {0x3D, 0xD5, 0xF0}, {0xA8, 0xEE, 0xFA}, {0x6F, 0xE0, 0xF4}, {0x26, 0xB6, 0xD0}, {0x1C, 0x6B, 0x7C},
      {0x12, 0x50, 0x5E}, {0x04, 0x23, 0x2B},
      {0xF5, 0xD2, 0x5E}, {0x2A, 0x24, 0x10}, {0xFF, 0x6B, 0x6B}, {0xFF, 0xB3, 0xB3}, {0x6E, 0x2A, 0x2A},
      {0x3A, 0x1E, 0x26}, {0x2E, 0x22, 0x30}, {0x3E, 0x5A, 0x72}, {0x0A, 0x18, 0x26},
      {0xFF, 0xFF, 0xFF} },
    // Classic: slate with a mint accent.
    { "Classic",
      {0x1C, 0x25, 0x2D}, {0x18, 0x21, 0x2A}, {0x27, 0x34, 0x3F}, {0x30, 0x40, 0x4C}, {0x18, 0x22, 0x2A},
      {0x14, 0x1C, 0x22}, {0x0F, 0x15, 0x1A}, {0x3A, 0x4B, 0x57},
      {0x36, 0x47, 0x53}, {0x33, 0x43, 0x4F}, {0x3C, 0x4D, 0x5A}, {0x4E, 0x61, 0x6E}, {0x5A, 0x6F, 0x7D},
      {0x24, 0x31, 0x3B}, {0x22, 0x2D, 0x36},
      {0x5B, 0x70, 0x7E}, {0x33, 0x44, 0x50}, {0xC8, 0xD8, 0xDF}, {0x4A, 0x5E, 0x6C}, {0x3A, 0x4C, 0x58},
      {0xE2, 0xEA, 0xEE}, {0xD3, 0xDE, 0xE3}, {0xA9, 0xBA, 0xC4}, {0x8A, 0x9E, 0xAB}, {0x6E, 0x85, 0x93},
      {0x6E, 0xDB, 0xA6}, {0xB4, 0xF2, 0xD3}, {0x8F, 0xE6, 0xBC}, {0x57, 0xC0, 0x8E}, {0x3C, 0x7A, 0x5E},
      {0x2F, 0x5E, 0x48}, {0x12, 0x24, 0x1B},
      {0xF5, 0xD2, 0x5E}, {0x2A, 0x24, 0x10}, {0xF2, 0x64, 0x5A}, {0xFF, 0xB1, 0xAB}, {0x6E, 0x2E, 0x2A},
      {0x3A, 0x23, 0x26}, {0x3A, 0x2A, 0x28}, {0x4E, 0x60, 0x6C}, {0x1B, 0x26, 0x2E},
      {0xFF, 0xFF, 0xFF} },
    // Sakura: dark plum with a cherry-blossom pink accent.
    { "Sakura",
      {0x1A, 0x12, 0x20}, {0x15, 0x0E, 0x1A}, {0x24, 0x17, 0x2C}, {0x33, 0x21, 0x3D}, {0x17, 0x0F, 0x1C},
      {0x11, 0x0B, 0x15}, {0x0A, 0x06, 0x0D}, {0x44, 0x30, 0x4F},
      {0x3A, 0x28, 0x43}, {0x36, 0x25, 0x40}, {0x45, 0x30, 0x4F}, {0x5A, 0x40, 0x66}, {0x6E, 0x51, 0x7B},
      {0x2A, 0x1B, 0x33}, {0x22, 0x16, 0x2A},
      {0x7A, 0x5A, 0x88}, {0x3A, 0x27, 0x45}, {0xF0, 0xD6, 0xE8}, {0x5A, 0x3F, 0x68}, {0x45, 0x2F, 0x52},
      {0xF6, 0xE8, 0xF1}, {0xE8, 0xD3, 0xE1}, {0xC4, 0xA8, 0xBB}, {0xA5, 0x8A, 0xA0}, {0x84, 0x69, 0x7F},
      {0xFF, 0x8F, 0xC7}, {0xFF, 0xD0, 0xE8}, {0xFF, 0xAA, 0xD5}, {0xE5, 0x6F, 0xAD}, {0x7E, 0x47, 0x66},
      {0x5E, 0x30, 0x49}, {0x3A, 0x0C, 0x24},
      {0xF5, 0xD2, 0x5E}, {0x2A, 0x24, 0x10}, {0xFF, 0x5A, 0x4E}, {0xFF, 0xB0, 0xA8}, {0x6E, 0x28, 0x24},
      {0x3A, 0x1A, 0x22}, {0x3A, 0x22, 0x30}, {0x5E, 0x4A, 0x66}, {0x1C, 0x12, 0x22},
      {0xFF, 0xFF, 0xFF} },
    // Ember: warm charcoal with an orange accent.
    { "Ember",
      {0x17, 0x16, 0x1A}, {0x12, 0x11, 0x15}, {0x22, 0x20, 0x24}, {0x2E, 0x2B, 0x30}, {0x16, 0x14, 0x17},
      {0x10, 0x0F, 0x12}, {0x08, 0x07, 0x0A}, {0x3E, 0x3A, 0x40},
      {0x36, 0x32, 0x3A}, {0x32, 0x2E, 0x35}, {0x40, 0x3B, 0x44}, {0x55, 0x4F, 0x59}, {0x6A, 0x63, 0x6F},
      {0x26, 0x23, 0x28}, {0x1F, 0x1D, 0x21},
      {0x6E, 0x66, 0x70}, {0x36, 0x32, 0x3A}, {0xED, 0xE2, 0xD8}, {0x4E, 0x48, 0x52}, {0x3D, 0x38, 0x3F},
      {0xF2, 0xEC, 0xE6}, {0xE2, 0xD9, 0xD0}, {0xBD, 0xB2, 0xA8}, {0x9C, 0x92, 0x8A}, {0x7C, 0x73, 0x6C},
      {0xFF, 0x9A, 0x3D}, {0xFF, 0xD2, 0xA6}, {0xFF, 0xB0, 0x66}, {0xE5, 0x82, 0x2A}, {0x7A, 0x4E, 0x28},
      {0x5A, 0x38, 0x18}, {0x2B, 0x16, 0x04},
      {0xF5, 0xD2, 0x5E}, {0x2A, 0x24, 0x10}, {0xFF, 0x55, 0x55}, {0xFF, 0xB0, 0xA8}, {0x6E, 0x26, 0x26},
      {0x3A, 0x1C, 0x1C}, {0x33, 0x24, 0x1C}, {0x5A, 0x52, 0x58}, {0x1A, 0x18, 0x1C},
      {0xFF, 0xFF, 0xFF} },
    // Light: white panels on soft grey, with a blue accent. Darker shades
    // wherever a colour has to read on white.
    { "Light",
      {0xEE, 0xF1, 0xF5}, {0xFF, 0xFF, 0xFF}, {0xFF, 0xFF, 0xFF}, {0xF7, 0xF9, 0xFB}, {0xF2, 0xF5, 0xF8},
      {0xE1, 0xE6, 0xEC}, {0xC6, 0xCF, 0xD9}, {0xFF, 0xFF, 0xFF},
      {0xDD, 0xE3, 0xEA}, {0xD5, 0xDC, 0xE4}, {0xC9, 0xD1, 0xDA}, {0xB5, 0xC0, 0xCC}, {0x98, 0xA6, 0xB5},
      {0xFF, 0xFF, 0xFF}, {0xF2, 0xF5, 0xF8},
      {0xFF, 0xFF, 0xFF}, {0xD5, 0xDD, 0xE6}, {0x5E, 0x6E, 0x80}, {0xFF, 0xFF, 0xFF}, {0xE3, 0xE8, 0xEE},
      {0x1C, 0x27, 0x33}, {0x2E, 0x3B, 0x49}, {0x4F, 0x5E, 0x6E}, {0x6B, 0x7A, 0x8B}, {0x8C, 0x99, 0xA7},
      {0x2F, 0x80, 0xED}, {0x1B, 0x5F, 0xC1}, {0x4D, 0x93, 0xF0}, {0x1F, 0x6A, 0xD1}, {0xA9, 0xC8, 0xF5},
      {0x1B, 0x4E, 0x9C}, {0xFF, 0xFF, 0xFF},
      {0xE0, 0xA8, 0x00}, {0x2A, 0x24, 0x10}, {0xE5, 0x48, 0x4D}, {0xB4, 0x23, 0x18}, {0xF5, 0xC2, 0xC0},
      {0xFD, 0xEC, 0xEC}, {0xFF, 0xF1, 0xE8}, {0xB5, 0xC0, 0xCC}, {0xE3, 0xE8, 0xEE},
      {0x0B, 0x12, 0x1A} },
};
int gTheme = ThemeOcean;
} // namespace

const Palette& ThemePalette(int id) { return kPalettes[std::clamp(id, 0, ThemeCount - 1)]; }
int  CurrentTheme() { return gTheme; }

void SetTheme(int id)
{
    gTheme = std::clamp(id, 0, ThemeCount - 1);
    const Palette& p = kPalettes[gTheme];
    Chassis = p.chassis;   Panel = p.panel;     PanelHi = p.panelHi;  PanelEdge = p.line;
    Slot = p.slot;         WellEdge = p.wellEdge; WellLip = p.wellLip;
    Legend = p.legend;     Muted = p.muted;     Accent = p.accent;    AccentHot = p.accentHot;
    AccentDim = p.accentDim; OnAccent = p.onAccent; Warm = p.warm;    Tally = p.tally;  LedOff = p.ledOff;
    KnobLight = p.knobLight; KnobDark = p.knobDark; Rim = p.rim;      KeyTop = p.keyTop; KeyBottom = p.keyBottom;
}

QString StyleSheet()
{
    // Colours mirror theme.h.
    // The stylesheet is written once with %role% placeholders, filled from the palette.
    const Palette& p = kPalettes[gTheme];
    auto rgb = [](const QColor& c) { return QStringLiteral("%1, %2, %3").arg(c.red()).arg(c.green()).arg(c.blue()); };
    const std::pair<const char*, QString> roles[] = {
        { "chassis", p.chassis.name() }, { "header", p.header.name() }, { "panel", p.panel.name() },
        { "well", p.well.name() }, { "slot", p.slot.name() }, { "wellEdge", p.wellEdge.name() },
        { "wellLip", p.wellLip.name() }, { "divider", p.divider.name() }, { "border", p.border.name() },
        { "line", p.line.name() }, { "outlineDim", p.outlineDim.name() }, { "outline", p.outline.name() },
        { "raised", p.raised.name() }, { "pillBg", p.pillBg.name() }, { "knobDark", p.knobDark.name() },
        { "rim", p.rim.name() }, { "legend", p.legend.name() }, { "textSoft", p.textSoft.name() },
        { "textDim", p.textDim.name() }, { "muted", p.muted.name() }, { "faint", p.faint.name() },
        { "accentHot", p.accentHot.name() }, { "accentHover", p.accentHover.name() },
        { "accentPressed", p.accentPressed.name() }, { "accentDim", p.accentDim.name() },
        { "accentDeep", p.accentDeep.name() }, { "accentRgb", rgb(p.accent) }, { "accent", p.accent.name() },
        { "onAccent", p.onAccent.name() }, { "onWarm", p.onWarm.name() }, { "warm", p.warm.name() },
        { "tallySoft", p.tallySoft.name() }, { "tallyDeep", p.tallyDeep.name() }, { "tallyRgb", rgb(p.tally) },
        { "tally", p.tally.name() }, { "liveBg", p.liveBg.name() }, { "bannerBg", p.bannerBg.name() },
        { "liveOff", p.liveOff.name() },
        { "strong", p.strong.name() },
    };
    QString css = QStringLiteral(R"(
QMainWindow, QDialog { background: %chassis%; }
QWidget { color: %legend%; }

QLabel { background: transparent; }
QLabel#Muted        { color: %muted%; }
QLabel#Warning      { color: %tally%; }

/* ---- Header -------------------------------------------------------------- */
QWidget#Header { background: %header%; border-bottom: 1px solid %wellEdge%; }
QLabel#Badge {
    background: %accent%; color: %onAccent%;
    border-radius: 4px; padding: 2px 7px;
    font-size: 13pt; font-weight: 800; letter-spacing: 1px;
}
QLabel#Wordmark     { font-size: 13pt; font-weight: 700; letter-spacing: 3px; color: %legend%; }
QLabel#HeaderLabel  { color: %muted%; font-size: 8pt; font-weight: 700; letter-spacing: 1.5px; }
QFrame#LivePill {
    background: %pillBg%; border: 1px solid %line%; border-radius: 12px;
}
QFrame#LivePill[live="true"] { background: %liveBg%; border-color: %tally%; }
QFrame#LivePill QLabel { font-size: 8.5pt; font-weight: 700; letter-spacing: 1.5px; color: %muted%; }
QFrame#LivePill[live="true"] QLabel { color: %tallySoft%; }
QLabel#LiveDot { background: %liveOff%; border-radius: 4px; }
QLabel#LiveDot[live="true"] { background: %tally%; border: 2px solid %tallyDeep%; border-radius: 5px; }

/* ---- Tabs ---------------------------------------------------------------- */
QFrame#TabBar { background: %header%; border-bottom: 1px solid %wellEdge%; }
QPushButton#Tab {
    background: transparent; border: none; border-bottom: 2px solid transparent; border-radius: 0;
    padding: 8px 4px; color: %muted%;
    font-size: 9pt; font-weight: 700; letter-spacing: 1.5px;
}
QPushButton#Tab:hover   { color: %legend%; background: transparent; }
QPushButton#Tab:checked { color: %legend%; border-bottom-color: %accent%; }
QPushButton#Tab:focus   { color: %accentHot%; }

/* ---- The console: one panel, a column per channel ------------------------ */
QWidget#Console {
    background: %panel%;
    border: 1px solid %border%;
    border-radius: 8px;
}
QFrame#Strip {
    background: transparent;
    border: none;
    border-right: 1px solid %divider%;
}
QFrame#Strip[off="true"] { background: rgba(0, 0, 0, 40); }
QFrame#MasterStrip {
    background: %panel%;
    border: 1px solid %border%;
    border-top: 2px solid %accent%;
    border-radius: 8px;
}
QLabel#StripTitle { color: %legend%; font-size: 9pt; font-weight: 700; letter-spacing: 1.5px; }
QLabel#StripSub   { color: %muted%; font-size: 8.5pt; }
QFrame#Strip[off="true"] QLabel#StripTitle { color: %muted%; }

QFrame#InfoPanel {
    background: %well%;
    border: 1px solid %wellEdge%;
    border-bottom-color: %wellLip%;
    border-radius: 6px;
}
QLabel#PanelLabel   { color: %faint%; font-size: 7.5pt; font-weight: 700; letter-spacing: 1.5px; }
QLabel#StripStatus  { color: %textDim%; font-size: 8.5pt; }
QLabel#StripStatus[warn="true"] { color: %tally%; }
QLabel#BigDb { padding: 0 2px; }

/* Outlined channel buttons; filled mint when on. */
QPushButton#StripBtn {
    background: transparent;
    border: 1.5px solid %outline%;
    border-radius: 7px;
    color: %textSoft%;
    font-size: 8.5pt; font-weight: 700; letter-spacing: 1px;
    padding: 0;
}
QPushButton#StripBtn:hover   { border-color: %accentHot%; color: %strong%; }
QPushButton#StripBtn:pressed { background: rgba(%accentRgb%, 40); }
QPushButton#StripBtn:focus   { border-color: %accentHot%; }
QPushButton#StripBtn:checked { background: %accent%; border-color: %accent%; color: %onAccent%; }
QPushButton#StripBtn:checked:hover { background: %accentHover%; }
QPushButton#StripBtn[power="true"]:!checked { color: %tally%; border-color: %tally%; }
QPushButton#StripBtn[solo="true"]:checked { background: %warm%; border-color: %warm%; color: %onWarm%; }
QPushButton#StripBtn:disabled { color: %outlineDim%; border-color: %border%; background: transparent; }

/* Soundboard page tabs. */
QPushButton#PageTab {
    background: transparent; border: 1.5px solid %line%; border-radius: 12px;
    padding: 3px 12px; color: %textDim%; font-size: 8.5pt; font-weight: 700;
}
QPushButton#PageTab:hover   { border-color: %accentHot%; color: %strong%; }
QPushButton#PageTab:checked { background: %accent%; border-color: %accent%; color: %onAccent%; }

/* Small pickers inside info panels (output B device). */
QToolButton#PanelPick {
    background: %raised%; border: 1px solid %line%; border-radius: 5px;
    padding: 2px 6px; color: %legend%; font-size: 8pt; font-weight: 700;
}
QToolButton#PanelPick:hover { border-color: %accent%; }
QToolButton#PanelPick::menu-indicator { image: none; width: 0; }
QLabel#SoloNote { color: %warm%; font-size: 7.5pt; font-weight: 700; letter-spacing: 1px; }

/* Preset picker inside the mic's info panel. */
QToolButton#NoiseBtn {
    background: %raised%;
    border: 1px solid %line%;
    border-radius: 5px;
    padding: 2px 6px;
    color: %legend%;
    font-size: 8pt; font-weight: 700;
}
QToolButton#NoiseBtn:hover { border-color: %accent%; }
QToolButton#NoiseBtn:focus { border-color: %accentHot%; }
QToolButton#NoiseBtn[active="true"] { color: %accent%; }
QToolButton#NoiseBtn::menu-indicator { image: none; width: 0; }

QPushButton#AddCard {
    background: transparent;
    border: none;
    border-right: 1px solid %divider%;
    color: %faint%;
    font-size: 10pt; font-weight: 700; letter-spacing: 1px;
}
QPushButton#AddCard:hover { color: %accent%; background: rgba(%accentRgb%, 14); }

QFrame#Divider { background: transparent; }
QFrame#DuckBar { background: %header%; border-top: 1px solid %wellEdge%; }
QFrame#Banner {
    background: %bannerBg%;
    border: 1px solid %tally%;
    border-radius: 8px;
}

/* ---- Generic buttons ------------------------------------------------------ */
QPushButton {
    background: transparent;
    border: 1.5px solid %outlineDim%;
    border-radius: 7px;
    padding: 5px 14px;
    color: %textSoft%;
    font-size: 9pt; font-weight: 600;
}
QPushButton:hover   { border-color: %accentHot%; color: %strong%; }
QPushButton:pressed { background: rgba(%accentRgb%, 30); }
QPushButton:focus   { border-color: %accentHot%; }
QPushButton:disabled { color: %outlineDim%; border-color: %border%; }

QPushButton#Toggle:checked,
QPushButton#Primary {
    background: %accent%;
    border-color: %accent%;
    color: %onAccent%;
    font-weight: 700;
}
QPushButton#Toggle:checked:hover, QPushButton#Primary:hover { background: %accentHover%; border-color: %accentHover%; }
QPushButton#Toggle:checked:pressed, QPushButton#Primary:pressed { background: %accentPressed%; }

QToolButton#Tool {
    background: transparent;
    border: 1.5px solid %outlineDim%;
    border-radius: 7px;
    padding: 4px 10px;
    color: %textSoft%;
    font-size: 9pt; font-weight: 600;
}
QToolButton#Tool:hover    { border-color: %accentHot%; color: %strong%; }
QToolButton#Tool:pressed  { background: rgba(%accentRgb%, 30); }
QToolButton#Tool:checked  { background: %accent%; color: %onAccent%; border-color: %accent%; }
QToolButton#Tool:disabled { color: %outlineDim%; border-color: %border%; }
QToolButton#Tool:focus    { border-color: %accentHot%; }
QToolButton#Tool::menu-indicator { image: none; width: 0; }
QLabel#Chip {
    background: %well%;
    border: 1px solid %line%;
    border-radius: 9px;
    padding: 1px 9px;
    color: %muted%;
    font-size: 8.5pt;
}

QToolButton#Remove {
    background: transparent; border: none; border-radius: 9px;
    color: %faint%; font-size: 10pt; padding: 0 4px;
}
QToolButton#Remove:hover { color: %tally%; background: rgba(%tallyRgb%, 40); }

/* ---- Inputs --------------------------------------------------------------- */
QComboBox, QLineEdit {
    background: %slot%;
    border: 1px solid %border%;
    border-radius: 6px;
    padding: 5px 10px;
    min-height: 18px;
    selection-background-color: %accent%;
    selection-color: %onAccent%;
}
QComboBox:hover, QLineEdit:hover { border-color: %outline%; }
QComboBox:focus, QLineEdit:focus { border-color: %accent%; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox QAbstractItemView {
    background: %raised%;
    border: 1px solid %line%;
    selection-background-color: %border%;
    selection-color: %accent%;
    outline: 0;
}

QSlider::groove:horizontal { height: 6px; border-radius: 3px; background: %slot%; }
QSlider::sub-page:horizontal { border-radius: 3px; background: %accent%; }
QSlider::handle:horizontal {
    width: 12px; height: 12px; margin: -5px 0; border-radius: 8px;
    background: %knobDark%; border: 2px solid %rim%;
}
QSlider::handle:horizontal:hover { border-color: %accentHot%; }
QSlider:disabled::sub-page:horizontal { background: %accentDim%; }
QSlider::handle:horizontal:disabled { border-color: %outline%; }

QListWidget {
    background: %slot%; border: 1px solid %border%; border-radius: 6px; padding: 4px; outline: 0;
}
QListWidget::item { padding: 6px; border-radius: 4px; }
QListWidget::item:hover { background: %raised%; }
QListWidget::item:selected { background: %border%; color: %accent%; }

QCheckBox { spacing: 8px; }
QCheckBox::indicator {
    width: 16px; height: 16px; border: 1.5px solid %outline%; border-radius: 4px; background: %slot%;
}
QCheckBox::indicator:hover { border-color: %accentHot%; }
QCheckBox::indicator:checked { background: %accent%; border-color: %accent%; }

QScrollArea { background: transparent; border: none; }
QWidget#qt_scrollarea_viewport { background: transparent; }
QScrollBar:horizontal { height: 10px; background: transparent; margin: 2px; }
QScrollBar::handle:horizontal { background: %line%; border-radius: 3px; min-width: 40px; }
QScrollBar::handle:horizontal:hover { background: %outline%; }
QScrollBar:vertical { width: 10px; background: transparent; margin: 2px; }
QScrollBar::handle:vertical { background: %line%; border-radius: 3px; min-height: 40px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

/* ---- Menus and tooltips --------------------------------------------------- */
QMenu { background: %raised%; border: 1px solid %line%; border-radius: 8px; padding: 6px; }
QMenu::item { padding: 6px 22px 6px 18px; border-radius: 5px; }
QMenu::item:selected { background: %border%; color: %accentHot%; }
QMenu::item:disabled { color: %accent%; font-size: 8pt; font-weight: 700; padding-top: 8px; }
QMenu::separator { height: 1px; background: %line%; margin: 5px 8px; }
QMenu::indicator { width: 10px; height: 10px; margin-left: 5px; border-radius: 6px; }
QMenu::indicator:checked { background: %accent%; border: 2px solid %accentDeep%; }
QMenu::indicator:unchecked { background: transparent; border: 2px solid %line%; }
QMenu::right-arrow { width: 8px; height: 8px; }

QToolTip { background: %raised%; color: %legend%; border: 1px solid %outline%; border-radius: 6px; padding: 6px; }
)");
    for (const auto& [role, value] : roles)
        css.replace(QLatin1Char('%') + QLatin1String(role) + QLatin1Char('%'), value);
    return css;
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
