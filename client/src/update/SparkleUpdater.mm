#include "update/SparkleUpdater.h"

#include "annotate/AnnotateWindow.h"
#include "app/Analytics.h"
#include "app/Config.h"
#include "app/MacPermissions.h"
#include "errors/ErrorCatalog.h"

#include <QDebug>

#ifdef SEENSHOT_HAS_SPARKLE
#import <Sparkle/Sparkle.h>
#endif

static SparkleUpdater *g_instance = nullptr;

#ifdef SEENSHOT_HAS_SPARKLE

@interface SeenShotSparkleDriver : NSObject <SPUUserDriver>
@property (nonatomic, copy) void (^foundReply)(SPUUserUpdateChoice);
@property (nonatomic, copy) void (^relaunchReply)(SPUUserUpdateChoice);
@end

@implementation SeenShotSparkleDriver

- (void)showUpdatePermissionRequest:(SPUUpdatePermissionRequest *)request
                              reply:(void (^)(SUUpdatePermissionResponse *))reply
{
    (void)request;
    qInfo() << "SparkleUpdater: permission request, auto-check on, auto-install off";
    SUUpdatePermissionResponse *response =
        [[SUUpdatePermissionResponse alloc] initWithAutomaticUpdateChecks:YES
                                               automaticUpdateDownloading:@NO
                                                        sendSystemProfile:NO];
    reply(response);
    if (SparkleUpdater *u = SparkleUpdater::instance()) {
        u->handlePermission();
    }
}

- (void)showUserInitiatedUpdateCheckWithCancellation:(void (^)(void))cancellation
{
    (void)cancellation;
    qInfo() << "SparkleUpdater: user-initiated check ignored, no extra window";
}

- (void)showUpdateFoundWithAppcastItem:(SUAppcastItem *)appcastItem
                                 state:(SPUUserUpdateState *)state
                                 reply:(void (^)(SPUUserUpdateChoice))reply
{
    const QString version = QString::fromNSString(appcastItem.displayVersionString);
    const bool infoOnly = appcastItem.informationOnlyUpdate;
    const bool downloaded = state.stage == SPUUserUpdateStageDownloaded
        || state.stage == SPUUserUpdateStageInstalling;
    qInfo() << "SparkleUpdater: update found version=" << version
            << " infoOnly=" << infoOnly
            << " critical=" << appcastItem.criticalUpdate
            << " stage=" << static_cast<int>(state.stage)
            << " userInitiated=" << state.userInitiated;
    if (infoOnly) {
        qInfo() << "SparkleUpdater: information-only update, dismiss, no install card";
        reply(SPUUserUpdateChoiceDismiss);
        return;
    }
    self.foundReply = reply;
    if (SparkleUpdater *u = SparkleUpdater::instance()) {
        u->handleUpdateFound(version, infoOnly, downloaded);
    }
}

- (void)showUpdateReleaseNotesWithDownloadData:(SPUDownloadData *)downloadData
{
    (void)downloadData;
    qInfo() << "SparkleUpdater: release notes ignored";
}

- (void)showUpdateReleaseNotesFailedToDownloadWithError:(NSError *)error
{
    qInfo() << "SparkleUpdater: release notes failed"
            << (error ? QString::fromNSString(error.localizedDescription) : QString());
}

- (void)showUpdateNotFoundWithError:(NSError *)error acknowledgement:(void (^)(void))acknowledgement
{
    qInfo() << "SparkleUpdater: no update"
            << (error ? QString::fromNSString(error.localizedDescription) : QString());
    if (SparkleUpdater *u = SparkleUpdater::instance()) {
        u->handleNoUpdate();
    }
    acknowledgement();
}

- (void)showUpdaterError:(NSError *)error acknowledgement:(void (^)(void))acknowledgement
{
    const QString detail = error ? QString::fromNSString(error.localizedDescription) : QString();
    qWarning() << "SparkleUpdater: updater error" << detail
               << " domain=" << (error ? QString::fromNSString(error.domain) : QString())
               << " code=" << (error ? static_cast<int>(error.code) : -1);
    if (SparkleUpdater *u = SparkleUpdater::instance()) {
        u->handleUpdaterError(detail);
    }
    acknowledgement();
}

