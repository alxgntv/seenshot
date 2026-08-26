#include "app/SettingsWindow.h"

#include "app/AccountSignInPanel.h"
#include "app/MacLoginItem.h"
#include "auth/AuthSession.h"
#include "cloud/CloudClient.h"
#include "errors/ErrorCatalog.h"
#include "local/LocalStore.h"

#include <QApplication>
#include <QCheckBox>
#include <QDebug>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QShowEvent>
#include <QSizePolicy>
#include <QUrl>
#include <QVBoxLayout>

// ─── Ariadne's Thread [AT-0085] ─────────────────────
// What: Settings Capture always; Account is signed-out or signed-in
// Why:  PRD-04 — Sign Out / Pro / Export / Delete must not show while signed out
// Date: 2026-08-25
// Related: [AT-0084] AuthSession.cpp, docs/PRD-04-settings-auth.md
// ─────────────────────────────────────────────────────
SettingsWindow::SettingsWindow(AuthSession *auth, CloudClient *cloud, QWidget *parent)
    : QWidget(parent)
    , m_auth(auth)
    , m_cloud(cloud)
{
    setWindowTitle(QStringLiteral("SeenShot Settings"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(16);

    auto *capture = new QGroupBox(QStringLiteral("Capture"), this);
    auto *captureForm = new QFormLayout(capture);
    captureForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    captureForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    captureForm->setHorizontalSpacing(12);
    captureForm->setVerticalSpacing(10);
    m_fullScreenHotkey = new QKeySequenceEdit(capture);
    m_pathHotkey = new QKeySequenceEdit(capture);
    m_fullScreenHotkey->setClearButtonEnabled(false);
    m_pathHotkey->setClearButtonEnabled(false);
    m_fullScreenHotkey->setMaximumSequenceLength(1);
    m_pathHotkey->setMaximumSequenceLength(1);
    m_fullScreenHotkey->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_pathHotkey->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // ─── Ariadne's Thread [AT-0107] ─────────────────────
    // What: Hotkey fields take focus only on click; show macOS focus rect
    // Why:  QKeySequenceEdit is first StrongFocus child and records keys with no visible ring
    // Date: 2026-08-26
    // Related: [AT-0085] SettingsWindow.cpp, docs/PRD-04-settings-auth.md
    // ─────────────────────────────────────────────────────
    m_fullScreenHotkey->setFocusPolicy(Qt::ClickFocus);
    m_pathHotkey->setFocusPolicy(Qt::ClickFocus);
    m_fullScreenHotkey->setAttribute(Qt::WA_MacShowFocusRect, true);
    m_pathHotkey->setAttribute(Qt::WA_MacShowFocusRect, true);
    captureForm->addRow(QStringLiteral("Full Screen Shot"), m_fullScreenHotkey);
    captureForm->addRow(QStringLiteral("Path Screen Shot"), m_pathHotkey);
    m_launchAtLogin = new QCheckBox(QStringLiteral("Open SeenShot at login"), capture);
    captureForm->addRow(QString(), m_launchAtLogin);
    connect(m_fullScreenHotkey, &QKeySequenceEdit::editingFinished, this, &SettingsWindow::applyHotkeys);
    connect(m_pathHotkey, &QKeySequenceEdit::editingFinished, this, &SettingsWindow::applyHotkeys);
    connect(m_launchAtLogin, &QCheckBox::toggled, this, &SettingsWindow::onLaunchAtLoginToggled);
    layout->addWidget(capture);

    // ─── Ariadne's Thread [AT-0087] ─────────────────────
    // What: Account signed-out is a stacked login: Google, then email fields, one Sign In
    // Why:  QFormLayout left 80px fields and four equal buttons; looked broken
    // Date: 2026-08-25
    // Related: [AT-0085] SettingsWindow.cpp, docs/PRD-04-settings-auth.md
    // ─────────────────────────────────────────────────────
    m_signedOutBox = new QGroupBox(QStringLiteral("Account"), this);
    auto *outLayout = new QVBoxLayout(m_signedOutBox);
    outLayout->setSpacing(10);
    m_signInPanel = new AccountSignInPanel(m_auth, m_signedOutBox);
    outLayout->addWidget(m_signInPanel);
    layout->addWidget(m_signedOutBox);

    m_signedInBox = new QGroupBox(QStringLiteral("Account"), this);
    auto *inLayout = new QVBoxLayout(m_signedInBox);
    inLayout->setSpacing(10);
    m_profile = new QLabel(m_signedInBox);
    m_profile->setWordWrap(true);
    inLayout->addWidget(m_profile);
    m_signOutBtn = new QPushButton(QStringLiteral("Sign Out"), m_signedInBox);
    m_proBtn = new QPushButton(QStringLiteral("Upgrade to Pro"), m_signedInBox);
    m_exportBtn = new QPushButton(QStringLiteral("Export my data"), m_signedInBox);
    m_deleteBtn = new QPushButton(QStringLiteral("Delete account"), m_signedInBox);
    m_signOutBtn->setMinimumHeight(32);
    m_proBtn->setMinimumHeight(32);
    inLayout->addWidget(m_signOutBtn);
    inLayout->addWidget(m_proBtn);
    inLayout->addWidget(m_exportBtn);
    inLayout->addWidget(m_deleteBtn);
    layout->addWidget(m_signedInBox);

    connect(m_signOutBtn, &QPushButton::clicked, this, &SettingsWindow::signOut);
    connect(m_exportBtn, &QPushButton::clicked, this, &SettingsWindow::exportData);
    connect(m_deleteBtn, &QPushButton::clicked, this, &SettingsWindow::deleteAccount);
    connect(m_proBtn, &QPushButton::clicked, this, [this]() {
        QString url;
        QString error;
        if (!m_cloud->createCheckoutUrl(&url, &error)) {
            showAuthError(error);
            return;
        }
        qInfo() << "SettingsWindow: open checkout";
        QDesktopServices::openUrl(QUrl(url));
    });
    connect(m_auth, &AuthSession::sessionChanged, this, &SettingsWindow::onSessionChanged);

    setMinimumWidth(420);
    setMaximumWidth(480);
    loadHotkeys();
    loadLaunchAtLogin();
    updateAccountUi();
    adjustSize();
    qInfo() << "SettingsWindow: constructed signedIn=" << m_auth->hasSession();
}

void SettingsWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    loadHotkeys();
    loadLaunchAtLogin();
    refreshQuota();
    m_fullScreenHotkey->clearFocus();
    m_pathHotkey->clearFocus();
    const QWidget *focus = QApplication::focusWidget();
    qInfo() << "SettingsWindow: showEvent hotkeyFocus cleared focus="
            << (focus ? QLatin1String(focus->metaObject()->className()) : QLatin1String("none"));
}

void SettingsWindow::loadHotkeys()
{
    const QString fullSpec = LocalStore::fullScreenHotkeySpec();
    const QString pathSpec = LocalStore::hotkeySpec();
    const QKeySequence fullSeq = LocalStore::keySequenceFromSpec(fullSpec);
    const QKeySequence pathSeq = LocalStore::keySequenceFromSpec(pathSpec);
    m_fullScreenHotkey->setKeySequence(fullSeq);
    m_pathHotkey->setKeySequence(pathSeq);
    qInfo() << "SettingsWindow: loaded hotkeys full=" << fullSpec << " seq=" << fullSeq.toString(QKeySequence::NativeText)
            << " path=" << pathSpec << " seq=" << pathSeq.toString(QKeySequence::NativeText);
}

void SettingsWindow::loadLaunchAtLogin()
{
    m_syncingLaunch = true;
    const bool on = MacLoginItem::isEnabled();
    m_launchAtLogin->setChecked(on);
    m_syncingLaunch = false;
    qInfo() << "SettingsWindow: launch at login=" << on;
}

void SettingsWindow::onLaunchAtLoginToggled(bool on)
{
    if (m_syncingLaunch) {
        return;
    }
    QString error;
    if (!MacLoginItem::setEnabled(on, &error)) {
        showAuthError(error);
        loadLaunchAtLogin();
        return;
    }
    qInfo() << "SettingsWindow: launch at login set=" << on;
}

void SettingsWindow::applyHotkeys()
{
    const QString fullSpec = LocalStore::specFromKeySequence(m_fullScreenHotkey->keySequence());
    const QString pathSpec = LocalStore::specFromKeySequence(m_pathHotkey->keySequence());
    qInfo() << "SettingsWindow: applyHotkeys full=" << fullSpec << " path=" << pathSpec;
    if (fullSpec.isEmpty() || pathSpec.isEmpty()) {
        qWarning() << "SettingsWindow: empty hotkey, reload stored";
        loadHotkeys();
        QMessageBox::warning(this, QStringLiteral("SeenShot"),
                             ErrorCatalog::message(QStringLiteral("HOTKEY_IN_USE")));
        return;
    }
    if (fullSpec == pathSpec) {
        qWarning() << "SettingsWindow: hotkeys collide";
        loadHotkeys();
        QMessageBox::warning(this, QStringLiteral("SeenShot"),
                             ErrorCatalog::message(QStringLiteral("HOTKEY_IN_USE")));
        return;
    }
    LocalStore::setFullScreenHotkeySpec(fullSpec);
    LocalStore::setHotkeySpec(pathSpec);
    emit hotkeysChanged();
}

void SettingsWindow::updateAccountUi()
{
    const bool in = m_auth->hasSession();
    m_signedOutBox->setVisible(!in);
    m_signedInBox->setVisible(in);
    if (!in) {
        adjustSize();
        qInfo() << "SettingsWindow: show signed-out account";
        return;
    }
    QString text = QStringLiteral("Signed in as %1").arg(m_auth->email().isEmpty() ? m_auth->uid() : m_auth->email());
    m_profile->setText(text);
    adjustSize();
    qInfo() << "SettingsWindow: show signed-in account emailChars=" << m_auth->email().size();
}

void SettingsWindow::onSessionChanged()
{
    qInfo() << "SettingsWindow: sessionChanged hasSession=" << m_auth->hasSession();
    updateAccountUi();
    refreshQuota();
}

void SettingsWindow::refreshQuota()
{
    updateAccountUi();
    if (!m_auth->hasSession() || !m_auth->isOnline()) {
        qInfo() << "SettingsWindow: skip quota offline or signed out";
        return;
    }
    int used = 0;
    QString plan;
    QString error;
    if (!m_cloud->fetchQuota(&used, &plan, &error)) {
        qWarning() << "SettingsWindow: quota failed" << error;
        if (error == QLatin1String("PRO_GRACE_ENDED")) {
            QMessageBox::information(this, QStringLiteral("SeenShot"), ErrorCatalog::message(error));
        }
        return;
    }
    const QString who = m_auth->email().isEmpty() ? m_auth->uid() : m_auth->email();
    m_profile->setText(QStringLiteral("Signed in as %1\nPlan: %2. Cloud used: %3 / 10 MB.")
                           .arg(who, plan, QString::number(used / 1024.0 / 1024.0, 'f', 2)));
}

void SettingsWindow::showAuthError(const QString &code)
{
    if (code == QLatin1String("GOOGLE_SIGN_IN_CANCELLED")) {
        qInfo() << "SettingsWindow: Google cancelled";
        return;
    }
    const QString text = ErrorCatalog::message(code);
    QMessageBox::warning(this, QStringLiteral("SeenShot"), text);
}

void SettingsWindow::signOut()
{
    m_auth->signOut();
    if (m_signInPanel) {
        m_signInPanel->clearSecrets();
    }
    qInfo() << "SettingsWindow: signed out";
}

void SettingsWindow::exportData()
{
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export data"),
                                                      QStringLiteral("seenshot-export.json"),
                                                      QStringLiteral("JSON (*.json)"));
    if (path.isEmpty()) {
        return;
    }
    QString error;
    if (!m_cloud->exportAccount(path, &error)) {
        QMessageBox::warning(this, QStringLiteral("SeenShot"), ErrorCatalog::message(error));
        return;
    }
    QMessageBox::information(this, QStringLiteral("SeenShot"), QStringLiteral("Export saved."));
}

void SettingsWindow::deleteAccount()
{
    if (QMessageBox::question(this, QStringLiteral("SeenShot"),
                              QStringLiteral("Delete your account and all cloud screenshots? This cannot be undone."))
        != QMessageBox::Yes) {
        return;
    }
    QString error;
    if (!m_cloud->deleteAccount(&error)) {
        QMessageBox::warning(this, QStringLiteral("SeenShot"), ErrorCatalog::message(error));
        return;
    }
    m_auth->signOut();
    QMessageBox::information(this, QStringLiteral("SeenShot"), ErrorCatalog::message(QStringLiteral("ACCOUNT_DELETED")));
}
