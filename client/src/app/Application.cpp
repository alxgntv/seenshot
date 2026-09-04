#include "app/Application.h"

#include "annotate/AnnotateWindow.h"
#include "app/Analytics.h"
#include "app/Config.h"
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
#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
#include <QUrl>
#include <QWidget>

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
            << " appIdChars=" << Config::firebaseAppId().size();
}

Application::~Application()
{
    delete m_store;
}

// ─── Ariadne's Thread [AT-0171] ─────────────────────
// What: Do not register SMAppService on launch
// Why:  Login Item prompt is extra TCC; only Screen Recording is required after install
// Date: 2026-08-26
// Related: [AT-0081] MacLoginItem.mm:setEnabled, [AT-0171] KeychainStore.cpp
// ─────────────────────────────────────────────────────
void Application::start()
{
    qInfo() << "Application: start firstRun=" << !LocalStore::firstRunCompleted()
            << " onboardingVersion=" << LocalStore::onboardingVersion()
            << " onboardingDone=" << LocalStore::onboardingCompleted()
            << " path=" << MacPermissions::runningAppPath();
    qApp->setQuitOnLastWindowClosed(false);
    qApp->installEventFilter(this);
    connect(qApp, &QGuiApplication::lastWindowClosed, this, []() {
        qInfo() << "Application: lastWindowClosed agent stays running quitOnLastWindowClosed="
                << qApp->quitOnLastWindowClosed();
    });
    MacUrlHandler::install([this](const QUrl &url) {
        handleOpenUrl(url);
    });
    // ─── Ariadne's Thread [AT-0321] ─────────────────────
    // What: Run onboarding on the next event-loop tick after start
    // Why:  Installer/pkg `open` launches the agent before NSApp's run loop; QWizard.exec in start hid the setup
    // Date: 2026-08-28
    // Related: [AT-0303] Application.cpp:finishLaunch, [AT-0322] packaging/macos/pkg/scripts/postinstall
    // ─────────────────────────────────────────────────────
    qInfo() << "Application: schedule finishLaunch on event loop";
    QTimer::singleShot(0, this, [this]() {
        qInfo() << "Application: finishLaunch timer fired";
        finishLaunch();
    });
}