- (void)showDownloadInitiatedWithCancellation:(void (^)(void))cancellation
{
    (void)cancellation;
    qInfo() << "SparkleUpdater: download initiated";
    if (SparkleUpdater *u = SparkleUpdater::instance()) {
        u->handleDownloadStarted();
    }
}

- (void)showDownloadDidReceiveExpectedContentLength:(uint64_t)expectedContentLength
{
    qInfo() << "SparkleUpdater: expected bytes=" << static_cast<qint64>(expectedContentLength);
    if (SparkleUpdater *u = SparkleUpdater::instance()) {
        u->handleDownloadExpected(static_cast<qint64>(expectedContentLength));
    }
}

- (void)showDownloadDidReceiveDataOfLength:(uint64_t)length
{
    if (SparkleUpdater *u = SparkleUpdater::instance()) {
        u->handleDownloadReceived(static_cast<qint64>(length));
    }
}

- (void)showDownloadDidStartExtractingUpdate
{
    qInfo() << "SparkleUpdater: extract started";
    if (SparkleUpdater *u = SparkleUpdater::instance()) {
        u->handleExtractStarted();
    }
}

- (void)showExtractionReceivedProgress:(double)progress
{
    qInfo() << "SparkleUpdater: extract progress=" << progress;
    if (SparkleUpdater *u = SparkleUpdater::instance()) {
        u->handleExtractProgress(progress);
    }
}

- (void)showReadyToInstallAndRelaunch:(void (^)(SPUUserUpdateChoice))reply
{
    qInfo() << "SparkleUpdater: ready to install and relaunch";
    self.relaunchReply = reply;
    if (SparkleUpdater *u = SparkleUpdater::instance()) {
        u->handleReadyToRelaunch();
    }
}

- (void)showInstallingUpdateWithApplicationTerminated:(BOOL)applicationTerminated
                        retryTerminatingApplication:(void (^)(void))retryTerminatingApplication
{
    (void)retryTerminatingApplication;
    qInfo() << "SparkleUpdater: installing terminated=" << applicationTerminated;
    if (SparkleUpdater *u = SparkleUpdater::instance()) {
        MacPermissions::allowQuit("sparkle-installing");
        u->handleInstalling();
    }
}

- (void)showUpdateInstalledAndRelaunched:(BOOL)relaunched acknowledgement:(void (^)(void))acknowledgement
{
    qInfo() << "SparkleUpdater: installed relaunched=" << relaunched;
    acknowledgement();
}

- (void)dismissUpdateInstallation
{
    qInfo() << "SparkleUpdater: dismiss installation";
    if (SparkleUpdater *u = SparkleUpdater::instance()) {
        u->handleDismissed();
    }
}

@end

static SeenShotSparkleDriver *g_driver = nil;
static SPUUpdater *g_sparkle = nil;

#endif

SparkleUpdater *SparkleUpdater::instance()
{
    return g_instance;
}

SparkleUpdater::SparkleUpdater(QObject *parent)
    : QObject(parent)
{
}

void SparkleUpdater::start()
{
    if (g_instance) {
        qInfo() << "SparkleUpdater: start already called";
        return;
    }
    g_instance = new SparkleUpdater();
    g_instance->startUpdater();
}

