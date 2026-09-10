#include "update/SparkleUpdater.h"

#include "annotate/AnnotateWindow.h"
#include "app/Analytics.h"
#include "app/Config.h"
#include "app/MacPermissions.h"
#include "errors/ErrorCatalog.h"
#include "local/LocalStore.h"

#include <QDebug>
#include <QVersionNumber>

#import <Foundation/Foundation.h>

#ifdef SEENSHOT_HAS_SPARKLE
#import <Sparkle/Sparkle.h>
#import <dispatch/dispatch.h>
#endif

static SparkleUpdater *g_instance = nullptr;

#ifdef SEENSHOT_HAS_SPARKLE

@interface SeenShotSparkleDriver : NSObject <SPUUserDriver, SPUUpdaterDelegate>
@property (nonatomic, copy) void (^foundReply)(SPUUserUpdateChoice);
@property (nonatomic, copy) void (^relaunchReply)(SPUUserUpdateChoice);
@end

static void logSparkleNsError(NSError *error, const char *label)
{
    NSError *current = error;
    int depth = 0;
    while (current && depth < 8) {
        qWarning() << "SparkleUpdater:" << label << " depth=" << depth
                   << " domain=" << QString::fromNSString(current.domain)
                   << " code=" << static_cast<int>(current.code)
                   << " desc=" << QString::fromNSString(current.localizedDescription)
                   << " reason=" << QString::fromNSString(current.localizedFailureReason ? current.localizedFailureReason : @"")
                   << " recovery=" << QString::fromNSString(current.localizedRecoverySuggestion ? current.localizedRecoverySuggestion : @"");
        NSDictionary *info = current.userInfo;
        for (NSString *key in info) {
            if ([key isEqualToString:NSUnderlyingErrorKey]) {
                continue;
            }
            id value = info[key];
            if ([value isKindOfClass:[NSData class]]) {
                qWarning() << "SparkleUpdater:" << label << " userInfo" << QString::fromNSString(key)
                           << " bytes=" << static_cast<qint64>([value length]);
                continue;
            }
            qWarning() << "SparkleUpdater:" << label << " userInfo" << QString::fromNSString(key)
                       << "=" << QString::fromNSString([value description]);
        }
        current = current.userInfo[NSUnderlyingErrorKey];
        ++depth;
    }
    if (!error) {
        qWarning() << "SparkleUpdater:" << label << " error is nil";
    }
}

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
    const QString displayVersion = QString::fromNSString(appcastItem.displayVersionString);
    const QString sparkleVersion = QString::fromNSString(appcastItem.versionString);
    const bool infoOnly = appcastItem.informationOnlyUpdate;
    const bool downloaded = state.stage == SPUUserUpdateStageDownloaded
        || state.stage == SPUUserUpdateStageInstalling;
    qInfo() << "SparkleUpdater: update found version=" << displayVersion
            << " sparkleVersion=" << sparkleVersion
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
        u->handleUpdateFound(displayVersion, sparkleVersion, infoOnly, downloaded);
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
    logSparkleNsError(error, "updater error");
    const QString detail = error ? QString::fromNSString(error.localizedDescription) : QString();
    const QString domain = error ? QString::fromNSString(error.domain) : QString();
    const int code = error ? static_cast<int>(error.code) : -1;
    if (SparkleUpdater *u = SparkleUpdater::instance()) {
        u->handleUpdaterError(code, domain, detail);
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

- (void)showUpdateInFocus
{
    qInfo() << "SparkleUpdater: showUpdateInFocus";
    if (SparkleUpdater *u = SparkleUpdater::instance()) {
        u->handleShowInFocus();
    }
}

- (void)updater:(SPUUpdater *)updater didFinishUpdateCycleForUpdateCheck:(SPUUpdateCheck)updateCheck error:(NSError *)error
{
    (void)updater;
    if (error) {
        logSparkleNsError(error, "cycle finished");
    }
    const int check = static_cast<int>(updateCheck);
    const int code = error ? static_cast<int>(error.code) : 0;
    const QString detail = error ? QString::fromNSString(error.localizedDescription) : QString();
    qInfo() << "SparkleUpdater: didFinishUpdateCycle check=" << check
            << " code=" << code
            << " hasError=" << (error != nil);
    if (SparkleUpdater *u = SparkleUpdater::instance()) {
        u->handleCycleFinished(check, code, detail);
    }
}

- (void)updater:(SPUUpdater *)updater didAbortWithError:(NSError *)error
{
    (void)updater;
    logSparkleNsError(error, "didAbortWithError");
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
                                              delegate:g_driver];
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
            << " download=" << m_downloadInFlight << " ready=" << m_readyToRelaunch
            << " pendingAuto=" << m_pendingAutoInstall;
    if (m_pendingAutoInstall) {
        tryStartPendingAutoInstall();
        return;
    }
    presentOfferIfPossible();
}

