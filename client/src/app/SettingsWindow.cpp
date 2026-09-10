#include "app/SettingsWindow.h"

#include "app/MacLoginItem.h"
#include "auth/AuthSession.h"
#include "cloud/CloudClient.h"
#include "errors/ErrorCatalog.h"
#include "local/LocalStore.h"

#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDebug>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QShowEvent>
#include <QSizePolicy>
#include <QTimer>
#include <QTextBrowser>
#include <QtMath>
#include <QUrl>
#include <QVBoxLayout>

namespace {

bool isMemberPlan(const QString &plan)
{
    return plan == QLatin1String("pro") || plan == QLatin1String("grace");
}

// ─── Ariadne's Thread [AT-0643] ─────────────────────
// What: Format quota bytes as 10 MB or 1 GB from /v1/quota limitBytes
// Why:  Settings always printed 10 MB, so Pro after redeem looked over cap
// Date: 2026-09-09
// Related: [AT-0642] CloudClient.cpp:fetchQuota, [AT-0279] backend→quota.ts:quotaLimitBytes
// ─────────────────────────────────────────────────────
QString formatQuotaBytes(int bytes)
{
    const qint64 gbUnit = 1024LL * 1024LL * 1024LL;
    const qint64 mbUnit = 1024LL * 1024LL;
    if (bytes >= gbUnit) {
        const double gb = static_cast<double>(bytes) / static_cast<double>(gbUnit);
        if (qAbs(gb - 1.0) < 0.005) {
            return QStringLiteral("1 GB");
        }
        return QStringLiteral("%1 GB").arg(gb, 0, 'f', 2);
    }
    const double mb = static_cast<double>(bytes) / static_cast<double>(mbUnit);
    if (qAbs(mb - 10.0) < 0.005) {
        return QStringLiteral("10 MB");
    }
    return QStringLiteral("%1 MB").arg(mb, 0, 'f', 2);
}

int fallbackQuotaLimit(const QString &plan)
{
    if (isMemberPlan(plan)) {
        return 1024 * 1024 * 1024;
    }
    return 10 * 1024 * 1024;
}

} // namespace

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
    // ─── Ariadne's Thread [AT-0645] ─────────────────────
    // What: Open bundled Credits.html from Settings
    // Why:  Sparkle, Qt, and posthog-cpp notices must ship in the app
    // Date: 2026-09-09
    // Related: [AT-0645] packaging/macos/Credits.html, https://sparkle-project.org/documentation/
    // ─────────────────────────────────────────────────────
    m_licensesBtn = new QPushButton(QStringLiteral("Licence"), this);
    m_licensesBtn->setMinimumHeight(32);
    connect(m_licensesBtn, &QPushButton::clicked, this, &SettingsWindow::openLicenses);
    layout->addWidget(m_licensesBtn);
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
    m_fullScreenHotkey->clearFocus();
    m_pathHotkey->clearFocus();
    const QWidget *focus = QApplication::focusWidget();
    qInfo() << "SettingsWindow: showEvent hotkeyFocus cleared focus="
            << (focus ? QLatin1String(focus->metaObject()->className()) : QLatin1String("none"));
    qInfo() << "SettingsWindow: queue refreshQuota after paint";
    QTimer::singleShot(0, this, [this]() {
        qInfo() << "SettingsWindow: deferred refreshQuota visible=" << isVisible();
        refreshQuota();
    });
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

void SettingsWindow::applyUpgradeVisibility(const QString &plan)
{
    const bool member = isMemberPlan(plan);
    if (m_proBtn) {
        m_proBtn->setVisible(!member);
    }
    qInfo() << "SettingsWindow: upgrade visible=" << !member << " plan=" << plan;
}

