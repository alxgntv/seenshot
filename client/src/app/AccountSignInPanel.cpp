#include "app/AccountSignInPanel.h"

#include "auth/AuthSession.h"
#include "errors/ErrorCatalog.h"

#include <QCursor>
#include <QDebug>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace {

bool looksLikeEmail(const QString &email)
{
    static const QRegularExpression re(QStringLiteral("^[^\\s@]+@[^\\s@]+\\.[^\\s@]+$"));
    return re.match(email).hasMatch();
}

} // namespace

AccountSignInPanel::AccountSignInPanel(AuthSession *auth, QWidget *parent)
    : QWidget(parent)
    , m_auth(auth)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    m_googleBtn = new QPushButton(QStringLiteral("Continue with Google"), this);
    m_googleBtn->setMinimumHeight(32);
    layout->addWidget(m_googleBtn);

    auto *orRow = new QHBoxLayout();
    auto *orLeft = new QFrame(this);
    orLeft->setFrameShape(QFrame::HLine);
    auto *orRight = new QFrame(this);
    orRight->setFrameShape(QFrame::HLine);
    auto *orLabel = new QLabel(QStringLiteral("or"), this);
    orLabel->setAlignment(Qt::AlignCenter);
    orRow->addWidget(orLeft, 1);
    orRow->addWidget(orLabel);
    orRow->addWidget(orRight, 1);
    layout->addLayout(orRow);

    auto *emailLabel = new QLabel(QStringLiteral("Email"), this);
    m_email = new QLineEdit(this);
    m_email->setPlaceholderText(QStringLiteral("name@example.com"));
    m_email->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *passwordLabel = new QLabel(QStringLiteral("Password"), this);
    m_password = new QLineEdit(this);
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(emailLabel);
    layout->addWidget(m_email);
    layout->addWidget(passwordLabel);
    layout->addWidget(m_password);

    m_signInBtn = new QPushButton(QStringLiteral("Sign In"), this);
    m_signInBtn->setDefault(true);
    m_signInBtn->setMinimumHeight(32);
    layout->addWidget(m_signInBtn);

    auto *linkRow = new QHBoxLayout();
    m_createBtn = new QPushButton(QStringLiteral("Create account"), this);
    m_forgotBtn = new QPushButton(QStringLiteral("Forgot password"), this);
    m_createBtn->setFlat(true);
    m_forgotBtn->setFlat(true);
    m_createBtn->setCursor(Qt::PointingHandCursor);
    m_forgotBtn->setCursor(Qt::PointingHandCursor);
    linkRow->addWidget(m_createBtn);
    linkRow->addStretch(1);
    linkRow->addWidget(m_forgotBtn);
    layout->addLayout(linkRow);

    m_linkBtn = new QPushButton(QStringLiteral("Email me a sign-in link"), this);
    m_linkBtn->setFlat(true);
    m_linkBtn->setCursor(Qt::PointingHandCursor);
    layout->addWidget(m_linkBtn, 0, Qt::AlignLeft);

    m_authStatus = new QLabel(this);
    m_authStatus->setWordWrap(true);
    layout->addWidget(m_authStatus);

    connect(m_googleBtn, &QPushButton::clicked, this, &AccountSignInPanel::signInGoogle);
    connect(m_signInBtn, &QPushButton::clicked, this, &AccountSignInPanel::signIn);
    connect(m_createBtn, &QPushButton::clicked, this, &AccountSignInPanel::createAccount);
    connect(m_forgotBtn, &QPushButton::clicked, this, &AccountSignInPanel::forgotPassword);
    connect(m_linkBtn, &QPushButton::clicked, this, &AccountSignInPanel::sendEmailLink);
    qInfo() << "AccountSignInPanel: constructed";
}

void AccountSignInPanel::clearSecrets()
{
    m_password->clear();
    qInfo() << "AccountSignInPanel: secrets cleared";
}

