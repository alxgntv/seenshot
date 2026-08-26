#include "app/Application.h"

#include "annotate/AnnotateWindow.h"
#include "app/Analytics.h"
#include "app/Config.h"
#include "app/MacLoginItem.h"
#include "app/MacPermissions.h"
#include "app/MacUrlHandler.h"
#include "app/FirstRunWizard.h"
#include "app/SettingsWindow.h"
#include "app/TrayController.h"
#include "auth/AuthSession.h"
#include "auth/KeychainStore.h"
#include "capture/RegionPicker.h"
#include "capture/ScreenCaptureBackend.h"
#include "cloud/CloudClient.h"
#include "errors/ErrorCatalog.h"
#include "hotkey/MacHotkeyBackend.h"
#include "local/LocalStore.h"
#include "update/SparkleUpdater.h"

#include <QApplication>
#include <QEvent>
#include <QCheckBox>
#include <QCursor>
#include <QDebug>
#include <QGuiApplication>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkInformation>
#include <QFileOpenEvent>
#include <QHash>
#include <QJsonObject>
#include <QPushButton>
#include <QScreen>
#include <QUrl>

Application::Application(QObject *parent)
    : QObject(parent)
{
    QNetworkInformation::loadDefaultBackend();
    m_nam = new QNetworkAccessManager(this);
    m_store = new KeychainStore();
    m_auth = new AuthSession(m_store, m_nam, this);
    m_cloud = new CloudClient(m_auth, m_nam);
    m_hotkeys = new MacHotkeyBackend(this);
    m_tray = new TrayController(this);
    connect(m_tray, &TrayController::captureRequested, this, &Application::beginCapture);
    connect(m_tray, &TrayController::fullScreenCaptureRequested, this, &Application::beginFullScreenCapture);
    connect(m_tray, &TrayController::settingsRequested, this, &Application::openSettings);
    connect(m_tray, &TrayController::quitRequested, this, &Application::confirmQuit);
    connect(m_hotkeys, &MacHotkeyBackend::pathCaptureTriggered, this, &Application::beginCapture);
    connect(m_hotkeys, &MacHotkeyBackend::fullScreenCaptureTriggered, this,
            &Application::beginFullScreenCapture);
    qInfo() << "Application: constructed firebaseApiKeyChars=" << Config::firebaseApiKey().size()
            << " firebaseProjectId=" << Config::firebaseProjectId()
            << " authDomain=" << Config::firebaseAuthDomain()
            << " appIdChars=" << Config::firebaseAppId().size()
            << " googleOAuthClientIdChars=" << Config::googleOAuthClientId().size();
}

Application::~Application()
{
    delete m_store;
}

void Application::start()
{
    qInfo() << "Application: start firstRun=" << !LocalStore::firstRunCompleted();
    qApp->installEventFilter(this);
    MacUrlHandler::install([this](const QUrl &url) {
        handleOpenUrl(url);
    });
    QString loginError;
    if (!MacLoginItem::ensureEnabled(&loginError)) {
        qWarning() << "Application: login item default-on failed" << loginError;
    }
    if (!LocalStore::firstRunCompleted()) {
        FirstRunWizard wizard;
        if (wizard.exec() == QDialog::Accepted) {
            LocalStore::setHotkeySpec(wizard.selectedHotkey());
            LocalStore::setFirstRunCompleted();
        } else {
            LocalStore::setFirstRunCompleted();
        }
    }
    applyHotkeys();
    m_tray->show();
    restoreEditorIfNeeded();
}

