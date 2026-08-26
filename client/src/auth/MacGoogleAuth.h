#pragma once

#include <QString>
#include <QUrl>

// ─── Ariadne's Thread [AT-0099] ─────────────────────
// What: Present Firebase createAuthUri via ASWebAuthenticationSession
// Why:  WKWebView has its own cookie jar; Apple session can share Safari Google accounts
// Date: 2026-08-25
// Related: [AT-0089] FirebaseAuthClient.cpp:createAuthUri, [AT-0097] Config.cpp:firebaseAuthHandlerUrl
// ─────────────────────────────────────────────────────
class MacGoogleAuth {
public:
    static bool captureHandlerRedirect(const QUrl &authUri, QString *requestUri, QString *errorCode);
};
