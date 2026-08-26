#pragma once

#include <QObject>
#include <QPointer>
#include <QString>

class AnnotateWindow;

// ─── Ariadne's Thread [AT-0092] ─────────────────────
// What: Sparkle SPUUpdater with a custom SPUUserDriver
// Why:  PRD-05 — one updater, offer on AnnotateWindow, no Sparkle alerts
// Date: 2026-08-25
// Related: [AT-0036] SparkleUpdater.mm, docs/PRD-05-auto-update.md
// ─────────────────────────────────────────────────────
class SparkleUpdater : public QObject {
    Q_OBJECT
public:
    static SparkleUpdater *instance();
    static void start();

    void attachEditor(AnnotateWindow *editor);
    void editorWillClose(AnnotateWindow *editor);
    void setCaptureInProgress(bool capturing);
    void userChoseUpdate();
    void retryPendingInstall();
    void persistBeforeQuit();
    bool isDownloadInFlight() const;

    void handlePermission();
    void handleUpdateFound(const QString &version, bool informationOnly, bool alreadyDownloaded);
    void handleNoUpdate();
    void handleUpdaterError(const QString &detail);
    void handleDownloadStarted();
    void handleDownloadExpected(qint64 bytes);
    void handleDownloadReceived(qint64 bytes);
    void handleExtractStarted();
    void handleExtractProgress(double progress);
    void handleReadyToRelaunch();
    void handleInstalling();
    void handleDismissed();
    void replyFoundInstall();
    void replyFoundDismiss();
    void replyRelaunchInstall();
    void replyRelaunchDismiss();

private:
    explicit SparkleUpdater(QObject *parent = nullptr);
    void startUpdater();
    void presentOfferIfPossible();
    void finishInstallWhenSafe();
    bool persistEditorNow();

    QPointer<AnnotateWindow> m_editor;
    QString m_version;
    qint64 m_expectedBytes = 0;
    qint64 m_receivedBytes = 0;
    bool m_captureInProgress = false;
    bool m_offerPending = false;
    bool m_downloadInFlight = false;
    bool m_readyToRelaunch = false;
    bool m_waitingInstall = false;
};
