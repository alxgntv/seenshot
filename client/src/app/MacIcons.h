#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>

// ─── Ariadne's Thread [AT-0149] ─────────────────────
// What: Rasterize SF Symbol into a square QIcon without stretching
// Why:  drawInRect to a square dest flattened glyphs on the annotate bar
// Date: 2026-08-26
// Related: [AT-0055] MacIcons.mm:macToolbarIcon, [AT-0063] AnnotateWindow.cpp
// ─────────────────────────────────────────────────────
QIcon macToolbarIcon(const QString &symbolName, const QColor &tint = QColor());
// ─── Ariadne's Thread [AT-0305] ─────────────────────
// What: Rasterize NSWorkspace iconForFile of the running .app
// Why:  QIcon does not load SeenShot.icns; Welcome page needs the bundle mark
// Date: 2026-08-28
// Related: [AT-0302] FirstRunWizard.cpp, [AT-0055] MacIcons.mm:macToolbarIcon
// ─────────────────────────────────────────────────────
QPixmap macBundleIcon(int pointSize);
