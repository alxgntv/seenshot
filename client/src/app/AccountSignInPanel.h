#pragma once

#include <QWidget>

class AuthSession;
class QLabel;
class QLineEdit;
class QPushButton;

// ─── Ariadne's Thread [AT-0109] ─────────────────────
// What: One signed-out form: Google, email, password, link
// Why:  Settings and Share Link must use the same AuthSession, not a second login
// Date: 2026-08-26
// Related: [AT-0085] SettingsWindow.cpp, [AT-0110] SignInDialog.cpp
// ─────────────────────────────────────────────────────
class AccountSignInPanel : public QWidget {
    Q_OBJECT
public:
    explicit AccountSignInPanel(AuthSession *auth, QWidget *parent = nullptr);
    void clearSecrets();

private slots:
    void signIn();
    void createAccount();
    void forgotPassword();
    void sendEmailLink();
    void signInGoogle();

private:
    void setAuthBusy(bool busy);
    bool requireEmail(QString *errorCode) const;
    bool requireEmailPassword(QString *errorCode) const;
    void showAuthError(const QString &code);

    AuthSession *m_auth = nullptr;
    QLineEdit *m_email = nullptr;
    QLineEdit *m_password = nullptr;
    QLabel *m_authStatus = nullptr;
    QPushButton *m_googleBtn = nullptr;
    QPushButton *m_signInBtn = nullptr;
    QPushButton *m_createBtn = nullptr;
    QPushButton *m_forgotBtn = nullptr;
    QPushButton *m_linkBtn = nullptr;
};