// ─── Ariadne's Thread [AT-0659] ─────────────────────
// What: Keep a found Sparkle offer after AnnotateWindow closes
// Why:  A new screenshot used to reply Dismiss, so the next editor never showed the bar
// Date: 2026-09-10
// Related: [AT-0092] SparkleUpdater.mm:attachEditor, [AT-0652] AnnotateWindow.cpp:layoutBottomBars
// ─────────────────────────────────────────────────────
void SparkleUpdater::editorWillClose(AnnotateWindow *editor)
{
    if (m_editor != editor) {
        qInfo() << "SparkleUpdater: editorWillClose ignored, not attached";
        return;
    }
    qInfo() << "SparkleUpdater: editorWillClose download=" << m_downloadInFlight
            << " ready=" << m_readyToRelaunch << " offer=" << m_offerPending
            << " capturing=" << m_captureInProgress;
    if (m_downloadInFlight || m_readyToRelaunch) {
        persistEditorNow();
    }
    if (m_readyToRelaunch) {
        replyRelaunchDismiss();
        m_readyToRelaunch = false;
        m_waitingInstall = false;
    } else if (m_offerPending && !m_downloadInFlight) {
        qInfo() << "SparkleUpdater: keep pending offer after annotate close";
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

// ─── Ariadne's Thread [AT-0663] ─────────────────────
// What: Persist, then Install if Sparkle still has a reply, else start checkForUpdates
// Why:  After SUInstallationError the session is dead. The card must still retry
// Date: 2026-09-10
// Related: [AT-0661] SparkleUpdater.mm:replyFoundInstall, [AT-0664] ErrorCatalog.cpp
// ─────────────────────────────────────────────────────
void SparkleUpdater::userChoseUpdate()
{
#ifdef SEENSHOT_HAS_SPARKLE
    const bool hasReply = g_driver && g_driver.foundReply;
    const bool session = g_sparkle ? static_cast<bool>(g_sparkle.sessionInProgress) : false;
    const bool canCheck = g_sparkle ? static_cast<bool>(g_sparkle.canCheckForUpdates) : false;
    qInfo() << "SparkleUpdater: userChoseUpdate ready=" << m_readyToRelaunch
            << " offer=" << m_offerPending
            << " hasFoundReply=" << hasReply
            << " sessionInProgress=" << session
            << " canCheckForUpdates=" << canCheck
            << " allowed=" << LocalStore::autoInstallAllowed()
            << " writable=" << MacPermissions::hostBundleWritable();
#else
    qInfo() << "SparkleUpdater: userChoseUpdate ready=" << m_readyToRelaunch
            << " offer=" << m_offerPending
            << " allowed=" << LocalStore::autoInstallAllowed()
            << " writable=" << MacPermissions::hostBundleWritable();
#endif
    if (!requestUpdateWritePermission()) {
        qWarning() << "SparkleUpdater: userChoseUpdate stopped, write permission refused";
        return;
    }
    if (m_readyToRelaunch) {
        finishInstallWhenSafe();
        return;
    }
    if (!persistEditorNow()) {
        qWarning() << "SparkleUpdater: skip install, persist failed before download";
        return;
    }
    m_installWhenFound = true;
#ifdef SEENSHOT_HAS_SPARKLE
    if (g_driver && g_driver.foundReply) {
        replyFoundInstall();
        return;
    }
#endif
    requestSparkleInstall();
}

// ─── Ariadne's Thread [AT-0673] ─────────────────────
// What: Ask host-bundle write permission again when the user clicks Update
// Why:  Onboarding refusal still shows the card. Install must not skip the replace-app prompt
// Date: 2026-09-10
// Related: [AT-0663] SparkleUpdater.mm:userChoseUpdate, [AT-0667] MacPermissions.mm:ensureHostBundleWritable, [AT-0671] LocalStore.cpp:setAutoInstallAllowed
// ─────────────────────────────────────────────────────
bool SparkleUpdater::requestUpdateWritePermission()
{
    MacPermissions::activateApp();
    const bool writableBefore = MacPermissions::hostBundleWritable();
    qInfo() << "SparkleUpdater: requestUpdateWritePermission writableBefore=" << writableBefore
            << " allowedBefore=" << LocalStore::autoInstallAllowed();
    const bool writableAfter = MacPermissions::ensureHostBundleWritable();
    LocalStore::setAutoInstallAllowed(writableAfter);
    qInfo() << "SparkleUpdater: requestUpdateWritePermission writableAfter=" << writableAfter;
    if (writableAfter) {
        return true;
    }
    qWarning() << "SparkleUpdater: requestUpdateWritePermission refused, keep Update card";
    m_installWhenFound = false;
    m_pendingAutoInstall = false;
    presentOfferIfPossible();
    return false;
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

QString SparkleUpdater::hostBundleVersion() const
{
#ifdef SEENSHOT_HAS_SPARKLE
    NSString *raw = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleVersion"];
    const QString version = raw ? QString::fromNSString(raw) : QString();
    qInfo() << "SparkleUpdater: hostBundleVersion=" << version;
    return version;
#else
    return QString();
#endif
}

// ─── Ariadne's Thread [AT-0666] ─────────────────────
// What: Auto-install only after onboarding allowed it, and only when CFBundleVersion is more than 2 patches behind
// Why:  A refused replace-app prompt must not start an install, including large version gaps
// Date: 2026-09-10
// Related: [AT-0663] SparkleUpdater.mm:userChoseUpdate, [AT-0671] LocalStore.cpp:autoInstallAllowed
// ─────────────────────────────────────────────────────
bool SparkleUpdater::shouldAutoInstall(const QString &hostVersion, const QString &foundVersion) const
{
    const bool allowed = LocalStore::autoInstallAllowed();
    const QVersionNumber host = QVersionNumber::fromString(hostVersion);
    const QVersionNumber found = QVersionNumber::fromString(foundVersion);
    const int hostMajor = host.segmentCount() > 0 ? host.segmentAt(0) : 0;
    const int hostMinor = host.segmentCount() > 1 ? host.segmentAt(1) : 0;
    const int hostPatch = host.segmentCount() > 2 ? host.segmentAt(2) : 0;
    const int foundMajor = found.segmentCount() > 0 ? found.segmentAt(0) : 0;
    const int foundMinor = found.segmentCount() > 1 ? found.segmentAt(1) : 0;
    const int foundPatch = found.segmentCount() > 2 ? found.segmentAt(2) : 0;
    const int hostFlat = hostMajor * 1000000 + hostMinor * 1000 + hostPatch;
    const int foundFlat = foundMajor * 1000000 + foundMinor * 1000 + foundPatch;
    const int delta = foundFlat - hostFlat;
    const bool gapOk = !host.isNull() && !found.isNull() && delta > 2;
    const bool autoInstall = allowed && gapOk;
    qInfo() << "SparkleUpdater: shouldAutoInstall host=" << hostVersion
            << " found=" << foundVersion
            << " hostFlat=" << hostFlat
            << " foundFlat=" << foundFlat
            << " delta=" << delta
            << " allowed=" << allowed
            << " gapOk=" << gapOk
            << " auto=" << autoInstall
            << " hostNull=" << host.isNull()
            << " foundNull=" << found.isNull();
    return autoInstall;
}

void SparkleUpdater::handleUpdateFound(const QString &displayVersion, const QString &sparkleVersion,
                                       bool informationOnly, bool alreadyDownloaded)
{
    m_version = displayVersion;
    m_offerPending = true;
    const QString hostVersion = hostBundleVersion();
    qInfo() << "SparkleUpdater: handleUpdateFound version=" << displayVersion
            << " sparkleVersion=" << sparkleVersion
            << " hostVersion=" << hostVersion
            << " alreadyDownloaded=" << alreadyDownloaded
            << " informationOnly=" << informationOnly
            << " installWhenFound=" << m_installWhenFound;
    Analytics::instance().track(QStringLiteral("update"), {{QStringLiteral("stage"), QStringLiteral("offer")}});
    if (informationOnly) {
        m_installWhenFound = false;
        presentOfferIfPossible();
        return;
    }
    if (m_installWhenFound) {
        qInfo() << "SparkleUpdater: install as soon as Sparkle has a found reply";
        replyFoundInstall();
        return;
    }
    if (shouldAutoInstall(hostVersion, sparkleVersion)) {
        qInfo() << "SparkleUpdater: auto install, version gap greater than 2";
        Analytics::instance().track(QStringLiteral("update"),
                                   {{QStringLiteral("stage"), QStringLiteral("auto")}});
        m_pendingAutoInstall = true;
        tryStartPendingAutoInstall();
        return;
    }
    qInfo() << "SparkleUpdater: no auto install, wait for Update click";
    presentOfferIfPossible();
}

void SparkleUpdater::handleNoUpdate()
{
    qInfo() << "SparkleUpdater: handleNoUpdate installWhenFound=" << m_installWhenFound
            << " offer=" << m_offerPending;
    m_installWhenFound = false;
    if (!m_offerPending && m_editor) {
        m_editor->hideUpdateCard();
    }
}

// ─── Ariadne's Thread [AT-0665] ─────────────────────
// What: Keep the Update card after Sparkle install errors and re-check the feed
// Why:  SUInstallationError hid the bar and SULastCheckTime blocked the next offer
// Date: 2026-09-10
// Related: [AT-0663] SparkleUpdater.mm:userChoseUpdate, [AT-0664] ErrorCatalog.cpp
// ─────────────────────────────────────────────────────
void SparkleUpdater::handleUpdaterError(int sparkleCode, const QString &domain, const QString &detail)
{
    qWarning() << "SparkleUpdater: handleUpdaterError code=" << sparkleCode
               << " domain=" << domain
               << " chars=" << detail.size()
               << " version=" << m_version;
    m_downloadInFlight = false;
    m_readyToRelaunch = false;
    m_waitingInstall = false;
    m_installWhenFound = false;
    m_offerPending = true;
    m_rearmAfterCycle = true;
    m_pendingAutoInstall = false;
    logHostInstallPermissions();
    QString catalog = QStringLiteral("UPDATE_FAILED");
#ifdef SEENSHOT_HAS_SPARKLE
    if (sparkleCode == SUAuthenticationFailure || sparkleCode == SUInstallationCanceledError
        || sparkleCode == SUInstallationAuthorizeLaterError) {
        catalog = QStringLiteral("UPDATE_AUTH_REQUIRED");
    } else if (sparkleCode == SUInstallationWriteNoPermissionError) {
        catalog = QStringLiteral("UPDATE_WRITE_DENIED");
    } else if (sparkleCode == SUDownloadError) {
        catalog = QStringLiteral("UPDATE_DOWNLOAD_FAILED");
    }
#endif
    qWarning() << "SparkleUpdater: map sparkle code=" << sparkleCode << " catalog=" << catalog;
    restoreOfferAfterFailure(catalog);
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
    logHostInstallPermissions();
    MacPermissions::activateApp();
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
    qInfo() << "SparkleUpdater: handleDismissed offer=" << m_offerPending
            << " rearm=" << m_rearmAfterCycle
            << " version=" << m_version;
    m_downloadInFlight = false;
    m_waitingInstall = false;
    if (m_editor && m_offerPending) {
        m_editor->resetUpdateOffer();
        return;
    }
    if (m_editor) {
        m_editor->hideUpdateCard();
    }
}

// ─── Ariadne's Thread [AT-0661] ─────────────────────
// What: Copy the Sparkle reply block, then invoke Install on the next main-queue turn
// Why:  MRC released foundReply before invoke, SIGSEGV at 0 inside Qt mouseReleaseEvent
// Date: 2026-09-10
// Related: [AT-0660] CMakeLists.txt, [AT-0092] SparkleUpdater.mm:userChoseUpdate
// ─────────────────────────────────────────────────────
void SparkleUpdater::replyFoundInstall()
{
#ifdef SEENSHOT_HAS_SPARKLE
    if (!g_driver || !g_driver.foundReply) {
        qWarning() << "SparkleUpdater: replyFoundInstall missing reply";
        return;
    }
    void (^reply)(SPUUserUpdateChoice) = [g_driver.foundReply copy];
    g_driver.foundReply = nil;
    qInfo() << "SparkleUpdater: replyFoundInstall queued";
    MacPermissions::activateApp();
    dispatch_async(dispatch_get_main_queue(), ^{
        if (!reply) {
            qWarning() << "SparkleUpdater: replyFoundInstall copy is nil";
            return;
        }
        qInfo() << "SparkleUpdater: replyFoundInstall invoke";
        reply(SPUUserUpdateChoiceInstall);
    });
#endif
}

void SparkleUpdater::replyFoundDismiss()
{
#ifdef SEENSHOT_HAS_SPARKLE
    if (!g_driver || !g_driver.foundReply) {
        return;
    }
    void (^reply)(SPUUserUpdateChoice) = [g_driver.foundReply copy];
    g_driver.foundReply = nil;
    m_offerPending = false;
    qInfo() << "SparkleUpdater: replyFoundDismiss queued";
    dispatch_async(dispatch_get_main_queue(), ^{
        if (!reply) {
            qWarning() << "SparkleUpdater: replyFoundDismiss copy is nil";
            return;
        }
        qInfo() << "SparkleUpdater: replyFoundDismiss invoke";
        reply(SPUUserUpdateChoiceDismiss);
    });
#endif
}

void SparkleUpdater::replyRelaunchInstall()
{
#ifdef SEENSHOT_HAS_SPARKLE
    if (!g_driver || !g_driver.relaunchReply) {
        qWarning() << "SparkleUpdater: replyRelaunchInstall missing reply";
        return;
    }
    void (^reply)(SPUUserUpdateChoice) = [g_driver.relaunchReply copy];
    g_driver.relaunchReply = nil;
    m_readyToRelaunch = false;
    m_waitingInstall = false;
    qInfo() << "SparkleUpdater: replyRelaunchInstall queued";
    MacPermissions::allowQuit("sparkle-relaunch");
    dispatch_async(dispatch_get_main_queue(), ^{
        if (!reply) {
            qWarning() << "SparkleUpdater: replyRelaunchInstall copy is nil";
            return;
        }
        qInfo() << "SparkleUpdater: replyRelaunchInstall invoke";
        reply(SPUUserUpdateChoiceInstall);
    });
#endif
}

void SparkleUpdater::replyRelaunchDismiss()
{
#ifdef SEENSHOT_HAS_SPARKLE
    if (!g_driver || !g_driver.relaunchReply) {
        return;
    }
    void (^reply)(SPUUserUpdateChoice) = [g_driver.relaunchReply copy];
    g_driver.relaunchReply = nil;
    qInfo() << "SparkleUpdater: replyRelaunchDismiss queued";
    dispatch_async(dispatch_get_main_queue(), ^{
        if (!reply) {
            qWarning() << "SparkleUpdater: replyRelaunchDismiss copy is nil";
            return;
        }
        qInfo() << "SparkleUpdater: replyRelaunchDismiss invoke";
        reply(SPUUserUpdateChoiceDismiss);
    });
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

void SparkleUpdater::handleShowInFocus()
{
    qInfo() << "SparkleUpdater: handleShowInFocus pending=" << m_offerPending
            << " download=" << m_downloadInFlight
            << " ready=" << m_readyToRelaunch
            << " installWhenFound=" << m_installWhenFound
            << " pendingAuto=" << m_pendingAutoInstall;
    if (m_pendingAutoInstall) {
        tryStartPendingAutoInstall();
        return;
    }
    presentOfferIfPossible();
}

// ─── Ariadne's Thread [AT-0669] ─────────────────────
// What: Hold auto-install until SeenShot is front, capture is idle, and the host .app is writable
// Why:  Auto-install must not raise a replace-app sheet over another program
// Date: 2026-09-10
// Related: [AT-0666] SparkleUpdater.mm:shouldAutoInstall, [AT-0667] MacPermissions.mm:ensureHostBundleWritable, [AT-0670] Application.cpp:eventFilter
// ─────────────────────────────────────────────────────
void SparkleUpdater::applicationBecameActive()
{
    qInfo() << "SparkleUpdater: applicationBecameActive pendingAuto=" << m_pendingAutoInstall
            << " capture=" << m_captureInProgress
            << " offer=" << m_offerPending;
    if (m_pendingAutoInstall) {
        tryStartPendingAutoInstall();
    }
}

void SparkleUpdater::tryStartPendingAutoInstall()
{
    const bool allowed = LocalStore::autoInstallAllowed();
    qInfo() << "SparkleUpdater: tryStartPendingAutoInstall pending=" << m_pendingAutoInstall
            << " capture=" << m_captureInProgress
            << " appActive=" << MacPermissions::isApplicationActive()
            << " writable=" << MacPermissions::hostBundleWritable()
            << " allowed=" << allowed
            << " download=" << m_downloadInFlight
            << " ready=" << m_readyToRelaunch;
    if (!m_pendingAutoInstall) {
        return;
    }
    if (!allowed) {
        qWarning() << "SparkleUpdater: tryStartPendingAutoInstall refused, keep Update card";
        m_pendingAutoInstall = false;
        presentOfferIfPossible();
        return;
    }
    if (m_downloadInFlight || m_readyToRelaunch) {
        qInfo() << "SparkleUpdater: tryStartPendingAutoInstall already installing";
        return;
    }
    if (m_captureInProgress) {
        qInfo() << "SparkleUpdater: tryStartPendingAutoInstall wait for capture";
        return;
    }
    if (!MacPermissions::isApplicationActive()) {
        qInfo() << "SparkleUpdater: tryStartPendingAutoInstall wait, another app is front";
        return;
    }
    if (!MacPermissions::hostBundleWritable()) {
        qWarning() << "SparkleUpdater: tryStartPendingAutoInstall not writable, keep Update card";
        m_pendingAutoInstall = false;
        presentOfferIfPossible();
        return;
    }
    if (!persistEditorNow()) {
        qWarning() << "SparkleUpdater: tryStartPendingAutoInstall persist failed, keep Update card";
        m_pendingAutoInstall = false;
        presentOfferIfPossible();
        return;
    }
    m_pendingAutoInstall = false;
    qInfo() << "SparkleUpdater: tryStartPendingAutoInstall start Sparkle install";
    replyFoundInstall();
}

void SparkleUpdater::handleCycleFinished(int updateCheck, int sparkleCode, const QString &detail)
{
    qInfo() << "SparkleUpdater: handleCycleFinished check=" << updateCheck
            << " code=" << sparkleCode
            << " chars=" << detail.size()
            << " rearm=" << m_rearmAfterCycle
            << " offer=" << m_offerPending
            << " version=" << m_version;
#ifdef SEENSHOT_HAS_SPARKLE
    if (sparkleCode == SUNoUpdateError) {
        m_rearmAfterCycle = false;
        qInfo() << "SparkleUpdater: cycle finished with no update, skip rearm";
        return;
    }
    if (!m_rearmAfterCycle) {
        return;
    }
    m_rearmAfterCycle = false;
    dispatch_async(dispatch_get_main_queue(), ^{
        if (SparkleUpdater *u = SparkleUpdater::instance()) {
            u->rearmBackgroundCheck();
        }
    });
#else
    (void)updateCheck;
    m_rearmAfterCycle = false;
#endif
}

void SparkleUpdater::requestSparkleInstall()
{
#ifdef SEENSHOT_HAS_SPARKLE
    if (!g_sparkle) {
        qWarning() << "SparkleUpdater: requestSparkleInstall missing updater";
        restoreOfferAfterFailure(QStringLiteral("UPDATE_FAILED"));
        return;
    }
    if (g_sparkle.sessionInProgress) {
        qInfo() << "SparkleUpdater: requestSparkleInstall wait, session in progress";
        if (m_editor) {
            m_editor->showUpdateProgress(0, 0, QStringLiteral("Checking…"));
        }
        return;
    }
    qInfo() << "SparkleUpdater: checkForUpdates retry canCheck="
            << static_cast<bool>(g_sparkle.canCheckForUpdates);
    if (m_editor) {
        m_editor->showUpdateProgress(0, 0, QStringLiteral("Checking…"));
    }
    MacPermissions::activateApp();
    [g_sparkle checkForUpdates];
#else
    qWarning() << "SparkleUpdater: requestSparkleInstall Sparkle missing";
    restoreOfferAfterFailure(QStringLiteral("UPDATE_FAILED"));
#endif
}

void SparkleUpdater::restoreOfferAfterFailure(const QString &catalogCode)
{
    qWarning() << "SparkleUpdater: restoreOfferAfterFailure" << catalogCode
               << " editor=" << (m_editor != nullptr)
               << " version=" << m_version;
    clearPersistIfEditorOpen();
    if (!m_editor) {
        qInfo() << "SparkleUpdater: keep offer for next annotate, no editor";
        return;
    }
    m_editor->showUpdateError(catalogCode);
    m_editor->resetUpdateOffer();
}

void SparkleUpdater::rearmBackgroundCheck()
{
#ifdef SEENSHOT_HAS_SPARKLE
    if (!g_sparkle) {
        qWarning() << "SparkleUpdater: rearm missing updater";
        return;
    }
    if (g_driver && g_driver.foundReply) {
        qInfo() << "SparkleUpdater: rearm skip, found reply exists";
        presentOfferIfPossible();
        return;
    }
    if (g_sparkle.sessionInProgress) {
        qInfo() << "SparkleUpdater: rearm skip, session in progress";
        return;
    }
    qInfo() << "SparkleUpdater: checkForUpdatesInBackground rearm version=" << m_version;
    [g_sparkle checkForUpdatesInBackground];
#endif
}

void SparkleUpdater::logHostInstallPermissions()
{
    NSString *path = [[NSBundle mainBundle] bundlePath];
    NSFileManager *fm = [NSFileManager defaultManager];
    const BOOL writable = [fm isWritableFileAtPath:path];
    NSError *attrError = nil;
    NSDictionary *attrs = [fm attributesOfItemAtPath:path error:&attrError];
    const unsigned long posix = attrs[NSFilePosixPermissions]
        ? [attrs[NSFilePosixPermissions] unsignedLongValue]
        : 0;
    qInfo() << "SparkleUpdater: host path=" << QString::fromNSString(path)
            << " writable=" << static_cast<bool>(writable)
            << " owner=" << QString::fromNSString(attrs[NSFileOwnerAccountName] ? attrs[NSFileOwnerAccountName] : @"")
            << " posix=" << static_cast<qulonglong>(posix)
            << " attrError=" << (attrError ? QString::fromNSString(attrError.localizedDescription) : QString());
}

void SparkleUpdater::clearPersistIfEditorOpen()
{
    if (!m_editor) {
        qInfo() << "SparkleUpdater: keep persist, editor closed";
        return;
    }
    LocalStore::clearEditorSession();
    qInfo() << "SparkleUpdater: cleared persist because editor is still open";
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
