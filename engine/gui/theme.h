// MixCast GUI - look & feel.
//
// Concept: a modern software mixing console. One slate panel with each
// channel as a column: a section header, a dark info panel, a big gain
// readout, an LED meter, outlined buttons and a fat pill fader with a round
// knob. One mint accent for everything that's "on" or carrying signal; coral
// for the talking lamp, boost above 0 dB and warnings.
#pragma once

#include <QColor>
#include <QFont>
#include <QIcon>
#include <QString>

namespace theme {

inline const QColor Chassis   {0x1C, 0x25, 0x2D};   // window background
inline const QColor Panel     {0x27, 0x34, 0x3F};   // console panel, cards
inline const QColor PanelHi   {0x30, 0x40, 0x4C};   // raised controls
inline const QColor PanelEdge {0x3C, 0x4D, 0x5A};   // dividers, outlines
inline const QColor Slot      {0x14, 0x1C, 0x22};   // wells: meters, info panels, fader tracks
inline const QColor Legend    {0xE2, 0xEA, 0xEE};   // main text
inline const QColor Muted     {0x8A, 0x9E, 0xAB};   // secondary text
inline const QColor Accent    {0x6E, 0xDB, 0xA6};   // mint: signal, on, selected
inline const QColor AccentHot {0xB4, 0xF2, 0xD3};   // lighter mint: hover, focus
inline const QColor AccentDim {0x3C, 0x7A, 0x5E};   // mint on switched-off channels
inline const QColor Warm      {0xF5, 0xD2, 0x5E};   // meter near the top
inline const QColor Tally     {0xF2, 0x64, 0x5A};   // coral: talking, clipping, boost, warnings
inline const QColor LedOff    {0x1B, 0x26, 0x2E};   // unlit LED segment

QFont   Font(qreal pointSize, int weight = QFont::Normal);
QString StyleSheet();

QIcon   LogoIcon();   // app / tray icon
QIcon   MicIcon();
QIcon   AppFallbackIcon();
QIcon   SoundboardIcon();

} // namespace theme