void SparkleUpdater::startUpdater()
{
#ifdef SEENSHOT_HAS_SPARKLE
    NSBundle *bundle = [NSBundle mainBundle];
    const NSString *publicKey = [bundle objectForInfoDictionaryKey:@"SUPublicEDKey"];
    const int keyLen = publicKey ? static_cast<int>(publicKey.length) : 0;
    qInfo() << "SparkleUpdater: SUPublicEDKey length=" << keyLen << " feed=" << Config::sparkleFeedUrl();
    if (keyLen <= 0) {
        qWarning() << "SparkleUpdater: empty SUPublicEDKey, no user-visible update";
        return;
    }
    g_driver = [[SeenShotSparkleDriver alloc] init];
    g_sparkle = [[SPUUpdater alloc] initWithHostBundle:bundle
                                     applicationBundle:bundle
                                            userDriver:g_driver
                                              delegate:nil];
    g_sparkle.automaticallyDownloadsUpdates = NO;
    NSError *error = nil;
    const BOOL ok = [g_sparkle startUpdater:&error];
    if (!ok) {
        qWarning() << "SparkleUpdater: startUpdater failed"
                   << (error ? QString::fromNSString(error.localizedDescription) : QStringLiteral("nil error"))
                   << " domain=" << (error ? QString::fromNSString(error.domain) : QString())
                   << " code=" << (error ? static_cast<int>(error.code) : -1);
        g_sparkle = nil;
        g_driver = nil;
        return;
    }
    qInfo() << "SparkleUpdater: started autoCheck=" << g_sparkle.automaticallyChecksForUpdates
            << " autoDownload=" << g_sparkle.automaticallyDownloadsUpdates;
#else
    qInfo() << "SparkleUpdater: Sparkle.framework not linked, feed=" << Config::sparkleFeedUrl();
#endif
}

void SparkleUpdater::attachEditor(AnnotateWindow *editor)
{
    m_editor = editor;
    qInfo() << "SparkleUpdater: attachEditor pending=" << m_offerPending
            << " download=" << m_downloadInFlight << " ready=" << m_readyToRelaunch;
    presentOfferIfPossible();
}

void SparkleUpdater::editorWillClose(AnnotateWindow *editor)
{
    if (m_editor != editor) {
        qInfo() << "SparkleUpdater: editorWillClose ignored, not attached";
        return;
    }
    qInfo() << "SparkleUpdater: editorWillClose download=" << m_downloadInFlight
            << " ready=" << m_readyToRelaunch << " offer=" << m_offerPending;
    if (m_downloadInFlight || m_readyToRelaunch) {
        persistEditorNow();
    }
    if (m_readyToRelaunch) {
        replyRelaunchDismiss();
        m_readyToRelaunch = false;
        m_waitingInstall = false;
    } else if (m_offerPending && !m_downloadInFlight) {
        replyFoundDismiss();
    }
    m_editor = nullptr;
}

void SparkleUpdater::setCaptureInProgress(bool capturing)
{
    if (m_captureInProgress == capturing) {
        return;
    }
    m_captureInProgress = capturing;
    qInfo() << "SparkleUpdater: captureInProgress=" << capturing;
    if (!capturing && m_waitingInstall) {
        finishInstallWhenSafe();
    }
}

void SparkleUpdater::userChoseUpdate()
{
    qInfo() << "SparkleUpdater: userChoseUpdate ready=" << m_readyToRelaunch
            << " offer=" << m_offerPending;
    if (m_readyToRelaunch) {
        finishInstallWhenSafe();
        return;
    }
    replyFoundInstall();
}

void SparkleUpdater::retryPendingInstall()
{
    qInfo() << "SparkleUpdater: retryPendingInstall waiting=" << m_waitingInstall;
    if (m_waitingInstall) {
        finishInstallWhenSafe();
    }
}

void SparkleUpdater::persistBeforeQuit()
{
    qInfo() << "SparkleUpdater: persistBeforeQuit download=" << m_downloadInFlight
            << " ready=" << m_readyToRelaunch;
    if (m_downloadInFlight || m_readyToRelaunch) {
        persistEditorNow();
    }
}

bool SparkleUpdater::isDownloadInFlight() const
{
    return m_downloadInFlight;
}

void SparkleUpdater::handlePermission()
{
    qInfo() << "SparkleUpdater: permission granted in driver";
}

