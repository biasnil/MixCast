// MixCast GUI - look & feel.
//
// Concept: a broadcast desk. Graphite chassis, printed warm-white legends,
// amber LED ladders, and a red tally lamp that lights when you're talking.
#pragma once

#include <QColor>
#include <QFont>
#include <QIcon>
#include <QString>

namespace theme {

inline const QColor Chassis   {0x20, 0x24, 0x2B};   // window background
inline const QColor Panel     {0x2A, 0x30, 0x38};   // channel strips
inline const QColor PanelEdge {0x3A, 0x42, 0x4D};   // strip borders, buttons
inline const QColor Slot      {0x15, 0x18, 0x1D};   // fader slot, meter well
inline const QColor Legend    {0xE6, 0xE1, 0xD6};   // main text
inline const QColor Muted     {0x8C, 0x93, 0x9C};   // secondary text
inline const QColor Amber     {0xF2, 0xA9, 0x3B};   // signal, active controls
inline const QColor AmberHot  {0xFF, 0xD2, 0x8A};   // meter near the top
inline const QColor Tally     {0xE5, 0x48, 0x4D};   // talking / clipping
inline const QColor LedOff    {0x1C, 0x20, 0x26};   // unlit LED segment

QFont   Font(qreal pointSize, int weight = QFont::Normal);
QString StyleSheet();

QIcon   LogoIcon();   // app / tray icon
QIcon   MicIcon();
QIcon   AppFallbackIcon();
QIcon   SoundboardIcon();

} // namespace theme
