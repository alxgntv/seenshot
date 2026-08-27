#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

// ─── Ariadne's Thread [AT-0191] ─────────────────────
// What: ASWebAuthenticationSession client for seenshot.app PKCE
// Why:  Official macOS OAuth API for native apps; callback scheme seenshot
// Date: 2026-08-27
// Related: [AT-0193] AuthSession.cpp:startWebsiteSignIn, [AT-0040] seenshot-web→src/oauth.ts
// ─────────────────────────────────────────────────────
class MacOAuthClient : public QObject {
    Q_OBJECT
public:
    explicit MacOAuthClient(QObject *parent = nullptr);
    ~MacOAuthClient() override;

    static bool generatePkce(QString *verifier, QString *challenge, QString *state, QString *errorCode);
    bool start(const QUrl &authorizeUrl);

signals:
    void finished(const QUrl &callbackUrl, const QString &errorCode);

private slots:
    void finishFromBrowser(const QUrl &callbackUrl, const QString &errorCode);

private:
    void *m_native = nullptr;
};
