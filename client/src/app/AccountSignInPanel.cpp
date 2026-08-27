#include "app/AccountSignInPanel.h"

#include "auth/AuthSession.h"
#include "errors/ErrorCatalog.h"

#include <QDebug>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

AccountSignInPanel::AccountSignInPanel(AuthSession *auth, QWidget *parent)
    : QWidget(parent)
    , m_auth(auth)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    auto *hint = new QLabel(
        QStringLiteral("Create an account or reset your password on seenshot.app."), this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    m_continueBtn = new QPushButton(QStringLiteral("Continue on seenshot.app"), this);
    m_continueBtn->setDefault(true);
    m_continueBtn->setMinimumHeight(32);
    layout->addWidget(m_continueBtn);

    m_authStatus = new QLabel(this);
    m_authStatus->setWordWrap(true);
    layout->addWidget(m_authStatus);

    connect(m_continueBtn, &QPushButton::clicked, this, &AccountSignInPanel::continueOnWebsite);
    connect(m_auth, &AuthSession::websiteSignInSettled, this, &AccountSignInPanel::onWebsiteSignInSettled);
    qInfo() << "AccountSignInPanel: constructed";
}

void AccountSignInPanel::clearSecrets()
{
    qInfo() << "AccountSignInPanel: secrets cleared";
}

void AccountSignInPanel::setAuthBusy(bool busy)
{
    m_continueBtn->setEnabled(!busy);
    qInfo() << "AccountSignInPanel: auth busy=" << busy;
}

void AccountSignInPanel::showAuthError(const QString &code)
{
    const QString text = ErrorCatalog::message(code);
    m_authStatus->setText(text);
    QMessageBox::warning(this, QStringLiteral("SeenShot"), text);
}

void AccountSignInPanel::continueOnWebsite()
{
    QString error;
    setAuthBusy(true);
    m_authStatus->clear();
    if (!m_auth->startWebsiteSignIn(&error)) {
        if (error == QLatin1String("AUTH_IN_PROGRESS")) {
            m_authStatus->setText(ErrorCatalog::message(error));
            qInfo() << "AccountSignInPanel: website sign-in already in progress";
            return;
        }
        setAuthBusy(false);
        showAuthError(error.isEmpty() ? QStringLiteral("AUTH_OAUTH_FAILED") : error);
        return;
    }
    qInfo() << "AccountSignInPanel: website sign-in started";
}

void AccountSignInPanel::onWebsiteSignInSettled(const QString &errorCode)
{
    setAuthBusy(false);
    if (errorCode.isEmpty()) {
        qInfo() << "AccountSignInPanel: website sign-in settled ok";
        return;
    }
    if (errorCode == QLatin1String("AUTH_OAUTH_DENIED")) {
        qInfo() << "AccountSignInPanel: website sign-in canceled";
        return;
    }
    qWarning() << "AccountSignInPanel: website sign-in failed code=" << errorCode;
    showAuthError(errorCode);
}