// ─── Ariadne's Thread [AT-0094] ─────────────────────
// What: Restore persisted annotate session after Sparkle relaunch
// Why:  PRD-05 — one restore, then clear so a stale persist cannot loop
// Date: 2026-08-25
// Related: [AT-0091] LocalStore.cpp, [AT-0093] AnnotateWindow.cpp:restoreSession
// ─────────────────────────────────────────────────────
void Application::restoreEditorIfNeeded()
{
    if (!LocalStore::hasEditorSession()) {
        qInfo() << "Application: no persisted editor session";
        return;
    }
    QJsonObject json;
    QImage shot;
    QHash<QString, QImage> assets;
    QString error;
    if (!LocalStore::readEditorSession(&json, &shot, &assets, &error) || shot.isNull()) {
        qWarning() << "Application: persisted editor unreadable" << error;
        LocalStore::clearEditorSession();
        return;
    }
    LocalStore::clearEditorSession();
    qInfo() << "Application: restoring editor shot=" << shot.size() << " assets=" << assets.size();
    showAnnotate(shot);
    if (m_editor && !m_editor->restoreSession(json, assets, &error)) {
        qWarning() << "Application: restoreSession failed" << error;
    }
}

void Application::setCaptureFlag(bool capturing)
{
    m_capturing = capturing;
    qInfo() << "Application: captureFlag=" << capturing;
    if (SparkleUpdater *updater = SparkleUpdater::instance()) {
        updater->setCaptureInProgress(capturing);
    }
}

void Application::applyHotkeys()
{
    QString error;
    const QString pathSpec = LocalStore::hotkeySpec();
    const QString fullSpec = LocalStore::fullScreenHotkeySpec();
    qInfo() << "Application: applyHotkeys path=" << pathSpec << " full=" << fullSpec;
    if (!m_hotkeys->registerHotkeys(pathSpec, fullSpec, &error)) {
        QMessageBox::warning(nullptr, QStringLiteral("SeenShot"), ErrorCatalog::message(error));
    }
    m_tray->refreshCaptureShortcuts(fullSpec, pathSpec);
}

// ─── Ariadne's Thread [AT-0038] ─────────────────────
// What: Ask Screen Recording once per process; do not start capture until CGPreflight is true
// Why:  Settings toggle ON does not apply to the already-running process; old flow opened 3 dialogs every shot
// Date: 2026-08-25
// Related: [AT-0025] Application.cpp:beginCapture, [AT-0035] MacPermissions.mm
// ─────────────────────────────────────────────────────
bool Application::ensureScreenRecording()
{
    if (MacPermissions::hasScreenRecording()) {
        qInfo() << "Application: screen recording granted";
        return true;
    }
    if (!m_requestedScreenRecording) {
        m_requestedScreenRecording = true;
        qInfo() << "Application: requesting screen recording once this process";
        MacPermissions::requestScreenRecording();
        if (MacPermissions::hasScreenRecording()) {
            qInfo() << "Application: screen recording granted after request";
            return true;
        }
        qInfo() << "Application: system Screen Recording prompt shown, wait for toggle then Quit";
        return false;
    }
    qInfo() << "Application: skip CGRequestScreenCaptureAccess already asked this process";
    if (!m_promptedScreenRecording) {
        m_promptedScreenRecording = true;
        qInfo() << "Application: one-time Screen Recording prompt";
        MacPermissions::openScreenRecordingSettings();
        MacPermissions::activateApp();
        QMessageBox box;
        box.setWindowTitle(QStringLiteral("SeenShot"));
        box.setText(ErrorCatalog::message(QStringLiteral("SCREEN_RECORDING_DENIED")));
        auto *quitButton = box.addButton(QStringLiteral("Quit"), QMessageBox::AcceptRole);
        box.addButton(QStringLiteral("OK"), QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() == quitButton) {
            qInfo() << "Application: quit so Screen Recording can apply to the next process";
            qApp->quit();
        }
    } else {
        qInfo() << "Application: skip Screen Recording prompt already shown this process";
    }
    return false;
}

bool Application::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == qApp && event->type() == QEvent::Quit) {
        qInfo() << "Application: Quit event, teardown windows first";
        teardownNativeWindows();
    }
    if (event->type() == QEvent::FileOpen) {
        const auto *open = static_cast<QFileOpenEvent *>(event);
        qInfo() << "Application: FileOpen" << open->url().toString(QUrl::RemoveQuery);
        handleOpenUrl(open->url());
        return true;
    }
    return QObject::eventFilter(watched, event);
}

