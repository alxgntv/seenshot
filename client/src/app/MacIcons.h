#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

// ─── Ariadne's Thread [AT-0149] ─────────────────────
// What: Rasterize SF Symbol into a square QIcon without stretching
// Why:  drawInRect to a square dest flattened glyphs on the annotate bar
// Date: 2026-08-26
// Related: [AT-0055] MacIcons.mm:macToolbarIcon, [AT-0063] AnnotateWindow.cpp
// ─────────────────────────────────────────────────────
QIcon macToolbarIcon(const QString &symbolName, const QColor &tint = QColor());
