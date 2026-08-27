#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

// ─── Ariadne's Thread [AT-0201] ─────────────────────
// What: Open the OAuth authorize URL in the user's default browser
// Why:  ASWebAuthenticationSession always uses Safari, not the default HTTP handler
// Date: 2026-08-27
// Related: [AT-0193] AuthSession.cpp:startWebsiteSignIn, [AT-0191] MacOAuthClient.mm
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
    void onWorkspaceOpenCompleted(int generation, bool ok, const QString &bundleId, const QString &errorText);

private:
    int m_openGeneration = 0;
};
