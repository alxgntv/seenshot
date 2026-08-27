#pragma once

#include <QString>
#include <QUrl>

class QNetworkAccessManager;

struct FirebaseTokens {
    QString idToken;
    QString refreshToken;
    QString uid;
    QString email;
    qint64 expiresAtMs = 0;
};

// ─── Ariadne's Thread [AT-0016] ─────────────────────
// What: Firebase Identity Toolkit REST client
// Why:  Official desktop path is REST, not C++ SDK
// Date: 2026-08-25
// Related: client/src/auth/FirebaseAuthClient.cpp
// ─────────────────────────────────────────────────────
class FirebaseAuthClient {
public:
    explicit FirebaseAuthClient(QNetworkAccessManager *nam);

    bool signInEmail(const QString &email, const QString &password, FirebaseTokens *out, QString *errorCode);
    bool signUpEmail(const QString &email, const QString &password, FirebaseTokens *out, QString *errorCode);
    bool sendPasswordReset(const QString &email, QString *errorCode);
    bool sendEmailLink(const QString &email, QString *errorCode);
    bool signInEmailLink(const QString &email, const QString &oobCode, FirebaseTokens *out, QString *errorCode);
    bool signInWithCustomToken(const QString &customToken, FirebaseTokens *out, QString *errorCode);
    bool lookupEmail(const QString &idToken, QString *email, QString *errorCode);
    bool refresh(const QString &refreshToken, FirebaseTokens *out, QString *errorCode);

private:
    bool postJson(const QUrl &url, const QByteArray &body, QByteArray *response, QString *errorCode);
    bool parseTokens(const QByteArray &response, FirebaseTokens *out, QString *errorCode);
    QUrl identityUrl(const QString &path) const;

    QNetworkAccessManager *m_nam = nullptr;
};