void Application::handleOpenUrl(const QUrl &url)
{
    qInfo() << "Application: handleOpenUrl scheme=" << url.scheme() << " host=" << url.host()
            << " path=" << url.path();
    if (url.scheme() != QLatin1String("seenshot")) {
        qInfo() << "Application: ignore non-seenshot url";
        return;
    }
    const QString dest = url.host().isEmpty() ? url.path() : url.host();
    if (dest == QLatin1String("oauth") || dest == QLatin1String("/oauth")) {
        qInfo() << "Application: oauth callback handled by ASWebAuthenticationSession";
        return;
    }
    QString error;
    if (!m_auth->completeEmailLink(url, &error)) {
        qWarning() << "Application: email link failed" << error;
        MacPermissions::activateApp();
        QMessageBox::warning(nullptr, QStringLiteral("SeenShot"), ErrorCatalog::message(error));
        return;
    }
    qInfo() << "Application: email link finished hasSession=" << m_auth->hasSession();
    openSettings();
}

void Application::teardownNativeWindows()
{
    if (m_editor) {
        qInfo() << "Application: teardown editor photo + close";
        m_editor->abortPhoto();
        m_editor->hide();
        m_editor->close();
    }
    if (m_picker) {
        qInfo() << "Application: teardown region picker";
        m_picker->hide();
        m_picker->deleteLater();
        m_picker = nullptr;
    }
}

// ─── Ariadne's Thread [AT-0119] ─────────────────────
// What: Restart region picker on a second path hotkey
// Why:  Invisible Tool overlay left capturing=true; later hotkeys no-op
// Date: 2026-08-26
// Related: [AT-0118] MacPermissions.mm:pinCaptureOverlay, [AT-0010] RegionPicker.cpp
// ─────────────────────────────────────────────────────
void Application::beginCapture()
{
    if (m_capturing) {
        if (m_picker) {
            qInfo() << "Application: path hotkey while picker open, discard overlay and start again";
            m_picker->hide();
            m_picker->disconnect();
            delete m_picker;
            m_picker = nullptr;
            setCaptureFlag(false);
        } else {
            qWarning() << "Application: capture already in progress";
            return;
        }
    }
    qInfo() << "Application: beginCapture";
    if (m_editor) {
        qInfo() << "Application: abort photo before region picker";
        m_editor->abortPhoto();
    }
    if (!ensureScreenRecording()) {
        qWarning() << "Application: capture blocked until Screen Recording applies after Quit";
        return;
    }
    setCaptureFlag(true);
    if (m_picker) {
        m_picker->deleteLater();
        m_picker = nullptr;
    }
    m_picker = new RegionPicker();
    connect(m_picker, &RegionPicker::regionPicked, this, &Application::onRegionPicked);
    connect(m_picker, &RegionPicker::cancelled, this, [this]() {
        qInfo() << "Application: capture cancelled";
        setCaptureFlag(false);
        if (m_picker) {
            m_picker->deleteLater();
        }
    });
    MacPermissions::activateApp();
    m_picker->show();
    m_picker->raise();
    m_picker->activateWindow();
    MacPermissions::pinCaptureOverlay(m_picker);
    qInfo() << "Application: picker shown visible=" << m_picker->isVisible()
            << " geo=" << m_picker->geometry() << " active=" << m_picker->isActiveWindow();
}

void Application::beginFullScreenCapture()
{
    if (m_capturing) {
        if (m_picker) {
            qInfo() << "Application: full screen while picker open, discard overlay";
            m_picker->hide();
            m_picker->disconnect();
            delete m_picker;
            m_picker = nullptr;
            setCaptureFlag(false);
        } else {
            qWarning() << "Application: full screen ignored, capture already in progress";
            return;
        }
    }
    qInfo() << "Application: beginFullScreenCapture";
    if (m_editor) {
        qInfo() << "Application: abort photo before full screen capture";
        m_editor->abortPhoto();
    }
    if (!ensureScreenRecording()) {
        qWarning() << "Application: full screen blocked until Screen Recording applies after Quit";
        return;
    }
    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        qWarning() << "Application: no screen for full capture";
        return;
    }
    setCaptureFlag(true);
    qInfo() << "Application: full screen" << screen->name() << screen->geometry();
    captureRect(screen->geometry(), QStringLiteral("fullscreen"));
}