void SparkleUpdater::handleUpdateFound(const QString &version, bool informationOnly, bool alreadyDownloaded)
{
    (void)informationOnly;
    m_version = version;
    m_offerPending = true;
    qInfo() << "SparkleUpdater: handleUpdateFound version=" << version
            << " alreadyDownloaded=" << alreadyDownloaded;
    Analytics::instance().track(QStringLiteral("update"), {{QStringLiteral("stage"), QStringLiteral("offer")}});
    presentOfferIfPossible();
}

void SparkleUpdater::handleNoUpdate()
{
    qInfo() << "SparkleUpdater: handleNoUpdate";
}

void SparkleUpdater::handleUpdaterError(const QString &detail)
{
    qWarning() << "SparkleUpdater: handleUpdaterError chars=" << detail.size();
    m_downloadInFlight = false;
    m_readyToRelaunch = false;
    m_waitingInstall = false;
    if (m_editor) {
        m_editor->showUpdateError(QStringLiteral("UPDATE_FAILED"));
        if (m_offerPending) {
            m_editor->resetUpdateOffer();
        }
    }
}

void SparkleUpdater::handleDownloadStarted()
{
    m_downloadInFlight = true;
    m_expectedBytes = 0;
    m_receivedBytes = 0;
    qInfo() << "SparkleUpdater: handleDownloadStarted version=" << m_version;
    Analytics::instance().track(QStringLiteral("update"),
                               {{QStringLiteral("stage"), QStringLiteral("download")}});
    if (m_editor) {
        m_editor->showUpdateProgress(0, 0, QStringLiteral("Downloading…"));
    }
}

void SparkleUpdater::handleDownloadExpected(qint64 bytes)
{
    m_expectedBytes = bytes;
    qInfo() << "SparkleUpdater: handleDownloadExpected=" << bytes << " received=" << m_receivedBytes;
    if (m_editor) {
        m_editor->showUpdateProgress(m_receivedBytes, m_expectedBytes, QStringLiteral("Downloading…"));
    }
}

void SparkleUpdater::handleDownloadReceived(qint64 bytes)
{
    m_receivedBytes += bytes;
    if (m_editor) {
        m_editor->showUpdateProgress(m_receivedBytes, m_expectedBytes, QStringLiteral("Downloading…"));
    }
}

void SparkleUpdater::handleExtractStarted()
{
    m_downloadInFlight = true;
    qInfo() << "SparkleUpdater: handleExtractStarted received=" << m_receivedBytes
            << " expected=" << m_expectedBytes;
    if (m_editor) {
        m_editor->showUpdateExtracting(0);
    }
}

void SparkleUpdater::handleExtractProgress(double progress)
{
    if (m_editor) {
        m_editor->showUpdateExtracting(progress);
    }
}

void SparkleUpdater::handleReadyToRelaunch()
{
    m_readyToRelaunch = true;
    m_downloadInFlight = false;
    qInfo() << "SparkleUpdater: handleReadyToRelaunch version=" << m_version;
    finishInstallWhenSafe();
}

void SparkleUpdater::handleInstalling()
{
    qInfo() << "SparkleUpdater: handleInstalling";
    Analytics::instance().track(QStringLiteral("update"),
                               {{QStringLiteral("stage"), QStringLiteral("install")}});
    if (m_editor) {
        m_editor->showUpdateInstalling();
    }
}

void SparkleUpdater::handleDismissed()
{
    qInfo() << "SparkleUpdater: handleDismissed";
    m_downloadInFlight = false;
    m_waitingInstall = false;
    if (m_editor && !m_offerPending) {
        m_editor->hideUpdateCard();
    }
}

void SparkleUpdater::replyFoundInstall()
{
#ifdef SEENSHOT_HAS_SPARKLE
    if (!g_driver || !g_driver.foundReply) {
        qWarning() << "SparkleUpdater: replyFoundInstall missing reply";
        return;
    }
    void (^reply)(SPUUserUpdateChoice) = g_driver.foundReply;
    g_driver.foundReply = nil;
    m_offerPending = false;
    qInfo() << "SparkleUpdater: replyFoundInstall";
    reply(SPUUserUpdateChoiceInstall);
#endif
}

