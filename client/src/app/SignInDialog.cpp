#include "app/SignInDialog.h"

#include "app/AccountSignInPanel.h"
#include "auth/AuthSession.h"

#include <QApplication>
#include <QDebug>
#include <QLabel>
#include <QVBoxLayout>

SignInDialog::SignInDialog(AuthSession *auth, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("SeenShot"));
    setModal(true);
    setWindowModality(Qt::ApplicationModal);
    setMinimumWidth(420);
    setMaximumWidth(480);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(12);
    auto *lead = new QLabel(QStringLiteral("Sign in with seenshot.app to save screenshots to the cloud."), this);
    lead->setWordWrap(true);
    layout->addWidget(lead);
    m_panel = new AccountSignInPanel(auth, this);
    layout->addWidget(m_panel);
    const QString version = QApplication::applicationVersion();
    auto *versionLabel = new QLabel(QStringLiteral("Version %1").arg(version), this);
    versionLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(versionLabel);
    qInfo() << "SignInDialog: version label" << version;
    connect(auth, &AuthSession::sessionChanged, this, [this, auth]() {
        qInfo() << "SignInDialog: sessionChanged hasSession=" << auth->hasSession();
        if (auth->hasSession()) {
            accept();
        }
    });
    qInfo() << "SignInDialog: constructed";
}

bool SignInDialog::execSignIn(AuthSession *auth, QWidget *parent)
{
    if (!auth) {
        qWarning() << "SignInDialog: execSignIn null auth";
        return false;
    }
    if (auth->hasSession()) {
        qInfo() << "SignInDialog: already signed in";
        return true;
    }
    SignInDialog dialog(auth, parent);
    const int code = dialog.exec();
    qInfo() << "SignInDialog: exec code=" << code << " hasSession=" << auth->hasSession();
    return code == QDialog::Accepted && auth->hasSession();
}