void AccountSignInPanel::setAuthBusy(bool busy)
{
    m_googleBtn->setEnabled(!busy);
    m_signInBtn->setEnabled(!busy);
    m_createBtn->setEnabled(!busy);
    m_forgotBtn->setEnabled(!busy);
    m_linkBtn->setEnabled(!busy);
    qInfo() << "AccountSignInPanel: auth busy=" << busy;
}

bool AccountSignInPanel::requireEmail(QString *errorCode) const
{
    const QString email = m_email->text().trimmed();
    if (email.isEmpty()) {
        *errorCode = QStringLiteral("AUTH_EMAIL_REQUIRED");
        return false;
    }
    if (!looksLikeEmail(email)) {
        *errorCode = QStringLiteral("INVALID_EMAIL");
        return false;
    }
    return true;
}

bool AccountSignInPanel::requireEmailPassword(QString *errorCode) const
{
    if (!requireEmail(errorCode)) {
        return false;
    }
    if (m_password->text().isEmpty()) {
        *errorCode = QStringLiteral("AUTH_PASSWORD_REQUIRED");
        return false;
    }
    return true;
}

void AccountSignInPanel::showAuthError(const QString &code)
{
    if (code == QLatin1String("GOOGLE_SIGN_IN_CANCELLED")) {
        qInfo() << "AccountSignInPanel: Google cancelled";
        return;
    }
    const QString text = ErrorCatalog::message(code);
    m_authStatus->setText(text);
    QMessageBox::warning(this, QStringLiteral("SeenShot"), text);
}

void AccountSignInPanel::signIn()
{
    QString error;
    if (!requireEmailPassword(&error)) {
        showAuthError(error);
        return;
    }
    setAuthBusy(true);
    if (!m_auth->signInEmail(m_email->text().trimmed(), m_password->text(), &error)) {
        setAuthBusy(false);
        showAuthError(error);
        return;
    }
    m_password->clear();
    setAuthBusy(false);
    qInfo() << "AccountSignInPanel: signed in";
}

void AccountSignInPanel::createAccount()
{
    QString error;
    if (!requireEmailPassword(&error)) {
        showAuthError(error);
        return;
    }
    setAuthBusy(true);
    if (!m_auth->signUpEmail(m_email->text().trimmed(), m_password->text(), &error)) {
        setAuthBusy(false);
        showAuthError(error);
        return;
    }
    m_password->clear();
    setAuthBusy(false);
    qInfo() << "AccountSignInPanel: account created";
}

void AccountSignInPanel::forgotPassword()
{
    QString error;
    if (!requireEmail(&error)) {
        showAuthError(error);
        return;
    }
    setAuthBusy(true);
    if (!m_auth->sendPasswordReset(m_email->text().trimmed(), &error)) {
        setAuthBusy(false);
        showAuthError(error);
        return;
    }
    setAuthBusy(false);
    m_authStatus->setText(ErrorCatalog::message(QStringLiteral("AUTH_CHECK_EMAIL")));
    qInfo() << "AccountSignInPanel: password reset sent";
}

void AccountSignInPanel::sendEmailLink()
{
    QString error;
    if (!requireEmail(&error)) {
        showAuthError(error);
        return;
    }
    setAuthBusy(true);
    if (!m_auth->sendEmailLink(m_email->text().trimmed(), &error)) {
        setAuthBusy(false);
        showAuthError(error);
        return;
    }
    setAuthBusy(false);
    m_authStatus->setText(ErrorCatalog::message(QStringLiteral("AUTH_CHECK_EMAIL")));
    qInfo() << "AccountSignInPanel: email link sent";
}

void AccountSignInPanel::signInGoogle()
{
    QString error;
    setAuthBusy(true);
    if (!m_auth->signInGoogle(&error)) {
        setAuthBusy(false);
        showAuthError(error);
        return;
    }
    setAuthBusy(false);
    qInfo() << "AccountSignInPanel: Google signed in";
}