void SparkleUpdater::replyFoundDismiss()
{
#ifdef SEENSHOT_HAS_SPARKLE
    if (!g_driver || !g_driver.foundReply) {
        return;
    }
    void (^reply)(SPUUserUpdateChoice) = g_driver.foundReply;
    g_driver.foundReply = nil;
    m_offerPending = false;
    qInfo() << "SparkleUpdater: replyFoundDismiss";
    reply(SPUUserUpdateChoiceDismiss);
#endif
}

void SparkleUpdater::replyRelaunchInstall()
{
#ifdef SEENSHOT_HAS_SPARKLE
    if (!g_driver || !g_driver.relaunchReply) {
        qWarning() << "SparkleUpdater: replyRelaunchInstall missing reply";
        return;
    }
    void (^reply)(SPUUserUpdateChoice) = g_driver.relaunchReply;
    g_driver.relaunchReply = nil;
    m_readyToRelaunch = false;
    m_waitingInstall = false;
    qInfo() << "SparkleUpdater: replyRelaunchInstall";
    // ─── Ariadne's Thread [AT-0206] ─────────────────────
    // What: Mark Quit allowed before Sparkle replace-and-relaunch
    // Why:  The agent otherwise ignores NSApp terminate after the editor is already closed
    // Date: 2026-08-27
    // Related: [AT-0205] MacPermissions.mm:allowQuit, [AT-0092] SparkleUpdater.mm
    // ─────────────────────────────────────────────────────
    MacPermissions::allowQuit("sparkle-relaunch");
    reply(SPUUserUpdateChoiceInstall);
#endif
}

void SparkleUpdater::replyRelaunchDismiss()
{
#ifdef SEENSHOT_HAS_SPARKLE
    if (!g_driver || !g_driver.relaunchReply) {
        return;
    }
    void (^reply)(SPUUserUpdateChoice) = g_driver.relaunchReply;
    g_driver.relaunchReply = nil;
    qInfo() << "SparkleUpdater: replyRelaunchDismiss";
    reply(SPUUserUpdateChoiceDismiss);
#endif
}

void SparkleUpdater::presentOfferIfPossible()
{
    if (!m_editor) {
        qInfo() << "SparkleUpdater: offer kept, annotate is closed";
        return;
    }
    if (m_readyToRelaunch) {
        finishInstallWhenSafe();
        return;
    }
    if (m_downloadInFlight) {
        m_editor->showUpdateProgress(m_receivedBytes, m_expectedBytes, QStringLiteral("Downloading…"));
        return;
    }
    if (m_offerPending) {
        m_editor->showUpdateOffer();
    }
}

bool SparkleUpdater::persistEditorNow()
{
    if (!m_editor) {
        qInfo() << "SparkleUpdater: persist skipped, no editor";
        return true;
    }
    QString error;
    if (!m_editor->persistSession(&error)) {
        qWarning() << "SparkleUpdater: persist failed" << error;
        m_editor->showUpdateError(error.isEmpty() ? QStringLiteral("UPDATE_PERSIST_FAILED") : error);
        m_editor->resetUpdateOffer();
        return false;
    }
    qInfo() << "SparkleUpdater: persist ok";
    return true;
}

void SparkleUpdater::finishInstallWhenSafe()
{
    if (!m_readyToRelaunch) {
        qInfo() << "SparkleUpdater: finishInstallWhenSafe not ready";
        return;
    }
    if (m_captureInProgress) {
        m_waitingInstall = true;
        qInfo() << "SparkleUpdater: wait for capture to finish";
        return;
    }
    if (m_editor && m_editor->isPhotoCaptureBusy()) {
        m_waitingInstall = true;
        qInfo() << "SparkleUpdater: wait for photo cycle to finish";
        return;
    }
    m_waitingInstall = false;
    if (!persistEditorNow()) {
        qWarning() << "SparkleUpdater: persist failed, do not install";
        return;
    }
    if (m_editor) {
        m_editor->showUpdateInstalling();
    }
    replyRelaunchInstall();
}
