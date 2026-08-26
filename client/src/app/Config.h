#pragma once

#include <QString>

// ─── Ariadne's Thread [AT-0005] ─────────────────────
// What: Load API and Firebase keys from env, then Info.plist
// Why:  Keys are supplied later; no secrets hardcoded
// Date: 2026-08-25
// Related: client/src/app/Config.cpp
// ─────────────────────────────────────────────────────
class Config {
public:
    static QString apiBaseUrl();
    static QString firebaseApiKey();
    static QString firebaseProjectId();
    static QString firebaseAuthDomain();
    static QString firebaseAppId();
    static QString sparkleFeedUrl();
    static QString googleOAuthClientId();
    static QString emailLinkContinueUrl();
    static QString firebaseAuthHandlerUrl();
    static QString posthogApiKey();
    static QString posthogHost();
};
