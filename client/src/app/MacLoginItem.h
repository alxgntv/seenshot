#pragma once

#include <QString>

// ─── Ariadne's Thread [AT-0081] ─────────────────────
// What: SMAppService.mainApp wrapper for Open SeenShot at login
// Why:  PRD-04 checkbox must be the real login-item status, not QSettings
// Date: 2026-08-25
// Related: [AT-0081] MacLoginItem.mm, docs/PRD-04-settings-auth.md
// ─────────────────────────────────────────────────────
class MacLoginItem {
public:
    static bool isEnabled();
    static bool ensureEnabled(QString *errorCode);
    static bool setEnabled(bool on, QString *errorCode);
};
