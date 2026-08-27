#pragma once

#include "auth/FirebaseAuthClient.h"

#include <QMutex>
#include <QObject>
#include <QString>
#include <QUrl>

class ISecureStore;
class MacOAuthClient;
class QNetworkAccessManager;

// ─── Ariadne's Thread [AT-0018] ─────────────────────
// What: Single in-flight refresh session stored in Keychain
// Why:  Parallel refresh is a race; offline must stay signed in
// Date: 2026-08-25
// Related: client/src/auth/AuthSession.cpp
// ─────────────────────────────────────────────────────
class AuthSession : public QObject {
    Q_OBJECT
public:
    AuthSession(ISecureStore *store, QNetworkAccessManager *nam, QObject *parent = nullptr);

    bool hasSession() const;
    bool isOnline() const;
    QString uid() const;
    QString email() const;
    bool signInEmail(const QString &email, const QString &password, QString *errorCode);
    bool signUpEmail(const QString &email, const QString &password, QString *errorCode);
    bool sendPasswordReset(const QString &email, QString *errorCode);
    bool sendEmailLink(const QString &email, QString *errorCode);
    bool completeEmailLink(const QUrl &url, QString *errorCode);
    bool startWebsiteSignIn(QString *errorCode);
    bool completeWebsiteCallback(const QUrl &url, QString *errorCode);
    bool ensureIdToken(QString *idToken, QString *errorCode);
    void signOut();

signals:
    void sessionChanged();
    void websiteSignInSettled(const QString &errorCode);

private:
    bool loadFromStore();
    bool persist(const FirebaseTokens &tokens, QString *errorCode);
    bool beginAuth(QString *errorCode);
    void endAuth();
    bool finishTokens(const FirebaseTokens &tokens, const QString &method, QString *errorCode);
    bool fillEmailIfNeeded(FirebaseTokens *tokens, QString *errorCode);
    bool persistPendingPkce(const QString &verifier, const QString &state, QString *errorCode);
    bool loadPendingPkce(QString *verifier, QString *state, QString *errorCode);
    void clearPendingPkce();
    bool exchangeAuthorizationCode(const QString &code, const QString &verifier, QString *customToken,
                                   QString *errorCode);
    void onOAuthBrowserFinished(const QUrl &callbackUrl, const QString &errorCode);
    static QString oobCodeFromUrl(const QUrl &url);

    ISecureStore *m_store = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
    FirebaseAuthClient m_firebase;
    MacOAuthClient *m_oauth = nullptr;
    mutable QMutex m_mutex;
    FirebaseTokens m_tokens;
    bool m_refreshing = false;
    bool m_authBusy = false;
    bool m_signedOut = false;
};
