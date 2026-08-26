#pragma once

#include <QString>

// ─── Ariadne's Thread [AT-0001] ─────────────────────
// What: Declare human-readable English error codes for client and API
// Why:  Plan forbids raw HTTP/stack in UI dialogs
// Date: 2026-08-25
// Related: client/src/errors/ErrorCatalog.cpp
// ─────────────────────────────────────────────────────
class ErrorCatalog {
public:
    static QString message(const QString &code);
};
