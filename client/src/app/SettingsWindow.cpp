#include "app/SettingsWindow.h"

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
    // ─── Ariadne's Thread [AT-0202] ─────────────────────
    // What: Settings does not participate in last-window-closed quit
    // Why:  Closing Settings must leave the menu-bar agent running
    // Date: 2026-08-27
    // Related: [AT-0204] Application.cpp:eventFilter, [AT-0085] SettingsWindow.cpp
    // ─────────────────────────────────────────────────────
    setAttribute(Qt::WA_QuitOnClose, false);
    qInfo() << "SettingsWindow: WA_QuitOnClose=" << testAttribute(Qt::WA_QuitOnClose);
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
    // ─── Ariadne's Thread [AT-0319] ─────────────────────
    // What: Capture rows are Path then Full Screen Shot; Path is the UI name
    // Why:  Partial capture is Path everywhere, not Path Screen Shot
    // Date: 2026-08-28
    // Related: [AT-0318] FirstRunWizard.cpp, [AT-0320] TrayController.cpp
    // ─────────────────────────────────────────────────────
    captureForm->addRow(QStringLiteral("Path"), m_pathHotkey);
    captureForm->addRow(QStringLiteral("Full Screen Shot"), m_fullScreenHotkey);
    qInfo() << "SettingsWindow: capture rows Path then Full Screen Shot";
    m_launchAtLogin = new QCheckBox(QStringLiteral("Open SeenShot at login"), capture);
    captureForm->addRow(QString(), m_launchAtLogin);
    connect(m_fullScreenHotkey, &QKeySequenceEdit::editingFinished, this, &SettingsWindow::applyHotkeys);
    connect(m_pathHotkey, &QKeySequenceEdit::editingFinished, this, &SettingsWindow::applyHotkeys);
    connect(m_launchAtLogin, &QCheckBox::toggled, this, &SettingsWindow::onLaunchAtLoginToggled);
    layout->addWidget(capture);

    // ─── Ariadne's Thread [AT-0198] ─────────────────────
    // What: Settings Sign In opens seenshot.app in the default browser
    // Why:  No intermediate Qt dialog; NSWorkspace uses the user's HTTP handler
    // Date: 2026-08-27
    // Related: [AT-0193] AuthSession.cpp:startWebsiteSignIn, [AT-0085] SettingsWindow.cpp
    // ─────────────────────────────────────────────────────
    m_signedOutBox = new QGroupBox(QStringLiteral("Account"), this);
    auto *outLayout = new QVBoxLayout(m_signedOutBox);
    outLayout->setSpacing(10);
    m_signInBtn = new QPushButton(QStringLiteral("Sign In"), m_signedOutBox);
    m_signInBtn->setMinimumHeight(32);
    outLayout->addWidget(m_signInBtn);
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

    connect(m_signInBtn, &QPushButton::clicked, this, &SettingsWindow::openSignIn);
    connect(m_auth, &AuthSession::websiteSignInSettled, this, &SettingsWindow::onWebsiteSignInSettled);
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

    layout->addStretch(1);
    // ─── Ariadne's Thread [AT-0122] ─────────────────────
    // What: Show the running app version on Settings
    // Why:  User must see which build is installed
    // Date: 2026-08-26
    // Related: [AT-0198] SettingsWindow.cpp:openSignIn, client/src/main.cpp
    // ─────────────────────────────────────────────────────
    const QString version = QApplication::applicationVersion();
    m_version = new QLabel(QStringLiteral("Version %1").arg(version), this);
    m_version->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_version);
    qInfo() << "SettingsWindow: version label" << version;

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
        m_signInBtn->setEnabled(!m_websiteSignInBusy);
        adjustSize();
        qInfo() << "SettingsWindow: show signed-out account busy=" << m_websiteSignInBusy;
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
    const QString text = ErrorCatalog::message(code);
    QMessageBox::warning(this, QStringLiteral("SeenShot"), text);
}

void SettingsWindow::openSignIn()
{
    qInfo() << "SettingsWindow: openSignIn hasSession=" << m_auth->hasSession()
            << " busy=" << m_websiteSignInBusy;
    if (!m_auth || m_auth->hasSession()) {
        qInfo() << "SettingsWindow: openSignIn skipped";
        return;
    }
    m_websiteSignInBusy = true;
    m_signInBtn->setEnabled(false);
    QString error;
    if (!m_auth->startWebsiteSignIn(&error)) {
        if (error == QLatin1String("AUTH_IN_PROGRESS")) {
            qInfo() << "SettingsWindow: website sign-in already in progress";
            return;
        }
        m_websiteSignInBusy = false;
        m_signInBtn->setEnabled(true);
        qWarning() << "SettingsWindow: website sign-in start failed code=" << error;
        showAuthError(error.isEmpty() ? QStringLiteral("AUTH_OAUTH_FAILED") : error);
        return;
    }
    if (m_auth->hasSession()) {
        m_websiteSignInBusy = false;
        m_signInBtn->setEnabled(false);
        qInfo() << "SettingsWindow: website sign-in skipped, already signed in";
        return;
    }
    m_websiteSignInBusy = false;
    m_signInBtn->setEnabled(true);
    qInfo() << "SettingsWindow: website sign-in started in default browser";
}

void SettingsWindow::onWebsiteSignInSettled(const QString &errorCode)
{
    m_websiteSignInBusy = false;
    if (m_signInBtn) {
        m_signInBtn->setEnabled(!m_auth->hasSession());
    }
    if (errorCode.isEmpty()) {
        qInfo() << "SettingsWindow: website sign-in settled ok hasSession=" << m_auth->hasSession();
        return;
    }
    if (errorCode == QLatin1String("AUTH_OAUTH_DENIED")) {
        qInfo() << "SettingsWindow: website sign-in canceled";
        return;
    }
    qWarning() << "SettingsWindow: website sign-in failed code=" << errorCode;
    showAuthError(errorCode);
}

void SettingsWindow::signOut()
{
    m_auth->signOut();
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
