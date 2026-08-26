#pragma once

#include <QIcon>
#include <QString>

// ─── Ariadne's Thread [AT-0053] ─────────────────────
// What: Load macOS SF Symbol as QIcon for toolbar actions
// Why:  Native AppKit icons left of action titles
// Date: 2026-08-25
// Related: [AT-0012] AnnotateWindow.cpp, MacIcons.mm:macToolbarIcon
// ─────────────────────────────────────────────────────
QIcon macToolbarIcon(const QString &symbolName);
