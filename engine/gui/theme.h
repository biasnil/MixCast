// MixCast GUI - look & feel.
//
// Concept: a modern software mixing console. One panel with each channel as
// a column: a section header, a dark info panel, a big gain readout, an LED
// meter, outlined buttons and a fat pill fader with a round knob. One accent
// colour for everything that's "on" or carrying signal; coral for the
// talking lamp, boost above 0 dB and warnings, in every theme.
//
// Colours come from the current theme's palette. SetTheme() switches them
// at once: the stylesheet is rebuilt from the palette, and the painted
// controls read the colours below every time they paint.
#pragma once

#include <QColor>
#include <QFont>
#include <QIcon>
#include <QString>

namespace theme {

enum ThemeId : int { ThemeOcean = 0, ThemeClassic, ThemeCount };

struct Palette
{
    const char* name;
    QColor chassis, header, panel, panelHi, well, slot, wellEdge, wellLip;
    QColor divider, border, line, outlineDim, outline, raised, pillBg;
    QColor knobLight, knobDark, rim, keyTop, keyBottom;
    QColor legend, textSoft, textDim, muted, faint;
    QColor accent, accentHot, accentHover, accentPressed, accentDim, accentDeep, onAccent;
    QColor warm, onWarm, tally, tallySoft, tallyDeep, liveBg, bannerBg, liveOff, ledOff;
};

const Palette& ThemePalette(int id);
int  CurrentTheme();
void SetTheme(int id);   // then re-apply StyleSheet() and repaint

// Shorthands for painted controls: always the current theme's colours.
inline QColor Chassis, Panel, PanelHi, PanelEdge, Slot, WellEdge, WellLip;
inline QColor Legend, Muted, Accent, AccentHot, AccentDim, OnAccent, Warm, Tally, LedOff;
inline QColor KnobLight, KnobDark, Rim, KeyTop, KeyBottom;

QFont   Font(qreal pointSize, int weight = QFont::Normal);
QString StyleSheet();

QIcon   LogoIcon();   // app / tray icon
QIcon   MicIcon();
QIcon   AppFallbackIcon();
QIcon   SoundboardIcon();

} // namespace theme
