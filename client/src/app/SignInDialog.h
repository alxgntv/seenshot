#pragma once

#include <QDialog>

class AccountSignInPanel;
class AuthSession;

// ─── Ariadne's Thread [AT-0110] ─────────────────────
// What: Modal sign-in over Share Link
// Why:  PRD-04 — share needs a session; warning-only was not a login
// Date: 2026-08-26
// Related: [AT-0109] AccountSignInPanel.h, [AT-0057] AnnotateWindow.cpp:share
// ─────────────────────────────────────────────────────
class SignInDialog : public QDialog {
    Q_OBJECT
public:
    static bool execShareSignIn(AuthSession *auth, QWidget *parent);

private:
    SignInDialog(AuthSession *auth, QWidget *parent);
    AccountSignInPanel *m_panel = nullptr;
};