void Application::onRegionPicked(const QRect &rect)
{
    qInfo() << "Application: region" << rect;
    if (m_picker) {
        m_picker->hide();
        m_picker->deleteLater();
        m_picker = nullptr;
        qInfo() << "Application: picker hidden before capture, no processEvents";
    }
    captureRect(rect, QStringLiteral("region"));
}

// ─── Ariadne's Thread [AT-0105] ─────────────────────
// What: Track capture after a successful region or full-screen shot
// Why:  PRD-06 — kind only, no image path
// Date: 2026-08-26
// Related: [AT-0102] Analytics.cpp:track, [AT-0025] Application.cpp:beginCapture
// ─────────────────────────────────────────────────────
void Application::captureRect(const QRect &rect, const QString &kind)
{
    qInfo() << "Application: captureRect" << rect << " kind=" << kind;
    ScreenCaptureBackend backend;
    QString error;
    const QImage image = backend.captureRegion(rect, &error);
    setCaptureFlag(false);
    if (image.isNull()) {
        qWarning() << "Application: capture failed" << error;
        if (error == QLatin1String("SCREEN_RECORDING_DENIED")) {
            ensureScreenRecording();
            return;
        }
        MacPermissions::activateApp();
        QMessageBox::warning(nullptr, QStringLiteral("SeenShot"), ErrorCatalog::message(error));
        return;
    }
    Analytics::instance().track(QStringLiteral("capture"), {{QStringLiteral("kind"), kind}});
    showAnnotate(image);
}

void Application::showAnnotate(const QImage &image)
{
    if (m_editor) {
        m_editor->abortPhoto();
        m_editor->close();
        m_editor->deleteLater();
    }
    m_editor = new AnnotateWindow(image, m_auth, m_cloud);
    m_editor->setAttribute(Qt::WA_DeleteOnClose);
    if (SparkleUpdater *updater = SparkleUpdater::instance()) {
        connect(m_editor, &AnnotateWindow::updateRequested, updater, &SparkleUpdater::userChoseUpdate);
        connect(m_editor, &AnnotateWindow::photoCycleEnded, updater, &SparkleUpdater::retryPendingInstall);
        updater->attachEditor(m_editor);
    }
    MacPermissions::activateApp();
    m_editor->show();
    m_editor->raise();
    m_editor->activateWindow();
    qInfo() << "Application: annotate window shown";
}

void Application::openSettings()
{
    qInfo() << "Application: openSettings";
    if (!m_settings) {
        m_settings = new SettingsWindow(m_auth, m_cloud);
        m_settings->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_settings, &SettingsWindow::hotkeysChanged, this, &Application::applyHotkeys);
    }
    m_settings->show();
    m_settings->raise();
}

void Application::confirmQuit()
{
    qInfo() << "Application: confirmQuit hasSession=" << m_auth->hasSession();
    if (m_auth->hasSession()) {
        QMessageBox box;
        box.setWindowTitle(QStringLiteral("SeenShot"));
        box.setText(QStringLiteral("Quit SeenShot?"));
        auto *check = new QCheckBox(QStringLiteral("Also delete my SeenShot cloud data"));
        box.setCheckBox(check);
        box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        if (box.exec() != QMessageBox::Yes) {
            return;
        }
        if (check->isChecked()) {
            QString error;
            if (!m_cloud->deleteAccount(&error)) {
                QMessageBox::warning(nullptr, QStringLiteral("SeenShot"), ErrorCatalog::message(error));
                return;
            }
            m_auth->signOut();
            QMessageBox::information(nullptr, QStringLiteral("SeenShot"),
                                     ErrorCatalog::message(QStringLiteral("ACCOUNT_DELETED")));
        }
    }
    if (SparkleUpdater *updater = SparkleUpdater::instance()) {
        updater->persistBeforeQuit();
    }
    teardownNativeWindows();
    Analytics::instance().shutdown();
    qApp->quit();
}
