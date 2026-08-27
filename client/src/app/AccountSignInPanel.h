#pragma once

#include <QWidget>

class AuthSession;
class QLabel;
class QPushButton;

// ─── Ariadne's Thread [AT-0195] ─────────────────────
// What: Signed-out form is one Continue on seenshot.app button
// Why:  Email/password lives on the website; Mac uses Authorization Code + PKCE
// Date: 2026-08-27
// Related: [AT-0193] AuthSession.cpp:startWebsiteSignIn, [AT-0147] SignInDialog.h
// ─────────────────────────────────────────────────────
class AccountSignInPanel : public QWidget {
    Q_OBJECT
public:
    explicit AccountSignInPanel(AuthSession *auth, QWidget *parent = nullptr);
    void clearSecrets();

private slots:
    void continueOnWebsite();
    void onWebsiteSignInSettled(const QString &errorCode);

private:
    void setAuthBusy(bool busy);
    void showAuthError(const QString &code);

    AuthSession *m_auth = nullptr;
    QLabel *m_authStatus = nullptr;
    QPushButton *m_continueBtn = nullptr;
};