void Application::finishLaunch()
{
    bool captureAfterWizard = false;
    // ─── Ariadne's Thread [AT-0303] ─────────────────────
    // What: Run 6-step FirstRunWizard when onboardingVersion < 1; Dock Regular then Accessory
    // Why:  firstRunCompleted must not skip setup; Take screenshot Finish starts path capture
    // Date: 2026-08-28
    // Related: [AT-0302] FirstRunWizard.cpp, [AT-0300] MacPermissions.mm:setDockVisible
    // ─────────────────────────────────────────────────────
    if (!LocalStore::onboardingCompleted()) {
        FirstRunWizard wizard;
        m_onboarding = &wizard;
        const bool dockOn = MacPermissions::setDockVisible(true);
        qInfo() << "Application: onboarding dockOn=" << dockOn;
        MacPermissions::activateApp();
        wizard.show();
        wizard.raise();
        wizard.activateWindow();
        const int result = wizard.exec();
        m_onboarding = nullptr;
        const bool accepted = result == QDialog::Accepted;
        qInfo() << "Application: onboarding result=" << result << " accepted=" << accepted;
        const bool dockOff = MacPermissions::setDockVisible(false);
        qInfo() << "Application: onboarding dockOff=" << dockOff;
        if (accepted) {
            LocalStore::setOnboardingCompleted();
            captureAfterWizard = true;
        } else {
            qInfo() << "Application: onboarding dismissed, will show again next launch";
        }
    } else {
        qInfo() << "Application: skip onboarding, already completed";
    }
    applyHotkeys();
    m_tray->show();
    if (captureAfterWizard) {
        if (LocalStore::hasEditorSession()) {
            qInfo() << "Application: skip first capture, persisted editor exists";
        } else {
            qInfo() << "Application: first capture after onboarding Finish";
            beginCapture();
        }
    }
    restoreEditorIfNeeded();
    qInfo() << "Application: finishLaunch done captureAfterWizard=" << captureAfterWizard;
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

// ─── Ariadne's Thread [AT-0176] ─────────────────────
// What: Grant capture via CGPreflight or ScreenCaptureKit; help dialog instead of looping CGRequest
// Why:  CGRequest every launch showed Open Settings while the switch belonged to another signed copy
// Date: 2026-08-26
// Related: [AT-0178] MacPermissions.mm:probeScreenRecording, [AT-0175] LocalStore.cpp:screenRecordingRegistered
// ─────────────────────────────────────────────────────
bool Application::ensureScreenRecording()
{
    if (m_ensuringScreenRecording) {
        qWarning() << "Application: ensureScreenRecording reentered capturing=" << m_capturing
                   << " path=" << MacPermissions::runningAppPath();
        return false;
    }
    m_ensuringScreenRecording = true;

    const bool preflight = MacPermissions::hasScreenRecording();
    qInfo() << "Application: ensureScreenRecording preflight=" << preflight
            << " registered=" << LocalStore::screenRecordingRegistered()
            << " requestedThisProcess=" << m_requestedScreenRecording
            << " path=" << MacPermissions::runningAppPath();
    if (preflight) {
        LocalStore::setScreenRecordingRegistered();
        m_ensuringScreenRecording = false;
        qInfo() << "Application: screen recording granted by CGPreflight";
        return true;
    }

    const bool probe = MacPermissions::probeScreenRecording();
    qInfo() << "Application: ensureScreenRecording probe=" << probe;
    if (probe) {
        LocalStore::setScreenRecordingRegistered();
        m_ensuringScreenRecording = false;
        qInfo() << "Application: screen recording granted by ScreenCaptureKit probe";
        return true;
    }

    if (!LocalStore::screenRecordingRegistered() && !m_requestedScreenRecording) {
        m_requestedScreenRecording = true;
        qInfo() << "Application: first TCC register via CGRequestScreenCaptureAccess";
        MacPermissions::requestScreenRecording();
        LocalStore::setScreenRecordingRegistered();
        if (MacPermissions::hasScreenRecording() || MacPermissions::probeScreenRecording()) {
            m_ensuringScreenRecording = false;
            qInfo() << "Application: screen recording granted after request";
            return true;
        }
        qInfo() << "Application: CGRequestScreenCaptureAccess did not authorize this copy";
    } else {
        qInfo() << "Application: skip CGRequestScreenCaptureAccess registered="
                << LocalStore::screenRecordingRegistered()
                << " requestedThisProcess=" << m_requestedScreenRecording;
    }

    qWarning() << "Application: Screen Recording not effective for this copy, showing help";
    showScreenRecordingHelp();
    m_ensuringScreenRecording = false;
    return false;
}

void Application::showScreenRecordingHelp()
{
    const QString path = MacPermissions::runningAppPath();
    qInfo() << "Application: showScreenRecordingHelp path=" << path;
    MacPermissions::activateApp();
    QMessageBox box;
    box.setWindowTitle(QStringLiteral("SeenShot"));
    box.setIcon(QMessageBox::Warning);
    box.setText(QStringLiteral("SeenShot needs Screen Recording to capture your screen."));
    box.setInformativeText(
        QStringLiteral("This copy:\n%1\n\n"
                       "Open System Settings → Privacy & Security → Screen Recording.\n\n"
                       "If the SeenShot switch is already on, turn it off, then turn it on again. "
                       "macOS keeps a separate switch for each signed copy of SeenShot. "
                       "The switch that is on may belong to another SeenShot.app on this Mac.\n\n"
                       "Then click Restart SeenShot.")
            .arg(path));
    QPushButton *openBtn = box.addButton(QStringLiteral("Open System Settings"), QMessageBox::AcceptRole);
    QPushButton *restartBtn = box.addButton(QStringLiteral("Restart SeenShot"), QMessageBox::ActionRole);
    box.addButton(QStringLiteral("Cancel"), QMessageBox::RejectRole);
    box.setDefaultButton(openBtn);
    box.exec();
    if (box.clickedButton() == openBtn) {
        qInfo() << "Application: Screen Recording help Open System Settings";
        MacPermissions::openScreenRecordingSettings();
        return;
    }
    if (box.clickedButton() == restartBtn) {
        qInfo() << "Application: Screen Recording help Restart SeenShot";
        MacPermissions::relaunchApp();
        return;
    }
    qInfo() << "Application: Screen Recording help cancelled";
}

// ─── Ariadne's Thread [AT-0204] ─────────────────────
// What: Ignore unsolicited QEvent::Quit from window close; accept tray/Cmd+Q/Sparkle
// Why:  Closing AnnotateWindow must not kill the LSUIElement agent
// Date: 2026-08-27
// Related: [AT-0205] MacPermissions.mm:shouldAcceptApplicationQuit, [AT-0202] AnnotateWindow.cpp
// ─────────────────────────────────────────────────────
bool Application::eventFilter(QObject *watched, QEvent *event)
{
    // ─── Ariadne's Thread [AT-0336] ─────────────────────
    // What: Raise AnnotateWindow when the app becomes active from Dock or Cmd+Tab
    // Why:  Regular policy shows the icon; the editor still needs to become key
    // Date: 2026-08-28
    // Related: [AT-0335] Application.cpp:showAnnotate, [AT-0300] MacPermissions.mm:setDockVisible
    // ─────────────────────────────────────────────────────
    if (event->type() == QEvent::ApplicationStateChange
        && QGuiApplication::applicationState() == Qt::ApplicationActive) {
        if (m_onboarding && !m_onboarding->isActiveWindow()) {
            qInfo() << "Application: raise onboarding from Dock active=" << m_onboarding->isActiveWindow();
            MacPermissions::activateApp();
            m_onboarding->raise();
            m_onboarding->activateWindow();
        } else if (m_picker && m_picker->isVisible()) {
            QWidget *modal = QApplication::activeModalWidget();
            qInfo() << "Application: skip raise annotate, path overlay visible modal="
                    << (modal ? modal->metaObject()->className() : "none");
            if (modal) {
                MacPermissions::pinAlertAboveCapture(modal);
            }
        } else if (m_editor && m_editor->isVisible()) {
            QWidget *modal = QApplication::activeModalWidget();
            if (modal) {
                qInfo() << "Application: skip raise annotate, modal=" << modal->metaObject()->className();
            } else if (!m_editor->isActiveWindow()) {
                qInfo() << "Application: raise annotate from Dock/Cmd+Tab visible=" << m_editor->isVisible()
                        << " active=" << m_editor->isActiveWindow();
                MacPermissions::activateApp();
                m_editor->raise();
                m_editor->activateWindow();
            } else {
                qInfo() << "Application: annotate already key after Dock/Cmd+Tab";
            }
        }
    }
    if (watched == qApp && event->type() == QEvent::Quit) {
        const bool accept = MacPermissions::shouldAcceptApplicationQuit();
        qInfo() << "Application: Quit event accept=" << accept
                << " quitOnLastWindowClosed=" << qApp->quitOnLastWindowClosed();
        for (QWidget *w : QApplication::topLevelWidgets()) {
            qInfo() << "Application: Quit topLevel class=" << w->metaObject()->className()
                    << " visible=" << w->isVisible()
                    << " quitOnClose=" << w->testAttribute(Qt::WA_QuitOnClose)
                    << " flags=" << int(w->windowFlags());
        }
        if (!accept) {
            qWarning() << "Application: ignore Quit so the menu-bar agent stays running";
            event->ignore();
            return true;
        }
        qInfo() << "Application: Quit accepted, teardown windows";
        teardownNativeWindows();
    }
    if (event->type() == QEvent::FileOpen) {
        const auto *open = static_cast<QFileOpenEvent *>(event);
        qInfo() << "Application: FileOpen" << open->url().toString(QUrl::RemoveQuery);
        handleOpenUrl(open->url());
        return true;
    }
    // ─── Ariadne's Thread [AT-0402] ─────────────────────
    // What: While the path overlay is up, lift QMessageBox above the shield window
    // Why:  Warnings opened under the overlay and could not be closed or captured past
    // Date: 2026-09-03
    // Related: [AT-0401] MacPermissions.mm:pinAlertAboveCapture, [AT-0403] RegionPicker.cpp:yieldInput
    // ─────────────────────────────────────────────────────
    if (auto *box = qobject_cast<QMessageBox *>(watched)) {
        if (event->type() == QEvent::Show || event->type() == QEvent::WindowActivate) {
            const bool pickerUp = m_picker && m_picker->isVisible();
            qInfo() << "Application: QMessageBox" << event->type() << "pickerUp=" << pickerUp
                    << " visible=" << box->isVisible() << " modal=" << box->isModal();
            if (pickerUp) {
                m_picker->yieldInput();
                MacPermissions::activateApp();
                MacPermissions::pinAlertAboveCapture(box);
                QPointer<QMessageBox> boxPtr = box;
                QTimer::singleShot(0, this, [this, boxPtr]() {
                    if (!boxPtr || !boxPtr->isVisible() || !m_picker || !m_picker->isVisible()) {
                        qInfo() << "Application: skip delayed pinAlert boxVisible="
                                << (boxPtr && boxPtr->isVisible()) << " picker="
                                << (m_picker && m_picker->isVisible());
                        return;
                    }
                    qInfo() << "Application: delayed pinAlertAboveCapture";
                    MacPermissions::pinAlertAboveCapture(boxPtr);
                    boxPtr->raise();
                    boxPtr->activateWindow();
                });
            }
        } else if (event->type() == QEvent::Hide) {
            const bool pickerUp = m_picker && m_picker->isVisible();
            qInfo() << "Application: QMessageBox Hide pickerUp=" << pickerUp;
            if (pickerUp) {
                m_picker->resumeInput();
                MacPermissions::pinCaptureOverlay(m_picker);
            }
        }
    }
    return QObject::eventFilter(watched, event);
}

// ─── Ariadne's Thread [AT-0315] ─────────────────────
// What: seenshot://oauth and email-link success do not open Settings
// Why:  Settings was shown as a signed-in confirmation after the website sent the user back
// Date: 2026-08-28
// Related: [AT-0193] AuthSession.cpp:completeWebsiteCallback, [AT-0198] SettingsWindow.cpp:openSignIn
// ─────────────────────────────────────────────────────
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
        QString error;
        const bool ok = m_auth->completeWebsiteCallback(url, &error);
        qInfo() << "Application: oauth callback ok=" << ok << " error=" << error
                << " hasSession=" << m_auth->hasSession();
        if (!ok && !error.isEmpty() && error != QLatin1String("AUTH_OAUTH_DENIED")) {
            MacPermissions::activateApp();
            QMessageBox::warning(nullptr, QStringLiteral("SeenShot"), ErrorCatalog::message(error));
            return;
        }
        qInfo() << "Application: oauth callback done, skip Settings";
        return;
    }
    QString error;
    if (!m_auth->completeEmailLink(url, &error)) {
        qWarning() << "Application: email link failed" << error;
        MacPermissions::activateApp();
        QMessageBox::warning(nullptr, QStringLiteral("SeenShot"), ErrorCatalog::message(error));
        return;
    }
    qInfo() << "Application: email link finished hasSession=" << m_auth->hasSession()
            << " skip Settings";
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
        qWarning() << "Application: capture blocked until Screen Recording TCC applies";
        return;
    }
    setCaptureFlag(true);
    if (m_picker) {
        m_picker->deleteLater();
        m_picker = nullptr;
    }
    // ─── Ariadne's Thread [AT-0409] ─────────────────────
    // What: Capture every screen before the path overlay, then crop the freeze
    // Why:  Overlay click closed HTML selects and menus before ScreenCaptureKit
    // Date: 2026-09-04
    // Related: [AT-0406] ScreenCaptureBackend.mm:captureRegion, [AT-0010] RegionPicker.cpp
    // ─────────────────────────────────────────────────────
    ScreenCaptureBackend backend;
    QList<CaptureFreezeFrame> frames;
    QString freezeError;
    const QList<QScreen *> screens = QGuiApplication::screens();
    qInfo() << "Application: freeze capture screens=" << screens.size()
            << " appActive=" << (QGuiApplication::applicationState() == Qt::ApplicationActive);
    for (QScreen *screen : screens) {
        if (!screen) {
            qWarning() << "Application: freeze skip null screen";
            continue;
        }
        QString screenError;
        const QImage image = backend.captureRegion(screen->geometry(), &screenError);
        qInfo() << "Application: freeze screen name=" << screen->name() << "geo=" << screen->geometry()
                << "dpr=" << screen->devicePixelRatio() << "image=" << image.size()
                << "null=" << image.isNull() << "error=" << screenError;
        if (image.isNull()) {
            if (freezeError.isEmpty()) {
                freezeError = screenError;
            }
            continue;
        }
        CaptureFreezeFrame frame;
        frame.geometry = screen->geometry();
        frame.image = image;
        frames.append(frame);
    }
    if (frames.isEmpty()) {
        setCaptureFlag(false);
        qWarning() << "Application: freeze capture failed" << freezeError;
        if (freezeError == QLatin1String("SCREEN_RECORDING_DENIED")) {
            ensureScreenRecording();
            return;
        }
        MacPermissions::activateApp();
        QMessageBox::warning(nullptr, QStringLiteral("SeenShot"),
                             ErrorCatalog::message(freezeError.isEmpty()
                                                       ? QStringLiteral("SCREEN_CAPTURE_BLOCKED")
                                                       : freezeError));
        return;
    }
    m_picker = new RegionPicker(frames);
    connect(m_picker, &RegionPicker::regionPicked, this, &Application::onRegionPicked);
    connect(m_picker, &RegionPicker::cancelled, this, [this]() {
        qInfo() << "Application: capture cancelled";
        setCaptureFlag(false);
        if (m_picker) {
            m_picker->deleteLater();
        }
    });
    m_picker->winId();
    MacPermissions::pinCaptureOverlay(m_picker);
    m_picker->show();
    MacPermissions::pinCaptureOverlay(m_picker);
    qInfo() << "Application: picker shown visible=" << m_picker->isVisible()
            << " geo=" << m_picker->geometry() << " active=" << m_picker->isActiveWindow()
            << " frames=" << frames.size();
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
        qWarning() << "Application: full screen blocked until Screen Recording TCC applies";
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
    QImage image;
    if (m_picker) {
        image = m_picker->croppedFreeze(rect);
        qInfo() << "Application: freeze crop size=" << image.size() << "null=" << image.isNull();
        m_picker->hide();
        m_picker->deleteLater();
        m_picker = nullptr;
        qInfo() << "Application: picker hidden after freeze crop";
    } else {
        qWarning() << "Application: region picked with no picker" << rect;
    }
    setCaptureFlag(false);
    if (image.isNull()) {
        qWarning() << "Application: freeze crop failed" << rect;
        MacPermissions::activateApp();
        QMessageBox::warning(nullptr, QStringLiteral("SeenShot"),
                             ErrorCatalog::message(QStringLiteral("SCREEN_CAPTURE_BLOCKED")));
        return;
    }
    Analytics::instance().track(QStringLiteral("capture"), {{QStringLiteral("kind"), QStringLiteral("region")}});
    showAnnotate(image);
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
    m_editor->setAttribute(Qt::WA_QuitOnClose, false);
    qApp->setQuitOnLastWindowClosed(false);
    // ─── Ariadne's Thread [AT-0335] ─────────────────────
    // What: NSApplicationActivationPolicyRegular while AnnotateWindow is shown
    // Why:  Cmd+Tab and Dock must reach the screenshot editor; LSUIElement stays in Info.plist
    // Date: 2026-08-28
    // Related: [AT-0300] MacPermissions.mm:setDockVisible, [AT-0303] Application.cpp:finishLaunch
    // ─────────────────────────────────────────────────────
    connect(m_editor, &QObject::destroyed, this, [this]() {
        qInfo() << "Application: annotate window destroyed, agent still running"
                << " replacement=" << (m_editor != nullptr);
        if (m_editor) {
            qInfo() << "Application: keep Dock Regular for replacement editor";
            return;
        }
        const bool dockOff = MacPermissions::setDockVisible(false);
        qInfo() << "Application: annotate dockOff=" << dockOff;
    });
    qInfo() << "Application: annotate WA_QuitOnClose=" << m_editor->testAttribute(Qt::WA_QuitOnClose)
            << " quitOnLastWindowClosed=" << qApp->quitOnLastWindowClosed();
    if (SparkleUpdater *updater = SparkleUpdater::instance()) {
        connect(m_editor, &AnnotateWindow::updateRequested, updater, &SparkleUpdater::userChoseUpdate);
        connect(m_editor, &AnnotateWindow::photoCycleEnded, updater, &SparkleUpdater::retryPendingInstall);
        updater->attachEditor(m_editor);
    }
    // ─── Ariadne's Thread [AT-0372] ─────────────────────
    // What: Stop forwarding agent copy to the tray banner
    // Why:  Hint is a QToolTip under the clicked icon, not a system notification
    // Date: 2026-08-29
    // Related: [AT-0372] AnnotateWindow.cpp:copyExportedImageToClipboard, [AT-0369] TrayController.cpp:showPasteHint
    // ─────────────────────────────────────────────────────
    qInfo() << "Application: annotate paste hint is toolbar tooltip";
    const bool dockOn = MacPermissions::setDockVisible(true);
    qInfo() << "Application: annotate dockOn=" << dockOn;
    MacPermissions::activateApp();
    m_editor->show();
    m_editor->raise();
    m_editor->activateWindow();
    qInfo() << "Application: annotate window shown visible=" << m_editor->isVisible()
            << " active=" << m_editor->isActiveWindow();
}

void Application::openSettings()
{
    qInfo() << "Application: openSettings";
    if (!m_settings) {
        m_settings = new SettingsWindow(m_auth, m_cloud);
        m_settings->setAttribute(Qt::WA_DeleteOnClose);
        m_settings->setAttribute(Qt::WA_QuitOnClose, false);
        qInfo() << "Application: settings WA_QuitOnClose="
                << m_settings->testAttribute(Qt::WA_QuitOnClose);
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
    MacPermissions::allowQuit("tray");
    teardownNativeWindows();
    Analytics::instance().shutdown();
    qApp->quit();
}