void SettingsWindow::updateAccountUi()
{
    const bool in = m_auth->hasSession();
    m_signedOutBox->setVisible(!in);
    m_signedInBox->setVisible(in);
    if (!in) {
        m_signInBtn->setEnabled(!m_websiteSignInBusy);
        applyUpgradeVisibility(QString());
        adjustSize();
        qInfo() << "SettingsWindow: show signed-out account busy=" << m_websiteSignInBusy;
        return;
    }
    QString text = QStringLiteral("Signed in as %1").arg(m_auth->email().isEmpty() ? m_auth->uid() : m_auth->email());
    m_profile->setText(text);
    const QString cachedPlan =
        (LocalStore::planUid() == m_auth->uid()) ? LocalStore::plan() : QString();
    applyUpgradeVisibility(cachedPlan);
    adjustSize();
    qInfo() << "SettingsWindow: show signed-in account emailChars=" << m_auth->email().size()
            << " cachedPlan=" << cachedPlan;
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
    if (!m_auth->hasSession() || !m_auth->isOnline() || !m_cloud) {
        qInfo() << "SettingsWindow: skip quota offline or signed out hasSession="
                << m_auth->hasSession() << " online=" << m_auth->isOnline()
                << " cloud=" << (m_cloud != nullptr);
        return;
    }
    const QString uid = m_auth->uid();
    QPointer<SettingsWindow> self = this;
    qInfo() << "SettingsWindow: quota fetch start uidChars=" << uid.size();
    m_cloud->fetchQuota([self, uid](bool ok, int used, const QString &plan, int limitBytes, const QString &error) {
        if (!self) {
            qWarning() << "SettingsWindow: quota reply after close ok=" << ok << " error=" << error;
            return;
        }
        qInfo() << "SettingsWindow: quota reply ok=" << ok << " used=" << used << " plan=" << plan
                << " limit=" << limitBytes << " error=" << error;
        self->applyFetchedQuota(ok, used, plan, limitBytes, error, uid);
    });
}

// ─── Ariadne's Thread [AT-0656] ─────────────────────
// What: Apply /v1/quota on the callback after Settings already showed the cached account
// Why:  Opening Settings must not wait for the Worker, including when the Mac is offline
// Date: 2026-09-10
// Related: [AT-0654] CloudClient.cpp:fetchQuota, [AT-0643] SettingsWindow.cpp:refreshQuota
// ─────────────────────────────────────────────────────
void SettingsWindow::applyFetchedQuota(bool ok, int used, const QString &plan, int limitBytes, const QString &error,
                                       const QString &uid)
{
    if (!m_auth->hasSession() || m_auth->uid() != uid) {
        qInfo() << "SettingsWindow: quota discarded session changed signedIn=" << m_auth->hasSession()
                << " uidMatch=" << (m_auth->uid() == uid);
        updateAccountUi();
        return;
    }
    if (!ok) {
        qWarning() << "SettingsWindow: quota failed" << error;
        if (error == QLatin1String("PRO_GRACE_ENDED")) {
            LocalStore::setPlan(m_auth->uid(), QStringLiteral("free"));
            applyUpgradeVisibility(QStringLiteral("free"));
            QMessageBox::information(this, QStringLiteral("SeenShot"), ErrorCatalog::message(error));
        }
        return;
    }
    LocalStore::setPlan(m_auth->uid(), plan);
    int shownLimit = limitBytes;
    if (shownLimit <= 0) {
        shownLimit = fallbackQuotaLimit(plan);
        qWarning() << "SettingsWindow: quota missing limitBytes, fallback plan=" << plan
                   << " limit=" << shownLimit;
    }
    applyUpgradeVisibility(plan);
    qInfo() << "SettingsWindow: cached plan=" << plan << " used=" << used << " limit=" << shownLimit
            << " member=" << isMemberPlan(plan);
    const QString who = m_auth->email().isEmpty() ? m_auth->uid() : m_auth->email();
    m_profile->setText(QStringLiteral("Signed in as %1\nPlan: %2. Cloud used: %3 / %4.")
                           .arg(who, plan, formatQuotaBytes(used), formatQuotaBytes(shownLimit)));
    adjustSize();
}

void SettingsWindow::showAuthError(const QString &code)
{
    const QString text = ErrorCatalog::message(code);
    QMessageBox::warning(this, QStringLiteral("SeenShot"), text);
}

void SettingsWindow::openLicenses()
{
    const QString path = QDir(QCoreApplication::applicationDirPath())
                             .filePath(QStringLiteral("../Resources/Credits.html"));
    QFile file(path);
    qInfo() << "SettingsWindow: open licenses path=" << path << " exists=" << file.exists()
            << " size=" << file.size();
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "SettingsWindow: licenses missing path=" << path;
        QMessageBox::warning(this, QStringLiteral("SeenShot"),
                             QStringLiteral("Could not open open-source licenses."));
        return;
    }
    const QByteArray html = file.readAll();
    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setAttribute(Qt::WA_QuitOnClose, false);
    dialog->setWindowTitle(QStringLiteral("Open-source licenses"));
    dialog->resize(560, 480);
    auto *box = new QVBoxLayout(dialog);
    auto *view = new QTextBrowser(dialog);
    view->setOpenExternalLinks(true);
    view->setHtml(QString::fromUtf8(html));
    box->addWidget(view);
    auto *closeBtn = new QPushButton(QStringLiteral("Close"), dialog);
    connect(closeBtn, &QPushButton::clicked, dialog, &QDialog::accept);
    box->addWidget(closeBtn);
    qInfo() << "SettingsWindow: licenses dialog htmlChars=" << html.size();
    dialog->exec();
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
