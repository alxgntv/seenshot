#pragma once

#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QKeySequence>
#include <QString>

// ─── Ariadne's Thread [AT-0021] ─────────────────────
// What: Local settings and temp capture paths
// Why:  Drafts go to NSTemporaryDirectory equivalent
// Date: 2026-08-25
// Related: client/src/local/LocalStore.cpp
// ─────────────────────────────────────────────────────
class LocalStore {
public:
    static QString settingsPath();
    static QString tempCapturePath();
    static void removeTempCapture(const QString &path);
    static bool firstRunCompleted();
    static void setFirstRunCompleted();
    // ─── Ariadne's Thread [AT-0301] ─────────────────────
    // What: Gate first-run on onboardingVersion, not firstRunCompleted
    // Why:  0.1.4 already set firstRunCompleted; everyone must still pass the 6-step wizard
    // Date: 2026-08-28
    // Related: [AT-0303] Application.cpp:start, [AT-0302] FirstRunWizard.cpp
    // ─────────────────────────────────────────────────────
    static int onboardingVersion();
    static bool onboardingCompleted();
    static void setOnboardingCompleted();
    static void resetOnboarding();
    // ─── Ariadne's Thread [AT-0175] ─────────────────────
    // What: Persist that this Mac already registered SeenShot with Screen Recording TCC
    // Why:  CGRequestScreenCaptureAccess every launch showed Open Settings while another signed copy was already on
    // Date: 2026-08-26
    // Related: [AT-0176] Application.cpp:ensureScreenRecording, [AT-0178] MacPermissions.mm:probeScreenRecording
    // ─────────────────────────────────────────────────────
    static bool screenRecordingRegistered();
    static void setScreenRecordingRegistered();
    static QString hotkeySpec();
    static void setHotkeySpec(const QString &spec);
    static QString fullScreenHotkeySpec();
    static void setFullScreenHotkeySpec(const QString &spec);
    static QKeySequence keySequenceFromSpec(const QString &spec);
    static QString specFromKeySequence(const QKeySequence &sequence);
    static QString nativeHotkeyLabel(const QString &spec);
    static QString pendingSignInEmail();
    static void setPendingSignInEmail(const QString &email);
    static void clearPendingSignInEmail();
    // ─── Ariadne's Thread [AT-0390] ─────────────────────
    // What: Persist Blur Automatic and per-type auto-redact checkboxes
    // Why:  New screenshots must reuse the last Automatic type selection
    // Date: 2026-09-03
    // Related: [AT-0396] app→AnnotateWindow.cpp:ensureBlurSidebar, [AT-0391] app→SensitiveRedact.h
    // ─────────────────────────────────────────────────────
    static bool blurAutomatic();
    static void setBlurAutomatic(bool on);
    static bool blurAutoFaces();
    static void setBlurAutoFaces(bool on);
    static bool blurAutoPhones();
    static void setBlurAutoPhones(bool on);
    static bool blurAutoEmails();
    static void setBlurAutoEmails(bool on);
    static bool blurAutoApiKeys();
    static void setBlurAutoApiKeys(bool on);
    // ─── Ariadne's Thread [AT-0411] ─────────────────────
    // What: Persist last Text Size and Outline for new text blocks
    // Why:  Each new text reset to 18 / no outline
    // Date: 2026-09-04
    // Related: [AT-0334] app→AnnotateWindow.cpp:onTextSizeChanged, [AT-0344] app→AnnotateWindow.cpp:onTextOutlineToggled
    // ─────────────────────────────────────────────────────
    static int textSize();
    static void setTextSize(int size);
    static bool textOutline();
    static void setTextOutline(bool on);
    static bool hasEditorSession();
    static bool writeEditorSession(const QJsonObject &json, const QImage &shot,
                                   const QHash<QString, QImage> &assets, QString *errorCode);
    static bool readEditorSession(QJsonObject *json, QImage *shot, QHash<QString, QImage> *assets,
                                  QString *errorCode);
    static void clearEditorSession();
};
