#pragma once

#include <QUrl>
#include <functional>

// ─── Ariadne's Thread [AT-0082] ─────────────────────
// What: seenshot:// Apple Event / get-url handler
// Why:  Email-link continue must reach the running or freshly launched agent
// Date: 2026-08-25
// Related: [AT-0082] MacUrlHandler.mm, docs/PRD-04-settings-auth.md
// ─────────────────────────────────────────────────────
class MacUrlHandler {
public:
    static void install(const std::function<void(const QUrl &)> &onUrl);
};
