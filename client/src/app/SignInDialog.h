#pragma once

#include <QDialog>

class AccountSignInPanel;
class AuthSession;

// ─── Ariadne's Thread [AT-0147] ─────────────────────
// What: One application-modal sign-in dialog for Share and Settings
// Why:  Settings must not embed a second login form
// Date: 2026-08-26
// Related: [AT-0110] SignInDialog.cpp, [AT-0109] AccountSignInPanel.h
// ─────────────────────────────────────────────────────
class SignInDialog : public QDialog {
    Q_OBJECT
public:
    static bool execSignIn(AuthSession *auth, QWidget *parent);

private:
    SignInDialog(AuthSession *auth, QWidget *parent);
    AccountSignInPanel *m_panel = nullptr;
};
