#pragma once

#include <QString>
#include <QtGlobal>

// ─── Ariadne's Thread [AT-0003] ─────────────────────
// What: Install file logger with 5 MB x 3 rotation
// Why:  Plan requires English logs and disk rotation
// Date: 2026-08-25
// Related: client/src/app/Logger.cpp
// ─────────────────────────────────────────────────────
class Logger {
public:
    static void install();
    static QString filePath();
};
